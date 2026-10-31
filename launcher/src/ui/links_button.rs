//! "Set up join links": flashing until it's done, then a big green checkmark. The first run's pop-up says what it is.
use super::look::{self, GREEN, YELLOW};
use super::App;
use crate::links::{self, LinkStatus};
use crate::settings::Settings;
use eframe::egui::{self, Align2, RichText, Stroke, Vec2};

pub const FIRST_RUN_TEXT: &str =
    "Set up rampage:// links so a link from a friend opens the game and joins straight away.";

impl App {
    pub fn set_up_links(&mut self) {
        let me = std::env::current_exe().unwrap_or_default();
        match links::classes().and_then(|root| links::register(&root, &me)) {
            Ok(()) => self.say("Links set up: a rampage:// link now opens RampageRE.", false),
            Err(e) => self.say(&format!("Couldn't set up the links: {e}"), true),
        }
        self.refresh_links();
    }

    fn remove_links(&mut self) {
        if let Err(e) = links::classes().and_then(|root| links::unregister(&root)) {
            self.say(&format!("Couldn't remove the links: {e}"), true);
        }
        self.refresh_links();
    }

    pub fn refresh_links(&mut self) {
        let me = std::env::current_exe().unwrap_or_default();
        self.links = links::classes().map_or(LinkStatus::NotSet, |root| links::status(&root, &me));
    }

    pub fn links_button(&mut self, ui: &mut egui::Ui) {
        match self.links.clone() {
            LinkStatus::Ok => {
                ui.horizontal(|ui| {
                    ui.label(RichText::new("✔ Links set up").size(22.0).strong().color(GREEN));
                    if ui.small_button("Remove").clicked() {
                        self.remove_links();
                    }
                });
            }
            LinkStatus::Stale(old) => {
                ui.horizontal(|ui| {
                    ui.label(RichText::new("Links point to an old copy").color(YELLOW))
                        .on_hover_text(format!("They open {old}"));
                    if ui.button("Fix").clicked() {
                        self.set_up_links();
                    }
                });
            }
            LinkStatus::NotSet => {
                let r = look::big(ui, "Set up join links", true);
                // flashing on the first run, a steady highlight after "Not now"
                let t = ui.ctx().input(|i| i.time);
                let a = if self.settings.first_run_done { 0.6 } else { 0.35 + 0.65 * (0.5 + 0.5 * (t * std::f64::consts::TAU).sin()) };
                ui.painter().rect_stroke(r.rect.expand(3.0), 8.0, Stroke::new(3.0, YELLOW.gamma_multiply(a as f32)));
                if !self.settings.first_run_done {
                    ui.ctx().request_repaint();
                }
                if r.clicked() {
                    self.set_up_links();
                }
            }
        }
    }

    /// The first run, with links not set up: what they are, Set up / Not now.
    pub fn first_run_popup(&mut self, ctx: &egui::Context) {
        if self.settings.first_run_done || self.links == LinkStatus::Ok {
            return;
        }
        egui::Window::new("Join links")
            .anchor(Align2::CENTER_CENTER, Vec2::ZERO)
            .collapsible(false)
            .resizable(false)
            .show(ctx, |ui| {
                ui.set_max_width(360.0);
                ui.label(RichText::new(FIRST_RUN_TEXT).size(16.0));
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
}
