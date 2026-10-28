<#
net_local_test.ps1 - a hosted 2 player game's player 2 on the host's PC (docs/NETPLAY.md "Player slots"):
  1. hosted, nobody joined: WAITING FOR PLAYER over Lizzie, grey              -> build\nettest\local_1_waiting.png
  2. the host's player 2 keys (numpad 6) pressed: "Local controller took over Player 2", the label and grey gone
                                                                              -> local_2_taken.png
  3. a guest tries to join: "The game is full" (build\nettest\guest.out)      -> local_3_full.png
#>
param([int]$Port = 27960)
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "rampage.exe"
$out = Join-Path $root "build\nettest"
New-Item -ItemType Directory -Force $out | Out-Null
Get-Process rampage -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
$env:RAMPAGE_NET_UPNP = "0"
$env:RAMPAGE_NET_REJOIN = Join-Path $out "rejoin_local.cfg"
Remove-Item $env:RAMPAGE_NET_REJOIN -ErrorAction SilentlyContinue
$h = Start-Process $exe -ArgumentList "Chicago 1 --players 2 --monster George --monster2 Lizzie --host $Port" -WorkingDirectory $root `
    -PassThru -RedirectStandardOutput "$out\host.out" -RedirectStandardError "$out\host.err"
Start-Sleep 12
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "local_1_waiting"
& (Join-Path $PSScriptRoot "net_keys.ps1") -Who host -Keys "Num6,Num6,Num6" -Gap 300
Start-Sleep 1
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "local_2_taken"
$g = Start-Process $exe -ArgumentList "--join 127.0.0.1:$Port" -WorkingDirectory $root -PassThru `
    -RedirectStandardOutput "$out\guest.out" -RedirectStandardError "$out\guest.err"
Start-Sleep 10
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "local_3_full"
$h, $g | Where-Object { -not $_.HasExited } | Stop-Process -Force
"--- host"; Get-Content "$out\host.out" | Select-String "slot|refused|local" | Select-Object -Last 10
"--- guest"; Get-Content "$out\guest.out" | Select-String "said no|full" | Select-Object -Last 5
