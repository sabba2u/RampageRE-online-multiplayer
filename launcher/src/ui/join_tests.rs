use super::join::*;
use crate::game::Start;
use crate::links::Link;
use crate::lobby::http::LobbyServer;
use crate::settings::Settings;
use serde_json::json;

#[test]
fn code_uses_newest_server() {
    let s = Settings { servers: vec!["http://a:19980".into(), "http://b:19980".into()], ..Default::default() };
    assert_eq!(join_target(&Link::Code("K7PQ2".into()), &s).unwrap(),
               (Start::Join { target: "K7PQ2".into(), lobby: Some("http://a:19980".into()) }, None));
}

#[test]
fn code_without_server() {
    assert_eq!(join_target(&Link::Code("K7PQ2".into()), &Settings::default()).unwrap_err(),
               NO_SERVER);
}

#[test]
fn codeat_remembers() {
    let l = Link::CodeAt { code: "K7PQ2".into(), server: "http://203.0.113.5:19980".into() };
    assert_eq!(join_target(&l, &Settings::default()).unwrap(),
               (Start::Join { target: "K7PQ2".into(), lobby: Some("http://203.0.113.5:19980".into()) },
                Some("http://203.0.113.5:19980".into())));
}

#[test]
fn direct_has_no_lobby() {
    let s = Settings { servers: vec!["http://a:19980".into()], ..Default::default() };
    assert_eq!(join_target(&Link::Direct("203.0.113.5:27960".into()), &s).unwrap(),
               (Start::Join { target: "203.0.113.5:27960".into(), lobby: None }, None));
}

#[test]
fn check_build_against_lobby() {
    let s = LobbyServer::start(0, None).unwrap();
    let body = json!({"name": "host", "players": 1, "max": 2, "port": 27960, "build": "0000ABCD"});
    let code = s.games.lock().unwrap().register(body.as_object().unwrap(), "127.0.0.1").unwrap().code;
    let url = format!("http://127.0.0.1:{}", s.port);
    assert_eq!(check_build(&url, &code, "0000ABCD"), Ok(()));
    assert_eq!(check_build(&url, &code, "FFFF0000").unwrap_err(), DIFFERENT_VERSION);
    assert_eq!(check_build(&url, "ZZZZZ", "0000ABCD").unwrap_err(), NO_SUCH_CODE);
    let free = std::net::TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port();
    assert_eq!(check_build(&format!("http://127.0.0.1:{free}"), &code, "0000ABCD").unwrap_err(), CANT_REACH);
}

#[test]
fn no_game_is_said_first() {
    let none: Result<std::path::PathBuf, String> = Err(crate::game::NO_GAME.into());
    assert_eq!(plan_join(&Link::Code("K7PQ2".into()), &Settings::default(), &none).unwrap_err(), crate::game::NO_GAME);
    let l = Link::CodeAt { code: "K7PQ2".into(), server: "http://203.0.113.5:19980".into() };
    assert_eq!(plan_join(&l, &Settings::default(), &none).unwrap_err(), crate::game::NO_GAME);
}

/// Any web page can open a link: its server is only remembered once the join went through.
#[test]
fn link_server_remembered_only_after_success() {
    let d = std::env::temp_dir().join(format!("rampage-launcher-link-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    std::fs::write(d.join("RampageRE.exe"), b"x").unwrap();
    let file = d.join("launcher.json");
    let free = std::net::TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port();
    let r = open_link_in(&format!("rampage://join/K7PQ2@127.0.0.1:{free}"), &d, &file);
    assert_eq!(r.unwrap_err(), CANT_REACH);
    assert!(Settings::load_from(&file).servers.is_empty());
}

#[test]
fn players_read_from_the_server() {
    let s = LobbyServer::start(0, None).unwrap();
    let url = format!("http://127.0.0.1:{}", s.port);
    let g = s.games.lock().unwrap().register(json!({"name": "a", "monster": "", "city": "", "players": 1, "max": 2,
        "port": 27960, "build": "CAFE1234"}).as_object().unwrap(), "127.0.0.1").unwrap();
    s.games.lock().unwrap().report_players(&g.code, Some(&g.token), "0\thost\t\t0\tGeorge\n1\tonline\t10.0.0.2:5\t30\t\n").unwrap();
    let p = fetch_players(&url, &g.code).unwrap();
    assert_eq!(p.len(), 2);
    assert_eq!((p[0].name.as_str(), p[1].state.as_str(), p[1].ping), ("George", "online", 30));
    assert!(p[1].addr.is_empty());
    assert_eq!(fetch_players(&url, "ZZZZZ").unwrap_err(), GAME_GONE);
}

#[test]
fn connect_checks_its_a_server() {
    let s = LobbyServer::start(0, None).unwrap();
    assert_eq!(check_server(&format!("http://127.0.0.1:{}", s.port)), Ok(()));
    let free = std::net::TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port();
    assert_eq!(check_server(&format!("http://127.0.0.1:{free}")).unwrap_err(), CANT_REACH);
}

#[test]
fn rooms_from_the_servers_list() {
    let body = r#"{"games":[
        {"code":"K7PQ2","name":"sabba","monster":"George","city":"Chicago 1","players":2,"max":4,"reserved":1,"build":"CAFE1234"},
        {"code":"ZZZZZ","name":"","monster":"","city":"","players":4,"max":4,"reserved":0,"build":"CAFE1234"}],
        "you":"1.2.3.4","heartbeat":15}"#;
    let rooms = parse_rooms(body).unwrap();
    assert_eq!(rooms.len(), 2);
    assert_eq!(rooms[0].code, "K7PQ2");
    assert_eq!(rooms[0].label(), "sabba · George · Chicago 1");
    assert_eq!(rooms[0].seats(), "2 / 4 (1 rejoining)");
    assert!(!rooms[0].full());
    assert!(rooms[1].full());
    assert_eq!(rooms[1].label(), "A room");
    assert!(parse_rooms("not json").is_err());
    assert!(parse_rooms(r#"{"games":[]}"#).unwrap().is_empty());
}

#[test]
fn public_ip_answer_checked() {
    assert_eq!(public_ip_from("203.0.113.5\n").unwrap(), "203.0.113.5");
    assert_eq!(public_ip_from("2001:db8::1").unwrap(), "2001:db8::1");
    assert!(public_ip_from("<html>oops</html>").is_err());
    assert!(public_ip_from("").is_err());
}
