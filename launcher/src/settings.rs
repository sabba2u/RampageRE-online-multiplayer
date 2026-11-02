//! What the launcher remembers, in %LOCALAPPDATA%\RampageRE\launcher.json. Missing or broken: the defaults.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

const SERVERS_KEPT: usize = 8;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    /// the group servers used ("http://host:port"), newest first
    pub servers: Vec<String>,
    pub first_run_done: bool,
    pub firewall_note_shown: bool,
    pub spacewar_card_shown: bool,
    /// players banned from my hosted games (the launcher's player list): by IP, with the name they had
    pub banned: Vec<Banned>,
    /// Play Local's "Players" choice, 1 to 4 (0: never chosen, 2)
    pub local_players: u8,
    /// every monster (and city) unlocked: the game's own cheat 141421, passed at each start; not saved in the game's
    /// profile, so unticked they are as the profile has them again
    pub unlock_all: bool,
    /// "game" (as the game picks: the campaign's cities' own), "day" or "night": every city so (--time-of-day)
    pub time_of_day: String,
    /// smoother edges: "off" (the game's rampage.cfg decides), "medium" (SMAA), "high" (SMAA high and 4x MSAA)
    pub smoothing: String,
    /// "clear", "rain" or "storm" (rain, lightning and thunder) over every city (--weather)
    pub weather: String,
    /// "normal" or "film" (a 1950s monster movie: black and white, scratches, grain, vignette: --film)
    pub look: String,
}

impl Default for Settings {
    /// Unlock all monsters on; the rest empty, off, never chosen.
    fn default() -> Settings {
        Settings { servers: Vec::new(), first_run_done: false, firewall_note_shown: false, spacewar_card_shown: false,
                   banned: Vec::new(), local_players: 0, unlock_all: true, time_of_day: "game".into(),
                   smoothing: "off".into(), weather: "clear".into(),
                   look: "normal".into() }
    }
}

#[derive(Debug, Default, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Banned {
    pub ip: String,
    pub name: String,
}

pub fn path() -> PathBuf {
    let base = std::env::var_os("LOCALAPPDATA").map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
    base.join("RampageRE").join("launcher.json")
}

impl Settings {
    pub fn load() -> Settings { Settings::load_from(&path()) }
    pub fn save(&self) -> std::io::Result<()> { self.save_to(&path()) }

    pub fn load_from(file: &Path) -> Settings {
        std::fs::read_to_string(file).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default()
    }

    pub fn save_to(&self, file: &Path) -> std::io::Result<()> {
        if let Some(dir) = file.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(file, serde_json::to_string_pretty(self).unwrap())
    }

    /// A change made to what is on disk now (a clicked link may have saved since this window read it), saved;
    /// the result is the new state.
    pub fn update_at(file: &Path, f: impl FnOnce(&mut Settings)) -> Settings {
        let mut s = Settings::load_from(file);
        f(&mut s);
        let _ = s.save_to(file);
        s
    }

    pub fn update(f: impl FnOnce(&mut Settings)) -> Settings { Settings::update_at(&path(), f) }

    pub fn local_players(&self) -> u8 { if (1..=4).contains(&self.local_players) { self.local_players } else { 2 } }
    /// 3 or 4 players: the game's menu gets 3 Player and 4 Player (--four-players)
    pub fn four_players(&self) -> bool { self.local_players() >= 3 }

    /// `ip` banned (once: banned again, its newest name kept).
    pub fn ban(&mut self, ip: &str, name: &str) {
        match self.banned.iter_mut().find(|b| b.ip == ip) {
            Some(b) => b.name = name.to_string(),
            None => self.banned.push(Banned { ip: ip.to_string(), name: name.to_string() }),
        }
    }
    pub fn unban(&mut self, ip: &str) { self.banned.retain(|b| b.ip != ip); }
    pub fn banned_ips(&self) -> Vec<String> { self.banned.iter().map(|b| b.ip.clone()).collect() }

    /// `server` to the front; the oldest drop off past SERVERS_KEPT.
    pub fn remember(&mut self, server: &str) {
        self.servers.retain(|s| s != server);
        self.servers.insert(0, server.to_string());
        self.servers.truncate(SERVERS_KEPT);
    }
}
