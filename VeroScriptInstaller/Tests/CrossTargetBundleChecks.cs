// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core;

namespace VeroScriptInstaller.Tests;

internal static partial class Program
{
	private static async Task CrossTargetBundleAsync()
	{
		await WithBundleAsync(async context =>
		{
			var architecture = PlatformInfo.CurrentArchitecture == "arm64" ? "x64" : "arm64";
			var bundle = Path.Combine(context.Root, "foreign-bundle");
			var path = await InstallerBundleBuilder.BuildAsync(bundle, "1.0.0",
				new Dictionary<string, string> { [InstallerConstants.RuntimeComponentId] = context.RuntimeSource },
				architecture: architecture);
			var manifest = await ManifestValidator.LoadAsync(path, requireCurrentPlatform: false);
			await ManifestValidator.VerifyPayloadAsync(manifest, bundle, requireCurrentPlatform: false);
			await ExpectThrowsAsync<PlatformNotSupportedException>(() => ManifestValidator.LoadAsync(path));
			await ExpectThrowsAsync<PlatformNotSupportedException>(() => ManifestValidator.VerifyPayloadAsync(manifest, bundle));
			await File.AppendAllTextAsync(Path.Combine(bundle, "payload", InstallerConstants.RuntimeComponentId, ToolName("veroscript")), "tampered");
			await ExpectThrowsAsync<InvalidDataException>(() => ManifestValidator.VerifyPayloadAsync(manifest, bundle, requireCurrentPlatform: false));
		});
	}
}
