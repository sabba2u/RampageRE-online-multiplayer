//! lobby/test/games.test.ts, case for case.
use super::games::*;
use serde_json::{json, Map, Value};
use std::sync::{Arc, Mutex};

fn base(extra: Value) -> Map<String, Value> {
    let mut m = json!({"name": "sabba", "monster": "George", "city": "Chicago 1", "players": 1, "max": 2,
                       "port": 27960, "build": "cafe1234"}).as_object().unwrap().clone();
    for (k, v) in extra.as_object().unwrap() {
        m.insert(k.clone(), v.clone());
    }
    m
}
fn obj(v: Value) -> Map<String, Value> { v.as_object().unwrap().clone() }
fn clock(start: u64) -> (Arc<Mutex<u64>>, Games) {
    let t = Arc::new(Mutex::new(start));
    let t2 = t.clone();
    (t, Games::new(None, Box::new(move || *t2.lock().unwrap())))
}
fn advance(t: &Arc<Mutex<u64>>, ms: u64) { *t.lock().unwrap() += ms; }

#[test]
fn fields_clean_names_and_check_numbers() {
    let f = parse_fields(&base(json!({"name": "  evil\tname\n", "build": "CAFE1234", "players": "3", "max": "2"})), false).unwrap();
    assert_eq!(f.name.as_deref(), Some("evil name"));
    assert_eq!(f.build.as_deref(), Some("CAFE1234"));
    assert_eq!((f.players, f.max), (Some(2), Some(2)));
    assert!(parse_fields(&base(json!({"port": 70000})), false).unwrap_err().contains("port"));
    assert!(parse_fields(&base(json!({"build": "xyz"})), false).unwrap_err().contains("build"));
    assert!(parse_fields(&base(json!({"lan": "8.8.8.8"})), false).unwrap_err().contains("lan"));
    assert!(parse_fields(&base(json!({"external": "a;b"})), false).unwrap_err().contains("external"));
    assert_eq!(parse_fields(&base(json!({"name": ""})), false).unwrap().name.as_deref(), Some("Rampage host"));
    let p = parse_fields(&obj(json!({"players": 2})), true).unwrap();
    assert_eq!(p, Fields { players: Some(2), ..Default::default() });
}

#[test]
fn codes_and_token() {
    let mut games = Games::new(None, Box::new(|| 1000));
    let g = games.register(&base(json!({})), "203.0.113.5").unwrap();
    assert_eq!(g.code.len(), CODE_LENGTH);
    assert!(g.code.chars().all(|c| CODE_ALPHABET.contains(c)));
    assert!(!CODE_ALPHABET.contains(['0', '1', 'O', 'I']));
    assert!(is_code(&g.code.to_lowercase()));
    let e = games.heartbeat(&g.code, &obj(json!({"token": "nope"}))).unwrap_err();
    assert_eq!((e.status, e.msg.as_str()), (403, "not your game"));
    games.heartbeat(&g.code.to_lowercase(), &obj(json!({"token": g.token, "players": 2, "city": "Chicago 2"}))).unwrap();
    let h = games.get(&g.code).unwrap();
    assert_eq!((h.players, h.city.as_str()), (2, "Chicago 2"));
    // a guest dropped out: its slot kept for it; never more than max with the players
    games.heartbeat(&g.code, &obj(json!({"token": g.token, "players": 1, "reserved": 1}))).unwrap();
    let h = games.get(&g.code).unwrap();
    assert_eq!((h.players, h.max, h.reserved), (1, 2, 1));
    assert_eq!(games.public(h).reserved, 1);
    games.heartbeat(&g.code, &obj(json!({"token": g.token, "players": 2}))).unwrap();
    assert_eq!(games.get(&g.code).unwrap().reserved, 0);
    assert!(games.heartbeat(&g.code, &obj(json!({"token": g.token, "reserved": 9}))).unwrap_err().msg.contains("reserved"));
    games.unregister(&g.code, Some(&g.token)).unwrap();
    assert!(games.list().is_empty());
    assert!(games.heartbeat(&g.code, &obj(json!({"token": g.token}))).unwrap_err().msg.contains("register again"));
}

#[test]
fn expires_after_expire_s() {
    let (t, mut games) = clock(1000);
    let g = games.register(&base(json!({})), "203.0.113.5").unwrap();
    advance(&t, (EXPIRE_S - 1) * 1000);
    games.sweep();
    assert!(games.get(&g.code).is_some());
    games.heartbeat(&g.code, &obj(json!({"token": g.token}))).unwrap();
    advance(&t, (EXPIRE_S - 1) * 1000);
    games.sweep();
    assert!(games.get(&g.code).is_some());
    advance(&t, 2000);
    games.sweep();
    assert!(games.get(&g.code).is_none());
}

#[test]
fn rate_limit_and_replacing_listing() {
    let (t, mut games) = clock(0);
    games.register(&base(json!({})), "198.51.100.1").unwrap();
    games.register(&base(json!({})), "198.51.100.1").unwrap();
    assert_eq!(games.list().len(), 1); // the same host and port: the old one went
    for i in 2..REGISTER_PER_MINUTE {
        games.register(&base(json!({"port": 28000 + (i % 3)})), "198.51.100.1").unwrap();
    }
    assert_eq!(games.list().len(), 4);
    assert!(games.register(&base(json!({})), "198.51.100.1").unwrap_err().msg.contains("too many"));
    games.register(&base(json!({})), "198.51.100.2").unwrap(); // someone else is fine
    advance(&t, 61_000);
    let e = games.register(&base(json!({"port": 29001})), "198.51.100.1").unwrap_err();
    assert!(e.msg.contains("at most")); // still 4 listed
}

