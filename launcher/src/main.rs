//! RampageRE's launcher (docs/superpowers/specs/2026-10-31-launcher-design.md).
//!   rampage-launcher                       the window
//!   rampage-launcher --serve-lobby [port]  only the built-in lobby, no window (tests, a lobby on a spare PC)
mod links;
#[cfg(test)]
mod links_tests;
mod lobby;

fn main() {
    let args: Vec<String> = std::env::args().collect();
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
}
