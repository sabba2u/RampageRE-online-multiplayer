<#
net4_test.ps1 - a hosted game with up to three guests on this PC, every copy compared (docs/NETPLAY.md, "Tests").

  .\tools\net4_test.ps1                          host + 3 guests, 4 players, 40 s, two 2 x 2 screenshots
  .\tools\net4_test.ps1 -Guests 1                host + 1 guest in a 4 player game (players 3 and 4 the host's)
  .\tools\net4_test.ps1 -Rejoin 3                the guest in seat 3 is killed at 15 s and started again (its token)
  .\tools\net4_test.ps1 -Lag 80 -Loss 3          the internet on one PC

Each copy logs every monster once a second (RAMPAGE_NET_TAILS: move state, tail springs, points, lives) to
build\nettest\host.out and guest<k>.out. Checks, one PASS / FAIL line each (exit 1 on any FAIL):
  seats     the host gave seats 1 .. Guests to the guests ("slot k: online")
  digests   the host found no difference ("MISMATCH")
  monsters  every copy has the host's number of monsters
  tails     on every guest, a monster with a tail walking has its springs on (weight over 0)
  scores    each guest's last points per monster are the host's (within 600: the copies log a moment apart) and
            the lives equal
  rejoin    (-Rejoin k) the guest started again took seat k back
#>
param([int]$Seconds = 40, [int]$Guests = 3, [int]$Shots = 2, [int]$Port = 27960, [int]$Lag = 0, [int]$Loss = 0,
      [int]$Rejoin = 0, [switch]$HostMoves, [string]$HostDo = "", [string]$GuestArgs = "--four-players",
      [string]$GameArgs = "Chicago 1 --four-players --players 4 --monster Lizzie --monster2 Ralph --monster3 Icky --monster4 Kingston")
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "RampageRE-dev.exe"
if (-not (Test-Path $exe)) { throw "Build first: build.bat dev" }
$out = Join-Path $root "build\nettest"
New-Item -ItemType Directory -Force $out | Out-Null
Get-Process RampageRE-dev -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force
Remove-Item "$out\host.out", "$out\guest*.out", "$out\rejoin*.cfg" -ErrorAction SilentlyContinue

$env:RAMPAGE_NET_LAG = "$Lag"; $env:RAMPAGE_NET_LOSS = "$Loss"
$env:RAMPAGE_NET_UPNP = "0"; $env:RAMPAGE_STUN = "0"; $env:RAMPAGE_NET_TAILS = "1"
$env:RAMPAGE_NET_HOSTDO = if ($HostDo) { $HostDo } else { $null }
$env:RAMPAGE_NET_HOSTMOVES = if ($HostMoves) { "1" } else { $null }
$env:RAMPAGE_NET_TESTPAD = $null
$h = Start-Process $exe -ArgumentList "$GameArgs --host $Port" -WorkingDirectory $root -PassThru `
    -RedirectStandardOutput "$out\host.out" -RedirectStandardError "$out\host.err"
Start-Sleep 6
$env:RAMPAGE_NET_HOSTDO = $null; $env:RAMPAGE_NET_HOSTMOVES = $null

function Start-Guest([int]$k, [string]$suffix = "") {
    $env:RAMPAGE_NET_REJOIN = "$out\rejoin$k.cfg"     # (each guest its own token)
    $env:RAMPAGE_NET_TESTPAD = "$k"                   # (each guest's pattern 2.7 s from the others')
    Start-Process $exe -ArgumentList ("$GuestArgs --join 127.0.0.1:$Port".Trim()) -WorkingDirectory $root -PassThru `
        -RedirectStandardOutput "$out\guest$k$suffix.out" -RedirectStandardError "$out\guest$k$suffix.err"
}
$g = @{}
for ($k = 1; $k -le $Guests; $k++) { $g[$k] = Start-Guest $k; Start-Sleep 1 }

