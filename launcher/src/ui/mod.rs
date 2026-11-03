//! The launcher's window: Play, Host a game (my own server), Steam (host / join), Join by code, and Settings (the
//! join links, group servers, tips). Anything slow (a version check, the router, Steam's checks) runs on a thread; the window keeps drawing.
pub mod join;
#[cfg(test)]
mod join_tests;
mod join_box;
mod host;
#[cfg(test)]
mod host_tests;
mod guest;
mod links_button;
mod players;
pub mod look;
mod steam_cards;

use crate::game::{self, Start};
use crate::links::LinkStatus;
use crate::settings::Settings;
use crate::steam::SteamProblem;
use steam_cards::SteamAction;
use eframe::egui::{self, RichText};
use look::{DIM_TEXT, GREEN, RED};
use std::path::PathBuf;
use std::sync::mpsc::{channel, Receiver};

pub struct App {
    pub game: Result<PathBuf, String>,
    /// the game's build id ("" when there's no game)
    pub build: String,
    pub settings: Settings,
    pub links: LinkStatus,
    code: String,
    /// the last thing to tell the player, and whether it is a problem
    note: Option<(String, bool)>,
    /// the tab the note belongs to
    note_tab: u8,
    /// the Steam tab's look (Steam running, Spacewar in the library) and when: looked again every few seconds
    steam_seen: Option<(bool, bool, std::time::Instant)>,
    /// a join under way: its outcome (the server to remember once it went through)
    busy: Option<Receiver<Result<(Option<String>, std::process::Child), String>>>,
    /// the room being joined (its server, its code): the guest view once the game started
    joining: Option<(String, String)>,
    /// in a room (joined by its code)
    pub guest: Option<guest::Guest>,
    /// when the settings were last read (a clicked link saves from another process)
    settings_read: std::time::Instant,
    pub host: host::Hosting,
    /// a card showing in the middle of the window
    pub card: Option<Card>,
    /// Steam's checks under way, for which button
    pub steam_check: Option<(SteamAction, Receiver<Result<(), SteamProblem>>)>,
    pub steam_problem: Option<SteamProblem>,
    /// the Settings window showing
    pub settings_open: bool,
    pub join_box: join_box::JoinBox,
    /// this PC's address on its network, looked up once (Host Local's hint)
    lan_ip: Option<String>,
    /// its address on the internet, looked up when asked (Host Local's "Show my internet address")
    public_ip: Option<Result<String, String>>,
    public_rx: Option<std::sync::mpsc::Receiver<Result<String, String>>>,
    /// Join Local (LAN): the host's address typed (empty: look for games on the network)
    lan_addr: String,
    /// the look for games under way
    lan_rx: Option<Receiver<Vec<crate::lan::Found>>>,
    /// whether other PCs can reach this PC's game (firewall.rs): looked at once, again after the fix
    fw_status: Option<crate::firewall::Status>,
    /// a look or the fix under way (the fix: true)
    fw_rx: Option<(Receiver<Result<crate::firewall::Status, String>>, bool)>,
    /// the fix's outcome, shown under the button (not in the Join a Room note): the text, whether a problem
    fw_note: Option<(String, bool)>,
}

#[derive(Clone)]
pub enum Card {
    Firewall,
    /// what Spacewar is; then that Steam button's checks (None: opened from the "?")
    Spacewar(Option<SteamAction>),
    ShiftTab,
    JoinGame,
}

