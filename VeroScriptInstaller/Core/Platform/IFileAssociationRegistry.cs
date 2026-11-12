// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core.Platform;

/// <summary>Per-user registry access. Other value types are observed but never modified.</summary>
public interface IFileAssociationRegistry
{
	bool IsSupported { get; }
	bool KeyExists(string key);
	AssociationRegistryValue? Read(string key, string name);
	void WriteString(string key, string name, string value);
	void DeleteValue(string key, string name);
	void DeleteKeyIfEmpty(string key);
	void NotifyChanged();
}

public sealed record AssociationRegistryValue(string Kind, string Data)
{
	public static AssociationRegistryValue String(string value) => new("String", value);
}

internal static class FileAssociationRegistryFactory
{
	internal static IFileAssociationRegistry Create() => OperatingSystem.IsWindows()
		? new WindowsFileAssociationRegistry() : new UnsupportedFileAssociationRegistry();
}

internal sealed class UnsupportedFileAssociationRegistry : IFileAssociationRegistry
{
	public bool IsSupported => false;
	public bool KeyExists(string key) => false;
	public AssociationRegistryValue? Read(string key, string name) => null;
	public void WriteString(string key, string name, string value) => throw new PlatformNotSupportedException();
	public void DeleteValue(string key, string name) => throw new PlatformNotSupportedException();
	public void DeleteKeyIfEmpty(string key) => throw new PlatformNotSupportedException();
	public void NotifyChanged()
	{
	}
}
