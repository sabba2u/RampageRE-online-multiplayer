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
               "Paste the group link from your host first.");
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
