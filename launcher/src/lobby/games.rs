//! Every listed game, in memory (lobby/src/Games.ts, line for line). A lobby that restarts forgets them, and their
//! next heartbeat (a 404) makes each host's game register again. The clock is injectable for the tests.
use super::addr::{is_ipv4, is_local, is_loopback, is_private, norm_ip, public_endpoint, split_host_port};
use rand::{rngs::OsRng, RngCore};
use serde::Serialize;
use serde_json::{Map, Value};
use std::collections::HashMap;

/// Join codes: short enough to say over voice chat, from letters and digits nobody mixes up (no 0 O 1 I).
pub const CODE_ALPHABET: &str = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
pub const CODE_LENGTH: usize = 5;
/// How often a host's game says it is still there, and how long the lobby waits before it drops a silent one.
pub const HEARTBEAT_S: u64 = 10;
pub const EXPIRE_S: u64 = 30;
/// A guest's knock waits this long for its host to collect it, and a game keeps this many at most.
pub const KNOCK_S: u64 = 15;
pub const KNOCKS_KEPT: usize = 8;
/// Per source address: registrations a minute, and games listed at once.
pub const REGISTER_PER_MINUTE: u64 = 6;
pub const GAMES_PER_ADDRESS: usize = 4;

pub fn is_code(s: &str) -> bool {
    s.chars().count() == CODE_LENGTH && s.to_uppercase().chars().all(|c| CODE_ALPHABET.contains(c))
}

