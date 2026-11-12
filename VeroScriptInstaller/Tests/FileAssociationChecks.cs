// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Microsoft.Win32;
using VeroScriptInstaller.Core;
using VeroScriptInstaller.Core.Platform;

namespace VeroScriptInstaller.Tests;

internal static partial class Program
{
	private static readonly FileAssociationOptions AllAssociations = new() { Source = true, Application = true, PackageIcon = true };
	private const string OwnerMarker = "VeroScriptInstallerOwner";
	private const string PackageIconRegistryKey = @"Software\Classes\.vspkg\DefaultIcon";

	private static async Task WithAssociationBundle(Func<BundleContext, Task> action)
	{
		if (!OperatingSystem.IsWindows())
		{
			Console.WriteLine("SKIP Windows installer association integration.");
			return;
		}
		await WithBundleAsync(async context =>
		{
			foreach (var name in new[] { "source", "app", "package" })
			{
				await WriteAsync(Path.Combine(context.RuntimeSource, "assets", "icons", "veroscript-" + name + ".ico"), "synthetic icon " + name);
			}
			await InstallerBundleBuilder.BuildAsync(context.Bundle, "1.0.0", new Dictionary<string, string>
				{ ["runtime"] = context.RuntimeSource, ["docs"] = context.DocsSource });
			await action(context);
		});
	}

