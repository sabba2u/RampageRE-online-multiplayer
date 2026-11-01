use super::steam::*;

#[test]
fn files_missing_only_without_the_dll_the_appid_made() {
    let d = std::env::temp_dir().join(format!("rampage-launcher-steam-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    assert_eq!(check_files(&d), Err(SteamProblem::FilesMissing));
    assert!(!d.join("steam_appid.txt").exists(), "no DLL: nothing written");
    std::fs::write(d.join("steam_api64.dll"), b"x").unwrap();
    // steam_appid.txt missing: the launcher writes it (Spacewar's app id)
    assert_eq!(check_files(&d), Ok(()));
    assert_eq!(std::fs::read_to_string(d.join("steam_appid.txt")).unwrap().trim(), "480");
    // one that is there is left as it is
    std::fs::write(d.join("steam_appid.txt"), b"480\n").unwrap();
    assert_eq!(check_files(&d), Ok(()));
    assert_eq!(std::fs::read_to_string(d.join("steam_appid.txt")).unwrap(), "480\n");
}

#[test]
fn texts_match_spec() {
    assert_eq!(problem_text(&SteamProblem::NotRunning), "Steam isn't running.");
    assert_eq!(problem_text(&SteamProblem::FilesMissing), "This copy is missing Steam's files: download the release again.");
    assert_eq!(problem_text(&SteamProblem::NoSpacewar), "You need Spacewar (free) in your Steam library.");
    assert_eq!(GET_SPACEWAR_NOTE, "Click Install, then you can cancel the download: it just needs to be in your library.");
}
