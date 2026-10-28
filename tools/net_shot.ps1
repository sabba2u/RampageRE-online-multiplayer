<#
net_shot.ps1 - a picture of each running rampage.exe window (the host's and the guest's), side by side, into
build\nettest\<name>.png: PrintWindow on each window, so whatever covers them is not in it.
  .\tools\net_shot.ps1 -Name both_1
#>
param([string]$Name = "both")
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
$exe = Join-Path $root "rampage.exe"
$procs = Get-Process rampage -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 -and $_.Path -eq $exe } |
    Sort-Object StartTime   # this folder's copies only
if (-not $procs) { throw "no rampage.exe window" }
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
$W = 0; $H = 0
foreach ($b in $imgs) { $W += $b.Width; if ($b.Height -gt $H) { $H = $b.Height } }
$all = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($all); $x = 0
foreach ($b in $imgs) { $g.DrawImage($b, $x, 0); $x += $b.Width; $b.Dispose() }
$g.Dispose()
$to = Join-Path $root "build\nettest\$Name.png"
$all.Save($to); $all.Dispose()
"wrote $to ($($procs.Count) windows: host first)"
