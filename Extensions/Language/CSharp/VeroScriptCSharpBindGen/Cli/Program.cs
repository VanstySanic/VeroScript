// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Reflection;
using VeroScript.CSharpBindGen;
using VeroScript.CSharpFrontend;

string productVersion = typeof(CSharpBindGen).Assembly
	.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion
	.Split('+', 2)[0]
	?? typeof(CSharpBindGen).Assembly.GetName().Version?.ToString(3)
	?? "0.1.1";
if (args is ["--help"] or ["-h"])
{
	PrintUsage(productVersion);
	return 0;
}
if (args is ["--version"])
{
	Console.WriteLine($"VeroScriptCSharpBindGen {productVersion}");
	return 0;
}

var inputs = new List<string>();
var references = new List<string>();
string catalogPath = "";
string apiPath = "";
string apiDirectory = "";
string registrationPath = "";
string aotBridgePath = "";
string manifestPath = "";
string languageVersion = "latest";
bool requireAttribute = false;
try
{
	for (int index = 0; index < args.Length; ++index)
	{
		string argument = args[index];
		switch (argument)
		{
			case "--catalog":
				{
					catalogPath = RequireValue(args, ref index, argument);
					break;
				}

			case "--api-reference":
				{
					apiPath = RequireValue(args, ref index, argument);
					break;
				}

			case "--api-reference-dir":
				{
					apiDirectory = RequireValue(args, ref index, argument);
					break;
				}

			case "--registration":
				{
					registrationPath = RequireValue(args, ref index, argument);
					break;
				}

			case "--aot-bridge":
				{
					aotBridgePath = RequireValue(args, ref index, argument);
					break;
				}

			case "--manifest":
				{
					manifestPath = RequireValue(args, ref index, argument);
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

			case "--require-attribute":
				{
					requireAttribute = true;
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
	if (inputs.Count == 0 || string.IsNullOrWhiteSpace(catalogPath))
	{
		throw new ArgumentException("At least one input and --catalog are required.");
	}

	if (!string.IsNullOrWhiteSpace(apiPath) && !string.IsNullOrWhiteSpace(apiDirectory))
	{
		throw new ArgumentException("--api-reference and --api-reference-dir are mutually exclusive.");
	}

	if (!string.IsNullOrWhiteSpace(apiPath) && !string.IsNullOrWhiteSpace(manifestPath))
	{
		throw new ArgumentException("--api-reference with --manifest is ambiguous; use --api-reference-dir.");
	}

	var parseOptions = new CSharpParseOptions
	{
		LanguageVersion = languageVersion,
		ReferencePaths = references
	};
	var bindOptions = new CSharpBindGenOptions
	{
		RequireBindingAttribute = requireAttribute,
		EmitApiReference = !string.IsNullOrWhiteSpace(apiPath)
			|| !string.IsNullOrWhiteSpace(apiDirectory),
		EmitRegistrationSource = !string.IsNullOrWhiteSpace(registrationPath),
		EmitAotBridgeSource = !string.IsNullOrWhiteSpace(aotBridgePath)
	};

	if (!string.IsNullOrWhiteSpace(apiDirectory) || !string.IsNullOrWhiteSpace(manifestPath))
	{
		CSharpBindGenProjectResult project = CSharpBindGen.GenerateBindingProject(
			inputs, parseOptions, bindOptions, new CSharpBindGenProjectLayout
			{
				CatalogPath = catalogPath,
				RegistrationPath = registrationPath,
				AotBridgePath = aotBridgePath,
				ApiReferenceDirectory = apiDirectory
			});
		foreach (CSharpDiagnostic diagnostic in project.Diagnostics)
		{
			Console.Error.WriteLine(FormatDiagnostic(diagnostic));
		}

		if (!project.Succeeded) return 1;
		var projectOutputs = new List<(string Path, string Contents)>
		{
			(catalogPath, project.CatalogJson)
		};
		if (!string.IsNullOrWhiteSpace(registrationPath))
		{
			projectOutputs.Add((registrationPath, project.RegistrationSource));
		}

		if (!string.IsNullOrWhiteSpace(aotBridgePath))
		{
			projectOutputs.Add((aotBridgePath, project.AotBridgeSource));
		}

		foreach (CSharpBindGenApiReference reference in project.ApiReferences)
		{
			projectOutputs.Add((Path.Combine(apiDirectory, reference.OutputName), reference.Contents));
		}

		if (!string.IsNullOrWhiteSpace(manifestPath))
		{
			projectOutputs.Add((manifestPath, project.ManifestJson));
		}

		ValidateOutputPaths(inputs, projectOutputs.Select(output => output.Path));
		WriteAllAtomically(projectOutputs);
		return 0;
	}

	ValidateOutputPaths(inputs, [catalogPath, apiPath, registrationPath, aotBridgePath]);

	CSharpBindGenResult result = CSharpBindGen.GenerateBindingsFromFiles(inputs,
		parseOptions, bindOptions);
	foreach (CSharpDiagnostic diagnostic in result.Diagnostics)
	{
		Console.Error.WriteLine(FormatDiagnostic(diagnostic));
	}

	if (!result.Succeeded) return 1;
	var outputs = new List<(string Path, string Contents)>
	{
		(catalogPath, result.CatalogJson)
	};
	if (!string.IsNullOrWhiteSpace(apiPath))
	{
		outputs.Add((apiPath, result.ApiReference));
	}

	if (!string.IsNullOrWhiteSpace(registrationPath))
	{
		outputs.Add((registrationPath, result.RegistrationSource));
	}

	if (!string.IsNullOrWhiteSpace(aotBridgePath))
	{
		outputs.Add((aotBridgePath, result.AotBridgeSource));
	}

	WriteAllAtomically(outputs);
	return 0;
}
catch (Exception exception) when (exception is ArgumentException or IOException or UnauthorizedAccessException)
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

static string FormatDiagnostic(CSharpDiagnostic diagnostic)
{
	string location = string.IsNullOrEmpty(diagnostic.Location.File) ? ""
		: $"{diagnostic.Location.File}({diagnostic.Location.Line},{diagnostic.Location.Column}): ";
	return $"{location}{diagnostic.Severity.ToString().ToLowerInvariant()} {diagnostic.Code}: {diagnostic.Message}";
}

static void ValidateOutputPaths(IEnumerable<string> inputs, IEnumerable<string> outputs)
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
			string directory = Path.GetDirectoryName(target)!;
			Directory.CreateDirectory(directory);
			if (Directory.Exists(target))
			{
				throw new IOException($"Existing generated output target is not a file: '{path}'.");
			}

			var item = new StagedOutput(
							target,
							target + $".tmp.{nonce}.{index}",
							target + $".bak.{nonce}.{index}",
							File.Exists(target));
			staged.Add(item);
			File.WriteAllText(item.Temporary, contents, new System.Text.UTF8Encoding(false));
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
	Console.WriteLine($"VeroScriptCSharpBindGen {version}");
	Console.WriteLine("Usage: veroscript-csharp-bindgen <input.cs...> --catalog <output.json> [options]");
	Console.WriteLine("Options:");
	Console.WriteLine("  --registration <output.g.cs>   Emit reflection-free live registration source.");
	Console.WriteLine("  --aot-bridge <output.g.cs>     Emit managed C# AOT host-call implementations.");
	Console.WriteLine("  --api-reference <output.txt>   Emit a human-readable API reference.");
	Console.WriteLine("  --api-reference-dir <dir>     Emit one stable API reference per input file.");
	Console.WriteLine("  --manifest <output.json>      Emit a multi-file project artifact manifest.");
	Console.WriteLine("  --reference <assembly.dll>     Add a metadata reference (repeatable).");
	Console.WriteLine("  --language-version <version>   Select the C# language version.");
	Console.WriteLine("  --require-attribute            Export only [VeroScriptBinding] types.");
}

file sealed class StagedOutput(
	string target,
	string temporary,
	string backup,
	bool hadOriginal)
{
	public string Target { get; } = target;
	public string Temporary { get; } = temporary;
	public string Backup { get; } = backup;
	public bool HadOriginal { get; } = hadOriginal;
	public bool OriginalMoved { get; set; }
	public bool Committed { get; set; }
}
