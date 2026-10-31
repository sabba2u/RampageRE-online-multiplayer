//! Join links and Settings. The first run (links not set up yet) asks once, in a pop-up of its own colour; after that
//! links are set up, and everything else changed, from the Settings window: register / unregister join links, the
//! group servers, the tips shown again, where the game is.
use super::look::{self, DIM_TEXT, GREEN, RED, YELLOW};
use super::App;
use crate::links::{self, LinkStatus};
use crate::settings::Settings;
use eframe::egui::{self, Align2, Color32, RichText, Stroke, Vec2};

pub const FIRST_RUN_TEXT: &str =
    "Set up rampage:// links so a link from a friend opens the game and joins straight away.";
pub const FIRST_RUN_ONCE: &str =
    "This window won't pop up again: to set up join links later, use the Settings button.";

impl App {
    pub fn set_up_links(&mut self) {
        let me = std::env::current_exe().unwrap_or_default();
        match links::classes().and_then(|root| links::register(&root, &me)) {
            Ok(()) => self.say("Join links registered: a rampage:// link now opens RampageRE.", false),
            Err(e) => self.say(&format!("Couldn't register the join links: {e}"), true),
        }
        self.refresh_links();
    }

    fn remove_links(&mut self) {
        match links::classes().and_then(|root| links::unregister(&root)) {
            Ok(()) => self.say("Join links unregistered.", false),
            Err(e) => self.say(&format!("Couldn't unregister the join links: {e}"), true),
        }
        self.refresh_links();
    }

    pub fn refresh_links(&mut self) {
        let me = std::env::current_exe().unwrap_or_default();
        self.links = links::classes().map_or(LinkStatus::NotSet, |root| links::status(&root, &me));
    }

    /// The Settings button, at the foot of the main panel.
    pub fn settings_button(&mut self, ui: &mut egui::Ui) {
        if ui.button(RichText::new("⚙ Settings").size(16.0)).clicked() {
            self.settings_open = true;
            self.refresh_links();
        }
    }

    /// The first run, with links not set up: what they are, Set up / Not now, and that it asks only once.
    pub fn first_run_popup(&mut self, ctx: &egui::Context) {
        if self.settings.first_run_done || self.links == LinkStatus::Ok {
            return;
        }
        // its own colour: a dark amber card with a yellow edge, not the plain grey of the other windows
        let frame = egui::Frame::window(&ctx.style()).fill(Color32::from_rgb(58, 42, 8))
            .stroke(Stroke::new(2.0, YELLOW));
        egui::Window::new(RichText::new("Join links").color(YELLOW).strong())
            .frame(frame)
            .anchor(Align2::CENTER_CENTER, Vec2::ZERO)
            .collapsible(false)
            .resizable(false)
            .show(ctx, |ui| {
                ui.set_max_width(380.0);
                ui.label(RichText::new(FIRST_RUN_TEXT).size(16.0).color(Color32::WHITE));
                ui.add_space(6.0);
                ui.label(RichText::new(FIRST_RUN_ONCE).size(14.0).color(Color32::from_rgb(235, 215, 170)));
                ui.add_space(8.0);
                ui.horizontal(|ui| {
                    if ui.button(RichText::new("Set up").size(18.0).strong()).clicked() {
                        self.set_up_links();
                        self.first_run_done();
                    }
                    if ui.button("Not now").clicked() {
                        self.first_run_done();
                    }
                });
            });
    }

    fn first_run_done(&mut self) { self.settings = Settings::update(|s| s.first_run_done = true); }

    pub fn settings_window(&mut self, ctx: &egui::Context) {
        if !self.settings_open {
            return;
        }
        let mut open = true;
        egui::Window::new("Settings").open(&mut open).anchor(Align2::CENTER_CENTER, Vec2::ZERO).collapsible(false)
            .resizable(false).show(ctx, |ui| {
                ui.set_width(420.0);
                look::section(ui, "Join links");
                ui.label(RichText::new("rampage:// links from friends open the game and join.").color(DIM_TEXT));
                match self.links.clone() {
                    LinkStatus::Ok => {
                        ui.label(RichText::new("✔ Join links registered").size(22.0).strong().color(GREEN));
                        if ui.button("Unregister join links").clicked() {
                            self.remove_links();
                        }
                    }
                    LinkStatus::Stale(old) => {
                        ui.label(RichText::new("Join links point to an old copy of the launcher").color(YELLOW))
                            .on_hover_text(format!("They open {old}"));
                        ui.horizontal(|ui| {
                            if ui.button(RichText::new("Register join links").strong()).clicked() {
                                self.set_up_links();
                            }
                            if ui.button("Unregister join links").clicked() {
                                self.remove_links();
                            }
                        });
                    }
                    LinkStatus::NotSet => {
                        if look::big(ui, "Register join links", true).clicked() {
                            self.set_up_links();
                        }
                    }
                }
                ui.separator();
                look::section(ui, "Group servers");
                if self.settings.servers.is_empty() {
                    ui.label(RichText::new("None yet: a group link from your host adds one.").color(DIM_TEXT));
                }
                let mut forget = None;
                for (i, s) in self.settings.servers.iter().enumerate() {
                    ui.horizontal(|ui| {
                        let t = if i == 0 { format!("{s}  (in use)") } else { s.clone() };
                        ui.label(RichText::new(t).monospace());
                        if ui.small_button("Remove").clicked() {
                            forget = Some(s.clone());
                        }
                    });
                }
                if let Some(s) = forget {
                    self.settings = Settings::update(|x| x.servers.retain(|y| *y != s));
                }
                ui.separator();
                look::section(ui, "Tips");
                if ui.button("Show the tips again").on_hover_text("The join links pop-up, the firewall note, What's Spacewar?")
                    .clicked()
                {
                    self.settings = Settings::update(|s| {
                        s.first_run_done = false;
                        s.firewall_note_shown = false;
                        s.spacewar_card_shown = false;
                    });
                    self.say("The tips will show again.", false);
                }
                ui.separator();
                look::section(ui, "Game");
                match &self.game {
                    Ok(g) => {
                        ui.label(RichText::new(g.display().to_string()).monospace().small());
                        ui.label(RichText::new(format!("build {}", self.build)).color(DIM_TEXT));
                    }
                    Err(e) => {
                        ui.label(RichText::new(e).color(RED));
                    }
                }
            });
        self.settings_open = open;
    }
}
