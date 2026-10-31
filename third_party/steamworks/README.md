# Steamworks redistributable

`steam_api64.dll` is Valve's Steamworks API library, from the Steamworks SDK (the version `src/netsteam.c` is built
against: it exports `SteamAPI_SteamFriends_v017`). SHA-256
`670d654aa3255c5061cf0236a1cdbb2f5076cbd5ae44f613b6f4921558e83e2d`.

The game loads it at run time (`LoadLibrary`) for online play over Steam; without it Steam is simply not offered. The
Windows release zip carries it next to `RampageRE.exe`, with `steam_appid.txt` (`480`: Spacewar, Valve's free test app;
docs/NETPLAY.md). A newer SDK's DLL (`SteamFriends_v018` only) does not work with the game as it is.
