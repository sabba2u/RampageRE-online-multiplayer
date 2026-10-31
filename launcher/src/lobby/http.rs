//! The lobby's plain HTTP side (lobby/src/http.ts, route for route), for RampageRE (src/netlobby.c) and the games page:
//!   POST   /games                register: answers "ok <code> <token> <heartbeat>" (Accept: text/plain) or JSON
//!   PUT    /games/<code>         the heartbeat; DELETE /games/<code> unlists (token in the body, ?token= or X-Token)
//!   GET    /games, /games.txt, /games/<code>[.txt]
//!   POST   /games/<code>/knock   a guest about to join; GET /games/<code>/knocks?token= the host's, every second
//!   GET    /, /logo.png, /background.jpg, /health
//! No Colyseus (/colyseus.js is a 404: the page then reads /games every few seconds), no link installers (the launcher
//! sets links up itself), no trustProxy (that is for the Node lobby behind nginx).
use super::addr::norm_ip;
use super::games::{is_code, Game, Games, LobbyError, HEARTBEAT_S};
use serde_json::{json, Map, Value};
use std::io::Read;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;
use tiny_http::{Header, Method, Request, Response, Server};

const PAGE: &str = include_str!("../../../lobby/public/index.html");
const LOGO: &[u8] = include_bytes!("../../assets/logo.png");
const BACKGROUND: &[u8] = include_bytes!("../../assets/background.jpg");
const WORKERS: usize = 4;
/// The game's bodies are a few hundred bytes. Anything larger, or without a length, is refused before it is read: a
/// slow sender can't hold a thread, and tiny_http reads bodies this small with the request.
const BODY_MAX: usize = 1024;

#[derive(Debug)]
pub enum StartError {
    PortInUse,
    Other(String),
}

pub struct LobbyServer {
    pub port: u16,
    pub games: Arc<Mutex<Games>>,
    server: Arc<Server>,
    stop: Arc<AtomicBool>,
    threads: Vec<JoinHandle<()>>,
}

impl LobbyServer {
    /// Listens on TCP `port` (0: any free one) on every address; a sweep drops silent games every 5 s. A lobby that
    /// already answers on this PC (the Node one listens on IPv6 too, where Windows would let this one in beside it)
    /// is PortInUse.
    pub fn start(port: u16, public_address: Option<String>) -> Result<LobbyServer, StartError> {
        if port != 0 {
            for a in [format!("127.0.0.1:{port}"), format!("[::1]:{port}")] {
                let a: std::net::SocketAddr = a.parse().unwrap();
                if std::net::TcpStream::connect_timeout(&a, Duration::from_millis(300)).is_ok() {
                    return Err(StartError::PortInUse);
                }
            }
        }
        let server = Server::http(("0.0.0.0", port)).map_err(|e| match e.downcast_ref::<std::io::Error>() {
            Some(io) if io.kind() == std::io::ErrorKind::AddrInUse => StartError::PortInUse,
            _ => StartError::Other(e.to_string()),
        })?;
        let port = server.server_addr().to_ip().map_or(port, |a| a.port());
        let (server, games) = (Arc::new(server), Arc::new(Mutex::new(Games::with_system_clock(public_address))));
        let stop = Arc::new(AtomicBool::new(false));
        let mut threads = Vec::new();
        for _ in 0..WORKERS {
            let (server, games) = (server.clone(), games.clone());
            // each request on a thread of its own: a client that never reads its answer holds only that one
            threads.push(std::thread::spawn(move || {
                while let Ok(req) = server.recv() {
                    let games = games.clone();
                    std::thread::spawn(move || handle(&games, req));
                }
            }));
        }
        let (g, s) = (games.clone(), stop.clone());
        threads.push(std::thread::spawn(move || {
            while !s.load(Ordering::Relaxed) {
                for _ in 0..50 {
                    if s.load(Ordering::Relaxed) {
                        return;
                    }
                    std::thread::sleep(Duration::from_millis(100));
                }
                g.lock().unwrap().sweep();
            }
        }));
        Ok(LobbyServer { port, games, server, stop, threads })
    }

    pub fn stop(mut self) { self.shutdown(); }

    /// This PC's address on the internet (the launcher's UPnP found it), for guests there when the game knows none.
    pub fn set_public_address(&self, ip: &str) { self.games.lock().unwrap().set_public_address(Some(ip.to_string())); }

    fn shutdown(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        for _ in 0..WORKERS {
            self.server.unblock();
        }
        for t in self.threads.drain(..) {
            let _ = t.join();
        }
    }
}

impl Drop for LobbyServer {
    fn drop(&mut self) { self.shutdown(); }
}

fn header(name: &str, value: &str) -> Header { Header::from_bytes(name.as_bytes(), value.as_bytes()).unwrap() }

fn send(req: Request, status: u16, body: Vec<u8>, ctype: &str) {
    let mut r = Response::from_data(body).with_status_code(status);
    if status != 204 {
        r = r.with_header(header("Content-Type", ctype));
    }
    let r = r.with_header(header("Access-Control-Allow-Origin", "*")).with_header(header("Cache-Control", "no-store"));
    let _ = req.respond(r);
}
fn text(req: Request, status: u16, body: &str) { send(req, status, body.as_bytes().to_vec(), "text/plain; charset=utf-8") }
fn json_out(req: Request, status: u16, v: &Value) { send(req, status, v.to_string().into_bytes(), "application/json") }

