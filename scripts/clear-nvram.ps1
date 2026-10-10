# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2026 thanhnndev and itlmt7925 contributors
# Deletes the itl* diagnostic variables (Apple GUID) from firmware NVRAM so the variable store has room again. Needs an elevated PowerShell.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FwDel {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool SetFirmwareEnvironmentVariableW(string n, string g, byte[] b, uint sz);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr p, uint a, out IntPtr t);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool LookupPrivilegeValue(string s, string n, out long l);
    [StructLayout(LayoutKind.Sequential, Pack=1)] struct TP { public int Count; public long Luid; public int Attr; }
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool AdjustTokenPrivileges(IntPtr t, bool d, ref TP n, int l, IntPtr p, IntPtr r);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    public static bool Priv() {
        IntPtr t; if (!OpenProcessToken(GetCurrentProcess(), 0x28, out t)) return false;
        TP tp = new TP(); tp.Count = 1; tp.Attr = 2;
        if (!LookupPrivilegeValue(null, "SeSystemEnvironmentPrivilege", out tp.Luid)) return false;
        return AdjustTokenPrivileges(t, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero);
    }
    public static bool Del(string name, string guid) { return SetFirmwareEnvironmentVariableW(name, guid, null, 0); }
}
'@
if (-not [FwDel]::Priv()) { Write-Host "need an elevated (Administrator) PowerShell"; exit 1 }
$g = '{7C436110-AB2A-4BBB-A880-FE41995C9F82}'
$names = @('itl0','itl1','itl2','itl3','itlm0','itlm1','itlm2','itlm3','itlm4','itlm5','itln0','itln1','itln2','itln3','itln4','itlk0','itlk1','itlk2','itlk3','itlk4','itlt0','itlt1','itli0','itly0','itly1','itly2','itlx0','itlx1','itlx2','itlx3','itlk5','itlk6','itlk7','itls0','itlz0','itlmt7925-log')
foreach ($n in $names) { $ok = [FwDel]::Del($n, $g); Write-Host ("{0,-16} {1}" -f $n, $(if ($ok) {'deleted'} else {'-'})) }
