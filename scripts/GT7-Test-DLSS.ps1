[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GamePath,
    [Parameter(Mandatory=$true)][string]$EmulatorPath,
    [Parameter(Mandatory=$true)][string]$BackendPath,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$SourceUserDataRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string[]]$ProgressSearchRoots,
    [switch]$Baseline,
    [switch]$FrameGeneration,
    [switch]$OcclusionGuard,
    [switch]$VulkanValidation,
    [switch]$CaptureTemporalInputs,
    [switch]$CaptureGeneratedFrames,
    [switch]$DiagnosticLogging,
    [switch]$CheckOnly,
    [switch]$GraphicsCheckOnly
)
# Local developer playtest. Input capture uses F9; separate FG output capture uses F10.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GT7-LaunchSupport.ps1')
. (Join-Path $PSScriptRoot 'GT7-PlaytestState.ps1')
try {
    if ($CheckOnly -and $GraphicsCheckOnly) {
        throw '-CheckOnly and -GraphicsCheckOnly are mutually exclusive.'
    }
    if ($Baseline -and $FrameGeneration) {
        throw '-Baseline and -FrameGeneration are mutually exclusive.'
    }
    if ($OcclusionGuard -and -not $FrameGeneration) {
        throw '-OcclusionGuard requires -FrameGeneration.'
    }
    if ($CaptureTemporalInputs -and $GraphicsCheckOnly) {
        throw '-CaptureTemporalInputs requires a game session and cannot be combined with -GraphicsCheckOnly.'
    }
    if ($CaptureGeneratedFrames -and (-not $FrameGeneration -or $GraphicsCheckOnly -or $CaptureTemporalInputs)) {
        throw '-CaptureGeneratedFrames requires -FrameGeneration and cannot be combined with -GraphicsCheckOnly or -CaptureTemporalInputs.'
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
    $frameGenerationRuntime = Join-Path (Split-Path -Parent $exe) 'amd_fidelityfx_vk.dll'
    if ($FrameGeneration -and -not (Test-Path -LiteralPath $frameGenerationRuntime -PathType Leaf)) {
        throw "Missing frame generation runtime: $frameGenerationRuntime"
    }
    $progress = $null
    if (-not $GraphicsCheckOnly) {
        $progress = Select-GT7PlaytestState -SourceUserDataRoot $source -OutputRoot $OutputRoot -ProgressSearchRoots $ProgressSearchRoots
        Write-Host "Progress source ($($progress.selection)): $($progress.selected_root)"
        Write-Host "GT7 save: $($progress.save_path); SHA-256: $($progress.save_sha256)"
    }
    # Full per-API traces can grow by tens of GiB per hour even in menus.
    # Ordinary play keeps fatal messages in stdout without the detailed trace.
    $traceEnabled = [bool]($DiagnosticLogging -or $CaptureTemporalInputs)
    $logDirection = $(if ($traceEnabled) { 'File' } else { 'Silent' })
    Write-Host "Logging mode: $logDirection (detailed trace opt-in; fatal output is still retained)."
    $arguments = @('--game', $game, '--screen-width', '2560', '--screen-height', '1440',
        '--present-mode', 'Mailbox', '--tessellation', '--bindless', '--redzone',
        '--shader-optimization-type', 'Performance', '--console-language', '11',
        '--readback-linear-images', 'false', '--printf-direction', $logDirection,
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
    if ($VulkanValidation) { $arguments += @('--vulkan-validation', 'true') }
    $run = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    [void][IO.Directory]::CreateDirectory($run)
    if (-not $GraphicsCheckOnly) { Copy-GT7PlaytestState -Selection $progress -DestinationRoot $run }
    $settings = [ordered]@{
        KYTY_GT7_DLSS = $(if ($Baseline) { '0' } else { '1' })
        KYTY_GT7_DLSS_BACKEND = $backend
        KYTY_GT7_DLSS_RUNTIME = $runtime
        KYTY_GT7_FSR_FG = $(if ($FrameGeneration) { '1' } else { '0' })
        KYTY_GT7_FSR_FG_OCCLUSION_GUARD = $(if ($OcclusionGuard) { '1' } else { '0' })
    }
    $captureSettings = $null
    if ($CaptureTemporalInputs) {
        # Match the verified TAA in Gt7Dlss::BeginTaa and the final tone-map pass in
        # FrameGenerationTonemapMatches. Keep the 1080p depth/MV/color at mip 0,
        # row_step=1; larger history/tone-map inputs can use a mip or sampled rows.
        $captureSettings = [ordered]@{
            KYTY_FG_MANUAL_ONLY = '1'
            KYTY_FG_FRAMES = '4'
            KYTY_FG_VERIFY = '0'
            KYTY_FG_SAMPLES = '0'
            KYTY_DUMP_IMAGES = '1920x1080,3840x2160'
            KYTY_DUMP_IMAGES_SHADERS = 'c3113b114fb02e5d,edb9cdad0985bf44'
            KYTY_DUMP_IMAGES_MAX = '24'
            KYTY_DUMP_IMAGES_SIZE = '1920'
            KYTY_DUMP_IMAGES_MB = '64'
            KYTY_DUMP_IMAGES_RAW = '1'
            KYTY_DUMP_IMAGES_RAW_MB = '512'
        }
        foreach ($entry in $captureSettings.GetEnumerator()) { $settings[$entry.Key] = $entry.Value }
    }
    if ($CaptureGeneratedFrames) {
        # Bounded output pairs have their own metadata; no full API trace needed.
        $settings['KYTY_GT7_FSR_FG_CAPTURE_OUTPUTS'] = '1'
        $settings['KYTY_FG_MANUAL_ONLY'] = '1'
    }
    $manifest = [ordered]@{
        schema = 1; kind = 'gt7_dlss_experimental_playtest'; status = 'prepared'; exit_code = $null
        mode = $(if ($GraphicsCheckOnly) { 'graphics_check_only' } else { 'playtest' })
        graphics_check_only = [bool]$GraphicsCheckOnly
        diagnostic_logging_requested = [bool]$DiagnosticLogging
        detailed_trace_enabled = $traceEnabled
        log_direction = $logDirection
        trace_note = 'Ordinary play uses Silent: detailed API traces and DLSS/FG counters are disabled; fatal messages remain in stdout.log. DiagnosticLogging or CaptureTemporalInputs enables the full trace for a short diagnostic session.'
        temporal_input_capture_requested = [bool]$CaptureTemporalInputs
        generated_frame_capture_requested = [bool]$CaptureGeneratedFrames
        generated_frame_capture = $(if ($CaptureGeneratedFrames) {
            [ordered]@{
                trigger = 'F10'; output_directory = 'fg-output-captures'
                requested_pairs_per_batch = 4; maximum_batches_per_process = 4
                matching_inputs = @('HUDless display color PNG', 'private depth R32_FLOAT raw', 'private backward pixel motion RG32_FLOAT raw')
                maximum_readback_bytes_per_batch = 276480000
                phase = 'After successful SDK generation dispatch, before presentation. Images do not prove display.'
                completion_verified = $false
                note = 'Use frame IDs, guest-frame IDs and reset metadata to establish valid comparisons. Do not press F9 in this mode. A batch may briefly pause after collecting its outputs for readback and encoding.'
            }
        } else { $null })
        temporal_input_capture = $(if ($CaptureTemporalInputs) {
            [ordered]@{
                trigger = 'F9'; requested_environment = $captureSettings
                trigger_note = 'Manual-only FrameGraph capture is requested: automatic diagnostics and scheduled captures are ignored, while the four-frame sequence started by F9 continues normally.'
                image_capture_phase = 'Before the bound shader operation. A write binding is not an after-write result.'
                raw_note = 'The verified single-layer 1920x1080 inputs are requested at full resolution without row sampling. Larger inputs may use a smaller mip or sampled rows. Inspect each raw JSON layout and the log to confirm completeness; image and 512 MiB process-wide raw limits can stop dumping.'
                completion_verified = $false
            }
        } else { $null })
        frame_generation_requested = [bool]$FrameGeneration
        occlusion_guard_requested = [bool]$OcclusionGuard
        occlusion_guard_verified = $false
        occlusion_guard_note = 'Experimental local fallback to current real pixels at large current/previous motion discontinuities. No fixed wheel rectangle. Requested state does not prove GPU execution, visual improvement or performance; inspect guard counters and capture metadata. Local temporal seams remain possible.'
        vulkan_validation_requested = [bool]$VulkanValidation
        vulkan_validation_active = $null # Confirm the enabled layer in the runtime log.
        frame_generation_requested_multiplier = $(if ($FrameGeneration) { 2 } else { 1 })
        frame_generation_verified = $false # Requesting FG does not prove interpolation ran.
        frame_generation_runtime_sha256 = $(if ($FrameGeneration) { (Get-FileHash -LiteralPath $frameGenerationRuntime -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null })
        started_utc = [DateTime]::UtcNow.ToString('o'); baseline = [bool]$Baseline
        emulator = $exe; backend = $backend; runtime_directory = $runtime
        emulator_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        backend_sha256 = (Get-FileHash -LiteralPath $backend -Algorithm SHA256).Hash.ToLowerInvariant()
        runtime_sha256 = (Get-FileHash -LiteralPath (Join-Path $runtime 'nvngx_dlss.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
        source_user_data = $source; working_directory = $run; arguments = $arguments; environment = $settings
        progress_state = $progress
        note = $(if ($GraphicsCheckOnly) {
            'Initializes the real emulator window and Vulkan device, then exits before guest ELF loading or execution. No game state copied. This does not validate DLSS evaluation or rendering.'
        } else {
            'Conditional fixed 1920x1080 to 2560x1440 desktop SR, preset K with Quality hint; guest tone-map/UI surface remains 3840x2160, window is 2560x1440. Enable DiagnosticLogging for short verification sessions and inspect DLSS evaluation/composite and FSR FG dispatch counters separately. Requested options are not verification results. Mirror fallback is spatial; exposure/temporal quality/FPS not certified.'
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
        if ($traceEnabled) { Write-Host 'Full diagnostic trace enabled: keep this session short and close normally after the required evidence is collected.' }
        if ($GraphicsCheckOnly) {
            Write-Host 'Checking real window and Vulkan device initialization. The emulator will exit before loading or running the game.'
        } else {
            if ($CaptureGeneratedFrames) {
                Write-Host 'Enter the affected external wheel view with frame generation enabled, then press F10 once while ghosting is visible.'
                Write-Host 'F10 requests four real/generated pairs plus matching HUDless color, depth and motion under fg-output-captures. A short pause after capture is expected. Do not press F9 in this mode.'
            } elseif ($CaptureTemporalInputs) {
                Write-Host 'Enter the same race and external camera view, wait for shader compilation to settle, then press F9 once while the wheel issue is visible.'
                Write-Host 'F9 captures 4 frames of temporal inputs, including full-resolution 1080p depth/motion/color. Capture can briefly pause rendering; drive a few seconds, then close normally.'
            } else {
                Write-Host 'Enter the same race/cockpit view and drive normally. No F9 capture is needed. Close normally when finished.'
            }
            Write-Host 'This is a local experimental build. Unsupported pass combinations retain the original image.'
            if ($FrameGeneration) {
                Write-Host 'Desktop 2x Vulkan FSR frame generation requested alongside DLSS SR. DiagnosticLogging is required to record FG verification counters; unsupported frames use normal presentation.'
            }
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
