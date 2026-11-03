//! Join links and Settings. The first run (links not set up yet) asks once, in a pop-up of its own colour; after that
//! links are set up, and everything else changed, from the Settings window: the launcher's own (register / unregister
//! join links), hosting (the address and port friends use), the firewall, the group servers, the tips shown again,
//! where the game is.
use super::look::{self, DIM_TEXT, GREEN, RED, YELLOW};
use super::App;
use crate::links::{self, LinkStatus};
use crate::settings::{Settings, HOST_PORT};
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
                ui.vertical_centered(|ui| {
                    if look::half(ui, "REGISTER RAMPAGE:// LINKS", true).clicked() {
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

    /// Hosting: the address friends use (a port added: hosting uses that one), and the ports forwarded by hand.
    fn hosting_settings(&mut self, ui: &mut egui::Ui) {
        look::section(ui, "Hosting");
        let mut name = self.settings.public_name.clone();
        ui.horizontal(|ui| {
            ui.label("Address friends use:");
            let e = ui.add(egui::TextEdit::singleline(&mut name).hint_text("empty: your internet address")
                .desired_width(200.0));
            if e.changed() {
                self.settings = Settings::update(|s| s.public_name = name.trim().to_string());
            }
        }).response.on_hover_text("Like 192.168.7.105, sabba2u.no-ip.org (a name keeps working when your home's address changes), or with a port: sabba2u.no-ip.org:19990. No port: 19980.");
        match self.settings.host_address() {
            Some(_) => {
                let port = self.settings.host_port();
                let which = if port == HOST_PORT { "the usual port" } else { "your own port" };
                ui.label(RichText::new(format!("Hosting uses port {port} ({which}), TCP and UDP. Add :port to use another."))
                    .small().color(DIM_TEXT));
            }
            None => {
                ui.label(RichText::new("That isn't an address: type it like sabba2u.no-ip.org or 192.168.7.105:19990 (hosting uses port 19980 meanwhile).").small().color(RED));
            }
        }
        let mut by_hand = self.settings.forwarded_by_hand;
        if ui.checkbox(&mut by_hand, RichText::new("I forwarded my router's ports myself").strong())
            .on_hover_text(format!("Port {}, TCP and UDP (\"Both\"), to this PC, by hand: hosting won't say only your home network can join when the router doesn't open itself.", self.settings.host_port())).changed()
        {
            self.settings = Settings::update(|s| s.forwarded_by_hand = by_hand);
        }
    }

    pub fn settings_window(&mut self, ctx: &egui::Context) {
        if !self.settings_open {
            return;
        }
        let mut open = true;
        egui::Window::new("Settings").open(&mut open).anchor(Align2::CENTER_CENTER, Vec2::ZERO).collapsible(false).vscroll(true)
            .resizable(false).show(ctx, |ui| {
                ui.set_width(420.0);
                look::section(ui, "Launcher");
                ui.label(RichText::new("rampage:// links from friends open the game and join their room.").color(DIM_TEXT));
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
                        if look::half(ui, "Register join links", true).clicked() {
                            self.set_up_links();
                        }
                    }
                }
                ui.separator();
                self.hosting_settings(ui);
                ui.separator();
                look::section(ui, "Firewall");
                ui.label(RichText::new("Lets friends reach your game and server when you host (only the host needs it).")
                    .color(DIM_TEXT));
                ui.horizontal(|ui| self.firewall_button(ui));
                if let Some((text, problem)) = &self.fw_note {
                    ui.label(RichText::new(text).small().color(if *problem { RED } else { GREEN }));
                }
                ui.separator();
                look::section(ui, "Servers");
                if self.settings.servers.is_empty() {
                    ui.label(RichText::new("None yet: connecting to a server adds it.").color(DIM_TEXT));
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
                look::section(ui, "Banned players");
                if self.settings.banned.is_empty() {
                    ui.label(RichText::new("Nobody. Ban someone from the player list while you host.").color(DIM_TEXT));
                }
                let mut unban = None;
                for b in &self.settings.banned {
                    ui.horizontal(|ui| {
                        let who = if b.name.is_empty() { b.ip.clone() } else { format!("{}  ({})", b.name, b.ip) };
                        ui.label(who);
                        if ui.small_button("Unban").clicked() {
                            unban = Some(b.ip.clone());
                        }
                    });
                }
                if let Some(ip) = unban {
                    self.settings = Settings::update(|s| s.unban(&ip));
                    self.send_bans();
                }
                ui.separator();
                look::section(ui, "Tips");
                let tips = ui.vertical_centered(|ui| {
                    ui.button("Show the tips again").on_hover_text("The join links pop-up, the firewall note, What's Spacewar?")
                        .clicked()
                }).inner;
                if tips {
                    self.settings = Settings::update(|s| {
                        s.first_run_done = false;
                        s.firewall_note_shown = false;
                        s.spacewar_card_shown = false;
                    });
                    self.say("The tips will show again.", false);
                }
                ui.separator();
                look::section(ui, "Game");
                let mut unlock = self.settings.unlock_all;
                if ui.checkbox(&mut unlock, RichText::new("Unlock all monsters").strong())
                    .on_hover_text("Every monster (the Wii's 40 too) and every city, each time the game starts. Untick it and they're as your profile has them again.").changed()
                {
                    self.settings = Settings::update(|s| s.unlock_all = unlock);
                }
                let mut moves = self.settings.unlock_moves;
                if ui.checkbox(&mut moves, RichText::new("Unlock all moves").strong())
                    .on_hover_text("Every monster's upgrade moves from the start, each time the game starts. Untick it and each monster has only the upgrades it has earned.").changed()
                {
                    self.settings = Settings::update(|s| s.unlock_moves = moves);
                }
                let mut outline = self.settings.own_outline;
                if ui.checkbox(&mut outline, RichText::new("Outline my monster").strong())
                    .on_hover_text("A thin outline in your player's colour around your own monster, on your screen only: easy to find among up to 8.").changed()
                {
                    self.settings = Settings::update(|s| s.own_outline = outline);
                }
                let mut shuffle = self.settings.shuffle_buildings;
                if ui.checkbox(&mut shuffle, RichText::new("Shuffle the city's buildings").strong())
                    .on_hover_text("Every game a new street: each lot gets another of the city's buildings of its size.").changed()
                {
                    self.settings = Settings::update(|s| s.shuffle_buildings = shuffle);
                }
                // the time of day and smoother edges, passed at each start (the game's own settings when left alone)
                let mut tod = self.settings.time_of_day.clone();
                let mut smooth = self.settings.smoothing.clone();
                let mut weather = self.settings.weather.clone();
                let mut look_pick = self.settings.look.clone();
                ui.horizontal(|ui| {
                    ui.label("Time of day:");
                    egui::ComboBox::from_id_salt("tod").selected_text(match tod.as_str() {
                        "day" => "Always day", "night" => "Always night", _ => "As the game picks" })
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut tod, "game".to_string(), "As the game picks");
                            ui.selectable_value(&mut tod, "day".to_string(), "Always day");
                            ui.selectable_value(&mut tod, "night".to_string(), "Always night");
                        });
                });
                ui.horizontal(|ui| {
                    ui.label("Weather:");
                    egui::ComboBox::from_id_salt("weather").selected_text(match weather.as_str() {
                        "rain" => "Rain", "storm" => "Storm (rain, lightning, thunder)",
                        "random" => "Random (each block)", _ => "As the game picks" })
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut weather, "clear".to_string(), "As the game picks");
                            ui.selectable_value(&mut weather, "rain".to_string(), "Rain");
                            ui.selectable_value(&mut weather, "storm".to_string(), "Storm (rain, lightning, thunder)");
                            ui.selectable_value(&mut weather, "random".to_string(), "Random (each block)")
                                .on_hover_text("Clear, rain or a storm, picked anew for every block. Online the host's pick is everyone's.");
                        });
                });
                ui.horizontal(|ui| {
                    ui.label("Look:");
                    egui::ComboBox::from_id_salt("look").selected_text(if look_pick == "film" {
                        "1950s monster movie" } else { "Normal" })
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut look_pick, "normal".to_string(), "Normal");
                            ui.selectable_value(&mut look_pick, "film".to_string(), "1950s monster movie");
                        });
                });
                ui.horizontal(|ui| {
                    ui.label("Smooth edges:");
                    egui::ComboBox::from_id_salt("smooth").selected_text(match smooth.as_str() {
                        "medium" => "Medium", "high" => "High", _ => "Game's setting" })
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut smooth, "off".to_string(), "Game's setting");
                            ui.selectable_value(&mut smooth, "medium".to_string(), "Medium");
                            ui.selectable_value(&mut smooth, "high".to_string(), "High");
                        });
                });
                if tod != self.settings.time_of_day || smooth != self.settings.smoothing || weather != self.settings.weather
                    || look_pick != self.settings.look {
                    self.settings = Settings::update(|s| {
                        s.time_of_day = tod.clone();
                        s.smoothing = smooth.clone();
                        s.weather = weather.clone();
                        s.look = look_pick.clone();
                    });
                }
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