impl App {
    pub fn new(cc: &eframe::CreationContext) -> App {
        egui_extras::install_image_loaders(&cc.egui_ctx);
        look::style(&cc.egui_ctx);
        let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.to_path_buf())).unwrap_or_default();
        let game = game::find_game(&dir);
        let build = game.as_ref().ok().and_then(|g| game::build_id(g).ok()).unwrap_or_default();
        let mut app = App { game, build, settings: Settings::load(), links: LinkStatus::NotSet, code: String::new(), note: None, note_tab: 0, steam_seen: None,
                            busy: None, joining: None, guest: None, settings_read: std::time::Instant::now(), host: host::Hosting::default(), card: None,
                            steam_check: None, steam_problem: None, settings_open: false,
                            join_box: join_box::JoinBox::default(), lan_ip: crate::upnp::lan_ip(),
                            public_ip: None, public_rx: None,
                            lan_addr: String::new(), lan_rx: None, fw_status: None, fw_rx: None, fw_note: None };
        app.refresh_links();
        app
    }

    /// A message under the tab being looked at.
    pub fn say(&mut self, text: &str, problem: bool) { self.say_on(self.settings.tab.min(2), text, problem) }
    /// A message under its own tab (0 LAN, 1 Internet, 2 Steam): each tab works on its own, its messages with it.
    pub fn say_on(&mut self, tab: u8, text: &str, problem: bool) {
        self.note = Some((text.to_string(), problem));
        self.note_tab = tab;
    }

    /// The top button: 2 or 5 to 8 players host (friends join it, up to 8 players in all), 1, 3 and 4 play on this PC.
    fn play(&mut self) {
        let hosting = self.settings.hosts();
        // on this PC: the game's number of players screen with all of 1 to 4 (computer players fill the others)
        let start = if hosting { Start::HostLan { players: self.settings.local_players().max(2) } } else { Start::Offline { four_players: true } };
        match &self.game {
            Ok(g) => match game::start(g, &start) {
                Ok(_) if hosting => self.say("Starting RampageRE, hosting: friends join this PC's address.", false),
                Ok(_) => self.say("Starting RampageRE...", false),
                Err(e) => self.say(&format!("Couldn't start RampageRE: {e}"), true),
            },
            Err(e) => self.say(&e.clone(), true),
        }
    }

    /// Join Local (LAN): the address typed, or else the games on the network looked for (lan.rs) and the one found
    /// joined (poll_lan).
    fn join_lan(&mut self) {
        match crate::lan::typed_addr(&self.lan_addr) {
            Some(Ok(addr)) => self.start_join_lan(&addr),
            Some(Err(e)) => self.say(&e, true),
            None => {
                let (tx, rx) = channel();
                let lan = self.lan_ip.clone();
                std::thread::spawn(move || {
                    let _ = tx.send(crate::lan::find(lan.as_deref(), std::time::Duration::from_millis(1500)));
                });
                self.lan_rx = Some(rx);
                self.say("Looking for games on your network...", false);
            }
        }
    }

    fn start_join_lan(&mut self, addr: &str) {
        let start = Start::Join { target: addr.to_string(), lobby: None };
        match &self.game {
            Ok(g) => match game::start(g, &start) {
                Ok(_) => self.say(&format!("Starting RampageRE: joining the game at {addr}..."), false),
                Err(e) => self.say(&format!("Couldn't start RampageRE: {e}"), true),
            },
            Err(e) => self.say(&e.clone(), true),
        }
    }

    fn poll_lan(&mut self) {
        let Some(rx) = &self.lan_rx else { return };
        let Ok(found) = rx.try_recv() else { return };
        self.lan_rx = None;
        let mine = env!("CARGO_PKG_VERSION");
        let same: Vec<_> = found.iter().filter(|f| f.version == mine).collect();
        match (same.first(), found.first()) {
            (Some(f), _) => {
                let addr = f.addr.clone();
                if same.len() > 1 {
                    let all: Vec<_> = same.iter().map(|f| f.addr.as_str()).collect();
                    self.lan_addr = addr.clone();
                    self.say(&format!("Games found: {}. Joining {addr} (type another in the box to pick it).",
                                      all.join(", ")), false);
                }
                self.start_join_lan(&addr);
            }
            (None, Some(f)) => self.say(&format!("Found a game at {}, but it runs RampageRE {} and you have {mine}: you both need the same version.", f.addr, f.version), true),
            (None, None) => self.say("No game found on your network. Is the host hosting yet, and has their PC's firewall allowed RampageRE (README: LAN play)? Or type the host's address in the box.", true),
        }
    }

    fn join(&mut self) {
        self.settings = Settings::load();
        let r = join::typed(&self.code).and_then(|link| join::plan_join(&link, &self.settings, &self.game));
        let (game, start, remember) = match r {
            Ok(x) => x,
            Err(e) => return self.say(&e, true),
        };
        self.joining = match &start {
            Start::Join { target, lobby: Some(server) } => Some((server.clone(), target.clone())),
            _ => None,
        };
        let (tx, rx) = channel();
        let build = self.build.clone();
        std::thread::spawn(move || {
            let _ = tx.send(join::run_join(&game, &build, &start).map(|child| (remember, child)));
        });
        self.busy = Some(rx);
        self.say("Checking the game...", false);
    }

    fn poll(&mut self) {
        if let Some(rx) = &self.busy {
            if let Ok(r) = rx.try_recv() {
                self.busy = None;
                match r {
                    Ok((remember, child)) => {
                        if let Some(s) = remember {
                            self.settings = Settings::update(|x| x.remember(&s));
                        }
                        match self.joining.take() {
                            Some((server, code)) => {
                                self.guest = Some(guest::Guest::new(server, code, child));
                                self.note = None;
                            }
                            None => self.say("Starting RampageRE: joining...", false),
                        }
                    }
                    Err(e) => self.say(&e, true),
                }
            }
        }
        if self.settings_read.elapsed().as_secs() >= 2 {
            self.settings = Settings::load();
            self.settings_read = std::time::Instant::now();
        }
    }

    /// Under Play Local: how many players (1 to 8; 3 or 4 adds 3 Player and 4 Player to the game's menu; 2 and 5 to 8
    /// host, the others joining over the network), and that hosting is still there in the game (Insert).
    fn players_choice(&mut self, ui: &mut egui::Ui) {
        let label = |n: u8| if n == 1 { "1 player".to_string() } else { format!("{n} players") };
        let now = self.settings.local_players();
        let mut pick = now;
        ui.horizontal(|ui| {
            ui.add_space((ui.available_width() - 200.0).max(0.0) / 2.0);
            ui.label(RichText::new("Players:").color(DIM_TEXT));
            egui::ComboBox::from_id_salt("players").selected_text(label(now)).width(120.0).show_ui(ui, |ui| {
                for n in 1..=8u8 {
                    ui.selectable_value(&mut pick, n, label(n));
                }
            });
        });
        if pick != now {
            self.settings = Settings::update(|s| s.local_players = pick);
        }
        let n = self.settings.local_players();
        let hint = if self.settings.hosts() {
            let lan = self.lan_ip.as_ref().map_or(String::new(), |ip| format!(", same Wi-Fi: {ip}"));
            let who = if n > 2 { format!("Pick {n} Player in the game; up to {} friends ", n - 1) } else { "Friends ".to_string() };
            format!("{who}join your address: this PC: 127.0.0.1{lan}. From the internet: your public IP.")
        } else {
            "On this PC: the game's players screen gives you 1 to 4, with computer players.".to_string()
        };
        ui.label(RichText::new(hint).strong().color(look::YELLOW));
        if self.settings.hosts() {
            self.public_ip_row(ui);
        }
    }

    /// The firewall: looked at on a thread (the first time the row shows), or fixed (Windows' admin Yes/No).
    fn firewall_job(&mut self, fix: bool) {
        let Ok(g) = &self.game else { return };
        let progs = crate::firewall::programs(g, std::env::current_exe().ok());
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let r = if fix { crate::firewall::fix(&progs) } else { Ok(crate::firewall::status(&progs)) };
            let _ = tx.send(r);
        });
        self.fw_rx = Some((rx, fix));
        if fix {
            self.fw_note = None;
        }
    }

    /// "Allow through firewall", beside "Show my internet address": only the host needs it.
    fn firewall_button(&mut self, ui: &mut egui::Ui) {
        use crate::firewall::Status;
        if let Some((rx, fix)) = &self.fw_rx {
            if let Ok(r) = rx.try_recv() {
                let fix = *fix;
                self.fw_rx = None;
                match r {
                    Ok(st) => {
                        self.fw_status = Some(st);
                        if fix && st == Status::Allowed {
                            self.fw_note = Some(("Done: friends can reach your game through Windows Firewall now.".into(), false));
                        }
                    }
                    Err(e) => self.fw_note = Some((e, true)),
                }
            }
        }
        if self.fw_status.is_none() && self.fw_rx.is_none() && self.game.is_ok() {
            self.firewall_job(false);
        }
        let hover = "Only the PC that hosts needs this: friends joining connect out, which Windows always allows. Windows asks for permission (admin) once, like a Steam game's installer.";
        match (&self.fw_rx, self.fw_status) {
            (Some((_, true)), _) => { ui.label(RichText::new("Asking Windows...").small()); }
            (Some((_, false)), None) => { ui.label(RichText::new("Checking the firewall...").small()); }
            (_, Some(Status::Allowed)) => {
                // (the button kept, ticked: the rule is there; clicking looks again)
                if ui.button(RichText::new("✔ Allowed through firewall").color(look::GREEN))
                    .on_hover_text(format!("The firewall rule is there. {hover} Click to look again."))
                    .clicked() { self.firewall_job(false); }
            }
            (_, Some(Status::Blocked)) => {
                if ui.button(RichText::new("Unblock in firewall").color(look::RED))
                    .on_hover_text(format!("Windows is blocking RampageRE (its \"Allow access?\" box was cancelled once). {hover}"))
                    .clicked() { self.firewall_job(true); }
            }
            _ => {
                if ui.button("Allow through firewall").on_hover_text(hover).clicked() {
                    self.firewall_job(true);
                }
            }
        }
        if self.fw_rx.is_some() {
            ui.ctx().request_repaint_after(std::time::Duration::from_millis(300));
        }
    }

    /// "Show my internet address": looked up on a thread when clicked, then shown with a Copy button.
    fn public_ip_row(&mut self, ui: &mut egui::Ui) {
        if let Some(rx) = &self.public_rx {
            if let Ok(r) = rx.try_recv() {
                self.public_ip = Some(r);
                self.public_rx = None;
            }
        }
        ui.horizontal(|ui| {
            match &self.public_ip {
                Some(Ok(ip)) => {
                    ui.label(RichText::new(format!("Your internet address: {ip}")).strong());
                    if ui.button("Copy").clicked() {
                        ui.ctx().copy_text(ip.clone());
                    }
                    ui.label(RichText::new(format!("(friends need port {}, TCP and UDP, forwarded to this PC)",
                        self.settings.host_port())).small().color(look::DIM_TEXT));
                }
                Some(Err(e)) => {
                    ui.label(RichText::new(e).small().color(look::RED));
                }
                None if self.public_rx.is_some() => {
                    ui.label(RichText::new("Looking up your internet address...").small());
                }
                None => {
                    if ui.button("Show my internet address").clicked() {
                        let (tx, rx) = std::sync::mpsc::channel();
                        std::thread::spawn(move || {
                            let _ = tx.send(join::public_ip());
                        });
                        self.public_rx = Some(rx);
                    }
                }
            }
            if !matches!(self.public_ip, Some(Ok(_))) {
                self.firewall_button(ui);
            }
        });
        if matches!(self.public_ip, Some(Ok(_))) {
            ui.horizontal(|ui| self.firewall_button(ui));
        }
        if let Some((text, problem)) = &self.fw_note {
            let color = if *problem { look::RED } else { look::GREEN };
            ui.label(RichText::new(text).small().color(color));
        }
        if self.public_rx.is_some() {
            ui.ctx().request_repaint_after(std::time::Duration::from_millis(300));
        }
    }

    /// The LAN tab: play or host on this network, or join a game on it.
    fn lan_tab(&mut self, ui: &mut egui::Ui, ready: bool) {
        let play = match self.settings.local_players() {
            1 => "Play Alone Locally",
            2 | 5..=8 => "Host Local (LAN)",
            _ => "Play Local",
        };
        if look::big(ui, play, ready).clicked() {
            self.play();
        }
        self.players_choice(ui);
        ui.separator();
        let searching = self.lan_rx.is_some();
        let label = if searching { "Looking for games..." } else { "Join Local (LAN)" };
        if look::big(ui, label, ready && !searching)
            .on_hover_text("Joins a game hosted on your network: the address typed below, or (left empty) the game it finds")
            .clicked()
        {
            self.join_lan();
        }
        ui.horizontal(|ui| {
            ui.label("Host's address:");
            let edit = ui.add(egui::TextEdit::singleline(&mut self.lan_addr)
                .hint_text("empty: find it, or like 192.168.7.105").desired_width(240.0));
            if edit.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) && ready && !searching {
                self.join_lan();
            }
        });
        self.note_line(ui, 0);
    }

    /// The Internet tab: host with the launcher's own server (first the server, then the game in it), or join a
    /// room on someone's. Hosting or joining shows here only: the other tabs stay as they are.
    fn internet_tab(&mut self, ui: &mut egui::Ui, ready: bool) {
        if self.hosting() {
            return self.hosting_box(ui);
        }
        if self.guest.is_some() {
            return self.guest_box(ui);
        }
        look::section(ui, "Host an internet game");
        if look::big(ui, "Start Internet Server", ready)
            .on_hover_text("Step 1: your own room server (friends Connect to it). Then launch the game as host.")
            .clicked()
        {
            self.host_own();
        }
        ui.add_enabled(false, egui::Button::new(RichText::new("Launch Game as Host").size(16.5).family(look::semibold()))
            .min_size(egui::vec2(300.0, 44.0)))
            .on_disabled_hover_text("Start the server first");
        self.room_size_choice(ui, 1);
        ui.separator();
        self.join_room_box(ui);
    }

    /// Players for the Internet tab (1) or the Steam tab (2), each its own: the room's size, you counted. The game's
    /// menu offers N Player up to it, and no more friends than that get in.
    pub fn room_size_choice(&mut self, ui: &mut egui::Ui, tab: u8) {
        let now = if tab == 1 { self.settings.internet_players() } else { self.settings.steam_players() };
        let mut pick = now;
        let label = |n: u8| format!("{n} players");
        ui.horizontal(|ui| {
            ui.add_space((ui.available_width() - 200.0).max(0.0) / 2.0);
            ui.label(RichText::new("Players:").color(DIM_TEXT));
            egui::ComboBox::from_id_salt(("room_size", tab)).selected_text(label(now)).width(120.0).show_ui(ui, |ui| {
                for n in 2..=8u8 {
                    ui.selectable_value(&mut pick, n, label(n));
                }
            });
        }).response.on_hover_text(format!("You and up to {} friends", now - 1));
        if pick != now {
            self.settings = Settings::update(|s| if tab == 1 { s.internet_players = pick } else { s.steam_players = pick });
        }
    }

    /// The Steam tab: host, or wait for a friend's Join Game, through Steam (Spacewar).
    fn steam_tab(&mut self, ui: &mut egui::Ui, ready: bool) {
        ui.horizontal(|ui| {
            // the heading centered over the row, the "?" at its end
            ui.add_space((ui.available_width() - 170.0).max(0.0) / 2.0);
            ui.label(RichText::new("Play through Steam").size(16.0).family(look::semibold()));
            if ui.small_button("?").on_hover_text("What's Spacewar?").clicked() {
                self.card = Some(Card::Spacewar(None));
            }
        });
        // Steam running, and Spacewar owned (the buttons wait for it), looked at every few seconds while shown
        if self.steam_seen.map_or(true, |s| s.2.elapsed().as_secs() >= 3) {
            self.steam_seen = Some((crate::steam::steam_running(), crate::steam::spacewar_in_library(), std::time::Instant::now()));
        }
        ui.ctx().request_repaint_after(std::time::Duration::from_secs(3));
        let (running, owned, _) = self.steam_seen.unwrap();
        if running {
            ui.label(RichText::new("✔ Steam is running").color(look::GREEN));
        } else {
            ui.label(RichText::new("Steam isn't running").color(look::YELLOW));
            if ui.small_button("Open Steam").clicked() {
                crate::steam::open_steam();
            }
        }
        if owned {
            ui.label(RichText::new("✔ Spacewar is in your Steam library").color(look::GREEN));
        } else {
            ui.label(RichText::new("You need Spacewar (free) in your Steam library first").color(look::RED));
            if look::big(ui, "Install Spacewar (free)", true).clicked() {
                crate::steam::get_spacewar();
            }
            ui.label(RichText::new(crate::steam::GET_SPACEWAR_NOTE).small().color(DIM_TEXT));
        }
        let can = ready && owned;
        if look::big(ui, "Host on Steam", can).on_disabled_hover_text("Install Spacewar first").clicked() {
            self.steam_host();
        }
        self.room_size_choice(ui, 2);
        if look::big(ui, "Join on Steam", can).on_disabled_hover_text("Install Spacewar first").clicked() {
            self.steam_join();
        }
        self.steam_problem_line(ui);
        self.note_line(ui, 2);
    }

    /// What the last action said (an error red), under the tab that did it (the Internet tab shows it under its
    /// room code box).
    pub fn note_line(&self, ui: &mut egui::Ui, tab: u8) {
        if let Some((t, problem)) = self.note.as_ref().filter(|_| self.note_tab == tab) {
            ui.label(RichText::new(t).size(15.0).color(if *problem { look::RED } else { look::GREEN }));
        }
    }

    fn main_screen(&mut self, ui: &mut egui::Ui) {
        ui.vertical_centered(|ui| {
            ui.add_space(12.0);
            look::logo(ui, 420.0);
            look::panel().show(ui, |ui| {
                ui.set_width(380.0);
                ui.vertical_centered(|ui| {
                    // (without the game every button still answers: with why it can't start)
                    let ready = self.steam_check.is_none();
                    let mut tab = self.settings.tab.min(2) as usize;
                    // (hosting or in a friend's room: a dot on the Internet tab, wherever you are)
                    let internet = if self.hosting() || self.guest.is_some() { "Internet ●" } else { "Internet" };
                    if look::tabs(ui, &["LAN", internet, "Steam"], &mut tab) {
                        self.settings = Settings::update(|s| s.tab = tab as u8);
                    }
                    ui.add_space(4.0);
                    match tab {
                        0 => self.lan_tab(ui, ready),
                        1 => self.internet_tab(ui, ready),
                        _ => self.steam_tab(ui, ready),
                    }
                    ui.separator();
                    self.settings_button(ui);
                });
            });
        });
        ui.with_layout(egui::Layout::bottom_up(egui::Align::Max), |ui| {
            let v = if self.build.is_empty() { "no game found".to_string() } else { format!("game build {}", self.build) };
            ui.label(RichText::new(v).small().color(DIM_TEXT));
        });
    }
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.poll();
        self.poll_hosting();
        self.poll_steam();
        self.poll_guest();
        self.poll_connect();
        self.poll_lan();
        if self.lan_rx.is_some() || self.connecting() || self.busy.is_some() || self.hosting() || self.steam_check.is_some() || self.guest.is_some() {
            ctx.request_repaint_after(std::time::Duration::from_millis(250));
        }
        // closing while hosting: asked first (the lobby lives in this window)
        if ctx.input(|i| i.viewport().close_requested()) && self.hosting() && !self.host.confirm_close {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.host.confirm_close = true;
        }
        egui::CentralPanel::default().frame(egui::Frame::none()).show(ctx, |ui| {
            look::background(ui);
            // (a long rooms list or the hosting box's players: the screen scrolls, the window's size stays)
            egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
                self.main_screen(ui);
                ui.add_space(12.0);
            });
        });
        self.settings_window(ctx);
        self.first_run_popup(ctx);
        self.router_popup(ctx);
        self.card_window(ctx);
        self.close_question(ctx);
    }

    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) { self.stop_hosting_now(); }
}

