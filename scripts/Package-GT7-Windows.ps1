[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^v?\d+\.\d+\.\d+$')][string]$ReleaseTag
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$source = (Resolve-Path -LiteralPath $InstallDirectory).ProviderPath
$output = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($output)
$version = $ReleaseTag -replace '^v', ''
$name = 'Tohi-GT7-' + $version + '-Windows-x64'
$stage = Join-Path $output $name
$archive = Join-Path $output ($name + '.zip')
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $archive)) {
    throw 'Release stage or archive already exists. Use a new output directory or release tag.'
}

$required = @('launcher.exe','kyty_emulator.exe','Start-GT7-Desktop.ps1','Start-GT7-Desktop.cmd','Qt6Core.dll','Qt6Gui.dll',
    'Qt6Network.dll','Qt6Svg.dll','Qt6Widgets.dll','libwinpthread-1.dll','msvcp140.dll',
    'vcruntime140.dll','vcruntime140_1.dll','msvcp140_1.dll','msvcp140_2.dll',
    'msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','icuuc.dll',
    'platforms\qwindows.dll','patches\GT7\patches.json',
    'kyty_dlss_backend.dll','nvngx_dlss.dll','amd_fidelityfx_vk.dll',
    'docs\GT7.md','docs\GT7-FSR-FRAME-GENERATION.md',("docs\RELEASE-" + $version + '.zh-CN.md'),'LICENSE',
    'translations\qt_zh_CN.qm',
    'licenses\Kyty-MIT.txt','licenses\nvidia\LICENSE.txt',
    'licenses\nvidia\NVIDIA-DLSS-NOTICE.md',
    'licenses\fidelityfx\LICENSE.txt','licenses\fidelityfx\provenance.json',
    'licenses\ffmpeg\copyright','licenses\ffmpeg\SOURCE.txt','licenses\opus\COPYING',
    'licenses\qt\Qt-NOTICE.md','licenses\qt\Qt-LGPL-3.0-only.txt',
    'licenses\qt\Qt-GPL-3.0-only.txt','licenses\openvr\LICENSE',
    'fonts\NotoSansCJKsc-Regular.otf','licenses\fonts\noto-cjk\OFL.txt',
    'licenses\fonts\noto-cjk\NOTICE.md','licenses\fonts\noto-cjk\provenance.json',
    'licenses\winpthread\COPYING')
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $relative) -PathType Leaf)) {
        throw "Windows installation is incomplete: $relative"
    }
}

# This is the tested full desktop preset, not a generic source-build artifact.
# Pin the unmodified vendor runtimes so a stale or partial install cannot silently
# become the default-enabled DLSS + frame-generation release.
$vendorHashes = [ordered]@{
    'nvngx_dlss.dll' = 'd00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e'
    'amd_fidelityfx_vk.dll' = 'a1624cc4238fef046f30c4d80ce3f47be63fc5f5373f49e3ee9edb9960f54c78'
}
foreach ($relative in $vendorHashes.Keys) {
    $actualHash = (Get-FileHash -LiteralPath (Join-Path $source $relative) -Algorithm SHA256).Hash
    if ($actualHash -ine $vendorHashes[$relative]) {
        throw "Unvalidated vendor runtime in Windows installation: $relative"
    }
}
$dlssVersion = (Get-Item -LiteralPath (Join-Path $source 'nvngx_dlss.dll')).VersionInfo.FileVersion

# Only the pinned, unmodified OFL font is distributable through this package.
# Never enumerate/copy a user's fonts folder or the Windows Fonts directory.
$fontHashes = [ordered]@{
    'fonts\NotoSansCJKsc-Regular.otf' = '2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b'
    'licenses\fonts\noto-cjk\OFL.txt' = '6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2'
}
foreach ($relative in $fontHashes.Keys) {
    $actualHash = (Get-FileHash -LiteralPath (Join-Path $source $relative) -Algorithm SHA256).Hash
    if ($actualHash -ine $fontHashes[$relative]) {
        throw "Unvalidated bundled font or license: $relative"
    }
}

