//! The game next to the launcher: which exe, its build id (src/net.c build_id: guest and host must match), and
//! starting it with the flags it already has (--host, --steam-host, --join, --lobby).
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::{Child, Command};

pub const NO_GAME: &str = "RampageRE.exe isn't next to the launcher: put the launcher in the game's folder.";

/// RampageRE.exe, else the developer build.
pub fn find_game(dir: &Path) -> Result<PathBuf, String> {
    ["RampageRE.exe", "RampageRE-dev.exe"].iter().map(|n| dir.join(n)).find(|p| p.is_file()).ok_or_else(|| NO_GAME.to_string())
}

/// FNV-1a 32 over the exe's bytes, "%08X", exactly as the game computes and lists it.
pub fn build_id(exe: &Path) -> io::Result<String> {
    let mut f = std::fs::File::open(exe)?;
    let mut buf = vec![0u8; 65536];
    let mut h: u32 = 2166136261;
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 {
            break;
        }
        for b in &buf[..n] {
            h = (h ^ *b as u32).wrapping_mul(16777619);
        }
    }
    Ok(format!("{:08X}", if h == 0 { 1 } else { h }))
}

#[derive(Debug, Clone, PartialEq)]
pub enum Start {
    /// Play Local: the game's own menus; four_players adds 3 and 4 Player there (rampage.cfg four_players)
    Offline { four_players: bool },
    /// hosting, listed in this lobby
    Host { lobby: String },
    SteamHost,
    /// a code (through `lobby`) or an address
    Join { target: String, lobby: Option<String> },
    /// started in its menus, ready for a friend's Steam invite or Join Game
    SteamWait,
}

pub fn args(s: &Start) -> Vec<String> {
    let v: Vec<&str> = match s {
        Start::Offline { four_players: true } => vec!["--four-players"],
        Start::Offline { four_players: false } => vec!["--no-four-players"],
        // online: 2 players (the host and one guest) is what online play supports today; 3 and 4 Player off in the
        // menus (a third or fourth monster isn't kept in step on the guest's screen)
        Start::SteamWait => vec!["--no-four-players"],
        Start::Host { lobby } => vec!["--no-four-players", "--host", "--lobby", lobby],
        Start::SteamHost => vec!["--no-four-players", "--steam-host"],
        Start::Join { target, lobby: Some(l) } => vec!["--no-four-players", "--lobby", l, "--join", target],
        Start::Join { target, lobby: None } => vec!["--no-four-players", "--join", target],
    };
    // --shell first, always: the player build opens its menus only when started with no flags at all; with any
    // (these) it opened a free camera over Chicago, no menu and no monster
    std::iter::once("--shell").chain(v).map(String::from).collect()
}

/// Settings' "Unlock all monsters": the game's cheat for every monster and city (MainMenu.activateCheat 141421)
pub const UNLOCK_ALL: [&str; 2] = ["--cheat", "141421"];

pub const OLD_GAME: &str = "This RampageRE.exe is an older version without online play. Get the online release \
(RampageRE.exe with the launcher) and put it in this folder. Play Local still works.";

/// The game knows the online flags: an older release reads "--host" as the path to its disc image.
pub fn supports_online(exe: &Path) -> bool {
    std::fs::read(exe).is_ok_and(|b| b.windows(12).any(|w| w == b"--steam-host"))
}

/// What a start passes: args(s), and the cheat for every monster when Settings' Unlock all monsters is on.
pub fn start_args(s: &Start, unlock_all: bool) -> Vec<String> {
    let mut a = args(s);
    if unlock_all {
        a.extend(UNLOCK_ALL.iter().map(|x| x.to_string()));
    }
    a
}

/// Started from its own folder (it finds its data there). Anything but Offline needs a game with online play.
pub fn start(exe: &Path, s: &Start) -> io::Result<Child> {
    if !matches!(s, Start::Offline { .. }) && !supports_online(exe) {
        return Err(io::Error::other(OLD_GAME));
    }
    let mut c = Command::new(exe);
    c.args(start_args(s, crate::settings::Settings::load().unlock_all));
    if let Some(dir) = exe.parent() {
        c.current_dir(dir);
    }
    c.spawn()
}
