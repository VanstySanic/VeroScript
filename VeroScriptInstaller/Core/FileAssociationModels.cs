// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace VeroScriptInstaller.Core;

public sealed record FileAssociationOptions
{
	[JsonPropertyName("source")]
	public bool Source { get; init; }
	[JsonPropertyName("application")]
	public bool Application { get; init; }
	[JsonPropertyName("packageIcon")]
	public bool PackageIcon { get; init; }
	[JsonIgnore]
	public bool Any => Source || Application || PackageIcon;
}

public sealed class FileAssociationRecord
{
	[JsonPropertyName("ownerId")]
	public string OwnerId { get; set; } = "";
	[JsonPropertyName("options")]
	public FileAssociationOptions Options { get; set; } = new();
	[JsonPropertyName("values")]
	public List<OwnedAssociationValue> Values { get; set; } = [];
	[JsonPropertyName("createdKeys")]
	public List<string> CreatedKeys { get; set; } = [];
}

public sealed class OwnedAssociationValue
{
	[JsonPropertyName("key")]
	public string Key { get; set; } = "";
	[JsonPropertyName("name")]
	public string Name { get; set; } = "";
	[JsonPropertyName("value")]
	public string Value { get; set; } = "";
	// False is a tombstone: repair/update must not reclaim a later user's edit.
	[JsonPropertyName("owned")]
	public bool Owned { get; set; }
}

internal sealed class AssociationValueChange
{
	public string Key { get; set; } = "";
	public string Name { get; set; } = "";
	public string? Before { get; set; }
	public string? After { get; set; }
}

internal sealed class FileAssociationChange
{
	public string OwnerId { get; set; } = "";
	public List<AssociationValueChange> Values { get; set; } = [];
	public List<string> CreatedKeys { get; set; } = [];
	public List<string> PruneKeys { get; set; } = [];
}

internal sealed record FileAssociationPlan(FileAssociationRecord? Record, FileAssociationChange? Change, bool PreservedSettings);
