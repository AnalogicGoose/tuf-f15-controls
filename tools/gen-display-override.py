#!/usr/bin/env python3
"""Build the macOS display override for the GA403WM OLED (SDC ATNA40CU05-0, 2880x1800).

The panel's base EDID DTD is 2880x1800 @ 60 Hz (vtotal 3648); the 120 Hz mode (total 2980x1824, same pixel
clock) exists only in the DisplayID 2.0 extension, which macOS does not parse. This rewrites DTD 0 of the base
block to the 120 Hz timing so macOS picks it as the preferred mode, and adds a HiDPI scale-resolutions entry.

usage: gen-display-override.py PANEL.edid OUTFILE
       (PANEL.edid: 256 bytes, e.g. `cat /sys/class/drm/card1-eDP-1/edid` on Linux)
"""
import plistlib
import struct
import sys

VENDOR, PRODUCT = 0x4C83, 0x419C
PCLK_10KHZ_120 = 65226          # 652.26 MHz, what NVKMS reports for the 120 Hz raster (652264 kHz truncated)
HTOTAL, VTOTAL_120 = 2980, 1824


def checksum(block):
    return (-sum(block[:127])) & 0xFF


def to_120hz(edid):
    if len(edid) != 256:
        raise SystemExit("expected a 256-byte EDID (base + DisplayID extension), got %d" % len(edid))
    if (edid[8] << 8 | edid[9]) != VENDOR or (edid[10] | edid[11] << 8) != PRODUCT:
        raise SystemExit("EDID is not SDC 0x4c83 / product 0x419c")
    out = bytearray(edid)
    d = out[54:72]
    ha = d[2] | (d[4] >> 4) << 8
    hb = d[3] | (d[4] & 15) << 8
    va = d[5] | (d[7] >> 4) << 8
    if (ha, va, ha + hb) != (2880, 1800, HTOTAL):
        raise SystemExit("DTD 0 is not the expected 2880x1800 / htotal 2980 timing")
    vb = VTOTAL_120 - va                       # 24 lines of vertical blanking
    struct.pack_into("<H", d, 0, PCLK_10KHZ_120)
    d[6] = vb & 0xFF
    d[7] = (d[7] & 0xF0) | (vb >> 8)
    # vfp/vsw/hfp/hsw and the flags byte are unchanged (vfp 8, vsw 8 -> vbp 8, same as DisplayID Type VII)
    out[54:72] = d
    # range-limits descriptor already allows 48..120 Hz, pclk max 660 MHz: nothing to change
    out[127] = checksum(out[:128])
    return bytes(out)


def hidpi(width, height):
    return struct.pack(">IIII", width, height, 0x00000001, 0x00200000)


def main():
    src, dst = sys.argv[1:3]
    edid = to_120hz(open(src, "rb").read())
    plist = {
        "DisplayProductName": "Built-in OLED 120 Hz",
        "DisplayVendorID": VENDOR,
        "DisplayProductID": PRODUCT,
        "IODisplayEDID": edid,
        # backing store == panel raster, so WindowServer composes at 2880x1800 and the framebuffer needs no scaler
        "scale-resolutions": [hidpi(2880, 1800)],
    }
    with open(dst, "wb") as f:
        plistlib.dump(plist, f, fmt=plistlib.FMT_XML)
    print("wrote", dst, "dtd0 pclk %d x10kHz vtotal %d" % (PCLK_10KHZ_120, VTOTAL_120))


main()
