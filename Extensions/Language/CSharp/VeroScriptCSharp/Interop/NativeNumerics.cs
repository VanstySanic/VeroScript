// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeNumericLimits
{
	internal uint StructSize, Reserved;
	internal ulong MaxValueBits, MaxInputBytes, MaxOutputBytes, MaxScratchBytes, MaxWorkUnits;
}

// Keep the raw declaration surface aligned with the Core C ABI. Numeric values
// remain opaque host values; this does not add automatic managed number mapping.
internal static unsafe partial class NativeMethods
{
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_session_set_numeric_limits(nint session, NativeNumericLimits* limits);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern uint vs_interop_value_numeric_kind(nint value);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_numeric_parse(nint session, nint call, uint kind, NativeStringView text, nint output);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_numeric_to_text(nint session, nint call, nint value, nint output);

	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_numeric_convert(nint session, nint call, nint value, uint targetNativeType, uint exactFloat, nint output);
}