impl App {
    fn close_question(&mut self, ctx: &egui::Context) {
        if !self.host.confirm_close || !self.hosting() {
            return;
        }
        egui::Window::new("Stop hosting?").anchor(egui::Align2::CENTER_CENTER, egui::Vec2::ZERO).collapsible(false)
            .resizable(false).show(ctx, |ui| {
                ui.label(RichText::new(host::CLOSE_TEXT).size(16.0));
                ui.horizontal(|ui| {
                    if ui.button(RichText::new("Close anyway").strong()).clicked() {
                        self.stop_hosting_now();
                        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                    }
                    if ui.button("Keep hosting").clicked() {
                        self.host.confirm_close = false;
                    }
                });
            });
    }
}

/// The window, drawn with OpenGL; where the driver won't give OpenGL 2.0 (an old or missing driver, a remote
/// session, a laptop handing the window to the wrong GPU), drawn with DirectX / Vulkan instead.
/// RAMPAGE_LAUNCHER_RENDERER=wgpu skips OpenGL (to try the fallback).
pub fn run() -> eframe::Result<()> {
    let force_wgpu = std::env::var("RAMPAGE_LAUNCHER_RENDERER").is_ok_and(|v| v.eq_ignore_ascii_case("wgpu"));
    if !force_wgpu {
        match run_with(eframe::Renderer::Glow) {
            Err(e @ (eframe::Error::OpenGL(_) | eframe::Error::Glutin(_) | eframe::Error::NoGlutinConfigs(..))) => {
                eprintln!("OpenGL failed ({e}); trying DirectX / Vulkan");
            }
            r => return r,
        }
    }
    run_with(eframe::Renderer::Wgpu)
}

fn run_with(renderer: eframe::Renderer) -> eframe::Result<()> {
    look::using_wgpu(renderer == eframe::Renderer::Wgpu);
    let mut viewport = egui::ViewportBuilder::default().with_inner_size([900.0, 760.0]).with_resizable(false)
        .with_maximize_button(false).with_title("RampageRE Launcher");
    if let Some(icon) = look::icon() {
        viewport = viewport.with_icon(icon);
    }
    let opts = eframe::NativeOptions {
        viewport,
        renderer,
        ..Default::default()
    };
    eframe::run_native("RampageRE Launcher", opts, Box::new(|cc| Ok(Box::new(App::new(cc)))))
}
