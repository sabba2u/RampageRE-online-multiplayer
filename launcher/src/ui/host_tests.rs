use super::host::*;
use crate::lobby::games::Games;
use crate::upnp::Reach;
use serde_json::json;

#[test]
fn links_for_internet_and_same_wifi() {
    let net = Reach::Internet { public_ip: "203.0.113.5".into() };
    assert_eq!(invite_links("K7PQ2", &net, Some("192.168.1.20")), vec![
        ("Friends anywhere".to_string(), "rampage://join/K7PQ2@203.0.113.5".to_string()),
        ("Same Wi-Fi".to_string(), "rampage://join/K7PQ2@192.168.1.20".to_string()),
    ]);
    let home = Reach::HomeOnly { reason: "x".into() };
    assert_eq!(invite_links("K7PQ2", &home, Some("192.168.1.20")),
               vec![("Same Wi-Fi".to_string(), "rampage://join/K7PQ2@192.168.1.20".to_string())]);
    assert!(invite_links("K7PQ2", &home, None).is_empty());
}

#[test]
fn finds_code_of_local_host() {
    let mut g = Games::new(None, Box::new(|| 0));
    let body = |port: u16| json!({"name": "h", "port": port, "build": "0000ABCD"}).as_object().unwrap().clone();
    g.register(&body(27960), "203.0.113.9").unwrap();
    let mine = g.register(&body(27960), "127.0.0.1").unwrap().code;
    g.register(&body(27961), "127.0.0.1").unwrap();
    assert_eq!(find_my_code(&g, 27960), Some(mine));
    assert_eq!(find_my_code(&g, 27999), None);
}

fn hidden(cmd: &str, args: &[&str]) -> std::process::Child {
    use std::os::windows::process::CommandExt;
    std::process::Command::new(cmd).args(args).creation_flags(0x0800_0000).spawn().unwrap()
}

#[test]
fn a_game_that_closes_ends_hosting() {
    let mut h = Hosting::default();
    h.game = Some(hidden("cmd", &["/c", "exit 0"]));
    std::thread::sleep(std::time::Duration::from_millis(800));
    assert_eq!(h.check_game(), Some(GAME_CLOSED_EARLY));
    h.game = Some(hidden("cmd", &["/c", "exit 0"]));
    h.code = Some("K7PQ2".into());
    std::thread::sleep(std::time::Duration::from_millis(800));
    assert_eq!(h.check_game(), Some(GAME_CLOSED));
    h.game = Some(hidden("ping", &["-n", "30", "127.0.0.1"]));
    assert_eq!(h.check_game(), None);
    h.end_game();
}

#[test]
fn end_game_closes_it() {
    let mut h = Hosting::default();
    let child = hidden("ping", &["-n", "30", "127.0.0.1"]);
    let pid = child.id().to_string();
    h.game = Some(child);
    h.end_game();
    assert!(h.game.is_none());
    let out = std::process::Command::new("tasklist").args(["/FI", &format!("PID eq {pid}"), "/NH"]).output().unwrap();
    assert!(!String::from_utf8_lossy(&out.stdout).contains(&pid), "the hosting game must be gone");
}

#[test]
fn close_gently_ends_it_even_without_a_window() {
    let mut child = hidden("ping", &["-n", "30", "127.0.0.1"]);
    let pid = child.id().to_string();
    let t = std::time::Instant::now();
    crate::game::close_gently(&mut child, std::time::Duration::from_millis(600));
    assert!(t.elapsed() < std::time::Duration::from_secs(5));
    let out = std::process::Command::new("tasklist").args(["/FI", &format!("PID eq {pid}"), "/NH"]).output().unwrap();
    assert!(!String::from_utf8_lossy(&out.stdout).contains(&pid), "closed, by force after the wait");
}

#[test]
fn ports_forwarded_by_hand() {
    use crate::settings::Settings;
    use crate::upnp::Opened;
    let home = || Opened { reach: Reach::HomeOnly { reason: "no router answered (UPnP is off)".into() }, forwarded: false };
    let mut s = Settings::default();
    // (not said: as the router said)
    assert_eq!(by_hand(home(), &s, || Ok("1.2.3.4".into())), home());
    s.forwarded_by_hand = true;
    assert_eq!(by_hand(home(), &s, || Ok("184.170.168.240".into())).reach,
               Reach::Internet { public_ip: "184.170.168.240".into() });
    s.public_name = "sabba2u.no-ip.org".into();
    assert_eq!(by_hand(home(), &s, || panic!("no lookup with a name")).reach,
               Reach::Internet { public_ip: "sabba2u.no-ip.org".into() });
    // (the lookup failing: as the router said)
    s.public_name.clear();
    assert_eq!(by_hand(home(), &s, || Err("no".into())), home());
    // (the router opened itself: its answer kept)
    let net = Opened { reach: Reach::Internet { public_ip: "5.6.7.8".into() }, forwarded: true };
    assert_eq!(by_hand(net.clone(), &s, || Ok("1.2.3.4".into())), net);
}

#[test]
fn a_name_in_the_links() {
    let net = Reach::Internet { public_ip: "sabba2u.no-ip.org".into() };
    assert_eq!(invite_links("K7PQ2", &net, None)[0].1, "rampage://join/K7PQ2@sabba2u.no-ip.org");
}
