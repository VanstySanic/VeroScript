// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reflection;
using System.Text;
using VeroScript.CSharpFrontend;
using VeroScript.CSharpTranspiler;

string productVersion = typeof(CSharpTranspiler).Assembly
	.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion
	.Split('+', 2)[0]
	?? typeof(CSharpTranspiler).Assembly.GetName().Version?.ToString(3)
	?? "0.1.1";
if (args is ["--help"] or ["-h"])
{
	PrintUsage(productVersion);
	return 0;
}
if (args is ["--version"])
{
	Console.WriteLine($"VeroScriptCSharpTranspiler {productVersion}");
	return 0;
}

var inputs = new List<string>();
var references = new List<string>();
string outputPath = "";
string sourceMapPath = "";
string outputDirectory = "";
string manifestPath = "";
string projectRoot = "";
string executable = "";
string languageVersion = "latest";
bool validate = true;
double timeoutSeconds = 120;
try
{
	for (int index = 0; index < args.Length; ++index)
	{
		string argument = args[index];
		switch (argument)
		{
			case "-o" or "--output":
				{
					outputPath = RequireValue(args, ref index, argument);
					break;
				}

			case "--source-map":
				{
					sourceMapPath = RequireValue(args, ref index, argument);
					break;
				}

			case "--output-dir":
				{
					outputDirectory = RequireValue(args, ref index, argument);
					break;
				}

			case "--manifest":
				{
					manifestPath = RequireValue(args, ref index, argument);
					break;
				}

			case "--project-root":
				{
					projectRoot = RequireValue(args, ref index, argument);
					break;
				}

			case "--veroscript":
				{
					executable = RequireValue(args, ref index, argument);
					break;
				}

			case "--reference":
				{
					references.Add(RequireValue(args, ref index, argument));
					break;
				}

			case "--language-version":
				{
					languageVersion = RequireValue(args, ref index, argument);
					break;
				}

			case "--timeout":
				{
					timeoutSeconds = double.Parse(RequireValue(args, ref index, argument),
									CultureInfo.InvariantCulture);
					break;
				}

			case "--no-validate":
				{
					validate = false;
					break;
				}

			default:
				{
					if (argument.Length != 0 && argument[0] == '-')
					{
						throw new ArgumentException($"Unknown option '{argument}'.");
					}

					inputs.Add(argument);
					break;
				}
		}
	}
	if (inputs.Count == 0)
	{
		throw new ArgumentException("At least one input is required.");
	}

	if (!double.IsFinite(timeoutSeconds) || timeoutSeconds <= 0)
	{
		throw new ArgumentException("--timeout must be finite and positive.");
	}

	bool projectMode = inputs.Count > 1 || outputDirectory.Length != 0 || manifestPath.Length != 0;
	if (projectMode && outputPath.Length != 0)
	{
		throw new ArgumentException("--output cannot be combined with project mode.");
	}

	if (projectMode && sourceMapPath.Length != 0)
	{
		throw new ArgumentException("--source-map cannot be combined with project mode.");
	}

	var parseOptions = new CSharpParseOptions
	{
		LanguageVersion = languageVersion,
		ReferencePaths = references
	};
	if (!projectMode)
	{
		if (outputPath.Length == 0)
		{
			throw new ArgumentException("--output is required.");
		}

		sourceMapPath = sourceMapPath.Length == 0 ? outputPath + ".map.json" : sourceMapPath;
		ValidatePaths(inputs, [outputPath, sourceMapPath]);
		CSharpTranspileResult result = CSharpTranspiler.TranspileFile(inputs[0], new()
		{
			ValidateGeneratedSource = validate,
			VeroScriptExecutable = executable,
			GeneratedSourcePath = outputPath,
			ValidationTimeout = TimeSpan.FromSeconds(timeoutSeconds),
			ParseOptions = parseOptions
		});
		PrintDiagnostics(result.Diagnostics);
		if (!result.Succeeded) return 1;
		WriteAllAtomically([(outputPath, result.Source), (sourceMapPath, result.SourceMapJson)]);
		return 0;
	}

	if (outputDirectory.Length == 0)
	{
		throw new ArgumentException("--output-dir is required in project mode.");
	}

	manifestPath = manifestPath.Length == 0
			? Path.Combine(outputDirectory, "transpile.manifest.json") : manifestPath;
	CSharpTranspileProjectResult project = CSharpTranspiler.TranspileProject(inputs, new()
	{
		ValidateGeneratedSources = validate,
		VeroScriptExecutable = executable,
		ProjectRoot = projectRoot,
		ValidationTimeout = TimeSpan.FromSeconds(timeoutSeconds),
		ParseOptions = parseOptions
	});
	PrintDiagnostics(project.Diagnostics);
	if (!project.Succeeded) return 1;
	var outputs = new List<(string Path, string Contents)>();
	foreach (CSharpTranspiledFile file in project.Files)
	{
		string path = Path.Combine(outputDirectory, file.OutputName);
		outputs.Add((path, file.Source));
		outputs.Add((path + ".map.json", file.SourceMapJson));
	}
	outputs.Add((manifestPath, project.ManifestJson));
	ValidatePaths(inputs, outputs.Select(item => item.Path));
	WriteAllAtomically(outputs);
	return 0;
}
catch (Exception exception) when (exception is ArgumentException or IOException
	or UnauthorizedAccessException or FormatException or OverflowException)
{
	Console.Error.WriteLine(exception.Message);
	PrintUsage(productVersion);
	return 2;
}

