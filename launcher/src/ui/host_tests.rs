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
