<#
net_test.ps1 - online play on one PC: starts a host and a guest copy of rampage.exe (build.bat dev) over localhost,
keeps their output in build\nettest\host.out / guest.out, and optionally takes desktop screenshots.

  .\tools\net_test.ps1                          Chicago 1, George and Lizzie, 40 seconds
  .\tools\net_test.ps1 -Seconds 0               leave both running (play them yourself; Insert opens the panel)
  .\tools\net_test.ps1 -Lag 100 -Loss 5         the internet on one PC: 100 ms each way, 5 % of the updates lost
  .\tools\net_test.ps1 -GameArgs "NewYork 3 --players 2 --play kingcity"
  .\tools\net_test.ps1 -JoinDelay 40 -HostDo "up 1.5, square 0.3, wait 0.4, collapse"   the guest joins 40 s late
                                                (the host's monster plays the --do words first)
  .\tools\net_test.ps1 -Shots 3                 three screenshots of the desktop into build\nettest\shot_N.png
  .\tools\net_test.ps1 -Menus -Seconds 0         both in the front end (--shell): the host picks 2 Player, the guest
                                                its monster (tools\net_keys.ps1 presses keys in either window)
  .\tools\net_test.ps1 -Upnp                    the host asks the router to forward its port too (off by default here)
#>
param([int]$Seconds = 40, [int]$Port = 27960, [int]$Lag = 0, [int]$Loss = 0, [int]$Shots = 0, [switch]$TestPad, [switch]$Trace,
      [switch]$Menus, [switch]$Upnp, [string]$GuestArgs = "", [int]$JoinDelay = 6, [string]$HostDo = "",
      [string]$GameArgs = "Chicago 1 --players 2 --monster George --monster2 Lizzie")
$root = Split-Path $PSScriptRoot -Parent
if ($Menus) { $GameArgs = "--shell"; $GuestArgs = "--shell" }
$exe = Join-Path $root "rampage.exe"
if (-not (Test-Path $exe)) { throw "Build first: build.bat dev" }
$out = Join-Path $root "build\nettest"
New-Item -ItemType Directory -Force $out | Out-Null
Get-Process rampage -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Stop-Process -Force   # (only this folder's)
$env:RAMPAGE_NET_LAG = "$Lag"
$env:RAMPAGE_NET_LOSS = "$Loss"
$env:RAMPAGE_NET_TRACE = if ($Trace) { "1" } else { $null }
$env:RAMPAGE_NET_HOSTDO = if ($HostDo) { $HostDo } else { $null }   # (--do's words for the host's monster)
$env:RAMPAGE_NET_UPNP = if ($Upnp) { $null } else { "0" }   # (localhost: the router is left alone unless -Upnp)
$h = Start-Process $exe -ArgumentList "$GameArgs --host $Port" -WorkingDirectory $root -PassThru `
    -RedirectStandardOutput "$out\host.out" -RedirectStandardError "$out\host.err"
Start-Sleep $JoinDelay
$env:RAMPAGE_NET_HOSTDO = $null
$env:RAMPAGE_NET_TESTPAD = if ($TestPad) { "1" } else { $null }   # (the guest's alone)
$g = Start-Process $exe -ArgumentList ("$GuestArgs --join 127.0.0.1:$Port".Trim()) -WorkingDirectory $root -PassThru `
    -RedirectStandardOutput "$out\guest.out" -RedirectStandardError "$out\guest.err"
if ($Seconds -le 0) { "host pid $($h.Id), guest pid $($g.Id)"; return }

$every = if ($Shots -gt 0) { [math]::Max(1, [int](($Seconds - 10) / $Shots)) } else { 0 }
Start-Sleep 10
for ($i = 1; $i -le $Shots; $i++) {
    & (Join-Path $PSScriptRoot "net_shot.ps1") -Name "shot_$i"
    Start-Sleep $every
}
$left = $Seconds - 10 - $Shots * $every
if ($left -gt 0) { Start-Sleep $left }
"host alive: $(-not $h.HasExited), guest alive: $(-not $g.HasExited)"
$h, $g | Where-Object { -not $_.HasExited } | Stop-Process -Force
"--- host"; Get-Content "$out\host.out" | Select-String "^net:" | Select-Object -Last 20
"--- guest"; Get-Content "$out\guest.out" | Select-String "^net:" | Select-Object -Last 20
