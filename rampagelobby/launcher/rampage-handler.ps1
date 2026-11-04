# Opens rampage:// links from the lobby's games page:
#   rampage://join/K7PQ2   RampageRE.exe --lobby <lobby> --join K7PQ2          (a join code: the game asks
#                                                                                         the lobby for the address)
#   rampage://join/203.0.113.5:27960   RampageRE.exe --lobby <lobby> --join 203.0.113.5:27960
# Any web page can open a rampage:// link, so only these two forms are accepted, and the game and the lobby come
# from config.json next to this script (written by install.ps1), never from the link.
param([string]$Url, [switch]$DryRun)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Fail([string]$message) {
  Add-Content -Path (Join-Path $here 'handler.log') -Value "$(Get-Date -Format s) $message"
  if ($DryRun) { Write-Output "ERROR: $message"; exit 1 }
  Add-Type -AssemblyName System.Windows.Forms
  [void][System.Windows.Forms.MessageBox]::Show($message, 'RampageRE', 'OK', 'Error')
  exit 1
}

try { $config = Get-Content -Raw (Join-Path $here 'config.json') | ConvertFrom-Json }
catch { Fail "RampageRE links are not set up: run install.ps1 again." }

# A join code (5 of ABCDEFGHJKLMNPQRSTUVWXYZ23456789, any case), or a host name / IPv4 with an optional port.
$code = '^rampage://join/([A-HJ-NP-Za-hj-np-z2-9]{5})/?$'
$addr = '^rampage://join/([A-Za-z0-9][A-Za-z0-9.\-]{0,62}(:[0-9]{1,5})?)/?$'
if ($Url -match $code) { $target = $Matches[1].ToUpper() }
elseif ($Url -match $addr) { $target = $Matches[1] }
else { Fail "Not a RampageRE link: $Url" }

$gameArgs = @('--join', $target)
if ($config.lobby) { $gameArgs = @('--lobby', $config.lobby) + $gameArgs }
if ($DryRun) { Write-Output "$($config.exe) $($gameArgs -join ' ')"; exit 0 }
if (-not (Test-Path $config.exe)) { Fail "RampageRE is not at $($config.exe) any more: run install.ps1 again." }
Start-Process -FilePath $config.exe -ArgumentList $gameArgs -WorkingDirectory (Split-Path -Parent $config.exe)
