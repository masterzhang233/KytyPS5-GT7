[CmdletBinding()]
param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\_Build\deps\fidelityfx')
)

$ErrorActionPreference = 'Stop'
$revision = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55' # FidelityFX SDK v1.1.4 / FSR 3.1.4
$baseUri = "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$revision"
$files = @(
    @{ Source = 'ffx-api/include/ffx_api/ffx_api.h'; Target = 'include/ffx_api/ffx_api.h'; Blob = 'e1361915ac3f554179537d0e2b21f425c7b7e14c' },
    @{ Source = 'ffx-api/include/ffx_api/ffx_api_loader.h'; Target = 'include/ffx_api/ffx_api_loader.h'; Blob = '9df9ac32b813d8ace5c2d506aa8aab8d0f9ff8d2' },
    @{ Source = 'ffx-api/include/ffx_api/ffx_api_types.h'; Target = 'include/ffx_api/ffx_api_types.h'; Blob = '87a9d0bd3364565ea01133515f2f73ebde4cd93c' },
    @{ Source = 'ffx-api/include/ffx_api/ffx_framegeneration.h'; Target = 'include/ffx_api/ffx_framegeneration.h'; Blob = '12f996d5a87371564c6b248f67e8be526a66cc53' },
    @{ Source = 'ffx-api/include/ffx_api/vk/ffx_api_vk.h'; Target = 'include/ffx_api/vk/ffx_api_vk.h'; Blob = '1f56ea6db296516067abeacc541cf455318aae28' },
    @{ Source = 'PrebuiltSignedDLL/amd_fidelityfx_vk.dll'; Target = 'bin/amd_fidelityfx_vk.dll'; Blob = '6efacc57a9ec40aa49067535df402e371730e996' },
    @{ Source = 'LICENSE.txt'; Target = 'LICENSE.txt'; Blob = '3712e34adea415aa00dab08625ce7224b27f1706' }
)

function Get-GitBlobHash([string]$LiteralPath) {
    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    $prefix = [Text.Encoding]::ASCII.GetBytes("blob $($bytes.Length)`0")
    $hash = [Security.Cryptography.SHA1]::Create()
    try {
        [void]$hash.TransformBlock($prefix, 0, $prefix.Length, $prefix, 0)
        [void]$hash.TransformFinalBlock($bytes, 0, $bytes.Length)
        return ([BitConverter]::ToString($hash.Hash)).Replace('-', '').ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }
}

$Destination = [IO.Path]::GetFullPath($Destination)
$manifest = @()
foreach ($file in $files) {
    $target = Join-Path $Destination $file.Target
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
    if (!(Test-Path -LiteralPath $target) -or (Get-GitBlobHash $target) -ne $file.Blob) {
        $temporary = "$target.download"
        Invoke-WebRequest -UseBasicParsing -Uri "$baseUri/$($file.Source)" -OutFile $temporary
        if ((Get-GitBlobHash $temporary) -ne $file.Blob) {
            throw "Pinned Git blob verification failed: $($file.Source)"
        }
        Move-Item -LiteralPath $temporary -Destination $target -Force
    }
    $manifest += [ordered]@{ path = $file.Target; git_blob_sha1 = $file.Blob; sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
}

$runtime = Join-Path $Destination 'bin/amd_fidelityfx_vk.dll'
$signature = Get-AuthenticodeSignature -LiteralPath $runtime
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Advanced Micro Devices') {
    throw "The pinned runtime does not have a valid AMD Authenticode signature: $($signature.Status)"
}
[ordered]@{
    sdk = 'FidelityFX SDK 1.1.4'
    fsr = '3.1.4'
    source = "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/$revision"
    revision = $revision
    runtime_signature = [string]$signature.Status
    runtime_signer = $signature.SignerCertificate.Subject
    files = $manifest
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Destination 'provenance.json') -Encoding UTF8
Write-Output "Verified FidelityFX SDK 1.1.4 headers and AMD Vulkan runtime: $Destination"
