//! Join Local (LAN): finding the games hosted on this network. The launcher sends "RampageRE?LAN" (UDP) to the game's
//! port as a broadcast (and to this PC, and to its network's x.y.z.255); every hosting game answers from its own port
//! "RampageRE!LAN <version> <port>" (the game's src/nettransport.c, nte_intercept).
use std::net::{Ipv4Addr, SocketAddr, UdpSocket};
use std::time::{Duration, Instant};

pub const PORT: u16 = 19980;                         // (the game's UDP port: NET_PORT)
pub const ASK: &[u8] = b"RampageRE?LAN";
const SAY: &str = "RampageRE!LAN ";

/// A game found on the network: where to join it ("192.168.7.105:19980") and its version ("1.3.0").
#[derive(Debug, Clone, PartialEq)]
pub struct Found {
    pub addr: String,
    pub version: String,
}

/// A host's answer from `from`: the game it describes, or None (not one).
pub fn parse_say(bytes: &[u8], from: SocketAddr) -> Option<Found> {
    let s = std::str::from_utf8(bytes).ok()?.strip_prefix(SAY)?;
    let mut it = s.split_whitespace();
    let version = it.next()?.to_string();
    let port: u16 = it.next()?.parse().ok()?;
    Some(Found { addr: format!("{}:{}", from.ip(), port), version })
}

/// Where the question goes: everyone on the network, this PC, and `lan`'s x.y.z.255 (Windows sends the
/// 255.255.255.255 broadcast out of one network card only: with WSL, VMware or a VPN it may be the wrong one).
pub fn targets(lan: Option<&str>) -> Vec<Ipv4Addr> {
    let mut t = vec![Ipv4Addr::BROADCAST, Ipv4Addr::LOCALHOST];
    if let Some(ip) = lan.and_then(|s| s.parse::<Ipv4Addr>().ok()) {
        let o = ip.octets();
        let b = Ipv4Addr::new(o[0], o[1], o[2], 255);
        if !t.contains(&b) {
            t.push(b);
        }
    }
    t
}

/// The games answering within `wait`, each once, this PC's own after the others (it is seldom the one wanted).
pub fn find(lan: Option<&str>, wait: Duration) -> Vec<Found> {
    let mut found: Vec<Found> = Vec::new();
    let Ok(s) = UdpSocket::bind("0.0.0.0:0") else { return found };
    let _ = s.set_broadcast(true);
    for to in targets(lan) {
        let _ = s.send_to(ASK, (to, PORT));
    }
    let end = Instant::now() + wait;
    let mut buf = [0u8; 128];
    while let Some(left) = end.checked_duration_since(Instant::now()) {
        if left.is_zero() || s.set_read_timeout(Some(left)).is_err() {
            break;
        }
        match s.recv_from(&mut buf) {
            Ok((n, from)) => {
                if let Some(f) = parse_say(&buf[..n], from) {
                    if !found.contains(&f) {
                        found.push(f);
                    }
                }
            }
            Err(_) => break,
        }
    }
    let own = |f: &Found| f.addr.starts_with("127.") || lan.is_some_and(|ip| f.addr.starts_with(&format!("{ip}:")));
    found.sort_by_key(|f| own(f));
    // (this PC answers on 127.0.0.1 and on its LAN address: one is enough)
    let mut out: Vec<Found> = Vec::new();
    for f in found {
        if own(&f) && out.iter().any(|o| own(o)) {
            continue;
        }
        out.push(f);
    }
    out
}

/// A typed host address: "192.168.7.105" or "192.168.7.105:19980" or a name ("gamepc", "gamepc:27961"). None: empty.
pub fn typed_addr(s: &str) -> Option<Result<String, String>> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    let (host, port) = match s.rsplit_once(':') {
        Some((h, p)) => (h, Some(p)),
        None => (s, None),
    };
    let ok_host = !host.is_empty() && host.chars().all(|c| c.is_ascii_alphanumeric() || c == '.' || c == '-');
    let ok_port = port.map_or(true, |p| p.parse::<u16>().is_ok_and(|n| n > 0));
    if !ok_host || !ok_port {
        return Some(Err("Type the host's address, like 192.168.7.105 (their launcher shows it).".into()));
    }
    Some(Ok(s.to_string()))
}
