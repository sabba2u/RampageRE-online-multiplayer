<#
net_slots_test.ps1 - the player slots on one PC (docs/NETPLAY.md "Player slots"): a host and a guest as net_test.ps1
starts them, then the guest is killed mid-round and started again, as a player whose game crashed:
  1. the guest plays (-TestPad's moves) for -PlaySeconds
  2. it is killed: the host shows DISCONNECTED with the minute's countdown over Lizzie   -> build\nettest\slots_1_disconnected.png
  3. it is started again with its rejoin token (build\nettest\rejoin.cfg) within the minute: the same slot and
     monster mid-round                                                                    -> slots_2_rejoined.png
  4. it is killed again and left out past the minute: WAITING FOR PLAYER                  -> slots_3_waiting.png
  5. a new guest (its token file deleted) joins: it takes Lizzie as she is                -> slots_4_newcomer.png
The guests' output: build\nettest\guest.out, guest_2.out, guest_3.out; the host's host.out.
  .\tools\net_slots_test.ps1
  .\tools\net_slots_test.ps1 -Lag 80 -Loss 3
  .\tools\net_slots_test.ps1 -OnePlayer                   the host plays a 1 player game: the guest drops in as player 2
#>
param([int]$Port = 27960, [int]$Lag = 0, [int]$Loss = 0, [int]$PlaySeconds = 15, [switch]$OnePlayer, [switch]$Quick,
      [string]$GameArgs = "Chicago 1 --players 2 --monster George --monster2 Lizzie", [string]$GuestArgs = "--monster Ralph")
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "rampage.exe"
if (-not (Test-Path $exe)) { throw "Build first: build.bat dev" }
if ($OnePlayer) { $GameArgs = "Chicago 1 --players 1 --monster George" }
$out = Join-Path $root "build\nettest"
New-Item -ItemType Directory -Force $out | Out-Null
Get-Process rampage -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
$rejoin = Join-Path $out "rejoin.cfg"
Remove-Item $rejoin -ErrorAction SilentlyContinue
$env:RAMPAGE_NET_LAG = "$Lag"
$env:RAMPAGE_NET_LOSS = "$Loss"
$env:RAMPAGE_NET_UPNP = "0"
$env:RAMPAGE_NET_REJOIN = $rejoin
$env:RAMPAGE_NET_TESTDIE = "70,9"           # (the host fells both monsters 70 s in, while the guest is out: the kept one takes nothing)
$h = Start-Process $exe -ArgumentList "$GameArgs --host $Port" -WorkingDirectory $root -PassThru `
    -RedirectStandardOutput "$out\host.out" -RedirectStandardError "$out\host.err"
$env:RAMPAGE_NET_TESTDIE = $null
Start-Sleep 6
if ($OnePlayer) { & (Join-Path $PSScriptRoot "net_shot.ps1") -Name "slots_0_oneplayer" }
$env:RAMPAGE_NET_TESTPAD = "1"
function Guest($n) {
    $o = if ($n -le 1) { "guest" } else { "guest_$n" }
    Start-Process $exe -ArgumentList "$GuestArgs --join 127.0.0.1:$Port" -WorkingDirectory $root -PassThru `
        -RedirectStandardOutput "$out\$o.out" -RedirectStandardError "$out\$o.err"
}
$g = Guest 1
Start-Sleep $PlaySeconds
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "slots_0_playing"
if ($OnePlayer -and $Quick) { $h, $g | Stop-Process -Force; return }
$g | Stop-Process -Force                     # (a crash: no goodbye; the host hears of it by ENet's timeout, 5-10 s)
Start-Sleep 14
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "slots_1_disconnected"
"rejoin file: $(Get-Content $rejoin -ErrorAction SilentlyContinue)"
$g = Guest 2
Start-Sleep 14
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "slots_2_rejoined"
if ($Quick) { $h, $g | Stop-Process -Force; return }
$g | Stop-Process -Force
Start-Sleep 76                               # (ENet's timeout, then the minute)
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "slots_3_waiting"
Remove-Item $rejoin -ErrorAction SilentlyContinue
$g = Guest 3
Start-Sleep 14
& (Join-Path $PSScriptRoot "net_shot.ps1") -Name "slots_4_newcomer"
$h, $g | Where-Object { -not $_.HasExited } | Stop-Process -Force
"--- host"; Get-Content "$out\host.out" | Select-String "slot|guest|MISMATCH|drop-in|shielded|monster 1" | Select-Object -Last 40
"MISMATCH: $((Select-String -Path "$out\host.out", "$out\guest*.out" -Pattern MISMATCH).Count)"
