// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace VeroScriptInstaller.Core;

public sealed class InstallerManifest
{
	[JsonPropertyName("schemaVersion")]
	public int SchemaVersion { get; set; } = InstallerConstants.ManifestSchemaVersion;

	[JsonPropertyName("productId")]
	public string ProductId { get; set; } = InstallerConstants.ProductId;

	[JsonPropertyName("productName")]
	public string ProductName { get; set; } = "VeroScript";

	[JsonPropertyName("productVersion")]
	public string ProductVersion { get; set; } = "0.1.1";

	[JsonPropertyName("profileId")]
	public string ProfileId { get; set; } = InstallerProductCatalog.RuntimeProfileId;

	[JsonPropertyName("description")]
	public string Description { get; set; } = string.Empty;

	[JsonPropertyName("localizedNames")]
	public Dictionary<string, string> LocalizedNames { get; set; } = new(StringComparer.OrdinalIgnoreCase);

	[JsonPropertyName("localizedDescriptions")]
	public Dictionary<string, string> LocalizedDescriptions { get; set; } = new(StringComparer.OrdinalIgnoreCase);

	[JsonPropertyName("descriptionResourceKey")]
	public string? DescriptionResourceKey { get; set; }

	[JsonPropertyName("installRoot")]
	public InstallerRootDefinition InstallRoot { get; set; } = new();

	[JsonPropertyName("pathIntegration")]
	public InstallerPathDefinition? PathIntegration { get; set; }

	[JsonPropertyName("platform")]
	public string Platform { get; set; } = PlatformInfo.CurrentPlatform;

	[JsonPropertyName("architecture")]
	public string Architecture { get; set; } = PlatformInfo.CurrentArchitecture;

	[JsonPropertyName("components")]
	public List<InstallerComponent> Components { get; set; } = [];
}

public sealed class InstallerComponent
{
	[JsonPropertyName("id")]
	public string Id { get; set; } = string.Empty;

	[JsonPropertyName("displayName")]
	public string DisplayName { get; set; } = string.Empty;

	[JsonPropertyName("description")]
	public string Description { get; set; } = string.Empty;

	[JsonPropertyName("localizedNames")]
	public Dictionary<string, string> LocalizedNames { get; set; } = new(StringComparer.OrdinalIgnoreCase);

	[JsonPropertyName("localizedDescriptions")]
	public Dictionary<string, string> LocalizedDescriptions { get; set; } = new(StringComparer.OrdinalIgnoreCase);

	[JsonPropertyName("displayNameResourceKey")]
	public string? DisplayNameResourceKey { get; set; }

	[JsonPropertyName("descriptionResourceKey")]
	public string? DescriptionResourceKey { get; set; }

	[JsonPropertyName("installDirectory")]
	public string InstallDirectory { get; set; } = ".";

	[JsonPropertyName("payloadDirectory")]
	public string PayloadDirectory { get; set; } = string.Empty;

	[JsonPropertyName("required")]
	public bool Required { get; set; }

	[JsonPropertyName("defaultSelected")]
	public bool DefaultSelected { get; set; } = true;

	[JsonPropertyName("dependencies")]
	public List<string> Dependencies { get; set; } = [];

	[JsonPropertyName("validation")]
	public InstallerComponentValidation Validation { get; set; } = new();

	[JsonPropertyName("files")]
	public List<InstallerFile> Files { get; set; } = [];
}

public sealed class InstallerRootDefinition
{
	[JsonPropertyName("kind")]
	public string Kind { get; set; } = InstallerRootKinds.Programs;

	[JsonPropertyName("directoryName")]
	public string DirectoryName { get; set; } = "VeroScript";

	[JsonPropertyName("includeVersion")]
	public bool IncludeVersion { get; set; }
}

public sealed class InstallerPathDefinition
{
	[JsonPropertyName("componentId")]
	public string ComponentId { get; set; } = string.Empty;

	[JsonPropertyName("relativeDirectory")]
	public string RelativeDirectory { get; set; } = ".";

	[JsonPropertyName("defaultEnabled")]
	public bool DefaultEnabled { get; set; }
}

public sealed class InstallerComponentValidation
{
	[JsonPropertyName("requiredFiles")]
	public List<string> RequiredFiles { get; set; } = [];

	[JsonPropertyName("requiredExtensions")]
	public List<string> RequiredExtensions { get; set; } = [];

	[JsonPropertyName("requiredFilePrefixes")]
	public List<string> RequiredFilePrefixes { get; set; } = [];

	[JsonPropertyName("requiredExecutables")]
	public List<string> RequiredExecutables { get; set; } = [];
}

public sealed class InstallerFile
{
	[JsonPropertyName("path")]
	public string Path { get; set; } = string.Empty;

	[JsonPropertyName("size")]
	public long Size { get; set; }

	[JsonPropertyName("sha256")]
	public string Sha256 { get; set; } = string.Empty;

	[JsonPropertyName("executable")]
	public bool Executable { get; set; }
}
