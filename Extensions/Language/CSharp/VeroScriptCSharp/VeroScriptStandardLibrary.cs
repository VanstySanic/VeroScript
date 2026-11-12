// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

public readonly record struct VeroScriptStandardLibraryLimits(
	ulong MaxTextBytes = 0, ulong MaxReadBytes = 0, ulong MaxSplitParts = 0);

public sealed unsafe partial class VeroScriptSession
{
	public void SetStandardLibraryLimits(VeroScriptStandardLibraryLimits limits)
	{
		using var lifetime = new NativeLifetime(this);
		CheckOwner();
		NativeStandardLibraryLimits native = new()
		{
			StructSize = (uint)sizeof(NativeStandardLibraryLimits),
			MaxTextBytes = limits.MaxTextBytes,
			MaxReadBytes = limits.MaxReadBytes,
			MaxSplitParts = limits.MaxSplitParts
		};
		CheckStatus(NativeMethods.vs_interop_session_set_stdlib_limits(Handle, &native), "set standard library limits");
	}
}
