# Read-only selection and copy-forward of completed GT7 playtest progress.
# Never writes to a seed or a previous run; every launch receives a new copy.
Set-StrictMode -Version Latest

function Assert-GT7StatePathNoReparse {
    param([Parameter(Mandatory=$true)][string]$LiteralPath)
    $item = Get-Item -LiteralPath $LiteralPath -Force -ErrorAction Stop
    while ($null -ne $item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Playtest state path contains a junction or symbolic link: $($item.FullName)"
        }
        $parent = Split-Path -Parent $item.FullName
        if (-not $parent -or $parent -eq $item.FullName) { break }
        $item = Get-Item -LiteralPath $parent -Force -ErrorAction Stop
    }
}

function Assert-GT7StateTreeNoReparse {
    param([Parameter(Mandatory=$true)][string]$LiteralPath)
    Assert-GT7StatePathNoReparse -LiteralPath $LiteralPath
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($LiteralPath)
    while ($pending.Count) {
        foreach ($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force -ErrorAction Stop)) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Playtest state contains a junction or symbolic link: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
}

function Get-GT7CompletedProgressCandidate {
    param([Parameter(Mandatory=$true)][string]$Root)
    try {
        Assert-GT7StatePathNoReparse -LiteralPath $Root
        $manifestPath = Join-Path $Root 'playtest-manifest.json'
        Assert-GT7StatePathNoReparse -LiteralPath $manifestPath
        $manifest = Get-Content -LiteralPath $manifestPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        if ($manifest.kind -ne 'gt7_dlss_experimental_playtest' -or
            $manifest.mode -ne 'playtest' -or $manifest.status -ne 'finished' -or
            $null -eq $manifest.exit_code -or $manifest.exit_code -ne 0) { return $null }
        if ($manifest.PSObject.Properties['graphics_check_only'] -and $manifest.graphics_check_only) { return $null }
        if ($manifest.PSObject.Properties['pid'] -and $manifest.pid) {
            # A reused PID is conservatively excluded too. Never copy a running
            # process's data merely because a stale manifest says "finished".
            $active = Get-Process -Id ([int]$manifest.pid) -ErrorAction SilentlyContinue
            if ($null -ne $active) { return $null }
        }
        # PowerShell 7 may deserialize ISO timestamps into DateTime already;
        # converting that to a string would discard its UTC kind and fractions.
        if ($manifest.finished_utc -is [DateTime]) {
            $finished = $manifest.finished_utc.ToUniversalTime()
        } elseif ($manifest.finished_utc -is [DateTimeOffset]) {
            $finished = $manifest.finished_utc.UtcDateTime
        } else {
            $finished = [DateTimeOffset]::Parse([string]$manifest.finished_utc, [Globalization.CultureInfo]::InvariantCulture).UtcDateTime
        }
        $save = Join-Path $Root '_DownloadData\PPSA01317\gt7save.bin'
        Assert-GT7StatePathNoReparse -LiteralPath $save
        $file = Get-Item -LiteralPath $save -Force -ErrorAction Stop
        if ($file.PSIsContainer -or $file.Length -le 0) { return $null }
        foreach ($relative in @('_SaveData\PPSA01317', '_Trophies\PPSA01317', '_DownloadData\PPSA01317')) {
            $directory = Join-Path $Root $relative
            if (Test-Path -LiteralPath $directory) {
                if (-not (Test-Path -LiteralPath $directory -PathType Container)) { return $null }
                Assert-GT7StateTreeNoReparse -LiteralPath $directory
            }
        }
        return [pscustomobject]@{
            root = [IO.Path]::GetFullPath($Root); save_path = $file.FullName
            save_last_write_utc = $file.LastWriteTimeUtc; finished_utc = $finished
            save_bytes = $file.Length
        }
    } catch {
        # Missing, malformed, linked or inaccessible runs are not safe candidates.
        return $null
    }
}

