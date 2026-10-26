# PowerShell 5.1-compatible process construction. No shell or persistent environment changes.
Set-StrictMode -Version Latest

function ConvertTo-GT7NativeArgument {
    param([AllowEmptyString()][string]$Value)
    # Windows CommandLineToArgvW / CRT quoting, including embedded quotes and trailing slashes.
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * ($slashes * 2 + 1)))
            [void]$builder.Append('"')
        } else {
            [void]$builder.Append(('\' * $slashes))
            [void]$builder.Append($character)
        }
        $slashes = 0
    }
    [void]$builder.Append(('\' * ($slashes * 2)))
    [void]$builder.Append('"')
    return $builder.ToString()
}

function New-GT7ProcessStartInfo {
    param(
        [Parameter(Mandatory=$true)][string]$Executable,
        [Parameter(Mandatory=$true)][AllowEmptyCollection()][AllowEmptyString()][string[]]$ArgumentList,
        [Parameter(Mandatory=$true)][string]$WorkingDirectory,
        [switch]$CaptureOutput
    )
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = (($ArgumentList | ForEach-Object { ConvertTo-GT7NativeArgument $_ }) -join ' ')
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = [bool]$CaptureOutput
    $info.RedirectStandardOutput = [bool]$CaptureOutput
    $info.RedirectStandardError = [bool]$CaptureOutput
    return $info
}

function Invoke-GT7CompatibilityCheck {
    param(
        [Parameter(Mandatory=$true)][string]$Executable,
        [Parameter(Mandatory=$true)][string[]]$ArgumentList,
        [Parameter(Mandatory=$true)][string]$WorkingDirectory
    )
    $process = New-Object System.Diagnostics.Process
    try {
        $process.StartInfo = New-GT7ProcessStartInfo -Executable $Executable `
            -ArgumentList ($ArgumentList + '--gt7-check-only') `
            -WorkingDirectory $WorkingDirectory -CaptureOutput
        if (-not $process.Start()) { throw 'Could not start the compatibility checker.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'GT7 compatibility check exceeded 30 seconds; the game was not started.'
        }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StandardOutput = $stdout.GetAwaiter().GetResult()
            StandardError = $stderr.GetAwaiter().GetResult()
        }
    } finally { $process.Dispose() }
}
