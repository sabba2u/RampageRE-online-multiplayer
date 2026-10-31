//! The HTTP side against a real server on a free port, as the game and lobby/test talk to it.
use super::http::{LobbyServer, StartError};

fn call(method: &str, url: &str, accept_text: bool, form: Option<&str>) -> (u16, String) {
    let mut r = ureq::request(method, url);
    if accept_text {
        r = r.set("Accept", "text/plain");
    }
    let res = match form {
        Some(f) => r.set("Content-Type", "application/x-www-form-urlencoded").send_string(f),
        None => r.call(),
    };
    match res {
        Ok(resp) => (resp.status(), resp.into_string().unwrap()),
        Err(ureq::Error::Status(code, resp)) => (code, resp.into_string().unwrap()),
        Err(e) => panic!("{e}"),
    }
}

const GAME: &str = "name=sabba&monster=George&city=Chicago%201&players=1&max=2&port=27960&build=cafe1234";

fn start() -> (LobbyServer, String) {
    let s = LobbyServer::start(0, None).unwrap();
    let url = format!("http://127.0.0.1:{}", s.port);
    (s, url)
}

fn register(url: &str) -> (String, String) {
    let (st, body) = call("POST", &format!("{url}/games"), true, Some(GAME));
    assert_eq!(st, 200, "{body}");
    let p: Vec<&str> = body.trim_end().split(' ').collect();
    assert_eq!((p[0], p[3]), ("ok", "10"));
    (p[1].to_string(), p[2].to_string())
}

#[test]
fn register_text_and_json() {
    let (_s, url) = start();
    let (code, token) = register(&url);
    assert_eq!((code.len(), token.len()), (5, 32));
    let (st, body) = call("POST", &format!("{url}/games"), false, Some(&GAME.replace("27960", "27961")));
    assert_eq!(st, 200);
    let v: serde_json::Value = serde_json::from_str(&body).unwrap();
    assert_eq!(v["heartbeat"], 10);
    assert!(v["code"].as_str().unwrap().len() == 5 && v["token"].as_str().unwrap().len() == 32);
}

#[test]
fn games_txt_format() {
    let (_s, url) = start();
    let (code, _) = register(&url);
    let (st, body) = call("GET", &format!("{url}/games.txt"), false, None);
    assert_eq!(st, 200);
    let lines: Vec<&str> = body.trim_end().split('\n').collect();
    assert_eq!(lines[0], "rampage-lobby 1");
    let f: Vec<&str> = lines[1].split('\t').collect();
    assert_eq!(f, vec![code.as_str(), "127.0.0.1:27960", "1", "2", "CAFE1234", "sabba", "George", "Chicago 1", "0"]);
    let (st, one) = call("GET", &format!("{url}/games/{}.txt", code.to_lowercase()), false, None);
    assert_eq!((st, one), (200, body));
    let (st, json) = call("GET", &format!("{url}/games"), false, None);
    let v: serde_json::Value = serde_json::from_str(&json).unwrap();
    assert_eq!((st, v["you"].as_str(), v["games"][0]["join"].as_str()), (200, Some("127.0.0.1"), Some("127.0.0.1:27960")));
    assert_eq!(call("GET", &format!("{url}/games/ZZZZZ.txt"), false, None), (404, "error no game with that code\n".into()));
}

#[test]
fn heartbeat_and_delete() {
    let (_s, url) = start();
    let (code, token) = register(&url);
    let at = format!("{url}/games/{code}");
    assert_eq!(call("PUT", &at, false, Some(&format!("token={token}&players=2"))), (204, String::new()));
    assert_eq!(call("PUT", &at, true, Some(&format!("token={token}"))), (200, "ok\n".into()));
    assert_eq!(call("PUT", &at, false, Some("token=nope")), (403, "error not your game\n".into()));
    assert_eq!(call("DELETE", &format!("{at}?token={token}"), false, None).0, 204);
    assert_eq!(call("PUT", &at, false, Some(&format!("token={token}"))).0, 404);
}

#[test]
fn knock_then_knocks() {
    let (_s, url) = start();
    let (code, token) = register(&url);
    assert_eq!(call("POST", &format!("{url}/games/{code}/knock"), false, Some("port=51000")), (200, "ok\n".into()));
    assert_eq!(call("GET", &format!("{url}/games/{code}/knocks?token={token}"), false, None),
               (200, "ok\nknock 127.0.0.1:51000\n".into()));
    assert_eq!(call("GET", &format!("{url}/games/{code}/knocks?token={token}"), false, None), (200, "ok\n".into()));
}

#[test]
fn health() {
    let (_s, url) = start();
    assert_eq!(call("GET", &format!("{url}/health"), false, None), (200, "rampage_lobby\n".into()));
    assert_eq!(call("GET", &format!("{url}/nope"), false, None), (404, "error not found\n".into()));
}

#[test]
fn port_in_use() {
    let (s, _url) = start();
    match LobbyServer::start(s.port, None) {
        Err(StartError::PortInUse) => {}
        other => panic!("expected PortInUse, got {:?}", other.map(|x| x.port)),
    }
}
