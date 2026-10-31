//! IP addresses as the lobby sees them (lobby/src/addr.ts). Only IPv4 is handed out: ENet in RampageRE binds IPv4.

/// IPv4 seen as IPv6 (::ffff:1.2.3.4) is that IPv4, and ::1 is 127.0.0.1.
pub fn norm_ip(s: &str) -> String {
    let s = s.trim().to_lowercase();
    let s = s.strip_prefix("::ffff:").unwrap_or(&s).to_string();
    if s == "::1" { "127.0.0.1".into() } else { s }
}

pub fn is_ipv4(s: &str) -> bool {
    let parts: Vec<&str> = s.split('.').collect();
    parts.len() == 4
        && parts.iter().all(|p| {
            !p.is_empty() && p.len() <= 3 && p.bytes().all(|b| b.is_ascii_digit())
                && p.parse::<u32>().map_or(false, |n| n <= 255 && n.to_string() == *p)
        })
}

fn octets(s: &str) -> Option<[u32; 4]> {
    if !is_ipv4(s) {
        return None;
    }
    let v: Vec<u32> = s.split('.').map(|p| p.parse().unwrap()).collect();
    Some([v[0], v[1], v[2], v[3]])
}

pub fn is_loopback(s: &str) -> bool { is_ipv4(s) && s.starts_with("127.") }

/// Addresses that only mean something on the network they are on: RFC 1918, link-local, and 100.64/10 (carrier-grade
/// NAT, and Tailscale's).
pub fn is_private(s: &str) -> bool {
    match octets(s) {
        Some([a, b, _, _]) => a == 10 || (a == 172 && (16..=31).contains(&b)) || (a == 192 && b == 168)
            || (a == 169 && b == 254) || (a == 100 && (64..=127).contains(&b)),
        None => false,
    }
}

/// Somewhere only this network can reach: loopback or private.
pub fn is_local(s: &str) -> bool { is_loopback(s) || is_private(s) }

/// A host name or IPv4 a player typed, or UPnP reported: letters, digits, dots and dashes, nothing else.
pub fn is_host_name(s: &str) -> bool {
    let b = s.as_bytes();
    !b.is_empty() && b.len() <= 64 && b[0].is_ascii_alphanumeric() && b[b.len() - 1].is_ascii_alphanumeric()
        && b.iter().all(|c| c.is_ascii_alphanumeric() || *c == b'.' || *c == b'-') && !s.contains("..")
}

/// "host" or "host:port"; None when it is neither.
pub fn split_host_port(s: &str) -> Option<(String, Option<u16>)> {
    let s = s.trim();
    let (host, port) = match s.split_once(':') {
        Some((h, p)) => {
            if p.is_empty() || p.len() > 5 || !p.bytes().all(|c| c.is_ascii_digit()) {
                return None;
            }
            let n: u32 = p.parse().ok()?;
            if !(1..=65535).contains(&n) {
                return None;
            }
            (h, Some(n as u16))
        }
        None => (s, None),
    };
    if !is_host_name(host) {
        return None;
    }
    Some((host.to_string(), port))
}

/// "a.b.c.d:port" on the internet (a STUN answer), else None.
pub fn public_endpoint(v: &str) -> Option<String> {
    let (ip, port) = v.trim().split_once(':')?;
    if port.is_empty() || port.len() > 5 || !port.bytes().all(|c| c.is_ascii_digit()) {
        return None;
    }
    let p: u32 = port.parse().ok()?;
    if !is_ipv4(ip) || is_local(ip) || !(1..=65535).contains(&p) {
        return None;
    }
    Some(format!("{ip}:{p}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn addresses_normalise_and_classify() {
        assert_eq!(norm_ip("::ffff:192.168.1.5"), "192.168.1.5");
        assert_eq!(norm_ip("::1"), "127.0.0.1");
        assert!(["10.0.0.1", "172.16.4.4", "192.168.0.9", "100.101.1.1"].iter().all(|s| is_private(s)));
        assert!(!["8.8.8.8", "172.32.0.1", "127.0.0.1", "1.2.3"].iter().any(|s| is_private(s)));
        assert_eq!(split_host_port("example.org:27961"), Some(("example.org".into(), Some(27961))));
        assert_eq!(split_host_port("a b"), None);
        assert_eq!(split_host_port("1.2.3.4:99999"), None);
    }
}