	private static InstallOptions AssociationOptions(BundleContext context, string destination, FileAssociationOptions? options = null, string? existing = null) => new()
	{
		ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { "runtime" },
		ComponentDestinations = new Dictionary<string, string> { ["runtime"] = destination }, FileAssociations = options ?? AllAssociations,
		ExistingInstallRecordPath = existing,
	};
	private static string Marker(string destination) => Path.Combine(destination, InstallerConstants.InstallRecordFileName);
	private static string ValueId(string key, string name) => key + "\0" + name;
	private static async Task AssociationRegistryCaseInsensitiveAsync() => await WithAssociationBundle(async context =>
	{
		foreach (var foreignOwner in new[] { false, true })
		{
			var registry = new MemoryAssociationRegistry();
			var engine = new InstallerEngine(new FakePathIntegration(), registry);
			var destination = Path.Combine(context.Root, foreignOwner ? "case-foreign" : "case-owned");
			await engine.InstallAsync(AssociationOptions(context, destination));
			var record = await engine.LoadRecordAsync(Marker(destination));
			var state = record.FileAssociations!;
			foreach (var value in state.Values)
			{
				value.Key = value.Key.ToUpperInvariant();
				value.Name = value.Name.ToUpperInvariant();
			}
			state.CreatedKeys = state.CreatedKeys.Select(key => key.ToUpperInvariant()).ToList();
			await File.WriteAllTextAsync(Marker(destination), JsonSerializer.Serialize(record, InstallerJson.Options));
			// The record validator accepts Windows' case-insensitive identities.
			await engine.LoadRecordAsync(Marker(destination));
			var sourceRoot = state.Values.Single(value => value.Name.Equals(OwnerMarker, StringComparison.OrdinalIgnoreCase)
				&& value.Key.Contains("VSRun.", StringComparison.OrdinalIgnoreCase)).Key;
			var sourceOpenWith = state.Values.Single(value => value.Key.EndsWith(@".vs\OpenWithProgids", StringComparison.OrdinalIgnoreCase));
			string? foreignSnapshot = null;
			if (foreignOwner)
			{
				registry.WriteString(sourceRoot, OwnerMarker, "newer-source-owner");
				registry.WriteString(PackageIconRegistryKey, OwnerMarker, "newer-package-owner");
				foreignSnapshot = JsonSerializer.Serialize(registry.Values.Where(pair =>
					pair.Key.StartsWith(sourceRoot + "\\", StringComparison.OrdinalIgnoreCase)
					|| pair.Key.StartsWith(sourceRoot + "\0", StringComparison.OrdinalIgnoreCase)
					|| pair.Key.Equals(ValueId(sourceOpenWith.Key, sourceOpenWith.Name), StringComparison.OrdinalIgnoreCase)
					|| pair.Key.StartsWith(PackageIconRegistryKey + "\0", StringComparison.OrdinalIgnoreCase)).OrderBy(pair => pair.Key).ToArray());
			}
			await engine.UninstallAsync(Marker(destination));
			Expect(foreignOwner ? registry.Snapshot() == foreignSnapshot : registry.Values.Count == 0,
				"Registry identity casing changed ownership cleanup or removed another owner's values.");
		}
	});
	private static OwnedAssociationValue OwnedValue(InstallationRecord record, string suffix, string name = "") => record.FileAssociations!.Values.Single(value => value.Key.EndsWith(suffix, StringComparison.OrdinalIgnoreCase) && value.Name == name);
	private static async Task AssociationsOptInAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "Runtime 空格 & test");
		await engine.InstallAsync(AssociationOptions(context, destination, new()));
		Expect(registry.Values.Count == 0 && (await engine.LoadRecordAsync(Marker(destination))).FileAssociations is null, "Associations were not opt-in.");
		var result = await engine.InstallAsync(AssociationOptions(context, destination));
		var record = await engine.LoadRecordAsync(Marker(destination));
		Expect(record.FileAssociations!.Values.All(value => value.Owned) && result.DefaultApplicationName is not null, "Complete handler registration was not recorded.");
		var commands = record.FileAssociations.Values.Where(value => value.Key.EndsWith(@"\command", StringComparison.Ordinal)).ToArray();
		Expect(commands.Length == 2 && commands.All(value => registry.Read(value.Key, value.Name)?.Data == $"\"{Path.Combine(destination, "veroscript.exe")}\" \"%1\""), "Run commands do not quote the executable and file path.");
		foreach (var icon in record.FileAssociations.Values.Where(value => value.Key.EndsWith(@"\DefaultIcon", StringComparison.Ordinal) && value.Name == ""))
		{
			Expect(File.Exists(icon.Value.Split('"')[1]) && icon.Value.StartsWith("\"" + destination, StringComparison.Ordinal), "An icon points outside the installed Runtime.");
		}
		Expect(!registry.Values.Keys.Any(key => key.Contains("UserChoice", StringComparison.OrdinalIgnoreCase))
			&& registry.Read(@"Software\Classes\.vs", "") is null && registry.Read(@"Software\Classes\.vsapp", "") is null
			&& registry.Read(@"Software\Classes\.vspkg", "") is null, "Registration changed extension defaults.");
		Expect(!registry.Values.Keys.Any(key => key.Contains(".vspkg\\shell", StringComparison.OrdinalIgnoreCase)), "Package icon registered an install command.");
		Expect(DefaultAppsSettings.BuildUri(result.DefaultApplicationName!).StartsWith("ms-settings:defaultapps?registeredAppUser=VeroScript%20Runtime", StringComparison.Ordinal), "Default app URI is not escaped.");
		await engine.UninstallAsync(Marker(destination));
		Expect(registry.Values.Count == 0 && registry.Keys.All(key => key == @"Software\RegisteredApplications") && registry.Notifications >= 2, "Uninstall retained owned registrations/keys or failed to notify the shell.");
	});

	private static async Task AssociationsOptOutAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		registry.AddKey(@"Software\Classes\.vs");
		registry.WriteString(@"Software\Classes\.vs", "custom", "unrelated");
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		await engine.InstallAsync(AssociationOptions(context, destination));
		var before = await engine.LoadRecordAsync(Marker(destination));
		await engine.InstallAsync(AssociationOptions(context, destination, new() { Application = true }));
		var current = await engine.LoadRecordAsync(Marker(destination));
		Expect(current.FileAssociations!.OwnerId == before.FileAssociations!.OwnerId, "Update changed association identity.");
		Expect(!registry.Values.Keys.Any(key => key.Contains("VSRun.", StringComparison.Ordinal))
			&& registry.Read(PackageIconRegistryKey, "") is null && registry.Values.Keys.Any(key => key.Contains("VSApp.", StringComparison.Ordinal)), "Opt-out removed the wrong association.");
		await engine.InstallAsync(AssociationOptions(context, destination, new()));
		Expect((await engine.LoadRecordAsync(Marker(destination))).FileAssociations is null && registry.Values.Count == 1
			&& registry.Read(@"Software\Classes\.vs", "custom")?.Data == "unrelated", "Disabling all associations retained owned values or removed other settings.");
		await engine.UninstallAsync(Marker(destination));
		Expect(registry.KeyExists(@"Software\Classes\.vs"), "Uninstall removed a pre-existing key.");
	});

	private static async Task AssociationsCompetingAppAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		registry.WriteString(@"Software\Classes\.vs", "", "Existing.Editor");
		registry.WriteString(@"Software\Classes\.vs\OpenWithProgids", "Existing.Editor", "");
		registry.WriteString(@"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.vs\UserChoice", "ProgId", "Existing.Editor");
		registry.WriteString(PackageIconRegistryKey, "", "existing-package.ico,0");
		var snapshot = registry.Snapshot();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		var result = await engine.InstallAsync(AssociationOptions(context, destination));
		Expect(result.AssociationSettingsPreserved && result.DefaultApplicationName is not null, "A competing package icon should not prevent the run handlers.");
		await engine.UninstallAsync(Marker(destination));
		Expect(registry.Snapshot() == snapshot, "Competing defaults, OpenWith entries, or icon were modified.");
	});

	private static async Task AssociationsRepairAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		await engine.InstallAsync(AssociationOptions(context, destination));
		var record = await engine.LoadRecordAsync(Marker(destination));
		var command = record.FileAssociations!.Values.First(value => value.Key.EndsWith(@"\command", StringComparison.Ordinal));
		var icon = record.FileAssociations.Values.First(value => value.Key.EndsWith(@"\DefaultIcon", StringComparison.Ordinal) && value.Name == "");
		registry.WriteString(command.Key, "", "user custom command");
		registry.Values[ValueId(icon.Key, "")] = new("ExpandString", "%USERPROFILE%\\custom.ico");
		File.Delete(Path.Combine(destination, "veroscript.exe"));
		var repaired = await engine.RepairAsync(Marker(destination), context.ManifestPath, context.Bundle);
		Expect(repaired.AssociationSettingsPreserved && File.Exists(Path.Combine(destination, "veroscript.exe"))
			&& registry.Read(command.Key, "")?.Data == "user custom command" && registry.Read(icon.Key, "")?.Kind == "ExpandString", "Repair overwrote a user edit.");
		registry.DeleteValue(command.Key, "");
		await engine.RepairAsync(Marker(destination), context.ManifestPath, context.Bundle);
		Expect(registry.Read(command.Key, "") is null, "Repair reclaimed relinquished ownership.");
		await engine.UninstallAsync(Marker(destination));
		Expect(registry.Read(icon.Key, "")?.Kind == "ExpandString" && registry.Values.Count == 1, "Uninstall removed a changed value or retained owned values.");
	});

	private static async Task AssociationsSideBySideAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var first = Path.Combine(context.Root, "one");
		var second = Path.Combine(context.Root, "two");
		await engine.InstallAsync(AssociationOptions(context, first));
		await engine.InstallAsync(AssociationOptions(context, second));
		var record1 = await engine.LoadRecordAsync(Marker(first));
		var record2 = await engine.LoadRecordAsync(Marker(second));
		Expect(record1.FileAssociations!.OwnerId != record2.FileAssociations!.OwnerId, "Two installations share association ownership.");
		var secondValues = record2.FileAssociations.Values.Where(value => value.Owned).ToArray();
		// A later writer takes ownership while retaining the exact old icon path.
		registry.WriteString(PackageIconRegistryKey, OwnerMarker, "newer-installation");
		var iconValue = registry.Read(PackageIconRegistryKey, "");
		await engine.UninstallAsync(Marker(first));
		Expect(secondValues.All(value => registry.Read(value.Key, value.Name) == AssociationRegistryValue.String(value.Value))
			&& registry.Read(PackageIconRegistryKey, "") == iconValue && registry.Read(PackageIconRegistryKey, OwnerMarker)?.Data == "newer-installation", "Uninstall deleted another installation's settings.");
		await engine.UninstallAsync(Marker(second));
		Expect(registry.Values.Count == 2, "A later-owned icon registration was cleaned by the wrong installation.");
	});

	private static async Task AssociationsRelocationAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var first = Path.Combine(context.Root, "old");
		var second = Path.Combine(context.Root, "new 中文");
		await engine.InstallAsync(AssociationOptions(context, first));
		var snapshot = registry.Snapshot();
		var notifiedBeforeIconRestore = false;
		registry.OnNotify = () =>
		{
			foreach (var pair in registry.Values.Where(pair => pair.Key.EndsWith("\\DefaultIcon\0", StringComparison.OrdinalIgnoreCase)))
			{
				if (!File.Exists(pair.Value.Data.Split('"')[1]))
				{
					notifiedBeforeIconRestore = true;
				}
			}
		};
		using var cts = new CancellationTokenSource();
		await ExpectThrowsAsync<OperationCanceledException>(() => engine.InstallAsync(AssociationOptions(context, second, existing: Marker(first)),
			new ImmediateProgress(value =>
		{
			if (value.Stage == "associations")
			{
				cts.Cancel();
			}
		}), cts.Token));
		Expect(registry.Snapshot() == snapshot && File.Exists(Marker(first)) && !File.Exists(Marker(second)), "Cancelled relocation did not restore paths and registrations.");
		Expect(!notifiedBeforeIconRestore, "The shell was notified before the rolled-back icon files were restored.");
		await engine.InstallAsync(AssociationOptions(context, second, existing: Marker(first)));
		Expect(registry.Values.Values.All(value => !value.Data.Contains(first, StringComparison.Ordinal)) && File.Exists(Marker(second)), "Relocation retained old command/icon paths.");
		await engine.UninstallAsync(Marker(second));
	});

	private static async Task AssociationsFailureAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		foreach (var afterWrite in new[] { false, true })
		{
			registry.FailAfter = 5;
			registry.FailAfterWrite = afterWrite;
			await ExpectThrowsAsync<IOException>(() => engine.InstallAsync(AssociationOptions(context, destination)));
			Expect(registry.Values.Count == 0 && !File.Exists(Marker(destination)), "A partial registration failure was not rolled back.");
		}
		await engine.InstallAsync(AssociationOptions(context, destination));
		var snapshot = registry.Snapshot();
		registry.FailAfter = 6;
		await ExpectThrowsAsync<IOException>(() => engine.UninstallAsync(Marker(destination)));
		Expect(registry.Snapshot() == snapshot && File.Exists(Marker(destination)), "A partial uninstall did not restore registrations and files.");
		await engine.UninstallAsync(Marker(destination));
		Expect(registry.Values.Count == 0, "Retry after failed uninstall retained owned values.");
	});

	private static async Task AssociationsCancellationAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		string? edited = null;
		using var cts = new CancellationTokenSource();
		await ExpectThrowsAsync<OperationCanceledException>(() => engine.InstallAsync(AssociationOptions(context, destination), new ImmediateProgress(value =>
		{
			if (value.Stage != "associations") return;
			edited = registry.Values.Keys.First(key => key.Contains(@"\command", StringComparison.Ordinal));
			registry.Values[edited] = AssociationRegistryValue.String("concurrent user command");
			cts.Cancel();
		}), cts.Token));
		Expect(registry.Values.Count == 1 && registry.Values[edited!].Data == "concurrent user command" && !File.Exists(Marker(destination)), "Cancellation lost a later user edit or left owned settings.");
	});

	private static async Task AssociationsRecoveryAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var path = new FakePathIntegration();
		foreach (var commit in new[] { false, true })
		{
			var destination = Path.Combine(context.Root, commit ? "committed" : "interrupted");
			var id = Guid.NewGuid().ToString("N");
			var stage = Path.Combine(context.Root, $".{Path.GetFileName(destination)}.veroscript-stage-{id}");
			var backup = Path.Combine(context.Root, $".{Path.GetFileName(destination)}.veroscript-backup-{id}");
			Directory.CreateDirectory(stage);
			await WriteAsync(Path.Combine(stage, "generation.txt"), "new");
			var plan = FileAssociationRegistration.Prepare(registry, AllAssociations, destination, null);
			var transaction = await InstallerTransaction.BeginAsync(id, "install", [new(destination, destination, stage, backup, false)], CancellationToken.None);
			await transaction.RecordFileAssociationsIntentAsync(plan.Change!, CancellationToken.None);
			FileAssociationRegistration.Apply(registry, plan.Change!, CancellationToken.None);
			Directory.Move(stage, destination);
			if (commit)
			{
				await transaction.MarkCommittedAsync(CancellationToken.None);
			}
			await transaction.DisposeAsync();
			await InstallerTransaction.RecoverForRootsAsync([destination], path, CancellationToken.None, registry);
			Expect(commit ? registry.Values.Count > 0 && Directory.Exists(destination) : registry.Values.Count == 0 && !Directory.Exists(destination), "Crash recovery handled associations differently from the committed file generation.");
			if (commit)
			{
				FileAssociationRegistration.Apply(registry, FileAssociationRegistration.Prepare(registry, new(), null, plan.Record).Change!, CancellationToken.None);
			}
		}
		Expect(!Directory.EnumerateFiles(context.Root, ".veroscript-transaction-*.json").Any(), "Recovery retained a completed journal.");
	});

	private static async Task AssociationsInvalidRecordAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		await engine.InstallAsync(AssociationOptions(context, destination));
		var record = await engine.LoadRecordAsync(Marker(destination));
		var snapshot = registry.Snapshot();
		record.FileAssociations!.Values[0].Key = @"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.vs\UserChoice";
		await File.WriteAllTextAsync(Marker(destination), JsonSerializer.Serialize(record, InstallerJson.Options));
		await ExpectThrowsAsync<InvalidDataException>(() => engine.UninstallAsync(Marker(destination)));
		Expect(registry.Snapshot() == snapshot, "Forged ownership caused a registry mutation.");
		var badChange = new FileAssociationChange { OwnerId = Guid.NewGuid().ToString("N"), PruneKeys = [@"Software\Classes"] };
		await ExpectThrowsAsync<InvalidDataException>(() =>
		{
			FileAssociationRegistration.Undo(registry, badChange);
			return Task.CompletedTask;
		});
	});

	private static async Task AssociationsLegacyAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		await engine.InstallAsync(AssociationOptions(context, destination, new()));
		registry.WriteString(PackageIconRegistryKey, "", "legacy.ico,0");
		var snapshot = registry.Snapshot();
		await engine.UninstallAsync(Marker(destination));
		Expect(snapshot == registry.Snapshot(), "Historical records inferred icon ownership from files.");
	});

	private static async Task AssociationsUnsupportedAsync() => await WithAssociationBundle(async context =>
	{
		var registry = new MemoryAssociationRegistry();
		var engine = new InstallerEngine(new FakePathIntegration(), registry);
		var destination = Path.Combine(context.Root, "installed");
		var manifest = await ManifestValidator.LoadAsync(context.ManifestPath);
		manifest.Components.Single(component => component.Id == "runtime").Files.RemoveAll(file => file.Path.EndsWith("veroscript-app.ico", StringComparison.Ordinal));
		await File.WriteAllTextAsync(context.ManifestPath, JsonSerializer.Serialize(manifest, InstallerJson.Options));
		await ExpectThrowsAsync<InvalidDataException>(() => engine.InstallAsync(AssociationOptions(context, destination)));
		Expect(!Directory.Exists(destination) && registry.Values.Count == 0, "Missing association resources were rejected after modifying installation state.");
		var unsupported = new InstallerEngine(new FakePathIntegration(), new UnsupportedFileAssociationRegistry());
		await ExpectThrowsAsync<PlatformNotSupportedException>(() => unsupported.InstallAsync(AssociationOptions(context, destination)));
	});

	private static Task AssociationRegistryAdapterAsync()
	{
		if (!OperatingSystem.IsWindows()) return Task.CompletedTask;
		var path = @"Software\VeroScriptInstaller.Tests\" + Guid.NewGuid().ToString("N");
		using var root = Registry.CurrentUser.CreateSubKey(path);
		try
		{
			using var registry = new WindowsFileAssociationRegistry(root);
			var plan = FileAssociationRegistration.Prepare(registry, AllAssociations, Path.GetFullPath(Path.Combine(Path.GetTempPath(), "Runtime 中文 空格")), null);
			FileAssociationRegistration.Apply(registry, plan.Change!, CancellationToken.None);
			Expect(registry.Read(PackageIconRegistryKey, "")?.Data.Contains("中文 空格", StringComparison.Ordinal) == true, "Windows adapter lost Unicode path data.");
			using (var extra = root.CreateSubKey(@"Software\Classes\.vs"))
			{
				extra.SetValue("Keep", 42, RegistryValueKind.DWord);
			}
			FileAssociationRegistration.Apply(registry, FileAssociationRegistration.Prepare(registry, new(), null, plan.Record).Change!, CancellationToken.None);
			Expect(registry.KeyExists(@"Software\Classes\.vs") && registry.Read(@"Software\Classes\.vs", "Keep")?.Kind == "DWord"
				&& !registry.KeyExists(PackageIconRegistryKey), "Real registry cleanup removed unrelated values or retained empty owned keys.");
		}
		finally
		{
			// The absolute test subtree is fixed and contains only this test's generated GUID.
			Registry.CurrentUser.DeleteSubKeyTree(path, throwOnMissingSubKey: false);
		}
		return Task.CompletedTask;
	}

	private sealed class ImmediateProgress(Action<InstallProgress> handler) : IProgress<InstallProgress>
	{
		public void Report(InstallProgress value) => handler(value);
	}

	private sealed class MemoryAssociationRegistry : IFileAssociationRegistry
	{
		internal Dictionary<string, AssociationRegistryValue> Values { get; } = new(StringComparer.OrdinalIgnoreCase);
		internal HashSet<string> Keys { get; } = new(StringComparer.OrdinalIgnoreCase);
		internal int Notifications { get; private set; }
		internal int FailAfter { get; set; } = -1;
		internal bool FailAfterWrite { get; set; }
		internal Action? OnNotify { get; set; }
		public bool IsSupported => true;
		public bool KeyExists(string key) => Keys.Contains(key);
		public AssociationRegistryValue? Read(string key, string name) => Values.GetValueOrDefault(ValueId(key, name));
		internal void AddKey(string key) => Keys.Add(key);
		private void MayFail()
		{
			if (FailAfter < 0) return;
			if (--FailAfter == 0)
			{
				FailAfter = -1;
				throw new IOException("Injected registry failure.");
			}
		}
		public void WriteString(string key, string name, string value)
		{
			if (!FailAfterWrite)
			{
				MayFail();
			}
			Keys.Add(key);
			Values[ValueId(key, name)] = AssociationRegistryValue.String(value);
			if (FailAfterWrite)
			{
				MayFail();
			}
		}
		public void DeleteValue(string key, string name)
		{
			if (!FailAfterWrite)
			{
				MayFail();
			}
			Values.Remove(ValueId(key, name));
			if (FailAfterWrite)
			{
				MayFail();
			}
		}
		public void DeleteKeyIfEmpty(string key)
		{
			if (!Values.Keys.Any(id => id.StartsWith(key + "\0", StringComparison.OrdinalIgnoreCase))
				&& !Keys.Any(child => child.StartsWith(key + "\\", StringComparison.OrdinalIgnoreCase)))
			{
				Keys.Remove(key);
			}
		}
		public void NotifyChanged()
		{
			Notifications++;
			OnNotify?.Invoke();
		}
		internal string Snapshot() => JsonSerializer.Serialize(Values.OrderBy(pair => pair.Key).ToArray());
	}
}
