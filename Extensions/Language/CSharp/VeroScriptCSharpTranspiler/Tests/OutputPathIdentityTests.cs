// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.CSharpTools;

internal static class OutputPathIdentityTests
{
	internal static void Run()
	{
		if (OperatingSystem.IsWindows())
		{
			return;
		}

		string root = Path.Combine(Path.GetTempPath(), "veroscript-output-identity-" + Guid.NewGuid().ToString("N"));
		Directory.CreateDirectory(root);
		try
		{
			string source = Path.Combine(root, "caf\u00e9.cs");
			string alternate = Path.Combine(root, "cafe\u0301.cs");
			File.WriteAllText(source, "original");
			if (File.Exists(alternate))
			{
				ExpectRejected([source], [alternate]);
				ExpectRejected([], [source, alternate]);
			}
			else
			{
				// Distinct names on a normalization-sensitive filesystem must remain usable,
				// both before and after the second file is created.
				OutputPathIdentity.Validate([source], [alternate]);
				File.WriteAllText(alternate, "independent");
				OutputPathIdentity.Validate([source], [alternate]);
				OutputPathIdentity.Validate([], [source, alternate]);
				if (File.ReadAllText(source) != "original")
				{
					throw new InvalidOperationException("Unicode path identity checks changed their input.");
				}
			}

			string directory = Path.Combine(root, "r\u00e9sum\u00e9");
			string alternateDirectory = Path.Combine(root, "re\u0301sume\u0301");
			Directory.CreateDirectory(directory);
			string nestedSource = Path.Combine(directory, "input.cs");
			string nestedAlternate = Path.Combine(alternateDirectory, "input.cs");
			File.WriteAllText(nestedSource, "nested original");
			if (Directory.Exists(alternateDirectory))
			{
				ExpectRejected([nestedSource], [nestedAlternate]);
			}
			else
			{
				OutputPathIdentity.Validate([nestedSource], [nestedAlternate]);
			}
		}
		finally
		{
			Directory.Delete(root, recursive: true);
		}
	}

	private static void ExpectRejected(IEnumerable<string> inputs, IEnumerable<string> outputs)
	{
		try
		{
			OutputPathIdentity.Validate(inputs, outputs);
		}
		catch (ArgumentException)
		{
			return;
		}

		throw new InvalidOperationException("Equivalent Unicode paths bypassed output overwrite protection.");
	}
}
