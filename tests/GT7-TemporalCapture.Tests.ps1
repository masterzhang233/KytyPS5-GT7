param([Parameter(Mandatory=$true)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$script = Join-Path $repo 'scripts\GT7-Capture-TemporalInputs.ps1'
. (Join-Path $repo 'scripts\GT7-LaunchSupport.ps1')
$testRoot = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ([Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($testRoot)
$tokens = $null; $errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($script, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$fixture = Join-Path $testRoot 'process fixture.exe'
$fixtureSource = @'
using System;
using System.IO;
using System.Text;
public class TemporalCaptureFixture {
  public static int Main(string[] args) {
    if (Array.IndexOf(args, "--gt7-check-only") >= 0) {
      Console.WriteLine("fixture preflight, no game");
      return Environment.GetEnvironmentVariable("CAPTURE_TEST_REJECT") == "1" ? 7 : 0;
    }
    using (var f = new StreamWriter("fixture.txt", false, Encoding.UTF8)) {
      foreach (var arg in args) f.WriteLine("arg=" + Convert.ToBase64String(Encoding.UTF8.GetBytes(arg)));
      foreach (var key in new [] {"KYTY_FG_FRAMES", "KYTY_DUMP_IMAGES_RAW", "KYTY_DUMP_IMAGES_RAW_MB",
          "KYTY_DUMP_IMAGES_SHADERS", "KYTY_DUMP_IMAGES_SIZE", "KYTY_DUMP_IMAGES_MB",
          "KYTY_DUMP_IMAGES_TEMPORAL_CANDIDATES",
          "KYTY_FG_DUMP_SHADERS", "KYTY_FG_DUMP_BYTES", "KYTY_BISECT", "GT7_TEST_INHERITED"})
        f.WriteLine(key + "=" + Environment.GetEnvironmentVariable(key));
    }
    Console.WriteLine("stdout copied to disk");
    Console.Error.WriteLine("stderr copied to disk");
    return 0;
  }
}
'@
Add-Type -TypeDefinition $fixtureSource -OutputAssembly $fixture -OutputType ConsoleApplication
$game = Join-Path $testRoot 'game with spaces'
[void][IO.Directory]::CreateDirectory($game)
[IO.File]::WriteAllText((Join-Path $game 'eboot.bin'), 'Not a game; capture process test.')
$state = Join-Path $testRoot 'source state'
$save = Join-Path $state '_SaveData\PPSA01317'
[void][IO.Directory]::CreateDirectory($save)
[IO.File]::WriteAllText((Join-Path $save 'save.dat'), 'preserve original')
$cache = Join-Path $state '_ShaderCache'
[void][IO.Directory]::CreateDirectory($cache)
[IO.File]::WriteAllText((Join-Path $cache 'fixture.bin'), 'private shader cache copy')
[void][IO.Directory]::CreateDirectory((Join-Path $state '_Textures'))
[IO.File]::WriteAllText((Join-Path $state '_Textures\not-copied.bin'), 'unrelated cache')
$before = (Get-FileHash -LiteralPath (Join-Path $save 'save.dat')).Hash
$output = Join-Path $testRoot 'captures'
$ps51 = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
function Invoke-CaptureFixture([string[]]$Extra, [bool]$Reject = $false) {
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$script,
        '-GamePath',$game,'-EmulatorPath',$fixture,'-SourceUserDataRoot',$state,'-OutputRoot',$output) + $Extra
    $process = New-Object Diagnostics.Process
    try {
        $process.StartInfo = New-GT7ProcessStartInfo -Executable $ps51 -ArgumentList $arguments `
            -WorkingDirectory $testRoot -CaptureOutput
        $process.StartInfo.EnvironmentVariables['KYTY_BISECT'] = 'unwanted inherited flag'
        $process.StartInfo.EnvironmentVariables['GT7_TEST_INHERITED'] = 'unwanted'
        $process.StartInfo.EnvironmentVariables['CAPTURE_TEST_REJECT'] = $(if ($Reject) { '1' } else { '0' })
        [void]$process.Start()
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill(); $process.WaitForExit(); throw 'Fixture timed out.'
        }
        return [pscustomobject]@{ Exit = $process.ExitCode; Out = $outTask.GetAwaiter().GetResult(); Err = $errTask.GetAwaiter().GetResult() }
    } finally { $process.Dispose() }
}
$result = Invoke-CaptureFixture @('-CheckOnly')
if ($result.Exit -ne 0 -or (Test-Path -LiteralPath $output)) { throw "CheckOnly mutated output or failed: $($result.Err)" }
$result = Invoke-CaptureFixture @('-CheckOnly') $true
if ($result.Exit -eq 0 -or (Test-Path -LiteralPath $output)) { throw 'A rejected preflight was not stopped.' }
$result = Invoke-CaptureFixture @('-Frames','3','-RawBudgetMB','64','-ShaderHashes','0123456789abcdef','-UserId','2000',
    '-ImageMaxSide','4096','-ImageReadbackMB','128','-ShaderMemoryHashes','abcdef0123456789','-ReuseShaderCache','-DiscoverTemporalCandidates')
if ($result.Exit -ne 0) { throw "Capture fixture failed: $($result.Out) $($result.Err)" }
$runs = @(Get-ChildItem -LiteralPath $output -Directory)
if ($runs.Count -ne 1) { throw 'Expected exactly one capture folder.' }
$run = $runs[0].FullName
$manifest = Get-Content -LiteralPath (Join-Path $run 'capture-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.status -ne 'finished' -or $manifest.exit_code -ne 0 -or $manifest.dlss_enabled) { throw 'Incorrect manifest result.' }
$record = @(Get-Content -LiteralPath (Join-Path $run 'fixture.txt'))
foreach ($line in @('KYTY_FG_FRAMES=3','KYTY_DUMP_IMAGES_RAW=1','KYTY_DUMP_IMAGES_RAW_MB=64',
    'KYTY_DUMP_IMAGES_SHADERS=0123456789abcdef','KYTY_DUMP_IMAGES_SIZE=4096','KYTY_DUMP_IMAGES_MB=128',
    'KYTY_DUMP_IMAGES_TEMPORAL_CANDIDATES=1',
    'KYTY_FG_DUMP_SHADERS=abcdef0123456789','KYTY_FG_DUMP_BYTES=4096','KYTY_BISECT=','GT7_TEST_INHERITED=')) {
    if ($record -notcontains $line) { throw "Incorrect child environment: $line" }
}
$received = @($record | Where-Object { $_.StartsWith('arg=') } | ForEach-Object {
    [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_.Substring(4)))
})
if ($received -notcontains (Join-Path $game 'eboot.bin')) { throw 'Game path quoting changed.' }
$idIndex = [Array]::IndexOf($received, '--user-id')
if ($idIndex -lt 0 -or $received[$idIndex + 1] -ne '2000' -or $manifest.user_id -ne 2000) {
    throw 'Selected save user ID was not forwarded.'
}
if (-not $manifest.shader_cache_copied -or
    (Get-FileHash -LiteralPath (Join-Path $run '_ShaderCache\fixture.bin')).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $cache 'fixture.bin')).Hash -or
    (Test-Path -LiteralPath (Join-Path $run '_Textures'))) { throw 'Optional cache copy was not isolated or scoped.' }
if ($manifest.image_capture_phase -notmatch 'Before the bound shader') { throw 'Snapshot timing must be explicit.' }
if (-not $manifest.temporal_candidate_discovery) { throw 'Candidate discovery must be explicit.' }
if ((Get-Content -LiteralPath (Join-Path $run 'stdout.log') -Raw) -notmatch 'stdout copied' -or
    (Get-Content -LiteralPath (Join-Path $run 'stderr.log') -Raw) -notmatch 'stderr copied') { throw 'Process output was lost.' }
if ((Get-FileHash -LiteralPath (Join-Path $save 'save.dat')).Hash -ne $before -or
    (Get-FileHash -LiteralPath (Join-Path $run '_SaveData\PPSA01317\save.dat')).Hash -ne $before) {
    throw 'Save isolation failed.'
}
Write-Output 'PASS: parser, read-only preflight, preflight rejection, argument quoting, child environment, output streaming, private save copy and honest manifest.'
