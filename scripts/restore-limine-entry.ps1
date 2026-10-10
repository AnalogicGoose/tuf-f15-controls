# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and itlmt7925 contributors
# Re-creates the UEFI boot entry of a Linux boot loader that lives on the Windows EFI system partition, from Windows.
# Needed when the firmware's NVRAM boot entries were wiped (OpenCore "Reset NVRAM", firmware clean-up, CMOS reset...): the files
# on the ESP are untouched, only the Boot#### variable is gone. Run from an elevated PowerShell. Nothing is deleted.
#   .\restore-limine-entry.ps1                                   # Limine of CachyOS: \EFI\limine\limine_x64.efi, put first
#   .\restore-limine-entry.ps1 -Path '\EFI\systemd\systemd-bootx64.efi' -Name 'Linux Boot Manager' -Last
# Undo: bcdedit /delete {guid}  (the guid is printed; also listed by: bcdedit /enum firmware)
param([string]$Path = '\EFI\limine\limine_x64.efi', [string]$Name = 'CachyOS (Limine)', [switch]$Last)
$existing = bcdedit /enum firmware | Out-String
if ($existing -match [regex]::Escape($Name)) { Write-Host "an entry named '$Name' already exists:"; bcdedit /enum firmware; exit 0 }
$r = bcdedit /copy '{bootmgr}' /d $Name | Out-String
$g = [regex]::Match($r, '\{[0-9a-fA-F-]{36}\}').Value
if (-not $g) { Write-Host "could not copy {bootmgr}: $r"; exit 1 }
bcdedit /set $g path $Path | Out-Host
$pos = if ($Last) { '/addlast' } else { '/addfirst' }
bcdedit /set '{fwbootmgr}' displayorder $g $pos | Out-Host
Write-Host "created $g ($Name -> $Path). Undo with: bcdedit /delete $g"
bcdedit /enum firmware | Out-Host
