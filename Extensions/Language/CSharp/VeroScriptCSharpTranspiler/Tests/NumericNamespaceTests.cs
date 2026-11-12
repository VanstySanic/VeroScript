// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.CSharpTranspiler;

internal static class NumericNamespaceTests
{
	internal static void Run(string directory)
	{
		const string arithmetic = "public static class Arithmetic { public static int Add(int left, int right) => left + right; }";
		string arithmeticPath = Path.Combine(directory, "Arithmetic.cs");
		CSharpTranspileResult isolated = CSharpTranspiler.TranspileSource(arithmetic, arithmeticPath, new() { ValidateGeneratedSource = false });
		if (!isolated.Succeeded)
		{
			throw new InvalidOperationException("Could not prepare numeric helper namespace collision regression.");
		}

		string generatedNamespace = isolated.Source.Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries).Last(line => line.StartsWith("namespace ", StringComparison.Ordinal))["namespace ".Length..];
		string collisionPath = Path.Combine(directory, "NumericCollision.cs");
		File.WriteAllText(arithmeticPath, arithmetic);
		File.WriteAllText(collisionPath, "public static class " + generatedNamespace + " { public static int Subtract(int left, int right) => left - right; }");
		var options = new CSharpTranspileProjectOptions
		{
			ProjectRoot = directory,
			ValidateGeneratedSources = false
		};
		CSharpTranspileProjectResult project = CSharpTranspiler.TranspileProject([arithmeticPath, collisionPath], options);
		CSharpTranspileProjectResult reversed = CSharpTranspiler.TranspileProject([collisionPath, arithmeticPath], options);
		if (!project.Succeeded || !reversed.Succeeded)
		{
			throw new InvalidOperationException("Project identifier collision regression failed to transpile.");
		}

		string arithmeticOutput = project.Files.Single(file => file.InputPath == arithmeticPath).Source;
		if (!arithmeticOutput.Contains("namespace " + generatedNamespace + "_" + Environment.NewLine, StringComparison.Ordinal) || project.Files.Any(file => reversed.Files.Single(other => other.InputPath == file.InputPath).Source != file.Source))
		{
			throw new InvalidOperationException("Numeric helper names must account for all project files independently of input order.");
		}

		CSharpTranspileResult fresh = CSharpTranspiler.TranspileSource(arithmetic, arithmeticPath, new() { ValidateGeneratedSource = false });
		if (!fresh.Succeeded || fresh.Source != isolated.Source)
		{
			throw new InvalidOperationException("Numeric identifier caches must not escape their compilation.");
		}
	}
}
