// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript;

/// <summary>The selected interpreter, independently of VM/Mixed/JIT policy.</summary>
/// <remarks>Strings are copied snapshots. Capabilities preserves the native VS_INTEROP_VM_CAP_* bits,
/// independently of process-wide VeroScriptCapabilities. Static selections have an empty LibraryPath.</remarks>
public sealed record VeroScriptVmBackendInfo(string Id, string Version, string Target,
	string LibraryPath, ulong Capabilities, bool Selected, bool Frozen);