[void][IO.Directory]::CreateDirectory($stage)
foreach ($file in @('launcher.exe','kyty_emulator.exe','Start-GT7-Desktop.ps1','Start-GT7-Desktop.cmd','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $stage
}
[void][IO.Directory]::CreateDirectory((Join-Path $stage 'fonts'))
Copy-Item -LiteralPath (Join-Path $source 'fonts\NotoSansCJKsc-Regular.otf') -Destination (Join-Path $stage 'fonts')
Get-ChildItem -LiteralPath $source -Filter '*.dll' -File | ForEach-Object {
    if ($_.Name -notmatch '^(Qt6[A-Za-z0-9_]+|icu[a-z0-9_]*|concrt140|msvcp140(?:_[a-z0-9_]+)?|vcruntime140(?:_[a-z0-9_]+)?|vccorlib140|libwinpthread-1|kyty_dlss_backend|nvngx_dlss|amd_fidelityfx_vk)\.dll$') {
        throw "Unexpected DLL in release installation: $($_.Name)"
    }
    Copy-Item -LiteralPath $_.FullName -Destination $stage
}
# launcher.exe and its desktop shortcut script are the release entry points. Capture/playtest/legacy VR scripts
# remain available in the source repository, never in the player package.
foreach ($directory in @('assets','docs','patches','licenses','generic',
    'iconengines','imageformats','networkinformation','platforms','styles','tls',
    'translations')) {
    $path = Join-Path $source $directory
    if (Test-Path -LiteralPath $path -PathType Container) {
        $linked = @(Get-Item -LiteralPath $path) + @(Get-ChildItem -LiteralPath $path -Recurse -Force)
        foreach ($item in $linked) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Release content must not contain directory links: $($item.FullName)"
            }
        }
        Copy-Item -LiteralPath $path -Destination $stage -Recurse
    }
}
foreach ($document in @('README.md','CHANGELOG.md','RELEASE-NOTES.md')) {
    $path = Join-Path $source $document
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Copy-Item -LiteralPath $path -Destination $stage
    }
}

$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName)
$sums = foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
    if ($file.Extension -in @('.ttf','.otf','.ttc','.otc','.woff','.woff2','.eot','.fon','.fnt') -and
        $relative -ine 'fonts/NotoSansCJKsc-Regular.otf') {
        throw "Unexpected font in release package: $relative"
    }
    if ($relative -match '(^|/)(eboot\.bin|gt7save\.bin|_SaveData|_DownloadData|_Trophies|caches?|shader[-_]cache|_PipelineCache|captures?|logs?|outputs)(/|$)' -or
        $file.Extension -in @('.pdb','.map','.log','.dmp','.gpudmp','.bin','.raw','.rgba16f','.ini')) {
        throw "Game, user state or diagnostic content found in release: $relative"
    }
    if ($file.Extension -in @('.md','.txt','.json','.ps1','.xml')) {
        $content = [IO.File]::ReadAllText($file.FullName)
        if ($content -match '(?i)[a-z]:[\\/](Users|Documents and Settings)[\\/]') {
            throw "Builder-specific absolute path found in release: $relative"
        }
    }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'), [string[]]$sums,
    (New-Object System.Text.UTF8Encoding($false)))
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive + '.sha256'), ($digest + '  ' + [IO.Path]::GetFileName($archive) + "`n"),
    (New-Object System.Text.UTF8Encoding($false)))
$manifest = [ordered]@{
    product = 'Tohi-GT7'
    version = $version
    tag = $ReleaseTag
    archive = [IO.Path]::GetFileName($archive)
    bytes = (Get-Item -LiteralPath $archive).Length
    sha256 = $digest
    files = $files.Count + 1
    staged_directory = [IO.Path]::GetFileName($stage)
    desktop_preset = '1920x1080 -> 2560x1440 DLSS Quality/K + Vulkan FSR 3.1.4 2x + occlusion guard'
    dlss_runtime_file_version = $dlssVersion
    vendor_runtime_sha256 = $vendorHashes
    font_asset_sha256 = $fontHashes
    created_utc = [DateTime]::UtcNow.ToString('o')
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $output 'release-manifest.json') -Encoding UTF8
$manifest | ConvertTo-Json -Depth 3
