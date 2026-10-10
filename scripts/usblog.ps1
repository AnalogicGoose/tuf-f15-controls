# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and itlmt7925 contributors
# Prepare / read the USB log file used by ItlDiag's itlfile=1 path (no admin rights needed). Experimental: this path never
# worked on the reference machine; the NVRAM channel (scripts/read-nvram.ps1) is the reliable one.
#   .\scripts\usblog.ps1 -Prepare            create Y:\ITLLOG.BIN (1 MB, magic head/tail)
#   .\scripts\usblog.ps1 -Read               print and save the log the kext wrote
# Unplug and re-plug the stick after the macOS boot before -Read so Windows does not serve a cached copy.
param([string]$Drive = "Y", [switch]$Prepare, [switch]$Read)
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
    if ([Text.Encoding]::ASCII.GetString($b, 0, 8) -ne "ITLLOG01") { "header missing" ; return }
    $end = 512
    while ($end -lt $b.Length - 512 -and $b[$end] -ne 0) { $end++ }
    $text = [Text.Encoding]::UTF8.GetString($b, 512, $end - 512)
    $out = Join-Path $PSScriptRoot "..\out\usblog-$(Get-Date -Format yyyyMMdd-HHmmss).txt"
    New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
    Set-Content -Path $out -Value $text -Encoding UTF8
    "$($text.Length) chars saved to $out"
}