static string RequireValue(string[] arguments, ref int index, string option)
{
	if (++index >= arguments.Length)
	{
		throw new ArgumentException($"Option '{option}' requires a value.");
	}

	return arguments[index];
}

static void PrintDiagnostics(IEnumerable<CSharpDiagnostic> diagnostics)
{
	foreach (CSharpDiagnostic diagnostic in diagnostics)
	{
		string location = string.IsNullOrEmpty(diagnostic.Location.File) ? ""
			: $"{diagnostic.Location.File}({diagnostic.Location.Line},{diagnostic.Location.Column}): ";
		Console.Error.WriteLine($"{location}{diagnostic.Severity.ToString().ToLowerInvariant()} "
			+ $"{diagnostic.Code}: {diagnostic.Message}");
	}
}

static void ValidatePaths(IEnumerable<string> inputs, IEnumerable<string> outputs)
	=> VeroScript.CSharpTools.OutputPathIdentity.Validate(inputs, outputs);

static void WriteAllAtomically(IReadOnlyList<(string Path, string Contents)> outputs)
{
	string nonce = Guid.NewGuid().ToString("N");
	var staged = new List<StagedOutput>(outputs.Count);
	bool succeeded = false;
	try
	{
		for (int index = 0; index < outputs.Count; ++index)
		{
			(string path, string contents) = outputs[index];
			string target = Path.GetFullPath(path);
			Directory.CreateDirectory(Path.GetDirectoryName(target)!);
			if (Directory.Exists(target))
			{
				throw new IOException($"Generated output target is a directory: '{path}'.");
			}

			var item = new StagedOutput(target, target + $".tmp.{nonce}.{index}",
							target + $".bak.{nonce}.{index}", File.Exists(target));
			staged.Add(item);
			File.WriteAllText(item.Temporary, contents, new UTF8Encoding(false));
		}
		foreach (StagedOutput item in staged.Where(item => item.HadOriginal))
		{
			File.Move(item.Target, item.Backup);
			item.OriginalMoved = true;
		}
		foreach (StagedOutput item in staged)
		{
			File.Move(item.Temporary, item.Target);
			item.Committed = true;
		}
		succeeded = true;
	}
	catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
	{
		Exception? rollbackFailure = null;
		foreach (StagedOutput item in staged.Where(item => item.Committed).Reverse())
		{
			try
			{
				File.Delete(item.Target);
			}
			catch (Exception rollback) when (rollback is IOException or UnauthorizedAccessException)
			{
				rollbackFailure ??= rollback;
			}
		}

		foreach (StagedOutput item in staged.Where(item => item.OriginalMoved).Reverse())
		{
			try
			{
				File.Move(item.Backup, item.Target, true);
			}
			catch (Exception rollback) when (rollback is IOException or UnauthorizedAccessException)
			{
				rollbackFailure ??= rollback;
			}
		}

		if (rollbackFailure is not null)
		{
			throw new IOException($"{exception.Message} Rollback failed: {rollbackFailure.Message}",
						new AggregateException(exception, rollbackFailure));
		}

		throw;
	}
	finally
	{
		foreach (StagedOutput item in staged)
		{
			TryDelete(item.Temporary);
			if (succeeded)
			{
				TryDelete(item.Backup);
			}
		}
	}
}

static void TryDelete(string path)
{
	try
	{
		File.Delete(path);
	}
	catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
	{
	}
}

static void PrintUsage(string version)
{
	Console.WriteLine($"VeroScriptCSharpTranspiler {version}");
	Console.WriteLine("Usage: veroscript-csharp-transpiler <input.cs> -o <output.vs> [options]");
	Console.WriteLine("       veroscript-csharp-transpiler <input.cs...> --output-dir <dir> [options]");
	Console.WriteLine("Options:");
	Console.WriteLine("  --source-map <path>          Select the single-file source map path.");
	Console.WriteLine("  --output-dir <dir>           Enable project mode and preserve relative layout.");
	Console.WriteLine("  --manifest <path>            Select the project manifest path.");
	Console.WriteLine("  --project-root <dir>         Root used for project-relative output names.");
	Console.WriteLine("  --reference <assembly.dll>   Add a metadata reference (repeatable).");
	Console.WriteLine("  --language-version <value>   Select the C# language version.");
	Console.WriteLine("  --veroscript <path>          Select the validator executable or SDK root.");
	Console.WriteLine("  --timeout <seconds>          Bound validation; default is 120 seconds.");
	Console.WriteLine("  --no-validate                Skip generated-source compiler validation.");
}

file sealed class StagedOutput(string target, string temporary, string backup, bool hadOriginal)
{
	public string Target { get; } = target;
	public string Temporary { get; } = temporary;
	public string Backup { get; } = backup;
	public bool HadOriginal { get; } = hadOriginal;
	public bool OriginalMoved { get; set; }
	public bool Committed { get; set; }
}
