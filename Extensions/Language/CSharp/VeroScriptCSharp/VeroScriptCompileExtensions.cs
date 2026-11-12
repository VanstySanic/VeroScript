// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript;

public sealed record VeroScriptCompileExtensionRoot(string Path, string RegistrationEntry = "");

/// <summary>Explicit compilation extensions and execution policy. Each compile owns its extension state.</summary>
public sealed class VeroScriptCompileExtensionsOptions
{
	public IReadOnlyList<VeroScriptCompileExtensionRoot> Roots { get; init; } = [];
	public IReadOnlyDictionary<string, string> Configuration { get; init; } = new Dictionary<string, string>();
	public IReadOnlyList<string> AllowedRoots { get; init; } = [];
	public bool AllowSourceImports { get; init; } = true;
	public ulong MaxInstructions { get; init; } = 1_000_000;
	public ulong TimeoutMilliseconds { get; init; } = 2_000;
}
