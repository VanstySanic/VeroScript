// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeCompileExtensionRoot
{
	internal NativeStringView Path, RegistrationEntry;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeCompileConfiguration
{
	internal NativeStringView Key, Value;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeScriptCompileOptions
{
	internal uint StructSize;
	internal byte AllowSourceImports;
	private fixed byte _reserved[3];
	internal NativeCompileExtensionRoot* Roots;
	internal ulong RootCount;
	internal NativeCompileConfiguration* Configuration;
	internal ulong ConfigurationCount;
	internal ulong MaxInstructions, TimeoutMilliseconds;
	internal NativeStringView* AllowedRoots;
	internal ulong AllowedRootCount;
}
