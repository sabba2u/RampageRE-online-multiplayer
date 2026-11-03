//! "Allow through firewall" (Host Local's row): rules letting other PCs reach this PC's RampageRE, as a Steam game's
//! installer adds one: the games by UDP, and the launcher itself by TCP (its lobby server, port 19980: internet games
//! and a friend's Connect). Only the host needs them: guests connect out, which Windows always lets through.
//! The look runs PowerShell without admin, hidden (it only reads the rules). The fix is Windows' own netsh, run through
//! the usual admin Yes/No box as plain readable commands (its "Show more details" shows them): each program's inbound
//! rules dropped (one Windows made blocking it when its "Allow access?" box was cancelled), ours added, for every
//! network (as Steam's games are). Its output goes to %TEMP%\RampageRE-firewall.log, for the message when it fails.
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Status {
    /// a rule lets each in, none blocks one
    Allowed,
    /// a rule blocks one (Windows' box cancelled once)
    Blocked,
    /// one has no rule: Windows' own box may ask, or (a managed PC) it is silently blocked
    Missing,
    /// the look failed
    Unknown,
}

/// A program other PCs connect to, and how: the games by UDP, the launcher's lobby by TCP.
#[derive(Debug, Clone, PartialEq)]
pub struct Prog {
    pub path: PathBuf,
    pub tcp: bool,
}

/// The games beside the launcher (RampageRE.exe, and RampageRE-dev.exe when there), and the launcher itself.
pub fn programs(game: &Path, launcher: Option<PathBuf>) -> Vec<Prog> {
    let mut v = vec![Prog { path: game.to_path_buf(), tcp: false }];
    if let Some(dir) = game.parent() {
        let dev = dir.join("RampageRE-dev.exe");
        if dev.is_file() && dev != game {
            v.push(Prog { path: dev, tcp: false });
        }
    }
    if let Some(l) = launcher {
        v.push(Prog { path: l, tcp: true });
    }
    v
}

/// PowerShell's @('a','b') of the paths (a ' doubled).
fn ps_list(progs: &[Prog]) -> String {
    let items: Vec<String> = progs.iter().map(|p| format!("'{}'", p.path.display().to_string().replace('\'', "''"))).collect();
    format!("@({})", items.join(","))
}

/// The look: one line per enabled inbound rule for these programs, "<program>|<action>" (Allow / Block).
pub fn status_script(progs: &[Prog]) -> String {
    format!("$p = {}; Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue | Where-Object {{ $p -contains $_.Program }} | \
ForEach-Object {{ $f = $_; $f | Get-NetFirewallRule | Where-Object {{ $_.Enabled -eq 'True' -and $_.Direction -eq 'Inbound' }} | \
ForEach-Object {{ $f.Program + '|' + [string]$_.Action }} }}",
            ps_list(progs))
}

/// The look's lines read: a rule blocking any of them: Blocked; one allowing each of them: Allowed; else Missing.
pub fn parse_status(out: &str, progs: &[Prog]) -> Status {
    let lines: Vec<(String, String)> = out.lines().filter_map(|l| l.trim().rsplit_once('|'))
        .map(|(p, a)| (p.trim().to_lowercase(), a.trim().to_lowercase())).collect();
    if lines.iter().any(|(_, a)| a == "block") {
        return Status::Blocked;
    }
    let allowed = |p: &Prog| {
        let want = p.path.display().to_string().to_lowercase();
        lines.iter().any(|(q, a)| *q == want && a == "allow")
    };
    if progs.iter().all(allowed) { Status::Allowed } else { Status::Missing }
}

/// Our rule's name for a program: "RampageRE (RampageRE.exe)".
pub fn rule_name(prog: &Path) -> String {
    format!("RampageRE ({})", prog.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default())
}

