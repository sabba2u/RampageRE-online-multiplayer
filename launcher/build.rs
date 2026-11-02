//! The launcher exe's icon (assets/icon.ico, made from images/bossrush.png): assets/icon.rc compiled with the Windows
//! SDK's rc.exe and linked in. Without the SDK the launcher builds as before, with no file icon (a warning says so);
//! the window's icon (ui/mod.rs) is in the exe either way.
use std::path::{Path, PathBuf};
use std::process::Command;

fn find_rc() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("RC") {
        return Some(PathBuf::from(p));
    }
    let kits = Path::new(r"C:\Program Files (x86)\Windows Kits\10\bin");
    let mut vers: Vec<PathBuf> = std::fs::read_dir(kits).ok()?.flatten().map(|e| e.path())
        .filter(|p| p.join("x64").join("rc.exe").is_file()).collect();
    vers.sort();
    vers.pop().map(|p| p.join("x64").join("rc.exe"))
}

fn main() {
    println!("cargo:rerun-if-changed=assets/icon.rc");
    println!("cargo:rerun-if-changed=assets/icon.ico");
    if std::env::var("CARGO_CFG_TARGET_ENV").as_deref() != Ok("msvc") {
        return;
    }
    let Some(rc) = find_rc() else {
        println!("cargo:warning=rc.exe (the Windows SDK) not found: the launcher has no file icon");
        return;
    };
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("icon.res");
    let ok = Command::new(&rc).args(["/nologo", "/fo"]).arg(&out).arg("assets/icon.rc").status()
        .map(|s| s.success()).unwrap_or(false);
    if ok {
        println!("cargo:rustc-link-arg-bins={}", out.display());
    } else {
        println!("cargo:warning=rc.exe failed: the launcher has no file icon");
    }
}
