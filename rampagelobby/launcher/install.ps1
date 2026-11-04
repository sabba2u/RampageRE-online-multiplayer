# Sets up RampageRE links (rampage://) for this Windows user, so the lobby's games page can start the game.
# No admin needed: it writes to %LOCALAPPDATA%\RampageRELinks and HKCU\Software\Classes\rampage.
#   powershell -ExecutionPolicy Bypass -File install.ps1 [-Exe <path to RampageRE.exe>] [-Lobby <http://host:19980>]
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
param([string]$Exe, [string]$Lobby = '__LOBBY__', [switch]$Uninstall)

$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:LOCALAPPDATA 'RampageRELinks'
$key = 'HKCU:\Software\Classes\rampage'

if ($Uninstall) {
  if (Test-Path $key) { Remove-Item $key -Recurse }
  if (Test-Path $dir) { Remove-Item $dir -Recurse }
  Write-Output 'RampageRE links removed.'
  exit 0
}

if (-not $Exe) {
  Add-Type -AssemblyName System.Windows.Forms
  $pick = New-Object System.Windows.Forms.OpenFileDialog
  $pick.Title = 'Where is RampageRE? Pick RampageRE.exe (or RampageRE-dev.exe)'
  $pick.Filter = 'RampageRE|RampageRE*.exe|Programs|*.exe'
  if ($pick.ShowDialog() -ne 'OK') { Write-Output 'Cancelled.'; exit 1 }
  $Exe = $pick.FileName
}
$Exe = (Resolve-Path $Exe).Path
if ($Lobby -eq '__LOBBY__') { $Lobby = '' }
if ($Lobby -and $Lobby -notmatch '^https?://[A-Za-z0-9.\-]+(:[0-9]{1,5})?/?$') { throw "Not a lobby address: $Lobby" }

New-Item -ItemType Directory -Force $dir | Out-Null
$handler = Join-Path $dir 'rampage-handler.ps1'
# The handler, as served with this installer (the lobby puts it here).
$handlerText = @'
__HANDLER__
'@
if ($handlerText.Trim() -eq '__HANDLER__') { Copy-Item (Join-Path $PSScriptRoot 'rampage-handler.ps1') $handler -Force }
else { Set-Content -Path $handler -Value $handlerText -Encoding UTF8 }
[ordered]@{ exe = $Exe; lobby = $Lobby } | ConvertTo-Json | Set-Content -Path (Join-Path $dir 'config.json') -Encoding UTF8

New-Item -Force $key | Out-Null
Set-ItemProperty $key '(default)' 'URL:RampageRE'
Set-ItemProperty $key 'URL Protocol' ''
New-Item -Force "$key\shell\open\command" | Out-Null
$command = "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$handler`" `"%1`""
Set-ItemProperty "$key\shell\open\command" '(default)' $command

Write-Output "RampageRE links are set up: $Exe$(if ($Lobby) { ", lobby $Lobby" })."
Write-Output 'Back on the games page, Join now starts the game. (-Uninstall removes this.)'
