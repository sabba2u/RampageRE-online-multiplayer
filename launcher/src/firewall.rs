//! "Allow through firewall" (Host Local's row): a rule letting other PCs reach this PC's RampageRE (UDP in), as a Steam
//! game's installer adds one. Only the host needs it: guests connect out, which Windows always lets through. Looking is
//! done without admin; the fix asks Windows for admin once (UAC), drops any rule blocking the game (one Windows made
//! when its "Allow access?" box was cancelled) and adds ours, for every network (as Steam's games are).
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Status {
    /// a rule lets it in, none blocks it
    Allowed,
    /// a rule blocks it (Windows' box cancelled once)
    Blocked,
    /// no rule: Windows' own box may ask, or (a managed PC) the game is silently blocked
    Missing,
    /// the look failed
    Unknown,
}

/// The games beside the launcher that other PCs connect to: RampageRE.exe, and RampageRE-dev.exe when there.
pub fn programs(game: &Path) -> Vec<PathBuf> {
    let mut v = vec![game.to_path_buf()];
    if let Some(dir) = game.parent() {
        let dev = dir.join("RampageRE-dev.exe");
        if dev.is_file() && dev != game {
            v.push(dev);
        }
    }
    v
}

/// PowerShell's @('a','b') of the paths (a ' doubled).
fn ps_list(progs: &[PathBuf]) -> String {
    let items: Vec<String> = progs.iter().map(|p| format!("'{}'", p.display().to_string().replace('\'', "''"))).collect();
    format!("@({})", items.join(","))
}

/// The look: one line per enabled inbound rule for these programs, its action ("Allow" / "Block").
pub fn status_script(progs: &[PathBuf]) -> String {
    format!("$p = {}; Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue | Where-Object {{ $p -contains $_.Program }} | \
Get-NetFirewallRule | Where-Object {{ $_.Enabled -eq 'True' -and $_.Direction -eq 'Inbound' }} | ForEach-Object {{ [string]$_.Action }}",
            ps_list(progs))
}

/// The fix (run as admin): the rules blocking these programs and our own old ones gone, ours added.
pub fn fix_script(progs: &[PathBuf]) -> String {
    format!("try {{ $p = {}; Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue | Where-Object {{ $p -contains $_.Program }} | \
Get-NetFirewallRule | Where-Object {{ $_.Action -eq 'Block' -or $_.DisplayName -like 'RampageRE (*' }} | Remove-NetFirewallRule; \
foreach ($x in $p) {{ New-NetFirewallRule -DisplayName ('RampageRE (' + (Split-Path $x -Leaf) + ')') -Direction Inbound \
-Action Allow -Protocol UDP -Program $x -Profile Any -ErrorAction Stop | Out-Null }}; exit 0 }} catch {{ [IO.File]::WriteAllText((Join-Path $env:TEMP 'RampageRE-firewall.log'), [string]$_); exit 2 }}",
            ps_list(progs))
}

/// The look's lines read: a Block wins, then an Allow.
pub fn parse_status(out: &str) -> Status {
    let lines: Vec<&str> = out.lines().map(|l| l.trim()).filter(|l| !l.is_empty()).collect();
    if lines.iter().any(|l| l.eq_ignore_ascii_case("Block")) {
        Status::Blocked
    } else if lines.iter().any(|l| l.eq_ignore_ascii_case("Allow")) {
        Status::Allowed
    } else {
        Status::Missing
    }
}

/// PowerShell's -EncodedCommand: the script as UTF-16LE, in base64 (no quoting to get wrong).
pub fn encode(script: &str) -> String {
    let bytes: Vec<u8> = script.encode_utf16().flat_map(|u| u.to_le_bytes()).collect();
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut s = String::with_capacity(bytes.len() * 4 / 3 + 4);
    for c in bytes.chunks(3) {
        let n = (c[0] as u32) << 16 | (*c.get(1).unwrap_or(&0) as u32) << 8 | *c.get(2).unwrap_or(&0) as u32;
        s.push(T[(n >> 18) as usize & 63] as char);
        s.push(T[(n >> 12) as usize & 63] as char);
        s.push(if c.len() > 1 { T[(n >> 6) as usize & 63] as char } else { '=' });
        s.push(if c.len() > 2 { T[n as usize & 63] as char } else { '=' });
    }
    s
}

fn powershell() -> Command {
    let mut c = Command::new("powershell.exe");
    c.args(["-NoProfile", "-NonInteractive"]);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        c.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    c
}

/// Whether other PCs can reach these programs (a few seconds: Windows lists every rule).
pub fn status(progs: &[PathBuf]) -> Status {
    match powershell().args(["-EncodedCommand", &encode(&status_script(progs))]).output() {
        Ok(o) if o.status.success() => parse_status(&String::from_utf8_lossy(&o.stdout)),
        _ => Status::Unknown,
    }
}

pub const CANCELLED: &str = "Windows didn't get permission (No was clicked), so nothing changed.";
pub const FAILED: &str = "Windows couldn't add the rule (a work PC may not allow it): ask whoever runs this PC.";

/// Where the fix says what went wrong (its own catch), for the message.
pub fn log_path() -> PathBuf { std::env::temp_dir().join("RampageRE-firewall.log") }

fn failed(code: Option<i32>) -> String {
    let why = std::fs::read_to_string(log_path()).unwrap_or_default();
    let why = why.trim();
    if why.is_empty() {
        format!("{FAILED} (code {})", code.map_or("none".to_string(), |c| c.to_string()))
    } else {
        format!("{FAILED} Windows said: {why}")
    }
}

/// The fix, as admin (Windows' Yes/No box), then the look again.
pub fn fix(progs: &[PathBuf]) -> Result<Status, String> {
    let outer = format!("try {{ $r = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -WindowStyle Hidden \
-ArgumentList '-NoProfile','-NonInteractive','-EncodedCommand','{}'; exit $r.ExitCode }} catch {{ exit 1223 }}",
                        encode(&fix_script(progs)));
    let _ = std::fs::remove_file(log_path());
    let code = powershell().args(["-EncodedCommand", &encode(&outer)]).status().map_err(|e| e.to_string())?.code();
    match code {
        Some(0) => match status(progs) {
            Status::Allowed => Ok(Status::Allowed),
            _ => Err(failed(Some(0))),
        },
        Some(1223) => Err(CANCELLED.into()),
        c => Err(failed(c)),
    }
}
