#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and itlmt7925 contributors
"""Fail if efi/OC/config.plist carries anything but the placeholder platform identifiers (run in CI and before a commit).

    python scripts/check-efi-privacy.py [path/to/config.plist ...]
"""
import plistlib, sys

EXPECT = {
    "SystemSerialNumber": "CHANGE_ME_SN",
    "MLB": "CHANGE_ME_MLB_000",
    "SystemUUID": "00000000-0000-0000-0000-000000000000",
    "ROM": bytes(6),
}


def main(paths):
    bad = 0
    for path in paths:
        with open(path, "rb") as f:
            cfg = plistlib.load(f)
        g = cfg.get("PlatformInfo", {}).get("Generic", {})
        for k, want in EXPECT.items():
            if g.get(k) != want:
                print("%s: PlatformInfo.Generic.%s is not the placeholder" % (path, k))
                bad += 1
        sec = cfg.get("Misc", {}).get("Security", {})
        if sec.get("PasswordHash") or sec.get("PasswordSalt"):
            print("%s: Misc.Security carries a password hash / salt" % path)
            bad += 1
    if bad:
        sys.exit(1)
    print("ok: %d file(s) carry placeholders only" % len(paths))


if __name__ == "__main__":
    main(sys.argv[1:] or ["efi/OC/config.plist"])
