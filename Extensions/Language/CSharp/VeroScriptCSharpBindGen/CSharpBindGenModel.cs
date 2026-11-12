// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.CSharpFrontend;

namespace VeroScript.CSharpBindGen;

public sealed class CSharpBindGenOptions
{
	public bool EmitApiReference { get; init; } = true;
	public bool EmitRegistrationSource { get; init; } = true;
	public bool EmitAotBridgeSource { get; init; }
	public string RegistrationNamespace { get; init; } = "VeroScript.GeneratedBindings";
	public string RegistrationClassName { get; init; } = "VeroScriptCSharpBindings";
	public bool RequireBindingAttribute { get; init; }
	public string BindingAttributeName { get; init; } = "VeroScriptBindingAttribute";
	public string IgnoreAttributeName { get; init; } = "VeroScriptIgnoreAttribute";
}

public sealed record CSharpBindGenResult(
	string CatalogJson,
	string ApiReference,
	string RegistrationSource,
	IReadOnlyList<CSharpDiagnostic> Diagnostics)
{
	public string AotBridgeSource { get; init; } = "";
	public bool Succeeded => Diagnostics.All(diagnostic =>
		diagnostic.Severity != CSharpDiagnosticSeverity.Error);
}

public sealed record CSharpBindGenApiReference(
	string InputPath,
	string OutputName,
	string Contents);

public sealed class CSharpBindGenProjectLayout
{
	public string CatalogPath { get; init; } = "bindings.json";
	public string RegistrationPath { get; init; } = "Bindings.g.cs";
	public string AotBridgePath { get; init; } = "Bindings.Aot.g.cs";
	public string ApiReferenceDirectory { get; init; } = "";
}

public sealed record CSharpBindGenProjectResult(
	string CatalogJson,
	string RegistrationSource,
	IReadOnlyList<CSharpBindGenApiReference> ApiReferences,
	string ManifestJson,
	IReadOnlyList<CSharpDiagnostic> Diagnostics)
{
	public string AotBridgeSource { get; init; } = "";
	public bool Succeeded => Diagnostics.All(diagnostic =>
		diagnostic.Severity != CSharpDiagnosticSeverity.Error);
}
