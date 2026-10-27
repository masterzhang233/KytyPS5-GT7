[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$ReleaseTag
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$source = (Resolve-Path -LiteralPath $InstallDirectory).ProviderPath
$output = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($output)
$name = $ReleaseTag + '-Windows-x64'
$stage = Join-Path $output $name
$archive = Join-Path $output ($name + '.zip')
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $archive)) {
    throw 'Release stage or archive already exists. Use a new output directory or release tag.'
}

$required = @('launcher.exe','kyty_emulator.exe','Qt6Core.dll','Qt6Gui.dll',
    'Qt6Network.dll','Qt6Widgets.dll','libwinpthread-1.dll','msvcp140.dll',
    'vcruntime140.dll','patches\GT7\patches.json','scripts\GT7-Start.ps1',
    'scripts\GT7-LaunchSupport.ps1','docs\GT7-VR.md','LICENSE',
    'translations\qt_zh_CN.qm',
    'licenses\qt\Qt-NOTICE.md','licenses\qt\Qt-LGPL-3.0-only.txt',
    'licenses\qt\Qt-GPL-3.0-only.txt','licenses\openvr\LICENSE',
    'licenses\winpthread\COPYING')
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $relative) -PathType Leaf)) {
        throw "Windows installation is incomplete: $relative"
    }
}

[void][IO.Directory]::CreateDirectory($stage)
foreach ($file in @('launcher.exe','kyty_emulator.exe','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $stage
}
Get-ChildItem -LiteralPath $source -Filter '*.dll' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stage
}
foreach ($directory in @('assets','docs','patches','scripts','licenses','generic',
    'iconengines','imageformats','networkinformation','platforms','styles','tls',
    'translations')) {
    $path = Join-Path $source $directory
    if (Test-Path -LiteralPath $path -PathType Container) {
        Copy-Item -LiteralPath $path -Destination $stage -Recurse
    }
}

$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName)
$sums = foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'), [string[]]$sums,
    (New-Object System.Text.UTF8Encoding($false)))
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    tag = $ReleaseTag
    archive = [IO.Path]::GetFileName($archive)
    bytes = (Get-Item -LiteralPath $archive).Length
    sha256 = $digest
    files = $files.Count + 1
    staged_directory = [IO.Path]::GetFileName($stage)
    created_utc = [DateTime]::UtcNow.ToString('o')
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $output 'release-manifest.json') -Encoding UTF8
$manifest | ConvertTo-Json -Depth 3
