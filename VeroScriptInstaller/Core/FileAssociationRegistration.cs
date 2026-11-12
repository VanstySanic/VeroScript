// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core.Platform;

namespace VeroScriptInstaller.Core;

/// <summary>Plans value-level, reversible registrations without changing extension defaults or UserChoice.</summary>
internal static class FileAssociationRegistration
{
	private const string Classes = @"Software\Classes\";
	private const string RegisteredApps = @"Software\RegisteredApplications";
	private const string OwnerValue = "VeroScriptInstallerOwner";
	private const string PackageIconKey = Classes + @".vspkg\DefaultIcon";
	private static readonly StringComparer RegistryComparer = StringComparer.OrdinalIgnoreCase;

	internal static FileAssociationOptions SupportedOptions(InstallerManifest manifest)
	{
		var runtime = manifest.Components.SingleOrDefault(component => component.Id == InstallerConstants.RuntimeComponentId);
		if (manifest.Platform != "windows" || runtime is null) return new();
		var files = runtime.Files.Select(file => PathSafety.NormalizeRelativePath(file.Path)).ToHashSet(StringComparer.OrdinalIgnoreCase);
		return new FileAssociationOptions
		{
			Source = files.Contains("veroscript.exe") && files.Contains("assets/icons/veroscript-source.ico"),
			Application = files.Contains("veroscript.exe") && files.Contains("assets/icons/veroscript-app.ico"),
			PackageIcon = files.Contains("assets/icons/veroscript-package.ico"),
		};
	}

	internal static string RegisteredApplicationName(string owner) => $"VeroScript Runtime ({owner})";
	internal static string? DefaultApplicationName(FileAssociationRecord? record) => record is not null
		&& (record.Options.Source || record.Options.Application)
		&& record.Values.Where(value => !RegistryComparer.Equals(value.Key, PackageIconKey)).All(value => value.Owned)
		? RegisteredApplicationName(record.OwnerId) : null;
	private static string Capabilities(string owner) => $@"Software\VeroScript\Installer\Associations\{owner}\Capabilities";
	private static string SourceId(string owner) => "VSRun." + owner;
	private static string AppId(string owner) => "VSApp." + owner;
	private static string Identity(string key, string name) => key + "\0" + name;
	private static string Identity(OwnedAssociationValue value) => Identity(value.Key, value.Name);

	private static List<OwnedAssociationValue> Expected(string owner, string runtimeRoot, FileAssociationOptions options)
	{
		var result = new List<OwnedAssociationValue>();
		void Add(string key, string name, string value) => result.Add(new() { Key = key, Name = name, Value = value });
		string Icon(string stem) => $"\"{Path.Combine(runtimeRoot, "assets", "icons", stem + ".ico")}\",0";
		void Handler(string extension, string id, string title, string icon)
		{
			var key = Classes + id;
			Add(key, OwnerValue, owner);
			Add(key, "", title);
			Add(key + @"\DefaultIcon", "", Icon(icon));
			Add(key + @"\shell", "", "open");
			Add(key + @"\shell\open", "", "Run with VeroScript");
			Add(key + @"\shell\open\command", "", $"\"{Path.Combine(runtimeRoot, "veroscript.exe")}\" \"%1\"");
			Add(Classes + extension + @"\OpenWithProgids", id, "");
		}
		if (options.Source)
		{
			Handler(".vs", SourceId(owner), "VeroScript source (run)", "veroscript-source");
		}
		if (options.Application)
		{
			Handler(".vsapp", AppId(owner), "VeroScript application", "veroscript-app");
		}
		if (options.Source || options.Application)
		{
			var capabilities = Capabilities(owner);
			Add(capabilities, OwnerValue, owner);
			Add(capabilities, "ApplicationName", RegisteredApplicationName(owner));
			Add(capabilities, "ApplicationDescription", "Runs VeroScript source files and application containers.");
			if (options.Source)
			{
				Add(capabilities + @"\FileAssociations", ".vs", SourceId(owner));
			}
			if (options.Application)
			{
				Add(capabilities + @"\FileAssociations", ".vsapp", AppId(owner));
			}
			Add(RegisteredApps, RegisteredApplicationName(owner), capabilities);
		}
		if (options.PackageIcon)
		{
			// An icon-only registration: no .vspkg open command, default ProgID, or package installation.
			Add(PackageIconKey, OwnerValue, owner);
			Add(PackageIconKey, "", Icon("veroscript-package"));
		}
		return result;
	}

