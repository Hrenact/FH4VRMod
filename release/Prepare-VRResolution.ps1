[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$GameDirectory = $PSScriptRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    if (Get-Process -Name ForzaHorizon4 -ErrorAction SilentlyContinue) {
        throw 'Close Forza Horizon 4 before preparing its resolution list.'
    }

    $directory = (Resolve-Path -LiteralPath $GameDirectory).Path
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw 'GameDirectory must be a folder.'
    }
    foreach ($relativePath in @('ForzaHorizon4.exe', 'fh4vr\query_runtime_mode.exe', 'fh4vr\openvr_api.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $directory $relativePath) -PathType Leaf)) {
            throw "Missing $relativePath. Extract the release beside ForzaHorizon4.exe, preserving the fh4vr subfolder."
        }
    }
    $ini = Join-Path $directory 'fh4vr.ini'
    $query = Join-Path $directory 'fh4vr\query_runtime_mode.exe'
    $api = Join-Path $directory 'fh4vr\openvr_api.dll'

    if (-not ('Fh4VrReleaseProfile' -as [type])) {
        Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Fh4VrReleaseProfile {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint GetPrivateProfileString(string section, string key, string fallback, StringBuilder value, uint size, string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
}
'@
    }

    $buffer = New-Object System.Text.StringBuilder 64
    $readLength = [Fh4VrReleaseProfile]::GetPrivateProfileString('VR', 'RenderScale', '1.0', $buffer, 64, $ini)
    $scaleText = $buffer.ToString()
    $scale = 0.0
    if ($readLength -ge 63 -or -not [double]::TryParse($scaleText, [System.Globalization.NumberStyles]::Float,
            [System.Globalization.CultureInfo]::InvariantCulture, [ref]$scale) -or
            [double]::IsNaN($scale) -or [double]::IsInfinity($scale) -or $scale -lt 0.25 -or $scale -gt 2.0) {
        throw 'Invalid [VR] RenderScale in fh4vr.ini. Use a decimal number from 0.25 to 2.0, for example 1.0.'
    }

    Write-Host 'Reading the connected headset and current SteamVR render settings...'
    $result = & $query $api $scale.ToString('R', [System.Globalization.CultureInfo]::InvariantCulture)
    $queryExitCode = $LASTEXITCODE
    if ($queryExitCode -ne 0) {
        $detail = ($result | Out-String).Trim()
        throw "Headset query failed (exit $queryExitCode). Connect and wake the headset, start SteamVR, then retry. Query output: $detail"
    }
    $plan = ($result -join [Environment]::NewLine) | ConvertFrom-Json
    $width = 0
    $height = 0
    if (-not [int]::TryParse([string]$plan.width, [ref]$width) -or
            -not [int]::TryParse([string]$plan.height, [ref]$height) -or
            $width -lt 256 -or $width -gt 4096 -or $height -lt 256 -or $height -gt 8192 -or
            $width % 4 -ne 0 -or $height % 4 -ne 0) {
        throw 'The runtime target is invalid or exceeds this build''s texture limits. Lower [VR] RenderScale in fh4vr.ini, then retry.'
    }

    # The query can take time. Refuse to write if the game was started meanwhile.
    if (Get-Process -Name ForzaHorizon4 -ErrorAction SilentlyContinue) {
        throw 'Forza Horizon 4 started during the query. Close it and retry; configuration was not changed.'
    }

    if (Test-Path -LiteralPath $ini) {
        $backup = $ini + '.backup-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')
        Copy-Item -LiteralPath $ini -Destination $backup -ErrorAction Stop
        Write-Host "Saved existing configuration: $backup"
    }

    # Disable first: an interrupted update must never enable half-updated dimensions.
    foreach ($entry in @(@('AddRuntimeMode', '0'), @('RuntimeWidth', [string]$width),
            @('RuntimeHeight', [string]$height), @('AddRuntimeMode', '1'))) {
        if (-not [Fh4VrReleaseProfile]::WritePrivateProfileString('Diagnostic', $entry[0], $entry[1], $ini)) {
            $writeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "Could not save $($entry[0]) in fh4vr.ini (Windows error $writeError). Check folder write access and retry."
        }
    }

    Write-Host "Prepared resolution: $width x $height (RenderScale $scaleText)." -ForegroundColor Green
    Write-Host 'Start FH4, use windowed mode, then select and apply this resolution in Video settings.'
    Write-Host 'This script adds an option; it does not change the game resolution automatically.'
    Write-Host 'Run it again with FH4 closed after changing the headset or SteamVR render resolution.'
    exit 0
}
catch {
    Write-Host ("Preparation failed: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
