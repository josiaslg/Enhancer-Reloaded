# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
# register.ps1 - installs EnhancerAPO on one or all render endpoints. Run as Administrator.
#   .\register.ps1 -EndpointId "{0.0.0.00000000}.{guid}"   (ids: apodev.exe)
#   .\register.ps1 -All                                     (all active outputs)
# Everything is reverted by unregister.ps1 (per-endpoint backup in C:\Program Files\EnhancerAPO\backup-{guid}.json).
param(
    [string]$EndpointId = '',
    [switch]$All,
    [string]$SourceDir = ''
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceDir)) { $SourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not (Test-Path (Join-Path $SourceDir 'EnhancerAPO.dll'))) { throw "EnhancerAPO.dll not found in $SourceDir" }
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Run this script from an elevated (Administrator) PowerShell.' }
if (-not $All -and [string]::IsNullOrWhiteSpace($EndpointId)) { throw 'Pass -EndpointId "{...}" or -All' }

$Clsid = '{8E7C1D3A-5B4F-4E2A-9C6D-3F1A2B4C5D6E}'
$InstallDir = 'C:\Program Files\EnhancerAPO'
$LogDir = 'C:\ProgramData\EnhancerAPO'
$RenderRoot = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
$K_ModeEffectClsid = '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6'
$K_MfxModes        = '{d3993a3f-99c2-4402-b5ec-a92a0367664b},6'
$K_DisableSysFx    = '{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5'
$ModeDefault       = '{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}'
$K_Name            = '{a45c254e-df1c-4efd-8020-67d146a850e0},2'
$sidEveryone = New-Object System.Security.Principal.SecurityIdentifier('S-1-1-0')
$sidUsers    = New-Object System.Security.Principal.SecurityIdentifier('S-1-5-32-545')

# --- target endpoints
$targets = @()
if ($All) {
    foreach ($k in Get-ChildItem $RenderRoot) {
        $state = (Get-ItemProperty $k.PSPath -ErrorAction SilentlyContinue).DeviceState
        if ($state -eq 1) { $targets += $k.PSChildName }   # 1 = active
    }
} else {
    if ($EndpointId -match '(\{[0-9a-fA-F-]{36}\})\s*$') { $targets += $Matches[1] } else { throw "Invalid endpoint id: $EndpointId" }
}
if ($targets.Count -eq 0) { throw 'No active render endpoint found.' }

# --- 1. files (stop audio first: audiodg keeps the DLL loaded while a stream exists)
New-Item -ItemType Directory -Force $InstallDir | Out-Null
Stop-Service Audiosrv -Force -ErrorAction SilentlyContinue
Stop-Service AudioEndpointBuilder -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
try { Copy-Item (Join-Path $SourceDir 'EnhancerAPO.dll') $InstallDir -Force }
finally { Start-Service AudioEndpointBuilder -ErrorAction SilentlyContinue; Start-Service Audiosrv -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force $LogDir | Out-Null
$acl = Get-Acl $LogDir
$acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule($sidEveryone,'Modify','ContainerInherit,ObjectInherit','None','Allow')))
Set-Acl $LogDir $acl

# --- 2. COM registration + AudioEngine APO list (done by the DLL through RegisterAPO)
$r = Start-Process regsvr32.exe -ArgumentList "/s `"$InstallDir\EnhancerAPO.dll`"" -Wait -PassThru
if ($r.ExitCode -ne 0) { throw "regsvr32 failed: $($r.ExitCode)" }
if (-not (Test-Path "HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\$Clsid")) { throw 'RegisterAPO did not create the AudioEngine\AudioProcessingObjects key' }
Write-Host "COM class + APO registered: $Clsid"

# --- 3. allow an unsigned APO inside audiodg
$audioKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio'
$prevDG = (Get-ItemProperty $audioKey -ErrorAction SilentlyContinue).DisableProtectedAudioDG
New-ItemProperty $audioKey -Name DisableProtectedAudioDG -PropertyType DWord -Value 1 -Force | Out-Null
$dgBackup = Join-Path $InstallDir 'backup-audiodg.json'
if (-not (Test-Path $dgBackup)) { @{DisableProtectedAudioDG = $prevDG} | ConvertTo-Json | Set-Content $dgBackup }

# --- 4. parameter key (neutral state), writable by Users so the panel needs no elevation
$pk = 'HKLM:\SOFTWARE\EnhancerAPO'
if (-not (Test-Path $pk)) {
    New-Item $pk -Force | Out-Null
    $defaults = @{Power=1;Boost=0;Volume=50;HarmBass=0;HarmBassRange=50;DrumBass=0;DrumBassRange=50;Dry=100;HarmTreble=0;HarmTrebleRange=50;Ambience=0;AmbienceRange=50}
    foreach ($kv in $defaults.GetEnumerator()) { New-ItemProperty $pk -Name $kv.Key -PropertyType DWord -Value $kv.Value -Force | Out-Null }
}
$pacl = Get-Acl $pk
$pacl.AddAccessRule((New-Object System.Security.AccessControl.RegistryAccessRule($sidUsers,'FullControl','ContainerInherit,ObjectInherit','None','Allow')))
Set-Acl $pk $pacl

# --- 5. FxProperties of each endpoint (per-endpoint backup; open the key with SetValue only, which Administrators hold)
$skipped = @()
foreach ($guid in $targets) {
  try {
    $props = "$RenderRoot\$guid\Properties"; $fx = "$RenderRoot\$guid\FxProperties"
    $name = (Get-ItemProperty $props -ErrorAction SilentlyContinue).$K_Name
    if (-not (Test-Path $fx)) {
        # Administrators cannot create subkeys under MMDevices (owner: SYSTEM); this endpoint is skipped.
        Write-Warning "No FxProperties subkey, skipping: $name ($guid)"
        $skipped += $name; continue
    }
    $backupFile = Join-Path $InstallDir "backup-$guid.json"
    if (-not (Test-Path $backupFile)) {
        $b = @{ EndpointId = $guid; Name = $name }
        foreach ($k in @($K_ModeEffectClsid, $K_MfxModes, $K_DisableSysFx)) {
            $v = Get-ItemProperty $fx -Name $k -ErrorAction SilentlyContinue
            if ($v) { $b[$k] = $v.$k } else { $b[$k] = $null }
        }
        $b | ConvertTo-Json | Set-Content $backupFile
    }
    $rel = $fx -replace '^HKLM:\\', ''
    $hk = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($rel, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree, [System.Security.AccessControl.RegistryRights]::SetValue)
    if (-not $hk) { Write-Warning "Could not open $fx"; continue }
    $hk.SetValue($K_ModeEffectClsid, $Clsid, [Microsoft.Win32.RegistryValueKind]::String)
    $hk.SetValue($K_MfxModes, [string[]]@($ModeDefault), [Microsoft.Win32.RegistryValueKind]::MultiString)
    $hk.SetValue($K_DisableSysFx, 0, [Microsoft.Win32.RegistryValueKind]::DWord)
    $hk.Close()
    Write-Host "Enhancer APO installed on: $name  ($guid)"
  } catch {
    Write-Warning "Failed on $guid : $_"
  }
}
if ($skipped.Count -gt 0) { Write-Host "Not installed (no FxProperties): $($skipped -join ', ')" }

# --- 6. restart audio so the endpoint configuration is reloaded
Restart-Service AudioEndpointBuilder -Force -ErrorAction SilentlyContinue
Start-Service Audiosrv -ErrorAction SilentlyContinue
Write-Host 'Audio services restarted. Done.'
