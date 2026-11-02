<#
net_local_test.ps1 - a hosted 2 player game's player 2 on the host's PC (docs/NETPLAY.md "Player slots"):
  1. hosted, nobody joined: WAITING FOR PLAYER over Lizzie, grey              -> build\nettest\local_1_waiting.png
  2. the host's player 2 keys (numpad 6) pressed: "Local controller took over Player 2", the label and grey gone
                                                                              -> local_2_taken.png
  3. a guest joins: rooms have four seats, so it plays player 3 (host.out "slot 2: online") -> local_3_full.png
  .\tools\net_local_test.ps1 -Seat2Online   a hosted 3 player game, a guest in player 2's seat, then the numpad
                                             pressed on the host's PC: it takes the next free seat (player 3), not
                                             player 2's -> PASS / FAIL, local_4_seat3.png
#>
param([int]$Port = 27960, [switch]$Seat2Online)
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "RampageRE-dev.exe"
$out = Join-Path $root "build\nettest"
New-Item -ItemType Directory -Force $out | Out-Null
Get-Process RampageRE-dev -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
$env:RAMPAGE_NET_UPNP = "0"
$env:RAMPAGE_NET_REJOIN = Join-Path $out "rejoin_local.cfg"
Remove-Item $env:RAMPAGE_NET_REJOIN -ErrorAction SilentlyContinue
if ($Seat2Online) {
    $h = Start-Process $exe -ArgumentList "Chicago 1 --four-players --players 3 --monster George --monster2 Lizzie --monster3 Ralph --host $Port" `
        -WorkingDirectory $root -PassThru -RedirectStandardOutput "$out\host.out" -RedirectStandardError "$out\host.err"
    Start-Sleep 8
    $env:RAMPAGE_NET_REJOIN = Join-Path $out "rejoin_local_g.cfg"
    Remove-Item $env:RAMPAGE_NET_REJOIN -ErrorAction SilentlyContinue
    $g = Start-Process $exe -ArgumentList "--four-players --join 127.0.0.1:$Port" -WorkingDirectory $root -PassThru `
        -RedirectStandardOutput "$out\guest.out" -RedirectStandardError "$out\guest.err"
    Start-Sleep 10
    & (Join-Path $PSScriptRoot "net_keys.ps1") -Who host -Keys "Num6,Num6,Num6" -Gap 300
    Start-Sleep 2
    & (Join-Path $PSScriptRoot "net_shot.ps1") -Name "local_4_seat3"
    $h, $g | Where-Object { -not $_.HasExited } | Stop-Process -Force
    $log = Get-Content "$out\host.out"
    $online1 = [bool]($log | Select-String -SimpleMatch "slot 1: online")
    $took2 = [bool]($log | Select-String -SimpleMatch "slot 2: a player on this PC took it")
    $took1 = [bool]($log | Select-String -SimpleMatch "slot 1: a player on this PC took it")
    if ($online1 -and $took2 -and -not $took1) { "PASS the numpad took player 3's seat; the guest kept player 2" }
    else { "FAIL guest in seat 1: $online1, the numpad took seat 2: $took2, took the guest's seat 1: $took1"; exit 1 }
    return
}
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
