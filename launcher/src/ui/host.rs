//! Hosting with my own server: the built-in lobby, the router opened, the game hosting in it, then the code and the
//! group links to send. A router that can't be opened says so (only this home can join) and offers Steam.
use super::look::{self, GREEN, YELLOW};
use super::{App, Card};
use crate::game::{self, Start};
use crate::lobby::addr::is_loopback;
use crate::lobby::games::Games;
use crate::lobby::http::{LobbyServer, StartError};
use crate::settings::Settings;
use crate::upnp::{self, Reach};
use eframe::egui::{self, Align2, RichText, Vec2};
use std::process::Child;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver};
use std::sync::Arc;
use std::time::Instant;
use upnp::Opened;

pub const LOBBY_PORT: u16 = 19980;
pub const SERVER_GONE: &str = "The server on this PC stopped answering (was its window closed?). Start it again, or Start Internet Server.";
pub const EXTERNAL_NOTE: &str = "The Players list (Mute, Kick, Ban) is only there with the launcher's own server.";
pub const PORT_IN_USE: &str = "Another program is using this PC's hosting port: add another port in Settings -> Hosting (like :19990).";
pub const ROUTER_TITLE: &str = "Friends outside your home can't connect to you on this network.";
pub const ROUTER_TEXT: &str = "Friends on the same home network can still join your room.";
pub const BY_HAND_TEXT: &str = "Your router didn't open itself. If you forwarded the port (19980 unless Settings says another, TCP and UDP) to this PC yourself, friends outside can come in anyway (Settings: the address they use, like a no-ip name).";
pub const FIREWALL_TEXT: &str = "Windows will ask whether RampageRE may use the network: click Allow.";
pub const CLOSE_TEXT: &str = "Your friends won't be able to join.";
pub const GAME_CLOSED_EARLY: &str = "The game closed before your friends could see it. Your server is still running.";
pub const GAME_CLOSED: &str = "The game closed. Your server is still running: launch it again, or stop the server.";

/// What to send friends: the internet one when the router let us in, and the home network's always (a friend in the
/// same house often can't reach the house's own public address).
/// `port`: hosting's (the lobby's), added to the home network's address when it isn't the usual one; the internet
/// address carries its own (by_hand).
pub fn invite_links(code: &str, reach: &Reach, lan_ip: Option<&str>, port: u16) -> Vec<(String, String)> {
    let mut out = Vec::new();
    if let Reach::Internet { public_ip } = reach {
        out.push(("Friends anywhere".to_string(), format!("rampage://join/{code}@{public_ip}")));
    }
    if let Some(lan) = lan_ip {
        out.push(("Same Wi-Fi".to_string(), format!("rampage://join/{code}@{}", with_port(lan, port))));
    }
    out
}

/// "host", or "host:port" when the port isn't the usual one.
pub fn with_port(host: &str, port: u16) -> String {
    if port == LOBBY_PORT { host.to_string() } else { format!("{host}:{port}") }
}

/// A RampageRE room server answering on this PC's `port` already (the Node.js one, rampagelobby\start.bat, or another
/// launcher's): its /health says so.
pub fn lobby_running_here(port: u16) -> bool {
    let agent = ureq::AgentBuilder::new().timeout(std::time::Duration::from_millis(800)).build();
    agent.get(&format!("http://127.0.0.1:{port}/health")).call().ok().and_then(|r| r.into_string().ok())
        .map_or(false, |b| b.trim() == "rampage_lobby")
}

/// Hosting in a server already running: our game's room is one listed since the launch (not in `before`), on our
/// port; None until it shows.
pub fn new_code_on(rooms: &[crate::ui::join::Room], before: &[String], port: u16) -> Option<String> {
    let tail = format!(":{port}");
    rooms.iter().find(|r| !before.contains(&r.code) && r.join.ends_with(&tail)).map(|r| r.code.clone())
}