#[test]
fn join_address_per_asker() {
    let mut games = Games::new(None, Box::new(|| 0));
    let net = games.register(&base(json!({"lan": "192.168.1.20"})), "203.0.113.5").unwrap();
    assert_eq!(games.join_for(&net, "198.51.100.9"), "203.0.113.5:27960");
    assert_eq!(games.join_for(&net, "203.0.113.5"), "192.168.1.20:27960");
    let upnp = games.register(&base(json!({"port": 27961, "external": "203.0.113.77"})), "203.0.113.6").unwrap();
    assert_eq!(games.join_for(&upnp, "198.51.100.9"), "203.0.113.77:27961");

    let mut local = Games::new(Some("203.0.113.50".into()), Box::new(|| 0));
    let h = local.register(&base(json!({"lan": "192.168.1.20"})), "127.0.0.1").unwrap();
    assert_eq!(local.join_for(&h, "::1"), "127.0.0.1:27960");
    assert_eq!(local.join_for(&h, "::ffff:192.168.1.30"), "192.168.1.20:27960");
    assert_eq!(local.join_for(&h, "198.51.100.9"), "203.0.113.50:27960");
    let p = local.public(&h);
    assert_eq!((p.address.as_str(), p.lan.as_str()), ("203.0.113.50:27960", "192.168.1.20:27960"));
    let mut bare = Games::new(None, Box::new(|| 0));
    let b = bare.register(&base(json!({"lan": "100.100.1.2"})), "127.0.0.1").unwrap();
    assert_eq!(bare.join_for(&b, "198.51.100.9"), "100.100.1.2:27960");
    assert_eq!(bare.public(&b).address, "");
}

#[test]
fn stun_and_knocks() {
    let (t, mut games) = clock(1_000_000);
    let g = games.register(&base(json!({"stun": "203.0.113.5:61022", "lan": "192.168.1.20"})), "203.0.113.5").unwrap();
    assert_eq!(games.join_for(&g, "198.51.100.9"), "203.0.113.5:61022");
    assert!(games.register(&base(json!({"port": 27961, "stun": "192.168.1.20:5000"})), "203.0.113.5").unwrap_err().msg.contains("stun"));
    assert_eq!(games.knock(&g.code, &obj(json!({"stun": "198.51.100.9:50000"})), "198.51.100.9").unwrap(), "198.51.100.9:50000");
    assert_eq!(games.knock(&g.code.to_lowercase(), &obj(json!({"port": "51000"})), "::ffff:198.51.100.10").unwrap(), "198.51.100.10:51000");
    assert!(games.knock(&g.code, &obj(json!({})), "198.51.100.11").is_err());
    assert!(games.knock("ZZZZZ", &obj(json!({"port": 1})), "198.51.100.11").unwrap_err().msg.contains("no game"));
    assert!(games.take_knocks(&g.code, Some("wrong")).unwrap_err().msg.contains("not your game"));
    assert_eq!(games.take_knocks(&g.code, Some(&g.token)).unwrap(), vec!["198.51.100.9:50000", "198.51.100.10:51000"]);
    assert!(games.take_knocks(&g.code, Some(&g.token)).unwrap().is_empty());
    games.knock(&g.code, &obj(json!({"port": 52000})), "198.51.100.12").unwrap();
    advance(&t, (KNOCK_S + 1) * 1000);
    assert!(games.take_knocks(&g.code, Some(&g.token)).unwrap().is_empty());
}

#[test]
fn players_reported_and_the_hosts_word() {
    let mut games = Games::new(None, Box::new(|| 5000));
    let g = games.register(&base(json!({})), "127.0.0.1").unwrap();
    let rows = "0\thost\t\t0\tGeorge\n1\tonline\t192.168.1.20:27961\t42\tLizzie\n9\tbogus\n";
    assert!(games.report_players(&g.code, Some("wrong"), rows).is_err());
    // nothing said yet: only that this lobby speaks it
    assert_eq!(games.report_players(&g.code, Some(&g.token), rows).unwrap(), vec!["admin 1"]);
    let p = games.roster(&g.code);
    assert_eq!(p.len(), 2);
    assert_eq!(p[1], Player { slot: 1, state: "online".into(), addr: "192.168.1.20:27961".into(), ping: 42, name: "Lizzie".into() });
    assert_eq!(p[1].ip(), "192.168.1.20");
    games.set_muted("192.168.1.20:27961", true);
    games.set_banned(vec!["203.0.113.9".into()]);
    games.kick("192.168.1.20:27961");
    assert_eq!(games.report_players(&g.code, Some(&g.token), rows).unwrap(),
               vec!["admin 1", "ban 203.0.113.9", "mute 192.168.1.20:27961", "kick 192.168.1.20:27961"]);
    // a kick is said once; an unmute is gone at the next
    games.set_muted("192.168.1.20:27961", false);
    assert!(games.is_muted("192.168.1.20:27961") == false);
    assert_eq!(games.report_players(&g.code, Some(&g.token), rows).unwrap(), vec!["admin 1", "ban 203.0.113.9"]);
    assert!(games.roster("ZZZZZ").is_empty());
}
