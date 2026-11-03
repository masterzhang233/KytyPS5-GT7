[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GamePath,
    [string]$EmulatorPath = '',
    [string]$SourceUserDataRoot = '',
    [switch]$ReuseShaderCache,
    [ValidateScript({ $_ -ge 0 -and $_ -ne 254 -and $_ -ne 255 })][int]$UserId = 1000,
    [string]$OutputRoot = '',
    [ValidateSet('native','1080','1440','2160')][string]$DesktopHeight = 'native',
    [ValidateSet('1920x1080','2560x1440','3840x2160')][string]$OutputResolution = '2560x1440',
    [ValidateRange(2,8)][int]$Frames = 4,
    [string]$ImageSelectors = '1920x1080,2560x1440,3840x2160,1280x720,960x540',
    [ValidatePattern('^([0-9a-fA-F]{16})(,[0-9a-fA-F]{16})*$')][string]$ShaderHashes,
    [ValidatePattern('^([0-9a-fA-F]{16})(,[0-9a-fA-F]{16})*$')][string]$ShaderMemoryHashes,
    [switch]$DiscoverTemporalCandidates,
    [ValidateRange(64,16384)][int]$ImageMaxSide = 1024,
    [ValidateRange(16,1024)][int]$ImageReadbackMB = 64,
    [ValidateRange(1,80)][int]$MaxImages = 24,
    [ValidateRange(16,4096)][int]$RawBudgetMB = 512,
    [string]$RunNote = '',
    [switch]$CheckOnly
)
# A developer capture, not a DLSS enable switch. Environment settings apply only to the child.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GT7-LaunchSupport.ps1')

function Copy-GT7CaptureState([string]$Source, [string]$Destination, [bool]$IncludeShaderCache) {
    if (-not $Source) { return }
    # A private save copy keeps diagnostics away from the user's normal save. Do not copy
    # game content or earlier captures, and do not follow directory junctions. An explicit
    # cache copy can reuse compiled shaders from an earlier run; it is not a junction.
    $relativePaths = @('_SaveData\PPSA01317', '_Trophies\PPSA01317')
    if ($IncludeShaderCache) { $relativePaths += '_ShaderCache' }
    foreach ($relative in $relativePaths) {
        $from = Join-Path $Source $relative
        if (-not (Test-Path -LiteralPath $from -PathType Container)) { continue }
        $items = @(Get-Item -LiteralPath $Source) + @(Get-Item -LiteralPath (Split-Path -Parent $from)) +
            @(Get-Item -LiteralPath $from) + @(Get-ChildItem -LiteralPath $from -Recurse -Force)
        if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
            throw "Capture state contains a junction or symbolic link: $from"
        }
        $to = Join-Path $Destination $relative
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $to))
        Copy-Item -LiteralPath $from -Destination $to -Recurse
    }
}