$t0 = Get-Date
$every = if ($Shots -gt 0) { [math]::Max(1, [int](($Seconds - 10) / $Shots)) } else { 0 }
$shot = 0; $rejoined = $false
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    $el = ((Get-Date) - $t0).TotalSeconds
    if ($Rejoin -gt 0 -and -not $rejoined -and $el -ge 15) {
        $g[$Rejoin] | Stop-Process -Force
        Start-Sleep 3
        $g[$Rejoin] = Start-Guest $Rejoin "b"
        $rejoined = $true
    }
    if ($Shots -gt 0 -and $shot -lt $Shots -and $el -ge 10 + $shot * $every) {
        $shot++
        & (Join-Path $PSScriptRoot "net_shot.ps1") -Name "net4_$shot" | Out-Null
    }
    Start-Sleep -Milliseconds 500
}
$all = @($h) + @($g.Values)
$all | Where-Object { -not $_.HasExited } | Stop-Process -Force
Start-Sleep 1

# ---- the checks ----
$fail = 0
function Report([string]$name, [bool]$ok, [string]$why) {
    if ($ok) { "PASS $name" } else { "FAIL $name : $why"; $script:fail++ }
}
function Read-Tails([string]$file) {
    $rx = 'net: tail (\d+): links (\d+) weight ([\d.]+) target ([\d.]+) state (\S+) points (-?\d+) lives (-?\d+)'
    Get-Content $file -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_ -match $rx) {
            [pscustomobject]@{ k = [int]$Matches[1]; links = [int]$Matches[2]; weight = [double]$Matches[3]
                state = $Matches[5]; points = [int]$Matches[6]; lives = [int]$Matches[7] }
        }
    }
}
$hostLog = Get-Content "$out\host.out" -ErrorAction SilentlyContinue
$seatsOk = $true; $missing = @()
for ($k = 1; $k -le $Guests; $k++) {
    if (-not ($hostLog | Select-String -SimpleMatch "slot ${k}: online")) { $seatsOk = $false; $missing += $k }
}
Report "seats" $seatsOk "no guest in seat(s) $($missing -join ', ')"
$mm = @($hostLog | Select-String -SimpleMatch "MISMATCH")
Report "digests" ($mm.Count -eq 0) "$($mm.Count) MISMATCH lines, first: $($mm | Select-Object -First 1)"

$ht = @(Read-Tails "$out\host.out")
$hn = if ($ht) { ($ht | Measure-Object k -Maximum).Maximum + 1 } else { 0 }
$hLast = @{}; foreach ($r in $ht) { $hLast[$r.k] = $r }
for ($k = 1; $k -le $Guests; $k++) {
    $file = if ($Rejoin -eq $k) { "$out\guest${k}b.out" } else { "$out\guest$k.out" }
    $gt = @(Read-Tails $file)
    $gn = if ($gt) { ($gt | Measure-Object k -Maximum).Maximum + 1 } else { 0 }
    Report "monsters (guest $k)" ($gn -eq $hn -and $hn -gt 0) "host $hn, guest $gn"
    $walks = @($gt | Where-Object { $_.links -gt 0 -and $_.state -like "*Walk*" })
    $stiff = @($walks | Where-Object { $_.weight -le 0 })
    Report "tails (guest $k)" ($walks.Count -gt 0 -and $stiff.Count -le [math]::Floor($walks.Count / 10)) `
        "$($stiff.Count) of $($walks.Count) walking samples with the springs off (monsters $((@($stiff | ForEach-Object k) | Sort-Object -Unique) -join ', '))"
    $gLast = @{}; foreach ($r in $gt) { $gLast[$r.k] = $r }
    $bad = @()
    foreach ($m in $hLast.Keys) {
        $a = $hLast[$m]; $b = $gLast[$m]
        if (-not $b) { $bad += "monster $m missing"; continue }
        if ([math]::Abs($a.points - $b.points) -gt 600) { $bad += "monster $m points host $($a.points) guest $($b.points)" }
        if ($a.lives -ne $b.lives) { $bad += "monster $m lives host $($a.lives) guest $($b.lives)" }
    }
    Report "scores (guest $k)" ($bad.Count -eq 0) ($bad -join '; ')
}
if ($Rejoin -gt 0) {
    $back = @($hostLog | Select-String -SimpleMatch "slot ${Rejoin}: online").Count
    Report "rejoin (seat $Rejoin)" ($back -ge 2) "seat $Rejoin went online $back time(s)"
}
if ($Shots -gt 0) { "screenshots: $out\net4_1.png .. net4_$Shots.png" }
if ($fail) { exit 1 }
