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
    // online, over the network and over Steam: up to 4 players (3 and 4 Player in the menus)
    let (s, f) = ("--shell", "--four-players");
    // hosting: the menus offer up to 8 Player (the guests' screens follow the host's)
    let m = ["--menu-players", "8"];
    let c = ["--seats", "8"];
    assert_eq!(args(&Start::Host { lobby: l.clone(), port: 19980, players: 8 }), vec![s, f, m[0], m[1], c[0], c[1], "--host", "--lobby", "http://127.0.0.1:19980"]);
    // (another port, set in Settings: after --host, where the game reads it)
    assert_eq!(args(&Start::Host { lobby: "http://127.0.0.1:20000".into(), port: 20000, players: 8 }),
               vec![s, f, m[0], m[1], c[0], c[1], "--host", "20000", "--lobby", "http://127.0.0.1:20000"]);
    assert_eq!(args(&Start::SteamHost { players: 8 }), vec![s, f, m[0], m[1], c[0], c[1], "--steam-host"]);
    assert_eq!(args(&Start::HostLan { players: 8 }), vec![s, f, m[0], m[1], c[0], c[1], "--host"]);
    // (Players: the room's size, the menus' N Player up to it)
    assert_eq!(args(&Start::SteamHost { players: 3 }), vec![s, f, "--menu-players", "3", "--seats", "3", "--steam-host"]);
    assert_eq!(args(&Start::HostLan { players: 1 })[2..6], ["--menu-players", "2", "--seats", "2"]);
    assert_eq!(args(&Start::Join { target: "K7PQ2".into(), lobby: Some(l.clone()) }), vec![s, f, "--lobby", l.as_str(), "--join", "K7PQ2"]);
    assert_eq!(args(&Start::Join { target: "203.0.113.5:27960".into(), lobby: None }), vec![s, f, "--join", "203.0.113.5:27960"]);
    // Join on Steam: the game signs in to Steam only when asked (--steam), so other starts never show "Spacewar"
    assert_eq!(args(&Start::SteamWait), vec![s, f, "--steam"]);
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
    let e = start(&d.join("old.exe"), &Start::SteamHost { players: 8 }).unwrap_err();
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
    // 2 and 5 to 8 host (friends join over the network: a PC takes four controllers), 1, 3 and 4 play on this PC
    for (n, four, hosts) in [(1, false, false), (2, false, true), (3, true, false), (4, true, false), (5, true, true),
                             (6, true, true), (7, true, true), (8, true, true)] {
        let s = Settings { local_players: n, ..Default::default() };
        assert_eq!((s.local_players(), s.four_players(), s.hosts()), (n, four, hosts));
    }
    assert_eq!(Settings { local_players: 9, ..Default::default() }.local_players(), 2);
}

#[test]
fn unlock_all_on_by_default_and_passed() {
    assert!(Settings::default().unlock_all);
    let old: Settings = serde_json::from_str(r#"{"servers": []}"#).unwrap();
    assert!(old.unlock_all, "a settings file from before the box: on");
    let off: Settings = serde_json::from_str(r#"{"unlock_all": false}"#).unwrap();
    assert!(!off.unlock_all);
    let a = start_args(&Start::Offline { four_players: false }, true, false);
    assert_eq!(&a[a.len() - 2..], ["--cheat", "141421"]);
    assert!(!start_args(&Start::Offline { four_players: false }, false, false).contains(&"--cheat".to_string()));
}

#[test]
fn unlock_moves_on_by_default_and_passed() {
    assert!(Settings::default().unlock_moves);
    let old: Settings = serde_json::from_str(r#"{"servers": []}"#).unwrap();
    assert!(old.unlock_moves, "a settings file from before the box: on");
    let off: Settings = serde_json::from_str(r#"{"unlock_moves": false}"#).unwrap();
    assert!(!off.unlock_moves);
    let o = Start::Offline { four_players: false };
    let a = start_args(&o, false, true);
    assert_eq!(&a[a.len() - 2..], ["--cheat", "011235"]);
    let both = start_args(&o, true, true);
    assert_eq!(&both[both.len() - 2..], ["--cheat", "141421,011235"]);
    assert_eq!(both.iter().filter(|x| *x == "--cheat").count(), 1);
}

#[test]
fn host_local_hosts_without_a_server() {
    assert_eq!(args(&Start::HostLan { players: 8 }), vec!["--shell", "--four-players", "--menu-players", "8", "--seats", "8", "--host"]);
}

#[test]
fn game_options_passed() {
    let d = Settings::default();
    assert_eq!(d.time_of_day, "game");
    assert_eq!(d.smoothing, "off");
    assert!(game_options(&d).is_empty(), "the defaults pass nothing: the game's own rampage.cfg decides");
    let mut s = d.clone();
    s.time_of_day = "night".into();
    s.smoothing = "high".into();
    assert_eq!(game_options(&s), vec!["--time-of-day", "night", "--smaa", "high", "--msaa", "4"]);
    s.time_of_day = "day".into();
    s.smoothing = "medium".into();
    assert_eq!(game_options(&s), vec!["--time-of-day", "day", "--smaa", "medium"]);
    s.time_of_day = "bogus".into();
    s.smoothing = "bogus".into();
    assert!(game_options(&s).is_empty());
    let old: Settings = serde_json::from_str(r#"{"unlock_all": true}"#).unwrap();
    assert_eq!((old.time_of_day.as_str(), old.smoothing.as_str(), old.weather.as_str()), ("game", "off", "clear"));
    let mut w = Settings::default();
    w.weather = "storm".into();
    assert_eq!(game_options(&w), vec!["--weather", "storm"]);
    w.weather = "rain".into();
    assert_eq!(game_options(&w), vec!["--weather", "rain"]);
    w.weather = "random".into();
    assert_eq!(game_options(&w), vec!["--weather", "random"]);
    let mut f = Settings::default();
    assert_eq!(f.look, "normal");
    f.look = "film".into();
    assert_eq!(game_options(&f), vec!["--film"]);
    let mut o = Settings::default();
    assert!(!o.own_outline, "off by default (the game's own Insert panel keeps it too)");
    o.own_outline = true;
    assert_eq!(game_options(&o), vec!["--own-outline"]);
    let mut r = Settings::default();
    assert!(!r.shuffle_buildings);
    r.shuffle_buildings = true;
    let o = game_options(&r);
    assert_eq!(o[0], "--random-blocks");
    let seed: u32 = o[1].parse().unwrap();
    assert!(seed > 0 && seed < 0x8000_0000, "a seed the game takes (31 bits, not 0)");
}
