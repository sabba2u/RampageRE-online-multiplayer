use super::steam::*;

#[test]
fn files_missing_when_dll_or_appid_absent() {
    let d = std::env::temp_dir().join(format!("rampage-launcher-steam-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    assert_eq!(check_files(&d), Err(SteamProblem::FilesMissing));
    std::fs::write(d.join("steam_api64.dll"), b"x").unwrap();
    assert_eq!(check_files(&d), Err(SteamProblem::FilesMissing));
    std::fs::write(d.join("steam_appid.txt"), b"480").unwrap();
    assert_eq!(check_files(&d), Ok(()));
}

#[test]
fn texts_match_spec() {
    assert_eq!(problem_text(&SteamProblem::NotRunning), "Steam isn't running.");
    assert_eq!(problem_text(&SteamProblem::FilesMissing), "This copy is missing Steam's files: download the release again.");
    assert_eq!(problem_text(&SteamProblem::NoSpacewar), "You need Spacewar (free) in your Steam library.");
    assert_eq!(GET_SPACEWAR_NOTE, "Click Install, then you can cancel the download: it just needs to be in your library.");
}
