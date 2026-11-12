// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Microsoft.Win32;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace VeroScriptInstaller.Core.Platform;

[SupportedOSPlatform("windows")]
internal sealed partial class WindowsFileAssociationRegistry : IFileAssociationRegistry, IDisposable
{
	private readonly RegistryKey _root;
	private readonly bool _notify;

	// A fixed registry view makes registrations visible to all Runtime architectures.
	internal WindowsFileAssociationRegistry() : this(RegistryKey.OpenBaseKey(RegistryHive.CurrentUser,
		Environment.Is64BitOperatingSystem ? RegistryView.Registry64 : RegistryView.Registry32), true)
	{
	}

	// Tests use a private disposable hive, never the user's Classes/defaults.
	internal WindowsFileAssociationRegistry(RegistryKey root, bool notify = false)
	{
		_root = root;
		_notify = notify;
	}
	public bool IsSupported => true;
	public bool KeyExists(string key)
	{
		using var entry = _root.OpenSubKey(key);
		return entry is not null;
	}
	public AssociationRegistryValue? Read(string key, string name)
	{
		using var entry = _root.OpenSubKey(key);
		if (entry is null || !entry.GetValueNames().Contains(name, StringComparer.OrdinalIgnoreCase)) return null;
		var kind = entry.GetValueKind(name);
		return kind == RegistryValueKind.String
			? AssociationRegistryValue.String((string)entry.GetValue(name, "", RegistryValueOptions.DoNotExpandEnvironmentNames)!)
			: new AssociationRegistryValue(kind.ToString(), "");
	}
	public void WriteString(string key, string name, string value)
	{
		using var entry = _root.CreateSubKey(key, writable: true);
		entry.SetValue(name, value, RegistryValueKind.String);
		entry.Flush();
	}
	public void DeleteValue(string key, string name)
	{
		using var entry = _root.OpenSubKey(key, writable: true);
		entry?.DeleteValue(name, throwOnMissingValue: false);
		entry?.Flush();
	}
	public void DeleteKeyIfEmpty(string key)
	{
		using (var entry = _root.OpenSubKey(key))
		{
			if (entry is null || entry.ValueCount != 0 || entry.SubKeyCount != 0) return;
		}
		// DeleteSubKey never recursively removes a later writer's subkeys.
		using var parent = _root.OpenSubKey(key[..key.LastIndexOf('\\')], writable: true);
		parent?.DeleteSubKey(key[(key.LastIndexOf('\\') + 1)..], throwOnMissingSubKey: false);
	}
	public void NotifyChanged()
	{
		if (_notify)
		{
			SHChangeNotify(0x08000000, 0x0000, IntPtr.Zero, IntPtr.Zero);
		}
	}
	public void Dispose() => _root.Dispose();

	[LibraryImport("shell32.dll")]
	private static partial void SHChangeNotify(uint eventId, uint flags, IntPtr item1, IntPtr item2);
}
