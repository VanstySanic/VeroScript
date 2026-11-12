// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace VeroScriptInstaller.Core;

public sealed class InstallOptions
{
	public required string ManifestPath { get; init; }
	public required string PayloadRoot { get; init; }
	public required IReadOnlyDictionary<string, string> ComponentDestinations { get; init; }
	public required IReadOnlySet<string> SelectedComponents { get; init; }
	public bool AddToUserPath { get; init; }
	public bool PathAlreadyOwned { get; init; }
	public string? ExistingInstallRecordPath { get; init; }
	public bool AllowDowngrade { get; init; }
	public FileAssociationOptions FileAssociations { get; init; } = new();
}

public sealed class InstallResult
{
	public required string InstallationId { get; init; }
	public required string ProductVersion { get; init; }
	public required IReadOnlyList<string> InstallRecordPaths { get; init; }
	public bool PathAdded { get; init; }
	public string? PathEntry { get; init; }
	public string? DefaultApplicationName { get; init; }
	public bool AssociationSettingsPreserved { get; init; }
}

public sealed class InstallProgress
{
	public required string Stage { get; init; }
	public string? ComponentId { get; init; }
	public string? RelativePath { get; init; }
	public int CompletedFiles { get; init; }
	public int TotalFiles { get; init; }
}

public sealed class InstallationRecord
{
	[JsonPropertyName("schemaVersion")]
	public int SchemaVersion { get; set; } = InstallerConstants.InstallRecordSchemaVersion;

	[JsonPropertyName("installationId")]
	public string InstallationId { get; set; } = string.Empty;

	[JsonPropertyName("productId")]
	public string ProductId { get; set; } = InstallerConstants.ProductId;

	[JsonPropertyName("productName")]
	public string ProductName { get; set; } = string.Empty;

	[JsonPropertyName("localizedNames")]
	public Dictionary<string, string> LocalizedNames { get; set; } = new(StringComparer.OrdinalIgnoreCase);

	[JsonPropertyName("productVersion")]
	public string ProductVersion { get; set; } = string.Empty;

	[JsonPropertyName("platform")]
	public string Platform { get; set; } = string.Empty;

	[JsonPropertyName("architecture")]
	public string Architecture { get; set; } = string.Empty;

	[JsonPropertyName("manifestFingerprint")]
	public string ManifestFingerprint { get; set; } = string.Empty;

	[JsonPropertyName("installedAtUtc")]
	public DateTimeOffset InstalledAtUtc { get; set; }

	[JsonPropertyName("components")]
	public List<InstalledComponentRecord> Components { get; set; } = [];

	[JsonPropertyName("pathAdded")]
	public bool PathAdded { get; set; }

	[JsonPropertyName("pathEntry")]
	public string? PathEntry { get; set; }

	[JsonPropertyName("pathComponentId")]
	public string? PathComponentId { get; set; }

	// Missing in historical records: never infer ownership from installed icons.
	[JsonPropertyName("fileAssociations")]
	public FileAssociationRecord? FileAssociations { get; set; }
}

public sealed class InstalledComponentRecord
{
	[JsonPropertyName("id")]
	public string Id { get; set; } = string.Empty;

	[JsonPropertyName("rootPath")]
	public string RootPath { get; set; } = string.Empty;

	[JsonPropertyName("files")]
	public List<string> Files { get; set; } = [];
}