/// The code of this PC's own hosted game: registered from loopback with the game's port.
pub fn find_my_code(games: &Games, game_port: u16) -> Option<String> {
    games.list().into_iter().find(|g| is_loopback(&g.source) && g.port == game_port).map(|g| g.code.clone())
}

#[derive(Default)]
pub struct Hosting {
    pub lobby: Option<LobbyServer>,
    /// the hosting game: when it closes, hosting ends
    pub game: Option<Child>,
    pub reach: Option<Reach>,
    reach_rx: Option<Receiver<Opened>>,
    /// the router made the forward: removed on stop, renewed while hosting
    forwarded: bool,
    opened_at: Option<Instant>,
    /// set on stop: a router answer still on its way removes its forward at once
    cancelled: Arc<AtomicBool>,
    pub code: Option<String>,
    pub lan: Option<String>,
    /// the port hosting uses: the lobby's TCP and the game's UDP (Settings' address; 19980 unless one is added)
    pub port: u16,
    pub router_popup: bool,
    pub confirm_close: bool,
    /// hosting in a room server that was already running on this PC (Node.js's, or another launcher's): not ours to
    /// stop; its rooms read over HTTP
    pub external: bool,
    /// its rooms when our game was launched (our room is the new one)
    before: Vec<String>,
    ext_rx: Option<Receiver<Result<Vec<crate::ui::join::Room>, String>>>,
    ext_at: Option<Instant>,
    /// its room list not answering, times running: 3 and it is gone
    ext_fails: u8,
}

impl Hosting {
    /// The game closed (or crashed): what to say. None while it runs.
    pub fn check_game(&mut self) -> Option<&'static str> {
        let exited = self.game.as_mut().map_or(false, |c| !matches!(c.try_wait(), Ok(None)));
        if !exited {
            return None;
        }
        self.game = None;
        Some(if self.code.is_some() { GAME_CLOSED } else { GAME_CLOSED_EARLY })
    }

    /// Closes the hosting game (switching to Steam: one game at a time).
    pub fn end_game(&mut self) {
        if let Some(mut c) = self.game.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }

    /// Asks the router (again), on a thread; a stop meanwhile removes what it made.
    pub fn open_router(&mut self) {
        let (tx, rx) = channel();
        let cancelled = self.cancelled.clone();
        let settings = Settings::load();
        let port = self.port;
        std::thread::spawn(move || {
            let o = by_hand(upnp::open_lobby_port(port), &settings, crate::ui::join::public_ip);
            if cancelled.load(Ordering::Relaxed) {
                if o.forwarded {
                    upnp::close_lobby_port(port);
                }
                return;
            }
            let _ = tx.send(o);
        });
        self.reach_rx = Some(rx);
        self.opened_at = Some(Instant::now());
    }
}

/// Settings' address applied to the router's answer: friends anywhere get the name set (sabba2u.no-ip.org) in place
/// of the router's address, with the port when it isn't the usual one. The router's opening failed but the host
/// forwarded the port by hand (Settings): friends outside can still come, to that name or else the address the
/// internet sees (`lookup`). Anything else: as the router said.
pub fn by_hand(o: upnp::Opened, s: &Settings, lookup: impl FnOnce() -> Result<String, String>) -> upnp::Opened {
    let (name, port) = s.host_address().unwrap_or((String::new(), LOBBY_PORT));
    let addr = match &o.reach {
        Reach::Internet { public_ip } => Some(if name.is_empty() { public_ip.clone() } else { name }),
        _ if s.forwarded_by_hand => if name.is_empty() { lookup().ok() } else { Some(name) },
        _ => None,
    };
    match addr {
        Some(a) => upnp::Opened { reach: Reach::Internet { public_ip: with_port(&a, port) }, forwarded: o.forwarded },
        None => o,
    }
}

impl App {
    pub fn hosting(&self) -> bool { self.host.lobby.is_some() || self.host.external }

