//! The launcher's window: Play, Host a game (my own server), Steam (host / join), Join by code, and Settings (the
//! join links, group servers, tips). Anything slow (a version check, the router, Steam's checks) runs on a thread; the window keeps drawing.
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
    /// a join under way: its outcome (the server to remember once it went through)
    busy: Option<Receiver<Result<Option<String>, String>>>,
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
                            busy: None, settings_read: std::time::Instant::now(), host: host::Hosting::default(), card: None,
                            steam_check: None, steam_problem: None, settings_open: false };
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
        self.settings = Settings::load();
        let r = join::typed(&self.code).and_then(|link| join::plan_join(&link, &self.settings, &self.game));
        let (game, start, remember) = match r {
            Ok(x) => x,
            Err(e) => return self.say(&e, true),
        };
        let (tx, rx) = channel();
        let build = self.build.clone();
        std::thread::spawn(move || {
            let _ = tx.send(join::run_join(&game, &build, &start).map(|_| remember));
        });
        self.busy = Some(rx);
        self.say("Checking the game...", false);
    }

    fn poll(&mut self) {
        if let Some(rx) = &self.busy {
            if let Ok(r) = rx.try_recv() {
                self.busy = None;
                match r {
                    Ok(remember) => {
                        if let Some(s) = remember {
                            self.settings = Settings::update(|x| x.remember(&s));
                        }
                        self.say("Starting RampageRE: joining...", false);
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
            self.settings = Settings::update(|x| x.remember(&s));
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
                    // (without the game every button still answers: with why it can't start)
                    let ready = self.steam_check.is_none();
                    if look::big(ui, "Play", ready).clicked() {
                        self.play();
                    }
                    ui.separator();
                    look::section(ui, "Host a game");
                    if look::big(ui, "With my own server", ready).clicked() {
                        self.host_own();
                    }
                    ui.separator();
                    ui.horizontal(|ui| {
                        // the heading centered over the row, the "?" at its end
                        ui.add_space((ui.available_width() - 120.0).max(0.0) / 2.0);
                        ui.label(RichText::new("STEAM").size(15.0).strong().color(look::YELLOW));
                        if ui.small_button("?").on_hover_text("What's Spacewar?").clicked() {
                            self.card = Some(Card::Spacewar(None));
                        }
                    });
                    ui.columns(2, |c| {
                        if look::half(&mut c[0], "Host on Steam", ready).clicked() {
                            self.steam_host();
                        }
                        if look::half(&mut c[1], "Join on Steam", ready).clicked() {
                            self.steam_join();
                        }
                    });
                    self.steam_problem_line(ui);
                    ui.separator();
                    look::section(ui, "Join by code");
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
                    if let Some((t, problem)) = &self.note {
                        ui.add_space(4.0);
                        ui.label(RichText::new(t).size(15.0).color(if *problem { RED } else { GREEN }));
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
    let opts = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([720.0, 700.0]).with_resizable(false)
            .with_maximize_button(false).with_title("RampageRE Launcher"),
        renderer,
        ..Default::default()
    };
    eframe::run_native("RampageRE Launcher", opts, Box::new(|cc| Ok(Box::new(App::new(cc)))))
}
