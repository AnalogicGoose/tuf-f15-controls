#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and itlmt7925 contributors
# Collects ioreg / system_profiler / kextstat / dmesg and a 25 s typing trace into <stick>/dump for USB HID latency work.
# Recovery Terminal (the stick auto-mounts under /Volumes):  sh /Volumes/<stick label>/recovery-dump.sh [mountpoint]
STICK="${1:-$(dirname "$0")}"
OUT="$STICK/dump"
mkdir -p "$OUT" || { echo "cannot write to $STICK"; exit 1; }
cd "$OUT" || exit 1
echo "== system"
date > info.txt
{
  uptime
  sw_vers
  sysctl machdep.cpu.brand_string hw.ncpu hw.tbfrequency hw.cpufrequency hw.busfrequency kern.clockrate machdep.tsc.frequency 2>&1
  nvram -p 2>&1 | grep -i "boot-args\|itl"
} >> info.txt 2>&1
echo "== processes (CPU snapshot before typing)"
ps -axo pid,pcpu,pmem,state,etime,comm | sort -k2 -nr | head -25 > ps_before.txt 2>&1
echo "== ioreg (full tree, takes a while)"
ioreg -l -w0 > ioreg_full.txt 2>&1
ioreg -p IOUSB -l -w0 > ioreg_usb.txt 2>&1
ioreg -r -c IOHIDSystem -l -w0 > ioreg_hidsystem.txt 2>&1
ioreg -r -c IOHIDEventService -l -w0 > ioreg_hideventservice.txt 2>&1
ioreg -r -c IOHIDDevice -l -w0 > ioreg_hiddevice.txt 2>&1
ioreg -r -c AppleUSBHostController -l -w0 > ioreg_usbctrl.txt 2>&1
echo "== system_profiler / hidutil / kexts"
system_profiler SPUSBDataType > sp_usb.txt 2>&1
system_profiler SPDisplaysDataType > sp_displays.txt 2>&1
hidutil list > hidutil_list.txt 2>&1
kextstat 2>&1 | grep -v " com.apple" > kextstat_third.txt
kextstat > kextstat_all.txt 2>&1
dmesg > dmesg.txt 2>&1
echo "== typing test: type keys NOW for 25 s (hold d 2 s and release a few times, then move the mouse). Do not stop."
log stream --level debug --predicate 'subsystem CONTAINS "iohid" OR subsystem CONTAINS "hid" OR subsystem CONTAINS "usb" OR process == "hidd" OR process == "WindowServer"' > logstream.txt 2>&1 &
LSPID=$!
top -l 13 -s 2 -o cpu -n 12 > top_typing.txt 2>&1
kill $LSPID 2>/dev/null
echo "== sample WindowServer (3 s)"
sample WindowServer 3 -file ws_sample.txt > /dev/null 2>&1
ps -axo pid,pcpu,pmem,state,etime,comm | sort -k2 -nr | head -25 > ps_after.txt 2>&1
sync
echo "== done. files in $OUT"
ls -la "$OUT"
