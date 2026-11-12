// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core.Platform;

public sealed class PathMutation
{
	public required string Entry { get; init; }
	public bool Changed { get; init; }
	internal string? StoragePath { get; init; }
}

public interface IPathIntegration
{
	Task<bool> ContainsUserPathAsync(string directory, CancellationToken cancellationToken = default);
	Task<PathMutation> AddToUserPathAsync(string directory, CancellationToken cancellationToken = default);
	Task UndoAsync(PathMutation mutation, CancellationToken cancellationToken = default);
	Task RemoveFromUserPathAsync(string directory, CancellationToken cancellationToken = default);
}

public static class PathIntegrationFactory
{
	public static IPathIntegration Create() => OperatingSystem.IsWindows()
		? new WindowsPathIntegration()
		: new UnixShellPathIntegration();
}
