[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GamePath,
    [Parameter(Mandatory=$true)][string]$EmulatorPath,
    [Parameter(Mandatory=$true)][string]$BackendPath,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$SourceUserDataRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [switch]$Baseline,
    [switch]$CheckOnly,
    [switch]$GraphicsCheckOnly
)
# Local developer playtest. No capture hotkey or raw texture dumping is required.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GT7-LaunchSupport.ps1')
try {
    if ($CheckOnly -and $GraphicsCheckOnly) {
        throw '-CheckOnly and -GraphicsCheckOnly are mutually exclusive.'
    }
    $exe = (Resolve-Path -LiteralPath $EmulatorPath).ProviderPath
    $backend = (Resolve-Path -LiteralPath $BackendPath).ProviderPath
    $runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).ProviderPath
    $source = (Resolve-Path -LiteralPath $SourceUserDataRoot).ProviderPath
    $game = (Resolve-Path -LiteralPath $GamePath).ProviderPath
    if (Test-Path -LiteralPath $game -PathType Container) { $game = Join-Path $game 'eboot.bin' }
    foreach ($file in @($exe, $backend, $game, (Join-Path $runtime 'nvngx_dlss.dll'))) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file" }
    }
    $arguments = @('--game', $game, '--screen-width', '2560', '--screen-height', '1440',
        '--present-mode', 'Mailbox', '--tessellation', '--bindless', '--redzone',
        '--shader-optimization-type', 'Performance', '--console-language', '11',
        '--readback-linear-images', 'false', '--printf-direction', 'File',
        '--printf-output-file', '_kyty.txt', '--gt7-desktop-height', '1080', '--user-id', '1000')
    $check = Invoke-GT7CompatibilityCheck -Executable $exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $exe)
    if ($check.ExitCode -ne 0) { throw "GT7 preflight failed: $($check.StandardOutput) $($check.StandardError)" }
    if ($CheckOnly) {
        Write-Output 'Playtest preflight passed. No game process or playtest directory created.'
        exit 0
    }
    # Keep the compatibility preflight free of the graphics flag: --gt7-check-only
    # returns before device initialization, so combining both would hide the check.
    if ($GraphicsCheckOnly) { $arguments += '--graphics-check-only' }
    $run = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    [void][IO.Directory]::CreateDirectory($run)
    $stateDirectories = @()
    if (-not $GraphicsCheckOnly) { $stateDirectories = @('_SaveData\PPSA01317', '_Trophies\PPSA01317', '_ShaderCache') }
    foreach ($relative in $stateDirectories) {
        $from = Join-Path $source $relative
        if (-not (Test-Path -LiteralPath $from -PathType Container)) { continue }
        $items = @(Get-Item -LiteralPath $source) + @(Get-Item -LiteralPath (Split-Path -Parent $from)) +
            @(Get-Item -LiteralPath $from) + @(Get-ChildItem -LiteralPath $from -Recurse -Force)
        if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
            throw "Playtest state contains a junction or symbolic link: $from"
        }
        $to = Join-Path $run $relative
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $to))
        Copy-Item -LiteralPath $from -Destination $to -Recurse -Force
    }
    $settings = [ordered]@{
        KYTY_GT7_DLSS = $(if ($Baseline) { '0' } else { '1' })
        KYTY_GT7_DLSS_BACKEND = $backend
        KYTY_GT7_DLSS_RUNTIME = $runtime
    }
    $manifest = [ordered]@{
        schema = 1; kind = 'gt7_dlss_experimental_playtest'; status = 'prepared'; exit_code = $null
        mode = $(if ($GraphicsCheckOnly) { 'graphics_check_only' } else { 'playtest' })
        graphics_check_only = [bool]$GraphicsCheckOnly
        started_utc = [DateTime]::UtcNow.ToString('o'); baseline = [bool]$Baseline
        emulator = $exe; backend = $backend; runtime_directory = $runtime
        emulator_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        backend_sha256 = (Get-FileHash -LiteralPath $backend -Algorithm SHA256).Hash.ToLowerInvariant()
        runtime_sha256 = (Get-FileHash -LiteralPath (Join-Path $runtime 'nvngx_dlss.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
        source_user_data = $source; working_directory = $run; arguments = $arguments; environment = $settings
        note = $(if ($GraphicsCheckOnly) {
            'Initializes the real emulator window and Vulkan device, then exits before guest ELF loading or execution. No game state copied. This does not validate DLSS evaluation or rendering.'
        } else {
            'Conditional 1920x1080 to 3840x2160 desktop SR; window is 2560x1440. Check runtime GT7 DLSS log counters to confirm use. Mirror fallback is spatial; exposure/temporal quality/FPS not certified.'
        })
    }
    $manifestPath = Join-Path $run 'playtest-manifest.json'
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $process = New-Object System.Diagnostics.Process
    $stdout = $null; $stderr = $null
    try {
        $process.StartInfo = New-GT7ProcessStartInfo -Executable $exe -ArgumentList $arguments -WorkingDirectory $run -CaptureOutput
        foreach ($key in @($process.StartInfo.EnvironmentVariables.Keys)) {
            if ($key -match '^(KYTY_|GT7_)') { $process.StartInfo.EnvironmentVariables.Remove($key) }
        }
        foreach ($entry in $settings.GetEnumerator()) { $process.StartInfo.EnvironmentVariables[$entry.Key] = $entry.Value }
        $stdout = [IO.File]::Open((Join-Path $run 'stdout.log'), [IO.FileMode]::CreateNew)
        $stderr = [IO.File]::Open((Join-Path $run 'stderr.log'), [IO.FileMode]::CreateNew)
        Write-Host "Playtest folder: $run"
        if ($GraphicsCheckOnly) {
            Write-Host 'Checking real window and Vulkan device initialization. The emulator will exit before loading or running the game.'
        } else {
            Write-Host 'Enter the same race/cockpit view and drive normally. No F9 capture is needed. Close normally when finished.'
            Write-Host 'This is a local experimental build. Unsupported pass combinations retain the original image.'
        }
        if (-not $process.Start()) { throw 'Could not start the emulator.' }
        $manifest['status'] = 'running'; $manifest['pid'] = $process.Id
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $stdoutCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $stderrCopy = $process.StandardError.BaseStream.CopyToAsync($stderr)
        $process.WaitForExit()
        $manifest['exit_code'] = $process.ExitCode
        $stdoutCopy.GetAwaiter().GetResult(); $stderrCopy.GetAwaiter().GetResult()
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
    Write-Host "Playtest finished: $run"
    exit $manifest['exit_code']
} catch {
    Write-Error $_
    exit 1
}
