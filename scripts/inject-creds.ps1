# Fills itl_ssid / itl_psk in a built itlmt7925.kext Info.plist from the Wi-Fi profile this PC is connected to.
# The passphrase ends up in plaintext inside the kext (and therefore on the EFI stick); it is never printed here
# and the repo keeps empty values.
# usage: .\inject-creds.ps1 -Kext Y:\EFI\OC\Kexts\itlmt7925.kext [-Ssid <profile>]   (reading the key needs an elevated shell)
param([Parameter(Mandatory)][string]$Kext, [string]$Ssid)
$plist = Join-Path $Kext 'Contents\Info.plist'
if (-not (Test-Path $plist)) { Write-Host "no Info.plist at $plist"; exit 1 }
if (-not $Ssid) {
    $Ssid = (netsh wlan show interfaces | Select-String '^\s+SSID\s+:\s+(.+)$' | Select-Object -First 1).Matches.Groups[1].Value.Trim()
}
if (-not $Ssid) { Write-Host "no connected Wi-Fi and no -Ssid given"; exit 1 }
$m = netsh wlan show profile name="$Ssid" key=clear | Select-String 'Key Content\s+:\s+(.+)$'
if (-not $m) { Write-Host "no key content for profile '$Ssid' (not elevated, or not a PSK profile)"; exit 1 }
$psk = $m.Matches.Groups[1].Value.Trim()
$esc = { param($t) [System.Security.SecurityElement]::Escape($t) }
$txt = [IO.File]::ReadAllText($plist)
$txt = [regex]::Replace($txt, '(<key>itl_ssid</key>\s*<string>)[^<]*(</string>)', { param($x) $x.Groups[1].Value + (& $esc $Ssid) + $x.Groups[2].Value })
$txt = [regex]::Replace($txt, '(<key>itl_psk</key>\s*<string>)[^<]*(</string>)', { param($x) $x.Groups[1].Value + (& $esc $psk) + $x.Groups[2].Value })
[IO.File]::WriteAllText($plist, $txt, (New-Object Text.UTF8Encoding($false)))
Write-Host ("injected ssid '{0}' ({1} chars) and a {2} char passphrase into {3}" -f $Ssid, $Ssid.Length, $psk.Length, $plist)
