//! The lobby's port through the host's router (UPnP), and whether friends outside the home can reach this PC at all.
//! The game opens its own UDP port the same way (src/netupnp.c); this is the lobby's TCP one.
use crate::lobby::addr::is_local;
use igd_next::{search_gateway, PortMappingProtocol, SearchOptions};
use std::net::{SocketAddr, UdpSocket};
use std::time::Duration;

#[derive(Debug, Clone, PartialEq)]
pub enum Reach {
    Internet { public_ip: String },
    /// only this home network can join: why, for the log
    HomeOnly { reason: String },
}

/// The router's answer about this PC's address on the internet.
pub fn classify(external_ip: Option<&str>) -> Reach {
    match external_ip {
        None => Reach::HomeOnly { reason: "no router answered (UPnP is off)".into() },
        Some(ip) if is_local(ip) => Reach::HomeOnly { reason: format!("the router's own address is shared ({ip})") },
        Some(ip) => Reach::Internet { public_ip: ip.to_string() },
    }
}

/// The address this PC uses on its network (the way toward the internet; nothing is sent).
pub fn lan_ip() -> Option<String> {
    let s = UdpSocket::bind("0.0.0.0:0").ok()?;
    s.connect("8.8.8.8:53").ok()?;
    match s.local_addr().ok()? {
        SocketAddr::V4(a) if !a.ip().is_unspecified() => Some(a.ip().to_string()),
        _ => None,
    }
}

const LEASE_S: u32 = 0; // until removed (close_lobby_port), as the game's own forward

/// The router search, sent from this PC's real network address: a PC with virtual adapters (Hyper-V, VPNs) would
/// otherwise ask on whichever one Windows picks, and no router answers there.
fn search(secs: u64) -> Option<igd_next::Gateway> {
    let lan: std::net::IpAddr = lan_ip()?.parse().ok()?;
    let opts = SearchOptions { timeout: Some(Duration::from_secs(secs)), bind_addr: SocketAddr::new(lan, 0), ..Default::default() };
    search_gateway(opts).ok()
}

/// Asks the router to forward TCP `port` to this PC, and reads its address on the internet.
pub fn open_lobby_port(port: u16) -> Reach {
    let Some(lan) = lan_ip().and_then(|ip| ip.parse().ok()) else {
        return Reach::HomeOnly { reason: "this PC has no network address".into() };
    };
    let Some(gw) = search(3) else {
        return classify(None);
    };
    if let Err(e) = gw.add_port(PortMappingProtocol::TCP, port, SocketAddr::new(lan, port), LEASE_S, "RampageRE lobby") {
        return Reach::HomeOnly { reason: format!("the router refused the forward ({e})") };
    }
    classify(gw.get_external_ip().ok().map(|ip| ip.to_string()).as_deref())
}

pub fn close_lobby_port(port: u16) {
    if let Some(gw) = search(2) {
        let _ = gw.remove_port(PortMappingProtocol::TCP, port);
    }
}
