// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core;
using VeroScriptInstaller.Core.Platform;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json.Nodes;

namespace VeroScriptInstaller.Tests;
internal static partial class Program
{
	private static readonly string[] StandardLibraryModules = ["algorithm.vs", "concurrent.vs", "core.vs", "delegate.vs", "io.vs", "math.vs", "numeric.vs", "path.vs", "random.vs", "string.vs", ];
	public static async Task<int> Main()
	{
		var tests = new (string Name, Func<Task> Run)[]
		{
			("association-registry-identities-are-case-insensitive", AssociationRegistryCaseInsensitiveAsync),
			("associations-explicit-opt-in-and-quoted-handlers", AssociationsOptInAsync),
			("associations-update-removes-only-disabled-owned-values", AssociationsOptOutAsync),
			("associations-preserve-competing-app-and-package-icon", AssociationsCompetingAppAsync),
			("associations-repair-preserves-user-edits-and-tombstones", AssociationsRepairAsync),
			("associations-independent-installations-and-new-owner", AssociationsSideBySideAsync),
			("associations-relocation-is-reversible", AssociationsRelocationAsync),
			("associations-partial-write-and-uninstall-failures-roll-back", AssociationsFailureAsync),
			("associations-cancellation-preserves-concurrent-user-edit", AssociationsCancellationAsync),
			("associations-crash-recovery-and-committed-finalization", AssociationsRecoveryAsync),
			("associations-records-reject-forged-ownership", AssociationsInvalidRecordAsync),
			("associations-historical-records-never-infer-ownership", AssociationsLegacyAsync),
			("associations-missing-resources-and-unsupported-platform", AssociationsUnsupportedAsync),
			("windows-registry-adapter-uses-isolated-test-root", AssociationRegistryAdapterAsync),
			("modules-do-not-provide-installers", ModulesDoNotProvideInstallersAsync),
			("foreign-bundles-can-be-built-and-audited-but-not-installed", CrossTargetBundleAsync),
			("functional-installer-omits-docs-and-examples", FunctionalInstallerOmitsAuxiliaryContentAsync),
			("exception-assertions-reject-no-throw", ExceptionAssertionsRejectNoThrowAsync),
			("linux-install-root-honors-absolute-xdg", LinuxInstallRootHonorsXdgAsync),
			("unix-python-requires-native-interop", UnixPythonRequiresNativeInteropAsync),
			("install-two-components-without-path", InstallTwoComponentsWithoutPathAsync),
			("path-is-explicit-opt-in-and-uninstalled", PathIsExplicitOptInAsync),
			("owned-path-can-be-removed-during-update", OwnedPathCanBeRemovedDuringUpdateAsync),
			("runtime-relocation-moves-owned-path", RuntimeRelocationMovesOwnedPathAsync),
			("runtime-relocation-rolls-back-path-on-failure", RuntimeRelocationRollsBackPathAsync),
			("failed-uninstall-restores-files-and-path", FailedUninstallRestoresFilesAndPathAsync),
			("interrupted-transactions-recover-or-finalize", InterruptedTransactionsRecoverOrFinalizeAsync),
			("record-load-recovers-interrupted-transaction", RecordLoadRecoversInterruptedTransactionAsync),
			("relocation-recovers-from-old-record-root", RelocationRecoversFromOldRecordRootAsync),
			("relocation-rejects-another-components-directory", RelocationRejectsAnotherComponentsDirectoryAsync),
			("repair-restores-files-and-path-ownership", RepairRestoresFilesAndPathOwnershipAsync),
			("failed-repair-preserves-installed-generation", FailedRepairPreservesInstallationAsync),
			("tampered-payload-is-rejected", TamperedPayloadIsRejectedAsync),
			("unmanaged-destination-is-preserved", UnmanagedDestinationIsPreservedAsync),
			("destination-cannot-overlap-installer", DestinationCannotOverlapInstallerAsync),
			("tampered-path-ownership-is-rejected", TamperedPathOwnershipIsRejectedAsync),
			("unsafe-manifest-path-is-rejected", UnsafeManifestPathIsRejectedAsync),
			("runtime-requires-region-tagged-localization", RegionTaggedLocalizationRequiredAsync),
			("runtime-requires-complete-standard-library", CompleteStandardLibraryRequiredAsync),
			("runtime-requires-explicit-vm", RuntimeVmRequiredAsync),
			("bundle-roots-cannot-overlap", BundleRootsCannotOverlapAsync),
			("bundle-excludes-local-workspace-settings", BundleExcludesLocalWorkspaceSettingsAsync),
			("downgrade-requires-explicit-opt-in", DowngradeRequiresOptInAsync),
			("repair-requires-exact-package", RepairRequiresExactPackageAsync),
			("multi-component-updates-are-atomic", MultiComponentUpdatesAreAtomicAsync),
			("null-manifest-collections-are-rejected", NullManifestCollectionsAreRejectedAsync),
			("upgrade-preserves-existing-path-ownership", UpgradePreservesPathOwnershipAsync),
			("upgrade-preserves-user-owned-files", UpgradePreservesUserOwnedFilesAsync),
			("same-version-update-restores-executable-permissions", SameVersionUpdateRestoresExecutablePermissionsAsync),
			("upgrade-rejects-new-payload-user-file-conflicts", UpgradeRejectsUserFileConflictAsync),
			("windows-commit-retries-transient-locks", WindowsCommitRetriesTransientLocksAsync),
			("unix-path-block-supports-multiple-installs", UnixPathBlockSupportsMultipleInstallsAsync),
			("windows-path-edit-preserves-existing-text", WindowsPathEditPreservesExistingTextAsync),
			("embedded-bundle-round-trips-and-verifies", EmbeddedBundleRoundTripsAsync),
			("single-file-content-is-bound-to-installer", SingleFileContentIsBoundAsync),
			("single-file-content-seeks-across-parts", SingleFileContentSeeksAcrossPartsAsync),
			("single-file-content-rejects-links", SingleFileContentRejectsLinksAsync),
			("embedded-bundle-rejects-path-traversal", EmbeddedBundleRejectsPathTraversalAsync),
			("embedded-bundle-rejects-linked-destination", EmbeddedBundleRejectsLinkedDestinationAsync),
			("sdk-product-installs-independent-components", SdkProductInstallsIndependentComponentsAsync),
			("sdk-product-installs-docs-component", SdkProductInstallsDocsComponentAsync),
			("different-products-cannot-replace-each-other", DifferentProductsCannotReplaceEachOtherAsync),
			("product-catalog-presets-are-independent", ProductCatalogPresetsAreIndependentAsync),
			("versioned-layout-and-runtime-paths-coexist", VersionedLayoutAndRuntimePathsCoexistAsync),
			("custom-product-definition-builds-and-installs", CustomProductDefinitionBuildsAndInstallsAsync),
			("optional-component-removal-preserves-user-files", OptionalComponentRemovalPreservesUserFilesAsync),
		};
		try
		{
			foreach (var test in tests)
			{
				await test.Run();
				Console.WriteLine($"PASS {test.Name}");
			}

			return 0;
		}
		catch (Exception exception)
		{
			Console.Error.WriteLine(exception);
			return 1;
		}
	}