/// What a host's game tells the lobby, checked; in a heartbeat anything may be left out (None).
#[derive(Debug, Default, Clone, PartialEq)]
pub struct Fields {
    pub name: Option<String>,
    pub monster: Option<String>,
    pub city: Option<String>,
    pub players: Option<u32>,
    pub max: Option<u32>,
    pub reserved: Option<u32>,
    pub port: Option<u16>,
    pub build: Option<String>,
    pub external: Option<String>,
    pub stun: Option<String>,
    pub lan: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Game {
    pub code: String,
    pub token: String,
    pub name: String,
    pub monster: String,
    pub city: String,
    pub players: u32,
    pub max: u32,
    pub reserved: u32,
    pub port: u16,
    pub build: String,
    /// where its registration came from; what UPnP said; what STUN saw; its own address on its network ("" none)
    pub source: String,
    pub external: String,
    pub stun: String,
    pub lan: String,
    pub since: u64,
    pub seen: u64,
    pub knocks: Vec<(String, u64)>,
}

/// What everyone sees of a game: no token, no source.
#[derive(Debug, Clone, Serialize)]
pub struct PublicGame {
    pub code: String,
    pub name: String,
    pub monster: String,
    pub city: String,
    pub players: u32,
    pub max: u32,
    pub reserved: u32,
    pub port: u16,
    pub build: String,
    pub since: u64,
    pub address: String,
    pub lan: String,
}

#[derive(Debug)]
pub struct LobbyError {
    pub status: u16,
    pub msg: String,
}
fn err(status: u16, msg: impl Into<String>) -> LobbyError { LobbyError { status, msg: msg.into() } }

/// JavaScript's String(v ?? ""), for the text fields.
fn js_string(v: Option<&Value>) -> String {
    match v {
        None | Some(Value::Null) => String::new(),
        Some(Value::String(s)) => s.clone(),
        Some(other) => other.to_string(),
    }
}

/// Typed by players and sent by games: no control characters, no tabs (games.txt's separator), not too long.
fn clean(v: Option<&Value>, n: usize) -> String {
    let s: String = js_string(v).chars().map(|c| if (c as u32) < 0x20 || c as u32 == 0x7f { ' ' } else { c }).collect();
    s.trim().chars().take(n).collect()
}

/// JavaScript's Number(v), as int() in Games.ts uses it.
fn js_number(v: Option<&Value>) -> f64 {
    match v {
        None => f64::NAN,
        Some(Value::Null) => 0.0,
        Some(Value::Bool(b)) => *b as u8 as f64,
        Some(Value::Number(n)) => n.as_f64().unwrap_or(f64::NAN),
        Some(Value::String(s)) => {
            let t = s.trim();
            if t.is_empty() { 0.0 } else { t.parse().unwrap_or(f64::NAN) }
        }
        Some(_) => f64::NAN,
    }
}

fn int(v: Option<&Value>, lo: u32, hi: u32, dflt: Option<u32>) -> Result<u32, String> {
    if let Some(d) = dflt {
        if v.is_none() || v == Some(&Value::String(String::new())) {
            return Ok(d);
        }
    }
    let n = js_number(v);
    if n.fract() != 0.0 || !n.is_finite() || n < lo as f64 || n > hi as f64 {
        return Err(format!("a number from {lo} to {hi}"));
    }
    Ok(n as u32)
}

/// A registration or a heartbeat's body, checked. `partial`: a heartbeat may leave anything out.
pub fn parse_fields(b: &Map<String, Value>, partial: bool) -> Result<Fields, String> {
    let has = |k: &str| b.get(k).map_or(false, |v| !v.is_null());
    let field = |k: &str, r: Result<u32, String>| r.map_err(|e| format!("{k}: {e}"));
    let mut out = Fields::default();
    if has("name") || !partial {
        let n = clean(b.get("name"), 24);
        out.name = Some(if n.is_empty() { "Rampage host".into() } else { n });
    }
    if has("monster") || !partial { out.monster = Some(clean(b.get("monster"), 24)); }
    if has("city") || !partial { out.city = Some(clean(b.get("city"), 32)); }
    if has("players") || !partial { out.players = Some(field("players", int(b.get("players"), 0, 4, Some(1)))?); }
    if has("max") || !partial { out.max = Some(field("max", int(b.get("max"), 1, 4, Some(2)))?); }
    if has("reserved") || !partial { out.reserved = Some(field("reserved", int(b.get("reserved"), 0, 3, Some(0)))?); }
    if has("port") || !partial { out.port = Some(field("port", int(b.get("port"), 1, 65535, None))? as u16); }
    if has("build") || !partial {
        let s = js_string(b.get("build"));
        if s.len() != 8 || !s.bytes().all(|c| c.is_ascii_hexdigit()) {
            return Err("build: 8 hex digits".into());
        }
        out.build = Some(s.to_uppercase());
    }
    let given = |k: &str| has(k) && b.get(k) != Some(&Value::String(String::new()));
    if given("external") {
        let (h, p) = split_host_port(&js_string(b.get("external"))).ok_or("external: not a host or host:port")?;
        out.external = Some(match p { Some(p) => format!("{h}:{p}"), None => h });
    }
    if given("stun") {
        out.stun = Some(public_endpoint(&js_string(b.get("stun"))).ok_or("stun: not a public IPv4 address and port")?);
    }
    if given("lan") {
        let s = js_string(b.get("lan"));
        if !is_private(&s) && !is_loopback(&s) {
            return Err("lan: not a local IPv4 address".into());
        }
        out.lan = Some(s);
    }
    if let (Some(p), Some(m)) = (out.players, out.max) {
        if p > m { out.players = Some(m); }
    }
    if let (Some(r), Some(p), Some(m)) = (out.reserved, out.players, out.max) {
        if p + r > m { out.reserved = Some(m.saturating_sub(p)); }
    }
    Ok(out)
}

fn hex(bytes: &[u8]) -> String { bytes.iter().map(|b| format!("{b:02x}")).collect() }

/// The same length and bytes, compared without stopping at the first difference.
fn same_token(a: &str, b: &str) -> bool {
    a.len() == b.len() && a.bytes().zip(b.bytes()).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

pub struct Games {
    games: HashMap<String, Game>,
    recent: HashMap<String, Vec<u64>>, // per source address: when it registered, for the rate limit
    public_address: Option<String>,
    now: Box<dyn Fn() -> u64 + Send>,
}

impl Games {
    /// `public_address`: this machine's address on the internet, for hosts on its PC or network without UPnP;
    /// `now`: the clock in milliseconds.
    pub fn new(public_address: Option<String>, now: Box<dyn Fn() -> u64 + Send>) -> Games {
        Games { games: HashMap::new(), recent: HashMap::new(), public_address: public_address.filter(|s| !s.is_empty()), now }
    }

    pub fn set_public_address(&mut self, ip: Option<String>) { self.public_address = ip.filter(|s| !s.is_empty()); }

    pub fn with_system_clock(public_address: Option<String>) -> Games {
        Games::new(public_address, Box::new(|| {
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_millis() as u64)
        }))
    }

    pub fn register(&mut self, body: &Map<String, Value>, from: &str) -> Result<Game, LobbyError> {
        let source = norm_ip(from);
        let f = parse_fields(body, false).map_err(|e| err(400, e))?;
        let t = (self.now)();
        let mut times: Vec<u64> = self.recent.get(&source).cloned().unwrap_or_default();
        times.retain(|x| t.saturating_sub(*x) < 60_000);
        if times.len() as u64 >= REGISTER_PER_MINUTE {
            return Err(err(429, "too many games registered from your address, try again in a minute"));
        }
        times.push(t);
        self.recent.insert(source.clone(), times);
        let (port, lan) = (f.port.unwrap(), f.lan.clone());
        // The same host starting again (same place, same port): its old listing goes rather than lingering 30 seconds.
        self.games.retain(|_, g| !(g.source == source && g.port == port && g.lan == lan.clone().unwrap_or(g.lan.clone())));
        if self.games.values().filter(|g| g.source == source).count() >= GAMES_PER_ADDRESS {
            return Err(err(429, format!("at most {GAMES_PER_ADDRESS} games from one address")));
        }
        let code = loop {
            let mut b = [0u8; CODE_LENGTH];
            OsRng.fill_bytes(&mut b);
            let c: String = b.iter().map(|x| CODE_ALPHABET.as_bytes()[*x as usize % CODE_ALPHABET.len()] as char).collect();
            if !self.games.contains_key(&c) {
                break c;
            }
        };
        let mut tok = [0u8; 16];
        OsRng.fill_bytes(&mut tok);
        let g = Game {
            code: code.clone(), token: hex(&tok),
            name: f.name.unwrap(), monster: f.monster.unwrap(), city: f.city.unwrap(),
            players: f.players.unwrap(), max: f.max.unwrap(), reserved: f.reserved.unwrap(), port, build: f.build.unwrap(),
            external: f.external.unwrap_or_default(), stun: f.stun.unwrap_or_default(),
            lan: lan.unwrap_or_else(|| if is_private(&source) { source.clone() } else { String::new() }),
            source, since: t, seen: t, knocks: Vec::new(),
        };
        self.games.insert(code, g.clone());
        Ok(g)
    }

    /// The token's game, or 404 gone (register again), 403 someone else's.
    fn own(&mut self, code: &str, token: Option<&str>) -> Result<&mut Game, LobbyError> {
        let g = self.games.get_mut(&code.to_uppercase()).ok_or_else(|| err(404, "no such game (it expired: register again)"))?;
        if !same_token(token.unwrap_or(""), &g.token) {
            return Err(err(403, "not your game"));
        }
        Ok(g)
    }

    pub fn heartbeat(&mut self, code: &str, body: &Map<String, Value>) -> Result<Game, LobbyError> {
        let token = body.get("token").map(|v| js_string(Some(v)));
        let now = (self.now)();
        let g = self.own(code, token.as_deref())?;
        let f = parse_fields(body, true).map_err(|e| err(400, e))?;
        if let Some(v) = f.name { g.name = v; }
        if let Some(v) = f.monster { g.monster = v; }
        if let Some(v) = f.city { g.city = v; }
        if let Some(v) = f.players { g.players = v; }
        if let Some(v) = f.max { g.max = v; }
        if let Some(v) = f.reserved { g.reserved = v; }
        if let Some(v) = f.port { g.port = v; }
        if let Some(v) = f.build { g.build = v; }
        if let Some(v) = f.external { g.external = v; }
        if let Some(v) = f.stun { g.stun = v; }
        if let Some(v) = f.lan { g.lan = v; }
        g.seen = now;
        if g.players > g.max { g.players = g.max; }
        if g.players + g.reserved > g.max { g.reserved = g.max.saturating_sub(g.players); }
        Ok(g.clone())
    }

    /// A guest about to join `code` from `from`: its STUN address (body.stun), else where it asked from with its game's
    /// port (body.port). The host collects it within a second (take_knocks) and punches.
    pub fn knock(&mut self, code: &str, body: &Map<String, Value>, from: &str) -> Result<String, LobbyError> {
        let now = (self.now)();
        let g = self.games.get_mut(&code.to_uppercase()).ok_or_else(|| err(404, "no game with that code"))?;
        let ip = norm_ip(from);
        let mut addr = public_endpoint(&js_string(body.get("stun")));
        if addr.is_none() && is_ipv4(&ip) {
            addr = Some(format!("{ip}:{}", int(body.get("port"), 1, 65535, None).map_err(|e| err(400, e))?));
        }
        let addr = addr.ok_or_else(|| err(400, "stun or port: where to punch toward"))?;
        g.knocks.retain(|(a, _)| *a != addr);
        g.knocks.push((addr.clone(), now));
        if g.knocks.len() > KNOCKS_KEPT {
            g.knocks.drain(..g.knocks.len() - KNOCKS_KEPT);
        }
        Ok(addr)
    }

    /// The host's: the knocks since it last asked (fresh ones only), and they are gone.
    pub fn take_knocks(&mut self, code: &str, token: Option<&str>) -> Result<Vec<String>, LobbyError> {
        let now = (self.now)();
        let g = self.own(code, token)?;
        let out = g.knocks.iter().filter(|(_, at)| now.saturating_sub(*at) < KNOCK_S * 1000).map(|(a, _)| a.clone()).collect();
        g.knocks.clear();
        Ok(out)
    }

    pub fn unregister(&mut self, code: &str, token: Option<&str>) -> Result<(), LobbyError> {
        let c = self.own(code, token)?.code.clone();
        self.games.remove(&c);
        Ok(())
    }

    /// Drops the games silent for EXPIRE_S; the server calls it every few seconds.
    pub fn sweep(&mut self) {
        let t = (self.now)();
        self.games.retain(|_, g| t.saturating_sub(g.seen) <= EXPIRE_S * 1000);
        self.recent.retain(|_, v| v.iter().any(|x| t.saturating_sub(*x) < 60_000));
    }

    pub fn get(&self, code: &str) -> Option<&Game> { self.games.get(&code.to_uppercase()) }

    pub fn list(&self) -> Vec<&Game> {
        let mut v: Vec<&Game> = self.games.values().collect();
        v.sort_by_key(|g| g.since);
        v
    }

    /// The host's public address: what UPnP said, else what STUN saw, else where it registered from when that is on
    /// the internet, else this lobby's public_address when the host is on its PC or network; "" not known.
    pub fn public_address(&self, g: &Game) -> String {
        let with_port = |h: &str| if h.contains(':') { h.to_string() } else { format!("{h}:{}", g.port) };
        if !g.external.is_empty() {
            return with_port(&g.external);
        }
        if !g.stun.is_empty() {
            return g.stun.clone();
        }
        if !is_local(&g.source) {
            return with_port(&g.source);
        }
        self.public_address.as_deref().map(with_port).unwrap_or_default()
    }

    pub fn public(&self, g: &Game) -> PublicGame {
        PublicGame {
            code: g.code.clone(), name: g.name.clone(), monster: g.monster.clone(), city: g.city.clone(),
            players: g.players, max: g.max, reserved: g.reserved, port: g.port, build: g.build.clone(), since: g.since,
            address: self.public_address(g), lan: if g.lan.is_empty() { String::new() } else { format!("{}:{}", g.lan, g.port) },
        }
    }

    /// The address a guest asking from `from` should join: 127.0.0.1 when both are on the lobby's PC; the host's LAN
    /// address for a guest on its network; otherwise the public one, failing that the LAN address (a VPN, a tailnet).
    pub fn join_for(&self, g: &Game, from: &str) -> String {
        let (r, s) = (norm_ip(from), &g.source);
        let lan = if g.lan.is_empty() { String::new() } else { format!("{}:{}", g.lan, g.port) };
        if is_loopback(&r) && is_loopback(s) {
            return format!("127.0.0.1:{}", g.port);
        }
        if !lan.is_empty() && (r == *s || (is_local(&r) && is_local(s))) {
            return lan;
        }
        let p = self.public_address(g);
        if !p.is_empty() {
            p
        } else if !lan.is_empty() {
            lan
        } else if is_ipv4(s) {
            format!("{s}:{}", g.port)
        } else {
            String::new()
        }
    }
}
