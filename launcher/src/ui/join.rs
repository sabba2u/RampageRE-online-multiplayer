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
        Some(b) if b.eq_ignore_ascii_case(mine) => Ok(()),
        Some(_) => Err(DIFFERENT_VERSION.into()),
        None => Err(CANT_REACH.into()),
    }
}

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
