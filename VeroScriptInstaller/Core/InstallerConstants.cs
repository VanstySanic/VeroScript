// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core;

public static class InstallerConstants
{
	public const int ManifestSchemaVersion = 1;
	public const int InstallRecordSchemaVersion = 1;
	public const string ProductId = "dev.veroscript.runtime";
	public const string SdkProductId = "dev.veroscript.sdk";
	public const string RuntimeComponentId = "runtime";
	public const string SdkComponentId = "sdk";
	public const string InteropSdkComponentId = "interop-sdk";
	public const string WebAssemblySdkComponentId = "web-sdk";
	public const string DocsComponentId = "docs";
	public const string SymbolsComponentId = "symbols";
	public const string ManifestFileName = "manifest.json";
	public const string PayloadDirectoryName = "payload";
	public const string InstallRecordFileName = ".veroscript-install.json";
	public const int MaximumFileCount = 200_000;
	public const long MaximumFileSize = 8L * 1024 * 1024 * 1024;

}
