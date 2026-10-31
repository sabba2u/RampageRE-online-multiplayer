//! Before either Steam button starts the game: Steam running, Steam's files next to the game, and the account owning
//! Spacewar (app 480, Valve's free test app the game borrows; src/netsteam.c). Ownership is asked of Steam itself, as
//! the game's own Steam start does, in a child process (--steam-check) so Steam's "playing Spacewar" never sticks to
//! the launcher.
use std::path::Path;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, PartialEq)]
pub enum SteamProblem {
    NotRunning,
    FilesMissing,
    NoSpacewar,
}

pub const GET_SPACEWAR_NOTE: &str = "Click Install, then you can cancel the download: it just needs to be in your library.";

pub fn problem_text(p: &SteamProblem) -> &'static str {
    match p {
        SteamProblem::NotRunning => "Steam isn't running.",
        SteamProblem::FilesMissing => "This copy is missing Steam's files: download the release again.",
        SteamProblem::NoSpacewar => "You need Spacewar (free) in your Steam library.",
    }
}

pub fn check_files(game_dir: &Path) -> Result<(), SteamProblem> {
    if game_dir.join("steam_api64.dll").is_file() && game_dir.join("steam_appid.txt").is_file() {
        Ok(())
    } else {
        Err(SteamProblem::FilesMissing)
    }
}

/// Running, files, Spacewar: the first problem found.
pub fn check(game_dir: &Path) -> Result<(), SteamProblem> {
    if !steam_running() {
        return Err(SteamProblem::NotRunning);
    }
    check_files(game_dir)?;
    let exe = std::env::current_exe().map_err(|_| SteamProblem::NoSpacewar)?;
    let mut child = std::process::Command::new(exe).arg("--steam-check").arg(game_dir).spawn()
        .map_err(|_| SteamProblem::NoSpacewar)?;
    let end = Instant::now() + Duration::from_secs(10);
    while Instant::now() < end {
        if let Ok(Some(st)) = child.try_wait() {
            return if st.success() { Ok(()) } else { Err(SteamProblem::NoSpacewar) };
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    let _ = child.kill(); // Steam didn't answer: the game will say what's wrong
    Ok(())
}

fn wide(s: &str) -> Vec<u16> { s.encode_utf16().chain(std::iter::once(0)).collect() }

#[cfg(windows)]
pub fn steam_running() -> bool {
    use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::System::Diagnostics::ToolHelp::*;
    unsafe {
        let snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if snap == INVALID_HANDLE_VALUE {
            return false;
        }
        let mut e: PROCESSENTRY32W = std::mem::zeroed();
        e.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;
        let mut found = false;
        let mut ok = Process32FirstW(snap, &mut e) != 0;
        while ok && !found {
            let n = e.szExeFile.iter().position(|c| *c == 0).unwrap_or(e.szExeFile.len());
            found = String::from_utf16_lossy(&e.szExeFile[..n]).eq_ignore_ascii_case("steam.exe");
            ok = Process32NextW(snap, &mut e) != 0;
        }
        CloseHandle(snap);
        found
    }
}

/// --steam-check <game folder>: 0 when Steam starts this process as Spacewar, 2 when it won't.
#[cfg(windows)]
pub fn steam_check_child(game_dir: &Path) -> i32 {
    use windows_sys::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};
    std::env::set_var("SteamAppId", "480");
    std::env::set_var("SteamGameId", "480");
    unsafe {
        let dll = LoadLibraryW(wide(&game_dir.join("steam_api64.dll").display().to_string()).as_ptr());
        if dll.is_null() {
            return 2;
        }
        let ok = if let Some(f) = GetProcAddress(dll, b"SteamAPI_InitFlat\0".as_ptr()) {
            let init: unsafe extern "C" fn(*mut u8) -> i32 = std::mem::transmute(f);
            let mut msg = [0u8; 1024];
            init(msg.as_mut_ptr()) == 0
        } else if let Some(f) = GetProcAddress(dll, b"SteamAPI_Init\0".as_ptr()) {
            let init: unsafe extern "C" fn() -> bool = std::mem::transmute(f);
            init()
        } else {
            false
        };
        if ok {
            if let Some(f) = GetProcAddress(dll, b"SteamAPI_Shutdown\0".as_ptr()) {
                let shutdown: unsafe extern "C" fn() = std::mem::transmute(f);
                shutdown();
            }
        }
        if ok { 0 } else { 2 }
    }
}

/// steam://... through the shell (Steam opens it).
#[cfg(windows)]
fn shell_open(url: &str) {
    use windows_sys::Win32::UI::Shell::ShellExecuteW;
    use windows_sys::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;
    unsafe {
        ShellExecuteW(std::ptr::null_mut(), wide("open").as_ptr(), wide(url).as_ptr(), std::ptr::null(), std::ptr::null(),
                      SW_SHOWNORMAL);
    }
}

pub fn open_steam() { shell_open("steam://open/main") }
pub fn get_spacewar() { shell_open("steam://install/480") }
