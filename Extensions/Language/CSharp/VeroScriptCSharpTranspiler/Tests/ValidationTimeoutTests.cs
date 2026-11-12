// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using VeroScript.CSharpTranspiler;

internal static class ValidationTimeoutTests
{
	internal static bool TryRunFixture(string[] arguments)
	{
		if (arguments is ["--hold-validation-pipe"])
		{
			Thread.Sleep(TimeSpan.FromSeconds(8));
			return true;
		}
		string? marker = Environment.GetEnvironmentVariable(MarkerVariable);
		if (arguments.Length == 0 || arguments[0] != "--check" || string.IsNullOrEmpty(marker))
		{
			return false;
		}
		if (Environment.GetEnvironmentVariable(ModeVariable) == "running")
		{
			Thread.Sleep(TimeSpan.FromSeconds(8));
			return true;
		}
		using Process child = Process.Start(new ProcessStartInfo(Executable(), "--hold-validation-pipe")
		{
			UseShellExecute = false,
			CreateNoWindow = true
		}) ?? throw new InvalidOperationException("Cannot start the validation pipe fixture.");
		File.WriteAllText(marker, child.Id.ToString(CultureInfo.InvariantCulture) + " "
			+ child.StartTime.Ticks.ToString(CultureInfo.InvariantCulture));
		return true;
	}

	internal static void Run()
	{
		const string source = "public static class Input { public static void Main() { System.Console.WriteLine(42); } }";
		// Warm semantic analysis so the elapsed check measures validation rather
		// than first-use Roslyn initialization on a loaded test machine.
		_ = CSharpTranspiler.TranspileSource(source, options: new() { ValidateGeneratedSource = false });
		string root = Path.Combine(Path.GetTempPath(), "veroscript-validation-" + Guid.NewGuid().ToString("N"));
		Directory.CreateDirectory(root);
		string marker = Path.Combine(root, "child.pid");
		string? previousMarker = Environment.GetEnvironmentVariable(MarkerVariable);
		string? previousMode = Environment.GetEnvironmentVariable(ModeVariable);
		try
		{
			Environment.SetEnvironmentVariable(MarkerVariable, marker);
			foreach (string mode in new[] { "running", "pipes" })
			{
				Environment.SetEnvironmentVariable(ModeVariable, mode);
				var timer = Stopwatch.StartNew();
				CSharpTranspileResult result = CSharpTranspiler.TranspileSource(source, options: new()
				{
					ValidateGeneratedSource = true,
					VeroScriptExecutable = Executable(),
					ValidationTimeout = TimeSpan.FromMilliseconds(500)
				});
				timer.Stop();
				Require(!result.Succeeded && result.Diagnostics.Any(item => item.Code == "VSCT2004")
					&& timer.Elapsed < TimeSpan.FromSeconds(4), "single-source validation timeout: " + mode);
				StopChild(marker);
			}
			string input = Path.Combine(root, "Input.cs");
			File.WriteAllText(input, source);
			var projectTimer = Stopwatch.StartNew();
			CSharpTranspileProjectResult project = CSharpTranspiler.TranspileProject([input], new()
			{
				ProjectRoot = root,
				ValidateGeneratedSources = true,
				VeroScriptExecutable = Executable(),
				ValidationTimeout = TimeSpan.FromMilliseconds(500)
			});
			projectTimer.Stop();
			Require(!project.Succeeded && project.Diagnostics.Any(item => item.Code == "VSCT2004")
				&& projectTimer.Elapsed < TimeSpan.FromSeconds(4), "project validation pipe timeout");
		}
		finally
		{
			Environment.SetEnvironmentVariable(MarkerVariable, previousMarker);
			Environment.SetEnvironmentVariable(ModeVariable, previousMode);
			StopChild(marker);
			Directory.Delete(root, true);
		}
	}

	private static string Executable() => Path.Combine(AppContext.BaseDirectory,
		typeof(ValidationTimeoutTests).Assembly.GetName().Name + (OperatingSystem.IsWindows() ? ".exe" : ""));

	private static void StopChild(string marker)
	{
		if (!File.Exists(marker))
		{
			return;
		}
		string[] identity = File.ReadAllText(marker).Split(' ');
		int identifier = int.Parse(identity[0], CultureInfo.InvariantCulture);
		long started = long.Parse(identity[1], CultureInfo.InvariantCulture);
		try
		{
			using Process child = Process.GetProcessById(identifier);
			// Match the process start as well as its PID before terminating the
			// owned fixture; process names can be truncated on Unix.
			if (!child.HasExited && child.StartTime.Ticks == started)
			{
				child.Kill();
				child.WaitForExit(5000);
			}
		}
		catch (ArgumentException)
		{
			// The fixture can already have completed before cleanup.
		}
		File.Delete(marker);
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException(message);
		}
	}

	private const string MarkerVariable = "VEROSCRIPT_CSHARP_VALIDATOR_TEST_PID";
	private const string ModeVariable = "VEROSCRIPT_CSHARP_VALIDATOR_TEST_MODE";
}