/// The fix, as cmd.exe's /s /c argument: for each program, its inbound rules deleted (none there is no failure),
/// then ours added; everything said into `log`. cmd's exit code is the last add's.
pub fn fix_command(progs: &[Prog], log: &Path) -> String {
    let dels: Vec<String> = progs.iter().map(|p| format!(
        "netsh advfirewall firewall delete rule name=all dir=in program=\"{}\"", p.path.display())).collect();
    let adds: Vec<String> = progs.iter().map(|p| format!(
        "netsh advfirewall firewall add rule name=\"{}\" dir=in action=allow protocol={} program=\"{}\" profile=any",
        rule_name(&p.path), if p.tcp { "TCP" } else { "UDP" }, p.path.display())).collect();
    format!("\"({} & {}) > \"{}\" 2>&1\"", dels.join(" & "), adds.join(" && "), log.display())
}

fn hidden(mut c: Command) -> Command {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        c.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    c
}

/// Whether other PCs can reach these programs (a few seconds: Windows lists every rule).
pub fn status(progs: &[Prog]) -> Status {
    let mut c = hidden(Command::new("powershell.exe"));
    c.args(["-NoProfile", "-NonInteractive", "-Command", &status_script(progs)]);
    match c.output() {
        Ok(o) if o.status.success() => parse_status(&String::from_utf8_lossy(&o.stdout), progs),
        _ => Status::Unknown,
    }
}

pub const CANCELLED: &str = "Windows didn't get permission (No was clicked), so nothing changed.";
pub const FAILED: &str = "Windows couldn't add the rule (a work PC may not allow it): ask whoever runs this PC.";

/// Where the fix's netsh output goes, for the message when it fails.
pub fn log_path() -> PathBuf { std::env::temp_dir().join("RampageRE-firewall.log") }

fn failed(code: Option<u32>) -> String {
    let said = std::fs::read_to_string(log_path()).unwrap_or_default();
    let said: Vec<&str> = said.lines().map(|l| l.trim()).filter(|l| !l.is_empty() && *l != "Ok." && !l.starts_with("Deleted ")
                                                                   && !l.starts_with("No rules match")).collect();
    if said.is_empty() {
        format!("{FAILED} (code {})", code.map_or("none".to_string(), |c| c.to_string()))
    } else {
        format!("{FAILED} Windows said: {}", said.join(" "))
    }
}

/// cmd.exe run as admin (Windows' Yes/No box) with `args`, waited for: its exit code, or Err(1223) when No was clicked.
#[cfg(windows)]
fn run_as_admin(args: &str) -> Result<u32, u32> {
    use windows_sys::Win32::Foundation::{CloseHandle, GetLastError};
    use windows_sys::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject, INFINITE};
    use windows_sys::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
    use windows_sys::Win32::UI::WindowsAndMessaging::SW_HIDE;
    let wide = |s: &str| s.encode_utf16().chain(std::iter::once(0)).collect::<Vec<u16>>();
    let (verb, file, params) = (wide("runas"), wide("cmd.exe"), wide(&format!("/s /c {args}")));
    unsafe {
        let mut info: SHELLEXECUTEINFOW = std::mem::zeroed();
        info.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb.as_ptr();
        info.lpFile = file.as_ptr();
        info.lpParameters = params.as_ptr();
        info.nShow = SW_HIDE as i32;
        if ShellExecuteExW(&mut info) == 0 {
            return Err(GetLastError());
        }
        if info.hProcess.is_null() {
            return Err(0);
        }
        WaitForSingleObject(info.hProcess, INFINITE);
        let mut code = 0u32;
        GetExitCodeProcess(info.hProcess, &mut code);
        CloseHandle(info.hProcess);
        Ok(code)
    }
}

#[cfg(not(windows))]
fn run_as_admin(_args: &str) -> Result<u32, u32> { Err(0) }

/// The fix, as admin, then the look again: Allowed, or why not.
pub fn fix(progs: &[Prog]) -> Result<Status, String> {
    let _ = std::fs::remove_file(log_path());
    match run_as_admin(&fix_command(progs, &log_path())) {
        Ok(0) => match status(progs) {
            Status::Allowed => Ok(Status::Allowed),
            _ => Err(failed(Some(0))),
        },
        Ok(c) => Err(failed(Some(c))),
        Err(1223) => Err(CANCELLED.into()),
        Err(e) => Err(failed(Some(e))),
    }
}
