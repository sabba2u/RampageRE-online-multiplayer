//! After joining a room by its code: the room's code and who is in it (the server says it every 2 s), as the host
//! sees them but without the host's buttons, until the game closes or the player leaves.
use super::look::{self, DIM_TEXT, YELLOW};
use super::{join, players, App};
use crate::lobby::games::Player;
use eframe::egui::{self, RichText};
use std::process::Child;
use std::sync::mpsc::{channel, Receiver};
use std::time::{Duration, Instant};

const ASK_EVERY: Duration = Duration::from_secs(2);
pub const GAME_CLOSED: &str = "The game closed.";

pub struct Guest {
    pub server: String,
    pub code: String,
    game: Child,
    roster: Vec<Player>,
    problem: Option<String>,
    asking: Option<Receiver<Result<Vec<Player>, String>>>,
    asked: Option<Instant>,
}

impl Guest {
    pub fn new(server: String, code: String, game: Child) -> Guest {
        Guest { server, code, game, roster: Vec::new(), problem: None, asking: None, asked: None }
    }
}

impl App {
    /// Each frame while in a room: the game still running, the server asked again every ASK_EVERY (on a thread).
    pub fn poll_guest(&mut self) {
        let Some(g) = &mut self.guest else { return };
        if !matches!(g.game.try_wait(), Ok(None)) {
            self.guest = None;
            return self.say_on(1, GAME_CLOSED, false);
        }
        if let Some(rx) = &g.asking {
            if let Ok(r) = rx.try_recv() {
                match r {
                    Ok(p) => { g.roster = p; g.problem = None; }
                    Err(e) => g.problem = Some(e),
                }
                g.asking = None;
            }
        }
        if g.asking.is_none() && g.asked.map_or(true, |t| t.elapsed() >= ASK_EVERY) {
            let (tx, rx) = channel();
            let (server, code) = (g.server.clone(), g.code.clone());
            std::thread::spawn(move || {
                let _ = tx.send(join::fetch_players(&server, &code));
            });
            g.asking = Some(rx);
            g.asked = Some(Instant::now());
        }
    }

    pub fn guest_box(&mut self, ui: &mut egui::Ui) {
        let Some(g) = &mut self.guest else { return };
        ui.label(RichText::new("You're in room").color(DIM_TEXT));
        ui.label(RichText::new(&g.code).size(40.0).strong().color(YELLOW).monospace());
        ui.label(RichText::new(format!("on server {}", g.server.trim_start_matches("http://"))).small().color(DIM_TEXT));
        ui.separator();
        look::section(ui, "Players");
        if g.roster.is_empty() {
            ui.label(RichText::new("Asking the server who's in...").color(DIM_TEXT));
        } else {
            players::guest_rows(ui, &g.roster);
        }
        if let Some(p) = &g.problem {
            ui.label(RichText::new(p).small().color(look::RED));
        }
        ui.label(RichText::new("The host starts the game from their menus: your screen follows.").small().color(DIM_TEXT));
        ui.separator();
        if ui.button("Leave the room").on_hover_text("Closes your game").clicked() {
            let _ = g.game.kill();
            let _ = g.game.wait();
            self.guest = None;
            self.say_on(1, "You left the room.", false);
        }
    }
}