fn get_header(req: &Request, name: &'static str) -> Option<String> {
    req.headers().iter().find(|h| h.field.equiv(name)).map(|h| h.value.as_str().to_string())
}

/// application/x-www-form-urlencoded (and query strings): + is a space, %XX a byte.
fn form(s: &str) -> Map<String, Value> {
    let dec = |t: &str| {
        let b = t.as_bytes();
        let mut out = Vec::with_capacity(b.len());
        let mut i = 0;
        while i < b.len() {
            match b[i] {
                b'+' => out.push(b' '),
                b'%' if i + 2 < b.len() => {
                    match u8::from_str_radix(std::str::from_utf8(&b[i + 1..i + 3]).unwrap_or("zz"), 16) {
                        Ok(v) => { out.push(v); i += 2; }
                        Err(_) => out.push(b'%'),
                    }
                }
                c => out.push(c),
            }
            i += 1;
        }
        String::from_utf8_lossy(&out).into_owned()
    };
    let mut m = Map::new();
    for pair in s.split('&').filter(|p| !p.is_empty()) {
        let (k, v) = pair.split_once('=').unwrap_or((pair, ""));
        m.insert(dec(k), Value::String(dec(v)));
    }
    m
}

fn read_body(req: &mut Request) -> Result<Map<String, Value>, String> {
    // no length: no body at all, unless it is sent in chunks (refused: its length isn't known up front)
    let chunked = get_header(req, "Transfer-Encoding").is_some();
    if chunked || req.body_length().unwrap_or(0) > BODY_MAX {
        return Err("too long".into());
    }
    if req.body_length().is_none() {
        return Ok(Map::new());
    }
    let mut buf = Vec::new();
    req.as_reader().take(BODY_MAX as u64 + 1).read_to_end(&mut buf).map_err(|e| e.to_string())?;
    if buf.len() > BODY_MAX {
        return Err("too long".into());
    }
    let text = String::from_utf8_lossy(&buf).into_owned();
    if text.trim().is_empty() {
        return Ok(Map::new());
    }
    if get_header(req, "Content-Type").unwrap_or_default().contains("json") {
        return match serde_json::from_str::<Value>(&text).map_err(|e| e.to_string())? {
            Value::Object(o) => Ok(o),
            _ => Err("expected a JSON object".into()),
        };
    }
    Ok(form(&text))
}

/// `/games/<code>` and the parts after it, when the code is letters and digits.
fn game_path(p: &str) -> Option<(&str, &str)> {
    let rest = p.strip_prefix("/games/")?;
    let (code, tail) = rest.split_once('/').map_or((rest, ""), |(c, t)| (c, t));
    let code_only = code.strip_suffix(".txt").unwrap_or(code);
    (!code_only.is_empty() && code_only.bytes().all(|c| c.is_ascii_alphanumeric())).then_some((code, tail))
}

fn view(games: &Games, g: &Game, from: &str) -> Value {
    let mut v = serde_json::to_value(games.public(g)).unwrap();
    v["join"] = Value::String(games.join_for(g, from));
    v
}

fn field(s: &str) -> String { s.replace(['\t', '\r', '\n'], " ") }

fn games_txt(games: &Games, list: &[&Game], from: &str) -> String {
    let mut out = String::from("rampage-lobby 1\n");
    for g in list {
        let join = games.join_for(g, from);
        let row = [g.code.clone(), if join.is_empty() { "-".into() } else { join }, g.players.to_string(), g.max.to_string(),
                   g.build.clone(), g.name.clone(), g.monster.clone(), g.city.clone(), g.reserved.to_string()];
        out += &row.iter().map(|s| field(s)).collect::<Vec<_>>().join("\t");
        out.push('\n');
    }
    out
}

