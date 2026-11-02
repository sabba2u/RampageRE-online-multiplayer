<#
net8_menu_test.ps1 - eight players through the menus, on one PC: a host in the front end (--menu-players 8) and seven
guests, all over the network on localhost. The host picks King of the City, 8 Player with no computer players; every
guest readies its own card on Monster Select by its own Enter (its seat's pad on the host), the host its own, then the
host picks the city. The keys go to each window as WM_KEYDOWN / WM_KEYUP (net_keys.ps1's way), so nothing needs the focus.
  .\tools\net8_menu_test.ps1                      eight players
  .\tools\net8_menu_test.ps1 -Guests 4            five (the host picks 5 Player)
  .\tools\net8_menu_test.ps1 -Keep                leave them running at the end
Runs in build\net8test (its own profile and rampage.cfg, none of this folder's). PASS / FAIL per check; pictures of every
window in build\net8test\ms.png (Monster Select) and game.png (in the city).
#>
param([int]$Guests = 7, [int]$Port = 27961, [string]$Exe = "RampageRE-dev.exe", [switch]$Keep,
      [string]$Iso = "Rampage - Total Destruction (Europe) (En,Fr,De,Es,It).iso")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root $Exe
$iso = Join-Path $root $Iso
$dir = Join-Path $root "build\net8test"
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
New-Item -ItemType Directory -Force $dir | Out-Null
# a profile, so no copy stops at the name entry (a guest's own popup keeps it out of the host's menus): this folder's
# copied, else none (then answer the name entry yourself)
foreach ($f in "rampage_profile.bin", "rampage_profile.wii.bin") {
    if (Test-Path (Join-Path $root $f)) { Copy-Item (Join-Path $root $f) $dir }
}
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Net8W {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
function Log([string]$m) { Add-Content (Join-Path $dir "progress.log") ("{0:HH:mm:ss} {1}" -f (Get-Date), $m) }
$KeyCodes = @{ Return = 0x0D; Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27 }
function Keys($p, [string]$keys, [int]$gap = 450) {
    foreach ($k in $keys.Split(",")) {
        $code = $KeyCodes[$k.Trim()]
        [Net8W]::PostMessage($p.MainWindowHandle, 0x100, [IntPtr]$code, [IntPtr]1) | Out-Null
        Start-Sleep -Milliseconds 80
        [Net8W]::PostMessage($p.MainWindowHandle, 0x101, [IntPtr]$code, [IntPtr]0xC0000001) | Out-Null
        Start-Sleep -Milliseconds $gap
    }
}
function Shot($procs, [string]$name) {
    $imgs = foreach ($p in $procs) {
        $p.Refresh()
        $r = New-Object Net8W+RECT
        [Net8W]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
        $w = [math]::Max(1, $r.R - $r.L); $h = [math]::Max(1, $r.B - $r.T)
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Net8W]::PrintWindow($p.MainWindowHandle, $hdc, 3) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose(); $bmp
    }
    $cols = 3; $cw = 480; $ch = 270; $rows = [math]::Ceiling($imgs.Count / $cols)
    $all = New-Object System.Drawing.Bitmap ($cw * $cols), ($ch * $rows)
    $g = [System.Drawing.Graphics]::FromImage($all)
    for ($i = 0; $i -lt $imgs.Count; $i++) { $g.DrawImage($imgs[$i], ($i % $cols) * $cw, [math]::Floor($i / $cols) * $ch, $cw, $ch) }
    $g.Dispose(); $all.Save((Join-Path $dir "$name.png")); $all.Dispose()
}
$env:RAMPAGE_NET_UPNP = "0"
$env:RAMPAGE_MS_TRACE = "1"
$common = "`"$iso`" --shell --mute --window 960x540 --cheat 141421"
$h = Start-Process $exe -ArgumentList "$common --menu-players 8 --host $Port" -WorkingDirectory $dir -PassThru `
    -RedirectStandardOutput "$dir\host.out" -RedirectStandardError "$dir\host.err"
Log "host started"
Start-Sleep 4
while ($h.MainWindowHandle -eq 0) { Start-Sleep -Milliseconds 300; $h.Refresh() }
Log "host window"
# the host first, past the boot's movies to its main menu; then the guests (joining skips their movies, netrep_menu.c),
# each answering its own autosave notice with one Enter (more would be presses on the host's main menu)
Keys $h "Return,Return,Return,Return,Return" 2500
Log "host at its main menu"
$gs = @()
for ($k = 1; $k -le $Guests; $k++) {
    $gs += Start-Process $exe -ArgumentList "$common --join 127.0.0.1:$Port" -WorkingDirectory $dir -PassThru `
        -RedirectStandardOutput "$dir\guest$k.out" -RedirectStandardError "$dir\guest$k.err"
    Start-Sleep 2
}
foreach ($p in $gs) { while ($p.MainWindowHandle -eq 0) { Start-Sleep -Milliseconds 300; $p.Refresh() } }
Log "guests up"
Start-Sleep 14
foreach ($p in $gs) { Keys $p "Return" 300 }
Log "guests past the notice"
Start-Sleep 3
$downs = (@("Down") * ($Guests + 1 - 2)) -join ","
Keys $h "Right" 800
Keys $h "Return" 1200
if ($downs) { Keys $h $downs 400 }
Keys $h "Return" 1200
Keys $h "Return" 2500                                # (hosting: no computer players is the bots' box's first)
Log "host at Monster Select"
Start-Sleep 2
foreach ($p in $gs) { Keys $p "Return" 700 }      # each guest readies its own card
Keys $h "Return" 2000                                # the host's: all ready, City Select
Shot (@($h) + $gs) "ms"
Log "shot ms"
Keys $h "Return" 1500                                # the city
Log "city picked"
Start-Sleep 30
Shot (@($h) + $gs) "game"
# closed, not killed, so each has written out what it printed (a killed copy loses its buffered output)
foreach ($p in @($h) + $gs) { if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null } }
Start-Sleep 6
$host_out = Get-Content "$dir\host.out" -Raw
$n = $Guests + 1
$results = @()
$results += if ($host_out -match "rumble|King|kingcity|Players: $n|players $n|--players $n|$n players") { "info" } else { "info" }
$started = ($host_out -match "models drawn")
$results += "{0} Monster Select had {1} players" -f ($(if ($host_out -match "MSEV ev \d+ pad 0 players $n bots 0") { "PASS" } else { "FAIL" }), $n)
for ($k = 1; $k -le $Guests; $k++) {
    $results += "{0} guest {1}'s press reached seat {1}" -f ($(if ($host_out -match "MSEV ev \d+ pad $k players $n") { "PASS" } else { "FAIL" }), $k)
}
$results += "{0} host's game started" -f ($(if ($started) { "PASS" } else { "FAIL" }))
for ($k = 1; $k -le $Guests; $k++) {
    $o = Get-Content "$dir\guest$k.out" -Raw
    $in = $o -match "models drawn"
    $results += "{0} guest $k in the game" -f ($(if ($in) { "PASS" } else { "FAIL" }))
}
$results | Where-Object { $_ -ne "info" }
Select-String -Path "$dir\host.out" -Pattern "^net: (slot|seat|.*online|.*players)" | Select-Object -Last 12 | ForEach-Object { $_.Line }
if ($true) { @($h) + $gs | Where-Object { -not $_.HasExited } | Stop-Process -Force }
