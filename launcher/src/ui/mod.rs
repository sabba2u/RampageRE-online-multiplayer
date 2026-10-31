//! The launcher's window: Play, Host a game (on Steam / with my own server), Join on Steam, Join by code, and the
//! join links. Anything slow (a version check, the router, Steam's checks) runs on a thread; the window keeps drawing.
pub mod join;
#[cfg(test)]
mod join_tests;
mod host;
#[cfg(test)]
mod host_tests;
mod links_button;
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
    /// a join under way: its outcome
    busy: Option<Receiver<Result<String, String>>>,
    pub host: host::Hosting,
    /// a card showing in the middle of the window
    pub card: Option<Card>,
    /// Steam's checks under way, for which button
    pub steam_check: Option<(SteamAction, Receiver<Result<(), SteamProblem>>)>,
    pub steam_problem: Option<SteamProblem>,
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
        let mut app = App { game, build, settings: Settings::load(), links: LinkStatus::NotSet, code: String::new(), note: None,
                            busy: None, host: host::Hosting::default(), card: None, steam_check: None, steam_problem: None };
        app.refresh_links();
        app
    }

    pub fn say(&mut self, text: &str, problem: bool) { self.note = Some((text.to_string(), problem)); }

    fn play(&mut self) {
        match &self.game {
            Ok(g) => match game::start(g, &Start::Offline) {
                Ok(_) => self.say("Starting RampageRE...", false),
                Err(e) => self.say(&format!("Couldn't start RampageRE: {e}"), true),
            },
            Err(e) => self.say(&e.clone(), true),
        }
    }

    fn join(&mut self) {
        let r = join::typed(&self.code).and_then(|link| join::join_target(&link, &self.settings));
        let (start, remember) = match r {
            Ok(x) => x,
            Err(e) => return self.say(&e, true),
        };
        if let Some(s) = remember {
            self.settings.remember(&s);
            let _ = self.settings.save();
        }
        let game = match &self.game {
            Ok(g) => g.clone(),
            Err(e) => return self.say(&e.clone(), true),
        };
        let (tx, rx) = channel();
        let build = self.build.clone();
        std::thread::spawn(move || {
            let _ = tx.send(join::run_join(&game, &build, &start).map(|_| "Starting RampageRE: joining...".to_string()));
        });
        self.busy = Some(rx);
        self.say("Checking the game...", false);
    }

    fn poll(&mut self) {
        if let Some(rx) = &self.busy {
            if let Ok(r) = rx.try_recv() {
                self.busy = None;
                match r {
                    Ok(t) => self.say(&t, false),
                    Err(e) => self.say(&e, true),
                }
            }
        }
    }

    fn server_picker(&mut self, ui: &mut egui::Ui) {
        if self.settings.servers.is_empty() {
            ui.label(RichText::new("No group server yet: paste the group link from your host.").small().color(DIM_TEXT));
            return;
        }
        let mut pick = None;
        ui.horizontal(|ui| {
            ui.label(RichText::new("Group server:").small().color(DIM_TEXT));
            egui::ComboBox::from_id_salt("server").selected_text(RichText::new(&self.settings.servers[0]).small())
                .show_ui(ui, |ui| {
                    for s in &self.settings.servers {
                        if ui.selectable_label(false, s).clicked() {
                            pick = Some(s.clone());
                        }
                    }
                });
        });
        if let Some(s) = pick {
            self.settings.remember(&s);
            let _ = self.settings.save();
        }
    }

    fn main_screen(&mut self, ui: &mut egui::Ui) {
        ui.vertical_centered(|ui| {
            ui.add_space(12.0);
            look::logo(ui, 420.0);
            look::panel().show(ui, |ui| {
                ui.set_width(380.0);
                ui.vertical_centered(|ui| {
                    if self.hosting() {
                        self.hosting_box(ui);
                        return;
                    }
                    let ready = self.game.is_ok() && self.steam_check.is_none();
                    if look::big(ui, "Play", ready).clicked() {
                        self.play();
                    }
                    ui.add_space(4.0);
                    ui.horizontal(|ui| {
                        ui.label(RichText::new("Host a game").size(17.0).strong());
                        if ui.add_enabled(ready, egui::Button::new(RichText::new("On Steam").size(17.0))).clicked() {
                            self.steam_host();
                        }
                        if ui.add_enabled(ready, egui::Button::new(RichText::new("With my own server").size(17.0))).clicked() {
                            self.host_own();
                        }
                    });
                    ui.horizontal(|ui| {
                        if look::big(ui, "Join on Steam", ready).clicked() {
                            self.steam_join();
                        }
                        if ui.small_button("?").on_hover_text("What's Spacewar?").clicked() {
                            self.card = Some(Card::Spacewar(None));
                        }
                    });
                    self.steam_problem_line(ui);
                    ui.add_space(4.0);
                    ui.add_space(6.0);
                    ui.label(RichText::new("Join by code").strong());
                    ui.horizontal(|ui| {
                        let edit = ui.add(egui::TextEdit::singleline(&mut self.code).hint_text("K7PQ2 or a group link")
                            .font(egui::TextStyle::Heading).desired_width(250.0));
                        let enter = edit.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
                        let button = ui.add_enabled(self.busy.is_none(),
                                                    egui::Button::new(RichText::new("Join").size(18.0).strong()));
                        if button.clicked() || enter {
                            self.join();
                        }
                    });
                    self.server_picker(ui);
                    ui.add_space(6.0);
                    self.links_button(ui);
                    if let Some((t, problem)) = &self.note {
                        ui.add_space(4.0);
                        ui.label(RichText::new(t).size(15.0).color(if *problem { RED } else { GREEN }));
                    }
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
        if self.busy.is_some() || self.hosting() || self.steam_check.is_some() {
            ctx.request_repaint_after(std::time::Duration::from_millis(250));
        }
        // closing while hosting: asked first (the lobby lives in this window)
        if ctx.input(|i| i.viewport().close_requested()) && self.hosting() && !self.host.confirm_close {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.host.confirm_close = true;
        }
        egui::CentralPanel::default().frame(egui::Frame::none()).show(ctx, |ui| {
            look::background(ui);
            self.main_screen(ui);
        });
        self.first_run_popup(ctx);
        self.router_popup(ctx);
        self.card_window(ctx);
        self.close_question(ctx);
    }

    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) { self.stop_hosting(); }
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
                        self.stop_hosting();
                        ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                    }
                    if ui.button("Keep hosting").clicked() {
                        self.host.confirm_close = false;
                    }
                });
            });
    }
}

pub fn run() -> eframe::Result<()> {
    let opts = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([720.0, 720.0]).with_min_inner_size([600.0, 640.0])
            .with_title("RampageRE Launcher"),
        ..Default::default()
    };
    eframe::run_native("RampageRE Launcher", opts, Box::new(|cc| Ok(Box::new(App::new(cc)))))
}
