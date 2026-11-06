[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$FixtureRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\scripts\GT7-LaunchSupport.ps1')

$fixture = [IO.Path]::GetFullPath($FixtureRoot)
if (Test-Path -LiteralPath $fixture) { throw 'Use a fresh fixture directory.' }
[void][IO.Directory]::CreateDirectory($fixture)
$entry = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\scripts\Start-GT7-Desktop.ps1')).ProviderPath
$shell = (Get-Process -Id $PID).Path
$checks = 0
function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw ('FAIL: ' + $Message) }
    $script:checks++
}
function Invoke-Entry([string]$Script, [string[]]$Extra) {
    $child = New-Object System.Diagnostics.Process
    try {
        $child.StartInfo = New-GT7ProcessStartInfo -Executable $shell `
            -ArgumentList (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Script, '-CheckOnly') + $Extra) `
            -WorkingDirectory $fixture -CaptureOutput
        if (-not $child.Start()) { throw 'Could not start the script test.' }
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        $child.WaitForExit()
        return [pscustomobject]@{ ExitCode = $child.ExitCode;
            Output = $stdout.GetAwaiter().GetResult(); Error = $stderr.GetAwaiter().GetResult() }
    } finally { $child.Dispose() }
}
$install = Join-Path $fixture 'Release with spaces & brackets [1]'
[void][IO.Directory]::CreateDirectory($install)
foreach ($name in @('launcher.exe', 'kyty_emulator.exe', 'kyty_dlss_backend.dll', 'nvngx_dlss.dll', 'amd_fidelityfx_vk.dll')) {
    # These are deliberately not executable: CheckOnly must never invoke any of them.
    [IO.File]::WriteAllText((Join-Path $install $name), 'check-only fixture; never execute')
}
$before = @(Get-ChildItem -LiteralPath $install -File | Sort-Object Name | Get-FileHash)
$result = Invoke-Entry $entry @('-InstallDirectory', $install)
Assert-Test ($result.ExitCode -eq 0) ('Explicit release path: ' + $result.Error)
$report = $result.Output | ConvertFrom-Json
Assert-Test ($report.executable -eq (Join-Path $install 'launcher.exe') -and $report.working_directory -eq $install) 'Uses selected release launcher and release working directory'
Assert-Test (-not $report.started -and -not $report.save_files_modified) 'CheckOnly does not launch or write saves'
Assert-Test ($report.entry_point -eq 'desktop_launcher' -and $report.required_files_present) 'Entry is the complete desktop release'
$packaged = Join-Path $install 'Start-GT7-Desktop.ps1'
Copy-Item -LiteralPath $entry -Destination $packaged
$result = Invoke-Entry $packaged @()
Assert-Test ($result.ExitCode -eq 0 -and ($result.Output | ConvertFrom-Json).working_directory -eq $install) 'Packaged script resolves its sibling launcher'
$after = @(Get-ChildItem -LiteralPath $install -File | Where-Object Name -ne 'Start-GT7-Desktop.ps1' | Sort-Object Name | Get-FileHash)
Assert-Test (($before.Hash -join ',') -eq ($after.Hash -join ',')) 'All original package files remain unchanged'
Assert-Test (-not (Test-Path -LiteralPath (Join-Path $install '_DownloadData'))) 'No user-data directory is created'
$incomplete = Join-Path $fixture 'Incomplete release'
[void][IO.Directory]::CreateDirectory($incomplete)
Copy-Item -LiteralPath (Join-Path $install 'launcher.exe') -Destination $incomplete
$result = Invoke-Entry $entry @('-InstallDirectory', $incomplete)
Assert-Test ($result.ExitCode -ne 0 -and $result.Error.Contains('amd_fidelityfx_vk.dll')) 'Incomplete release rejects missing frame-generation runtime'
$result = Invoke-Entry $entry @('-InstallDirectory', (Join-Path $fixture 'missing release'))
Assert-Test ($result.ExitCode -ne 0) 'Missing installation fails without fallback to old builds'
Write-Output ("PASS: {0} desktop entry checks; no game or launcher executed." -f $checks)
