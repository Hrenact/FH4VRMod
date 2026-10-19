param(
    [ValidateSet('Install','Remove')][string]$Action,
    [ValidateSet('ForwardOnly','CameraProbe','VtableProbe','YawTest','HeadTrack','GraphicsProbe','SwapchainWrapper','FrameCapture','GpuCopy','LiveOverlay','EyeOffset','EyePosition','AerProbe','AerDeferred','StereoTrial')][string]$Variant='ForwardOnly'
)
$ErrorActionPreference = 'Stop'
if (!$Action) { throw 'Specify -Action Install or -Action Remove' }
if (Get-Process ForzaHorizon4 -ErrorAction SilentlyContinue) { throw 'Close FH4 before changing the proxy DLL.' }
$workspace = Split-Path $PSScriptRoot -Parent
$gameDir = 'F:\SteamLibrary\steamapps\common\ForzaHorizon4'
$target = Join-Path $gameDir 'dxgi.dll'
$manifestPath = Join-Path $workspace 'reports\dxgi-deployment.json'
if ($Action -eq 'Install') {
    $exe = Join-Path $gameDir 'ForzaHorizon4.exe'
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne 'b838a79a65c6d1e12f46deaba3584e4c88ebccdfdc4214b4535ece4ba3c0abbe') { throw 'Unexpected game executable build' }
    $relativeSource = switch ($Variant) { 'CameraProbe' { 'build\dxgi-camera\dxgi.dll' } 'VtableProbe' { 'build\dxgi-vtable\dxgi.dll' } 'YawTest' { 'build\dxgi-yaw\dxgi.dll' } 'HeadTrack' { 'build\headtrack\dxgi.dll' } 'GraphicsProbe' { 'build\graphics\dxgi.dll' } 'SwapchainWrapper' { 'build\wrapper\dxgi.dll' } 'FrameCapture' { 'build\capture\dxgi.dll' } 'GpuCopy' { 'build\gpu-copy\dxgi.dll' } 'LiveOverlay' { 'build\live-overlay\dxgi.dll' } 'EyeOffset' { 'build\eye-offset\dxgi.dll' } 'EyePosition' { 'build\eye-position\dxgi.dll' } 'AerProbe' { 'build\aer-probe\dxgi.dll' } 'AerDeferred' { 'build\aer-deferred\dxgi.dll' } 'StereoTrial' { 'build\stereo-trial\dxgi.dll' } default { 'build\dxgi\dxgi.dll' } }
    $source = Join-Path $workspace $relativeSource
    if (Test-Path -LiteralPath $target) { throw 'Existing game dxgi.dll found; refusing to overwrite it.' }
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    # Portable Scene dependency lives beside the proxy, never under a developer path.
    if ($Variant -eq 'StereoTrial') {
        $apiSource = Join-Path $workspace 'third_party\openvr\bin\win64\openvr_api.dll'
        $apiDir = Join-Path $gameDir 'fh4vr'
        $apiTarget = Join-Path $apiDir 'openvr_api.dll'
        $apiHash = (Get-FileHash -LiteralPath $apiSource -Algorithm SHA256).Hash
        if ((Test-Path -LiteralPath $apiTarget) -and (Get-FileHash -LiteralPath $apiTarget -Algorithm SHA256).Hash -ne $apiHash) {
            throw 'Existing fh4vr OpenVR dependency differs; refusing to overwrite it.'
        }
        New-Item -ItemType Directory -Path $apiDir -Force | Out-Null
        if (!(Test-Path -LiteralPath $apiTarget)) { [IO.File]::Copy($apiSource,$apiTarget,$false) }
        if ((Get-FileHash -LiteralPath $apiTarget -Algorithm SHA256).Hash -ne $apiHash) { throw 'OpenVR dependency hash mismatch' }
    }
    $manifest = [ordered]@{target=$target; sha256=$hash; source=$source; variant=$Variant; installed=$false; timestamp_utc=[DateTime]::UtcNow.ToString('o')}
    New-Item -ItemType Directory -Path (Split-Path $manifestPath -Parent) -Force | Out-Null
    $manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath
    [IO.File]::Copy($source,$target,$false)
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $hash) { throw 'Installed proxy hash mismatch' }
    $manifest.installed=$true
    $manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath
    Write-Host "Installed $Variant DXGI proxy: $target"
} else {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([IO.Path]::GetFullPath($manifest.target) -ne [IO.Path]::GetFullPath($target)) { throw 'Manifest target mismatch' }
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $manifest.sha256) { throw 'Proxy changed since installation; refusing to delete an unknown file.' }
        Remove-Item -LiteralPath $target
    }
    $manifest.installed=$false
    $manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath
    Write-Host 'Removed our proxy DLL; diagnostic log retained.'
}
