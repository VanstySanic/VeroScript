// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.CSharpFrontend;

namespace VeroScript.CSharpTranspiler;

public sealed class CSharpTranspileOptions
{
	public bool ValidateGeneratedSource { get; init; } = true;
	public string VeroScriptExecutable { get; init; } = "";
	public string GeneratedSourcePath { get; init; } = "";
	public TimeSpan ValidationTimeout { get; init; } = TimeSpan.FromSeconds(120);
	public CSharpParseOptions ParseOptions { get; init; } = new();
}

public sealed record CSharpTranspileResult(
	string Source,
	string SourceMapJson,
	IReadOnlyList<CSharpDiagnostic> Diagnostics)
{
	public bool Succeeded => Source.Length != 0 && Diagnostics.All(
		diagnostic => diagnostic.Severity != CSharpDiagnosticSeverity.Error);
}

public sealed class CSharpTranspileProjectOptions
{
	public bool ValidateGeneratedSources { get; init; } = true;
	public string VeroScriptExecutable { get; init; } = "";
	public string ProjectRoot { get; init; } = "";
	public TimeSpan ValidationTimeout { get; init; } = TimeSpan.FromSeconds(120);
	public CSharpParseOptions ParseOptions { get; init; } = new();
}

public sealed record CSharpTranspiledFile(
	string InputPath,
	string OutputName,
	string Source,
	string SourceMapJson);

public sealed record CSharpTranspileProjectResult(
	IReadOnlyList<CSharpTranspiledFile> Files,
	string ManifestJson,
	IReadOnlyList<CSharpDiagnostic> Diagnostics)
{
	public bool Succeeded => Files.Count != 0 && Diagnostics.All(
		diagnostic => diagnostic.Severity != CSharpDiagnosticSeverity.Error);
}