	internal static FileAssociationPlan Prepare(
		IFileAssociationRegistry registry, FileAssociationOptions options,
		string? runtimeRoot, FileAssociationRecord? previous)
	{
		if (!options.Any && previous is null) return new(null, null, false);
		if (!registry.IsSupported)
		{
			throw new PlatformNotSupportedException("File associations require Windows.");
		}
		var owner = previous?.OwnerId ?? Guid.NewGuid().ToString("N");
		var desired = options.Any ? Expected(owner, runtimeRoot!, options) : [];
		var old = (previous?.Values ?? []).ToDictionary(Identity, RegistryComparer);
		var wanted = desired.Select(Identity).ToHashSet(RegistryComparer);
		var change = new FileAssociationChange { OwnerId = owner };
		var groups = desired.Select(value => GroupRoot(owner, value.Key, value.Name)).Distinct(RegistryComparer)
			.ToDictionary(key => key, key => CanUseGroup(key), RegistryComparer);
		var preserved = false;
		foreach (var value in desired)
		{
			var current = registry.Read(value.Key, value.Name);
			old.TryGetValue(Identity(value), out var prior);
			value.Owned = groups[GroupRoot(owner, value.Key, value.Name)]
				&& (prior is null ? current is null
					: prior.Owned && (current is null || current == AssociationRegistryValue.String(prior.Value)));
			if (!value.Owned)
			{
				preserved = true;
				continue;
			}
			if (current != AssociationRegistryValue.String(value.Value))
			{
				change.Values.Add(new() { Key = value.Key, Name = value.Name, Before = current?.Data, After = value.Value });
			}
			foreach (var key in KeyAncestors(owner, value.Key))
			{
				if (!registry.KeyExists(key) && !change.CreatedKeys.Contains(key, RegistryComparer))
				{
					change.CreatedKeys.Add(key);
				}
			}
		}
		// Markers are last on removal. Never delete a registry tree or an extension's default value.
		foreach (var value in (previous?.Values ?? []).Where(value => !wanted.Contains(Identity(value)))
			.OrderBy(value => RegistryComparer.Equals(value.Name, OwnerValue)))
		{
			if (!value.Owned)
			{
				continue;
			}
			var group = GroupRoot(owner, value.Key, value.Name);
			if (registry.Read(group, OwnerValue) != AssociationRegistryValue.String(owner))
			{
				preserved = true;
				continue;
			}
			var current = registry.Read(value.Key, value.Name);
			if (current == AssociationRegistryValue.String(value.Value))
			{
				change.Values.Add(new() { Key = value.Key, Name = value.Name, Before = value.Value });
			}
			else if (current is not null)
			{
				preserved = true;
			}
		}
		var createdKeys = (previous?.CreatedKeys ?? []).Concat(change.CreatedKeys).Distinct(RegistryComparer).ToList();
		change.PruneKeys = createdKeys.Where(key => !desired.Any(value => value.Owned
			&& (RegistryComparer.Equals(value.Key, key) || value.Key.StartsWith(key + "\\", StringComparison.OrdinalIgnoreCase)))).ToList();
		var state = options.Any ? new FileAssociationRecord { OwnerId = owner, Options = options, Values = desired,
			CreatedKeys = createdKeys.Except(change.PruneKeys, RegistryComparer).ToList() } : null;
		ValidateChange(change);
		return new(state, change, preserved);

		bool CanUseGroup(string key)
		{
			var marker = registry.Read(key, OwnerValue);
			if (marker == AssociationRegistryValue.String(owner)) return previous is not null;
			if (marker is not null) return false;
			var priorMarker = previous?.Values.SingleOrDefault(value => RegistryComparer.Equals(value.Key, key) && RegistryComparer.Equals(value.Name, OwnerValue));
			if (priorMarker is not null) return priorMarker.Owned;
			return RegistryComparer.Equals(key, PackageIconKey) ? registry.Read(key, "") is null : !registry.KeyExists(key);
		}
	}