function Select-GT7PlaytestState {
    param(
        [Parameter(Mandatory=$true)][string]$SourceUserDataRoot,
        [Parameter(Mandatory=$true)][string]$OutputRoot,
        [string[]]$ProgressSearchRoots
    )
    $seed = (Resolve-Path -LiteralPath $SourceUserDataRoot -ErrorAction Stop).ProviderPath
    Assert-GT7StatePathNoReparse -LiteralPath $seed
    $search = @($ProgressSearchRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if (-not $search.Count) { $search = @($OutputRoot) }
    $search = @($search | ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique)
    $roots = New-Object 'System.Collections.Generic.List[string]'
    $roots.Add($seed)
    foreach ($directory in $search) {
        if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
        try { Assert-GT7StatePathNoReparse -LiteralPath $directory } catch { continue }
        foreach ($child in @(Get-ChildItem -LiteralPath $directory -Directory -Force -ErrorAction Stop)) {
            if (-not ($child.Attributes -band [IO.FileAttributes]::ReparsePoint)) { $roots.Add($child.FullName) }
        }
    }
    $candidates = @(foreach ($root in @($roots | Select-Object -Unique)) {
        $candidate = Get-GT7CompletedProgressCandidate -Root $root
        if ($null -ne $candidate) { $candidate }
    })
    $selected = $candidates | Sort-Object -Property `
        @{Expression={$_.save_last_write_utc};Descending=$true}, `
        @{Expression={$_.finished_utc};Descending=$true}, `
        @{Expression={$_.root};Descending=$false} | Select-Object -First 1
    $kind = 'completed_playtest'
    if ($null -eq $selected) {
        $kind = 'seed_fallback'
        $seedManifestPath = Join-Path $seed 'playtest-manifest.json'
        if (Test-Path -LiteralPath $seedManifestPath) {
            Assert-GT7StatePathNoReparse -LiteralPath $seedManifestPath
            $seedManifest = Get-Content -LiteralPath $seedManifestPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
            if (($seedManifest.PSObject.Properties['status'] -and $seedManifest.status -eq 'running') -or
                ($seedManifest.PSObject.Properties['pid'] -and $seedManifest.pid -and
                 $null -ne (Get-Process -Id ([int]$seedManifest.pid) -ErrorAction SilentlyContinue))) {
                throw 'The fallback seed belongs to an active playtest; close it normally before copying progress.'
            }
        }
        $save = Join-Path $seed '_DownloadData\PPSA01317\gt7save.bin'
        $file = $null
        if (Test-Path -LiteralPath $save) {
            Assert-GT7StatePathNoReparse -LiteralPath $save
            $file = Get-Item -LiteralPath $save -Force -ErrorAction Stop
            if ($file.PSIsContainer) { throw "GT7 save is a directory: $save" }
        }
        $selected = [pscustomobject]@{
            root = $seed; save_path = $(if ($null -ne $file) { $file.FullName } else { $null })
            save_last_write_utc = $(if ($null -ne $file) { $file.LastWriteTimeUtc } else { $null })
            finished_utc = $null; save_bytes = $(if ($null -ne $file) { $file.Length } else { 0 })
        }
    }
    # CheckOnly also validates the selected trees without creating anything.
    foreach ($relative in @('_SaveData\PPSA01317', '_Trophies\PPSA01317', '_DownloadData\PPSA01317')) {
        $directory = Join-Path $selected.root $relative
        if (Test-Path -LiteralPath $directory) {
            if (-not (Test-Path -LiteralPath $directory -PathType Container)) { throw "State directory is not a directory: $directory" }
            Assert-GT7StateTreeNoReparse -LiteralPath $directory
        }
    }
    return [pscustomobject]@{
        selection = $kind; selected_root = $selected.root; seed_root = $seed
        search_roots = $search; save_path = $selected.save_path
        save_last_write_utc = $(if ($null -ne $selected.save_last_write_utc) { $selected.save_last_write_utc.ToString('o') } else { $null })
        source_finished_utc = $(if ($null -ne $selected.finished_utc) { $selected.finished_utc.ToString('o') } else { $null })
        save_bytes = $selected.save_bytes
        save_sha256 = $(if ($null -ne $selected.save_path) { (Get-FileHash -LiteralPath $selected.save_path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant() } else { $null })
        shader_cache_source = $seed
    }
}

function Copy-GT7PlaytestState {
    param(
        [Parameter(Mandatory=$true)]$Selection,
        [Parameter(Mandatory=$true)][string]$DestinationRoot
    )
    Assert-GT7StatePathNoReparse -LiteralPath $DestinationRoot
    if (@(Get-ChildItem -LiteralPath $DestinationRoot -Force).Count) { throw "Playtest destination is not empty: $DestinationRoot" }
    if ($Selection.selection -eq 'completed_playtest' -and
        $null -eq (Get-GT7CompletedProgressCandidate -Root $Selection.selected_root)) {
        throw 'Selected progress is no longer an inactive successfully completed playtest.'
    }
    $guard = $null
    try {
        if ($null -ne $Selection.save_path) {
            Assert-GT7StatePathNoReparse -LiteralPath $Selection.save_path
            # Prevent writes/renames of the chosen save throughout its copy.
            $guard = [IO.File]::Open($Selection.save_path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            if ((Get-FileHash -LiteralPath $Selection.save_path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Selection.save_sha256) {
                throw 'Selected progress changed after selection; refusing an inconsistent copy.'
            }
        }
        foreach ($relative in @('_SaveData\PPSA01317', '_Trophies\PPSA01317', '_DownloadData\PPSA01317', '_ShaderCache')) {
            $fromRoot = $(if ($relative -eq '_ShaderCache') { $Selection.seed_root } else { $Selection.selected_root })
            $from = Join-Path $fromRoot $relative
            if (-not (Test-Path -LiteralPath $from -PathType Container)) { continue }
            Assert-GT7StateTreeNoReparse -LiteralPath $from
            $to = Join-Path $DestinationRoot $relative
            if (Test-Path -LiteralPath $to) { throw "Refusing to overwrite existing state: $to" }
            [void][IO.Directory]::CreateDirectory((Split-Path -Parent $to))
            Copy-Item -LiteralPath $from -Destination $to -Recurse -Force -ErrorAction Stop
        }
        if ($null -ne $Selection.save_path) {
            $copied = Join-Path $DestinationRoot '_DownloadData\PPSA01317\gt7save.bin'
            if ((Get-FileHash -LiteralPath $copied -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Selection.save_sha256) {
                throw 'Copied GT7 save does not match the selected progress.'
            }
        }
    } finally {
        if ($null -ne $guard) { $guard.Dispose() }
    }
}
