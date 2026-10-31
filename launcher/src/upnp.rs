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

/// An hour: a forward left behind (a crash, the launcher killed) expires by itself; hosting renews it (RENEW_S).
const LEASE_S: u32 = 3600;
pub const RENEW_S: u64 = 3000;

/// What opening the router gave: who can reach this PC, and whether the router made the forward (it must then be
/// removed on stop even when the address it gave is no use: double NAT).
#[derive(Debug, Clone, PartialEq)]
pub struct Opened {
    pub reach: Reach,
    pub forwarded: bool,
}

pub fn opened(added: bool, external_ip: Option<&str>) -> Opened {
    if !added {
        return Opened { reach: Reach::HomeOnly { reason: "the router refused the forward".into() }, forwarded: false };
    }
    Opened { reach: classify(external_ip), forwarded: true }
}

/// The router search, sent from this PC's real network address: a PC with virtual adapters (Hyper-V, VPNs) would
/// otherwise ask on whichever one Windows picks, and no router answers there.
fn search(secs: u64) -> Option<igd_next::Gateway> {
    let lan: std::net::IpAddr = lan_ip()?.parse().ok()?;
    let opts = SearchOptions { timeout: Some(Duration::from_secs(secs)), bind_addr: SocketAddr::new(lan, 0), ..Default::default() };
    search_gateway(opts).ok()
}

/// Asks the router to forward TCP `port` to this PC (again: renewing it), and reads its address on the internet.
pub fn open_lobby_port(port: u16) -> Opened {
    let Some(lan) = lan_ip().and_then(|ip| ip.parse().ok()) else {
        return Opened { reach: Reach::HomeOnly { reason: "this PC has no network address".into() }, forwarded: false };
    };
    let Some(gw) = search(3) else {
        return Opened { reach: classify(None), forwarded: false };
    };
    let added = gw.add_port(PortMappingProtocol::TCP, port, SocketAddr::new(lan, port), LEASE_S, "RampageRE lobby").is_ok();
    opened(added, gw.get_external_ip().ok().map(|ip| ip.to_string()).as_deref())
}

pub fn close_lobby_port(port: u16) {
    if let Some(gw) = search(2) {
        let _ = gw.remove_port(PortMappingProtocol::TCP, port);
    }
}
