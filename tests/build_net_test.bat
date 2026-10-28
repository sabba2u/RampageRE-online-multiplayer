@echo off
rem build_net_test.bat - builds and runs tests\net_test.exe, online play's unit tests (the wire format, the snapshots, the lobby's parsers, the port forward without a router)
setlocal
cd /d "%~dp0.."
set "ZIG=tools\zig\zig-x86_64-windows-0.16.0\zig.exe"
if not exist "%ZIG%" set "ZIG=zig"
rem the port forward (src\netupnp.c) with its library, miniupnpc, from its sources
set "UPNP="
for %%f in (addr_is_reserved connecthostport igd_desc_parse minisoap minissdpc miniupnpc miniwget minixml portlistingparse receivedata upnpcommands upnpdev upnperrors upnpreplyparse) do call set "UPNP=%%UPNP%% src\net\third_party\miniupnpc\src\%%f.c"
%ZIG% cc -std=c99 -O1 -Wall -target x86_64-windows-gnu -o tests\net_test.exe tests\net_test.c src\netwire.c src\netsnap.c src\netlobby.c -DNETLOBBY_TEST src\netupnp.c %UPNP% -DMINIUPNP_STATICLIB -Isrc\net\third_party\miniupnpc\include -lws2_32 -liphlpapi || exit /b 1
tests\net_test.exe