    /// "Start Internet Server" (step 1): the firewall note once, then the lobby and the router. The game is step 2
    /// (launch_host_game), once the server runs.
    pub fn host_own(&mut self) {
        if let Err(e) = &self.game {
            return self.say_on(1, &e.clone(), true);
        }
        if !self.settings.firewall_note_shown {
            self.card = Some(Card::Firewall);
            return;
        }
        let port = self.settings.host_port();
        let lobby = match LobbyServer::start(port, None) {
            Ok(l) => l,
            Err(StartError::PortInUse) if lobby_running_here(port) => return self.use_running_server(port),
            Err(StartError::PortInUse) => return self.say_on(1, PORT_IN_USE, true),
            Err(StartError::Other(e)) => return self.say_on(1, &format!("The server couldn't start: {e}"), true),
        };
        lobby.games.lock().unwrap().set_banned(self.settings.banned_ips());
        self.host = Hosting { lobby: Some(lobby), lan: upnp::lan_ip(), port, ..Default::default() };
        self.host.open_router();
        self.say_on(1, "Your server is running: now launch the game as host.", false);
    }

    /// Step 1 when a room server already runs on this PC (rampagelobby\start.bat's Node.js one, or another launcher's): the
    /// game will host in it; the router opened for it all the same.
    pub fn use_running_server(&mut self, port: u16) {
        if let Err(e) = &self.game {
            return self.say_on(1, &e.clone(), true);
        }
        self.host = Hosting { external: true, lan: upnp::lan_ip(), port, ..Default::default() };
        self.host.open_router();
        self.say_on(1, "Using the server already running on this PC: now launch the game as host.", false);
    }

    /// "Launch Game as Host" (step 2): the game, hosting in this PC's server, the room as big as Players says.
    pub fn launch_host_game(&mut self) {
        let Ok(game) = self.game.clone() else { return };
        let port = self.host.port;
        if self.host.external {
            self.host.before = crate::ui::join::fetch_rooms(&format!("http://127.0.0.1:{port}"))
                .map(|r| r.into_iter().map(|r| r.code).collect()).unwrap_or_default();
        }
        let start = Start::Host { lobby: format!("http://127.0.0.1:{port}"), port, players: self.settings.internet_players() };
        match game::start(&game, &start) {
            Ok(c) => {
                self.host.game = Some(c);
                self.host.code = None;
                self.say_on(1, "Starting your game...", false);
            }
            Err(e) => self.say_on(1, &format!("Couldn't start RampageRE: {e}"), true),
        }
    }

    /// Stop hosting (the button): the hosting game closed too (as its X would, on a thread: it may take a moment to
    /// save), then the room server stopped and the router's forward removed.
    pub fn stop_hosting_and_game(&mut self) {
        if let Some(mut c) = self.host.game.take() {
            std::thread::spawn(move || crate::game::close_gently(&mut c, std::time::Duration::from_secs(4)));
        }
        self.stop_hosting();
        self.say_on(1, "Stopped hosting: your game is closing.", false);
    }

    /// Stops the lobby and removes the router's forward, on a thread (the router can take seconds). The game itself
    /// keeps running (the game closed by itself, or the launcher closing).
    pub fn stop_hosting(&mut self) {
        self.host.cancelled.store(true, Ordering::Relaxed);
        if let Some(l) = self.host.lobby.take() {
            l.stop();
        }
        if self.host.forwarded {
            let port = self.host.port;
            std::thread::spawn(move || upnp::close_lobby_port(port));
        }
        self.host = Hosting::default();
    }

    /// Like stop_hosting, but waits for the router (the launcher is closing: a thread would die with it).
    pub fn stop_hosting_now(&mut self) {
        let (forwarded, port) = (self.host.forwarded, self.host.port);
        self.host.forwarded = false;
        self.stop_hosting();
        if forwarded {
            upnp::close_lobby_port(port);
        }
    }

