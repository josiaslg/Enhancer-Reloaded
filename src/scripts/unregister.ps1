# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
# unregister.ps1 - reverts everything register.ps1 did (all endpoints with a backup). Run as Administrator.
$ErrorActionPreference = 'Continue'
$InstallDir = 'C:\Program Files\EnhancerAPO'
$RenderRoot = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render'
$kinds = @{ '{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6' = [Microsoft.Win32.RegistryValueKind]::String
            '{d3993a3f-99c2-4402-b5ec-a92a0367664b},6' = [Microsoft.Win32.RegistryValueKind]::MultiString
            '{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5' = [Microsoft.Win32.RegistryValueKind]::DWord }

Stop-Service Audiosrv -Force -ErrorAction SilentlyContinue
Stop-Service AudioEndpointBuilder -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

$backups = Get-ChildItem (Join-Path $InstallDir 'backup-*.json') | Where-Object { $_.Name -ne 'backup-audiodg.json' }
if (Test-Path (Join-Path $InstallDir 'backup.json')) { $backups += Get-Item (Join-Path $InstallDir 'backup.json') }
foreach ($bf in $backups) {
    $b = Get-Content $bf.FullName -Raw | ConvertFrom-Json
    $hk = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey("$RenderRoot\$($b.EndpointId)\FxProperties", [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree, [System.Security.AccessControl.RegistryRights]::SetValue)
    if (-not $hk) { Write-Warning "Endpoint $($b.EndpointId) not found"; continue }
    foreach ($k in $kinds.Keys) {
        $old = $b.$k
        if ($null -eq $old) { try { $hk.DeleteValue($k, $false) } catch {} }
        elseif ($kinds[$k] -eq [Microsoft.Win32.RegistryValueKind]::MultiString) { $hk.SetValue($k, [string[]]@($old), $kinds[$k]) }
        else { $hk.SetValue($k, $old, $kinds[$k]) }
    }
    $hk.Close()
    Write-Host "Restored: $($b.Name) ($($b.EndpointId))"
    Remove-Item $bf.FullName -Force
}
$dg = Join-Path $InstallDir 'backup-audiodg.json'
$audioKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Audio'
if (Test-Path $dg) {
    $prev = (Get-Content $dg -Raw | ConvertFrom-Json).DisableProtectedAudioDG
    if ($null -eq $prev) { Remove-ItemProperty $audioKey -Name DisableProtectedAudioDG -ErrorAction SilentlyContinue } else { Set-ItemProperty $audioKey -Name DisableProtectedAudioDG -Value $prev }
    Remove-Item $dg -Force
}
Start-Process regsvr32.exe -ArgumentList "/s /u `"$InstallDir\EnhancerAPO.dll`"" -Wait
Start-Service AudioEndpointBuilder -ErrorAction SilentlyContinue
Start-Service Audiosrv -ErrorAction SilentlyContinue
Write-Host "Enhancer APO removed. Files left in $InstallDir (delete manually if you want)."