	internal static void Apply(IFileAssociationRegistry registry, FileAssociationChange change, CancellationToken cancellationToken)
	{
		ValidateChange(change);
		foreach (var value in change.Values)
		{
			cancellationToken.ThrowIfCancellationRequested();
			if (!RegistryComparer.Equals(value.Name, OwnerValue) && registry.Read(GroupRoot(change.OwnerId, value.Key, value.Name), OwnerValue)
				!= AssociationRegistryValue.String(change.OwnerId))
			{
				throw new IOException("The file association registration is now owned by another installation.");
			}
			var expected = value.Before is null ? null : AssociationRegistryValue.String(value.Before);
			if (registry.Read(value.Key, value.Name) != expected)
			{
				throw new IOException("File association settings changed while the installer was running. Retry the operation.");
			}
			Set(registry, value.Key, value.Name, value.After);
		}
		Prune(registry, change.PruneKeys);
		if (change.Values.Count != 0)
		{
			registry.NotifyChanged();
		}
	}

	internal static void Undo(IFileAssociationRegistry registry, FileAssociationChange change, bool notify = true)
	{
		ValidateChange(change);
		foreach (var value in change.Values.AsEnumerable().Reverse())
		{
			if (!RegistryComparer.Equals(value.Name, OwnerValue) && registry.Read(GroupRoot(change.OwnerId, value.Key, value.Name), OwnerValue)
				!= AssociationRegistryValue.String(change.OwnerId))
			{
				continue;
			}
			var expected = value.After is null ? null : AssociationRegistryValue.String(value.After);
			if (registry.Read(value.Key, value.Name) == expected)
			{
				Set(registry, value.Key, value.Name, value.Before);
			}
		}
		Prune(registry, change.CreatedKeys);
		if (notify && change.Values.Count != 0)
		{
			registry.NotifyChanged();
		}
	}

	private static void Set(IFileAssociationRegistry registry, string key, string name, string? value)
	{
		if (value is null)
		{
			registry.DeleteValue(key, name);
		}
		else
		{
			registry.WriteString(key, name, value);
		}
	}
	private static void Prune(IFileAssociationRegistry registry, IEnumerable<string> keys)
	{
		foreach (var key in keys.OrderByDescending(key => key.Length))
		{
			registry.DeleteKeyIfEmpty(key);
		}
	}

	private static string GroupRoot(string owner, string key, string name)
	{
		if (RegistryComparer.Equals(key, PackageIconKey)) return PackageIconKey;
		if (key.StartsWith(Classes + SourceId(owner), StringComparison.OrdinalIgnoreCase) || RegistryComparer.Equals(key, Classes + @".vs\OpenWithProgids")) return Classes + SourceId(owner);
		if (key.StartsWith(Classes + AppId(owner), StringComparison.OrdinalIgnoreCase) || RegistryComparer.Equals(key, Classes + @".vsapp\OpenWithProgids")) return Classes + AppId(owner);
		return Capabilities(owner);
	}

	private static IEnumerable<string> KeyAncestors(string owner, string key)
	{
		if (RegistryComparer.Equals(key, RegisteredApps))
		{
			yield break;
		}
		var floor = key.StartsWith(Classes, StringComparison.OrdinalIgnoreCase)
			? Classes + key[Classes.Length..].Split('\\')[0]
			: Capabilities(owner)[..^"\\Capabilities".Length];
		for (var current = key; current.Length >= floor.Length; current = current[..current.LastIndexOf('\\')])
		{
			yield return current;
		}
	}

