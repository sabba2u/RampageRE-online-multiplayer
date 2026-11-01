//! While hosting: who is in the game (the game says it every second through the lobby: src/netlobby.c), with Mute
//! (their chat lines dropped by the host), Kick (let go; they can join again) and Ban (let go and refused from then on,
//! by their IP, kept in the settings; lifted in Settings) for each friend.
use super::look::{self, DIM_TEXT, GREEN, RED, YELLOW};
use super::App;
use crate::lobby::games::Player;
use crate::settings::Settings;
use eframe::egui::{self, RichText};

/// "Lizzie", else "Player 2" (no monster chosen yet)
pub fn player_name(p: &Player) -> String {
    if p.name.is_empty() { format!("Player {}", p.slot + 1) } else { p.name.clone() }
}

/// What the row says beside the name.
pub fn player_state(p: &Player) -> String {
    match p.state.as_str() {
        "host" => "you".into(),
        "local" => "on this PC".into(),
        "kept" => "dropped out: their place is kept for a minute".into(),
        "joining" => "joining...".into(),
        _ if p.ping > 0 => format!("{} ms", p.ping),
        _ => "playing".into(),
    }
}

enum Action {
    Mute(String, bool),
    Kick(String),
    Ban(String, String),
}

impl App {
    /// The banned IPs to the lobby (the game hears them within a second).
    pub fn send_bans(&self) {
        if let Some(l) = &self.host.lobby {
            l.games.lock().unwrap().set_banned(self.settings.banned_ips());
        }
    }

    pub fn players_box(&mut self, ui: &mut egui::Ui) {
        let (Some(l), Some(code)) = (&self.host.lobby, &self.host.code) else { return };
        let (roster, muted): (Vec<Player>, Vec<bool>) = {
            let g = l.games.lock().unwrap();
            let r = g.roster(code);
            let m = r.iter().map(|p| g.is_muted(&p.addr)).collect();
            (r, m)
        };
        look::section(ui, "Players");
        if roster.is_empty() {
            ui.label(RichText::new("Waiting for the game to say who's in...").color(DIM_TEXT));
            return;
        }
        let mut act = None;
        for (p, &mute) in roster.iter().zip(&muted) {
            // one row: P1, the name with its state under it (a fixed width: names never wrap), the buttons
            ui.horizontal(|ui| {
                ui.set_width(ui.available_width());
                ui.label(RichText::new(format!("P{}", p.slot + 1)).strong().color(if p.slot == 0 { YELLOW } else { GREEN }));
                ui.allocate_ui_with_layout(egui::vec2(150.0, 40.0), egui::Layout::top_down(egui::Align::Min), |ui| {
                    ui.set_min_width(150.0);
                    ui.spacing_mut().item_spacing.y = 2.0;
                    ui.add(egui::Label::new(RichText::new(player_name(p)).size(16.0).strong()).truncate());
                    let mut s = player_state(p);
                    if mute {
                        s += "  ·  muted";
                    }
                    ui.add(egui::Label::new(RichText::new(s).small().color(DIM_TEXT)).truncate());
                });
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if p.is_guest() {
                        // (right to left: they read Mute, Kick, Ban)
                        if ui.button(RichText::new("Ban").color(RED))
                            .on_hover_text("Removes them and stops them joining again (undo it in Settings)").clicked()
                        {
                            act = Some(Action::Ban(p.ip().to_string(), player_name(p)));
                        }
                        if ui.button("Kick").on_hover_text("Removes them from the game; they can join again").clicked() {
                            act = Some(Action::Kick(p.addr.clone()));
                        }
                        let m = if mute { "Unmute" } else { "Mute" };
                        if ui.button(m).on_hover_text("Their chat messages are hidden from everyone").clicked() {
                            act = Some(Action::Mute(p.addr.clone(), !mute));
                        }
                    }
                });
            });
        }
        if roster.len() == 1 {
            ui.label(RichText::new("No friends in yet: send them your code.").small().color(DIM_TEXT));
        }
        let Some(a) = act else { return };
        match a {
            Action::Mute(addr, on) => l.games.lock().unwrap().set_muted(&addr, on),
            Action::Kick(addr) => {
                l.games.lock().unwrap().kick(&addr);
                self.say("Removed from the game.", false);
            }
            Action::Ban(ip, name) => {
                self.settings = Settings::update(|s| s.ban(&ip, &name));
                self.send_bans();
                self.say(&format!("{name} is banned (Settings can undo it)."), false);
            }
        }
    }
}
