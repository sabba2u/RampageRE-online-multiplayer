use super::game::*;
use super::settings::Settings;
use std::fs;

fn temp(name: &str) -> std::path::PathBuf {
    let d = std::env::temp_dir().join(format!("rampage-launcher-test-{name}-{}", std::process::id()));
    let _ = fs::remove_dir_all(&d);
    fs::create_dir_all(&d).unwrap();
    d
}

#[test]
fn build_id_fnv1a() {
    let d = temp("build");
    fs::write(d.join("abc.exe"), b"abc").unwrap();
    fs::write(d.join("empty.exe"), b"").unwrap();
    assert_eq!(build_id(&d.join("abc.exe")).unwrap(), "1A47E90B");
    assert_eq!(build_id(&d.join("empty.exe")).unwrap(), "811C9DC5");
}

#[test]
fn args_per_start() {
    let l = "http://127.0.0.1:19980".to_string();
    let (s, n) = ("--shell", "--no-four-players");
    assert_eq!(args(&Start::Host { lobby: l.clone() }), vec![s, n, "--host", "--lobby", "http://127.0.0.1:19980"]);
    assert_eq!(args(&Start::SteamHost), vec![s, n, "--steam-host"]);
    assert_eq!(args(&Start::Join { target: "K7PQ2".into(), lobby: Some(l.clone()) }), vec![s, n, "--lobby", l.as_str(), "--join", "K7PQ2"]);
    assert_eq!(args(&Start::Join { target: "203.0.113.5:27960".into(), lobby: None }), vec![s, n, "--join", "203.0.113.5:27960"]);
    assert_eq!(args(&Start::SteamWait), vec![s, n]);
    assert_eq!(args(&Start::Offline { four_players: true }), vec![s, "--four-players"]);
    assert_eq!(args(&Start::Offline { four_players: false }), vec![s, "--no-four-players"]);
}

#[test]
fn find_game_prefers_release() {
    let d = temp("find");
    assert_eq!(find_game(&d).unwrap_err(), NO_GAME);
    fs::write(d.join("RampageRE-dev.exe"), b"x").unwrap();
    assert_eq!(find_game(&d).unwrap(), d.join("RampageRE-dev.exe"));
    fs::write(d.join("RampageRE.exe"), b"x").unwrap();
    assert_eq!(find_game(&d).unwrap(), d.join("RampageRE.exe"));
}

#[test]
fn settings_round_trip_and_remember() {
    let d = temp("settings");
    let f = d.join("launcher.json");
    let mut s = Settings::load_from(&f);
    assert!(s.servers.is_empty() && !s.first_run_done);
    for i in 0..9 {
        s.remember(&format!("http://h{i}:19980"));
    }
    assert_eq!(s.servers.len(), 8);
    assert_eq!(s.servers[0], "http://h8:19980");
    s.remember("http://h3:19980");
    assert_eq!((s.servers[0].as_str(), s.servers.len()), ("http://h3:19980", 8));
    s.first_run_done = true;
    s.save_to(&f).unwrap();
    let back = Settings::load_from(&f);
    assert_eq!((back.servers.clone(), back.first_run_done), (s.servers.clone(), true));
    fs::write(&f, b"{ broken").unwrap();
    assert!(Settings::load_from(&f).servers.is_empty());
}

/// The real game against the launcher's sum: GAME_EXE and the build it listed (EXPECT_BUILD, from a lobby's /games).
#[test]
#[ignore]
fn build_id_matches_game() {
    let exe = std::env::var("GAME_EXE").expect("GAME_EXE");
    assert_eq!(build_id(std::path::Path::new(&exe)).unwrap(), std::env::var("EXPECT_BUILD").expect("EXPECT_BUILD"));
}

/// Two launchers (the window, and a clicked link) share the file: a change reads it first, so neither undoes the other.
#[test]
fn settings_update_keeps_the_other_writer() {
    let d = temp("update");
    let f = d.join("launcher.json");
    let window = Settings::load_from(&f);
    Settings::update_at(&f, |s| s.remember("http://from-a-link:19980"));
    let after = Settings::update_at(&f, |s| s.first_run_done = true);
    assert!(after.first_run_done && after.servers == vec!["http://from-a-link:19980".to_string()]);
    drop(window);
}

#[test]
fn old_game_without_online_play_is_refused() {
    let d = temp("old");
    fs::write(d.join("old.exe"), b"MZ...usage: RampageRE <data>...").unwrap();
    fs::write(d.join("new.exe"), b"MZ...--host --steam-host --join...").unwrap();
    assert!(!supports_online(&d.join("old.exe")));
    assert!(supports_online(&d.join("new.exe")));
    let e = start(&d.join("old.exe"), &Start::SteamHost).unwrap_err();
    assert_eq!(e.to_string(), OLD_GAME);
}

#[test]
fn bans_kept_once_and_lifted() {
    let mut s = Settings::default();
    s.ban("203.0.113.9", "Lizzie");
    s.ban("203.0.113.9", "Lizzie again");
    s.ban("198.51.100.2", "");
    assert_eq!(s.banned_ips(), vec!["203.0.113.9", "198.51.100.2"]);
    assert_eq!(s.banned[0].name, "Lizzie again");
    s.unban("203.0.113.9");
    assert_eq!(s.banned_ips(), vec!["198.51.100.2"]);
}

#[test]
fn play_local_players_choice() {
    // never chosen: 2 players
    assert_eq!(Settings::default().local_players(), 2);
    assert!(!Settings::default().four_players());
    for (n, four) in [(1, false), (2, false), (3, true), (4, true)] {
        let s = Settings { local_players: n, ..Default::default() };
        assert_eq!((s.local_players(), s.four_players()), (n, four));
    }
    assert_eq!(Settings { local_players: 9, ..Default::default() }.local_players(), 2);
}