try {
    if (-not $EmulatorPath) { $EmulatorPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'kyty_emulator.exe' }
    if (-not $OutputRoot) { $OutputRoot = Join-Path $env:LOCALAPPDATA 'KytyPS5-GT7\sr-captures' }
    $game = (Resolve-Path -LiteralPath $GamePath).ProviderPath
    if (Test-Path -LiteralPath $game -PathType Container) { $game = Join-Path $game 'eboot.bin' }
    if (-not (Test-Path -LiteralPath $game -PathType Leaf) -or [IO.Path]::GetFileName($game) -ine 'eboot.bin') {
        throw 'GamePath must be an unpacked game folder or its eboot.bin.'
    }
    $exe = (Resolve-Path -LiteralPath $EmulatorPath).ProviderPath
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'EmulatorPath must be an executable file.' }
    if ($SourceUserDataRoot) {
        $SourceUserDataRoot = (Resolve-Path -LiteralPath $SourceUserDataRoot).ProviderPath
        if (-not (Test-Path -LiteralPath $SourceUserDataRoot -PathType Container)) {
            throw 'SourceUserDataRoot must be the folder containing _SaveData.'
        }
    }
    $outputBase = [IO.Path]::GetFullPath($OutputRoot)
    $size = $OutputResolution.Split('x')
    $arguments = @('--game', $game, '--screen-width', $size[0], '--screen-height', $size[1],
        '--present-mode', 'Mailbox', '--tessellation', '--bindless', '--redzone',
        '--shader-optimization-type', 'Performance', '--console-language', '11',
        '--readback-linear-images', 'false', '--printf-direction', 'File',
        '--printf-output-file', '_kyty.txt', '--gt7-desktop-height', $DesktopHeight,
        '--user-id', [string]$UserId)
    $check = Invoke-GT7CompatibilityCheck -Executable $exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $exe)
    if ($check.ExitCode -ne 0) {
        throw ('GT7 compatibility check failed (exit {0}): {1}{2}' -f
            $check.ExitCode, $check.StandardOutput, $check.StandardError)
    }
    if ($CheckOnly) {
        Write-Output ('CheckOnly passed; {0} frames per F9; no game or capture folder created.' -f $Frames)
        exit 0
    }
    $runName = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $run = Join-Path $outputBase $runName
    [void][IO.Directory]::CreateDirectory($run)
    if ($ReuseShaderCache) { Write-Host 'Copying the previous shader cache into this private capture folder...' }
    Copy-GT7CaptureState -Source $SourceUserDataRoot -Destination $run -IncludeShaderCache ([bool]$ReuseShaderCache)
    $settings = [ordered]@{
        KYTY_FG_FRAMES = [string]$Frames
        KYTY_FG_VERIFY = '0'
        KYTY_FG_SAMPLES = '0'
        KYTY_DUMP_IMAGES = $ImageSelectors
        KYTY_DUMP_IMAGES_MAX = [string]$MaxImages
        KYTY_DUMP_IMAGES_SIZE = [string]$ImageMaxSide
        KYTY_DUMP_IMAGES_MB = [string]$ImageReadbackMB
        KYTY_DUMP_IMAGES_RAW = '1'
        KYTY_DUMP_IMAGES_RAW_MB = [string]$RawBudgetMB
    }
    if ($ShaderHashes) { $settings['KYTY_DUMP_IMAGES_SHADERS'] = $ShaderHashes }
    if ($ShaderMemoryHashes) { $settings['KYTY_FG_DUMP_SHADERS'] = $ShaderMemoryHashes }
    if ($DiscoverTemporalCandidates) { $settings['KYTY_DUMP_IMAGES_TEMPORAL_CANDIDATES'] = '1' }
    if ($ShaderMemoryHashes -or $DiscoverTemporalCandidates) {
        $settings['KYTY_FG_DUMP_BYTES'] = '4096'
    }
    $manifest = [ordered]@{
        schema = 1; kind = 'gt7_temporal_input_capture'; dlss_enabled = $false
        started_utc = [DateTime]::UtcNow.ToString('o'); run_note = $RunNote
        emulator = $exe; emulator_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        game = $game; source_user_data = $SourceUserDataRoot; user_id = $UserId; working_directory = $run
        shader_cache_copied = [bool]($ReuseShaderCache -and $SourceUserDataRoot -and
            (Test-Path -LiteralPath (Join-Path $SourceUserDataRoot '_ShaderCache') -PathType Container))
        arguments = $arguments; environment = $settings; status = 'prepared'; exit_code = $null
        raw_note = 'Raw files preserve sampled rows at the recorded mip, not necessarily the complete image. See each JSON layout.'
        image_capture_phase = 'Before the bound shader operation. A write binding is not an after-write result.'
        temporal_candidate_discovery = [bool]$DiscoverTemporalCandidates
    }
    $manifestPath = Join-Path $run 'capture-manifest.json'
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $process = New-Object System.Diagnostics.Process
    $stdout = $null; $stderr = $null
    try {
        $process.StartInfo = New-GT7ProcessStartInfo -Executable $exe -ArgumentList $arguments `
            -WorkingDirectory $run -CaptureOutput
        foreach ($key in @($process.StartInfo.EnvironmentVariables.Keys)) {
            if ($key -match '^(KYTY_|GT7_)') { $process.StartInfo.EnvironmentVariables.Remove($key) }
        }
        foreach ($entry in $settings.GetEnumerator()) {
            $process.StartInfo.EnvironmentVariables[$entry.Key] = $entry.Value
        }
        $stdout = [IO.File]::Open((Join-Path $run 'stdout.log'), [IO.FileMode]::CreateNew)
        $stderr = [IO.File]::Open((Join-Path $run 'stderr.log'), [IO.FileMode]::CreateNew)
        Write-Host "Capture folder: $run"
        Write-Host 'Enter a race and wait for shader compilation to settle. Press F9 once, then drive/turn for a few seconds.'
        Write-Host 'The capture can briefly pause rendering. Close the game normally when finished.'
        Write-Host 'This build records DLSS integration inputs; it does not enable DLSS yet.'
        if (-not $process.Start()) { throw 'Could not start the emulator.' }
        $manifest['status'] = 'running'; $manifest['pid'] = $process.Id
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        # Stream to disk: long game runs must not accumulate all output in RAM.
        $stdoutCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $stderrCopy = $process.StandardError.BaseStream.CopyToAsync($stderr)
        $process.WaitForExit()
        $stdoutCopy.GetAwaiter().GetResult(); $stderrCopy.GetAwaiter().GetResult()
        $manifest['exit_code'] = $process.ExitCode
        $manifest['status'] = 'finished'
    } catch {
        $manifest['status'] = 'error'; $manifest['error'] = $_.Exception.Message
        throw
    } finally {
        if ($null -ne $stdout) { $stdout.Dispose() }
        if ($null -ne $stderr) { $stderr.Dispose() }
        $process.Dispose()
        $manifest['finished_utc'] = [DateTime]::UtcNow.ToString('o')
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    }
    Write-Host "Capture saved: $run"
    Write-Host 'Keep _kyty.txt, capture-manifest.json, shader-dumps and image-dumps together for analysis.'
    exit $manifest['exit_code']
} catch {
    Write-Error $_
    exit 1
}
