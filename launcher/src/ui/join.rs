//! Joining: what a code, a group link or an address starts, and the host's version checked first (a guest with
//! another build would be refused by the host; better said plainly before the game starts).
use crate::game::Start;
use crate::links::Link;
use crate::settings::Settings;
use std::time::Duration;

pub const NO_SERVER: &str = "Connect to your host's server first (type its address above, or paste their link).";
pub const CANT_REACH: &str = "Can't reach that server: check the address with your host.";
pub const NO_SUCH_CODE: &str = "No room with that code on this server: check the room code with your host.";
pub const DIFFERENT_VERSION: &str = "Your friend has a different version of RampageRE: you both need the same release.";

/// The game's start for `link`, and the server to remember (a group link's).
pub fn join_target(link: &Link, settings: &Settings) -> Result<(Start, Option<String>), String> {
    match link {
        Link::Code(code) => {
            let server = settings.servers.first().ok_or(NO_SERVER)?;
            Ok((Start::Join { target: code.clone(), lobby: Some(server.clone()) }, None))
        }
        Link::CodeAt { code, server } => {
            Ok((Start::Join { target: code.clone(), lobby: Some(server.clone()) }, Some(server.clone())))
        }
        Link::Direct(addr) => Ok((Start::Join { target: addr.clone(), lobby: None }, None)),
    }
}

/// The game `code` on `server` runs the same build as this PC's game (`mine`, "%08X").
pub fn check_build(server: &str, code: &str, mine: &str) -> Result<(), String> {
    let agent = ureq::AgentBuilder::new().timeout(Duration::from_secs(4)).build();
    let body = match agent.get(&format!("{server}/games/{code}")).call() {
        Ok(r) => r.into_string().map_err(|_| CANT_REACH.to_string())?,
        Err(ureq::Error::Status(404, _)) => return Err(NO_SUCH_CODE.into()),
        Err(_) => return Err(CANT_REACH.into()),
    };
    let v: serde_json::Value = serde_json::from_str(&body).map_err(|_| CANT_REACH.to_string())?;
    match v["build"].as_str() {
        Some(b) if b.eq_ignore_ascii_case(mine) => {}
        Some(_) => return Err(DIFFERENT_VERSION.into()),
        None => return Err(CANT_REACH.into()),
    }
    // the host's game has the Wii's monsters and cities and this PC hasn't: it couldn't show them
    if v["content"].as_str().is_some_and(|c| c.contains("wii")) && !here_has_wii_data() {
        return Err(NEEDS_WII.into());
    }
    Ok(())
}

pub const NEEDS_WII: &str = "This room uses the Wii's extra monsters and cities, and this PC doesn't have the Wii data: set wii_data= in rampage.cfg to the folder of the Wii disc's files (or ask your host to play without it).";

/// The game first (nothing else matters without it), then what `link` starts and the server to remember.
pub fn plan_join(link: &Link, settings: &Settings, game: &Result<std::path::PathBuf, String>)
                 -> Result<(std::path::PathBuf, Start, Option<String>), String> {
    let game = game.clone()?;
    let (start, remember) = join_target(link, settings)?;
    Ok((game, start, remember))
}

/// The version check (for a code), then the game started: the window's Join and a clicked link both end here.
pub fn run_join(game: &std::path::Path, build: &str, start: &Start) -> Result<std::process::Child, String> {
    if let Start::Join { target, lobby: Some(server) } = start {
        check_build(server, target, build)?;
    }
    crate::game::start(game, start).map_err(|e| format!("Couldn't start RampageRE: {e}"))
}

/// Connect: the address answers as a RampageRE server (GET /health).
pub fn check_server(server: &str) -> Result<(), String> {
    let agent = ureq::AgentBuilder::new().timeout(Duration::from_secs(4)).build();
    match agent.get(&format!("{server}/health")).call().ok().and_then(|r| r.into_string().ok()) {
        Some(b) if b.starts_with("rampage_lobby") => Ok(()),
        _ => Err(CANT_REACH.into()),
    }
}