	internal static void ValidateRecord(InstallationRecord record)
	{
		if (record.FileAssociations is not { } state) return;
		var runtime = record.Components.SingleOrDefault(component => component.Id == InstallerConstants.RuntimeComponentId);
		if (record.Platform != "windows" || runtime is null || state.Options is null || !state.Options.Any
			|| !Guid.TryParseExact(state.OwnerId, "N", out _) || state.Values is null || state.CreatedKeys is null)
		{
			throw new InvalidDataException("Invalid file association ownership metadata.");
		}
		ValidateRuntimeRoot(runtime.RootPath);
		var supportFiles = runtime.Files.ToHashSet(StringComparer.OrdinalIgnoreCase);
		if ((state.Options.Source || state.Options.Application) && !supportFiles.Contains("veroscript.exe")
			|| state.Options.Source && !supportFiles.Contains("assets/icons/veroscript-source.ico")
			|| state.Options.Application && !supportFiles.Contains("assets/icons/veroscript-app.ico")
			|| state.Options.PackageIcon && !supportFiles.Contains("assets/icons/veroscript-package.ico"))
		{
			throw new InvalidDataException("Association resources are not owned by the Runtime component.");
		}
		if (state.Values.Any(value => value is null || value.Key is null || value.Name is null || value.Value is null))
		{
			throw new InvalidDataException("Invalid file association value.");
		}
		var expected = Expected(state.OwnerId, runtime.RootPath, state.Options).ToDictionary(Identity, RegistryComparer);
		if (state.Values.Count != expected.Count || state.Values.Select(Identity).Distinct(RegistryComparer).Count() != expected.Count)
		{
			throw new InvalidDataException("Invalid file association value inventory.");
		}
		foreach (var value in state.Values)
		{
			if (!expected.TryGetValue(Identity(value), out var item) || item.Value != value.Value)
			{
				throw new InvalidDataException("Invalid file association registry entry.");
			}
		}
		ValidateKeys(state.OwnerId, state.CreatedKeys);
	}

	internal static void ValidateRuntimeRoot(string root)
	{
		if (!Path.IsPathFullyQualified(root) || root.IndexOfAny(['"', '\0', '\r', '\n']) >= 0)
		{
			throw new InvalidDataException("Invalid Runtime path for file association commands.");
		}
	}

	internal static void ValidateChange(FileAssociationChange change)
	{
		if (!Guid.TryParseExact(change.OwnerId, "N", out _) || change.Values is null || change.Values.Count > 64
			|| change.CreatedKeys is null || change.PruneKeys is null)
		{
			throw new InvalidDataException("Invalid file association transaction.");
		}
		var allowed = Expected(change.OwnerId, Path.GetFullPath("."), new() { Source = true, Application = true, PackageIcon = true })
			.Select(Identity).ToHashSet(RegistryComparer);
		var seen = new HashSet<string>(RegistryComparer);
		foreach (var value in change.Values)
		{
			if (value is null || value.Key is null || value.Name is null || value.Before == value.After
				|| !allowed.Contains(Identity(value.Key, value.Name)) || !seen.Add(Identity(value.Key, value.Name))
				|| value.Before?.Length > 32_768 || value.After?.Length > 32_768
				|| value.Before?.Contains('\0') == true || value.After?.Contains('\0') == true)
			{
				throw new InvalidDataException("Invalid file association transaction entry.");
			}
			else if (RegistryComparer.Equals(value.Name, OwnerValue) && (value.Before is not null && value.Before != change.OwnerId
				|| value.After is not null && value.After != change.OwnerId))
			{
				throw new InvalidDataException("Invalid file association owner marker.");
			}
		}
		ValidateKeys(change.OwnerId, change.CreatedKeys);
		ValidateKeys(change.OwnerId, change.PruneKeys);
	}

	private static void ValidateKeys(string owner, List<string> keys)
	{
		var allowed = Expected(owner, Path.GetFullPath("."), new() { Source = true, Application = true, PackageIcon = true })
			.SelectMany(value => KeyAncestors(owner, value.Key)).ToHashSet(RegistryComparer);
		if (keys.Count > 64 || keys.Distinct(RegistryComparer).Count() != keys.Count || keys.Any(key => key is null || !allowed.Contains(key)))
		{
			throw new InvalidDataException("Invalid owned file association key.");
		}
	}
}
