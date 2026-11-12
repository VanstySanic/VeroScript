// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using VeroScript.CSharpFrontend;
using FrontendParseOptions = VeroScript.CSharpFrontend.CSharpParseOptions;
using FrontendParser = VeroScript.CSharpFrontend.CSharpFrontend;

namespace VeroScript.CSharpTranspiler;
public static partial class CSharpTranspiler
{
	public static CSharpTranspileResult TranspileSource(string source, string sourcePath = "<csharp-source>", CSharpTranspileOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(source);
		options ??= new();
		ProjectContext project = CreateProject([new(sourcePath, source)], options.ParseOptions);
		var diagnostics = new List<CSharpDiagnostic>(project.Diagnostics);
		if (HasErrors(diagnostics))
		{
			return new("", "", diagnostics);
		}
		EmittedSource emitted = EmitTree(project, project.Trees[0], options.GeneratedSourcePath, [], false, diagnostics);
		if (!HasErrors(diagnostics) && options.ValidateGeneratedSource)
		{
			ValidateSingle(emitted.Source, sourcePath, options, diagnostics);
		}

		return HasErrors(diagnostics) ? new("", "", diagnostics) : new(emitted.Source, emitted.SourceMapJson, diagnostics);
	}

	public static CSharpTranspileResult TranspileFile(string inputPath, CSharpTranspileOptions? options = null)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(inputPath);
		return TranspileSource(File.ReadAllText(inputPath), inputPath, options);
	}

	public static CSharpTranspileProjectResult TranspileProject(IReadOnlyList<string> inputPaths, CSharpTranspileProjectOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(inputPaths);
		options ??= new();
		var diagnostics = new List<CSharpDiagnostic>();
		if (inputPaths.Count == 0)
		{
			diagnostics.Add(Error("VSCT0001", "At least one C# input file is required."));
			return new([], "", diagnostics);
		}

		if (!ValidTimeout(options.ValidationTimeout))
		{
			diagnostics.Add(Error("VSCT0004", "Validation timeout must be finite and positive."));
			return new([], "", diagnostics);
		}

		string projectRoot = string.IsNullOrWhiteSpace(options.ProjectRoot) ? CommonDirectory(inputPaths.Select(Path.GetFullPath).ToArray()) : Path.GetFullPath(options.ProjectRoot);
		var comparer = OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;
		var seen = new HashSet<string>(comparer);
		var sources = new List<CSharpSourceFile>(inputPaths.Count);
		foreach (string path in inputPaths)
		{
			string fullPath = Path.GetFullPath(path);
			if (!seen.Add(fullPath))
			{
				diagnostics.Add(Error("VSCT0002", $"Duplicate C# source path '{path}'."));
				continue;
			}

			string relative = Path.GetRelativePath(projectRoot, fullPath);
			if (relative == ".." || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) || Path.IsPathRooted(relative))
			{
				diagnostics.Add(Error("VSCT0003", $"Input '{path}' is outside project root '{projectRoot}'."));
				continue;
			}

			sources.Add(new(fullPath, File.ReadAllText(fullPath)));
		}

		if (HasErrors(diagnostics))
		{
			return new([], "", diagnostics);
		}
		ProjectContext project = CreateProject(sources, options.ParseOptions);
		diagnostics.AddRange(project.Diagnostics);
		if (HasErrors(diagnostics))
		{
			return new([], "", diagnostics);
		}
		Dictionary<SyntaxTree, string> outputNames = BuildOutputNames(project.Trees, projectRoot);
		var files = new List<CSharpTranspiledFile>(project.Trees.Length);
		foreach (SyntaxTree tree in project.Trees)
		{
			string[] imports = FindDependencies(project, tree).Select(dependency => outputNames[dependency].Replace('\\', '/')).Order(StringComparer.Ordinal).ToArray();
			EmittedSource emitted = EmitTree(project, tree, outputNames[tree], imports, true, diagnostics);
			if (!HasErrors(diagnostics))
			{
				files.Add(new(tree.FilePath, outputNames[tree], emitted.Source, emitted.SourceMapJson));
			}
		}

		if (HasErrors(diagnostics))
		{
			return new([], "", diagnostics);
		}
		if (options.ValidateGeneratedSources)
		{
			ValidateProject(files, options, diagnostics);
		}

		if (HasErrors(diagnostics))
		{
			return new([], "", diagnostics);
		}
		var manifest = new
		{
			version = 1,
			kind = "veroscript.csharp-transpiled-project",
			projectWideAccess = true,
			files = files.Select(file => new { input = NormalizePath(Path.GetRelativePath(projectRoot, file.InputPath)), output = NormalizePath(file.OutputName), sourceMap = NormalizePath(file.OutputName + ".map.json") })
		};
		return new(files, JsonSerializer.Serialize(manifest, JsonOptions) + Environment.NewLine, diagnostics);
	}

	private static ProjectContext CreateProject(IReadOnlyList<CSharpSourceFile> sources, FrontendParseOptions options)
	{
		CSharpParseResult frontend = FrontendParser.ParseSources(sources, options);
		var diagnostics = new List<CSharpDiagnostic>(frontend.Diagnostics);
		if (!LanguageVersionFacts.TryParse(options.LanguageVersion, out LanguageVersion version))
		{
			return new([], null!, null, diagnostics);
		}

		var parseOptions = new Microsoft.CodeAnalysis.CSharp.CSharpParseOptions(version, DocumentationMode.Parse, SourceCodeKind.Regular, options.PreprocessorSymbols);
		SyntaxTree[] trees = sources.Select(source => CSharpSyntaxTree.ParseText(source.Text, parseOptions, source.Path)).ToArray();
		ImmutableArray<MetadataReference> references = CreateReferences(options, diagnostics);
		CSharpCompilation compilation = CSharpCompilation.Create("VeroScript.CSharpTranspiler.Analysis", trees, references, new CSharpCompilationOptions(OutputKind.ConsoleApplication, allowUnsafe: options.AllowUnsafe, nullableContextOptions: NullableContextOptions.Enable, deterministic: true));
		foreach (Diagnostic diagnostic in compilation.GetDiagnostics().Where(item => item.Id == "CS0017"))
		{
			diagnostics.Add(Error("VSCT2001", "C# defines more than one applicable entry point; select a single Main method before transpiling.", diagnostic.Location.SourceTree?.FilePath ?? ""));
		}

		IMethodSymbol? entryPoint = compilation.GetEntryPoint(default);
		if (entryPoint is not null && (!entryPoint.ContainingType.IsStatic || entryPoint.Parameters.Length != 0 || entryPoint.ReturnType.SpecialType is not (SpecialType.System_Void or SpecialType.System_Int32)))
		{
			diagnostics.Add(Error("VSCT2001", "VeroScript entry lowering supports only a parameterless static int or void Main method declared in a static type.", entryPoint.Locations.FirstOrDefault(location => location.IsInSource)?.SourceTree?.FilePath ?? ""));
		}

		return new(trees, compilation, entryPoint, diagnostics);
	}

	private static ImmutableArray<MetadataReference> CreateReferences(FrontendParseOptions options, List<CSharpDiagnostic> diagnostics)
	{
		var paths = new HashSet<string>(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
		if (AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")is string trusted)
		{
			foreach (string path in trusted.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
			{
				paths.Add(path);
			}
		}

		foreach (string path in options.ReferencePaths)
		{
			paths.Add(Path.GetFullPath(path));
		}

		var result = ImmutableArray.CreateBuilder<MetadataReference>();
		foreach (string path in paths.Order(StringComparer.Ordinal))
		{
			try
			{
				result.Add(MetadataReference.CreateFromFile(path));
			}
			catch (Exception exception)when (exception is IOException or UnauthorizedAccessException or BadImageFormatException)
			{
				diagnostics.Add(Error("VSCT0005", $"Cannot load metadata reference '{path}': {exception.Message}"));
			}
		}

		return result.ToImmutable();
	}

	private static EmittedSource EmitTree(ProjectContext project, SyntaxTree tree, string generatedPath, string[] imports, bool exportDeclarations, List<CSharpDiagnostic> diagnostics)
	{
		var emitter = new Emitter(project.Compilation.GetSemanticModel(tree, true), tree.FilePath, generatedPath, exportDeclarations, project.EntryPoint, project.IdentifierNames, diagnostics);
		foreach (string import in imports)
		{
			emitter.Line($"import {Quote(import)};");
		}

		if (imports.Length != 0)
		{
			emitter.Line();
		}

		emitter.EmitCompilationUnit(tree.GetCompilationUnitRoot());
		return emitter.Finish();
	}

	private static SyntaxTree[] FindDependencies(ProjectContext project, SyntaxTree tree)
	{
		SemanticModel model = project.Compilation.GetSemanticModel(tree, true);
		var dependencies = new HashSet<SyntaxTree>();
		foreach (SyntaxNode node in tree.GetRoot().DescendantNodes())
		{
			ISymbol? symbol = model.GetSymbolInfo(node).Symbol;
			AddSymbolTrees(symbol, tree, dependencies);
			ITypeSymbol? type = model.GetTypeInfo(node).Type;
			AddSymbolTrees(type, tree, dependencies);
		}

		return dependencies.OrderBy(item => item.FilePath, StringComparer.Ordinal).ToArray();
	}

	private static void AddSymbolTrees(ISymbol? symbol, SyntaxTree own, HashSet<SyntaxTree> dependencies)
	{
		if (symbol is null)
		{
			return;
		}
		foreach (SyntaxReference reference in symbol.OriginalDefinition.DeclaringSyntaxReferences)
		{
			if (reference.SyntaxTree != own)
			{
				dependencies.Add(reference.SyntaxTree);
			}
		}

		if (symbol is IMethodSymbol method)
		{
			AddSymbolTrees(method.ReturnType, own, dependencies);
			foreach (IParameterSymbol parameter in method.Parameters)
			{
				AddSymbolTrees(parameter.Type, own, dependencies);
			}
		}

		if (symbol is INamedTypeSymbol named)
		{
			foreach (ITypeSymbol argument in named.TypeArguments)
			{
				AddSymbolTrees(argument, own, dependencies);
			}
		}
	}

	private static Dictionary<SyntaxTree, string> BuildOutputNames(IReadOnlyList<SyntaxTree> trees, string projectRoot)
	{
		var result = new Dictionary<SyntaxTree, string>();
		// Generated projects must also work on case- and normalization-insensitive filesystems.
		var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (SyntaxTree tree in trees.OrderBy(item => item.FilePath, StringComparer.Ordinal))
		{
			string relative = NormalizePath(Path.GetRelativePath(projectRoot, tree.FilePath));
			string candidate = Path.ChangeExtension(relative, ".vs").Replace('\\', '/');
			if (!used.Add(candidate.Normalize(NormalizationForm.FormC)))
			{
				string stem = Path.ChangeExtension(candidate, null) ?? candidate;
				string hashedStem = $"{stem}.{StableHash(relative):x16}";
				candidate = hashedStem + ".vs";
				// A source file can already have the hashed stem as its name.
				for (int suffix = 1; !used.Add(candidate.Normalize(NormalizationForm.FormC)); ++suffix)
				{
					candidate = hashedStem + "." + suffix.ToString(CultureInfo.InvariantCulture) + ".vs";
				}
			}

			result.Add(tree, candidate);
		}

		return result;
	}

	private static ulong StableHash(string value)
	{
		ulong hash = 14695981039346656037UL;
		foreach (byte item in Encoding.UTF8.GetBytes(NormalizePath(value)))
		{
			hash ^= item;
			hash *= 1099511628211UL;
		}

		return hash;
	}

	private static string CommonDirectory(string[] paths)
	{
		if (paths.Length == 0)
		{
			return Directory.GetCurrentDirectory();
		}
		string candidate = Path.GetDirectoryName(paths[0])!;
		while (paths.Any(path => !IsWithin(candidate, path)))
		{
			DirectoryInfo? parent = Directory.GetParent(candidate);
			if (parent is null)
			{
				break;
			}

			candidate = parent.FullName;
		}

		return candidate;
	}

	private static bool IsWithin(string directory, string path)
	{
		string relative = Path.GetRelativePath(directory, path);
		return relative != ".." && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) && !Path.IsPathRooted(relative);
	}

	private static void ValidateSingle(string source, string sourcePath, CSharpTranspileOptions options, List<CSharpDiagnostic> diagnostics)
	{
		if (!ValidTimeout(options.ValidationTimeout))
		{
			diagnostics.Add(Error("VSCT0004", "Validation timeout must be finite and positive."));
			return;
		}

		string? executable = FindVeroScript(options.VeroScriptExecutable);
		if (executable is null)
		{
			diagnostics.Add(Error("VSCT2002", "VeroScript validator was not found; set VeroScriptExecutable, VEROSCRIPT_EXECUTABLE, or VEROSCRIPT_SDK."));
			return;
		}

		string temporary = Path.Combine(Path.GetTempPath(), "veroscript-csharp-" + Guid.NewGuid().ToString("N", CultureInfo.InvariantCulture) + ".vs");
		try
		{
			File.WriteAllText(temporary, source, new UTF8Encoding(false));
			RunValidator(executable, ["--check", temporary], options.ValidationTimeout, sourcePath, diagnostics);
		}
		finally
		{
			TryDelete(temporary);
		}
	}

	private static void ValidateProject(IReadOnlyList<CSharpTranspiledFile> files, CSharpTranspileProjectOptions options, List<CSharpDiagnostic> diagnostics)
	{
		if (!ValidTimeout(options.ValidationTimeout))
		{
			diagnostics.Add(Error("VSCT0004", "Validation timeout must be finite and positive."));
			return;
		}

		string? executable = FindVeroScript(options.VeroScriptExecutable);
		if (executable is null)
		{
			diagnostics.Add(Error("VSCT2002", "VeroScript validator was not found; set VeroScriptExecutable, VEROSCRIPT_EXECUTABLE, or VEROSCRIPT_SDK."));
			return;
		}

		string root = Path.Combine(Path.GetTempPath(), "veroscript-csharp-project-" + Guid.NewGuid().ToString("N", CultureInfo.InvariantCulture));
		Directory.CreateDirectory(root);
		try
		{
			foreach (CSharpTranspiledFile file in files)
			{
				string target = Path.Combine(root, file.OutputName);
				Directory.CreateDirectory(Path.GetDirectoryName(target)!);
				File.WriteAllText(target, file.Source, new UTF8Encoding(false));
			}

			foreach (CSharpTranspiledFile file in files)
			{
				RunValidator(executable, ["--check", "--allow-project-wide-access", "-I", root, Path.Combine(root, file.OutputName)], options.ValidationTimeout, file.InputPath, diagnostics);
				if (HasErrors(diagnostics))
				{
					break;
				}
			}
		}
		finally
		{
			try
			{
				Directory.Delete(root, true);
			}
			catch (Exception exception)when (exception is IOException or UnauthorizedAccessException)
			{
			}
		}
	}

	private static void RunValidator(string executable, IReadOnlyList<string> arguments, TimeSpan timeout, string sourcePath, List<CSharpDiagnostic> diagnostics)
	{
		using var process = new Process
		{
			StartInfo = new(executable)
			{
				UseShellExecute = false,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				CreateNoWindow = true
			}
		};
		foreach (string argument in arguments)
		{
			process.StartInfo.ArgumentList.Add(argument);
		}

		try
		{
			process.Start();
			using var readCancellation = new CancellationTokenSource();
			Task<string> output = process.StandardOutput.ReadToEndAsync(readCancellation.Token);
			Task<string> error = process.StandardError.ReadToEndAsync(readCancellation.Token);
			try
			{
				// A launcher can exit while a child still owns its redirected
				// pipes. Apply the deadline to both exit and complete output.
				Task.WhenAll(process.WaitForExitAsync(), output, error).WaitAsync(TimeSpan.FromMilliseconds(Math.Min(int.MaxValue, timeout.TotalMilliseconds))).GetAwaiter().GetResult();
			}
			catch (TimeoutException)
			{
				readCancellation.Cancel();
				if (!process.HasExited)
				{
					try
					{
						process.Kill(true);
					}
					catch (InvalidOperationException)when (process.HasExited)
					{
					// The process can finish between the exit check and kill.
					}
				}

				diagnostics.Add(Error("VSCT2004", "VeroScript validation timed out.", sourcePath));
				return;
			}

			if (process.ExitCode != 0)
			{
				string detail = string.Join(Environment.NewLine, new[] { output.Result.Trim(), error.Result.Trim() }.Where(item => item.Length != 0));
				diagnostics.Add(Error("VSCT2003", $"Generated VeroScript validation failed: {detail}", sourcePath));
			}
		}
		catch (Exception exception)when (exception is IOException or InvalidOperationException or System.ComponentModel.Win32Exception)
		{
			diagnostics.Add(Error("VSCT2003", $"Cannot execute VeroScript validator: {exception.Message}", sourcePath));
		}
	}

	private static string? FindVeroScript(string configured)
	{
		foreach (string candidate in new[]
		{
			configured,
			Environment.GetEnvironmentVariable("VEROSCRIPT_EXECUTABLE") ?? "",
			Environment.GetEnvironmentVariable("VEROSCRIPT_SDK") ?? ""
		}

		)
		{
			if (string.IsNullOrWhiteSpace(candidate))
			{
				continue;
			}

			string full = Path.GetFullPath(candidate);
			if (File.Exists(full))
			{
				return full;
			}
			if (Directory.Exists(full))
			{
				string name = OperatingSystem.IsWindows() ? "VeroScript.exe" : "veroscript";
				foreach (string nested in new[]
				{
					Path.Combine(full, name),
					Path.Combine(full, "bin", name)
				}

				)
				{
					if (File.Exists(nested))
					{
						return nested;
					}
				}
			}
		}

		string executableName = OperatingSystem.IsWindows() ? "VeroScript.exe" : "veroscript";
		foreach (string directory in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
		{
			string path = Path.Combine(directory.Trim('"'), executableName);
			if (File.Exists(path))
			{
				return path;
			}
		}

		return null;
	}

	private static bool ValidTimeout(TimeSpan timeout) => timeout > TimeSpan.Zero && timeout != Timeout.InfiniteTimeSpan && double.IsFinite(timeout.TotalMilliseconds);
	private static bool HasErrors(IEnumerable<CSharpDiagnostic> diagnostics) => diagnostics.Any(item => item.Severity == CSharpDiagnosticSeverity.Error);
	private static CSharpDiagnostic Error(string code, string message, string file = "") => new(CSharpDiagnosticSeverity.Error, code, message, string.IsNullOrEmpty(file) ? CSharpSourceLocation.None : new(file, 1, 1, 0, 0));
	private static string NormalizePath(string path) => path.Replace('\\', '/');
	private static bool HasUnpairedSurrogate(string value)
	{
		for (int index = 0; index < value.Length; ++index)
		{
			if (char.IsHighSurrogate(value[index]))
			{
				if (index + 1 >= value.Length || !char.IsLowSurrogate(value[index + 1]))
				{
					return true;
				}

				++index;
			}
			else if (char.IsLowSurrogate(value[index]))
			{
				return true;
			}
		}

		return false;
	}

	private static string Quote(string value)
	{
		var result = new StringBuilder(value.Length + 2).Append('"');
		foreach (char character in value)
		{
			result.Append(character switch
			{
				'\\' => "\\\\",
				'"' => "\\\"",
				'\n' => "\\n",
				'\r' => "\\r",
				'\t' => "\\t",
				'\0' => "\\0",
				'\a' => "\\a",
				'\b' => "\\b",
				'\f' => "\\f",
				'\v' => "\\v",
				< ' ' or '\u007f' => "\\x" + ((int)character).ToString("X2", CultureInfo.InvariantCulture),
				_ => character.ToString()});
		}

		return result.Append('"').ToString();
	}

	private static void TryDelete(string path)
	{
		try
		{
			File.Delete(path);
		}
		catch (Exception exception)when (exception is IOException or UnauthorizedAccessException)
		{
		}
	}

	private static JsonSerializerOptions JsonOptions { get; } = new()
	{
		WriteIndented = true
	};

	private sealed record ProjectContext(SyntaxTree[] Trees, CSharpCompilation Compilation, IMethodSymbol? EntryPoint, IReadOnlyList<CSharpDiagnostic> Diagnostics)
	{
		// All emitters share the same immutable compilation. Scan its identifiers
		// only when a generated helper needs collision checking, and only once.
		public Lazy<IReadOnlySet<string>> IdentifierNames { get; } = new(() => Trees
			.SelectMany(tree => tree.GetRoot().DescendantTokens())
			.Select(token => token.ValueText).ToHashSet(StringComparer.Ordinal));
	}
	private sealed record EmittedSource(string Source, string SourceMapJson);
	private sealed partial class Emitter(SemanticModel model, string sourcePath, string generatedPath, bool exportDeclarations, IMethodSymbol? entryPoint, Lazy<IReadOnlySet<string>> identifierNames, List<CSharpDiagnostic> diagnostics)
	{
		private readonly List<string> lines = [];
		private readonly List<object> mappings = [];
		private int indent;
		public void EmitCompilationUnit(CompilationUnitSyntax unit)
		{
			Line("// SPDX-FileCopyrightText: VanstySanic");
			Line("// SPDX-License-Identifier: MIT");
			Line();
			foreach (UsingDirectiveSyntax directive in unit.Usings)
			{
				EmitUsing(directive);
			}

			if (unit.Usings.Count != 0)
			{
				Line();
			}

			foreach (MemberDeclarationSyntax member in unit.Members)
			{
				EmitMember(member);
			}
		}

		private void EmitUsing(UsingDirectiveSyntax directive)
		{
			if (directive.StaticKeyword.IsKind(SyntaxKind.StaticKeyword))
			{
				Fail(directive, "using static directive");
				return;
			}

			if (directive.Name is null)
			{
				Fail(directive, "unresolved using directive");
				return;
			}

			if (directive.Alias is not null)
			{
				ISymbol? target = model.GetSymbolInfo(directive.Name).Symbol;
				if (target is not ITypeSymbol type)
				{
					Fail(directive, "namespace using alias");
					return;
				}

				Line($"using {directive.Alias.Name.Identifier.ValueText} = " + TypeName(type, directive.Name) + ";", directive);
				return;
			}

			string name = directive.Name.ToString();
			if (name == "System" || name.StartsWith("System.", StringComparison.Ordinal))
			{
				return;
			}
			Line("using namespace " + name.Replace(".", "::", StringComparison.Ordinal) + ";", directive);
		}

		public EmittedSource Finish()
		{
			EmitNumericHelpers();
			string source = string.Join(Environment.NewLine, lines) + Environment.NewLine;
			var map = new
			{
				version = 1,
				source = sourcePath,
				generated = generatedPath,
				mappings
			};
			return new(source, JsonSerializer.Serialize(map, JsonOptions) + Environment.NewLine);
		}

		public void Line(string text = "", SyntaxNode? node = null)
		{
			lines.Add(new string ('\t', indent) + text);
			if (node is not null)
			{
				FileLinePositionSpan span = node.GetLocation().GetLineSpan();
				mappings.Add(new { generatedLine = lines.Count, sourcePath, sourceLine = span.StartLinePosition.Line + 1, sourceColumn = span.StartLinePosition.Character + 1 });
			}
		}

		private void EmitMember(MemberDeclarationSyntax member)
		{
			switch (member)
			{
				case NamespaceDeclarationSyntax declaration:
				{
					EmitNamespace(declaration.Name.ToString(), declaration.Members, declaration);
					break;
				}

				case FileScopedNamespaceDeclarationSyntax declaration:
				{
					EmitNamespace(declaration.Name.ToString(), declaration.Members, declaration);
					break;
				}

				case ClassDeclarationSyntax declaration:
				{
					EmitType(declaration);
					break;
				}

				case StructDeclarationSyntax declaration:
				{
					EmitType(declaration);
					break;
				}

				case InterfaceDeclarationSyntax declaration:
				{
					EmitType(declaration);
					break;
				}

				case EnumDeclarationSyntax declaration:
				{
					EmitEnum(declaration);
					break;
				}

				case GlobalStatementSyntax statement:
				{
					EmitStatement(statement.Statement);
					break;
				}

				default:
				{
					Fail(member, $"top-level {member.Kind()}");
					break;
				}
			}
		}

		private void EmitNamespace(string name, SyntaxList<MemberDeclarationSyntax> members, SyntaxNode node)
		{
			string[] segments = name.Split('.', StringSplitOptions.RemoveEmptyEntries);
			foreach (string segment in segments)
			{
				Line($"namespace {segment}", node);
				Line("{");
				++indent;
			}

			foreach (MemberDeclarationSyntax member in members)
			{
				EmitMember(member);
			}

			foreach (string _ in segments)
			{
				--indent;
				Line("}");
			}
		}

		private void EmitEnum(EnumDeclarationSyntax declaration)
		{
			RejectAttributes(declaration.AttributeLists);
			INamedTypeSymbol? symbol = model.GetDeclaredSymbol(declaration);
			if (symbol?.EnumUnderlyingType?.SpecialType == SpecialType.System_UInt64)
			{
				Fail(declaration, "unsigned 64-bit enum underlying type");
				return;
			}

			string prefix = ExportPrefix(symbol?.DeclaredAccessibility);
			string underlying = declaration.BaseList is null ? "int" : TypeName(model.GetTypeInfo(declaration.BaseList.Types[0].Type).Type, declaration.BaseList.Types[0].Type);
			Line($"{prefix}enum class {declaration.Identifier.ValueText} : {underlying}", declaration);
			Line("{");
			++indent;
			for (int index = 0; index < declaration.Members.Count; ++index)
			{
				EnumMemberDeclarationSyntax member = declaration.Members[index];
				RejectAttributes(member.AttributeLists);
				string value = member.EqualsValue is null ? "" : " = " + Expression(member.EqualsValue.Value);
				Line(member.Identifier.ValueText + value + (index + 1 == declaration.Members.Count ? "" : ","), member);
			}

			--indent;
			Line("};");
		}

		private void EmitType(TypeDeclarationSyntax declaration)
		{
			INamedTypeSymbol? symbol = model.GetDeclaredSymbol(declaration);
			if (symbol is null)
			{
				Fail(declaration, "unresolved type declaration");
				return;
			}

			bool isStatic = declaration.Modifiers.Any(SyntaxKind.StaticKeyword);
			if (isStatic)
			{
				RejectAttributes(declaration.AttributeLists);
				bool entryContainer = IsEntryContainer(symbol);
				if (!entryContainer)
				{
					Line("namespace " + declaration.Identifier.ValueText, declaration);
					Line("{");
					++indent;
				}

				foreach (MemberDeclarationSyntax member in declaration.Members)
				{
					EmitStaticContainerMember(member);
				}

				if (!entryContainer)
				{
					--indent;
					Line("}");
				}

				return;
			}

			if (declaration is InterfaceDeclarationSyntax)
			{
				Fail(declaration, "interface declaration (C# interface conversion is not implemented)");
				return;
			}

			if (declaration.Modifiers.Any(SyntaxKind.PartialKeyword) && symbol.DeclaringSyntaxReferences.Length > 1)
			{
				Fail(declaration, "partial type split across declarations");
				return;
			}

			if (declaration is StructDeclarationSyntax && (declaration.Modifiers.Any(SyntaxKind.ReadOnlyKeyword) || declaration.Modifiers.Any(SyntaxKind.RefKeyword)))
			{
				Fail(declaration, "readonly or ref struct declaration");
				return;
			}

			if (symbol.Interfaces.Length != 0)
			{
				Fail(declaration, "implemented interface (C# interface dispatch conversion is not implemented)");
				return;
			}

			string keyword = declaration is StructDeclarationSyntax ? "struct" : "class";
			RejectAttributes(declaration.AttributeLists);
			if (symbol.IsAbstract && !symbol.GetMembers().OfType<IMethodSymbol>().Any(method => method.IsAbstract))
			{
				Fail(declaration, "abstract class without an abstract member");
			}

			string template = TypeParameters(declaration.TypeParameterList, declaration);
			string bases = "";
			if (declaration.BaseList is not null)
			{
				string[] names = declaration.BaseList.Types.Select(item => TypeName(model.GetTypeInfo(item.Type).Type, item.Type)).ToArray();
				if (names.Length != 0)
				{
					bases = " : " + string.Join(", ", names.Select(name => "public " + name));
				}
			}

			string final = declaration.Modifiers.Any(SyntaxKind.SealedKeyword) ? " final" : "";
			Line($"{ExportPrefix(symbol.DeclaredAccessibility)}{template}{keyword} {symbol.Name}{final}{bases}", declaration);
			Line("{");
			++indent;
			string currentAccess = declaration is StructDeclarationSyntax ? "public" : "private";
			bool accessWritten = false;
			foreach (MemberDeclarationSyntax member in declaration.Members)
			{
				string access = MemberAccess(member);
				if (!accessWritten || access != currentAccess)
				{
					Line(access + ":");
					currentAccess = access;
					accessWritten = true;
				}

				EmitTypeMember(member, symbol.Name);
			}

			--indent;
			Line("};");
		}

		private void EmitStaticContainerMember(MemberDeclarationSyntax member)
		{
			switch (member)
			{
				case MethodDeclarationSyntax method:
				{
					EmitMethod(method, false);
					break;
				}

				case FieldDeclarationSyntax field:
				{
					EmitField(field, false);
					break;
				}

				// VeroScript does not currently permit type declarations inside a
				// namespace-backed static container. Emitting one would defer the
				// failure until the VeroScript compiler validates the output.
				case EnumDeclarationSyntax nested:
				{
					Fail(nested, "nested type declaration");
					break;
				}

				default:
				{
					Fail(member, $"static class member {member.Kind()}");
					break;
				}
			}
		}

		private void EmitTypeMember(MemberDeclarationSyntax member, string ownerName)
		{
			switch (member)
			{
				case FieldDeclarationSyntax field:
				{
					EmitField(field, true);
					break;
				}

				case PropertyDeclarationSyntax property:
				{
					EmitAutoProperty(property);
					break;
				}

				case ConstructorDeclarationSyntax constructor:
				{
					EmitConstructor(constructor, ownerName);
					break;
				}

				case MethodDeclarationSyntax method:
				{
					EmitMethod(method, true);
					break;
				}

				// Keep this fail-closed until VeroScript gains nested type syntax.
				// Flattening would alter C# type identity and member lookup.
				case EnumDeclarationSyntax nested:
				{
					Fail(nested, "nested type declaration");
					break;
				}

				default:
				{
					Fail(member, $"type member {member.Kind()}");
					break;
				}
			}
		}

		private string MemberAccess(MemberDeclarationSyntax member)
		{
			Accessibility accessibility = member switch
			{
				FieldDeclarationSyntax field when field.Declaration.Variables.Count != 0 => model.GetDeclaredSymbol(field.Declaration.Variables[0])?.DeclaredAccessibility ?? Accessibility.Private,
				PropertyDeclarationSyntax property => model.GetDeclaredSymbol(property)?.DeclaredAccessibility ?? Accessibility.Private,
				ConstructorDeclarationSyntax constructor => model.GetDeclaredSymbol(constructor)?.DeclaredAccessibility ?? Accessibility.Private,
				MethodDeclarationSyntax method => model.GetDeclaredSymbol(method)?.DeclaredAccessibility ?? Accessibility.Private,
				BaseTypeDeclarationSyntax type => model.GetDeclaredSymbol(type)?.DeclaredAccessibility ?? Accessibility.Private,
				_ => Accessibility.Private
			};
			return accessibility switch
			{
				Accessibility.Public => "public",
				Accessibility.Protected => "protected",
				Accessibility.Private => "private",
				_ => FailAccess(member, accessibility)};
		}

		private string FailAccess(SyntaxNode node, Accessibility accessibility)
		{
			Fail(node, $"accessibility {accessibility}");
			return "private";
		}

		private void EmitField(FieldDeclarationSyntax declaration, bool member)
		{
			RejectAttributes(declaration.AttributeLists);
			bool isConst = declaration.Modifiers.Any(SyntaxKind.ConstKeyword);
			bool isReadonly = declaration.Modifiers.Any(SyntaxKind.ReadOnlyKeyword);
			if (declaration.Modifiers.Any(SyntaxKind.VolatileKeyword) || declaration.Modifiers.Any(SyntaxKind.RequiredKeyword))
			{
				Fail(declaration, "volatile or required field");
				return;
			}

			if (isReadonly && !isConst)
			{
				Fail(declaration, "readonly field (constructor-only mutation cannot be preserved)");
				return;
			}

			ITypeSymbol? fieldType = model.GetTypeInfo(declaration.Declaration.Type).Type;
			string type = TypeName(fieldType, declaration.Declaration.Type);
			string staticPrefix = member && declaration.Modifiers.Any(SyntaxKind.StaticKeyword) ? "static " : "";
			string constPrefix = isConst ? "const " : "";
			foreach (VariableDeclaratorSyntax variable in declaration.Declaration.Variables)
			{
				if (variable.Initializer is null && HasUnsupportedNullDefault(fieldType))
				{
					Fail(variable, "uninitialized string, array or mapped collection field (CLR null default cannot be preserved)");
					continue;
				}

				if (declaration.Modifiers.Any(SyntaxKind.StaticKeyword) && variable.Initializer is not null && !CanEmitStaticInitializer(variable.Initializer.Value))
				{
					continue;
				}

				string initializer = variable.Initializer is null ? "" : " = " + Expression(variable.Initializer.Value);
				if (variable.Initializer is null && !member && fieldType?.IsReferenceType == true)
				{
					// Namespace-backed static containers need an explicit
					// global initializer; script class references preserve null.
					initializer = " = nullptr";
				}

				Line($"{staticPrefix}{constPrefix}{type} {variable.Identifier.ValueText}{initializer};", variable);
			}
		}

		private void EmitAutoProperty(PropertyDeclarationSyntax property)
		{
			RejectAttributes(property.AttributeLists);
			if (property.Modifiers.Any(SyntaxKind.VirtualKeyword) || property.Modifiers.Any(SyntaxKind.OverrideKeyword) || property.Modifiers.Any(SyntaxKind.AbstractKeyword) || property.Modifiers.Any(SyntaxKind.SealedKeyword) || property.Modifiers.Any(SyntaxKind.RequiredKeyword))
			{
				Fail(property, "virtual, abstract, sealed, override, or required property");
				return;
			}

			if (property.ExpressionBody is not null || property.AccessorList is null || property.AccessorList.Accessors.Any(item => item.Body is not null || item.ExpressionBody is not null) || property.AccessorList.Accessors.Any(item => item.Keyword.IsKind(SyntaxKind.InitKeyword)))
			{
				Fail(property, "non-trivial property");
				return;
			}

			bool getter = property.AccessorList.Accessors.Any(item => item.Keyword.IsKind(SyntaxKind.GetKeyword));
			bool setter = property.AccessorList.Accessors.Any(item => item.Keyword.IsKind(SyntaxKind.SetKeyword));
			foreach (AccessorDeclarationSyntax accessor in property.AccessorList.Accessors)
			{
				RejectAttributes(accessor.AttributeLists);
			}

			if (!getter || !setter || property.AccessorList.Accessors.Any(item => item.Modifiers.Count != 0))
			{
				Fail(property, "property without symmetric automatic get/set access");
				return;
			}

			ITypeSymbol? propertyType = model.GetTypeInfo(property.Type).Type;
			string type = TypeName(propertyType, property.Type);
			if (property.Initializer is null && HasUnsupportedNullDefault(propertyType))
			{
				Fail(property, "uninitialized string, array or mapped collection automatic property (CLR null default cannot be preserved)");
				return;
			}

			if (property.Modifiers.Any(SyntaxKind.StaticKeyword) && property.Initializer is not null && !CanEmitStaticInitializer(property.Initializer.Value))
			{
				return;
			}

			string staticPrefix = property.Modifiers.Any(SyntaxKind.StaticKeyword) ? "static " : "";
			string initializer = property.Initializer is null ? "" : " = " + Expression(property.Initializer.Value);
			Line($"{staticPrefix}{type} {property.Identifier.ValueText}{initializer};", property);
		}

		private bool CanEmitStaticInitializer(ExpressionSyntax expression)
		{
			if (model.GetConstantValue(expression).HasValue)
			{
				return true;
			}
			Fail(expression, "nonconstant static initializer (CLR type initialization timing cannot be preserved)");
			return false;
		}

		private static bool HasUnsupportedNullDefault(ITypeSymbol? type) => IsMappedContainer(type) || type?.SpecialType == SpecialType.System_String;
		private static bool IsMappedContainer(ITypeSymbol? type) => type is IArrayTypeSymbol || MappedContainerName(type)is not null;
		private static string? MappedContainerName(ITypeSymbol? type) => (type as INamedTypeSymbol)?.OriginalDefinition.ToDisplayString() switch
		{
			"System.Collections.Generic.List<T>" => "array",
			"System.Collections.Generic.Dictionary<TKey, TValue>" => "map",
			"System.Collections.Generic.SortedDictionary<TKey, TValue>" => "ordered_map",
			"System.Collections.Generic.HashSet<T>" => "set",
			"System.Collections.Generic.Queue<T>" => "queue",
			"System.Collections.Generic.Stack<T>" => "stack",
			_ => null
		};
		private static bool IsNullComparisonOperand(ExpressionSyntax expression)
		{
			SyntaxNode operand = expression;
			while (operand.Parent is ParenthesizedExpressionSyntax parent)
			{
				operand = parent;
			}

			return operand.Parent is BinaryExpressionSyntax binary && (binary.IsKind(SyntaxKind.EqualsExpression) || binary.IsKind(SyntaxKind.NotEqualsExpression));
		}

		private void EmitConstructor(ConstructorDeclarationSyntax declaration, string ownerName)
		{
			RejectAttributes(declaration.AttributeLists);
			if (declaration.Modifiers.Any(SyntaxKind.StaticKeyword))
			{
				Fail(declaration, "static constructor (CLR type initialization timing cannot be preserved)");
				return;
			}

			if (declaration.Initializer is not null)
			{
				Fail(declaration.Initializer, "constructor initializer");
				return;
			}

			string parameters = Parameters(declaration.ParameterList.Parameters);
			Line($"{ownerName}({parameters})", declaration);
			EmitCallableBody(declaration.Body, declaration.ExpressionBody, true);
		}

		private void EmitMethod(MethodDeclarationSyntax declaration, bool member)
		{
			RejectAttributes(declaration.AttributeLists);
			IMethodSymbol? symbol = model.GetDeclaredSymbol(declaration);
			if (symbol is null)
			{
				Fail(declaration, "unresolved method");
				return;
			}

			if (declaration.Modifiers.Any(SyntaxKind.AsyncKeyword))
			{
				Fail(declaration, "async method");
				return;
			}

			if (member && declaration.TypeParameterList is not null)
			{
				Fail(declaration, "generic instance/static member");
				return;
			}

			if (symbol.IsExtensionMethod)
			{
				Fail(declaration, "extension method (extension-call dispatch cannot be preserved)");
				return;
			}

			string template = member ? "" : TypeParameters(declaration.TypeParameterList, declaration);
			string modifiers = member ? (symbol.IsStatic ? "static " : "") + (symbol.IsVirtual || symbol.IsAbstract ? "virtual " : "") : ExportPrefix(symbol.DeclaredAccessibility);
			string returnType = TypeName(symbol.ReturnType, declaration.ReturnType);
			if (symbol.ReturnsByRefReadonly)
			{
				returnType = "const " + returnType + "&";
			}
			else if (symbol.ReturnsByRef)
			{
				returnType += "&";
			}

			string parameters = Parameters(declaration.ParameterList.Parameters);
			string suffix = symbol.IsReadOnly ? " const" : "";
			if (symbol.IsOverride)
			{
				suffix += " override";
			}

			if (symbol.IsSealed)
			{
				suffix += " final";
			}

			if (symbol.IsExtern || declaration.Modifiers.Any(SyntaxKind.UnsafeKeyword) || declaration.Modifiers.Any(SyntaxKind.NewKeyword))
			{
				Fail(declaration, "extern, unsafe, or explicitly hiding method");
				return;
			}

			if (symbol.IsAbstract)
			{
				Line($"{template}{modifiers}{returnType} {symbol.Name}({parameters}){suffix} = 0;", declaration);
				return;
			}

			string emittedName = !member && IsEntryPoint(symbol) ? "main" : symbol.Name;
			Line($"{template}{modifiers}{returnType} {emittedName}({parameters}){suffix}", declaration);
			EmitCallableBody(declaration.Body, declaration.ExpressionBody, symbol.ReturnsVoid, symbol.ReturnsByRef || symbol.ReturnsByRefReadonly);
		}

		private void EmitCallableBody(BlockSyntax? body, ArrowExpressionClauseSyntax? expressionBody, bool returnsVoid, bool returnsByRef = false)
		{
			if (body is null && expressionBody is null)
			{
				Line(";");
				return;
			}

			Line("{");
			++indent;
			if (body is not null)
			{
				foreach (StatementSyntax statement in body.Statements)
				{
					EmitStatement(statement);
				}
			}
			else if (expressionBody is not null)
			{
				ExpressionSyntax expression = returnsByRef && expressionBody.Expression is RefExpressionSyntax reference ? reference.Expression : expressionBody.Expression;
				Line((returnsVoid ? "" : "return ") + Expression(expression) + ";", expressionBody);
			}

			--indent;
			Line("}");
		}

		private string Parameters(SeparatedSyntaxList<ParameterSyntax> parameters)
		{
			var result = new List<string>(parameters.Count);
			foreach (ParameterSyntax parameter in parameters)
			{
				RejectAttributes(parameter.AttributeLists);
				IParameterSymbol? symbol = model.GetDeclaredSymbol(parameter);
				if (symbol is null || parameter.Type is null)
				{
					Fail(parameter, "unresolved or untyped parameter");
					continue;
				}

				if (symbol.IsParams && symbol.Type is not IArrayTypeSymbol)
				{
					Fail(parameter, "non-array params parameter");
					continue;
				}

				string prefix = symbol.RefKind switch
				{
					RefKind.Ref or RefKind.Out => "ref ",
					RefKind.In or RefKind.RefReadOnlyParameter => "in ",
					_ => ""
				};
				string parameterType = symbol.IsParams ? TypeName(((IArrayTypeSymbol)symbol.Type).ElementType, parameter.Type) + "..." : TypeName(symbol.Type, parameter.Type);
				string value = prefix + parameterType + " " + symbol.Name;
				if (parameter.Default is not null)
				{
					value += " = " + Expression(parameter.Default.Value);
				}

				result.Add(value);
			}

			return string.Join(", ", result);
		}

		private string TypeParameters(TypeParameterListSyntax? parameters, SyntaxNode owner)
		{
			if (parameters is null)
			{
				return "";
			}
			foreach (TypeParameterSyntax parameter in parameters.Parameters)
			{
				RejectAttributes(parameter.AttributeLists);
			}

			SyntaxNode? constraints = owner switch
			{
				TypeDeclarationSyntax type => type.ConstraintClauses.FirstOrDefault(),
				MethodDeclarationSyntax method => method.ConstraintClauses.FirstOrDefault(),
				_ => null
			};
			if (constraints is not null)
			{
				Fail(constraints, "generic constraints");
			}

			return "template<" + string.Join(", ", parameters.Parameters.Select(parameter => parameter.Identifier.ValueText)) + "> ";
		}

		private string TypeName(ITypeSymbol? type, SyntaxNode node)
		{
			if (type is null)
			{
				Fail(node, "unresolved type");
				return "void";
			}

			if (type.NullableAnnotation == NullableAnnotation.Annotated || type.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
			{
				Fail(node, $"nullable type {type.ToDisplayString()}");
				return "void";
			}

			string? primitive = type.SpecialType switch
			{
				SpecialType.System_Void => "void",
				SpecialType.System_Boolean => "bool",
				SpecialType.System_SByte => "int8",
				SpecialType.System_Byte => "uint8",
				SpecialType.System_Int16 => "int16",
				SpecialType.System_UInt16 => "uint16",
				SpecialType.System_Int32 => "int",
				SpecialType.System_UInt32 => "unsigned int",
				SpecialType.System_Int64 => "int64",
				SpecialType.System_UInt64 => "uint64",
				SpecialType.System_Single => "float",
				SpecialType.System_Double => "double",
				SpecialType.System_Char => FailType(node, "System.Char is UTF-16 while VeroScript char is an 8-bit scalar"),
				SpecialType.System_String => "string",
				_ => null
			};
			if (primitive is not null)
			{
				return primitive;
			}
			if (type is IArrayTypeSymbol array)
			{
				if (array.Rank != 1)
				{
					Fail(node, "multidimensional array");
					return "void";
				}

				return $"array<{TypeName(array.ElementType, node)}>";
			}

			if (type is ITypeParameterSymbol parameter)
			{
				return parameter.Name;
			}
			if (type is INamedTypeSymbol named)
			{
				if (named.TypeKind == TypeKind.Delegate && named.DelegateInvokeMethod is IMethodSymbol invoke)
				{
					string[] parameters = invoke.Parameters.Select(parameter =>
					{
						string prefix = parameter.RefKind switch
						{
							RefKind.Ref or RefKind.Out => "ref ",
							RefKind.In or RefKind.RefReadOnlyParameter => "in ",
							_ => ""
						};
						return prefix + TypeName(parameter.Type, node);
					}).ToArray();
					string returnType = TypeName(invoke.ReturnType, node);
					if (invoke.ReturnsByRefReadonly)
					{
						returnType = "const " + returnType + "&";
					}
					else if (invoke.ReturnsByRef)
					{
						returnType += "&";
					}

					return "fn(" + string.Join(",", parameters) + ")->" + returnType;
				}

				string? container = MappedContainerName(named);
				if (container is not null)
				{
					return container + "<" + string.Join(",", named.TypeArguments.Select(argument => TypeName(argument, node))) + ">";
				}

				if (named.TypeKind is TypeKind.Class or TypeKind.Struct or TypeKind.Enum)
				{
					string namespaceName = named.ContainingNamespace is { IsGlobalNamespace: false } ? named.ContainingNamespace.ToDisplayString().Replace(".", "::", StringComparison.Ordinal) : "";
					var containingTypes = new Stack<INamedTypeSymbol>();
					for (INamedTypeSymbol? current = named; current is not null; current = current.ContainingType)
					{
						containingTypes.Push(current);
					}

					string[] names = containingTypes.Select(current =>
					{
						string result = current.Name;
						if (current.Arity != 0)
						{
							result += "<" + string.Join(",", current.TypeArguments.Skip(current.TypeArguments.Length - current.Arity).Select(argument => TypeName(argument, node))) + ">";
						}

						return result;
					}).ToArray();
					return namespaceName.Length == 0 ? string.Join("::", names) : namespaceName + "::" + string.Join("::", names);
				}
			}

			Fail(node, $"type {type.ToDisplayString()}");
			return "void";
		}

		private void EmitStatement(StatementSyntax statement)
		{
			switch (statement)
			{
				case BlockSyntax block:
				{
					Line("{", block);
					++indent;
					foreach (StatementSyntax child in block.Statements)
					{
						EmitStatement(child);
					}

					--indent;
					Line("}");
					break;
				}

				case LocalDeclarationStatementSyntax local:
				{
					EmitLocal(local);
					break;
				}

				case ExpressionStatementSyntax expression:
				{
					Line(Expression(expression.Expression) + ";", expression);
					break;
				}

				case ReturnStatementSyntax @return:
				{
					ExpressionSyntax? returnExpression = @return.Expression;
					if (returnExpression is RefExpressionSyntax referenceReturn)
					{
						returnExpression = referenceReturn.Expression;
					}

					Line(returnExpression is null ? "return;" : "return " + Expression(returnExpression) + ";", @return);
					break;
				}

				case IfStatementSyntax @if:
				{
					Line("if (" + Expression(@if.Condition) + ")", @if);
					EmitEmbedded(@if.Statement);
					if (@if.Else is not null)
					{
						Line("else");
						EmitEmbedded(@if.Else.Statement);
					}

					break;
				}

				case WhileStatementSyntax @while:
				{
					Line("while (" + Expression(@while.Condition) + ")", @while);
					EmitEmbedded(@while.Statement);
					break;
				}

				case DoStatementSyntax @do:
				{
					Line("do", @do);
					EmitEmbedded(@do.Statement);
					Line("while (" + Expression(@do.Condition) + ");");
					break;
				}

				case ForStatementSyntax @for:
				{
					EmitFor(@for);
					break;
				}

				case ForEachStatementSyntax each:
				{
					if (!each.AwaitKeyword.IsKind(SyntaxKind.None) || each.Type is RefTypeSyntax)
					{
						Fail(each, "await or ref foreach");
						break;
					}

					Line($"for ({TypeName(model.GetTypeInfo(each.Type).Type, each.Type)} " + $"{each.Identifier.ValueText} : {Expression(each.Expression)})", each);
					EmitEmbedded(each.Statement);
					break;
				}

				case BreakStatementSyntax:
				{
					Line("break;", statement);
					break;
				}

				case ContinueStatementSyntax:
				{
					Line("continue;", statement);
					break;
				}

				case ThrowStatementSyntax @throw:
				{
					Line(@throw.Expression is null ? "throw;" : "throw " + Expression(@throw.Expression) + ";", @throw);
					break;
				}

				case TryStatementSyntax @try:
				{
					EmitTry(@try);
					break;
				}

				case SwitchStatementSyntax @switch:
				{
					EmitSwitch(@switch);
					break;
				}

				case LabeledStatementSyntax label:
				{
					Line(label.Identifier.ValueText + ":", label);
					EmitStatement(label.Statement);
					break;
				}

				case GotoStatementSyntax @goto:
				{
					if (@goto.CaseOrDefaultKeyword.IsKind(SyntaxKind.CaseKeyword))
					{
						Line("goto case " + Expression(@goto.Expression!) + ";", @goto);
					}
					else if (@goto.CaseOrDefaultKeyword.IsKind(SyntaxKind.DefaultKeyword))
					{
						Line("goto default;", @goto);
					}
					else
					{
						Line("goto " + @goto.Expression! + ";", @goto);
					}

					break;
				}

				case EmptyStatementSyntax:
				{
					Line(";", statement);
					break;
				}

				default:
				{
					Fail(statement, $"statement {statement.Kind()}");
					break;
				}
			}
		}

		private void EmitEmbedded(StatementSyntax statement)
		{
			if (statement is BlockSyntax)
			{
				EmitStatement(statement);
			}
			else
			{
				Line("{");
				++indent;
				EmitStatement(statement);
				--indent;
				Line("}");
			}
		}

		private void EmitLocal(LocalDeclarationStatementSyntax local)
		{
			if (!local.UsingKeyword.IsKind(SyntaxKind.None) || !local.AwaitKeyword.IsKind(SyntaxKind.None))
			{
				Fail(local, "using or await-using local declaration");
				return;
			}

			RefTypeSyntax? referenceType = local.Declaration.Type as RefTypeSyntax;
			string type = referenceType is not null ? (referenceType.ReadOnlyKeyword.IsKind(SyntaxKind.ReadOnlyKeyword) ? "const " : "") + TypeName(model.GetTypeInfo(referenceType.Type).Type, referenceType.Type) + "&" : local.Declaration.Type.IsVar ? "auto" : TypeName(model.GetTypeInfo(local.Declaration.Type).Type, local.Declaration.Type);
			if (local.Modifiers.Any(SyntaxKind.ConstKeyword))
			{
				type = "const " + type;
			}

			foreach (VariableDeclaratorSyntax variable in local.Declaration.Variables)
			{
				ExpressionSyntax? initializerExpression = variable.Initializer?.Value;
				if (referenceType is not null && initializerExpression is RefExpressionSyntax reference)
				{
					initializerExpression = reference.Expression;
				}

				string initializer = initializerExpression is null ? "" : " = " + Expression(initializerExpression);
				Line($"{type} {variable.Identifier.ValueText}{initializer};", variable);
			}
		}

		private void EmitFor(ForStatementSyntax statement)
		{
			string initializer = "";
			if (statement.Declaration is not null)
			{
				string type = statement.Declaration.Type.IsVar ? "auto" : TypeName(model.GetTypeInfo(statement.Declaration.Type).Type, statement.Declaration.Type);
				initializer = type + " " + string.Join(", ", statement.Declaration.Variables.Select(item => item.Identifier.ValueText + (item.Initializer is null ? "" : " = " + Expression(item.Initializer.Value))));
			}
			else
			{
				initializer = string.Join(", ", statement.Initializers.Select(Expression));
			}

			string condition = statement.Condition is null ? "" : Expression(statement.Condition);
			string increments = string.Join(", ", statement.Incrementors.Select(Expression));
			Line($"for ({initializer}; {condition}; {increments})", statement);
			EmitEmbedded(statement.Statement);
		}

		private void EmitTry(TryStatementSyntax statement)
		{
			Line("try", statement);
			EmitEmbedded(statement.Block);
			foreach (CatchClauseSyntax clause in statement.Catches)
			{
				if (clause.Filter is not null)
				{
					Fail(clause.Filter, "catch filter");
					continue;
				}

				string declaration = "...";
				if (clause.Declaration is not null)
				{
					string type = TypeName(model.GetTypeInfo(clause.Declaration.Type).Type, clause.Declaration.Type);
					declaration = type + (clause.Declaration.Identifier.IsKind(SyntaxKind.None) ? "" : " " + clause.Declaration.Identifier.ValueText);
				}

				Line($"catch ({declaration})", clause);
				EmitEmbedded(clause.Block);
			}

			if (statement.Finally is not null)
			{
				Line("finally", statement.Finally);
				EmitEmbedded(statement.Finally.Block);
			}
		}

		private void EmitSwitch(SwitchStatementSyntax statement)
		{
			Line("switch (" + Expression(statement.Expression) + ")", statement);
			Line("{");
			++indent;
			foreach (SwitchSectionSyntax section in statement.Sections)
			{
				foreach (SwitchLabelSyntax label in section.Labels)
				{
					if (label is CaseSwitchLabelSyntax @case)
					{
						Line("case " + Expression(@case.Value) + ":", label);
					}
					else if (label is DefaultSwitchLabelSyntax)
					{
						Line("default:", label);
					}
					else
					{
						Fail(label, "pattern switch label");
					}
				}

				++indent;
				foreach (StatementSyntax child in section.Statements)
				{
					EmitStatement(child);
				}

				--indent;
			}

			--indent;
			Line("}");
		}

		private string Expression(ExpressionSyntax expression)
		{
			if (TryStringConstant(expression, out string stringConstant))
			{
				return stringConstant;
			}
			if (TryNumericConstant(expression, out string constant))
			{
				return constant;
			}
			if (model.GetTypeInfo(expression)is { Type.SpecialType: SpecialType.System_Int64 or SpecialType.System_UInt64, ConvertedType.SpecialType: SpecialType.System_Single })
			{
				return FailLiteral(expression, "64-bit integer to float conversion (different rounding semantics)");
			}

			switch (expression)
			{
				case LiteralExpressionSyntax literal:
				{
					if (literal.IsKind(SyntaxKind.NullLiteralExpression))
					{
						ITypeSymbol? convertedType = model.GetTypeInfo(literal).ConvertedType;
						return HasUnsupportedNullDefault(convertedType) && !(IsMappedContainer(convertedType) && IsNullComparisonOperand(literal)) ? FailLiteral(literal, "null string, array or mapped collection value (CLR null semantics cannot be preserved)") : "nullptr";
					}

					if (literal.IsKind(SyntaxKind.TrueLiteralExpression))
					{
						return "true";
					}
					if (literal.IsKind(SyntaxKind.FalseLiteralExpression))
					{
						return "false";
					}
					if (literal.Token.Value is string text)
					{
						if (HasUnpairedSurrogate(text))
						{
							return FailLiteral(literal, "string literal containing an unpaired UTF-16 surrogate");
						}

						return Quote(text);
					}

					if (literal.Token.Value is char)
					{
						return FailLiteral(literal, "System.Char literal (VeroScript char has different width and range)");
					}

					return NumericLiteral(literal);
				}

				case IdentifierNameSyntax identifier:
					return model.GetSymbolInfo(identifier).Symbol is IMethodSymbol method && IsEntryPoint(method) ? "main" : identifier.Identifier.ValueText;
				case ThisExpressionSyntax:
					return "this";
				case BaseExpressionSyntax:
					return "Super";
				case ParenthesizedExpressionSyntax parenthesized:
					return "(" + Expression(parenthesized.Expression) + ")";
				case MemberAccessExpressionSyntax member:
				{
					if (model.GetSymbolInfo(member).Symbol is IFieldSymbol { IsStatic: true, Name: "Empty", ContainingType.SpecialType: SpecialType.System_String })
					{
						return "\"\"";
					}

					if (model.GetSymbolInfo(member).Symbol is IMethodSymbol entryMethod && IsEntryPoint(entryMethod))
					{
						return "main";
					}

					ISymbol? receiver = model.GetSymbolInfo(member.Expression).Symbol;
					if (receiver is INamedTypeSymbol receiverType && IsEntryContainer(receiverType))
					{
						return MemberName(member.Name);
					}

					string separator = receiver is INamespaceOrTypeSymbol ? "::" : ".";
					return Expression(member.Expression) + separator + MemberName(member.Name);
				}

				case ElementAccessExpressionSyntax element:
					return Expression(element.Expression) + "[" + string.Join(", ", element.ArgumentList.Arguments.Select(item => Expression(item.Expression))) + "]";
				case InvocationExpressionSyntax invocation:
					return Invocation(invocation);
				case ObjectCreationExpressionSyntax creation:
				{
					if (creation.Initializer is not null)
					{
						Fail(creation.Initializer, "object or collection initializer");
						return "0";
					}

					return TypeName(model.GetTypeInfo(creation).Type ?? model.GetTypeInfo(creation.Type).Type, creation.Type) + "(" + string.Join(", ", creation.ArgumentList?.Arguments.Select(Argument) ?? []) + ")";
				}

				case ImplicitObjectCreationExpressionSyntax creation:
				{
					if (creation.Initializer is not null)
					{
						Fail(creation.Initializer, "object or collection initializer");
						return "0";
					}

					ITypeSymbol? created = model.GetTypeInfo(creation).ConvertedType;
					return TypeName(created, creation) + "(" + string.Join(", ", creation.ArgumentList.Arguments.Select(Argument)) + ")";
				}

				case BinaryExpressionSyntax binary:
				{
					if (binary.OperatorToken.Text is not ("+" or "-" or "*" or "/" or "%" or "<<" or ">>" or "&" or "|" or "^" or "&&" or "||" or "==" or "!=" or "<" or "<=" or ">" or ">="))
					{
						Fail(binary, $"binary operator {binary.OperatorToken.Text}");
						return "0";
					}

					return BinaryExpression(binary);
				}

				case PrefixUnaryExpressionSyntax prefix:
				{
					if (prefix.OperatorToken.Text is not ("+" or "-" or "!" or "~" or "++" or "--"))
					{
						return FailLiteral(prefix, $"prefix unary operator {prefix.OperatorToken.Text}");
					}

					return UnaryExpression(prefix, prefix.Operand, prefix.OperatorToken.Text, false);
				}

				case PostfixUnaryExpressionSyntax postfix:
				{
					if (postfix.OperatorToken.Text is not ("++" or "--"))
					{
						return FailLiteral(postfix, $"postfix unary operator {postfix.OperatorToken.Text}");
					}

					return UnaryExpression(postfix, postfix.Operand, postfix.OperatorToken.Text, true);
				}

				case AssignmentExpressionSyntax assignment:
				{
					if (assignment.OperatorToken.Text is not ("=" or "+=" or "-=" or "*=" or "/=" or "%=" or "&=" or "|=" or "^=" or "<<=" or ">>="))
					{
						return FailLiteral(assignment, $"assignment operator {assignment.OperatorToken.Text}");
					}

					return AssignmentExpression(assignment);
				}

				case ConditionalExpressionSyntax conditional:
				{
					string whenTrue = ConvertedNumericExpression(conditional.WhenTrue, model.GetTypeInfo(conditional.WhenTrue).ConvertedType?.SpecialType ?? SpecialType.None);
					string whenFalse = ConvertedNumericExpression(conditional.WhenFalse, model.GetTypeInfo(conditional.WhenFalse).ConvertedType?.SpecialType ?? SpecialType.None);
					return $"({Expression(conditional.Condition)} ? {whenTrue} : {whenFalse})";
				}

				case CastExpressionSyntax cast:
					return CastExpression(cast);
				case AwaitExpressionSyntax:
				{
					Fail(expression, "await expression");
					return "0";
				}

				case InterpolatedStringExpressionSyntax:
				{
					Fail(expression, "interpolated string");
					return "\"\"";
				}

				case LambdaExpressionSyntax lambda:
					return Lambda(lambda);
				case InitializerExpressionSyntax initializer:
					return "{" + string.Join(", ", initializer.Expressions.Select(Expression)) + "}";
				case ArrayCreationExpressionSyntax array when array.Initializer is not null:
					return Expression(array.Initializer);
				case ImplicitArrayCreationExpressionSyntax array:
					return Expression(array.Initializer);
				default:
				{
					Fail(expression, $"expression {expression.Kind()}");
					return "0";
				}
			}
		}

		private string Lambda(LambdaExpressionSyntax lambda)
		{
			if (!lambda.AsyncKeyword.IsKind(SyntaxKind.None))
			{
				Fail(lambda, "async lambda expression");
				return "0";
			}

			IMethodSymbol? method = model.GetSymbolInfo(lambda).Symbol as IMethodSymbol ?? (model.GetTypeInfo(lambda).ConvertedType as INamedTypeSymbol)?.DelegateInvokeMethod;
			if (method is null)
			{
				Fail(lambda, "unresolved lambda signature");
				return "0";
			}

			SeparatedSyntaxList<ParameterSyntax> syntaxParameters = lambda switch
			{
				SimpleLambdaExpressionSyntax simple => SyntaxFactory.SingletonSeparatedList(simple.Parameter),
				ParenthesizedLambdaExpressionSyntax parenthesized => parenthesized.ParameterList.Parameters,
				_ => default
			};
			var parameters = new List<string>(method.Parameters.Length);
			for (int index = 0; index < method.Parameters.Length; ++index)
			{
				IParameterSymbol parameter = method.Parameters[index];
				ParameterSyntax syntax = syntaxParameters[index];
				RejectAttributes(syntax.AttributeLists);
				if (parameter.IsParams)
				{
					Fail(syntax, "params lambda parameter");
					continue;
				}

				string prefix = parameter.RefKind switch
				{
					RefKind.Ref or RefKind.Out => "ref ",
					RefKind.In or RefKind.RefReadOnlyParameter => "in ",
					_ => ""
				};
				parameters.Add(prefix + TypeName(parameter.Type, syntax) + " " + parameter.Name);
			}

			DataFlowAnalysis? flow = model.AnalyzeDataFlow(lambda);
			bool captures = flow is { Succeeded: true } && flow.CapturedInside.Length != 0;
			string introducer = captures ? "[&]" : "fn";
			string returnType = TypeName(method.ReturnType, lambda);
			if (method.ReturnsByRefReadonly)
			{
				returnType = "const " + returnType + "&";
			}
			else if (method.ReturnsByRef)
			{
				returnType += "&";
			}

			string signature = introducer + "(" + string.Join(", ", parameters) + ") -> " + returnType;
			return lambda.Body switch
			{
				RefExpressionSyntax body => signature + " => " + Expression(body.Expression),
				ExpressionSyntax body => signature + " => " + Expression(body),
				BlockSyntax body => signature + " " + RenderInlineBlock(body),
				_ => FailLiteral(lambda, "lambda body")};
		}

		private string RenderInlineBlock(BlockSyntax block)
		{
			int lineStart = lines.Count;
			int mappingStart = mappings.Count;
			int savedIndent = indent;
			indent = 0;
			EmitStatement(block);
			string result = string.Join(" ", lines.Skip(lineStart).Select(item => item.Trim()));
			lines.RemoveRange(lineStart, lines.Count - lineStart);
			mappings.RemoveRange(mappingStart, mappings.Count - mappingStart);
			indent = savedIndent;
			return result;
		}

		private string Argument(ArgumentSyntax argument)
		{
			if (argument.NameColon is not null)
			{
				Fail(argument, "named argument");
			}

			string prefix = argument.RefKindKeyword.Kind() switch
			{
				SyntaxKind.RefKeyword or SyntaxKind.OutKeyword => "ref ",
				SyntaxKind.InKeyword => "in ",
				_ => ""
			};
			return prefix + Expression(argument.Expression);
		}

		private string NumericLiteral(LiteralExpressionSyntax literal)
		{
			object? value = literal.Token.Value;
			SpecialType type = model.GetTypeInfo(literal).Type?.SpecialType ?? SpecialType.None;
			return type switch
			{
				SpecialType.System_UInt32 => Convert.ToString(value, CultureInfo.InvariantCulture) + "U",
				SpecialType.System_Int64 => Convert.ToString(value, CultureInfo.InvariantCulture) + "L",
				SpecialType.System_UInt64 => Convert.ToString(value, CultureInfo.InvariantCulture) + "UL",
				SpecialType.System_Single => ((float)value!).ToString("R", CultureInfo.InvariantCulture) + "f",
				SpecialType.System_Double => ((double)value!).ToString("R", CultureInfo.InvariantCulture),
				SpecialType.System_Decimal => FailLiteral(literal, "decimal literal"),
				_ => Convert.ToString(value, CultureInfo.InvariantCulture) ?? literal.Token.ValueText
			};
		}

		private string FailLiteral(SyntaxNode node, string detail)
		{
			Fail(node, detail);
			return "0";
		}

		private string FailType(SyntaxNode node, string detail)
		{
			Fail(node, detail);
			return "void";
		}

		private string Invocation(InvocationExpressionSyntax invocation)
		{
			IMethodSymbol? method = model.GetSymbolInfo(invocation).Symbol as IMethodSymbol;
			if (method?.ContainingType.ToDisplayString() == "System.Console" && method.Name is "Write" or "WriteLine")
			{
				return ConsoleInvocation(invocation, method);
			}

			string arguments = string.Join(", ", invocation.ArgumentList.Arguments.Select(Argument));
			if (method is not null && IsEntryPoint(method))
			{
				return "main(" + arguments + ")";
			}
			return Expression(invocation.Expression) + "(" + arguments + ")";
		}

		private bool IsEntryPoint(IMethodSymbol method) => entryPoint is not null && SymbolEqualityComparer.Default.Equals(method.OriginalDefinition, entryPoint.OriginalDefinition);
		private bool IsEntryContainer(INamedTypeSymbol type) => entryPoint is not null && SymbolEqualityComparer.Default.Equals(type.OriginalDefinition, entryPoint.ContainingType.OriginalDefinition);
		private string MemberName(SimpleNameSyntax name)
		{
			if (name is IdentifierNameSyntax identifier)
			{
				return identifier.Identifier.ValueText;
			}
			if (name is GenericNameSyntax generic)
			{
				return generic.Identifier.ValueText + "<" + string.Join(",", generic.TypeArgumentList.Arguments.Select(argument => TypeName(model.GetTypeInfo(argument).Type, argument))) + ">";
			}

			Fail(name, $"member name {name.Kind()}");
			return "";
		}

		private string ExportPrefix(Accessibility? accessibility) => exportDeclarations && accessibility == Accessibility.Public ? "export " : "";
		private void RejectAttributes(SyntaxList<AttributeListSyntax> attributes)
		{
			foreach (AttributeListSyntax attribute in attributes)
			{
				Fail(attribute, "attribute (no explicit VeroScript attribute mapping was configured)");
			}
		}

		private void Fail(SyntaxNode node, string detail)
		{
			FileLinePositionSpan line = node.GetLocation().GetLineSpan();
			diagnostics.Add(new(CSharpDiagnosticSeverity.Error, "VSCT2001", $"Unsupported C# construct: {detail}.", new(sourcePath, line.StartLinePosition.Line + 1, line.StartLinePosition.Character + 1, node.SpanStart, node.Span.Length)));
		}
	}
}
