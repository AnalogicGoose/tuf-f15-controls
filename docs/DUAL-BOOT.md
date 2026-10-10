# Sharing the machine with Windows and Linux

The reference machine has Windows and CachyOS (Limine boot manager) on the internal NVMe, sharing one EFI system partition
(ESP), and OpenCore on a USB stick. This page records what can go wrong and how to repair it.

## What happened on the reference machine

After several boots of the OpenCore stick, the firmware boot menu no longer offered Linux. Inspection from Windows
(`bcdedit /enum firmware`, and reading `BootOrder` / `Boot####` through `GetFirmwareEnvironmentVariable`) showed:

* the ESP was **intact**: `\EFI\limine\limine_x64.efi`, `limine.conf`, the kernel/initramfs directory, `\EFI\Microsoft`;
* the Linux partition and the Windows partition were untouched;
* NVRAM held only `Boot0000` (Windows Boot Manager) and `Boot0001` (the USB stick). The Limine entry (`Boot####`) was gone.

So nothing was overwritten; only the firmware's pointer to the boot loader was lost. The cause was not proven. Candidates:
OpenCore's *Reset NVRAM* action (`ResetNvramEntry.efi` is part of the reference configuration), a firmware clean-up of
entries after many NVRAM writes (the diagnostic kexts write `itl*` variables), or a CMOS/NVRAM reset. If you rely on
firmware boot entries for Linux, avoid *Reset NVRAM* in the OpenCore picker, and keep a note of how to recreate them.

## Repair from Windows

Run from an **elevated** PowerShell (adds a firmware entry, removes nothing):

```powershell
.\scripts\restore-limine-entry.ps1                       # Limine of CachyOS, put first in the boot order
.\scripts\restore-limine-entry.ps1 -Last                 # keep Windows first, pick Linux from the firmware boot menu
.\scripts\restore-limine-entry.ps1 -Path '\EFI\systemd\systemd-bootx64.efi' -Name 'Linux Boot Manager'   # other loaders
```

It copies `{bootmgr}` (so the entry points to the same ESP), sets the loader path and adds the entry to the firmware display
order. The script prints the new GUID and the undo command (`bcdedit /delete {guid}`). Check the loader path first:
mount the ESP (`mountvol S: /S`, unmount with `mountvol S: /D`) and look under `S:\EFI`.

Equivalent from Linux (live USB or an installed system): `efibootmgr --create --disk /dev/nvme0n1 --part 1 --label "CachyOS (Limine)" --loader '\EFI\limine\limine_x64.efi'`.

## Booting Linux without a firmware entry

* the firmware boot menu (ASUS: `Esc` or `F8` during POST) only lists existing entries and removable media;
* OpenCore with `ScanPolicy = 0` lists loaders it finds on internal disks, including the fallback
  `\EFI\BOOT\BOOTX64.EFI`; press `Space` in the picker to show auxiliary entries (`HideAuxiliary` is on). On the reference
  machine that fallback is **not** the current Limine build (different size and date), so prefer recreating the entry.

## Before installing macOS next to Linux/Windows

* **Back up** the Linux home and snapshots and the Windows data; resizing partitions is the risky step, not OpenCore.
* Free space has to come from a partition that is not mounted: shrink the Linux filesystem (for btrfs:
  `btrfs filesystem resize` first, then the partition) from a live USB, never the running root.
* Keep OpenCore on the USB stick until macOS boots from it. Install macOS to its **own** disk or partition; do not let the
  installer write to the shared ESP. The reference stick has `LauncherOption = Disabled`, so OpenCore does not register
  itself in NVRAM either.
* After installing, re-check `bcdedit /enum firmware` (or `efibootmgr -v`) and keep a copy of `limine.conf`.
