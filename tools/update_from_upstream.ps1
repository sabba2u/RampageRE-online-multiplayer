<#
update_from_upstream.ps1 - takes jjmhalew/RampageRE's newest work into this fork: fetches origin, replays the online
play branch on top of it (rebase), rebuilds, and runs online play's tests. Nothing is pushed.

  .\tools\update_from_upstream.ps1                 origin/main
  .\tools\update_from_upstream.ps1 -Merge          a merge commit instead of a rebase (keeps history as it was)

On a conflict it stops with git's message: the conflicts can only be in the lines marked NET: (docs/NETPLAY.md,
"Hooks"); fix them, `git add` the files, `git rebase --continue` (or commit the merge), then run this again.
#>
param([switch]$Merge, [string]$Upstream = "origin/main")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (git status --porcelain --untracked-files=no) { throw "Commit or stash your changes first." }
git fetch origin
$before = git rev-parse HEAD
if ($Merge) { git merge --no-edit $Upstream } else { git rebase $Upstream }
if ($LASTEXITCODE -ne 0) { throw "Conflict: fix the NET: lines (docs/NETPLAY.md, Hooks), then continue." }
Write-Host "Upstream taken ($before -> $(git rev-parse --short HEAD)). Building..."
cmd /c "$root\build.bat" dev
if ($LASTEXITCODE -ne 0) { throw "The build failed: upstream changed something netgame.c reads (see the errors)." }
cmd /c "$root\tests\build_net_test.bat"
if ($LASTEXITCODE -ne 0) { throw "net_test failed." }
Write-Host "Now play it: .\tools\net_test.ps1 -TestPad -Shots 2 (both screens should match)."
