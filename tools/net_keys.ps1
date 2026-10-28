<#
net_keys.ps1 - presses keys in a running rampage.exe window of this folder (the host's is the first started, the
guest's the second), as WM_KEYDOWN / WM_KEYUP: the window need not have the focus.
  .\tools\net_keys.ps1 -Who host -Keys "Return,Right,Return" [-Gap 400]
Names: Return Space Back Escape Up Down Left Right Insert P C V, Num0 .. Num9, or a number (the virtual key).
#>
param([ValidateSet("host", "guest")][string]$Who = "host", [string]$Keys = "Return", [int]$Gap = 400)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class NetKeysW {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "rampage.exe"
$procs = @(Get-Process rampage -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 -and $_.Path -eq $exe } |
    Sort-Object StartTime)
$p = if ($Who -eq "host") { $procs[0] } else { $procs[1] }
if (-not $p) { throw "no $Who window" }
$Names = @{ Return = 0x0D; Space = 0x20; Back = 0x08; Escape = 0x1B; Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27;
         Insert = 0x2D; P = 0x50; C = 0x43; V = 0x56 }
foreach ($k in $Keys.Split(",")) {
    $k = $k.Trim()
    $vk = if ($Names.ContainsKey($k)) { $Names[$k] } elseif ($k -match '^Num(\d)$') { 0x60 + [int]$Matches[1] } else { [int]$k }
    [NetKeysW]::PostMessage($p.MainWindowHandle, 0x100, [IntPtr]$vk, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 80
    [NetKeysW]::PostMessage($p.MainWindowHandle, 0x101, [IntPtr]$vk, [IntPtr]0xC0000001) | Out-Null
    Start-Sleep -Milliseconds $Gap
}