fn handle(games: &Mutex<Games>, mut req: Request) {
    let from = norm_ip(&req.remote_addr().map(|a| a.ip().to_string()).unwrap_or_default());
    let wants_text = get_header(&req, "Accept").unwrap_or_default().contains("text/plain");
    let url = req.url().to_string();
    let (p, query) = url.split_once('?').unwrap_or((&url, ""));
    let (p, query) = (p.to_string(), form(query));
    let q_token = query.get("token").and_then(|v| v.as_str()).map(String::from);
    let method = req.method().clone();
    if method == Method::Options {
        let r = Response::empty(204)
            .with_header(header("Access-Control-Allow-Origin", "*"))
            .with_header(header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE"))
            .with_header(header("Access-Control-Allow-Headers", "Content-Type, X-Token"));
        let _ = req.respond(r);
        return;
    }
    let result: Result<(), (Request, LobbyError)> = (|| {
        let fail = |req: Request, e: LobbyError| Err((req, e));
        let bad = |m: String| LobbyError { status: 400, msg: m };
        if let Some((code, tail)) = game_path(&p) {
            if tail == "knock" && method == Method::Post {
                let body = match read_body(&mut req) { Ok(b) => b, Err(e) => return fail(req, bad(e)) };
                let r = games.lock().unwrap().knock(code, &body, &from); // (the lock let go before answering)
                return match r {
                    Ok(addr) => { println!("knock on {} from {addr}", code.to_uppercase()); text(req, 200, "ok\n"); Ok(()) }
                    Err(e) => fail(req, e),
                };
            }
            if tail == "knocks" && method == Method::Get {
                let token = get_header(&req, "X-Token").or(q_token.clone());
                let r = games.lock().unwrap().take_knocks(code, token.as_deref());
                return match r {
                    Ok(k) => {
                        let mut out = vec!["ok".to_string()];
                        out.extend(k.into_iter().map(|a| format!("knock {a}")));
                        text(req, 200, &(out.join("\n") + "\n"));
                        Ok(())
                    }
                    Err(e) => fail(req, e),
                };
            }
        }
        if method == Method::Get {
            match p.as_str() {
                "/health" => { text(req, 200, "rampage_lobby\n"); return Ok(()); }
                "/" => { send(req, 200, PAGE.as_bytes().to_vec(), "text/html; charset=utf-8"); return Ok(()); }
                "/logo.png" => { send(req, 200, LOGO.to_vec(), "image/png"); return Ok(()); }
                "/background.jpg" => { send(req, 200, BACKGROUND.to_vec(), "image/jpeg"); return Ok(()); }
                "/games" => {
                    let g = games.lock().unwrap();
                    let list: Vec<Value> = g.list().into_iter().map(|x| view(&g, x, &from)).collect();
                    let v = json!({"games": list, "you": from, "heartbeat": HEARTBEAT_S});
                    drop(g);
                    json_out(req, 200, &v);
                    return Ok(());
                }
                "/games.txt" => {
                    let g = games.lock().unwrap();
                    let t = games_txt(&g, &g.list(), &from);
                    drop(g);
                    text(req, 200, &t);
                    return Ok(());
                }
                _ => {}
            }
            if let Some((code, "")) = game_path(&p) {
                let (c, is_txt) = code.strip_suffix(".txt").map_or((code, false), |c| (c, true));
                let g = games.lock().unwrap();
                let found = if is_code(c) { g.get(c) } else { None };
                let Some(found) = found else {
                    drop(g);
                    text(req, 404, "error no game with that code\n");
                    return Ok(());
                };
                let out = if is_txt { Err(games_txt(&g, &[found], &from)) } else { Ok(view(&g, found, &from)) };
                drop(g);
                match out {
                    Err(t) => text(req, 200, &t),
                    Ok(v) => json_out(req, 200, &v),
                }
                return Ok(());
            }
        }
        if method == Method::Post && p == "/games" {
            let body = match read_body(&mut req) { Ok(b) => b, Err(e) => return fail(req, bad(e)) };
            let mut g = games.lock().unwrap();
            let r = g.register(&body, &from).map(|x| (x.clone(), g.public_address(&x)));
            drop(g);
            return match r {
                Ok((x, address)) => {
                    let city = if x.city.is_empty() { "menus" } else { &x.city };
                    let monster = if x.monster.is_empty() { "-" } else { &x.monster };
                    println!("listed {}: {} ({monster}, {city}) from {from} port {}", x.code, x.name, x.port);
                    if wants_text {
                        text(req, 200, &format!("ok {} {} {HEARTBEAT_S}\n", x.code, x.token));
                    } else {
                        json_out(req, 200, &json!({"code": x.code, "token": x.token, "heartbeat": HEARTBEAT_S,
                                                   "address": address, "lan": x.lan}));
                    }
                    Ok(())
                }
                Err(e) => fail(req, e),
            };
        }
        if let Some((code, "")) = game_path(&p) {
            if (method == Method::Put || method == Method::Delete) && !code.ends_with(".txt") {
                let mut body = match read_body(&mut req) { Ok(b) => b, Err(e) => return fail(req, bad(e)) };
                let token = body.get("token").filter(|v| !v.is_null()).map(|v| match v {
                    Value::String(s) => s.clone(),
                    other => other.to_string(),
                }).or_else(|| get_header(&req, "X-Token")).or(q_token.clone());
                let r = if method == Method::Put {
                    if let Some(t) = &token {
                        body.insert("token".into(), Value::String(t.clone()));
                    }
                    games.lock().unwrap().heartbeat(code, &body).map(|_| ())
                } else {
                    let r = games.lock().unwrap().unregister(code, token.as_deref());
                    if r.is_ok() {
                        println!("unlisted {}", code.to_uppercase());
                    }
                    r
                };
                return match r {
                    Ok(()) if wants_text => { text(req, 200, "ok\n"); Ok(()) }
                    Ok(()) => { send(req, 204, Vec::new(), ""); Ok(()) }
                    Err(e) => fail(req, e),
                };
            }
        }
        text(req, 404, "error not found\n");
        Ok(())
    })();
    if let Err((req, e)) = result {
        text(req, e.status, &format!("error {}\n", e.msg));
    }
}
