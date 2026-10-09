# Reads firmware (NVRAM) variables written by macOS under the Apple GUID and prints them. Needs an elevated PowerShell.
# usage: .\read-nvram.ps1 [name ...]   (default: boot-args itl0..itl3 itlm0 itlm1)
param([string[]]$Names)
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Fw {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern uint GetFirmwareEnvironmentVariableW(string n, string g, byte[] b, uint sz);
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
    public static string Get(string name, string guid) {
        byte[] b = new byte[65536];
        uint n = GetFirmwareEnvironmentVariableW(name, guid, b, (uint)b.Length);
        if (n == 0) return null;
        return Encoding.UTF8.GetString(b, 0, (int)n);
    }
}
'@
if (-not [Fw]::Priv()) { Write-Host "need an elevated (Administrator) PowerShell"; exit 1 }
if (-not $Names) { $Names = @('boot-args') + @('itl0','itl1','itl2','itl3','itlm0','itlm1','itlm2','itlm4','itlm5','itlm3','itlx0','itlx1','itlx2','itlx3') }
foreach ($n in $Names) {
    $v = [Fw]::Get($n, '{7C436110-AB2A-4BBB-A880-FE41995C9F82}')
    if ($null -eq $v) { Write-Host "[$n] -" } else { Write-Host "[$n] $v" }
}