pub const GAME_GONE: &str = "That room isn't on the server any more.";

/// Who is in game `code` on `server` (the host's game says it every second): names and states, no addresses.
pub fn fetch_players(server: &str, code: &str) -> Result<Vec<crate::lobby::games::Player>, String> {
    let agent = ureq::AgentBuilder::new().timeout(Duration::from_secs(3)).build();
    let body = match agent.get(&format!("{server}/games/{code}/players")).call() {
        Ok(r) => r.into_string().map_err(|_| CANT_REACH.to_string())?,
        Err(ureq::Error::Status(404, _)) => return Err(GAME_GONE.into()),
        Err(_) => return Err(CANT_REACH.into()),
    };
    let v: serde_json::Value = serde_json::from_str(&body).map_err(|_| CANT_REACH.to_string())?;
    let list = v["players"].as_array().ok_or(CANT_REACH)?;
    Ok(list.iter().map(|p| crate::lobby::games::Player {
        slot: p["slot"].as_u64().unwrap_or(0) as u8,
        state: p["state"].as_str().unwrap_or("").chars().take(12).collect(),
        addr: String::new(),
        ping: p["ping"].as_u64().unwrap_or(0) as u32,
        name: p["name"].as_str().unwrap_or("").chars().take(32).collect(),
    }).take(8).collect())
}

/// A room on a server's list (GET /games): what the launcher shows to pick from.
#[derive(Clone, Debug, PartialEq)]
pub struct Room {
    pub code: String,
    pub name: String,
    pub monster: String,
    pub city: String,
    pub players: u32,
    pub max: u32,
    pub reserved: u32,
    /// where a guest's game goes ("192.168.7.105:19980"): its port the host game's
    pub join: String,
    /// the host game's release ("1.3.0") and build id ("CAFE1234"); "" from an older game or server
    pub version: String,
    pub build: String,
    /// "pc"; the game data it has: "ps2", or "ps2+wii" (the Wii's 40 monsters and its cities)
    pub platform: String,
    pub content: String,
    /// "Boss Rumble", "" in the menus; "Boss Rumble wave 7", "In the menus", "Results" ...
    pub mode: String,
    pub status: String,
    /// false: no one may come in now (a Boss Rumble run keeps its seats); true when the server doesn't say
    pub joinable: bool,
    /// the seconds this game has run (0 none); when the room opened (ms since 1970, the server's clock)
    pub playing: u64,
    pub since: u64,
    pub weather: String,
    pub tod: String,
}

