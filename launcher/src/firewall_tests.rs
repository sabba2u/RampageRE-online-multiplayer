use crate::firewall::*;
use std::path::{Path, PathBuf};

fn progs() -> Vec<Prog> {
    vec![Prog { path: PathBuf::from(r"C:\G\RampageRE.exe"), tcp: false },
         Prog { path: PathBuf::from(r"C:\G\RampageRE-Launcher.exe"), tcp: true }]
}

#[test]
fn the_look_is_read() {
    let p = progs();
    assert_eq!(parse_status("C:\\G\\RampageRE.exe|Allow\r\nc:\\g\\rampagere-launcher.exe|Allow\r\n", &p), Status::Allowed);
    // (the game alone allowed: the launcher's lobby still shut)
    assert_eq!(parse_status("C:\\G\\RampageRE.exe|Allow\n", &p), Status::Missing);
    assert_eq!(parse_status("C:\\G\\RampageRE.exe|Allow\nC:\\G\\RampageRE-Launcher.exe|Block\n", &p), Status::Blocked);
    assert_eq!(parse_status("", &p), Status::Missing);
}

#[test]
fn the_fix_is_plain_netsh() {
    let p = vec![Prog { path: PathBuf::from(r"C:\Games\Bob's Games\RampageRE.exe"), tcp: false },
                 Prog { path: PathBuf::from(r"C:\Games\Bob's Games\RampageRE-Launcher.exe"), tcp: true }];
    let c = fix_command(&p, Path::new(r"C:\Temp\RampageRE-firewall.log"));
    assert_eq!(c, "\"(netsh advfirewall firewall delete rule name=all dir=in program=\"C:\\Games\\Bob's Games\\RampageRE.exe\" \
& netsh advfirewall firewall delete rule name=all dir=in program=\"C:\\Games\\Bob's Games\\RampageRE-Launcher.exe\" \
& netsh advfirewall firewall add rule name=\"RampageRE (RampageRE.exe)\" dir=in action=allow protocol=UDP \
program=\"C:\\Games\\Bob's Games\\RampageRE.exe\" profile=any \
&& netsh advfirewall firewall add rule name=\"RampageRE (RampageRE-Launcher.exe)\" dir=in action=allow protocol=TCP \
program=\"C:\\Games\\Bob's Games\\RampageRE-Launcher.exe\" profile=any) > \"C:\\Temp\\RampageRE-firewall.log\" 2>&1\"");
    assert!(!c.contains("Encoded") && !c.contains("powershell"));
}

#[test]
fn the_programs() {
    let p = programs(Path::new(r"C:\nowhere\RampageRE.exe"), Some(PathBuf::from(r"C:\nowhere\RampageRE-Launcher.exe")));
    assert_eq!(p, vec![Prog { path: PathBuf::from(r"C:\nowhere\RampageRE.exe"), tcp: false },
                       Prog { path: PathBuf::from(r"C:\nowhere\RampageRE-Launcher.exe"), tcp: true }]);
    let p = vec![Prog { path: PathBuf::from(r"C:\Games\Bob's\RampageRE.exe"), tcp: false }];
    assert!(status_script(&p).contains(r"'C:\Games\Bob''s\RampageRE.exe'"));
    assert_eq!(rule_name(&p[0].path), "RampageRE (RampageRE.exe)");
}

#[test]
#[ignore] // (asks Windows: run by hand) the look on this PC: the repo's games are allowed, the launcher not yet
fn the_look_runs() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
    let games = programs(&root.join("RampageRE.exe"), None);
    println!("games: {:?}", status(&games));
    let all = programs(&root.join("RampageRE.exe"), Some(root.join("RampageRE-Launcher.exe")));
    println!("with the launcher: {:?}", status(&all));
}
