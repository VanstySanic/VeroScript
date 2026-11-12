// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScriptInstaller.Core.Platform;

internal sealed partial class WindowsPathIntegration : IPathIntegration
{
	public Task<bool> ContainsUserPathAsync(string directory, CancellationToken cancellationToken = default)
	{
		cancellationToken.ThrowIfCancellationRequested();
		var fullPath = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar);
		return Task.FromResult(PathList.Contains(
			Environment.GetEnvironmentVariable("Path", EnvironmentVariableTarget.User), fullPath));
	}

	public Task<PathMutation> AddToUserPathAsync(string directory, CancellationToken cancellationToken = default)
	{
		cancellationToken.ThrowIfCancellationRequested();
		var fullPath = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar);
		var previous = Environment.GetEnvironmentVariable("Path", EnvironmentVariableTarget.User);
		if (PathList.Contains(previous, fullPath))
		{
			return Task.FromResult(new PathMutation { Entry = fullPath, Changed = false });
		}
		var updated = PathList.Append(previous, fullPath);
		if (updated.Length > 32_767)
		{
			throw new InvalidOperationException("The user PATH would exceed the Windows environment-variable limit.");
		}
		var previousProcess = Environment.GetEnvironmentVariable("Path", EnvironmentVariableTarget.Process);
		var updatedProcess = PathList.Append(previousProcess, fullPath);
		if (updatedProcess.Length > 32_767)
		{
			throw new InvalidOperationException("The process PATH would exceed the Windows environment-variable limit.");
		}
		try
		{
			Environment.SetEnvironmentVariable("Path", updated, EnvironmentVariableTarget.User);
			Environment.SetEnvironmentVariable("Path", updatedProcess, EnvironmentVariableTarget.Process);
			BroadcastEnvironmentChange();
		}
		catch
		{
			// AddToUserPathAsync must either return a mutation that the caller can
			// undo, or leave both environment scopes unchanged.
			Environment.SetEnvironmentVariable("Path", previous, EnvironmentVariableTarget.User);
			Environment.SetEnvironmentVariable("Path", previousProcess, EnvironmentVariableTarget.Process);
			BroadcastEnvironmentChange();
			throw;
		}
		return Task.FromResult(new PathMutation
		{
			Entry = fullPath,
			Changed = true,
		});
	}

	public Task UndoAsync(PathMutation mutation, CancellationToken cancellationToken = default)
	{
		cancellationToken.ThrowIfCancellationRequested();
		if (!mutation.Changed)
		{
			return Task.CompletedTask;
		}
		return RemoveFromUserPathAsync(mutation.Entry, cancellationToken);
	}

	public Task RemoveFromUserPathAsync(string directory, CancellationToken cancellationToken = default)
	{
		cancellationToken.ThrowIfCancellationRequested();
		var fullPath = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar);
		var previous = Environment.GetEnvironmentVariable("Path", EnvironmentVariableTarget.User);
		if (PathList.Contains(previous, fullPath))
		{
			var previousProcess = Environment.GetEnvironmentVariable("Path", EnvironmentVariableTarget.Process);
			try
			{
				Environment.SetEnvironmentVariable(
					"Path", PathList.Remove(previous, fullPath), EnvironmentVariableTarget.User);
				UpdateProcessPath(fullPath, add: false);
				BroadcastEnvironmentChange();
			}
			catch
			{
				Environment.SetEnvironmentVariable("Path", previous, EnvironmentVariableTarget.User);
				Environment.SetEnvironmentVariable("Path", previousProcess, EnvironmentVariableTarget.Process);
				BroadcastEnvironmentChange();
				throw;
			}
		}
		return Task.CompletedTask;
	}

	private static void UpdateProcessPath(string directory, bool add)
	{
		var processPath = Environment.GetEnvironmentVariable("Path", EnvironmentVariableTarget.Process);
		Environment.SetEnvironmentVariable("Path",
			add ? PathList.Append(processPath, directory) : PathList.Remove(processPath, directory),
			EnvironmentVariableTarget.Process);
	}

	private static void BroadcastEnvironmentChange()
	{
		if (!OperatingSystem.IsWindows())
		{
			return;
		}
		_ = SendMessageTimeout(
			new IntPtr(0xffff), 0x001A, UIntPtr.Zero, "Environment",
			0x0002, 5000, out _);
	}

	[LibraryImport("user32.dll", EntryPoint = "SendMessageTimeoutW", StringMarshalling = StringMarshalling.Utf16)]
	private static partial IntPtr SendMessageTimeout(
		IntPtr windowHandle,
		uint message,
		UIntPtr wParam,
		string lParam,
		uint flags,
		uint timeout,
		out UIntPtr result);
}
