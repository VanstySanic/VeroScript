// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeStandardLibraryLimits
{
	internal uint StructSize, Reserved;
	internal ulong MaxTextBytes, MaxReadBytes, MaxSplitParts;
}

internal static unsafe partial class NativeMethods
{
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_session_set_stdlib_limits(
		nint session, NativeStandardLibraryLimits* limits);
}
