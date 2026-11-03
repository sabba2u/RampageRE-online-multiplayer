#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
//! RampageRE's launcher (docs/superpowers/specs/2026-10-31-launcher-design.md).
//!   rampage-launcher                       the window
//!   rampage-launcher rampage://join/...    a clicked link: the game joins, no window
//!   rampage-launcher --serve-lobby [port]  only the built-in lobby, no window (tests, a lobby on a spare PC)
//!   rampage-launcher --steam-check <dir>   exit 0 when Steam starts this process as Spacewar (steam.rs's check)
mod firewall;
#[cfg(test)]
mod firewall_tests;
mod game;
#[cfg(test)]
mod game_tests;
mod lan;
#[cfg(test)]
mod lan_tests;
mod links;
#[cfg(test)]
mod links_tests;
mod lobby;
mod settings;
mod steam;
#[cfg(test)]
mod steam_tests;
mod ui;
mod upnp;
#[cfg(test)]
mod upnp_tests;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.get(1).map(String::as_str) == Some("--steam-check") {
        let dir = args.get(2).map(std::path::PathBuf::from).unwrap_or_default();
        std::process::exit(steam::steam_check_child(&dir));
    }
    if args.get(1).map(String::as_str) == Some("--serve-lobby") {
        let port = args.get(2).and_then(|p| p.parse().ok()).unwrap_or(19980);
        match lobby::http::LobbyServer::start(port, None) {
            Ok(s) => {
                println!("RampageRE lobby on port {} (TCP)", s.port);
                loop {
                    std::thread::park();
                }
            }
            Err(lobby::http::StartError::PortInUse) => {
                eprintln!("Another lobby is already running on this PC (port {port}).");
                std::process::exit(1);
            }
            Err(lobby::http::StartError::Other(e)) => {
                eprintln!("The lobby could not start: {e}");
                std::process::exit(1);
            }
        }
    }
    if let Some(url) = args.get(1).filter(|a| a.get(..10).map_or(false, |p| p.eq_ignore_ascii_case("rampage://"))) {
        if let Err(e) = ui::join::open_link(url) {
            message_box(&e);
        }
        return;
    }
    if let Err(e) = ui::run() {
        message_box(&format!("The launcher couldn't open its window: {e}"));
    }
}

/// A plain Windows message (a clicked link has no window of its own to say it in).
fn message_box(text: &str) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_ICONWARNING, MB_OK};
    let w = |s: &str| s.encode_utf16().chain(std::iter::once(0)).collect::<Vec<u16>>();
    unsafe {
        MessageBoxW(std::ptr::null_mut(), w(text).as_ptr(), w("RampageRE").as_ptr(), MB_OK | MB_ICONWARNING);
    }
}
