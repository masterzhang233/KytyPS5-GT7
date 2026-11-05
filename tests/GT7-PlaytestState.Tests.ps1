[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$FixtureRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\scripts\GT7-PlaytestState.ps1')
$fixture = [IO.Path]::GetFullPath($FixtureRoot)
if (Test-Path -LiteralPath $fixture) { throw "Use a new fixture directory: $fixture" }
[void][IO.Directory]::CreateDirectory($fixture)
$checks = 0
function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
}
function Write-Fixture([string]$Path, [string]$Text) {
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, $Text)
}
function New-Run([string]$Root, [string]$Name, [string]$SaveTime, [string]$FinishTime,
                 [string]$Status='finished', [int]$ExitCode=0, [string]$Mode='playtest',
                 [string]$Contents='saved-progress', [int]$ProcessId=0) {
    $run = Join-Path $Root $Name
    $save = Join-Path $run '_DownloadData\PPSA01317\gt7save.bin'
    Write-Fixture $save $Contents
    [IO.File]::SetLastWriteTimeUtc($save, [DateTimeOffset]::Parse($SaveTime).UtcDateTime)
    Write-Fixture (Join-Path $run '_SaveData\PPSA01317\slot.bin') $Name
    Write-Fixture (Join-Path $run '_Trophies\PPSA01317\trophies.json') $Name
    $manifest = @{kind='gt7_dlss_experimental_playtest';status=$Status;exit_code=$ExitCode;
                  mode=$Mode;finished_utc=$FinishTime;pid=$ProcessId}
    Write-Fixture (Join-Path $run 'playtest-manifest.json') ($manifest | ConvertTo-Json)
    return $run
}
$seed = Join-Path $fixture 'seed'
Write-Fixture (Join-Path $seed '_SaveData\PPSA01317\slot.bin') 'seed-save'
Write-Fixture (Join-Path $seed '_ShaderCache\seed.cache') 'seed-shader'
$srRoot = Join-Path $fixture 'sr-runs'
$fgRoot = Join-Path $fixture 'fg-runs'
$old = New-Run $srRoot 'older-save-later-finish' '2026-11-01T01:00:00Z' '2026-11-04T22:00:00Z'
$expected = New-Run $fgRoot 'newest-good' '2026-11-04T20:00:00Z' '2026-11-04T20:01:00Z' -Contents 'career-buycar-cafe-complete'
$failed = New-Run $fgRoot 'failed' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z' -ExitCode 1
$running = New-Run $fgRoot 'running' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z' -Status 'running'
$active = New-Run $fgRoot 'stale-finished-but-active' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z' -ProcessId $PID
$graphics = New-Run $fgRoot 'graphics' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z' -Mode 'graphics_check_only'
$empty = New-Run $fgRoot 'empty-save' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z' -Contents ''
$malformed = New-Run $fgRoot 'malformed-manifest' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z'
[IO.File]::WriteAllText((Join-Path $malformed 'playtest-manifest.json'), '{broken')
$nested = New-Run (Join-Path $fgRoot 'not-a-run') 'nested-run' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z'
$before = @(Get-ChildItem -LiteralPath $fixture -Recurse -Force).Count
$selection = Select-GT7PlaytestState -SourceUserDataRoot $seed -OutputRoot $srRoot -ProgressSearchRoots @($srRoot,$fgRoot)
Assert-Test ($selection.selected_root -eq $expected) 'newest actual save wins across SR/FG roots; unsafe runs excluded'
Assert-Test ($selection.selection -eq 'completed_playtest' -and $selection.save_bytes -gt 0 -and $selection.save_sha256.Length -eq 64) 'selection records actual save metadata'
Assert-Test ($selection.source_finished_utc -eq '2026-11-04T20:01:00.0000000Z') 'completion timestamp retains UTC across PowerShell JSON versions'
Assert-Test (@(Get-ChildItem -LiteralPath $fixture -Recurse -Force).Count -eq $before) 'selection creates no directories or files'
$default = Select-GT7PlaytestState -SourceUserDataRoot $seed -OutputRoot $srRoot
Assert-Test ($default.selected_root -eq $old -and $default.search_roots.Count -eq 1) 'default search root is OutputRoot'
$tied = New-Run $srRoot 'same-save-later-completion' '2026-11-04T20:00:00Z' '2026-11-04T20:02:00Z'
$tieSelection = Select-GT7PlaytestState -SourceUserDataRoot $seed -OutputRoot $srRoot -ProgressSearchRoots @($srRoot,$fgRoot)
Assert-Test ($tieSelection.selected_root -eq $tied) 'equal save timestamps use successful completion timestamp'
$fallback = Select-GT7PlaytestState -SourceUserDataRoot $seed -OutputRoot (Join-Path $fixture 'missing-runs')
Assert-Test ($fallback.selected_root -eq $seed -and $fallback.selection -eq 'seed_fallback' -and $null -eq $fallback.save_sha256) 'legacy seed without gt7save is safe fallback'
$seedCandidate = Select-GT7PlaytestState -SourceUserDataRoot $expected -OutputRoot (Join-Path $fixture 'missing-runs')
Assert-Test ($seedCandidate.selected_root -eq $expected -and $seedCandidate.selection -eq 'completed_playtest') 'qualified seed participates as a candidate'
$activeSeedRefused = $false
try { $null = Select-GT7PlaytestState -SourceUserDataRoot $active -OutputRoot (Join-Path $fixture 'missing-runs') } catch { $activeSeedRefused = $true }
Assert-Test $activeSeedRefused 'active seed cannot be used through fallback'
$destination = Join-Path $fixture 'copy-target'
[void][IO.Directory]::CreateDirectory($destination)
$originalHash = (Get-FileHash -LiteralPath $selection.save_path).Hash
Copy-GT7PlaytestState -Selection $selection -DestinationRoot $destination
Assert-Test ((Get-FileHash -LiteralPath (Join-Path $destination '_DownloadData\PPSA01317\gt7save.bin')).Hash -eq $originalHash) 'actual DownloadData career save copied intact'
Assert-Test ([IO.File]::ReadAllText((Join-Path $destination '_SaveData\PPSA01317\slot.bin')) -eq 'newest-good') 'SaveData comes from chosen progress'
Assert-Test ([IO.File]::ReadAllText((Join-Path $destination '_Trophies\PPSA01317\trophies.json')) -eq 'newest-good') 'trophies come from chosen progress'
Assert-Test ([IO.File]::ReadAllText((Join-Path $destination '_ShaderCache\seed.cache')) -eq 'seed-shader') 'shader cache remains seeded independently'
Assert-Test ((Get-FileHash -LiteralPath $selection.save_path).Hash -eq $originalHash) 'original progress is never overwritten'
$overwriteRefused = $false
try { Copy-GT7PlaytestState -Selection $selection -DestinationRoot $destination } catch { $overwriteRefused = $true }
Assert-Test $overwriteRefused 'nonempty destination is never overwritten'
$changedDestination = Join-Path $fixture 'changed-source-target'
[void][IO.Directory]::CreateDirectory($changedDestination)
[IO.File]::AppendAllText($selection.save_path, '-changed-after-selection')
$changedRefused = $false
try { Copy-GT7PlaytestState -Selection $selection -DestinationRoot $changedDestination } catch { $changedRefused = $true }
Assert-Test $changedRefused 'source save changed after selection fails before copying'
Assert-Test (@(Get-ChildItem -LiteralPath $changedDestination -Force).Count -eq 0) 'changed source leaves destination empty'
# Junctions require no Developer Mode on Windows. Keep fixtures for inspection;
# this test deliberately performs no recursive cleanup or deletion.
$linked = New-Run $fgRoot 'linked-state' '2026-11-04T23:00:00Z' '2026-11-04T23:01:00Z'
$junction = Join-Path $linked '_DownloadData\PPSA01317\linked-directory'
$null = New-Item -ItemType Junction -Path $junction -Target $seed
Assert-Test ($null -eq (Get-GT7CompletedProgressCandidate -Root $linked)) 'nested state junction makes a candidate ineligible'
$aliasRoot = Join-Path $fixture 'linked-search'
$null = New-Item -ItemType Junction -Path $aliasRoot -Target $fgRoot
$linkedSelection = Select-GT7PlaytestState -SourceUserDataRoot $seed -OutputRoot $aliasRoot
Assert-Test ($linkedSelection.selection -eq 'seed_fallback') 'linked search roots are not followed'
Write-Output "GT7 playtest progress checks passed: $checks; fixtures retained at $fixture"