	private static async Task BundleExcludesLocalWorkspaceSettingsAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-workspace-filter-{Guid.NewGuid():N}");
		try
		{
			var source = Path.Combine(root, "source");
			await WriteAsync(Path.Combine(source, "tool.txt"), "custom");
			await WriteAsync(Path.Combine(source, ".veroscript", "workspace.json"), "{}");
			await WriteAsync(Path.Combine(source, "examples", ".veroscript", "workspace.json"), "{}");
			await WriteAsync(Path.Combine(source, "examples", "keep.txt"), "keep");
			await WriteAsync(Path.Combine(source, ".veroscript-notes", "keep.txt"), "keep");
			var bundle = Path.Combine(root, "bundle");
			var manifestPath = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0", new Dictionary<string, string> { ["core"] = source }, productDefinition: CreateCustomDefinition());
			var manifest = await ManifestValidator.LoadAsync(manifestPath);
			await ManifestValidator.VerifyPayloadAsync(manifest, bundle);
			Expect(!Directory.EnumerateDirectories(bundle, ".veroscript", SearchOption.AllDirectories).Any(), "Bundle contains local workspace settings.");
			var payload = Path.Combine(bundle, "payload", "core");
			Expect(File.Exists(Path.Combine(payload, "examples", "keep.txt")) && File.Exists(Path.Combine(payload, ".veroscript-notes", "keep.txt")), "Workspace filtering removed unrelated resources.");
			Expect(File.Exists(Path.Combine(source, ".veroscript", "workspace.json")), "Packaging must not delete source workspace settings.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task FunctionalInstallerOmitsAuxiliaryContentAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-functional-installer-{Guid.NewGuid():N}");
		try
		{
			var source = Path.Combine(root, "source");
			foreach (var name in new[]
			{
				"tool.txt",
				"README.md",
				"licenses/vendor/LICENSE.md",
				"assets/en-US.json",
				"Docs/manual.md",
				"Standards/PACKAGING.md",
				"Examples/main.vs"
			}

			)
			{
				await WriteAsync(Path.Combine(source, name), name);
			}

			var bundle = Path.Combine(root, "bundle");
			var path = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0", new Dictionary<string, string> { ["core"] = source, ["docs"] = source, ["examples"] = source }, productDefinition: CreateCustomDefinition(), functionalPayloadOnly: true);
			var manifest = await ManifestValidator.LoadAsync(path);
			Expect(manifest.Components.Count == 1 && manifest.Components[0].Id == "core", "Auxiliary options remain.");
			var payload = Path.Combine(bundle, "payload/core");
			Expect(!File.Exists(Path.Combine(payload, "README.md")) && File.Exists(Path.Combine(payload, "licenses/vendor/LICENSE.md")), "README remains or legal notices were removed.");
			Expect(new[] { "Docs", "Examples", "Standards" }.All(name => !Directory.Exists(Path.Combine(payload, name))), "Auxiliary content remains in a functional payload.");
			Expect(File.Exists(Path.Combine(source, "Docs/manual.md")), "Source documentation was modified.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task ModulesDoNotProvideInstallersAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-module-policy-{Guid.NewGuid():N}");
		var sources = new Dictionary<string, string>
		{
			["jit-backend"] = Path.Combine(root, "source")
		};
		await ExpectThrowsAsync<InvalidOperationException>(() => InstallerBundleBuilder.BuildAsync(Path.Combine(root, "jit"), "1.0.0", sources, profileId: "jit-asmjit"));
		var future = InstallerProductCatalog.Get("runtime")with
		{
			ProfileId = "future-module",
			ProductId = "dev.veroscript.future",
			ReleaseGroup = "Modules",
		};
		await ExpectThrowsAsync<InvalidOperationException>(() => InstallerBundleBuilder.BuildAsync(Path.Combine(root, "future"), "1.0.0", sources, productDefinition: future));
		Expect(!Directory.Exists(root), "Rejected module packaging must not create output directories.");
		Expect(InstallerProductCatalog.Get("jit-asmjit").Components.ContainsKey("jit-backend"), "Historical JIT profiles must remain readable.");
	}

	private static async Task VersionedLayoutAndRuntimePathsCoexistAsync()
	{
		Expect(InstallerProductCatalog.Get("core-sdk").Components["sdk"].InstallDirectory == "SDK/Core/{version}", "Core SDK layout");
		Expect(InstallerProductCatalog.Get("jit-asmjit").Components["jit-backend"].InstallDirectory == "Jit/asmjit/{version}", "JIT layout");
		Expect(InstallerProductCatalog.All.Where(product => product.ProfileId != "runtime").All(product => product.PathComponentId is null), "Only Runtime may own PATH");
		await WithBundleAsync(async context =>
		{
			var paths = new FakePathIntegration();
			var engine = new InstallerEngine(paths);
			var records = new List<string>();
			var destinations = new List<string>();
			foreach (var version in new[]
			{
				"0.1.1",
				"0.1.2"
			}

			)
			{
				var bundle = Path.Combine(context.Root, "bundle-" + version);
				var manifestPath = await InstallerBundleBuilder.BuildAsync(bundle, version, new Dictionary<string, string> { ["runtime"] = context.RuntimeSource }, profileId: "runtime");
				var manifest = await ManifestValidator.LoadAsync(manifestPath);
				var component = manifest.Components.Single();
				Expect(component.InstallDirectory == "Runtime/" + version, "Version placeholder was not rendered");
				var destination = Path.Combine(context.Root, "chosen-drive", component.InstallDirectory);
				var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = manifestPath, PayloadRoot = bundle, SelectedComponents = new HashSet<string> { "runtime" }, ComponentDestinations = new Dictionary<string, string> { ["runtime"] = destination }, AddToUserPath = true, });
				records.Add(installed.InstallRecordPaths.Single());
				destinations.Add(destination);
			}

			Expect(destinations.All(paths.Contains), "Multiple Runtime paths did not coexist");
			await engine.UninstallAsync(records[0]);
			Expect(!paths.Contains(destinations[0]) && paths.Contains(destinations[1]) && File.Exists(Path.Combine(destinations[1], ToolName("veroscript"))), "Uninstall affected another Runtime version");
		});
	}

	private static Task ProductCatalogPresetsAreIndependentAsync()
	{
		var profiles = InstallerProductCatalog.All;
		Expect(profiles.Count == 8, "Installer catalog must contain the eight supported products.");
		Expect(profiles.Select(profile => profile.ProfileId).Distinct(StringComparer.Ordinal).Count() == profiles.Count, "Installer profile ids are not unique.");
		Expect(profiles.Select(profile => profile.ProductId).Distinct(StringComparer.Ordinal).Count() == profiles.Count, "Installer product ids are not independent.");
		foreach (var requiredProfile in new[]
		{
			"runtime",
			"interop",
			"core-sdk",
			"cpp",
			"csharp",
			"python",
			"web"
		}

		)
		{
			Expect(profiles.Any(profile => profile.ProfileId == requiredProfile), $"Missing profile '{requiredProfile}'.");
		}

		Expect(InstallerProductCatalog.Get("runtime").PathComponentId == "runtime", "Runtime profile does not declare PATH ownership.");
		var sdk = InstallerProductCatalog.Get("core-sdk");
		Expect(sdk.Components.TryGetValue("examples", out var sdkExamples) && !sdkExamples.Required && !sdkExamples.DefaultSelected, "Core SDK examples must be an optional component.");
		if (OperatingSystem.IsWindows())
		{
			var cppRuntime = InstallerProductCatalog.Get("cpp").Components["cpp-runtime"];
			Expect(cppRuntime.RequiredFiles?.Contains("include/Cpp/VeroScriptSession.h", StringComparer.Ordinal) == true, "The C++ installer does not require the public session API header.");
			Expect(cppRuntime.RequiredFiles?.Contains("lib/cmake/VeroScriptCpp/VeroScriptCppConfig.cmake", StringComparer.Ordinal) == true, "The C++ installer does not require its CMake package configuration.");
			var cppTools = InstallerProductCatalog.Get("cpp").Components["cpp-tools"];
			foreach (var requiredTool in new[]
			{
				"include/CppFrontend/VeroScriptCppFrontend.h",
				"include/CppBindGen/VeroScriptCppBindGen.h",
				"include/CppTranspiler/VeroScriptCppTranspiler.h",
				"lib/VeroScriptCppFrontend.lib",
				"lib/VeroScriptCppBindGenLib.lib",
				"lib/VeroScriptCppTranspilerLib.lib",
				"bin/VeroScriptCppBindGen.exe",
				"bin/VeroScriptCppTranspiler.exe",
			}

			)
			{
				Expect(cppTools.RequiredFiles?.Contains(requiredTool, StringComparer.Ordinal) == true, $"The C++ installer does not require '{requiredTool}'.");
			}

			var csharpRuntime = InstallerProductCatalog.Get("csharp").Components["csharp-runtime"];
			Expect(csharpRuntime.RequiredFiles?.Any(path => path.EndsWith("VeroScriptInterop.dll", StringComparison.Ordinal)) == true, "The C# installer does not require its native Interop runtime.");
			var pythonRuntime = InstallerProductCatalog.Get("python").Components["python-runtime"];
			Expect(pythonRuntime.RequiredFiles?.Contains("Lib/site-packages/veroscript/_veroscript.pyi", StringComparer.Ordinal) == true, "The Python installer does not require its public type stub.");
			var coreSdk = InstallerProductCatalog.Get("core-sdk").Components["sdk"];
			foreach (var requiredHeader in new[]
			{
				"VeroScriptJit.h",
				"VeroScriptRun.h",
				"VeroScriptValue.h",
				"VeroScriptVm.h"
			}

			)
			{
				Expect(coreSdk.RequiredFiles?.Contains("include/Script/" + requiredHeader, StringComparer.Ordinal) == true, $"The Core SDK installer does not require {requiredHeader}.");
			}
		}

		return Task.CompletedTask;
	}

	private static Task LinuxInstallRootHonorsXdgAsync()
	{
		if (!OperatingSystem.IsLinux())
		{
			return Task.CompletedTask;
		}

		var previous = Environment.GetEnvironmentVariable("XDG_DATA_HOME");
		try
		{
			var data = Path.Combine(Path.GetTempPath(), "veroscript-xdg-test");
			Environment.SetEnvironmentVariable("XDG_DATA_HOME", data);
			Expect(PlatformInfo.GetDefaultRuntimeInstallDirectory() == Path.Combine(data, "VeroScript"), "Runtime installation ignored XDG_DATA_HOME.");
			var root = new InstallerRootDefinition
			{
				Kind = InstallerRootKinds.Developer,
				DirectoryName = "SDK",
				IncludeVersion = true
			};
			Expect(PlatformInfo.GetDefaultInstallDirectory(root, "1.0.0") == Path.Combine(data, "SDK", "1.0.0"), "Product installation ignored XDG_DATA_HOME.");
			Environment.SetEnvironmentVariable("XDG_DATA_HOME", "relative-data");
			Expect(Path.IsPathFullyQualified(PlatformInfo.GetUserDataDirectory()) && PlatformInfo.GetUserDataDirectory().EndsWith(Path.Combine(".local", "share"), StringComparison.Ordinal), "Relative XDG_DATA_HOME must fall back to the established user data directory.");
		}
		finally
		{
			Environment.SetEnvironmentVariable("XDG_DATA_HOME", previous);
		}

		return Task.CompletedTask;
	}

	private static async Task UnixPythonRequiresNativeInteropAsync()
	{
		if (OperatingSystem.IsWindows())
		{
			return;
		}

		var root = Path.Combine(Path.GetTempPath(), $"veroscript-unix-payload-{Guid.NewGuid():N}");
		try
		{
			var source = Path.Combine(root, "source");
			var package = Path.Combine(source, "lib", "python3.13", "site-packages", "veroscript");
			foreach (var name in new[]
			{
				"__init__.py",
				"_veroscript.pyi",
				"py.typed",
				"_veroscript.cpython-313.so"
			}

			)
			{
				await WriteAsync(Path.Combine(package, name), "fixture");
			}

			var sources = new Dictionary<string, string>
			{
				["python-runtime"] = source
			};
			await ExpectThrowsAsync<InvalidDataException>(() => InstallerBundleBuilder.BuildAsync(Path.Combine(root, "missing-native"), "1.0.0", sources, profileId: "python"));
			var shared = OperatingSystem.IsMacOS() ? "libVeroScriptInterop.dylib" : "libVeroScriptInterop.so";
			await WriteAsync(Path.Combine(package, shared), "native");
			var manifest = await InstallerBundleBuilder.BuildAsync(Path.Combine(root, "complete"), "1.0.0", sources, profileId: "python");
			await ManifestValidator.LoadAsync(manifest);
			Expect(InstallerProductCatalog.Get("interop").Components["interop-sdk"].RequiredFiles?.Contains("lib/" + shared) == true, "Unix Interop profile must require its native library.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task CustomProductDefinitionBuildsAndInstallsAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-custom-installer-{Guid.NewGuid():N}");
		try
		{
			var source = Path.Combine(root, "source");
			await WriteAsync(Path.Combine(source, "tool.txt"), "custom");
			var definition = CreateCustomDefinition();
			var bundle = Path.Combine(root, "bundle");
			var manifestPath = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0", new Dictionary<string, string> { ["core"] = source }, productDefinition: definition);
			var manifest = await ManifestValidator.LoadAsync(manifestPath);
			Expect(manifest.ProductId == definition.ProductId && manifest.Components.Single().Required, "Custom product metadata was not preserved.");
			var destination = Path.Combine(root, "installed", "Core");
			var engine = new InstallerEngine(new FakePathIntegration());
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = manifestPath, PayloadRoot = bundle, SelectedComponents = new HashSet<string> { "core" }, ComponentDestinations = new Dictionary<string, string> { ["core"] = destination }, });
			Expect(await File.ReadAllTextAsync(Path.Combine(destination, "tool.txt")) == "custom", "Custom component was not installed.");
			var record = await engine.LoadRecordAsync(installed.InstallRecordPaths.Single());
			Expect(record.ProductName == definition.ProductName && record.LocalizedNames.TryGetValue("zh-CN", out var localizedName) && localizedName == "娴嬭瘯浜у搧", "Custom product display identity was not preserved for uninstall.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task OptionalComponentRemovalPreservesUserFilesAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-module-removal-{Guid.NewGuid():N}");
		try
		{
			var coreSource = Path.Combine(root, "core-source");
			var extrasSource = Path.Combine(root, "extras-source");
			await WriteAsync(Path.Combine(coreSource, "core.txt"), "core");
			await WriteAsync(Path.Combine(extrasSource, "owned.txt"), "owned");
			var definition = CreateCustomDefinition(includeExtras: true);
			var bundle = Path.Combine(root, "bundle");
			var manifestPath = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0", new Dictionary<string, string> { ["core"] = coreSource, ["extras"] = extrasSource }, productDefinition: definition);
			var coreDestination = Path.Combine(root, "installed", "Core");
			var extrasDestination = Path.Combine(root, "installed", "Extras");
			var engine = new InstallerEngine(new FakePathIntegration());
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = manifestPath, PayloadRoot = bundle, SelectedComponents = new HashSet<string> { "core", "extras" }, ComponentDestinations = new Dictionary<string, string> { ["core"] = coreDestination, ["extras"] = extrasDestination, }, });
			await WriteAsync(Path.Combine(extrasDestination, "user.txt"), "keep");
			var updated = await engine.InstallAsync(new InstallOptions { ManifestPath = manifestPath, PayloadRoot = bundle, SelectedComponents = new HashSet<string> { "core" }, ComponentDestinations = new Dictionary<string, string> { ["core"] = coreDestination }, ExistingInstallRecordPath = installed.InstallRecordPaths.First(path => Path.GetDirectoryName(path) == coreDestination), });
			Expect(!File.Exists(Path.Combine(extrasDestination, "owned.txt")), "Deselected optional component retained installer-owned files.");
			Expect(await File.ReadAllTextAsync(Path.Combine(extrasDestination, "user.txt")) == "keep", "Deselecting an optional component removed a user-owned file.");
			Expect(!File.Exists(Path.Combine(extrasDestination, InstallerConstants.InstallRecordFileName)), "Deselected component retained an installation marker.");
			var record = await engine.LoadRecordAsync(updated.InstallRecordPaths.Single());
			Expect(record.Components.Select(component => component.Id).SequenceEqual(["core"]), "Updated record retained a deselected optional component.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static InstallerProductDefinition CreateCustomDefinition(bool includeExtras = false)
	{
		var components = new Dictionary<string, InstallerComponentDefinition>(StringComparer.Ordinal)
		{
			["core"] = new("core", "Core", "Required core.", "Core", Required: true),
		};
		if (includeExtras)
		{
			components["extras"] = new("extras", "Extras", "Optional content.", "Extras", DefaultSelected: false);
		}

		return new InstallerProductDefinition("custom-test", "dev.veroscript.test-product", "Test Product", "Test installer product.", InstallerRootKinds.Developer, "Test Product", false, components, LocalizedNames: new Dictionary<string, string> { ["zh-CN"] = "娴嬭瘯浜у搧" });
	}

	private static async Task EmbeddedBundleRoundTripsAsync()
	{
		await WithBundleAsync(async context =>
		{
			var archivePath = Path.Combine(context.Root, "embedded-bundle.zip");
			ZipFile.CreateFromDirectory(context.Bundle, archivePath, CompressionLevel.Optimal, includeBaseDirectory: false);
			var extracted = Path.Combine(context.Root, "extracted-bundle");
			await using var archive = File.OpenRead(archivePath);
			var manifestPath = await InstallerBundleArchive.ExtractAndVerifyAsync(archive, extracted);
			Expect(File.Exists(manifestPath), "The embedded manifest was not extracted.");
			Expect(File.Exists(Path.Combine(extracted, "payload", InstallerConstants.RuntimeComponentId, ToolName("veroscript"))), "The embedded runtime payload was not extracted.");
		});
	}

	private static async Task SingleFileContentIsBoundAsync()
	{
		await WithBundleAsync(async context =>
		{
			var path = Path.Combine(context.Root, EmbeddedInstallerBundle.ResourceName + ".0000");
			ZipFile.CreateFromDirectory(context.Bundle, path, CompressionLevel.Optimal, includeBaseDirectory: false);
			string hash;
			using (var input = File.OpenRead(path))
			{
				hash = Convert.ToHexString(SHA256.HashData(input));
			}
			using (var stream = EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash))
			{
				Expect(stream.Position == 0, "Content verification did not rewind the archive.");
				var manifest = await InstallerBundleArchive.ExtractAndVerifyAsync(stream, Path.Combine(context.Root, "single-file-extracted"));
				Expect(File.Exists(manifest), "Verified single-file content could not be extracted.");
			}
			ExpectThrows<InvalidDataException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, "invalid").Dispose());
			ExpectThrows<InvalidDataException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, "AB").Dispose());
			await File.AppendAllTextAsync(path, "modified after publication");
			ExpectThrows<InvalidDataException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash).Dispose());
			File.Delete(path);
			ExpectThrows<FileNotFoundException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash).Dispose());
		});
	}

	private static async Task SingleFileContentRejectsLinksAsync()
	{
		await WithBundleAsync(async context =>
		{
			var target = Path.Combine(context.Root, "linked-payload.zip");
			await File.WriteAllTextAsync(target, "payload");
			var hash = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(target)));
			var path = Path.Combine(context.Root, EmbeddedInstallerBundle.ResourceName + ".0000");
			try
			{
				File.CreateSymbolicLink(path, target);
			}
			catch (UnauthorizedAccessException) when (OperatingSystem.IsWindows())
			{
				Console.WriteLine("SKIP linked single-file content: Windows requires symbolic-link permission.");
				return;
			}
			catch (IOException error) when (OperatingSystem.IsWindows() && (error.HResult & 0xffff) == 1314)
			{
				Console.WriteLine("SKIP linked single-file content: Windows requires symbolic-link permission.");
				return;
			}
			try
			{
				ExpectThrows<InvalidDataException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash).Dispose());
				File.Delete(target);
				ExpectThrows<InvalidDataException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash).Dispose());
			}
			finally
			{
				File.Delete(path);
			}
		});
	}

	private static async Task SingleFileContentSeeksAcrossPartsAsync()
	{
		await WithBundleAsync(async context =>
		{
			var archive = Path.Combine(context.Root, "parts-source.zip");
			ZipFile.CreateFromDirectory(context.Bundle, archive, CompressionLevel.Optimal, includeBaseDirectory: false);
			var bytes = await File.ReadAllBytesAsync(archive);
			var split = bytes.Length / 2;
			var first = Path.Combine(context.Root, EmbeddedInstallerBundle.ResourceName + ".0000");
			var second = Path.Combine(context.Root, EmbeddedInstallerBundle.ResourceName + ".0001");
			await File.WriteAllBytesAsync(first, bytes[..split]);
			await File.WriteAllBytesAsync(second, bytes[split..]);
			var hash = Convert.ToHexString(SHA256.HashData(bytes));
			using (var stream = EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash, 2))
			{
				Expect(stream.Length == bytes.Length && stream.CanSeek, "Content parts do not expose a seekable archive.");
				stream.Seek(split - 3, SeekOrigin.Begin);
				var boundary = new byte[6];
				stream.ReadExactly(boundary);
				Expect(boundary.AsSpan().SequenceEqual(bytes.AsSpan(split - 3, 6)), "Reading across content parts changed data.");
				stream.Seek(-4, SeekOrigin.End);
				Expect(stream.ReadByte() == bytes[^4], "Seeking from the archive end failed.");
				stream.Position = 0;
				await InstallerBundleArchive.ExtractAndVerifyAsync(stream, Path.Combine(context.Root, "parts-extracted"));
			}
			ExpectThrows<InvalidDataException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash, 0).Dispose());
			ExpectThrows<InvalidDataException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash, 33).Dispose());
			File.Delete(second);
			ExpectThrows<FileNotFoundException>(() => EmbeddedInstallerBundle.OpenBundledFile(context.Root, hash, 2).Dispose());
			// A failed open must release the first part as well.
			using var exclusive = new FileStream(first, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
		});
	}

	private static async Task EmbeddedBundleRejectsPathTraversalAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-archive-test-{Guid.NewGuid():N}");
		try
		{
			await using var stream = new MemoryStream();
			using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: true))
			{
				var entry = archive.CreateEntry("../escape.txt");
				await using var output = entry.Open();
				await output.WriteAsync("escape"u8.ToArray());
			}

			stream.Position = 0;
			await ExpectThrowsAsync<InvalidDataException>(() => InstallerBundleArchive.ExtractAndVerifyAsync(stream, Path.Combine(root, "bundle")));
			Expect(!File.Exists(Path.Combine(root, "escape.txt")), "Archive traversal wrote outside its root.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task EmbeddedBundleRejectsLinkedDestinationAsync()
	{
		await WithBundleAsync(async context =>
		{
			var target = Path.Combine(context.Root, "external-target");
			var destination = Path.Combine(context.Root, "linked-destination");
			Directory.CreateDirectory(target);
			try
			{
				Directory.CreateSymbolicLink(destination, target);
			}
			catch (UnauthorizedAccessException)when (OperatingSystem.IsWindows())
			{
				Console.WriteLine("SKIP linked destination: Windows requires symbolic-link permission.");
				return;
			}
			catch (IOException exception)when (OperatingSystem.IsWindows() && (exception.HResult & 0xffff) == 1314)
			{
				Console.WriteLine("SKIP linked destination: Windows requires symbolic-link permission.");
				return;
			}

			try
			{
				using var stream = new MemoryStream();
				using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, leaveOpen: true))
				{
					var entry = archive.CreateEntry("outside-write.txt");
					await using var output = entry.Open();
					await output.WriteAsync("must not be written"u8.ToArray());
				}

				stream.Position = 0;
				await ExpectThrowsAsync<InvalidDataException>(() => InstallerBundleArchive.ExtractAndVerifyAsync(stream, destination));
				Expect(!Directory.EnumerateFileSystemEntries(target).Any(), "Archive extraction wrote through a linked destination.");
				Expect(new DirectoryInfo(destination).LinkTarget is not null, "Rejected archive extraction removed the destination link.");
				Directory.Delete(target);
				stream.Position = 0;
				await ExpectThrowsAsync<InvalidDataException>(() => InstallerBundleArchive.ExtractAndVerifyAsync(stream, destination));
				Expect(!Directory.Exists(target) && new DirectoryInfo(destination).LinkTarget is not null, "Archive extraction followed or removed a dangling destination link.");
			}
			finally
			{
				if (OperatingSystem.IsWindows())
				{
					Directory.Delete(destination);
				}
				else
				{
					// Unix removes a dangling directory symlink as a file.
					File.Delete(destination);
				}
			}
		});
	}

	private static async Task SdkProductInstallsIndependentComponentsAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-sdk-installer-test-{Guid.NewGuid():N}");
		try
		{
			var sdkSource = Path.Combine(root, "sdk-source");
			await CreateSdkSourceAsync(sdkSource);
			var bundle = Path.Combine(root, "bundle");
			var manifestPath = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0", new Dictionary<string, string> { [InstallerConstants.SdkComponentId] = sdkSource }, productId: InstallerConstants.SdkProductId, productName: "VeroScript SDK");
			var destination = Path.Combine(root, "installed", "Core");
			var result = await new InstallerEngine(new FakePathIntegration()).InstallAsync(new InstallOptions { ManifestPath = manifestPath, PayloadRoot = bundle, SelectedComponents = new HashSet<string> { InstallerConstants.SdkComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.SdkComponentId] = destination, }, });
			var record = await new InstallerEngine().LoadRecordAsync(result.InstallRecordPaths.Single());
			Expect(record.ProductId == InstallerConstants.SdkProductId, "The SDK product id was not persisted.");
			Expect(File.Exists(Path.Combine(destination, "include", "Script", "VeroScript.h")), "The SDK header was not installed.");
			Expect(!Directory.Exists(Path.Combine(destination, "include", "Cpp")), "The Core SDK unexpectedly contains the independently packaged C++ integration.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task SdkProductInstallsDocsComponentAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-sdk-docs-installer-test-{Guid.NewGuid():N}");
		try
		{
			var sdkSource = Path.Combine(root, "sdk-source");
			var docsSource = Path.Combine(root, "docs-source");
			await CreateSdkSourceAsync(sdkSource);
			await CreateDocsSourceAsync(docsSource);
			var bundle = Path.Combine(root, "bundle");
			var manifestPath = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0", new Dictionary<string, string> { [InstallerConstants.SdkComponentId] = sdkSource, [InstallerConstants.DocsComponentId] = docsSource, }, productId: InstallerConstants.SdkProductId, productName: "VeroScript SDK");
			var coreDestination = Path.Combine(root, "installed", "Core");
			var docsDestination = Path.Combine(root, "installed", "Docs");
			var result = await new InstallerEngine(new FakePathIntegration()).InstallAsync(new InstallOptions { ManifestPath = manifestPath, PayloadRoot = bundle, SelectedComponents = new HashSet<string> { InstallerConstants.SdkComponentId, InstallerConstants.DocsComponentId, }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.SdkComponentId] = coreDestination, [InstallerConstants.DocsComponentId] = docsDestination, }, });
			var record = await new InstallerEngine().LoadRecordAsync(result.InstallRecordPaths.First());
			Expect(record.Components.Count == 2, "SDK + docs install should create two component records.");
			Expect(File.Exists(Path.Combine(coreDestination, "include", "Script", "VeroScript.h")), "SDK header was not installed.");
			Expect(File.Exists(Path.Combine(docsDestination, "reference", "index.md")), "Docs index was not installed.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task DifferentProductsCannotReplaceEachOtherAsync()
	{
		await WithBundleAsync(async context =>
		{
			var destination = Path.Combine(context.Root, "shared-destination");
			await new InstallerEngine(new FakePathIntegration()).InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, });
			var sdkSource = Path.Combine(context.Root, "sdk-source");
			await CreateSdkSourceAsync(sdkSource);
			var sdkBundle = Path.Combine(context.Root, "sdk-bundle");
			var sdkManifest = await InstallerBundleBuilder.BuildAsync(sdkBundle, "1.0.0", new Dictionary<string, string> { [InstallerConstants.SdkComponentId] = sdkSource }, productId: InstallerConstants.SdkProductId, productName: "VeroScript SDK");
			await ExpectThrowsAsync<InvalidOperationException>(() => new InstallerEngine(new FakePathIntegration()).InstallAsync(new InstallOptions { ManifestPath = sdkManifest, PayloadRoot = sdkBundle, SelectedComponents = new HashSet<string> { InstallerConstants.SdkComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.SdkComponentId] = destination, }, }));
		});
	}

	private static async Task InstallTwoComponentsWithoutPathAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var runtimeDestination = Path.Combine(context.Root, "installed-runtime");
			var docsDestination = Path.Combine(context.Root, "installed-docs");
			var engine = new InstallerEngine(pathIntegration);
			var result = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string>(StringComparer.Ordinal) { InstallerConstants.RuntimeComponentId, InstallerConstants.DocsComponentId, }, ComponentDestinations = new Dictionary<string, string>(StringComparer.Ordinal) { [InstallerConstants.RuntimeComponentId] = runtimeDestination, [InstallerConstants.DocsComponentId] = docsDestination, }, AddToUserPath = false, });
			Expect(pathIntegration.AddCalls == 0, "PATH must not be touched when the option is disabled.");
			Expect(File.Exists(Path.Combine(runtimeDestination, ToolName("veroscript"))), "Runtime executable was not installed.");
			Expect(File.Exists(Path.Combine(docsDestination, "reference", "index.md")), "Documentation was not installed.");
			Expect(result.InstallRecordPaths.Count == 2, "Both components need install records.");
			await engine.UninstallAsync(result.InstallRecordPaths[0]);
			Expect(pathIntegration.RemoveCalls == 0, "Uninstall must not remove a PATH entry it did not add.");
			Expect(!Directory.Exists(runtimeDestination), "Runtime directory should be removed when empty.");
			Expect(!Directory.Exists(docsDestination), "Documentation directory should be removed when empty.");
		});
	}

	private static async Task PathIsExplicitOptInAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var destination = Path.Combine(context.Root, "runtime-with-path");
			var engine = new InstallerEngine(pathIntegration);
			var result = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, });
			Expect(pathIntegration.AddCalls == 1 && result.PathAdded, "PATH opt-in was not recorded.");
			await engine.UninstallAsync(result.InstallRecordPaths.Single());
			Expect(pathIntegration.RemoveCalls == 1, "Installer-owned PATH entry was not removed.");
			Expect(pathIntegration.LastRemoved == Path.GetFullPath(destination), "Wrong PATH entry was removed.");
		});
	}

	private static async Task OwnedPathCanBeRemovedDuringUpdateAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var destination = Path.Combine(context.Root, "runtime-remove-path");
			var engine = new InstallerEngine(pathIntegration);
			await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, });
			var updated = await engine.InstallAsync(CreateRuntimeOptions(context, destination));
			Expect(!updated.PathAdded, "Clearing PATH during update retained installer ownership.");
			Expect(pathIntegration.RemoveCalls == 1, "Updating with PATH disabled did not remove the owned entry.");
			Expect(pathIntegration.LastRemoved == Path.GetFullPath(destination), "Update removed the wrong PATH entry.");
			var record = await engine.LoadRecordAsync(updated.InstallRecordPaths.Single());
			Expect(!record.PathAdded && record.PathEntry is null, "Updated install record still claims a removed PATH entry.");
			await engine.UninstallAsync(updated.InstallRecordPaths.Single());
			Expect(pathIntegration.RemoveCalls == 1, "Uninstall tried to remove a PATH entry relinquished during update.");
		});
	}

	private static async Task RuntimeRelocationMovesOwnedPathAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var original = Path.Combine(context.Root, "runtime-old-location");
			var destination = Path.Combine(context.Root, "runtime-new-location");
			var engine = new InstallerEngine(pathIntegration);
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = original, }, AddToUserPath = true, });
			var userFile = Path.Combine(original, "projects", "kept.vs");
			await WriteAsync(userFile, "void main() {}\n");
			var moved = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, ExistingInstallRecordPath = installed.InstallRecordPaths.Single(), });
			Expect(!Directory.Exists(original), "Runtime relocation left the old installation directory behind.");
			Expect(File.Exists(Path.Combine(destination, ToolName("veroscript"))), "Runtime relocation did not install the runtime at the new location.");
			Expect(await File.ReadAllTextAsync(Path.Combine(destination, "projects", "kept.vs")) == "void main() {}\n", "Runtime relocation did not preserve a user-owned file.");
			Expect(pathIntegration.AddCalls == 2 && pathIntegration.RemoveCalls == 1, "Runtime relocation did not replace the PATH entry exactly once.");
			Expect(pathIntegration.LastRemoved == Path.GetFullPath(original), "Runtime relocation removed the wrong PATH entry.");
			var record = await engine.LoadRecordAsync(moved.InstallRecordPaths.Single());
			Expect(record.PathAdded && record.PathEntry == Path.GetFullPath(destination) && record.Components.Single().RootPath == Path.GetFullPath(destination), "Relocated install record does not own the new runtime and PATH location.");
			await engine.UninstallAsync(moved.InstallRecordPaths.Single());
			Expect(pathIntegration.RemoveCalls == 2 && pathIntegration.LastRemoved == Path.GetFullPath(destination), "Uninstall did not remove the relocated PATH entry.");
			Expect(File.Exists(Path.Combine(destination, "projects", "kept.vs")), "Uninstall removed a user-owned file preserved by relocation.");
		});
	}

	private static async Task RuntimeRelocationRollsBackPathAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var original = Path.Combine(context.Root, "rollback-path-old");
			var destination = Path.Combine(context.Root, "rollback-path-new");
			var engine = new InstallerEngine(pathIntegration);
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = original, }, AddToUserPath = true, });
			pathIntegration.ThrowOnRemove = true;
			await ExpectThrowsAsync<InvalidOperationException>(() => engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, ExistingInstallRecordPath = installed.InstallRecordPaths.Single(), }));
			Expect(Directory.Exists(original) && !Directory.Exists(destination), "A failed PATH migration changed the installed runtime location.");
			Expect(pathIntegration.Contains(original) && !pathIntegration.Contains(destination), "A failed PATH migration did not restore the original PATH state.");
			Expect(pathIntegration.UndoCalls == 1, "A failed PATH migration did not undo the newly added entry.");
			pathIntegration.ThrowOnRemove = false;
			await engine.UninstallAsync(installed.InstallRecordPaths.Single());
		});
	}

	private static async Task FailedUninstallRestoresFilesAndPathAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var destination = Path.Combine(context.Root, "failed-uninstall-runtime");
			var engine = new InstallerEngine(pathIntegration);
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, });
			var recordPath = installed.InstallRecordPaths.Single();
			var userFile = Path.Combine(destination, "projects", "keep.vs");
			await WriteAsync(userFile, "void main() {}\n");
			pathIntegration.ThrowOnRemove = true;
			await ExpectThrowsAsync<InvalidOperationException>(() => engine.UninstallAsync(recordPath));
			Expect(File.Exists(Path.Combine(destination, ToolName("veroscript"))), "A failed uninstall removed an installer-owned file.");
			Expect(File.Exists(recordPath), "A failed uninstall removed its recovery record.");
			Expect(File.Exists(userFile), "A failed uninstall removed a user-owned file.");
			Expect(pathIntegration.Contains(destination), "A failed uninstall lost the installer-owned PATH entry.");
			pathIntegration.ThrowOnRemove = false;
			await engine.UninstallAsync(recordPath);
			Expect(File.Exists(userFile), "Successful uninstall removed a user-owned file after retry.");
		});
	}

	private static async Task InterruptedTransactionsRecoverOrFinalizeAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-transaction-recovery-{Guid.NewGuid():N}");
		try
		{
			var pathIntegration = new FakePathIntegration();
			var rollbackDestination = Path.Combine(root, "rollback");
			Directory.CreateDirectory(rollbackDestination);
			await WriteAsync(Path.Combine(rollbackDestination, "generation.txt"), "old");
			var rollbackId = Guid.NewGuid().ToString("N");
			var rollbackStage = Path.Combine(root, $".rollback.veroscript-stage-{rollbackId}");
			var rollbackBackup = Path.Combine(root, $".rollback.veroscript-backup-{rollbackId}");
			Directory.CreateDirectory(rollbackStage);
			await WriteAsync(Path.Combine(rollbackStage, "generation.txt"), "new");
			var interrupted = await InstallerTransaction.BeginAsync(rollbackId, "install", [new InstallerTransactionComponent(rollbackDestination, rollbackDestination, rollbackStage, rollbackBackup, OriginalExisted: true)], CancellationToken.None);
			var competingId = Guid.NewGuid().ToString("N");
			await ExpectThrowsAsync<InvalidOperationException>(() => InstallerTransaction.BeginAsync(competingId, "install", [new InstallerTransactionComponent(rollbackDestination, rollbackDestination, Path.Combine(root, $".rollback.veroscript-stage-{competingId}"), Path.Combine(root, $".rollback.veroscript-backup-{competingId}"), OriginalExisted: true)], CancellationToken.None));
			Directory.Move(rollbackDestination, rollbackBackup);
			Directory.Move(rollbackStage, rollbackDestination);
			await interrupted.DisposeAsync();
			await InstallerTransaction.RecoverForRootsAsync([rollbackDestination], pathIntegration, CancellationToken.None);
			Expect(await File.ReadAllTextAsync(Path.Combine(rollbackDestination, "generation.txt")) == "old", "Recovery did not roll back an interrupted uncommitted transaction.");
			Expect(!Directory.Exists(rollbackBackup) && !Directory.Exists(rollbackStage), "Recovery retained rollback transaction artifacts.");
			var pathDestination = Path.Combine(root, "path-intent");
			var pathId = Guid.NewGuid().ToString("N");
			var pathStage = Path.Combine(root, $".path-intent.veroscript-stage-{pathId}");
			var pathBackup = Path.Combine(root, $".path-intent.veroscript-backup-{pathId}");
			Directory.CreateDirectory(pathStage);
			var interruptedPathChange = await InstallerTransaction.BeginAsync(pathId, "install", [new InstallerTransactionComponent(pathDestination, pathDestination, pathStage, pathBackup, OriginalExisted: false)], CancellationToken.None);
			await interruptedPathChange.RecordAddedPathIntentAsync(pathDestination, CancellationToken.None);
			await pathIntegration.AddToUserPathAsync(pathDestination, CancellationToken.None);
			await interruptedPathChange.DisposeAsync();
			await InstallerTransaction.RecoverForRootsAsync([pathDestination], pathIntegration, CancellationToken.None);
			Expect(!pathIntegration.Contains(pathDestination), "Recovery did not remove a PATH entry recorded before an interrupted add.");
			Expect(!Directory.Exists(pathStage) && !Directory.Exists(pathBackup), "Recovery retained PATH transaction artifacts.");
			var rolledBackDestination = Path.Combine(root, "rolled-back-path");
			var rolledBackId = Guid.NewGuid().ToString("N");
			var rolledBackStage = Path.Combine(root, $".rolled-back-path.veroscript-stage-{rolledBackId}");
			var rolledBackBackup = Path.Combine(root, $".rolled-back-path.veroscript-backup-{rolledBackId}");
			Directory.CreateDirectory(rolledBackStage);
			var rolledBackTransaction = await InstallerTransaction.BeginAsync(rolledBackId, "install", [new InstallerTransactionComponent(rolledBackDestination, rolledBackDestination, rolledBackStage, rolledBackBackup, OriginalExisted: false)], CancellationToken.None);
			await rolledBackTransaction.RecordAddedPathIntentAsync(rolledBackDestination, CancellationToken.None);
			await pathIntegration.AddToUserPathAsync(rolledBackDestination, CancellationToken.None);
			await pathIntegration.RemoveFromUserPathAsync(rolledBackDestination, CancellationToken.None);
			Directory.Delete(rolledBackStage, recursive: true);
			await rolledBackTransaction.DisposeAsync();
			var rolledBackJournal = Path.Combine(root, $".veroscript-transaction-{rolledBackId}.json");
			var terminalJournal = await File.ReadAllTextAsync(rolledBackJournal);
			terminalJournal = terminalJournal.Replace("\"State\": \"prepared\"", "\"State\": \"rolledback\"", StringComparison.Ordinal);
			await File.WriteAllTextAsync(rolledBackJournal, terminalJournal);
			await pathIntegration.AddToUserPathAsync(rolledBackDestination, CancellationToken.None);
			await InstallerTransaction.RecoverForRootsAsync([rolledBackDestination], pathIntegration, CancellationToken.None);
			Expect(pathIntegration.Contains(rolledBackDestination), "Recovery replayed a completed rollback and removed a later PATH entry.");
			var firstDestination = Path.Combine(root, "first-parent", "runtime");
			var secondDestination = Path.Combine(root, "second-parent", "docs");
			Directory.CreateDirectory(firstDestination);
			Directory.CreateDirectory(secondDestination);
			await WriteAsync(Path.Combine(firstDestination, "generation.txt"), "first-old");
			await WriteAsync(Path.Combine(secondDestination, "generation.txt"), "second-old");
			var multiId = Guid.NewGuid().ToString("N");
			var firstStage = Path.Combine(Path.GetDirectoryName(firstDestination)!, $".runtime.veroscript-stage-{multiId}");
			var firstBackup = Path.Combine(Path.GetDirectoryName(firstDestination)!, $".runtime.veroscript-backup-{multiId}");
			var secondStage = Path.Combine(Path.GetDirectoryName(secondDestination)!, $".docs.veroscript-stage-{multiId}");
			var secondBackup = Path.Combine(Path.GetDirectoryName(secondDestination)!, $".docs.veroscript-backup-{multiId}");
			Directory.CreateDirectory(firstStage);
			Directory.CreateDirectory(secondStage);
			await WriteAsync(Path.Combine(firstStage, "generation.txt"), "first-new");
			await WriteAsync(Path.Combine(secondStage, "generation.txt"), "second-new");
			var multiComponent = await InstallerTransaction.BeginAsync(multiId, "install", [new InstallerTransactionComponent(firstDestination, firstDestination, firstStage, firstBackup, OriginalExisted: true), new InstallerTransactionComponent(secondDestination, secondDestination, secondStage, secondBackup, OriginalExisted: true), ], CancellationToken.None);
			Directory.Move(firstDestination, firstBackup);
			Directory.Move(firstStage, firstDestination);
			await multiComponent.DisposeAsync();
			await InstallerTransaction.RecoverForRootsAsync([secondDestination], pathIntegration, CancellationToken.None);
			Expect(await File.ReadAllTextAsync(Path.Combine(firstDestination, "generation.txt")) == "first-old" && await File.ReadAllTextAsync(Path.Combine(secondDestination, "generation.txt")) == "second-old", "Recovery did not roll back an interrupted multi-component transaction from a sibling journal.");
			Expect(!Directory.Exists(firstBackup) && !Directory.Exists(firstStage) && !Directory.Exists(secondBackup) && !Directory.Exists(secondStage), "Recovery retained multi-component transaction artifacts.");
			var splitCommitId = Guid.NewGuid().ToString("N");
			var splitFirstStage = Path.Combine(Path.GetDirectoryName(firstDestination)!, $".runtime.veroscript-stage-{splitCommitId}");
			var splitFirstBackup = Path.Combine(Path.GetDirectoryName(firstDestination)!, $".runtime.veroscript-backup-{splitCommitId}");
			var splitSecondStage = Path.Combine(Path.GetDirectoryName(secondDestination)!, $".docs.veroscript-stage-{splitCommitId}");
			var splitSecondBackup = Path.Combine(Path.GetDirectoryName(secondDestination)!, $".docs.veroscript-backup-{splitCommitId}");
			Directory.CreateDirectory(splitFirstStage);
			Directory.CreateDirectory(splitSecondStage);
			await WriteAsync(Path.Combine(splitFirstStage, "generation.txt"), "first-committed");
			await WriteAsync(Path.Combine(splitSecondStage, "generation.txt"), "second-committed");
			var splitCommit = await InstallerTransaction.BeginAsync(splitCommitId, "install", [new InstallerTransactionComponent(firstDestination, firstDestination, splitFirstStage, splitFirstBackup, OriginalExisted: true), new InstallerTransactionComponent(secondDestination, secondDestination, splitSecondStage, splitSecondBackup, OriginalExisted: true), ], CancellationToken.None);
			Directory.Move(firstDestination, splitFirstBackup);
			Directory.Move(splitFirstStage, firstDestination);
			Directory.Move(secondDestination, splitSecondBackup);
			Directory.Move(splitSecondStage, secondDestination);
			await splitCommit.MarkCommittedAsync(CancellationToken.None);
			await splitCommit.DisposeAsync();
			var secondJournal = Path.Combine(Path.GetDirectoryName(secondDestination)!, $".veroscript-transaction-{splitCommitId}.json");
			var staleJournal = await File.ReadAllTextAsync(secondJournal);
			staleJournal = staleJournal.Replace("\"State\": \"committed\"", "\"State\": \"prepared\"", StringComparison.Ordinal);
			await File.WriteAllTextAsync(secondJournal, staleJournal);
			await InstallerTransaction.RecoverForRootsAsync([secondDestination], pathIntegration, CancellationToken.None);
			Expect(await File.ReadAllTextAsync(Path.Combine(firstDestination, "generation.txt")) == "first-committed" && await File.ReadAllTextAsync(Path.Combine(secondDestination, "generation.txt")) == "second-committed", "Recovery did not honor a canonical commit marker when a sibling journal was stale.");
			Expect(!Directory.Exists(splitFirstBackup) && !Directory.Exists(splitSecondBackup), "Recovery retained backups after reconciling a split commit marker.");
			var commitDestination = Path.Combine(root, "commit");
			Directory.CreateDirectory(commitDestination);
			await WriteAsync(Path.Combine(commitDestination, "generation.txt"), "old");
			var commitId = Guid.NewGuid().ToString("N");
			var commitStage = Path.Combine(root, $".commit.veroscript-stage-{commitId}");
			var commitBackup = Path.Combine(root, $".commit.veroscript-backup-{commitId}");
			Directory.CreateDirectory(commitStage);
			await WriteAsync(Path.Combine(commitStage, "generation.txt"), "new");
			var committed = await InstallerTransaction.BeginAsync(commitId, "install", [new InstallerTransactionComponent(commitDestination, commitDestination, commitStage, commitBackup, OriginalExisted: true)], CancellationToken.None);
			Directory.Move(commitDestination, commitBackup);
			Directory.Move(commitStage, commitDestination);
			await committed.MarkCommittedAsync(CancellationToken.None);
			await committed.DisposeAsync();
			await InstallerTransaction.RecoverForRootsAsync([commitDestination], pathIntegration, CancellationToken.None);
			Expect(await File.ReadAllTextAsync(Path.Combine(commitDestination, "generation.txt")) == "new", "Recovery rolled back a transaction whose commit marker was durable.");
			Expect(!Directory.Exists(commitBackup) && !Directory.Exists(commitStage), "Recovery retained committed transaction artifacts.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task RelocationRecoversFromOldRecordRootAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var engine = new InstallerEngine(pathIntegration);
			var originalDestination = Path.Combine(context.Root, "old-parent", "runtime");
			var initial = await engine.InstallAsync(CreateRuntimeOptions(context, originalDestination));
			var recordPath = initial.InstallRecordPaths.Single();
			var interruptedDestination = Path.Combine(context.Root, "abandoned-parent", "runtime");
			var transactionId = Guid.NewGuid().ToString("N");
			var stagePath = Path.Combine(Path.GetDirectoryName(interruptedDestination)!, $".runtime.veroscript-stage-{transactionId}");
			var backupPath = Path.Combine(Path.GetDirectoryName(originalDestination)!, $".runtime.veroscript-backup-{transactionId}");
			Directory.CreateDirectory(stagePath);
			await WriteAsync(Path.Combine(stagePath, "interrupted.txt"), "new");
			var interrupted = await InstallerTransaction.BeginAsync(transactionId, "install", [new InstallerTransactionComponent(interruptedDestination, originalDestination, stagePath, backupPath, OriginalExisted: true)], CancellationToken.None);
			Directory.Move(originalDestination, backupPath);
			Directory.Move(stagePath, interruptedDestination);
			await interrupted.DisposeAsync();
			var finalDestination = Path.Combine(context.Root, "final-parent", "runtime");
			var relocated = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = finalDestination, }, ExistingInstallRecordPath = recordPath, });
			Expect(relocated.InstallRecordPaths.Single() == Path.Combine(finalDestination, InstallerConstants.InstallRecordFileName), "Relocation did not complete after recovering from the old record root.");
			Expect(File.Exists(Path.Combine(finalDestination, ToolName("veroscript"))), "Recovered relocation did not install the runtime at its final destination.");
			Expect(!Directory.Exists(originalDestination) && !Directory.Exists(interruptedDestination), "Recovered relocation retained an old or abandoned live directory.");
			Expect(!Directory.Exists(backupPath) && !Directory.Exists(stagePath), "Recovered relocation retained transaction artifacts.");
		});
	}

	private static async Task RecordLoadRecoversInterruptedTransactionAsync()
	{
		await WithBundleAsync(async context =>
		{
			var engine = new InstallerEngine(new FakePathIntegration());
			var destination = Path.Combine(context.Root, "record-recovery", "runtime");
			var installed = await engine.InstallAsync(CreateRuntimeOptions(context, destination));
			var recordPath = installed.InstallRecordPaths.Single();
			var transactionId = Guid.NewGuid().ToString("N");
			var parent = Path.GetDirectoryName(destination)!;
			var stagePath = Path.Combine(parent, $".runtime.veroscript-stage-{transactionId}");
			var backupPath = Path.Combine(parent, $".runtime.veroscript-backup-{transactionId}");
			Directory.CreateDirectory(stagePath);
			await WriteAsync(Path.Combine(stagePath, "interrupted.txt"), "new");
			var interrupted = await InstallerTransaction.BeginAsync(transactionId, "install", [new InstallerTransactionComponent(destination, destination, stagePath, backupPath, OriginalExisted: true)], CancellationToken.None);
			Directory.Move(destination, backupPath);
			Directory.Move(stagePath, destination);
			await interrupted.DisposeAsync();
			var recovered = await engine.LoadRecordAsync(recordPath);
			Expect(recovered.InstallationId == installed.InstallationId, "Record loading did not recover the previous installation generation.");
			Expect(File.Exists(Path.Combine(destination, ToolName("veroscript"))), "Record loading did not restore the installed runtime.");
			Expect(!Directory.Exists(stagePath) && !Directory.Exists(backupPath), "Record loading retained interrupted transaction artifacts.");
		});
	}

	private static async Task RelocationRejectsAnotherComponentsDirectoryAsync()
	{
		await WithBundleAsync(async context =>
		{
			var originalRuntime = Path.Combine(context.Root, "owned-runtime");
			var relocatedRuntime = Path.Combine(context.Root, "relocated-runtime");
			var engine = new InstallerEngine(new FakePathIntegration());
			var installed = await engine.InstallAsync(CreateRuntimeOptions(context, originalRuntime));
			await ExpectThrowsAsync<InvalidOperationException>(() => engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string>(StringComparer.Ordinal) { InstallerConstants.RuntimeComponentId, InstallerConstants.DocsComponentId, }, ComponentDestinations = new Dictionary<string, string>(StringComparer.Ordinal) { [InstallerConstants.RuntimeComponentId] = relocatedRuntime, [InstallerConstants.DocsComponentId] = originalRuntime, }, ExistingInstallRecordPath = installed.InstallRecordPaths.Single(), }));
			Expect(Directory.Exists(originalRuntime) && !Directory.Exists(relocatedRuntime), "A rejected cross-component relocation changed the existing installation.");
			Expect(File.Exists(Path.Combine(originalRuntime, ToolName("veroscript"))), "A rejected cross-component relocation damaged the runtime.");
			await engine.UninstallAsync(installed.InstallRecordPaths.Single());
		});
	}

	private static async Task TamperedPayloadIsRejectedAsync()
	{
		await WithBundleAsync(async context =>
		{
			await File.AppendAllTextAsync(Path.Combine(context.Bundle, InstallerConstants.PayloadDirectoryName, InstallerConstants.RuntimeComponentId, ToolName("veroscript")), "tampered");
			var destination = Path.Combine(context.Root, "tampered-install");
			await ExpectThrowsAsync<InvalidDataException>(() => new InstallerEngine(new FakePathIntegration()).InstallAsync(CreateRuntimeOptions(context, destination)));
			Expect(!Directory.Exists(destination), "A rejected payload must not create the final destination.");
		});
	}

	private static async Task RepairRestoresFilesAndPathOwnershipAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var destination = Path.Combine(context.Root, "repair-runtime");
			var engine = new InstallerEngine(pathIntegration);
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, });
			var executable = Path.Combine(destination, ToolName("veroscript"));
			await File.WriteAllTextAsync(executable, "corrupted");
			var repaired = await engine.RepairAsync(installed.InstallRecordPaths.Single(), context.ManifestPath, context.Bundle);
			Expect(await File.ReadAllTextAsync(executable) == "runtime", "Repair did not restore the original payload.");
			Expect(repaired.PathAdded, "Repair lost ownership of the installer-added PATH entry.");
			await engine.UninstallAsync(repaired.InstallRecordPaths.Single());
			Expect(pathIntegration.RemoveCalls == 1, "Repaired installation did not remove its PATH entry.");
		});
	}

	private static async Task FailedRepairPreservesInstallationAsync()
	{
		await WithBundleAsync(async context =>
		{
			var destination = Path.Combine(context.Root, "repair-preserve-runtime");
			var engine = new InstallerEngine(new FakePathIntegration());
			var installed = await engine.InstallAsync(CreateRuntimeOptions(context, destination));
			var executable = Path.Combine(destination, ToolName("veroscript"));
			await File.AppendAllTextAsync(Path.Combine(context.Bundle, InstallerConstants.PayloadDirectoryName, InstallerConstants.RuntimeComponentId, ToolName("veroscript")), "tampered");
			await ExpectThrowsAsync<InvalidDataException>(() => engine.RepairAsync(installed.InstallRecordPaths.Single(), context.ManifestPath, context.Bundle));
			Expect(await File.ReadAllTextAsync(executable) == "runtime", "A failed repair modified the installed generation.");
			Expect(File.Exists(installed.InstallRecordPaths.Single()), "A failed repair removed the existing install record.");
			await engine.UninstallAsync(installed.InstallRecordPaths.Single());
		});
	}

	private static async Task UnmanagedDestinationIsPreservedAsync()
	{
		await WithBundleAsync(async context =>
		{
			var destination = Path.Combine(context.Root, "user-owned");
			Directory.CreateDirectory(destination);
			var sentinel = Path.Combine(destination, "keep.txt");
			await File.WriteAllTextAsync(sentinel, "keep");
			await ExpectThrowsAsync<IOException>(() => new InstallerEngine(new FakePathIntegration()).InstallAsync(CreateRuntimeOptions(context, destination)), InstallerError.DestinationConflict);
			Expect(await File.ReadAllTextAsync(sentinel) == "keep", "Unmanaged destination content was modified.");
		});
	}

	private static Task UnsafeManifestPathIsRejectedAsync()
	{
		var manifest = CreateMinimalManifest(InstallerConstants.RuntimeComponentId, "runtime", "../escape", false);
		ExpectThrows<InvalidDataException>(() => ManifestValidator.Validate(manifest));
		return Task.CompletedTask;
	}

	private static async Task TamperedPathOwnershipIsRejectedAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var destination = Path.Combine(context.Root, "path-record-runtime");
			var engine = new InstallerEngine(pathIntegration);
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, });
			var recordPath = installed.InstallRecordPaths.Single();
			var original = await File.ReadAllTextAsync(recordPath);
			var json = JsonNode.Parse(original)?.AsObject() ?? throw new InvalidOperationException("Install record JSON was not an object.");
			json["pathEntry"] = Path.Combine(context.Root, "unrelated-path");
			await File.WriteAllTextAsync(recordPath, json.ToJsonString());
			await ExpectThrowsAsync<InvalidDataException>(() => engine.UninstallAsync(recordPath));
			Expect(pathIntegration.RemoveCalls == 0, "A tampered record removed an unrelated PATH entry.");
			Expect(File.Exists(Path.Combine(destination, ToolName("veroscript"))), "A tampered record modified the installation.");
			await File.WriteAllTextAsync(recordPath, original);
			await engine.UninstallAsync(recordPath);
		});
	}

	private static async Task DestinationCannotOverlapInstallerAsync()
	{
		await WithBundleAsync(async context =>
		{
			var destination = Path.Combine(context.Bundle, "installed-here");
			await ExpectThrowsAsync<InvalidOperationException>(() => new InstallerEngine(new FakePathIntegration()).InstallAsync(CreateRuntimeOptions(context, destination)));
			Expect(!Directory.Exists(destination), "The installer payload was modified by an overlapping destination.");
		});
	}

	private static async Task RegionTaggedLocalizationRequiredAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-localization-layout-{Guid.NewGuid():N}");
		try
		{
			var runtime = Path.Combine(root, "runtime-source");
			await WriteAsync(Path.Combine(runtime, ToolName("veroscript")), "runtime");
			await WriteAsync(Path.Combine(runtime, ToolName("veroscript-lsp")), "lsp");
			await WriteAsync(Path.Combine(runtime, ToolName("veroscript-dap")), "dap");
			await CreateStandardLibrarySourceAsync(Path.Combine(runtime, "stdlib"));
			await WriteAsync(Path.Combine(runtime, "localization", "shared.json"), "{}");
			await WriteAsync(Path.Combine(runtime, "Vm", VmLibraryName()), "vm library");
			await ExpectThrowsAsync<InvalidDataException>(() => InstallerBundleBuilder.BuildAsync(Path.Combine(root, "bundle"), "1.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = runtime, }));
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task CompleteStandardLibraryRequiredAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-stdlib-layout-{Guid.NewGuid():N}");
		try
		{
			var runtime = Path.Combine(root, "runtime-source");
			await CreateRuntimeSourceAsync(runtime, "runtime");
			File.Delete(Path.Combine(runtime, "stdlib", "concurrent.vs"));
			await ExpectThrowsAsync<InvalidDataException>(() => InstallerBundleBuilder.BuildAsync(Path.Combine(root, "bundle"), "1.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = runtime, }));
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task RuntimeVmRequiredAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-vm-{Guid.NewGuid():N}");
		try
		{
			var runtime = Path.Combine(root, "runtime");
			await CreateRuntimeSourceAsync(runtime, "runtime");
			Directory.Delete(Path.Combine(runtime, "Vm"), recursive: true);
			await ExpectThrowsAsync<InvalidDataException>(() => InstallerBundleBuilder.BuildAsync(
				Path.Combine(root, "bundle"), "1.0.0", new Dictionary<string, string> { ["runtime"] = runtime }));
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task BundleRootsCannotOverlapAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-overlap-{Guid.NewGuid():N}");
		try
		{
			var runtime = Path.Combine(root, "runtime-source");
			await CreateRuntimeSourceAsync(runtime, "runtime");
			await ExpectThrowsAsync<InvalidOperationException>(() => InstallerBundleBuilder.BuildAsync(Path.Combine(runtime, "bundle"), "1.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = runtime }));
			Expect(!Directory.Exists(Path.Combine(runtime, "bundle")), "An overlapping bundle output was created.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task DowngradeRequiresOptInAsync()
	{
		await WithBundleAsync(async context =>
		{
			var versionTwoBundle = Path.Combine(context.Root, "bundle-v2");
			var versionTwoManifest = await InstallerBundleBuilder.BuildAsync(versionTwoBundle, "2.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = context.RuntimeSource, });
			var destination = Path.Combine(context.Root, "downgrade-runtime");
			var engine = new InstallerEngine(new FakePathIntegration());
			await engine.InstallAsync(new InstallOptions { ManifestPath = versionTwoManifest, PayloadRoot = versionTwoBundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, });
			await ExpectThrowsAsync<InvalidOperationException>(() => engine.InstallAsync(CreateRuntimeOptions(context, destination)), InstallerError.Downgrade);
			var record = await engine.LoadRecordAsync(Path.Combine(destination, InstallerConstants.InstallRecordFileName));
			Expect(record.ProductVersion == "2.0.0", "A rejected downgrade changed the installed version.");
			var downgradeOptions = CreateRuntimeOptions(context, destination, allowDowngrade: true);
			var downgraded = await engine.InstallAsync(downgradeOptions);
			Expect(downgraded.ProductVersion == "1.0.0", "Explicit downgrade did not install the requested version.");
			await engine.UninstallAsync(downgraded.InstallRecordPaths.Single());
		});
	}

	private static async Task RepairRequiresExactPackageAsync()
	{
		await WithBundleAsync(async context =>
		{
			var destination = Path.Combine(context.Root, "exact-repair-runtime");
			var engine = new InstallerEngine(new FakePathIntegration());
			var installed = await engine.InstallAsync(CreateRuntimeOptions(context, destination));
			await File.WriteAllTextAsync(Path.Combine(context.RuntimeSource, ToolName("veroscript")), "different-runtime");
			var otherBundle = Path.Combine(context.Root, "other-bundle");
			var otherManifest = await InstallerBundleBuilder.BuildAsync(otherBundle, "1.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = context.RuntimeSource, });
			await ExpectThrowsAsync<InvalidOperationException>(() => engine.RepairAsync(installed.InstallRecordPaths.Single(), otherManifest, otherBundle), InstallerError.RepairMismatch);
			Expect(await File.ReadAllTextAsync(Path.Combine(destination, ToolName("veroscript"))) == "runtime", "A mismatched repair package changed the installation.");
			await engine.UninstallAsync(installed.InstallRecordPaths.Single());
		});
	}

	private static async Task MultiComponentUpdatesAreAtomicAsync()
	{
		await WithBundleAsync(async context =>
		{
			var runtimeDestination = Path.Combine(context.Root, "atomic-runtime");
			var docsDestination = Path.Combine(context.Root, "atomic-docs");
			var engine = new InstallerEngine(new FakePathIntegration());
			var installed = await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId, InstallerConstants.DocsComponentId, }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = runtimeDestination, [InstallerConstants.DocsComponentId] = docsDestination, }, });
			await ExpectThrowsAsync<InvalidOperationException>(() => engine.InstallAsync(CreateRuntimeOptions(context, runtimeDestination)));
			Expect(File.Exists(Path.Combine(docsDestination, "reference", "index.md")), "A partial update modified the sibling documentation component.");
			await engine.UninstallAsync(installed.InstallRecordPaths[0]);
		});
	}

	private static Task NullManifestCollectionsAreRejectedAsync()
	{
		var manifest = new InstallerManifest
		{
			Components = null!
		};
		ExpectThrows<InvalidDataException>(() => ManifestValidator.Validate(manifest));
		return Task.CompletedTask;
	}

	private static async Task UpgradePreservesPathOwnershipAsync()
	{
		await WithBundleAsync(async context =>
		{
			var pathIntegration = new FakePathIntegration();
			var destination = Path.Combine(context.Root, "path-upgrade-runtime");
			var engine = new InstallerEngine(pathIntegration);
			await engine.InstallAsync(new InstallOptions { ManifestPath = context.ManifestPath, PayloadRoot = context.Bundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, AddToUserPath = true, });
			var upgraded = await engine.InstallAsync(CreateRuntimeOptions(context, destination, addToPath: true));
			Expect(upgraded.PathAdded, "An upgrade lost ownership of its existing PATH entry.");
			await engine.UninstallAsync(upgraded.InstallRecordPaths.Single());
			Expect(pathIntegration.RemoveCalls == 1, "The preserved PATH entry was not removed during uninstall.");
		});
	}

	private static async Task UpgradePreservesUserOwnedFilesAsync()
	{
		await WithBundleAsync(async context =>
		{
			var destination = Path.Combine(context.Root, "user-file-upgrade-runtime");
			var engine = new InstallerEngine(new FakePathIntegration());
			await engine.InstallAsync(CreateRuntimeOptions(context, destination));
			var userFile = Path.Combine(destination, "projects", "hello.vs");
			await WriteAsync(userFile, "void main() {}\n");
			var userEmptyDirectory = Path.Combine(destination, "projects", "empty");
			Directory.CreateDirectory(userEmptyDirectory);
			await File.WriteAllTextAsync(Path.Combine(context.RuntimeSource, ToolName("veroscript")), "runtime-v2");
			var versionTwoBundle = Path.Combine(context.Root, "user-file-bundle-v2");
			var versionTwoManifest = await InstallerBundleBuilder.BuildAsync(versionTwoBundle, "2.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = context.RuntimeSource, });
			var updated = await engine.InstallAsync(new InstallOptions { ManifestPath = versionTwoManifest, PayloadRoot = versionTwoBundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, });
			Expect(updated.ProductVersion == "2.0.0", "Update did not record the new version.");
			Expect(await File.ReadAllTextAsync(userFile) == "void main() {}\n", "Update changed a user-owned script.");
			Expect(Directory.Exists(userEmptyDirectory), "Update removed a user-owned empty directory.");
			Expect(await File.ReadAllTextAsync(Path.Combine(destination, ToolName("veroscript"))) == "runtime-v2", "Update did not replace an installer-owned runtime file.");
			await engine.UninstallAsync(updated.InstallRecordPaths.Single());
			Expect(File.Exists(userFile), "Uninstall removed a preserved user-owned script.");
		});
	}

	private static async Task SameVersionUpdateRestoresExecutablePermissionsAsync()
	{
		await WithBundleAsync(async context =>
		{
			var source = Path.Combine(context.Root, "permission-source");
			const string toolName = "CSharpTool.Cli";
			var sourceTool = Path.Combine(source, toolName);
			await WriteAsync(sourceTool, "unchanged apphost bytes");
			var sources = new Dictionary<string, string>
			{
				["core"] = source
			};
			var oldBundle = Path.Combine(context.Root, "permission-old-bundle");
			var oldManifestPath = await InstallerBundleBuilder.BuildAsync(oldBundle, "1.0.0", sources, productDefinition: CreateCustomDefinition());
			var oldManifest = await ManifestValidator.LoadAsync(oldManifestPath);
			var oldFile = oldManifest.Components.Single().Files.Single();
			Expect(!oldFile.Executable, "Old permission fixture unexpectedly marks the apphost executable.");
			var destination = Path.Combine(context.Root, "permission-installed");
			InstallOptions Options(string manifestPath, string bundle, string? record = null) => new()
			{
				ManifestPath = manifestPath,
				PayloadRoot = bundle,
				SelectedComponents = new HashSet<string>
				{
					"core"
				},
				ComponentDestinations = new Dictionary<string, string>
				{
					["core"] = destination
				},
				ExistingInstallRecordPath = record,
			};
			var engine = new InstallerEngine(new FakePathIntegration());
			var installed = await engine.InstallAsync(Options(oldManifestPath, oldBundle));
			var installedTool = Path.Combine(destination, toolName);
			const UnixFileMode executeBits = UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute;
			if (!OperatingSystem.IsWindows())
			{
				Expect((File.GetUnixFileMode(installedTool) & executeBits) == 0, "Old installation fixture unexpectedly has execution permissions.");
				File.SetUnixFileMode(sourceTool, File.GetUnixFileMode(sourceTool) | executeBits);
			}

			var userFile = Path.Combine(destination, "user-owned.txt");
			await WriteAsync(userFile, "preserve me");
			var newBundle = Path.Combine(context.Root, "permission-new-bundle");
			var newManifestPath = await InstallerBundleBuilder.BuildAsync(newBundle, "1.0.0", sources, productDefinition: CreateCustomDefinition());
			var newManifest = await ManifestValidator.LoadAsync(newManifestPath);
			var newFile = newManifest.Components.Single().Files.Single();
			// Windows has no Unix modes; retain the manifest-only update coverage there.
			if (OperatingSystem.IsWindows())
			{
				newFile.Executable = true;
				File.Delete(newManifestPath);
				await PackageManifestBuilder.WriteAsync(newManifestPath, newManifest);
			}

			Expect(newFile.Executable && newFile.Sha256 == oldFile.Sha256 && newFile.Size == oldFile.Size, "Permission update fixture must retain the same version and file bytes.");
			var updated = await engine.InstallAsync(Options(newManifestPath, newBundle, installed.InstallRecordPaths.Single()));
			Expect(updated.ProductVersion == "1.0.0", "Permission update changed the product version.");
			Expect(await File.ReadAllTextAsync(installedTool) == "unchanged apphost bytes", "Permission update changed the apphost bytes.");
			if (!OperatingSystem.IsWindows())
			{
				Expect((File.GetUnixFileMode(installedTool) & executeBits) == executeBits, "Same-version update did not restore all apphost execution permissions.");
				File.SetUnixFileMode(installedTool, File.GetUnixFileMode(installedTool) & ~executeBits);
			}

			var record = updated.InstallRecordPaths.Single();
			await engine.RepairAsync(record, newManifestPath, newBundle);
			if (!OperatingSystem.IsWindows())
			{
				Expect((File.GetUnixFileMode(installedTool) & executeBits) == executeBits, "Repair did not restore execution permissions when file bytes were unchanged.");
			}

			Expect(await File.ReadAllTextAsync(userFile) == "preserve me", "Update or repair changed a user-owned file.");
			await engine.UninstallAsync(record);
			Expect(!File.Exists(installedTool) && await File.ReadAllTextAsync(userFile) == "preserve me", "Uninstall did not preserve the user file after the permission update.");
		});
	}

	private static async Task UpgradeRejectsUserFileConflictAsync()
	{
		await WithBundleAsync(async context =>
		{
			var destination = Path.Combine(context.Root, "user-file-conflict-runtime");
			var engine = new InstallerEngine(new FakePathIntegration());
			await engine.InstallAsync(CreateRuntimeOptions(context, destination));
			var conflict = Path.Combine(destination, "future-runtime-file.txt");
			await File.WriteAllTextAsync(conflict, "user-owned");
			await WriteAsync(Path.Combine(context.RuntimeSource, "future-runtime-file.txt"), "installer-owned");
			var versionTwoBundle = Path.Combine(context.Root, "conflict-bundle-v2");
			var versionTwoManifest = await InstallerBundleBuilder.BuildAsync(versionTwoBundle, "2.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = context.RuntimeSource, });
			await ExpectThrowsAsync<IOException>(() => engine.InstallAsync(new InstallOptions { ManifestPath = versionTwoManifest, PayloadRoot = versionTwoBundle, SelectedComponents = new HashSet<string> { InstallerConstants.RuntimeComponentId }, ComponentDestinations = new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = destination, }, }));
			Expect(await File.ReadAllTextAsync(conflict) == "user-owned", "A rejected update changed the conflicting user file.");
			Expect(await File.ReadAllTextAsync(Path.Combine(destination, ToolName("veroscript"))) == "runtime", "A rejected update changed the installed runtime generation.");
			await engine.UninstallAsync(Path.Combine(destination, InstallerConstants.InstallRecordFileName));
		});
	}

	private static async Task WindowsCommitRetriesTransientLocksAsync()
	{
		if (!OperatingSystem.IsWindows())
		{
			return;
		}

		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-move-{Guid.NewGuid():N}");
		var source = Path.Combine(root, "source");
		var destination = Path.Combine(root, "destination");
		Directory.CreateDirectory(source);
		var lockedFile = Path.Combine(source, "tool.exe");
		await File.WriteAllTextAsync(lockedFile, "locked");
		try
		{
			var stream = new FileStream(lockedFile, FileMode.Open, FileAccess.Read, FileShare.Read);
			var move = FileSystemRetry.MoveDirectoryAsync(source, destination, CancellationToken.None);
			await Task.Delay(300);
			await stream.DisposeAsync();
			await move;
			Expect(Directory.Exists(destination), "A transient executable lock prevented the directory commit.");
			var movedFile = Path.Combine(destination, "tool.exe");
			var deleteStream = new FileStream(movedFile, FileMode.Open, FileAccess.Read, FileShare.Read);
			var delete = FileSystemRetry.DeleteFileAsync(movedFile, CancellationToken.None);
			await Task.Delay(300);
			await deleteStream.DisposeAsync();
			await delete;
			Expect(!File.Exists(movedFile), "A transient executable lock prevented file cleanup.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task UnixPathBlockSupportsMultipleInstallsAsync()
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-profile-{Guid.NewGuid():N}");
		var profile = Path.Combine(root, "profile");
		var first = Path.Combine(root, "runtime-one");
		var second = Path.Combine(root, "runtime-two");
		Directory.CreateDirectory(root);
		try
		{
			var integration = new UnixShellPathIntegration(profile);
			await integration.AddToUserPathAsync(first);
			await integration.AddToUserPathAsync(second);
			var withBoth = await File.ReadAllTextAsync(profile);
			Expect(withBoth.Contains(first, StringComparison.Ordinal) && withBoth.Contains(second, StringComparison.Ordinal), "Adding a second Unix PATH entry replaced the first installation.");
			await integration.RemoveFromUserPathAsync(first);
			var afterRemoval = await File.ReadAllTextAsync(profile);
			Expect(!afterRemoval.Contains(first, StringComparison.Ordinal) && afterRemoval.Contains(second, StringComparison.Ordinal), "Removing one Unix PATH entry removed or retained the wrong installation.");
			var rollbackMutation = await integration.AddToUserPathAsync(first);
			await File.AppendAllTextAsync(profile, Environment.NewLine + "# concurrent user edit" + Environment.NewLine);
			await integration.UndoAsync(rollbackMutation);
			var afterUndo = await File.ReadAllTextAsync(profile);
			Expect(afterUndo.Contains("# concurrent user edit", StringComparison.Ordinal), "PATH rollback overwrote a concurrent shell-profile edit.");
			Expect(!afterUndo.Contains(first, StringComparison.Ordinal) && afterUndo.Contains(second, StringComparison.Ordinal), "PATH rollback removed the wrong managed entry.");
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static Task WindowsPathEditPreservesExistingTextAsync()
	{
		var directory = Path.GetFullPath(Path.Combine(Path.GetTempPath(), "VeroScript Path Test"));
		var original = $"  quoted-entry  {Path.PathSeparator}";
		var updated = PathList.Append(original, directory);
		Expect(PathList.Contains(updated, directory), "The appended PATH entry cannot be found.");
		Expect(PathList.Remove(updated, directory) == original, "Adding and removing VeroScript changed unrelated PATH text or empty entries.");
		return Task.CompletedTask;
	}

	private static InstallOptions CreateRuntimeOptions(BundleContext context, string destination, bool allowDowngrade = false, bool addToPath = false) => new()
	{
		ManifestPath = context.ManifestPath,
		PayloadRoot = context.Bundle,
		SelectedComponents = new HashSet<string>
		{
			InstallerConstants.RuntimeComponentId
		},
		ComponentDestinations = new Dictionary<string, string>
		{
			[InstallerConstants.RuntimeComponentId] = destination,
		},
		AddToUserPath = addToPath,
		AllowDowngrade = allowDowngrade,
	};
	private static InstallerManifest CreateMinimalManifest(string componentId, string payloadDirectory, string filePath, bool executable)
	{
		return new InstallerManifest
		{
			Components = [new InstallerComponent
			{
				Id = componentId,
				DisplayName = componentId,
				PayloadDirectory = payloadDirectory,
				Files = [new InstallerFile
				{
					Path = filePath,
					Size = 0,
					Sha256 = new string ('0', 64),
					Executable = executable,
				}, ],
			}, ],
		};
	}

	private static async Task WithBundleAsync(Func<BundleContext, Task> action)
	{
		var root = Path.Combine(Path.GetTempPath(), $"veroscript-installer-test-{Guid.NewGuid():N}");
		try
		{
			var runtime = Path.Combine(root, "runtime-source");
			var docs = Path.Combine(root, "docs-source");
			await CreateRuntimeSourceAsync(runtime, "runtime");
			await CreateDocsSourceAsync(docs);
			var bundle = Path.Combine(root, "bundle");
			var manifest = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0", new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = runtime, [InstallerConstants.DocsComponentId] = docs, });
			await action(new BundleContext(root, bundle, manifest, runtime, docs));
		}
		finally
		{
			if (Directory.Exists(root))
			{
				Directory.Delete(root, recursive: true);
			}
		}
	}

	private static async Task CreateRuntimeSourceAsync(string runtime, string executableContents)
	{
		await WriteAsync(Path.Combine(runtime, ToolName("veroscript")), executableContents);
		await WriteAsync(Path.Combine(runtime, ToolName("veroscript-lsp")), "lsp");
		await WriteAsync(Path.Combine(runtime, ToolName("veroscript-dap")), "dap");
		await WriteAsync(Path.Combine(runtime, "Vm", VmLibraryName()), "vm library");
		await CreateStandardLibrarySourceAsync(Path.Combine(runtime, "stdlib"));
		await WriteAsync(Path.Combine(runtime, "localization", "en-US", "shared.json"), "{}");
	}

	private static async Task CreateSdkSourceAsync(string sdk)
	{
		await WriteAsync(Path.Combine(sdk, "include", "Script", "VeroScript.h"), "#pragma once");
		await WriteAsync(Path.Combine(sdk, "include", "Script", "VeroScriptJit.h"), "#pragma once");
		await WriteAsync(Path.Combine(sdk, "include", "Script", "VeroScriptRun.h"), "#pragma once");
		await WriteAsync(Path.Combine(sdk, "include", "Script", "VeroScriptValue.h"), "#pragma once");
		await WriteAsync(Path.Combine(sdk, "include", "Script", "VeroScriptVm.h"), "#pragma once");
		await WriteAsync(Path.Combine(sdk, "lib", LibraryName("VeroScript")), "library");
		await WriteAsync(Path.Combine(sdk, "lib", LibraryName("VeroScriptBytecodeVm")), "vm library");
		await WriteAsync(Path.Combine(sdk, "include", "Vm", "VeroScriptBytecodeVm.h"), "#pragma once");
		await WriteAsync(Path.Combine(sdk, "bin", "Vm", VmLibraryName()), "vm backend");
		await WriteAsync(Path.Combine(sdk, "lib", "cmake", "VeroScriptBytecodeVm", "VeroScriptBytecodeVmConfig.cmake"), "# config");
		await WriteAsync(Path.Combine(sdk, "lib", "cmake", "VeroScriptExecutionContract", "VeroScriptExecutionContractConfig.cmake"), "# config");
		await WriteAsync(Path.Combine(sdk, "lib", "cmake", "VeroScript", "VeroScriptConfig.cmake"), "# config");
		await CreateStandardLibrarySourceAsync(Path.Combine(sdk, "assets", "stdlib"));
	}

	private static async Task CreateStandardLibrarySourceAsync(string directory)
	{
		foreach (var module in StandardLibraryModules)
		{
			await WriteAsync(Path.Combine(directory, module), $"// {module}");
		}
	}

	private static async Task CreateDocsSourceAsync(string docs)
	{
		await WriteAsync(Path.Combine(docs, "reference", "index.md"), "# VeroScript Docs");
	}

	private static async Task WriteAsync(string path, string content)
	{
		Directory.CreateDirectory(Path.GetDirectoryName(path)!);
		await File.WriteAllTextAsync(path, content);
		if (!OperatingSystem.IsWindows() && Path.GetFileName(path).StartsWith("veroscript", StringComparison.OrdinalIgnoreCase))
		{
			File.SetUnixFileMode(path, File.GetUnixFileMode(path) | UnixFileMode.UserExecute);
		}
	}

	private static string ToolName(string stem) => OperatingSystem.IsWindows() ? stem + ".exe" : stem;
	private static string LibraryName(string stem) => OperatingSystem.IsWindows() ? stem + ".lib" : "lib" + stem + ".a";
	private static string VmLibraryName() => OperatingSystem.IsWindows() ? "VeroScriptVmBytecode.dll"
		: OperatingSystem.IsMacOS() ? "libVeroScriptVmBytecode.dylib" : "libVeroScriptVmBytecode.so";
	private static void Expect(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException(message);
		}
	}

	private static void ExpectThrows<T>(Action action)
		where T : Exception
	{
		try
		{
			action();
		}
		catch (T)
		{
			return;
		}

		throw new InvalidOperationException($"Expected {typeof(T).Name}.");
	}

	private static async Task ExceptionAssertionsRejectNoThrowAsync()
	{
		var rejectedSync = false;
		try
		{
			ExpectThrows<InvalidOperationException>(() =>
			{
			});
		}
		catch (InvalidOperationException)
		{
			rejectedSync = true;
		}

		Expect(rejectedSync, "Synchronous exception assertion accepted a successful action.");
		var rejectedAsync = false;
		try
		{
			await ExpectThrowsAsync<InvalidOperationException>(() => Task.CompletedTask);
		}
		catch (InvalidOperationException)
		{
			rejectedAsync = true;
		}

		Expect(rejectedAsync, "Asynchronous exception assertion accepted a successful action.");
	}

	private static async Task ExpectThrowsAsync<T>(Func<Task> action, InstallerError? expectedCode = null)
		where T : Exception
	{
		try
		{
			await action();
		}
		catch (T exception)
		{
			if (expectedCode is not null)
			{
				Expect(InstallerErrors.GetCode(exception) == expectedCode, $"Expected installer error {expectedCode}, got {InstallerErrors.GetCode(exception)}.");
			}

			return;
		}

		throw new InvalidOperationException($"Expected {typeof(T).Name}.");
	}

	private sealed record BundleContext(string Root, string Bundle, string ManifestPath, string RuntimeSource, string DocsSource);
	private sealed class FakePathIntegration : IPathIntegration
	{
		private readonly HashSet<string> _entries = new(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
		internal int AddCalls { get; private set; }
		internal int RemoveCalls { get; private set; }
		internal int UndoCalls { get; private set; }
		internal string? LastRemoved { get; private set; }
		internal bool ThrowOnRemove { get; set; }

		public Task<bool> ContainsUserPathAsync(string directory, CancellationToken cancellationToken = default)
		{
			cancellationToken.ThrowIfCancellationRequested();
			return Task.FromResult(_entries.Contains(Path.GetFullPath(directory)));
		}

		public Task<PathMutation> AddToUserPathAsync(string directory, CancellationToken cancellationToken = default)
		{
			++AddCalls;
			var fullPath = Path.GetFullPath(directory);
			return Task.FromResult(new PathMutation { Entry = fullPath, Changed = _entries.Add(fullPath) });
		}

		public Task UndoAsync(PathMutation mutation, CancellationToken cancellationToken = default)
		{
			++UndoCalls;
			if (mutation.Changed)
			{
				_entries.Remove(Path.GetFullPath(mutation.Entry));
			}

			return Task.CompletedTask;
		}

		public Task RemoveFromUserPathAsync(string directory, CancellationToken cancellationToken = default)
		{
			++RemoveCalls;
			LastRemoved = Path.GetFullPath(directory);
			if (ThrowOnRemove)
			{
				throw new InvalidOperationException("Injected PATH removal failure.");
			}

			_entries.Remove(LastRemoved);
			return Task.CompletedTask;
		}

		internal bool Contains(string directory) => _entries.Contains(Path.GetFullPath(directory));
	}
}
