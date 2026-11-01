//! rampage:// links. Any web page can open one, so a link may only say which game and which lobby: the program it
//! starts is always the game next to the launcher. The launcher itself is the link's handler (HKCU, no admin).
//!   rampage://join/<code>                 the code, through the group's server the launcher remembers
//!   rampage://join/<code>@<host[:port]>   the code through that server (port 19980 when left out), remembered
//!   rampage://join/<host[:port]>          a game's own address, no lobby
use crate::lobby::addr::split_host_port;
use crate::lobby::games::{is_code, CODE_LENGTH};
use std::path::Path;
#[cfg(windows)]
use winreg::RegKey;

pub const LOBBY_PORT: u16 = 19980;

#[derive(Debug, Clone, PartialEq)]
pub enum Link {
    Code(String),
    CodeAt { code: String, server: String },
    Direct(String),
}

pub fn parse_link(s: &str) -> Result<Link, String> {
    let refuse = || {
        let shown: String = s.chars().take(80).collect();
        format!("That isn't a RampageRE join link: {shown}")
    };
    if s.len() > 300 || !s.is_ascii() {
        return Err(refuse());
    }
    let rest = s.get(..15).filter(|p| p.eq_ignore_ascii_case("rampage://join/")).map(|_| &s[15..]).ok_or_else(refuse)?;
    let rest = rest.strip_suffix('/').unwrap_or(rest);
    let code_ok = |c: &str| c.len() == CODE_LENGTH && is_code(c);
    if let Some((code, server)) = rest.split_once('@') {
        let (host, port) = split_host_port(server).ok_or_else(refuse)?;
        if !code_ok(code) {
            return Err(refuse());
        }
        return Ok(Link::CodeAt { code: code.to_uppercase(), server: format!("http://{host}:{}", port.unwrap_or(LOBBY_PORT)) });
    }
    if code_ok(rest) {
        return Ok(Link::Code(rest.to_uppercase()));
    }
    // a game's address: an IP, a dotted name, or with its port (so a mistyped code is never taken for a host)
    match split_host_port(rest) {
        Some((host, port)) if host.contains('.') || port.is_some() => {
            Ok(Link::Direct(port.map_or(host.clone(), |p| format!("{host}:{p}"))))
        }
        _ => Err(refuse()),
    }
}

pub const NOT_A_SERVER: &str = "That isn't a server address: type it as your host gave it, like 192.168.7.105.";

/// A server as typed ("192.168.7.105", "host:port", "http://host:port") or the server of a pasted room link:
/// "http://host:port" (port 19980 when left out).
pub fn server_url(input: &str) -> Result<String, String> {
    let s = input.trim();
    if s.len() >= 10 && s.is_char_boundary(10) && s[..10].eq_ignore_ascii_case("rampage://") {
        return match parse_link(s)? {
            Link::CodeAt { server, .. } => Ok(server),
            _ => Err(NOT_A_SERVER.into()),
        };
    }
    let s = s.strip_prefix("http://").unwrap_or(s);
    let s = s.strip_suffix('/').unwrap_or(s);
    match split_host_port(s) {
        Some((host, port)) if !host.is_empty() && s.is_ascii() => Ok(format!("http://{host}:{}", port.unwrap_or(LOBBY_PORT))),
        _ => Err(NOT_A_SERVER.into()),
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum LinkStatus {
    NotSet,
    Ok,
    /// set up, but for a launcher somewhere else (this one was moved): the path it points at
    Stale(String),
}

/// HKCU\Software\Classes, where a user's own link handlers live.
#[cfg(windows)]
pub fn classes() -> std::io::Result<RegKey> {
    RegKey::predef(winreg::enums::HKEY_CURRENT_USER).create_subkey(r"Software\Classes").map(|(k, _)| k)
}

#[cfg(windows)]
pub fn status(root: &RegKey, exe: &Path) -> LinkStatus {
    let cmd: String = match root.open_subkey(r"rampage\shell\open\command").and_then(|k| k.get_value("")) {
        Ok(c) => c,
        Err(_) => return LinkStatus::NotSet,
    };
    // "\"<exe>\" \"%1\"": the first quoted part
    let target = cmd.strip_prefix('"').and_then(|c| c.split_once('"')).map_or(cmd.as_str(), |(p, _)| p);
    if target.eq_ignore_ascii_case(&exe.display().to_string()) { LinkStatus::Ok } else { LinkStatus::Stale(target.to_string()) }
}

#[cfg(windows)]
pub fn register(root: &RegKey, exe: &Path) -> std::io::Result<()> {
    let (k, _) = root.create_subkey("rampage")?;
    k.set_value("", &"URL:RampageRE")?;
    k.set_value("URL Protocol", &"")?;
    let (cmd, _) = k.create_subkey(r"shell\open\command")?;
    cmd.set_value("", &format!("\"{}\" \"%1\"", exe.display()))
}

#[cfg(windows)]
pub fn unregister(root: &RegKey) -> std::io::Result<()> {
    match root.delete_subkey_all("rampage") {
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(()),
        r => r,
    }
}
