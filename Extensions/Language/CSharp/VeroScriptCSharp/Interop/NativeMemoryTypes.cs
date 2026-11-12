// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeMemoryLimits
{
	internal uint StructSize, Reserved;
	internal ulong MaxBufferBytes, MaxRetainedBytes;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeMemoryStats
{
	internal uint StructSize, Reserved;
	internal ulong OwnedBytes, BorrowedBytes, PeakOwnedBytes;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeMemoryLeaseToken
{
	internal ulong Runtime, Serial;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeMemoryLease
{
	internal uint StructSize, Reserved;
	internal nint Data, WritableData;
	internal ulong Size;
	internal NativeMemoryLeaseToken Token;
}
