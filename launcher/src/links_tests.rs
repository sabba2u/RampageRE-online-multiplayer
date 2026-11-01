use super::links::*;
use winreg::{enums::HKEY_CURRENT_USER, RegKey};
use std::path::Path;

#[test]
fn parses_the_three_forms() {
    assert_eq!(parse_link("rampage://join/k7pq2").unwrap(), Link::Code("K7PQ2".into()));
    assert_eq!(parse_link("rampage://join/K7PQ2/").unwrap(), Link::Code("K7PQ2".into()));
    assert_eq!(parse_link("rampage://join/K7PQ2@203.0.113.5").unwrap(),
               Link::CodeAt { code: "K7PQ2".into(), server: "http://203.0.113.5:19980".into() });
    assert_eq!(parse_link("rampage://join/K7PQ2@lobby.example.org:8080").unwrap(),
               Link::CodeAt { code: "K7PQ2".into(), server: "http://lobby.example.org:8080".into() });
    assert_eq!(parse_link("rampage://join/203.0.113.5:27960").unwrap(), Link::Direct("203.0.113.5:27960".into()));
}

#[test]
fn refuses_the_rest() {
    let long = format!("rampage://join/K7PQ2@{}", "a".repeat(2048));
    for bad in ["rampage://join/K7PQ2@a;calc", "rampage://host/K7PQ2", "rampage://join/", "rampage://join/K7PQ0",
                long.as_str(), "rampage://join/ÄBCDE", "http://join/K7PQ2", "rampage://join/K7PQ2@", "rampage://join/a b"] {
        assert!(parse_link(bad).is_err(), "{bad} should be refused");
    }
}

#[test]
fn register_status_unregister() {
    let (root, _) = RegKey::predef(HKEY_CURRENT_USER).create_subkey(r"Software\RampageRELauncherTest\Classes").unwrap();
    let me = Path::new(r"C:\Games\RampageRE\RampageRE-Launcher.exe");
    let _ = unregister(&root);
    assert_eq!(status(&root, me), LinkStatus::NotSet);
    register(&root, me).unwrap();
    assert_eq!(status(&root, me), LinkStatus::Ok);
    assert_eq!(status(&root, Path::new(r"c:\games\rampagere\rampagere-launcher.EXE")), LinkStatus::Ok);
    let old = Path::new(r"D:\Old\RampageRE-Launcher.exe");
    register(&root, old).unwrap();
    assert_eq!(status(&root, me), LinkStatus::Stale(old.display().to_string()));
    unregister(&root).unwrap();
    assert_eq!(status(&root, me), LinkStatus::NotSet);
    RegKey::predef(HKEY_CURRENT_USER).delete_subkey_all(r"Software\RampageRELauncherTest").unwrap();
}

#[test]
fn server_typed_or_from_a_link() {
    assert_eq!(server_url("192.168.7.105").unwrap(), "http://192.168.7.105:19980");
    assert_eq!(server_url("  192.168.7.105:20000 ").unwrap(), "http://192.168.7.105:20000");
    assert_eq!(server_url("http://example.org:19980/").unwrap(), "http://example.org:19980");
    assert_eq!(server_url("rampage://join/KREJC@192.168.7.105").unwrap(), "http://192.168.7.105:19980");
    assert!(server_url("").is_err());
    assert!(server_url("not a server!").is_err());
    assert!(server_url("rampage://join/KREJC").is_err());
}
