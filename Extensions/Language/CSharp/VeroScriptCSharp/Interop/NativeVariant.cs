// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
namespace VeroScript.Interop;

internal static unsafe partial class NativeMethods
{
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_value_set_variant_empty(nint value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_value_set_variant(nint session, nint value, NativeStringView type, nint payload);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_variant_has_value(nint value, byte* present);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_variant_get_type_name(nint value, NativeStringView* name);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_variant_get(nint value, nint output);
}
