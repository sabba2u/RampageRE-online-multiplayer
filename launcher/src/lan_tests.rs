use crate::lan::*;
use std::net::{Ipv4Addr, SocketAddr, UdpSocket};
use std::time::Duration;

#[test]
fn a_hosts_answer_is_read() {
    let from: SocketAddr = "192.168.7.105:27960".parse().unwrap();
    assert_eq!(parse_say(b"RampageRE!LAN 1.3.0 27960", from),
               Some(Found { addr: "192.168.7.105:27960".into(), version: "1.3.0".into() }));
    let other_port: SocketAddr = "10.0.0.2:5555".parse().unwrap();
    assert_eq!(parse_say(b"RampageRE!LAN 1.3.0 27961", other_port).unwrap().addr, "10.0.0.2:27961");
    assert_eq!(parse_say(b"RampageRE?LAN", from), None);
    assert_eq!(parse_say(b"RampageRE!LAN 1.3.0", from), None);
    assert_eq!(parse_say(b"hello", from), None);
}

#[test]
fn the_question_goes_to_the_network_and_this_pc() {
    let t = targets(Some("192.168.7.105"));
    assert!(t.contains(&Ipv4Addr::BROADCAST));
    assert!(t.contains(&Ipv4Addr::LOCALHOST));
    assert!(t.contains(&Ipv4Addr::new(192, 168, 7, 255)));
    assert_eq!(targets(None).len(), 2);
    assert_eq!(targets(Some("not an ip")).len(), 2);
}

#[test]
fn typed_addresses() {
    assert_eq!(typed_addr("  "), None);
    assert_eq!(typed_addr("192.168.7.105"), Some(Ok("192.168.7.105".into())));
    assert_eq!(typed_addr(" 192.168.7.105:27961 "), Some(Ok("192.168.7.105:27961".into())));
    assert_eq!(typed_addr("gamepc"), Some(Ok("gamepc".into())));
    assert!(matches!(typed_addr("192.168.7.105:"), Some(Err(_))));
    assert!(matches!(typed_addr("192.168.7.105:99999"), Some(Err(_))));
    assert!(matches!(typed_addr("http://x"), Some(Err(_))));
}

#[test]
fn a_game_answering_is_found() {
    // a pretend host on 127.0.0.1:PORT (skipped when a real game holds the port)
    let Ok(host) = UdpSocket::bind(("127.0.0.1", PORT)) else { return };
    let t = std::thread::spawn(move || {
        let mut buf = [0u8; 64];
        host.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
        while let Ok((n, from)) = host.recv_from(&mut buf) {
            if &buf[..n] == ASK {
                host.send_to(format!("RampageRE!LAN 1.3.0 {PORT}").as_bytes(), from).unwrap();
                break;
            }
        }
    });
    let found = find(None, Duration::from_millis(800));
    t.join().unwrap();
    // (a real game hosting on this PC may answer too)
    assert!(found.contains(&Found { addr: format!("127.0.0.1:{PORT}"), version: "1.3.0".into() }), "{found:?}");
}
