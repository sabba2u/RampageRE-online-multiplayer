<#
net_shot.ps1 - a picture of each running RampageRE-dev.exe window (the host's and the guest's), side by side (a 2 x 2 grid for 3 or 4), into
build\nettest\<name>.png: PrintWindow on each window, so whatever covers them is not in it.
  .\tools\net_shot.ps1 -Name both_1
#>
param([string]$Name = "both", [string]$Program = "RampageRE-dev")   # -Program RampageRE: the player build's windows
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class NetShotW {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "$Program.exe"
$procs = Get-Process $Program -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 -and $_.Path -eq $exe } |
    Sort-Object StartTime   # this folder's copies only
if (-not $procs) { throw "no $Program.exe window" }
$imgs = foreach ($p in $procs) {
    $r = New-Object NetShotW+RECT
    [NetShotW]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = [math]::Max(1, $r.R - $r.L); $h = [math]::Max(1, $r.B - $r.T)
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [NetShotW]::PrintWindow($p.MainWindowHandle, $hdc, 3) | Out-Null   # client area, full content
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp
}
$imgs = @($imgs | Where-Object { $_ -is [System.Drawing.Bitmap] })
# one row for two windows; a 2 x 2 grid for three or four (host top left, then the guests in the order they started)
$cols = if ($imgs.Count -gt 4) { 3 } elseif ($imgs.Count -gt 2) { 2 } else { $imgs.Count }   # (a 3-wide grid past 4)
$cw = 0; $ch = 0
foreach ($b in $imgs) { if ($b.Width -gt $cw) { $cw = $b.Width }; if ($b.Height -gt $ch) { $ch = $b.Height } }
$rows = [math]::Ceiling($imgs.Count / $cols)
$all = New-Object System.Drawing.Bitmap ($cw * $cols), ($ch * $rows)
$g = [System.Drawing.Graphics]::FromImage($all)
for ($i = 0; $i -lt $imgs.Count; $i++) {
    $g.DrawImage($imgs[$i], ($i % $cols) * $cw, [math]::Floor($i / $cols) * $ch); $imgs[$i].Dispose()
}
$g.Dispose()
$to = Join-Path $root "build\nettest\$Name.png"
$all.Save($to); $all.Dispose()
"wrote $to ($($procs.Count) windows: host first)"