    pub fn poll_hosting(&mut self) {
        if !self.hosting() {
            return;
        }
        if let Some(why) = self.host.check_game() {
            // (the server stays: the game launched again, or the server stopped, by the host)
            self.host.code = None;
            self.say_on(1, why, true);
        }
        if let Some(rx) = &self.host.reach_rx {
            if let Ok(o) = rx.try_recv() {
                let renewal = self.host.reach.is_some();
                self.host.forwarded |= o.forwarded;
                if let (Reach::Internet { public_ip }, Some(l)) = (&o.reach, &self.host.lobby) {
                    l.set_public_address(public_ip);
                }
                self.host.router_popup |= !renewal && matches!(o.reach, Reach::HomeOnly { .. });
                if !renewal || matches!(o.reach, Reach::Internet { .. }) {
                    self.host.reach = Some(o.reach);
                }
                self.host.reach_rx = None;
            }
        }
        let due = self.host.opened_at.map_or(false, |t| t.elapsed().as_secs() >= upnp::RENEW_S);
        if self.host.forwarded && self.host.reach_rx.is_none() && due {
            self.host.open_router();
        }
        if self.host.external {
            self.poll_running_server();
        }
        if self.host.code.is_none() && self.host.game.is_some() {
            if let Some(l) = &self.host.lobby {
                self.host.code = find_my_code(&l.games.lock().unwrap(), self.host.port);
                if self.host.code.is_some() {
                    self.note = None;
                }
            }
        }
    }

    /// A server already running on this PC: its rooms every second (our room's code), and whether it is still there.
    fn poll_running_server(&mut self) {
        if let Some(rx) = &self.host.ext_rx {
            match rx.try_recv() {
                Ok(Ok(rooms)) => {
                    self.host.ext_rx = None;
                    self.host.ext_fails = 0;
                    if self.host.code.is_none() && self.host.game.is_some() {
                        self.host.code = new_code_on(&rooms, &self.host.before, self.host.port);
                        if self.host.code.is_some() {
                            self.note = None;
                        }
                    }
                }
                Ok(Err(_)) => {
                    self.host.ext_rx = None;
                    self.host.ext_fails += 1;
                    if self.host.ext_fails >= 3 {
                        self.stop_hosting();
                        return self.say_on(1, SERVER_GONE, true);
                    }
                }
                Err(_) => return,
            }
        }
        if self.host.ext_at.map_or(true, |t| t.elapsed().as_millis() >= 1000) {
            let url = format!("http://127.0.0.1:{}", self.host.port);
            let (tx, rx) = channel();
            std::thread::spawn(move || { let _ = tx.send(crate::ui::join::fetch_rooms(&url)); });
            self.host.ext_rx = Some(rx);
            self.host.ext_at = Some(Instant::now());
        }
    }

    /// The server without its game yet (or the game closed): where friends connect, Players, Launch Game as Host.
    fn server_box(&mut self, ui: &mut egui::Ui) {
        look::section(ui, if self.host.external { "Using the server already running on this PC" } else { "Your server is running" });
        match &self.host.reach {
            None => {
                ui.horizontal(|ui| {
                    ui.spinner();
                    ui.label("Opening your router for friends...");
                });
            }
            Some(Reach::Internet { public_ip }) => {
                ui.label(RichText::new(format!("Friends anywhere Connect to: {public_ip}")).strong());
            }
            Some(Reach::HomeOnly { .. }) => {
                ui.label(RichText::new("Only your home network can connect (the router didn't open).").color(YELLOW));
            }
        }
        if let Some(lan) = &self.host.lan {
            ui.label(RichText::new(format!("Same Wi-Fi: {}", with_port(lan, self.host.port))).color(look::DIM_TEXT));
        }
        ui.add_space(4.0);
        if look::big(ui, "Launch Game as Host", true).on_hover_text("Step 2: your game, hosting a room in this server").clicked() {
            self.launch_host_game();
        }
        self.room_size_choice(ui, 1);
        self.note_line(ui, 1);
        ui.separator();
        if self.host.external {
            ui.label(RichText::new(EXTERNAL_NOTE).small().color(look::DIM_TEXT));
            if ui.button("Stop using it").on_hover_text("The server keeps running: close its own window to stop it").clicked() {
                self.stop_hosting();
                self.say_on(1, "Not using that server any more (it is still running).", false);
            }
        } else if ui.button("Stop server").clicked() {
            self.stop_hosting();
            self.say_on(1, "Server stopped.", false);
        }
    }

