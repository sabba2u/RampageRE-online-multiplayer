//! Hosting with my own server: the built-in lobby, the router opened, the game hosting in it, then the code and the
//! group links to send. A router that can't be opened says so (only this home can join) and offers Steam.
use super::look::{self, GREEN, YELLOW};
use super::{App, Card};
use crate::game::{self, Start};
use crate::lobby::addr::is_loopback;
use crate::lobby::games::Games;
use crate::lobby::http::{LobbyServer, StartError};
use crate::upnp::{self, Reach};
use eframe::egui::{self, Align2, RichText, Vec2};
use std::process::Child;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver};
use std::sync::Arc;
use std::time::Instant;
use upnp::Opened;

pub const LOBBY_PORT: u16 = 19980;
/// the game's own port when hosting (src/net.h NET_PORT)
pub const GAME_PORT: u16 = 27960;
pub const PORT_IN_USE: &str = "Another RampageRE server is already running on this PC.";
pub const ROUTER_TITLE: &str = "Friends outside your home can't connect to you on this network.";
pub const ROUTER_TEXT: &str = "Friends on the same home network can still join your room.";
pub const FIREWALL_TEXT: &str = "Windows will ask whether RampageRE may use the network: click Allow.";
pub const CLOSE_TEXT: &str = "Your friends won't be able to join.";
pub const GAME_CLOSED_EARLY: &str = "The game closed before your friends could see it: hosting stopped.";
pub const GAME_CLOSED: &str = "The game closed: hosting stopped.";

/// What to send friends: the internet one when the router let us in, and the home network's always (a friend in the
/// same house often can't reach the house's own public address).
pub fn invite_links(code: &str, reach: &Reach, lan_ip: Option<&str>) -> Vec<(String, String)> {
    let mut out = Vec::new();
    if let Reach::Internet { public_ip } = reach {
        out.push(("Friends anywhere".to_string(), format!("rampage://join/{code}@{public_ip}")));
    }
    if let Some(lan) = lan_ip {
        out.push(("Same Wi-Fi".to_string(), format!("rampage://join/{code}@{lan}")));
    }
    out
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
    pub router_popup: bool,
    pub confirm_close: bool,
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
    fn open_router(&mut self) {
        let (tx, rx) = channel();
        let cancelled = self.cancelled.clone();
        std::thread::spawn(move || {
            let o = upnp::open_lobby_port(LOBBY_PORT);
            if cancelled.load(Ordering::Relaxed) {
                if o.forwarded {
                    upnp::close_lobby_port(LOBBY_PORT);
                }
                return;
            }
            let _ = tx.send(o);
        });
        self.reach_rx = Some(rx);
        self.opened_at = Some(Instant::now());
    }
}

impl App {
    pub fn hosting(&self) -> bool { self.host.lobby.is_some() }

    /// "With my own server": the firewall note once, then the lobby, the router, the game.
    pub fn host_own(&mut self) {
        let game = match &self.game {
            Ok(g) => g.clone(),
            Err(e) => return self.say(&e.clone(), true),
        };
        if !self.settings.firewall_note_shown {
            self.card = Some(Card::Firewall);
            return;
        }
        let lobby = match LobbyServer::start(LOBBY_PORT, None) {
            Ok(l) => l,
            Err(StartError::PortInUse) => return self.say(PORT_IN_USE, true),
            Err(StartError::Other(e)) => return self.say(&format!("The server couldn't start: {e}"), true),
        };
        let child = match game::start(&game, &Start::Host { lobby: format!("http://127.0.0.1:{LOBBY_PORT}") }) {
            Ok(c) => c,
            Err(e) => return self.say(&format!("Couldn't start RampageRE: {e}"), true),
        };
        lobby.games.lock().unwrap().set_banned(self.settings.banned_ips());
        self.host = Hosting { lobby: Some(lobby), game: Some(child), lan: upnp::lan_ip(), ..Default::default() };
        self.host.open_router();
        self.say("Starting your game and server...", false);
    }

    /// Stop hosting (the button): the hosting game closed too (as its X would, on a thread: it may take a moment to
    /// save), then the room server stopped and the router's forward removed.
    pub fn stop_hosting_and_game(&mut self) {
        if let Some(mut c) = self.host.game.take() {
            std::thread::spawn(move || crate::game::close_gently(&mut c, std::time::Duration::from_secs(4)));
        }
        self.stop_hosting();
        self.say("Stopped hosting: your game is closing.", false);
    }

    /// Stops the lobby and removes the router's forward, on a thread (the router can take seconds). The game itself
    /// keeps running (the game closed by itself, or the launcher closing).
    pub fn stop_hosting(&mut self) {
        self.host.cancelled.store(true, Ordering::Relaxed);
        if let Some(l) = self.host.lobby.take() {
            l.stop();
        }
        if self.host.forwarded {
            std::thread::spawn(|| upnp::close_lobby_port(LOBBY_PORT));
        }
        self.host = Hosting::default();
    }

    /// Like stop_hosting, but waits for the router (the launcher is closing: a thread would die with it).
    pub fn stop_hosting_now(&mut self) {
        let forwarded = self.host.forwarded;
        self.host.forwarded = false;
        self.stop_hosting();
        if forwarded {
            upnp::close_lobby_port(LOBBY_PORT);
        }
    }

    pub fn poll_hosting(&mut self) {
        if !self.hosting() {
            return;
        }
        if let Some(why) = self.host.check_game() {
            self.stop_hosting();
            return self.say(why, true);
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
        if self.host.code.is_none() {
            if let Some(l) = &self.host.lobby {
                self.host.code = find_my_code(&l.games.lock().unwrap(), GAME_PORT);
                if self.host.code.is_some() {
                    self.note = None;
                }
            }
        }
    }

    /// While hosting: the code big, the links to send, Stop.
    pub fn hosting_box(&mut self, ui: &mut egui::Ui) {
        let Some(code) = self.host.code.clone() else {
            ui.label(RichText::new("Starting your game and server...").size(16.0));
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
                for (label, link) in invite_links(&code, reach, self.host.lan.as_deref()) {
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
            });
    }
}
