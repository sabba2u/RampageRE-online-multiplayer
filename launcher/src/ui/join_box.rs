//! JOIN A ROOM: connect to a server (the address your host gave you, or their link pasted), then join a room on it by
//! its code. The words are the same everywhere: a server (the host's PC, or a shared one later) holds rooms; a room
//! is one hosted game, with a 5-letter room code.
use super::look::{self, DIM_TEXT, GREEN, RED};
use super::{join, App};
use crate::links;
use crate::settings::Settings;
use eframe::egui::{self, RichText};
use std::sync::mpsc::{channel, Receiver};

#[derive(Default)]
pub struct JoinBox {
    pub server_text: String,
    /// a Connect under way: the server it checks
    connecting: Option<(String, Receiver<Result<(), String>>)>,
    /// the server Connect reached this session
    connected: Option<String>,
}

/// "http://192.168.7.105:19980" as people say it: "192.168.7.105" (the port only when it isn't the usual one).
pub fn server_shown(url: &str) -> String {
    let s = url.trim_start_matches("http://");
    s.strip_suffix(&format!(":{}", links::LOBBY_PORT)).unwrap_or(s).to_string()
}

impl App {
    /// Connect: the typed server (or a pasted link's, whose room code fills the room box), checked on a thread.
    fn connect(&mut self) {
        let text = self.join_box.server_text.trim().to_string();
        let url = match links::server_url(&text) {
            Ok(u) => u,
            Err(e) => return self.say(&e, true),
        };
        if let Ok(links::Link::CodeAt { code, .. }) = links::parse_link(&text) {
            self.code = code;
        }
        self.join_box.server_text = server_shown(&url);
        let (tx, rx) = channel();
        let u = url.clone();
        std::thread::spawn(move || {
            let _ = tx.send(join::check_server(&u));
        });
        self.join_box.connecting = Some((url, rx));
        self.say("Connecting to the server...", false);
    }

    pub fn poll_connect(&mut self) {
        let Some((url, rx)) = &self.join_box.connecting else { return };
        let Ok(r) = rx.try_recv() else { return };
        let url = url.clone();
        self.join_box.connecting = None;
        match r {
            Ok(()) => {
                self.settings = Settings::update(|s| s.remember(&url));
                self.join_box.connected = Some(url);
                self.note = None;
            }
            Err(e) => self.say(&e, true),
        }
    }

    pub fn connecting(&self) -> bool { self.join_box.connecting.is_some() }

    pub fn join_room_box(&mut self, ui: &mut egui::Ui) {
        look::section(ui, "Join a room");
        let busy = self.busy.is_some() || self.join_box.connecting.is_some();
        ui.horizontal(|ui| {
            let edit = ui.add(egui::TextEdit::singleline(&mut self.join_box.server_text)
                .hint_text("Server, like 192.168.7.105").font(egui::TextStyle::Heading).desired_width(230.0));
            let enter = edit.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
            let b = ui.add_enabled(!busy, egui::Button::new(RichText::new("Connect").size(17.0).strong()));
            if b.clicked() || enter {
                self.connect();
            }
        });
        // where Join goes: the server connected to, else the last one used (with the others to switch to)
        let current = self.settings.servers.first().cloned();
        let mut pick = None;
        ui.horizontal(|ui| {
            match (&current, &self.join_box.connected) {
                (Some(c), Some(ok)) if c == ok => {
                    ui.label(RichText::new(format!("✔ Connected to {}", server_shown(c))).color(GREEN));
                }
                (Some(c), _) => {
                    ui.label(RichText::new(format!("Server: {}", server_shown(c))).small().color(DIM_TEXT));
                }
                (None, _) => {
                    ui.label(RichText::new("Not connected to a server yet.").small().color(DIM_TEXT));
                }
            }
            if self.settings.servers.len() > 1 {
                egui::ComboBox::from_id_salt("servers").selected_text(RichText::new("Recent").small()).width(70.0)
                    .show_ui(ui, |ui| {
                        for s in &self.settings.servers {
                            if ui.selectable_label(false, server_shown(s)).clicked() {
                                pick = Some(s.clone());
                            }
                        }
                    });
            }
        });
        if let Some(s) = pick {
            self.join_box.server_text = server_shown(&s);
            self.connect();
        }
        ui.horizontal(|ui| {
            let edit = ui.add(egui::TextEdit::singleline(&mut self.code).hint_text("Room code, like K7PQ2")
                .font(egui::TextStyle::Heading).desired_width(230.0));
            let enter = edit.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
            let b = ui.add_enabled(!busy, egui::Button::new(RichText::new("Join").size(17.0).strong()));
            if b.clicked() || enter {
                self.join();
            }
        });
        if let Some((t, problem)) = &self.note {
            ui.label(RichText::new(t).size(15.0).color(if *problem { RED } else { GREEN }));
        }
    }
}
