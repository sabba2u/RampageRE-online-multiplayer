use crate::firewall::*;
use std::path::{Path, PathBuf};

#[test]
fn the_look_is_read() {
    assert_eq!(parse_status("Allow\r\nAllow\r\n"), Status::Allowed);
    assert_eq!(parse_status("Allow\nBlock\n"), Status::Blocked);
    assert_eq!(parse_status("  \n"), Status::Missing);
    assert_eq!(parse_status(""), Status::Missing);
}

#[test]
fn the_fix_is_plain_netsh() {
    let p = vec![PathBuf::from(r"C:\Games\Bob's Games\RampageRE.exe"), PathBuf::from(r"C:\Games\Bob's Games\RampageRE-dev.exe")];
    let c = fix_command(&p, Path::new(r"C:\Temp\RampageRE-firewall.log"));
    assert_eq!(c, "\"(netsh advfirewall firewall delete rule name=all dir=in program=\"C:\\Games\\Bob's Games\\RampageRE.exe\" \
& netsh advfirewall firewall delete rule name=all dir=in program=\"C:\\Games\\Bob's Games\\RampageRE-dev.exe\" \
& netsh advfirewall firewall add rule name=\"RampageRE (RampageRE.exe)\" dir=in action=allow protocol=UDP \
program=\"C:\\Games\\Bob's Games\\RampageRE.exe\" profile=any \
&& netsh advfirewall firewall add rule name=\"RampageRE (RampageRE-dev.exe)\" dir=in action=allow protocol=UDP \
program=\"C:\\Games\\Bob's Games\\RampageRE-dev.exe\" profile=any) > \"C:\\Temp\\RampageRE-firewall.log\" 2>&1\"");
    assert!(!c.contains("Encoded") && !c.contains("powershell"));
}

#[test]
fn the_status_script_quotes_paths() {
    let p = vec![PathBuf::from(r"C:\Games\Bob's\RampageRE.exe")];
    assert!(status_script(&p).contains(r"'C:\Games\Bob''s\RampageRE.exe'"));
    assert_eq!(rule_name(&p[0]), "RampageRE (RampageRE.exe)");
}

#[test]
#[ignore] // (asks Windows: run by hand) the look on this PC finishes and says something
fn the_look_runs() {
    let s = status(&[PathBuf::from(r"C:\nowhere\RampageRE.exe")]);
    assert_eq!(s, Status::Missing);
}
