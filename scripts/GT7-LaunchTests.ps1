[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$testOutput = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($testOutput)
. (Join-Path $PSScriptRoot 'GT7-LaunchSupport.ps1')

$checks = New-Object 'System.Collections.Generic.List[string]'
foreach ($file in @('GT7-LaunchSupport.ps1','GT7-Start.ps1','GT7-LaunchTests.ps1')) {
    $tokens = $null; $errors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot $file), [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
    $checks.Add('parser: ' + $file)
}

# A tiny argument recorder, not the emulator; no game is started during these tests.
$probePath = Join-Path $testOutput ('argument-probe-' + [Guid]::NewGuid().ToString('N') + '.exe')
$probeSource = @'
using System;
using System.Text;
public class ArgumentProbe {
    public static int Main(string[] args) {
        foreach (string arg in args) Console.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(arg)));
        if (Environment.GetEnvironmentVariable("GT7_WRAPPER_TEST_REJECT") == "1") return 7;
        if (Array.IndexOf(args, "--reject-test") >= 0) return 7;
        return 0;
    }
}
'@
Add-Type -TypeDefinition $probeSource -OutputAssembly $probePath -OutputType ConsoleApplication
$edgeArguments = @('', 'normal', 'space here', 'percent%PATH%&echo', 'trailing\', 'with"quote',
    'slash\"quote', ([char]0x4E2D).ToString() + ' unicode')
$probe = New-Object System.Diagnostics.Process
try {
    $probe.StartInfo = New-GT7ProcessStartInfo -Executable $probePath -ArgumentList $edgeArguments `
        -WorkingDirectory $testOutput -CaptureOutput
    [void]$probe.Start()
    $text = $probe.StandardOutput.ReadToEnd()
    $probe.WaitForExit()
    if ($probe.ExitCode -ne 0) { throw 'Argument probe failed.' }
    $lines = $text -split '\r?\n'
    for ($index = 0; $index -lt $edgeArguments.Length; $index++) {
        $decoded = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($lines[$index]))
        if ($decoded -cne $edgeArguments[$index]) { throw ('Argument changed at index ' + $index) }
    }
    $checks.Add('native argument round-trip: empty, spaces, quotes, trailing backslash, percent, Unicode')
} finally { $probe.Dispose() }
$rejected = Invoke-GT7CompatibilityCheck -Executable $probePath -ArgumentList @('--reject-test') `
    -WorkingDirectory $testOutput
if ($rejected.ExitCode -ne 7) { throw 'Checker did not preserve a rejection exit code.' }
$checks.Add('checker preserves nonzero rejection')

$fakeGame = Join-Path $testOutput 'fake game %PATH%'
[void][IO.Directory]::CreateDirectory($fakeGame)
[IO.File]::WriteAllText((Join-Path $fakeGame 'eboot.bin'), 'Not a game. Process wrapper test only.')
$userData = Join-Path $testOutput ('must-not-create-' + [Guid]::NewGuid().ToString('N'))
$ps51 = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$startScript = Join-Path $PSScriptRoot 'GT7-Start.ps1'
foreach ($profile in @(@('-DesktopHeight','1440'), @('-Mode','VR','-EyeScale','110'))) {
    $args = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$startScript,
        '-GamePath',$fakeGame,'-EmulatorPath',$probePath,'-UserDataRoot',$userData,'-CheckOnly') + $profile
    $child = New-Object System.Diagnostics.Process
    try {
        $child.StartInfo = New-GT7ProcessStartInfo -Executable $ps51 -ArgumentList $args `
            -WorkingDirectory $testOutput -CaptureOutput
        [void]$child.Start()
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        $child.WaitForExit()
        if ($child.ExitCode -ne 0) { throw ($stderr.GetAwaiter().GetResult()) }
        if ($stdout.GetAwaiter().GetResult() -notmatch 'CheckOnly passed:') { throw 'CheckOnly result missing.' }
        if (Test-Path -LiteralPath $userData) { throw 'CheckOnly created user data.' }
        $checks.Add('PS5.1 CheckOnly wrapper: ' + ($profile -join ' '))
    } finally { $child.Dispose() }
}
$negativeProfiles = @(
    @{ Name='desktop/VR conflict'; Args=@('-Mode','VR','-DesktopHeight','1440'); RejectCore=$false },
    @{ Name='eye scale without VR'; Args=@('-EyeScale','110'); RejectCore=$false },
    @{ Name='unknown preset'; Args=@('-DesktopHeight','1620'); RejectCore=$false },
    @{ Name='core rejection stops normal launch'; Args=@('-DesktopHeight','1440'); RejectCore=$true }
)
foreach ($case in $negativeProfiles) {
    $args = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$startScript,
        '-GamePath',$fakeGame,'-EmulatorPath',$probePath,'-UserDataRoot',$userData) + $case.Args
    $child = New-Object System.Diagnostics.Process
    try {
        $child.StartInfo = New-GT7ProcessStartInfo -Executable $ps51 -ArgumentList $args `
            -WorkingDirectory $testOutput -CaptureOutput
        if ($case.RejectCore) { $child.StartInfo.EnvironmentVariables['GT7_WRAPPER_TEST_REJECT'] = '1' }
        [void]$child.Start()
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        $child.WaitForExit()
        [void]$stdout.GetAwaiter().GetResult()
        [void]$stderr.GetAwaiter().GetResult()
        if ($child.ExitCode -eq 0) { throw ('Negative case accepted: ' + $case.Name) }
        if (Test-Path -LiteralPath $userData) { throw ('Rejected launch created user data: ' + $case.Name) }
        $checks.Add('negative: ' + $case.Name)
    } finally { $child.Dispose() }
}
$result = [ordered]@{ passed=$true; powershell=$PSVersionTable.PSVersion.ToString();
    game_started=$false; real_eboot_validated=$false; scope='Process wrapper only; core hash tests are separate'; checks=@($checks) }
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $testOutput 'result.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 4