/// "6:12", "1:02:03"
fn clock(s: u64) -> String {
    if s >= 3600 { format!("{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60) } else { format!("{}:{:02}", s / 60, s % 60) }
}

/// "42 min", "3 h 5 min", "under a minute"
fn span(s: u64) -> String {
    if s < 60 { "under a minute".into() } else if s < 3600 { format!("{} min", s / 60) }
    else { format!("{} h {} min", s / 3600, s / 60 % 60) }
}

fn upper_first(s: &str) -> String {
    let mut c = s.chars();
    c.next().map_or(String::new(), |f| f.to_uppercase().collect::<String>() + c.as_str())
}

/// This PC's game has the Wii's data (as the Windows game finds it, src/main.c): rampage.cfg's wii_data= a folder
/// that is there ("none", or no line: not used; the game asks for it the first time it starts)
pub fn has_wii_data(dir: &std::path::Path) -> bool {
    let cfg = std::fs::read_to_string(dir.join("rampage.cfg")).unwrap_or_default();
    cfg.lines().filter_map(|l| l.trim().strip_prefix("wii_data=")).last()
        .map(|v| v.trim()).is_some_and(|v| !v.is_empty() && !v.eq_ignore_ascii_case("none") && std::path::Path::new(v).is_dir())
}

/// has_wii_data for the launcher's own folder (the game's), looked at once
pub fn here_has_wii_data() -> bool {
    static HAS: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *HAS.get_or_init(|| std::env::current_exe().ok().and_then(|p| p.parent().map(has_wii_data)).unwrap_or(false))
}

impl Room {
    /// "sabba · George · Chicago 1", the parts it has; "A room" with none
    pub fn label(&self) -> String {
        let parts: Vec<&str> = [&self.name, &self.monster, &self.city].iter().map(|s| s.as_str())
            .filter(|s| !s.is_empty()).collect();
        if parts.is_empty() { "A room".into() } else { parts.join(" · ") }
    }
    /// "2 / 4", "2 / 4 (1 rejoining)"
    pub fn seats(&self) -> String {
        if self.reserved > 0 { format!("{} / {} ({} rejoining)", self.players, self.max, self.reserved) }
        else { format!("{} / {}", self.players, self.max) }
    }
    pub fn full(&self) -> bool { self.players + self.reserved >= self.max }
    /// "v1.3.0" ("v?" from an older game)
    pub fn version_shown(&self) -> String {
        format!("v{}", if self.version.is_empty() { "?" } else { &self.version })
    }
    /// the hover card: version and build, platform, game data, mode and status, its times, weather (now_ms: this
    /// PC's clock, for how long the room has been open)
    pub fn details(&self, now_ms: u64) -> String {
        let mut l = Vec::new();
        let mut v = format!("Version {}", if self.version.is_empty() { "unknown" } else { &self.version });
        if !self.build.is_empty() { v += &format!(" (build {})", self.build); }
        if !self.platform.is_empty() { v += &format!(" · {}", self.platform.to_uppercase()); }
        l.push(v);
        l.push(match self.content.as_str() {
            "" => "Game data: unknown".to_string(),
            c if c.contains("wii") => "Game data: PS2 + Wii (the Wii's 40 monsters and its cities)".into(),
            c => format!("Game data: {}", c.to_uppercase()),
        });
        match (self.mode.as_str(), self.status.as_str()) {
            ("", "") => {}
            ("", s) | (s, "") => l.push(s.to_string()),
            (m, s) if s.starts_with(m) => l.push(s.to_string()),
            (m, s) => l.push(format!("{m}: {s}")),
        }
        let mut t = Vec::new();
        if self.playing > 0 { t.push(format!("This game {}", clock(self.playing))); }
        if self.since > 0 && now_ms > self.since { t.push(format!("room open {}", span((now_ms - self.since) / 1000))); }
        if !t.is_empty() { l.push(upper_first(&t.join(" · "))); }
        let w: Vec<&str> = [self.weather.as_str(), self.tod.as_str()].into_iter().filter(|s| !s.is_empty()).collect();
        if !w.is_empty() { l.push(format!("Weather: {}", w.join(", "))); }
        if !self.joinable { l.push("Not taking players right now".into()); }
        l.join("\n")
    }
    /// why this PC can't join it now (None: it can): full, a run under way, another version, the Wii's data
    pub fn why_not(&self, my_version: &str, have_wii: bool) -> Option<String> {
        if self.full() { return Some("This room is full.".into()); }
        if !self.joinable {
            return Some(if self.mode == "Boss Rumble" { "A Boss Rumble run is under way: its seats open when it ends.".into() }
                        else { "This room isn't taking players right now.".into() });
        }
        if !self.version.is_empty() && self.version != my_version {
            return Some(format!("This room runs RampageRE {} and you have {}: you both need the same release.",
                                self.version, my_version));
        }
        if self.content.contains("wii") && !have_wii {
            return Some("This room uses the Wii's extra monsters and cities: this PC doesn't have the Wii data (rampage.cfg's wii_data: the folder of the Wii disc's files).".into());
        }
        None
    }
}

pub fn parse_rooms(body: &str) -> Result<Vec<Room>, String> {
    let v: serde_json::Value = serde_json::from_str(body).map_err(|_| CANT_REACH.to_string())?;
    let list = v["games"].as_array().ok_or(CANT_REACH)?;
    let s = |g: &serde_json::Value, k: &str| g[k].as_str().unwrap_or("").chars().take(32).collect::<String>();
    let n = |g: &serde_json::Value, k: &str| g[k].as_u64().unwrap_or(0).min(8) as u32;
    let big = |g: &serde_json::Value, k: &str| g[k].as_u64().unwrap_or(0);
    Ok(list.iter().filter(|g| g["code"].is_string()).map(|g| Room {
        code: s(g, "code"), name: s(g, "name"), monster: s(g, "monster"), city: s(g, "city"),
        players: n(g, "players"), max: n(g, "max"), reserved: n(g, "reserved"), join: s(g, "join"),
        version: s(g, "version"), build: s(g, "build"), platform: s(g, "platform"), content: s(g, "content"),
        mode: s(g, "mode"), status: s(g, "status"), joinable: g["joinable"].as_u64().map_or(true, |j| j != 0),
        playing: big(g, "playing"), since: big(g, "since"), weather: s(g, "weather"), tod: s(g, "tod"),
    }).take(50).collect())
}

/// The rooms on `server` now.
pub fn fetch_rooms(server: &str) -> Result<Vec<Room>, String> {
    let agent = ureq::AgentBuilder::new().timeout(Duration::from_secs(3)).build();
    let body = agent.get(&format!("{server}/games")).call().ok().and_then(|r| r.into_string().ok())
        .ok_or_else(|| CANT_REACH.to_string())?;
    parse_rooms(&body)
}

pub const NO_PUBLIC_IP: &str = "Couldn't find your internet address (no internet, or the lookup service is down).";

/// An address lookup's answer: the address alone, or why not.
pub fn public_ip_from(body: &str) -> Result<String, String> {
    let t = body.trim();
    t.parse::<std::net::IpAddr>().map(|ip| ip.to_string()).map_err(|_| NO_PUBLIC_IP.to_string())
}

/// Services that answer with the caller's address alone. Plain http: the launcher's web client has no HTTPS.
pub const PUBLIC_IP_SERVICES: [&str; 3] = ["http://api.ipify.org", "http://checkip.amazonaws.com", "http://icanhazip.com"];

/// This connection's address on the internet, as a lookup service sees it: the first that answers.
pub fn public_ip() -> Result<String, String> {
    let agent = ureq::AgentBuilder::new().timeout(Duration::from_secs(4)).build();
    PUBLIC_IP_SERVICES.iter()
        .find_map(|url| agent.get(url).call().ok().and_then(|r| r.into_string().ok()).and_then(|b| public_ip_from(&b).ok()))
        .ok_or_else(|| NO_PUBLIC_IP.to_string())
}

/// What the player typed in the Join box: a code, or a pasted group link.
pub fn typed(input: &str) -> Result<Link, String> {
    let s = input.trim();
    if s.len() >= 10 && s.is_char_boundary(10) && s[..10].eq_ignore_ascii_case("rampage://") {
        return crate::links::parse_link(s);
    }
    if crate::lobby::games::is_code(s) {
        return Ok(Link::Code(s.to_uppercase()));
    }
    Err("Type the 5-letter room code, or paste your host's link.".into())
}

/// A rampage:// link clicked anywhere: no window, the game joins (errors in a message box, by the caller).
pub fn open_link(url: &str) -> Result<(), String> {
    let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.to_path_buf())).unwrap_or_default();
    open_link_in(url, &dir, &crate::settings::path())
}

/// open_link for the game in `dir` and the settings in `settings_file`. The link's server is remembered only once
/// the join went through: any web page can open a link.
pub fn open_link_in(url: &str, dir: &std::path::Path, settings_file: &std::path::Path) -> Result<(), String> {
    let link = crate::links::parse_link(url)?;
    let (game, start, remember) = plan_join(&link, &Settings::load_from(settings_file), &crate::game::find_game(dir))?;
    let build = crate::game::build_id(&game).map_err(|e| e.to_string())?;
    run_join(&game, &build, &start)?;
    if let Some(s) = remember {
        Settings::update_at(settings_file, |x| x.remember(&s));
    }
    Ok(())
}
