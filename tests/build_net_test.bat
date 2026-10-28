@echo off
rem build_net_test.bat - builds and runs tests\net_test.exe, online play's unit tests (the wire format, the snapshots)
setlocal
cd /d "%~dp0.."
set "ZIG=tools\zig\zig-x86_64-windows-0.16.0\zig.exe"
if not exist "%ZIG%" set "ZIG=zig"
%ZIG% cc -std=c99 -O1 -Wall -target x86_64-windows-gnu -o tests\net_test.exe tests\net_test.c src\netwire.c src\netsnap.c || exit /b 1
tests\net_test.exe
