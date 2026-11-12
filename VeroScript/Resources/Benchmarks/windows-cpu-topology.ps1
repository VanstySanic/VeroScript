# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Read-only topology query. Offsets follow SYSTEM_CPU_SET_INFORMATION in the
# Windows SDK; never changes affinity, priority or power policy for other apps.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class VeroPerfCpuTopology {
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetSystemCpuSetInformation(IntPtr info, uint length, out uint returned, IntPtr process, uint flags);
    public static byte[] Read() {
        uint size;
        GetSystemCpuSetInformation(IntPtr.Zero, 0, out size, IntPtr.Zero, 0);
        if (size == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
        IntPtr buffer = Marshal.AllocHGlobal((int)size);
        try {
            uint returned;
            if (!GetSystemCpuSetInformation(buffer, size, out returned, IntPtr.Zero, 0))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] bytes = new byte[returned];
            Marshal.Copy(buffer, bytes, 0, bytes.Length);
            return bytes;
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }
}
'@
$perfCpuBytes = [VeroPerfCpuTopology]::Read()
$perfCpuRows = for ($perfOffset = 0; $perfOffset -lt $perfCpuBytes.Length;) {
	$perfSize = [BitConverter]::ToUInt32($perfCpuBytes, $perfOffset)
	if ($perfSize -lt 32 -or $perfOffset + $perfSize -gt $perfCpuBytes.Length) { throw 'invalid CPU-set record' }
	if ([BitConverter]::ToUInt32($perfCpuBytes, $perfOffset + 4) -eq 0) {
		[pscustomobject]@{
			id = [BitConverter]::ToUInt32($perfCpuBytes, $perfOffset + 8)
			group = [BitConverter]::ToUInt16($perfCpuBytes, $perfOffset + 12)
			logicalProcessor = $perfCpuBytes[$perfOffset + 14]
			core = $perfCpuBytes[$perfOffset + 15]
			efficiencyClass = $perfCpuBytes[$perfOffset + 18]
			schedulingClass = $perfCpuBytes[$perfOffset + 20]
		}
	}
	$perfOffset += $perfSize
}
$perfCpuRows | ConvertTo-Json
