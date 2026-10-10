# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and contributors
# Prepare / read the log file ItlDiag writes straight into the clusters of a pre-allocated ITLLOG.BIN in the root of a FAT32
# volume (the internal ESP that OpenCore boots from, and/or the boot stick). Every volume that carries the file gets the log.
#   .\scripts\usblog.ps1 -Esp -Prepare      create ITLLOG.BIN (1 MB, magic head/tail) in the root of the EFI System Partition
#   .\scripts\usblog.ps1 -Esp -Read         print and save what ItlDiag wrote there
#   .\scripts\usblog.ps1 -Drive Y -Prepare  same for a stick mounted as Y:
# -Esp mounts the ESP as S: for the duration of the call and needs an elevated shell. For a stick, unplug and re-plug it after
# the macOS boot before -Read so Windows does not serve a cached copy (the ESP is mounted fresh each time).
param([string]$Drive = "Y", [switch]$Esp, [switch]$Prepare, [switch]$Read)
$mounted = $false
if ($Esp) {
    $Drive = "S"
    if (-not (Test-Path "S:\")) { mountvol S: /S | Out-Null; $mounted = $true }
    if (-not (Test-Path "S:\")) { "could not mount the ESP as S: (elevated shell needed)"; return }
}
try {
    $path = "${Drive}:\ITLLOG.BIN"
    $size = 1MB
    if ($Prepare) {
        $buf = New-Object byte[] $size
        [Text.Encoding]::ASCII.GetBytes("ITLLOG01").CopyTo($buf, 0)
        [Text.Encoding]::ASCII.GetBytes("ITLEND01").CopyTo($buf, $size - 512)
        [IO.File]::WriteAllBytes($path, $buf)
        (Get-Item $path).Attributes = 'Normal'
        "created $path ($size bytes)"
    }
    if ($Read) {
        $b = [IO.File]::ReadAllBytes($path)
        if ([Text.Encoding]::ASCII.GetString($b, 0, 8) -ne "ITLLOG01") { "header missing"; return }
        $end = 512
        while ($end -lt $b.Length - 512 -and $b[$end] -ne 0) { $end++ }
        $text = [Text.Encoding]::UTF8.GetString($b, 512, $end - 512)
        $out = Join-Path $PSScriptRoot "..\out\usblog-$(Get-Date -Format yyyyMMdd-HHmmss).txt"
        New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
        Set-Content -Path $out -Value $text -Encoding UTF8
        "$($text.Length) chars saved to $out"
    }
} finally {
    if ($mounted) { mountvol S: /D | Out-Null }
}
