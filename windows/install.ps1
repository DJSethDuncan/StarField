<#
  Installs StarField.scr for the current user (no administrator rights needed)
  and makes it the active screensaver.

    powershell -ExecutionPolicy Bypass -File install.ps1            # install
    powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall # remove
#>
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$dest = Join-Path $env:LOCALAPPDATA 'StarField'
$target = Join-Path $dest 'StarField.scr'
$desktop = 'HKCU:\Control Panel\Desktop'

if ($Uninstall) {
    $current = (Get-ItemProperty $desktop -Name 'SCRNSAVE.EXE' -ErrorAction SilentlyContinue).'SCRNSAVE.EXE'
    if ($current -and $current -ieq $target) {
        Remove-ItemProperty $desktop -Name 'SCRNSAVE.EXE'
    }
    Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force 'HKCU:\Software\StarField' -ErrorAction SilentlyContinue
    Write-Host 'StarField removed.'
    exit 0
}

$source = Join-Path $PSScriptRoot 'StarField.scr'
if (-not (Test-Path $source)) { throw "StarField.scr not found next to install.ps1" }

New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Force $source $target
Unblock-File $target -ErrorAction SilentlyContinue

Set-ItemProperty $desktop -Name 'SCRNSAVE.EXE' -Value $target
Set-ItemProperty $desktop -Name 'ScreenSaveActive' -Value '1'

Write-Host "Installed to $target and set as your screensaver."
# Open Screen Saver Settings so you can preview, adjust options and timeout.
Start-Process 'control.exe' -ArgumentList 'desk.cpl,,@screensaver'