    /// While hosting: the code big, the links to send, Stop.
    pub fn hosting_box(&mut self, ui: &mut egui::Ui) {
        if self.host.game.is_none() {
            return self.server_box(ui);
        }
        let Some(code) = self.host.code.clone() else {
            ui.label(RichText::new("Starting your game...").size(16.0));
            ui.spinner();
            if ui.button("Stop hosting").on_hover_text("Closes your game too").clicked() {
                self.stop_hosting_and_game();
            }
            return;
        };
        ui.label(RichText::new("Your room code").color(look::DIM_TEXT));
        ui.horizontal(|ui| {
            ui.label(RichText::new(&code).size(40.0).strong().color(YELLOW).monospace());
            if ui.button("Copy").clicked() {
                ui.ctx().copy_text(code.clone());
            }
        });
        match &self.host.reach {
            None => {
                ui.horizontal(|ui| {
                    ui.spinner();
                    ui.label("Opening your router for friends...");
                });
            }
            Some(reach) => {
                ui.label(RichText::new("Send your friends a link:").color(look::DIM_TEXT));
                for (label, link) in invite_links(&code, reach, self.host.lan.as_deref(), self.host.port) {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(format!("{label}:")).strong());
                        ui.label(RichText::new(&link).monospace().small());
                        if ui.small_button("Copy").clicked() {
                            ui.ctx().copy_text(link.clone());
                        }
                    });
                }
                if matches!(reach, Reach::Internet { .. }) {
                    ui.label(RichText::new("Friends who already connected to your server can just type the room code.").small().color(GREEN));
                }
            }
        }
        ui.separator();
        if self.host.external {
            ui.label(RichText::new(EXTERNAL_NOTE).small().color(look::DIM_TEXT));
        }
        self.players_box(ui);
        ui.separator();
        if ui.button("Stop hosting").on_hover_text("Closes your game too").clicked() {
            self.stop_hosting_and_game();
        }
    }

    /// The router wouldn't open: only this home can join. Steam instead, or keep going for the home network.
    pub fn router_popup(&mut self, ctx: &egui::Context) {
        if !self.host.router_popup {
            return;
        }
        egui::Window::new("Only your home network").anchor(Align2::CENTER_CENTER, Vec2::ZERO).collapsible(false)
            .resizable(false).show(ctx, |ui| {
                ui.set_max_width(380.0);
                ui.label(RichText::new(ROUTER_TITLE).size(17.0).strong());
                ui.label(ROUTER_TEXT);
                ui.add_space(8.0);
                ui.horizontal(|ui| {
                    if ui.button(RichText::new("Host on Steam instead").strong()).clicked() {
                        self.host.end_game();
                        self.stop_hosting();
                        self.steam_host();
                    }
                    if ui.button("Keep hosting for my home network").clicked() {
                        self.host.router_popup = false;
                    }
                });
                ui.add_space(6.0);
                ui.label(RichText::new(BY_HAND_TEXT).small().color(look::DIM_TEXT));
                if ui.button("I forwarded the ports myself").clicked() {
                    self.settings = Settings::update(|s| s.forwarded_by_hand = true);
                    self.host.router_popup = false;
                    self.host.open_router();
                }
            });
    }
}
