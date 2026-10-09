# Reads the NVRAM variable written by the kext (run in an elevated PowerShell on Windows).
# macOS only flushes NVRAM to firmware on a clean shutdown/restart, so shut down from the Apple menu first.
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class Fw {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern uint GetFirmwareEnvironmentVariableW(string n, string g, byte[] buf, uint size);
  [DllImport("advapi32.dll", SetLastError=true)] public static extern bool OpenProcessToken(IntPtr h, uint a, out IntPtr t);
  [DllImport("advapi32.dll", SetLastError=true)] public static extern bool LookupPrivilegeValue(string s, string n, out long luid);
  [DllImport("advapi32.dll", SetLastError=true)] public static extern bool AdjustTokenPrivileges(IntPtr t, bool d, ref TP n, int l, IntPtr p, IntPtr r);
  [StructLayout(LayoutKind.Sequential, Pack=1)] public struct TP { public int Count; public long Luid; public int Attr; }
  public static void Enable() { IntPtr t; OpenProcessToken(System.Diagnostics.Process.GetCurrentProcess().Handle, 0x28, out t);
    TP tp = new TP(); tp.Count=1; tp.Attr=2; LookupPrivilegeValue(null,"SeSystemEnvironmentPrivilege",out tp.Luid); AdjustTokenPrivileges(t,false,ref tp,0,IntPtr.Zero,IntPtr.Zero); }
}
"@
[Fw]::Enable()
$guid = "{7C436110-AB2A-4BBB-A880-FE41995C9F82}"
foreach ($name in "itlmt7925-log") {
  $buf = New-Object byte[] 1024
  $n = [Fw]::GetFirmwareEnvironmentVariableW($name, $guid, $buf, 1024)
  if ($n -gt 0) { "$name = " + [Text.Encoding]::ASCII.GetString($buf, 0, $n).TrimEnd([char]0) }
  else { "$name : not found (win32 error " + [Runtime.InteropServices.Marshal]::GetLastWin32Error() + ")" }
}
"boot-args = " + $(try { $b = New-Object byte[] 1024; $m=[Fw]::GetFirmwareEnvironmentVariableW("boot-args",$guid,$b,1024); [Text.Encoding]::ASCII.GetString($b,0,$m) } catch { "?" })
