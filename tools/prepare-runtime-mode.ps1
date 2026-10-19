param([Parameter(Mandatory=$true)][string]$ModuleDirectory)
$ErrorActionPreference='Stop'
if (Get-Process ForzaHorizon4 -ErrorAction SilentlyContinue) { throw 'Close FH4 before preparing its startup mode list.' }
$workspace=Split-Path $PSScriptRoot -Parent
$directory=(Resolve-Path -LiteralPath $ModuleDirectory).Path
$ini=Join-Path $directory 'fh4vr.ini'
$query=Join-Path $workspace 'build\stereo-trial\query_runtime_mode.exe'
$api=Join-Path $directory 'fh4vr\openvr_api.dll'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Fh4ModeProfile {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string section,string key,string fallback,StringBuilder value,uint size,string path);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern bool WritePrivateProfileString(string section,string key,string value,string path);
}
'@
$buffer=New-Object Text.StringBuilder 64
[void][Fh4ModeProfile]::GetPrivateProfileString('VR','RenderScale','1.0',$buffer,64,$ini)
$result=& $query $api $buffer.ToString()
if ($LASTEXITCODE -ne 0) { throw "Runtime query failed: $result" }
$plan=$result | ConvertFrom-Json
if ($plan.width -lt 256 -or $plan.width -gt 4096 -or $plan.height -lt 256 -or $plan.height -gt 8192 -or $plan.width%4 -or $plan.height%4) { throw 'Runtime target exceeds current GPU copy limits; choose a lower RenderScale.' }
if (Test-Path -LiteralPath $ini) { Copy-Item -LiteralPath $ini -Destination ($ini+'.backup-'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')) }
# Disable first so an incomplete write cannot enable a partially updated mode.
foreach($entry in @(@('AddRuntimeMode','0'),@('RuntimeWidth',[string]$plan.width),@('RuntimeHeight',[string]$plan.height),@('AddRuntimeMode','1'))) {
 if (![Fh4ModeProfile]::WritePrivateProfileString('Diagnostic',$entry[0],$entry[1],$ini)) { throw 'Failed to save runtime mode configuration' }
}
$plan | ConvertTo-Json -Compress
Write-Host "Prepared optional runtime mode in $ini. Re-run with the connected headset before each diagnostic launch."
