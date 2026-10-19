[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._+-]{0,79}$')]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$DllPath,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$ExpectedDllSha256,

    [string]$QueryPath,
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$stage = $null
$zipPath = $null
$zipCreated = $false
$utf8 = New-Object System.Text.UTF8Encoding $false

function Invoke-GitChecked {
    param([string]$Directory, [string[]]$Arguments)
    $lines = & $script:gitPath -C $Directory @Arguments 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "Git failed (exit $code): git $($Arguments -join ' ')`n$($lines -join [Environment]::NewLine)"
    }
    return ($lines -join [Environment]::NewLine).Trim()
}

function Resolve-File {
    param([string]$Path)
    $resolved = (Resolve-Path -LiteralPath $Path).ProviderPath
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Required file is missing: $Path"
    }
    return $resolved
}

function Write-NewUtf8File {
    param([string]$Path, [string]$Text)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $bytes = $script:utf8.GetBytes($Text)
        $stream.Write($bytes, 0, $bytes.Length)
    }
    finally { $stream.Dispose() }
}

try {
    $workspace = (Resolve-Path -LiteralPath (Split-Path $PSScriptRoot -Parent)).ProviderPath
    $gitPath = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $gitRoot = Invoke-GitChecked $workspace @('rev-parse', '--show-toplevel')
    if (-not [string]::Equals([IO.Path]::GetFullPath($gitRoot).TrimEnd('\', '/'),
            $workspace.TrimEnd('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The script must be inside the FH4VRMod repository root, not an unrelated enclosing repository.'
    }
    $sourceCommit = Invoke-GitChecked $workspace @('rev-parse', '--verify', 'HEAD^{commit}')
    if ($sourceCommit -notmatch '^(?:[0-9a-f]{40}|[0-9a-f]{64})$') {
        throw 'Cannot determine the source HEAD commit. Commit the release source before packaging.'
    }
    $openVrLink = Invoke-GitChecked $workspace @('ls-tree', 'HEAD', '--', 'third_party/openvr')
    if ($openVrLink -notmatch '^160000 commit (?<commit>[0-9a-f]{40}|[0-9a-f]{64})\tthird_party/openvr$') {
        throw 'HEAD does not contain an OpenVR gitlink at third_party/openvr.'
    }
    $openVrCommit = $Matches['commit']
    $openVrDirectory = Join-Path $workspace 'third_party\openvr'
    $checkedOutOpenVr = Invoke-GitChecked $openVrDirectory @('rev-parse', '--verify', 'HEAD^{commit}')
    if ($checkedOutOpenVr -ne $openVrCommit) {
        throw 'The OpenVR checkout differs from the source commit gitlink. Initialize/update the pinned submodule first.'
    }
    # Do not label a locally changed dependency as the pinned upstream binary.
    foreach ($relative in @('bin/win64/openvr_api.dll', 'LICENSE')) {
        [void](Invoke-GitChecked $openVrDirectory @('cat-file', '-e', ('HEAD:' + $relative)))
    }
    [void](Invoke-GitChecked $openVrDirectory @('diff', '--exit-code', '--no-ext-diff', 'HEAD', '--', 'bin/win64/openvr_api.dll', 'LICENSE'))
    $sourceStatus = Invoke-GitChecked $workspace @('status', '--porcelain', '--untracked-files=normal')
    # Only require the source files actually shipped in the ZIP to match HEAD.
    # Other working files may be dirty; the manifest records that fact separately.
    $committedInputs = @('README.md', 'CHANGELOG.md', 'fh4vr.ini.example',
        'release/Prepare-VRResolution.ps1', 'release/Prepare-VRResolution.cmd')
    foreach ($relative in $committedInputs) {
        [void](Invoke-GitChecked $workspace @('cat-file', '-e', ('HEAD:' + $relative)))
    }
    [void](Invoke-GitChecked $workspace (@('diff', '--exit-code', '--no-ext-diff', 'HEAD', '--') + $committedInputs))

    if ([string]::IsNullOrWhiteSpace($QueryPath)) {
        $QueryPath = Join-Path $workspace 'build\stereo-trial\query_runtime_mode.exe'
    }
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
        $OutputDirectory = Join-Path $workspace 'build\releases'
    }
    $dll = Resolve-File $DllPath
    $query = Resolve-File $QueryPath
    $expectedHash = $ExpectedDllSha256.ToLowerInvariant()
    if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
        throw 'The selected DLL does not match ExpectedDllSha256. Nothing has been packaged.'
    }

    # This explicit allowlist is the entire runtime payload. Never copy directories recursively.
    $payload = @(
        [pscustomobject]@{ Source = $dll; Path = 'dxgi.dll' },
        [pscustomobject]@{ Source = (Join-Path $openVrDirectory 'bin\win64\openvr_api.dll'); Path = 'fh4vr/openvr_api.dll' },
        [pscustomobject]@{ Source = $query; Path = 'fh4vr/query_runtime_mode.exe' },
        [pscustomobject]@{ Source = (Join-Path $workspace 'release\Prepare-VRResolution.ps1'); Path = 'Prepare-VRResolution.ps1' },
        [pscustomobject]@{ Source = (Join-Path $workspace 'release\Prepare-VRResolution.cmd'); Path = 'Prepare-VRResolution.cmd' },
        [pscustomobject]@{ Source = (Join-Path $workspace 'README.md'); Path = 'README.md' },
        [pscustomobject]@{ Source = (Join-Path $workspace 'CHANGELOG.md'); Path = 'CHANGELOG.md' },
        [pscustomobject]@{ Source = (Join-Path $workspace 'fh4vr.ini.example'); Path = 'fh4vr.ini.example' },
        [pscustomobject]@{ Source = (Join-Path $openVrDirectory 'LICENSE'); Path = 'licenses/OpenVR-LICENSE.txt' }
    )
    foreach ($item in $payload) {
        $item.Source = Resolve-File $item.Source
        $item | Add-Member -NotePropertyName Sha256 -NotePropertyValue ((Get-FileHash -LiteralPath $item.Source -Algorithm SHA256).Hash.ToLowerInvariant())
    }
    if ($payload[0].Sha256 -ne $expectedHash) { throw 'The DLL changed while preparing the package.' }

    $output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
    if (Test-Path -LiteralPath $output) {
        if (-not (Test-Path -LiteralPath $output -PathType Container)) { throw 'OutputDirectory must be a folder.' }
    }
    else { [void](New-Item -ItemType Directory -Path $output) }
    $zipName = "FH4VRMod-$Version-win-x64.zip"
    $zipPath = Join-Path $output $zipName
    $checksumPath = $zipPath + '.sha256'
    if ((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath $checksumPath)) {
        throw 'The release ZIP or its .sha256 file already exists. Refusing to overwrite an existing release.'
    }
    $stage = Join-Path $output ('.staging-FH4VRMod-' + $Version + '-' + [Guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $stage)
    $files = @()
    foreach ($item in $payload) {
        $destination = Join-Path $stage $item.Path
        $parent = Split-Path $destination -Parent
        if (-not (Test-Path -LiteralPath $parent)) { [void](New-Item -ItemType Directory -Path $parent) }
        [IO.File]::Copy($item.Source, $destination, $false)
        $copiedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($copiedHash -ne $item.Sha256) { throw "File changed during packaging: $($item.Path)" }
        $files += [pscustomobject][ordered]@{
            path = $item.Path
            size = (Get-Item -LiteralPath $destination).Length
            sha256 = $copiedHash
        }
    }

    $manifest = [ordered]@{
        schema_version = 1
        package = 'FH4VRMod'
        version = $Version
        platform = 'win-x64'
        created_utc = [DateTime]::UtcNow.ToString('o')
        source_commit = $sourceCommit
        source_worktree_dirty = -not [string]::IsNullOrWhiteSpace($sourceStatus)
        dll_sha256 = $expectedHash
        openvr_commit = $openVrCommit
        validated_game = [ordered]@{
            name = 'Forza Horizon 4'
            distribution = 'Steam'
            version = '1.478.564.0'
            steam_build_id = 12772551
            executable_sha256 = 'b838a79a65c6d1e12f46deaba3584e4c88ebccdfdc4214b4535ece4ba3c0abbe'
        }
        files = $files
    }
    Write-NewUtf8File (Join-Path $stage 'manifest.json') (($manifest | ConvertTo-Json -Depth 8) + "`n")
    $archivePaths = @($payload | ForEach-Object { $_.Path }) + @('manifest.json')
    $sumLines = foreach ($relative in ($archivePaths | Sort-Object)) {
        $hash = (Get-FileHash -LiteralPath (Join-Path $stage $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
    # SHA256SUMS covers every other entry, including manifest.json; it cannot hash itself.
    Write-NewUtf8File (Join-Path $stage 'SHA256SUMS.txt') (($sumLines -join "`n") + "`n")
    $archivePaths += 'SHA256SUMS.txt'

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # CreateNew handles races as well as the existence check above: no existing ZIP can be replaced.
    $zipStream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $zipCreated = $true
    $archive = $null
    try {
        $archive = New-Object System.IO.Compression.ZipArchive($zipStream, [IO.Compression.ZipArchiveMode]::Create, $true)
        foreach ($relative in ($archivePaths | Sort-Object)) {
            $entry = $archive.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
            $entryStream = $null
            $inputStream = $null
            try {
                $entryStream = $entry.Open()
                $inputStream = [IO.File]::OpenRead((Join-Path $stage $relative))
                $inputStream.CopyTo($entryStream)
            }
            finally {
                if ($inputStream) { $inputStream.Dispose() }
                if ($entryStream) { $entryStream.Dispose() }
            }
        }
    }
    finally {
        if ($archive) { $archive.Dispose() }
        $zipStream.Dispose()
    }
    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-NewUtf8File $checksumPath ("$zipHash  $zipName`n")

    Write-Host "Created: $zipPath"
    Write-Host "Checksum: $checksumPath"
    Write-Host "DLL SHA256: $expectedHash"
    Write-Host "Source commit: $sourceCommit"
    if ($manifest.source_worktree_dirty) { Write-Host 'Manifest records source_worktree_dirty=true; HEAD alone does not describe all current working files.' }
    Write-Host "Staging retained for inspection: $stage"
    exit 0
}
catch {
    Write-Host ("Packaging failed: " + $_.Exception.Message) -ForegroundColor Red
    if ($stage) { Write-Host "Staging was retained: $stage" }
    if ($zipCreated) { Write-Host "A new ZIP may be incomplete and was retained for inspection: $zipPath" }
    exit 1
}
