# Debugging without a working screen, keyboard or network

On a Hackintosh in the Recovery environment there is nothing to `scp` and the screen may be gone after a panic. The kexts
here therefore log into **firmware NVRAM** under the Apple GUID `7C436110-AB2A-4BBB-A880-FE41995C9F82`. NVRAM survives
hangs, panics and power cycles, and can be read from Windows on the same machine.

## Reading

```powershell
# Windows, elevated PowerShell on the same PC (needs SeSystemEnvironmentPrivilege)
.\scripts\read-nvram.ps1                 # all itl* variables below, plus boot-args
.\scripts\read-nvram.ps1 itls0 itlz0     # only some
.\scripts\clear-nvram.ps1                # delete them again (NVRAM space is small)
```

Kexts write with `IONVRAM-FORCESYNCNOW-PROPERTY` on `/options`, so values reach the flash without a clean shutdown.
From macOS: `nvram -p | grep itl`. From Linux: `/sys/firmware/efi/efivars/<name>-7c436110-ab2a-4bbb-a880-fe41995c9f82`
(not tested here).

> **Redact before posting.** The Wi-Fi logs contain the card MAC, BSSIDs and the SSIDs of nearby networks.

## Variables

| Variable | Written by | Content |
|---|---|---|
| `itl0`..`itl3` | ItlDiag | digest: clocks (mach/TSC ratio), xHCI controller state, USB device list |
| `itlm0`..`itlm5` | ItlDiag | mirror of the kext's probe / firmware / scan logs: `start 14c3:7925 ... CHIP=...`, download stages, scan summary (APs per channel) |
| `itln0`..`itln4` | ItlDiag | copy of the connect (M3) log; `itln4` = one-line result (`hs-ok tgt=1 ch=44 ...`) |
| `itlk0`..`itlk7` | itlmt7925 | the connect log written by the kext itself, 380 characters per variable, in order |
| `itls0` | itlmt7925 | data path counters: `up=145s rxd=5905 eapol=2 eth=5903 drop=0 tx=4698 fail=0 rxpk=8552` |
| `itlb0`..`itlb2` | itlmt7925 | fast-mode / Block Ack log (`itlmt_fast`), see [WIFI.md](WIFI.md#reading-the-result-of-a-test); in `fast` mode `itls0` also carries `L<level> uni=.. ba=.. reo=.. txf=.. am=..`; `itlb*` also holds the `rxd`, `amsdu ok/BAD` and `fast feat` diagnostics |
| `itlmt7925-log` | itlmt7925 | the last log line, useful for a hang |
| `itlt0`, `itlt1` | ItlDiag | timer probe: `mach/cal/tsc` per 100 ms sleep, and the mach vs. wall-clock ratio |
| `itli0` | ItlDiag | xHCI interrupter register state per controller |
| `itly0`..`itly2` | ItlDiag | HID device list with the transport (USB / I2C) |
| `itlx0`..`itlx3` | ItlDiag | event-ring sampler for xHCI 0..3: `ev`, `late5`, `maxlat` per ring (`itlxr=2`) |
| `itlz0` | ItlXhciKick | `kick up=<s> p=<us> c0:kicks/hits/miss ...` |

## Reading a Wi-Fi log

```
credentials: 2 network(s);net1 ssid len=8 psk len=9;net2 ssid len=13 psk len=8;       <- SSID/PSK lengths only, never the values
scan #1 for net2 (directed);selected net2 ssid len=13;target <bssid> ch=44 ...;       <- which network was picked and where
roc_acquire rc=0 st=0;roc grant=1;auth resp alg=0 seq=2 status=0;assoc resp status=0 aid=38;
eapol m1 ..;eapol m2 tx;eapol m3 mic ok;eapol m4 tx;key(ptk) rc=0;key(gtk) rc=0;     <- 4-way handshake
RESULT rc=0 link up;ethernet interface created;tx udp 68>67 ...;rx udp 67>68 ...      <- DHCP
```

A nonzero `rc` names the stage that failed (`-110` is a timeout, `-5` an I/O error). Typical causes: wrong passphrase
(the AP rejects msg 2, the log ends with `eapol: no msg3`), AP not found (out of range, or on a channel that is not
scanned; 6 GHz is not scanned), firmware not ready (`stage 9: FIRMWARE READY` missing).

## ItlDiag

Optional kext, boot-args:

| Boot-arg | Effect |
|---|---|
| `itldiag=<n>` | enable the diagnostics worker (any non-zero value; it runs for about 20 minutes) |
| `itlxr=1\|2` | sample xHCI event rings (`itlx*`); `2` is the variant used on the reference machine |
| `itlfile=1` | experimental: also try to write the text log into `ITLLOG.BIN` on the boot stick (`scripts/usblog.ps1 -Prepare` creates it, `-Read` shows it). Never worked on the reference machine and may block; use NVRAM instead |
| `itlpanic=<s>` | after that many seconds call `panic()` with the digest as the message, to get a log out of a machine you cannot otherwise reach |
| `itloff=1` | stay completely passive (to check whether the diagnostics themselves disturb something) |
| `itlkbd=<ms>` | fallback: initial key-repeat delay in milliseconds, `0` = off |

`scripts/recovery-dump.sh [mountpoint]` collects `ioreg`, `system_profiler`, `kextstat`, `dmesg` and a 25-second typing
trace from the Recovery Terminal into `<stick>/dump`.
