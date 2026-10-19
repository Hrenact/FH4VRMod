param(
    [string]$GameExe = 'F:\SteamLibrary\steamapps\common\ForzaHorizon4\ForzaHorizon4.exe',
    [switch]$Live
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$reportDir = Join-Path $workspace ('reports\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$dumpbin = & $vswhere -latest -products '*' -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
if (!$dumpbin) { throw 'MSVC dumpbin not found' }
& $dumpbin /imports $GameExe | Set-Content -LiteralPath (Join-Path $reportDir 'imports.txt')
if ($LASTEXITCODE -ne 0) { throw 'dumpbin imports failed' }
& $dumpbin /headers $GameExe | Set-Content -LiteralPath (Join-Path $reportDir 'headers.txt')
if ($LASTEXITCODE -ne 0) { throw 'dumpbin headers failed' }
$runtime = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -ErrorAction SilentlyContinue
$game = Get-Process ForzaHorizon4 -ErrorAction SilentlyContinue | Select-Object -First 1
$environment = [ordered]@{
    game_version = (Get-Item -LiteralPath $GameExe).VersionInfo.FileVersion
    active_openxr_runtime = $runtime.ActiveRuntime
    gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion)
    game_running = ($null -ne $game)
    game_pid = if ($game) { $game.Id } else { $null }
}
$environment | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportDir 'environment.json')
$scanArgs = @((Join-Path $PSScriptRoot 'inspect_game.py'), '--exe', $GameExe, '--out', (Join-Path $reportDir 'disk.json'))
& python @scanArgs
if ($LASTEXITCODE -ne 0) { throw 'Disk inspection failed' }
if ($Live) {
    if (!$game) { throw 'FH4 is not running. Start the game and enter cockpit view first.' }
    $module = $game.MainModule
    if ([IO.Path]::GetFullPath($module.FileName) -ne [IO.Path]::GetFullPath($GameExe)) { throw 'Running executable path differs' }
    $game.Modules | Select-Object ModuleName,FileName,@{Name='BaseAddress';Expression={'0x{0:X}' -f $_.BaseAddress.ToInt64()}},ModuleMemorySize |
        ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $reportDir 'modules.json')
    & python (Join-Path $PSScriptRoot 'inspect_game.py') --exe $GameExe --out (Join-Path $reportDir 'live.json') --pid $game.Id --base $module.BaseAddress.ToInt64() --size $module.ModuleMemorySize
    if ($LASTEXITCODE -ne 0) { throw 'Live read-only inspection failed' }
}
Write-Host "Baseline saved: $reportDir"
