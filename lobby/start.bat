@echo off
rem start.bat - runs the RampageRE lobby on this PC (the host's, or any Windows PC): the games list on TCP 19980.
rem Needs Node.js 20 or newer (nodejs.org). The first start installs the packages and makes config.json from
rem config.example.json. Players point their game at http://<this PC>:19980 (the Insert panel's Lobby field, or
rem --lobby); the console prints this PC's addresses. Ctrl+C stops it. On a VPS: docs\NETPLAY.md "The lobby".
setlocal
cd /d "%~dp0"
where node >nul 2>nul || (echo Node.js is not installed: get it from https://nodejs.org ^(20 or newer^) & pause & exit /b 1)
if not exist node_modules (
    echo Installing the lobby's packages, once...
    call npm install --omit=dev || (pause & exit /b 1)
)
if not exist config.json copy config.example.json config.json >nul
call npm start
if errorlevel 1 pause
