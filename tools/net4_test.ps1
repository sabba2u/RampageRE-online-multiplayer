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
  joinlate  (-JoinLate: the host plays alone, the guests come 10 s apart) each joined mid-block: no block started
            again on the host, and every guest loaded the host's game once
#>
param([int]$Seconds = 40, [int]$Guests = 3, [int]$Shots = 2, [int]$Port = 27960, [int]$Lag = 0, [int]$Loss = 0,
      [int]$Rejoin = 0, [switch]$HostMoves, [switch]$JoinLate, [int]$HostWait = 6, [string]$HostDo = "", [string]$GuestArgs = "--four-players",
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
if ($JoinLate) { $GameArgs = $GameArgs -replace '--players \d', '--players 1' }
$h = Start-Process $exe -ArgumentList "$GameArgs --host $Port" -WorkingDirectory $root -PassThru `
    -RedirectStandardOutput "$out\host.out" -RedirectStandardError "$out\host.err"
Start-Sleep $HostWait                                # (-HostWait 2: the first guest comes during the intro)
$env:RAMPAGE_NET_HOSTDO = $null; $env:RAMPAGE_NET_HOSTMOVES = $null

function Start-Guest([int]$k, [string]$suffix = "") {
    $env:RAMPAGE_NET_REJOIN = "$out\rejoin$k.cfg"     # (each guest its own token)
    $env:RAMPAGE_NET_TESTPAD = "$k"                   # (each guest's pattern 2.7 s from the others')
    Start-Process $exe -ArgumentList ("$GuestArgs --join 127.0.0.1:$Port".Trim()) -WorkingDirectory $root -PassThru `
        -RedirectStandardOutput "$out\guest$k$suffix.out" -RedirectStandardError "$out\guest$k$suffix.err"
}
$g = @{}
for ($k = 1; $k -le $Guests; $k++) { $g[$k] = Start-Guest $k; Start-Sleep $(if ($JoinLate) { 10 } else { 1 }) }

$t0 = Get-Date
$every = if ($Shots -gt 0) { [math]::Max(1, [int](($Seconds - 10) / $Shots)) } else { 0 }
$shot = 0; $rejoined = $false
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    $el = ((Get-Date) - $t0).TotalSeconds
    if ($Rejoin -gt 0 -and -not $rejoined -and $el -ge 15) {
        $g[$Rejoin] | Stop-Process -Force   # (the host notices in 5 to 10 s: ENet's timeout; its seat is kept from then)
        Start-Sleep 3                                # (before the host notices: the old connection is let go when it comes back)
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
    $rx = 'net: tail (\d+): links (\d+) weight ([\d.]+) target ([\d.]+) state (\S+) points (-?\d+) lives (-?\d+) wall (\d+) block (\d+)'
    Get-Content $file -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_ -match $rx) {
            [pscustomobject]@{ k = [int]$Matches[1]; links = [int]$Matches[2]; weight = [double]$Matches[3]
                state = $Matches[5]; points = [int]$Matches[6]; lives = [int]$Matches[7]; wall = [long]$Matches[8]
                block = [int]$Matches[9] }
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
$hAt = @{}; foreach ($r in $ht) { $hAt["$($r.wall)/$($r.k)"] = $r }   # (the host's sample of each second)
for ($k = 1; $k -le $Guests; $k++) {
    $file = if ($Rejoin -eq $k) { "$out\guest${k}b.out" } else { "$out\guest$k.out" }
    $gt = @(Read-Tails $file)
    $gn = if ($gt) { ($gt | Measure-Object k -Maximum).Maximum + 1 } else { 0 }
    Report "monsters (guest $k)" ($gn -eq $hn -and $hn -gt 0) "host $hn, guest $gn"
    $walks = @($gt | Where-Object { $_.links -gt 0 -and $_.state -like "*Walk*" })
    $stiff = @($walks | Where-Object { $_.weight -le 0 })
    Report "tails (guest $k)" ($walks.Count -gt 0 -and $stiff.Count -le [math]::Floor($walks.Count / 10)) `
        "$($stiff.Count) of $($walks.Count) walking samples with the springs off (monsters $((@($stiff | ForEach-Object k) | Sort-Object -Unique) -join ', '))"
    # the same second of the PC's clock, both in the same block: points within 600 (the guest shows the host a moment
    # late), lives equal; up to 1 in 10 such samples may differ (a block's end or a life lost inside that moment)
    $same = 0; $bad = @()
    foreach ($b in $gt) {
        $a = $hAt["$($b.wall)/$($b.k)"]
        if (-not $a -or $a.block -ne $b.block) { continue }
        $same++
        if ([math]::Abs($a.points - $b.points) -gt 600 -or $a.lives -ne $b.lives) {
            $bad += "monster $($b.k) at $($b.wall): points host $($a.points) guest $($b.points), lives $($a.lives) / $($b.lives)"
        }
    }
    Report "scores (guest $k)" ($same -ge 10 -and $bad.Count -le [math]::Floor($same / 10)) `
        "$($bad.Count) of $same samples differ (first: $($bad | Select-Object -First 1))"
}
# the host's upload per guest, on average: a 2 player game's one guest costs 136 KB/s, a 4 player one's 199 (measured
# 2026-11-01: the snapshot carries 4 monsters, and 4 smash more); 230 allowed. Most of it is the world's puppets (people,
# army, traffic) sent to each guest; fewer updates a second or deltas would cut it (docs/TODO.md).
$rates = @($hostLog | ForEach-Object { if ($_ -match 'net: rate: snapshot (\d+) bytes, ([\d.]+) KB/s out') {
    [pscustomobject]@{ bytes = [int]$Matches[1]; kb = [double]$Matches[2] } } } | Where-Object { $_.kb -gt 0 })
$avg = if ($rates) { [math]::Round(($rates | Measure-Object kb -Average).Average, 1) } else { -1 }
$top = if ($rates) { ($rates | Measure-Object kb -Maximum).Maximum } else { -1 }
$big = if ($rates) { ($rates | Measure-Object bytes -Maximum).Maximum } else { -1 }
Report "upload" ($avg -ge 0 -and $avg / [math]::Max(1, $Guests) -le 230) "the host sent $avg KB/s on average to $Guests guest(s)"
"  (the host: $avg KB/s on average, $top at most; snapshots up to $big bytes)"
if ($Rejoin -gt 0) {
    $back = @($hostLog | Select-String -SimpleMatch "slot ${Rejoin}: online").Count
    Report "rejoin (seat $Rejoin)" ($back -ge 2) "seat $Rejoin went online $back time(s)"
}
if ($JoinLate) {
    $starts = @($hostLog | Select-String -SimpleMatch "flow: state 6 (block").Count
    $mid = @($hostLog | Select-String -SimpleMatch "(mid-block: nothing started again)").Count
    $loads = @(); for ($k = 1; $k -le $Guests; $k++) {
        $loads += @(Get-Content "$out\guest$k.out" -ErrorAction SilentlyContinue | Select-String -SimpleMatch "loading the host's game").Count }
    Report "joinlate" ($mid -eq $Guests -and $starts -le 1 -and -not ($loads | Where-Object { $_ -ne 1 })) `
        "joined mid-block $mid of $Guests, the host's block starts $starts, the guests' loads $($loads -join ', ')"
}
if ($Shots -gt 0) { "screenshots: $out\net4_1.png .. net4_$Shots.png" }
if ($fail) { exit 1 }
