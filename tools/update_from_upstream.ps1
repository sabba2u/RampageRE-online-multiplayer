<#
update_from_upstream.ps1 - takes jjmhalew/RampageRE's newest work into this fork: fetches origin, merges it into the
online play branch (one merge commit: the branch is pushed, so its history is never rewritten), rebuilds, and runs
online play's tests. Nothing is pushed.

  .\tools\update_from_upstream.ps1                 origin/main
  .\tools\update_from_upstream.ps1 -Rebase         replay the branch on top instead (only for a branch never pushed)

On a conflict it stops with git's message: the conflicts can only be in the lines marked NET: (docs/NETPLAY.md,
"Hooks"); fix them, `git add` the files, commit the merge (`git rebase --continue` with -Rebase), then run this again.
#>
param([switch]$Rebase, [string]$Upstream = "origin/main")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
if (git status --porcelain --untracked-files=no) { throw "Commit or stash your changes first." }
git fetch origin
$before = git rev-parse HEAD
if ($Rebase) { git rebase $Upstream } else { git merge --no-edit $Upstream }
if ($LASTEXITCODE -ne 0) { throw "Conflict: fix the NET: lines (docs/NETPLAY.md, Hooks), then continue." }
Write-Host "Upstream taken ($before -> $(git rev-parse --short HEAD)). Building..."
cmd /c "$root\build.bat" dev
if ($LASTEXITCODE -ne 0) { throw "The build failed: upstream changed something netgame.c reads (see the errors)." }
cmd /c "$root\tests\build_net_test.bat"
if ($LASTEXITCODE -ne 0) { throw "net_test failed." }
Write-Host "Now play it: .\tools\net_test.ps1 -TestPad -Shots 2 (both screens should match)."
