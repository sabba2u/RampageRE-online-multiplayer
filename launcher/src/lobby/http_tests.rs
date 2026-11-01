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
fn knocks_with_players_hear_the_hosts_word() {
    let (s, url) = start();
    let (code, token) = register(&url);
    s.games.lock().unwrap().kick("192.168.1.20:27961");
    let rows = "0%09host%09%090%09George%0A1%09online%09192.168.1.20%3A27961%0942%09Lizzie%0A";
    assert_eq!(call("GET", &format!("{url}/games/{code}/knocks?token={token}&players={rows}"), false, None),
               (200, "ok\nadmin 1\nkick 192.168.1.20:27961\n".into()));
    let p = s.games.lock().unwrap().roster(&code);
    assert_eq!((p.len(), p[1].name.as_str(), p[1].ping), (2, "Lizzie", 42));
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

/// A lobby already listening on IPv6 only (Node's listen(port) is [::], dual-stack) must not be shadowed on IPv4.
#[test]
fn port_in_use_by_ipv6_lobby() {
    let other = std::net::TcpListener::bind("[::1]:0").unwrap();
    let port = other.local_addr().unwrap().port();
    assert!(matches!(LobbyServer::start(port, None), Err(StartError::PortInUse)));
}

fn health_within(url: &str, secs: u64) -> bool {
    let agent = ureq::AgentBuilder::new().timeout(std::time::Duration::from_secs(secs)).build();
    agent.get(&format!("{url}/health")).call().map_or(false, |r| r.status() == 200)
}

/// Strangers on TCP 19980 that send a body slowly, or ask and never read, must not stop friends or Stop hosting.
#[test]
fn slow_and_stalled_clients_dont_block() {
    use std::io::Write;
    let (s, url) = start();
    let addr = format!("127.0.0.1:{}", s.port);
    let mut held = Vec::new();
    for _ in 0..6 {
        let mut c = std::net::TcpStream::connect(&addr).unwrap();
        c.write_all(b"POST /games HTTP/1.1\r\nHost: x\r\nContent-Length: 4000\r\n\r\nab").unwrap();
        held.push(c);
    }
    for _ in 0..6 {
        let mut c = std::net::TcpStream::connect(&addr).unwrap();
        for _ in 0..8 {
            c.write_all(b"GET /logo.png HTTP/1.1\r\nHost: x\r\n\r\n").unwrap();
        }
        held.push(c);
    }
    std::thread::sleep(std::time::Duration::from_millis(500));
    assert!(health_within(&url, 3), "an honest request must still be answered");
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        s.stop();
        let _ = tx.send(());
    });
    assert!(rx.recv_timeout(std::time::Duration::from_secs(3)).is_ok(), "stop must not wait on stuck clients");
    drop(held);
}

#[test]
fn launcher_knows_the_public_address() {
    let (s, url) = start();
    let body = serde_json::json!({"name": "h", "port": 27960, "build": "0000ABCD", "lan": "192.168.1.20"});
    let code = s.games.lock().unwrap().register(body.as_object().unwrap(), "127.0.0.1").unwrap().code;
    s.set_public_address("203.0.113.50");
    let g = s.games.lock().unwrap().get(&code).unwrap().clone();
    assert_eq!(s.games.lock().unwrap().join_for(&g, "198.51.100.9"), "203.0.113.50:27960");
    drop(url);
}
