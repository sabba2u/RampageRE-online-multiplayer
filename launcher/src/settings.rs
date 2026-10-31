//! What the launcher remembers, in %LOCALAPPDATA%\RampageRE\launcher.json. Missing or broken: the defaults.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

const SERVERS_KEPT: usize = 8;

#[derive(Debug, Default, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    /// the group servers used ("http://host:port"), newest first
    pub servers: Vec<String>,
    pub first_run_done: bool,
    pub firewall_note_shown: bool,
    pub spacewar_card_shown: bool,
}

pub fn path() -> PathBuf {
    let base = std::env::var_os("LOCALAPPDATA").map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
    base.join("RampageRE").join("launcher.json")
}

impl Settings {
    pub fn load() -> Settings { Settings::load_from(&path()) }
    pub fn save(&self) -> std::io::Result<()> { self.save_to(&path()) }

    pub fn load_from(file: &Path) -> Settings {
        std::fs::read_to_string(file).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default()
    }

    pub fn save_to(&self, file: &Path) -> std::io::Result<()> {
        if let Some(dir) = file.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(file, serde_json::to_string_pretty(self).unwrap())
    }

    /// A change made to what is on disk now (a clicked link may have saved since this window read it), saved;
    /// the result is the new state.
    pub fn update_at(file: &Path, f: impl FnOnce(&mut Settings)) -> Settings {
        let mut s = Settings::load_from(file);
        f(&mut s);
        let _ = s.save_to(file);
        s
    }

    pub fn update(f: impl FnOnce(&mut Settings)) -> Settings { Settings::update_at(&path(), f) }

    /// `server` to the front; the oldest drop off past SERVERS_KEPT.
    pub fn remember(&mut self, server: &str) {
        self.servers.retain(|s| s != server);
        self.servers.insert(0, server.to_string());
        self.servers.truncate(SERVERS_KEPT);
    }
}
