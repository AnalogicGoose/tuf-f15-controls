#!/usr/bin/env python3
"""python3 test/occonfig_test.py - occonfig.py against a scratch copy of efi/OC/config.plist (no ESP, no network)."""
import os
import plistlib
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import occonfig  # noqa: E402

GUID = "7C436110-AB2A-4BBB-A880-FE41995C9F82"


class OcConfig(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.c = os.path.join(self.d, "config.plist")
        shutil.copy(os.path.join(ROOT, "efi", "OC", "config.plist"), self.c)

    def tearDown(self):
        shutil.rmtree(self.d)

    def run_cmd(self, *a):
        return occonfig.main(["-c", self.c] + list(a))

    def read(self):
        with open(self.c, "rb") as f:
            return plistlib.load(f)

    def test_roundtrip_is_lossless_and_stable(self):
        # Hand-wrapped <data> in the source may be re-wrapped, so compare parsed
        # content and key order, then require the second pass to be byte-stable.
        with open(self.c, "rb") as f:
            raw = f.read()
        cfg = plistlib.loads(raw)
        once = occonfig.dumps(cfg)
        self.assertEqual(plistlib.loads(once), cfg)
        self.assertEqual(list(plistlib.loads(once)), list(cfg))
        self.assertEqual(once, occonfig.dumps(plistlib.loads(once)))

    def test_set_types_and_backup(self):
        self.run_cmd("set", "UEFI/Quirks/ResizeGpuBars", "13", "--tag", "t")
        self.assertEqual(self.read()["UEFI"]["Quirks"]["ResizeGpuBars"], 13)
        self.assertTrue(os.path.exists(self.c + ".t"))
        self.run_cmd("set", "Booter/Quirks/ProvideCustomSlide", "false", "--tag", "t2")
        self.assertIs(self.read()["Booter"]["Quirks"]["ProvideCustomSlide"], False)
        self.run_cmd("set", "NVRAM/Add/%s/csr-active-config" % GUID, "430a0000", "--tag", "t3")
        self.assertEqual(self.read()["NVRAM"]["Add"][GUID]["csr-active-config"], bytes.fromhex("430a0000"))

    def test_unknown_key_and_bad_value_refused(self):
        with self.assertRaises(SystemExit):
            self.run_cmd("set", "UEFI/Quirks/ResizeGpuBarz", "1")
        with self.assertRaises(SystemExit):
            self.run_cmd("set", "UEFI/Quirks/ResizeGpuBars", "abc")
        self.assertFalse([f for f in os.listdir(self.d) if f != "config.plist"])

    def test_array_selector_and_add(self):
        self.run_cmd("set", "Kernel/Add/[BundlePath=Lilu.kext]/Enabled", "false", "--tag", "t")
        lilu = [k for k in self.read()["Kernel"]["Add"] if k["BundlePath"] == "Lilu.kext"][0]
        self.assertIs(lilu["Enabled"], False)
        n = len(self.read()["Kernel"]["Block"])
        self.run_cmd("add", "Kernel/Block", '{"Identifier":"com.apple.iokit.IONDRVSupport","Strategy":"Exclude",'
                     '"Enabled":true,"Arch":"Any","Comment":"","MinKernel":"","MaxKernel":""}', "--tag", "t4")
        self.assertEqual(len(self.read()["Kernel"]["Block"]), n + 1)

    def test_boot_args(self):
        self.run_cmd("args", "add", "--tag", "t", "--", "-nvoff", "itlmt_fast=7")
        a = self.read()["NVRAM"]["Add"][GUID]["boot-args"].split()
        self.assertIn("-nvoff", a)
        self.assertEqual([w for w in a if w.startswith("itlmt_fast")], ["itlmt_fast=7"])
        self.run_cmd("args", "del", "--tag", "t2", "--", "-nvoff")
        self.assertNotIn("-nvoff", self.read()["NVRAM"]["Add"][GUID]["boot-args"].split())

    def test_secrets_masked(self):
        self.assertEqual(occonfig.show({"SystemSerialNumber": "X"}, False)["SystemSerialNumber"], "<masked>")

    def test_repo_config_matches_schema(self):
        out = []
        occonfig.typecheck(self.read(), occonfig.load_schema(), "", out)
        self.assertEqual(out, [])


if __name__ == "__main__":
    unittest.main()
