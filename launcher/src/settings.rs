//! What the launcher remembers, in %LOCALAPPDATA%\RampageRE\launcher.json. Missing or broken: the defaults.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

const SERVERS_KEPT: usize = 8;
/// hosting's usual port (the lobby's TCP and the game's UDP: src/netwire.h NET_PORT)
pub const HOST_PORT: u16 = 19980;

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
    /// Play Local's "Players" choice, 1 to 8 (0: never chosen, 2; 5 to 8 host: the rest join over the network)
    pub local_players: u8,
    /// every monster (and city) unlocked: the game's own cheat 141421, passed at each start; not saved in the game's
    /// profile, so unticked they are as the profile has them again
    pub unlock_all: bool,
    /// every monster's upgrade moves usable from the start: the game's own cheat 011235, passed at each start (not
    /// saved in the profile: unticked, each monster has the upgrades it earned)
    pub unlock_moves: bool,
    /// "game" (as the game picks: the campaign's cities' own), "day" or "night": every city so (--time-of-day)
    pub time_of_day: String,
    /// smoother edges: "off" (the game's rampage.cfg decides), "medium" (SMAA), "high" (SMAA high and 4x MSAA)
    pub smoothing: String,
    /// "clear", "rain" or "storm" (rain, lightning and thunder) over every city (--weather)
    pub weather: String,
    /// "normal" or "film" (a 1950s monster movie: black and white, scratches, grain, vignette: --film)
    pub look: String,
    /// each city's buildings shuffled (the same width, as tall or taller), a new seed every game (--random-blocks)
    pub shuffle_buildings: bool,
    /// a thin outline in your player's colour around your own monster, on your screen only (--own-outline)
    pub own_outline: bool,
    /// the host forwarded the router's ports by hand (19980, TCP and UDP): the router's own opening (UPnP) failing
    /// no longer means "only your home network"
    pub forwarded_by_hand: bool,
    /// what internet friends type to reach this PC (a name like sabba2u.no-ip.org that follows a changing address),
    /// maybe with the port hosting uses (":19990"; 19980 when left out); empty: the address the internet sees
    pub public_name: String,
}

impl Default for Settings {
    /// Unlock all monsters and Unlock all moves on; the rest empty, off, never chosen.
    fn default() -> Settings {
        Settings { servers: Vec::new(), first_run_done: false, firewall_note_shown: false, spacewar_card_shown: false,
                   banned: Vec::new(), local_players: 0, unlock_all: true, unlock_moves: true, time_of_day: "game".into(),
                   smoothing: "off".into(), weather: "clear".into(),
                   look: "normal".into(), shuffle_buildings: false, own_outline: false,
                   forwarded_by_hand: false, public_name: String::new() }
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

    pub fn local_players(&self) -> u8 { if (1..=8).contains(&self.local_players) { self.local_players } else { 2 } }
    /// 3 or 4 players: the game's menu gets 3 Player and 4 Player (--four-players)
    pub fn four_players(&self) -> bool { self.local_players() >= 3 }
    /// 2, or 5 to 8: the top button hosts (friends join this PC; one PC takes four controllers), else it plays here
    pub fn hosts(&self) -> bool { matches!(self.local_players(), 2 | 5..=8) }

    /// `ip` banned (once: banned again, its newest name kept).
    pub fn ban(&mut self, ip: &str, name: &str) {
        match self.banned.iter_mut().find(|b| b.ip == ip) {
            Some(b) => b.name = name.to_string(),
            None => self.banned.push(Banned { ip: ip.to_string(), name: name.to_string() }),
        }
    }
    /// "Address friends use", read: the name ("" for the address the internet sees) and the port hosting uses, 19980
    /// unless one is added ("sabba2u.no-ip.org:19990", or just ":19990"). None: not an address (hosting: 19980).
    pub fn host_address(&self) -> Option<(String, u16)> {
        let s = self.public_name.trim();
        if s.is_empty() {
            return Some((String::new(), HOST_PORT));
        }
        if let Some(p) = s.strip_prefix(':') {
            return p.parse::<u16>().ok().filter(|n| *n > 0).map(|n| (String::new(), n));
        }
        crate::lobby::addr::split_host_port(s).map(|(h, p)| (h, p.unwrap_or(HOST_PORT)))
    }
    /// The port hosting uses: the lobby's TCP and the game's UDP (one router entry, "Both").
    pub fn host_port(&self) -> u16 { self.host_address().map_or(HOST_PORT, |a| a.1) }
    pub fn unban(&mut self, ip: &str) { self.banned.retain(|b| b.ip != ip); }
    pub fn banned_ips(&self) -> Vec<String> { self.banned.iter().map(|b| b.ip.clone()).collect() }

    /// `server` to the front; the oldest drop off past SERVERS_KEPT.
    pub fn remember(&mut self, server: &str) {
        self.servers.retain(|s| s != server);
        self.servers.insert(0, server.to_string());
        self.servers.truncate(SERVERS_KEPT);
    }
}
