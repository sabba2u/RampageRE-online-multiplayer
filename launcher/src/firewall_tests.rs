use crate::firewall::*;
use std::path::PathBuf;

#[test]
fn the_look_is_read() {
    assert_eq!(parse_status("Allow\r\nAllow\r\n"), Status::Allowed);
    assert_eq!(parse_status("Allow\nBlock\n"), Status::Blocked);
    assert_eq!(parse_status("  \n"), Status::Missing);
    assert_eq!(parse_status(""), Status::Missing);
}

#[test]
fn base64_as_powershell_wants_it() {
    // "ab" in UTF-16LE is 61 00 62 00
    assert_eq!(encode("ab"), "YQBiAA==");
    assert_eq!(encode("abc"), "YQBiAGMA");
    assert_eq!(encode(""), "");
}

#[test]
fn the_paths_are_quoted() {
    let p = vec![PathBuf::from(r"C:\Games\Bob's\RampageRE.exe")];
    let s = fix_script(&p);
    assert!(s.contains(r"'C:\Games\Bob''s\RampageRE.exe'"), "{s}");
    assert!(s.contains("-Protocol UDP") && s.contains("-Profile Any") && s.contains("Remove-NetFirewallRule"));
    assert!(status_script(&p).contains(r"'C:\Games\Bob''s\RampageRE.exe'"));
}

#[test]
#[ignore] // (asks Windows: run by hand) the look on this PC finishes and says something
fn the_look_runs() {
    let s = status(&[PathBuf::from(r"C:\nowhere\RampageRE.exe")]);
    assert_eq!(s, Status::Missing);
}

#[test]
#[ignore] // (by hand) the scripts written out, for PowerShell's parser: RAMPAGE_FW_OUT=dir
fn write_scripts() {
    let dir = std::env::var("RAMPAGE_FW_OUT").unwrap();
    let p = vec![PathBuf::from(r"C:\Games\Bob's\RampageRE.exe"), PathBuf::from(r"C:\Games\Bob's\RampageRE-dev.exe")];
    std::fs::write(format!("{dir}/fix.ps1"), fix_script(&p)).unwrap();
    std::fs::write(format!("{dir}/status.ps1"), status_script(&p)).unwrap();
}
