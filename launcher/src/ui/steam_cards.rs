//! Steam: the checks before either button (each problem with the button that fixes it), "What's Spacewar?", the
//! Shift+Tab card after hosting, and the Join Game card with its picture (our own drawing: no Valve logo or screen).
use super::look::{DIM_TEXT, RED, YELLOW};
use super::{App, Card};
use crate::game::{self, Start};
use crate::settings::Settings;
use crate::steam::{self, SteamProblem, GET_SPACEWAR_NOTE};
use eframe::egui::{self, Align2, Color32, Pos2, Rect, RichText, Rounding, Sense, Stroke, Vec2};
use std::sync::mpsc::channel;

/// "What's Spacewar?", one paragraph per entry.
pub const SPACEWAR_TEXT: &[&str] = &[
    "Steam online play only works for games that have their own Steam app. RampageRE doesn't have one (that needs a \
paid Steam listing). Valve gives every game maker a free test app called Spacewar (app 480).",
    "Game developers use it to test their multiplayer through Steam before their own game has a store page, and \
RampageRE uses the same technique.",
    "Steam treats RampageRE as Spacewar, so you and your friends each need to own Spacewar. It's free, and you don't \
need to download or play it: it only has to be in your Steam library.",
    "Your friends list will show you as \"playing Spacewar\".",
];
pub const SHIFT_TAB_TEXT: &str = "Press Shift+Tab in the game to open Steam, then click Invite to choose your friends.";
pub const JOIN_GAME_TEXT: &str = "Open your Steam friends list, right-click your friend, then click Join Game.";
/// Before Join Game: RampageRE has to be open already (Join on Steam started it). With it closed, Steam starts the real
/// Spacewar instead.
pub const SPACEWAR_WARNING: &str = "Keep RampageRE open while you do this. If the real Spacewar game starts instead, RampageRE wasn't running: close Spacewar, click Join on Steam again, and wait for RampageRE to open before you click Join Game.";

#[derive(Clone, Copy, PartialEq)]
pub enum SteamAction {
    Host,
    Join,
}

impl App {
    pub fn steam_host(&mut self) { self.steam(SteamAction::Host) }
    pub fn steam_join(&mut self) { self.steam(SteamAction::Join) }

    /// The first time: what Spacewar is. Then the checks (on a thread: the ownership check asks Steam), then the game.
    fn steam(&mut self, action: SteamAction) {
        let dir = match &self.game {
            Ok(g) => g.parent().map(|d| d.to_path_buf()).unwrap_or_default(),
            Err(e) => return self.say(&e.clone(), true),
        };
        if !self.settings.spacewar_card_shown {
            self.settings = Settings::update(|s| s.spacewar_card_shown = true);
            self.card = Some(Card::Spacewar(Some(action)));
            return;
        }
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            let _ = tx.send(steam::check(&dir));
        });
        self.steam_check = Some((action, rx));
        self.steam_problem = None;
        self.say("Checking Steam...", false);
    }

    pub fn poll_steam(&mut self) {
        let Some((action, rx)) = &self.steam_check else { return };
        let Ok(r) = rx.try_recv() else { return };
        let action = *action;
        self.steam_check = None;
        self.note = None;
        if let Err(p) = r {
            self.steam_problem = Some(p);
            return;
        }
        let Ok(g) = self.game.clone() else { return };
        let start = if action == SteamAction::Host { Start::SteamHost } else { Start::SteamWait };
        match game::start(&g, &start) {
            Ok(_) => self.card = Some(if action == SteamAction::Host { Card::ShiftTab } else { Card::JoinGame }),
            Err(e) => self.say(&format!("Couldn't start RampageRE: {e}"), true),
        }
    }

    /// The last check's problem, with its fix.
    pub fn steam_problem_line(&mut self, ui: &mut egui::Ui) {
        let Some(p) = self.steam_problem.clone() else { return };
        ui.label(RichText::new(steam::problem_text(&p)).size(15.0).color(RED));
        match p {
            SteamProblem::NotRunning => {
                if ui.button("Open Steam").clicked() {
                    steam::open_steam();
                    self.steam_problem = None;
                }
            }
            SteamProblem::NoSpacewar => {
                ui.label(RichText::new(GET_SPACEWAR_NOTE).small().color(DIM_TEXT));
                if ui.button("Get Spacewar").clicked() {
                    steam::get_spacewar();
                    self.steam_problem = None;
                }
            }
            SteamProblem::FilesMissing => {}
        }
    }

    /// The card showing (a window in the middle), if any.
    pub fn card_window(&mut self, ctx: &egui::Context) {
        let Some(card) = self.card.clone() else { return };
        let title = match card {
            Card::Firewall => "Before you host",
            Card::Spacewar(_) => "What's Spacewar?",
            Card::ShiftTab => "Invite your friends",
            Card::JoinGame => "Join your friend",
        };
        let mut close = false;
        let mut then: Option<Box<dyn FnOnce(&mut App)>> = None;
        egui::Window::new(title).anchor(Align2::CENTER_CENTER, Vec2::ZERO).collapsible(false).resizable(false)
            .show(ctx, |ui| {
                ui.set_max_width(420.0);
                match card {
                    Card::Firewall => {
                        ui.label(RichText::new(super::host::FIREWALL_TEXT).size(16.0));
                        ui.add_space(4.0);
                        let ok = ui.vertical_centered(|ui| ui.button(RichText::new("OK, host my game").size(16.0).strong())
                            .clicked()).inner;
                        if ok {
                            close = true;
                            then = Some(Box::new(|app: &mut App| {
                                app.settings = Settings::update(|s| s.firewall_note_shown = true);
                                app.host_own();
                            }));
                        }
                    }
                    Card::Spacewar(next) => {
                        for (i, para) in SPACEWAR_TEXT.iter().enumerate() {
                            if i > 0 {
                                ui.add_space(4.0);
                            }
                            ui.label(RichText::new(*para).size(15.0));
                        }
                        ui.add_space(6.0);
                        ui.vertical_centered(|ui| {
                            let text = if next.is_some() { "Got it" } else { "Close" };
                            if ui.button(RichText::new(text).size(16.0).strong()).clicked() {
                                close = true;
                                if let Some(a) = next {
                                    then = Some(Box::new(move |app: &mut App| app.steam(a)));
                                }
                            }
                        });
                    }
                    Card::ShiftTab => {
                        ui.label(RichText::new(SHIFT_TAB_TEXT).size(17.0).strong());
                        ui.label(RichText::new("The game's panel (Insert) has an Invite button too.").color(DIM_TEXT));
                        close = ui.vertical_centered(|ui| ui.button(RichText::new("OK").size(16.0).strong()).clicked()).inner;
                    }
                    Card::JoinGame => {
                        ui.label(RichText::new(SPACEWAR_WARNING).size(15.0).color(YELLOW));
                        ui.add_space(4.0);
                        ui.label(RichText::new(JOIN_GAME_TEXT).size(17.0).strong());
                        ui.label(RichText::new("Or accept their invite in Steam chat.").color(DIM_TEXT));
                        friends_picture(ui);
                        close = ui.vertical_centered(|ui| ui.button(RichText::new("OK").size(16.0).strong()).clicked()).inner;
                    }
                }
            });
        if close {
            self.card = None;
        }
        if let Some(f) = then {
            f(self);
        }
    }
}

