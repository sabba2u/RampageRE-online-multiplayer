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
use std::sync::mpsc::channel;

pub const LOBBY_PORT: u16 = 19980;
/// the game's own port when hosting (src/net.h NET_PORT)
pub const GAME_PORT: u16 = 27960;
pub const PORT_IN_USE: &str = "Another lobby is already running on this PC.";
pub const ROUTER_TITLE: &str = "Friends outside your home can't connect to you on this network.";
pub const ROUTER_TEXT: &str = "Friends on the same home network can still join with your code.";
pub const FIREWALL_TEXT: &str = "Windows will ask whether RampageRE may use the network: click Allow.";
pub const CLOSE_TEXT: &str = "Your friends won't be able to join.";

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
    pub reach: Option<Reach>,
    reach_rx: Option<std::sync::mpsc::Receiver<Reach>>,
    pub code: Option<String>,
    pub lan: Option<String>,
    pub router_popup: bool,
    pub confirm_close: bool,
}

impl App {
    pub fn hosting(&self) -> bool { self.host.lobby.is_some() }

    /// "With my own server": the firewall note once, then the lobby, the router, the game.
    pub fn host_own(&mut self) {
        if !self.settings.firewall_note_shown {
            self.card = Some(Card::Firewall);
            return;
        }
        let game = match &self.game {
            Ok(g) => g.clone(),
            Err(e) => return self.say(&e.clone(), true),
        };
        let lobby = match LobbyServer::start(LOBBY_PORT, None) {
            Ok(l) => l,
            Err(StartError::PortInUse) => return self.say(PORT_IN_USE, true),
            Err(StartError::Other(e)) => return self.say(&format!("The lobby couldn't start: {e}"), true),
        };
        if let Err(e) = game::start(&game, &Start::Host { lobby: format!("http://127.0.0.1:{LOBBY_PORT}") }) {
            return self.say(&format!("Couldn't start RampageRE: {e}"), true);
        }
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let _ = tx.send(upnp::open_lobby_port(LOBBY_PORT));
        });
        self.host = Hosting { lobby: Some(lobby), reach_rx: Some(rx), lan: upnp::lan_ip(), ..Default::default() };
        self.say("Starting your game and server...", false);
    }

    /// Stops the lobby and closes the router's forward (the game itself keeps running).
    pub fn stop_hosting(&mut self) {
        if let Some(l) = self.host.lobby.take() {
            l.stop();
            if matches!(self.host.reach, Some(Reach::Internet { .. })) {
                upnp::close_lobby_port(LOBBY_PORT);
            }
        }
        self.host = Hosting::default();
    }

    pub fn poll_hosting(&mut self) {
        if let Some(rx) = &self.host.reach_rx {
            if let Ok(r) = rx.try_recv() {
                self.host.router_popup = matches!(r, Reach::HomeOnly { .. });
                self.host.reach = Some(r);
                self.host.reach_rx = None;
            }
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
            return;
        };
        ui.label(RichText::new("Your game's code").color(look::DIM_TEXT));
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
                    ui.label(RichText::new("Friends who already have your link can just type the code.").small().color(GREEN));
                }
            }
        }
        if ui.button("Stop hosting").clicked() {
            self.stop_hosting();
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
