[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GamePath,
    [ValidateSet('Desktop','VR')][string]$Mode = 'Desktop',
    [ValidateSet('native','1080','1440','2160')][string]$DesktopHeight = 'native',
    [ValidateSet(100,110,120,125)][int]$EyeScale = 100,
    [ValidateSet('Auto','1280x720','1920x1080','2560x1440','3840x2160')]
    [string]$OutputResolution = 'Auto',
    [string]$EmulatorPath = '',
    [string]$UserDataRoot = '',
    [switch]$Fullscreen,
    [switch]$CheckOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $EmulatorPath) {
    $EmulatorPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'kyty_emulator.exe'
}
if (-not $UserDataRoot) {
    $UserDataRoot = Join-Path $env:LOCALAPPDATA 'KytyPS5-GT7'
}
. (Join-Path $PSScriptRoot 'GT7-LaunchSupport.ps1')

try {
    if ($Mode -eq 'VR' -and $DesktopHeight -ne 'native') {
        throw 'DesktopHeight and VR mode are mutually exclusive.'
    }
    if ($Mode -eq 'Desktop' -and $PSBoundParameters.ContainsKey('EyeScale')) {
        throw 'EyeScale requires VR mode.'
    }
    $resolvedGame = (Resolve-Path -LiteralPath $GamePath).ProviderPath
    if (Test-Path -LiteralPath $resolvedGame -PathType Container) {
        $resolvedGame = Join-Path $resolvedGame 'eboot.bin'
    }
    if (-not (Test-Path -LiteralPath $resolvedGame -PathType Leaf) -or
        [IO.Path]::GetFileName($resolvedGame) -ine 'eboot.bin') {
        throw 'Select an unpacked game folder or its eboot.bin. Archives are not supported by these profiles.'
    }
    $resolvedExe = (Resolve-Path -LiteralPath $EmulatorPath).ProviderPath
    if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
        throw 'EmulatorPath must point to kyty_emulator.exe from this release.'
    }
    $userData = [IO.Path]::GetFullPath($UserDataRoot)
    if ($OutputResolution -eq 'Auto') {
        $OutputResolution = '3840x2160'
        if ($Mode -eq 'Desktop') {
            if ($DesktopHeight -eq '1080') { $OutputResolution = '1920x1080' }
            if ($DesktopHeight -eq '1440') { $OutputResolution = '2560x1440' }
        }
    }
    $dimensions = $OutputResolution.Split('x')
    $arguments = @('--game', $resolvedGame, '--screen-width', $dimensions[0],
        '--screen-height', $dimensions[1], '--present-mode', 'Mailbox', '--tessellation',
        '--bindless', '--redzone', '--shader-optimization-type', 'Performance',
        '--readback-linear-images', 'false', '--printf-direction', 'File',
        '--printf-output-file', '_kyty.txt')
    if ($Fullscreen) { $arguments += '--fullscreen' }
    if ($Mode -eq 'VR') { $arguments += @('--vr', '--gt7-vr-scale', [string]$EyeScale) }
    else { $arguments += @('--gt7-desktop-height', $DesktopHeight) }

    # The core performs the authoritative hash, original-byte and packaged-patch checks.
    # This runs from an existing directory and must not create a user data folder in CheckOnly.
    $check = Invoke-GT7CompatibilityCheck -Executable $resolvedExe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $resolvedExe)
    if ($check.StandardOutput) { Write-Output $check.StandardOutput.TrimEnd() }
    if ($check.ExitCode -ne 0) {
        throw ('GT7 compatibility check failed (exit {0}): {1}' -f $check.ExitCode, $check.StandardError.TrimEnd())
    }
    if ($CheckOnly) {
        Write-Output ('CheckOnly passed: mode={0}; output={1}; user data={2}; game not started.' -f `
            $Mode, $OutputResolution, $userData)
        exit 0
    }
    [void][IO.Directory]::CreateDirectory($userData)
    $gameProcess = New-Object System.Diagnostics.Process
    try {
        $gameProcess.StartInfo = New-GT7ProcessStartInfo -Executable $resolvedExe `
            -ArgumentList $arguments -WorkingDirectory $userData
        if (-not $gameProcess.Start()) { throw 'Could not start the emulator.' }
        $gameProcess.WaitForExit()
        $gameExit = $gameProcess.ExitCode
    } finally { $gameProcess.Dispose() }
    exit $gameExit
} catch {
    Write-Error $_
    exit 1
}
