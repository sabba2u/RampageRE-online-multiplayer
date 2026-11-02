@echo off
rem build_rumble_test.bat - builds and runs tests\rumble_test.exe, Boss Rumble's rules (src\rumble.c)
setlocal
cd /d "%~dp0.."
set "ZIG=tools\zig\zig-x86_64-windows-0.16.0\zig.exe"
if not exist "%ZIG%" set "ZIG=zig"
%ZIG% cc -std=c99 -O1 -Wall -target x86_64-windows-gnu -o tests\rumble_test.exe tests\rumble_test.c src\rumble.c || exit /b 1
tests\rumble_test.exe