/// A friends list with a right-click menu on the second friend, "Join Game" lit: drawn, not a screenshot.
fn friends_picture(ui: &mut egui::Ui) {
    let (resp, p) = ui.allocate_painter(Vec2::new(400.0, 190.0), Sense::hover());
    let r = resp.rect;
    let list = Rect::from_min_size(r.min, Vec2::new(210.0, 190.0));
    p.rect_filled(list, Rounding::same(6.0), Color32::from_rgb(28, 32, 40));
    p.text(list.min + Vec2::new(12.0, 10.0), Align2::LEFT_TOP, "FRIENDS", egui::FontId::proportional(13.0), DIM_TEXT);
    let friends = [("Alex", "Online"), ("Sam", "Playing Spacewar"), ("Jo", "Online")];
    let row_y = |i: usize| list.min.y + 38.0 + i as f32 * 48.0;
    for (i, (name, state)) in friends.iter().enumerate() {
        let y = row_y(i);
        if i == 1 {
            p.rect_filled(Rect::from_min_size(Pos2::new(list.min.x + 4.0, y - 4.0), Vec2::new(202.0, 44.0)),
                          Rounding::same(4.0), Color32::from_rgb(52, 60, 76));
        }
        p.circle_filled(Pos2::new(list.min.x + 26.0, y + 18.0), 15.0,
                        [Color32::from_rgb(90, 140, 200), Color32::from_rgb(110, 190, 90), Color32::from_rgb(200, 120, 80)][i]);
        p.text(Pos2::new(list.min.x + 50.0, y + 4.0), Align2::LEFT_TOP, *name, egui::FontId::proportional(15.0), Color32::WHITE);
        let c = if i == 1 { Color32::from_rgb(140, 200, 90) } else { Color32::from_rgb(90, 160, 220) };
        p.text(Pos2::new(list.min.x + 50.0, y + 22.0), Align2::LEFT_TOP, *state, egui::FontId::proportional(12.0), c);
    }
    // the right-click menu, beside Sam
    let menu = Rect::from_min_size(Pos2::new(list.max.x + 18.0, row_y(1) - 14.0), Vec2::new(160.0, 112.0));
    p.rect_filled(menu, Rounding::same(4.0), Color32::from_rgb(40, 44, 52));
    p.rect_stroke(menu, Rounding::same(4.0), Stroke::new(1.0, Color32::from_rgb(70, 76, 90)));
    for (i, item) in ["Send a message", "Join Game", "View profile", "Invite to game"].iter().enumerate() {
        let row = Rect::from_min_size(menu.min + Vec2::new(4.0, 4.0 + i as f32 * 26.0), Vec2::new(152.0, 24.0));
        if i == 1 {
            p.rect_filled(row, Rounding::same(3.0), YELLOW);
        }
        let color = if i == 1 { Color32::from_rgb(26, 18, 0) } else { Color32::from_rgb(220, 220, 225) };
        p.text(row.left_center() + Vec2::new(10.0, 0.0), Align2::LEFT_CENTER, *item, egui::FontId::proportional(14.0), color);
    }
    // the pointer, on Join Game
    let tip = Pos2::new(menu.min.x + 110.0, menu.min.y + 38.0);
    p.add(egui::Shape::convex_polygon(vec![tip, tip + Vec2::new(0.0, 18.0), tip + Vec2::new(5.0, 13.0), tip + Vec2::new(12.0, 13.0)],
                                      Color32::WHITE, Stroke::new(1.0, Color32::BLACK)));
}
