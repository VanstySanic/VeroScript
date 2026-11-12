// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using VeroScript.CSharpTranspiler;

internal static class NumericParityTests
{
	private const string Source = """
        using System;
        public enum ByteMode : byte { Ready = 255 }
        public enum WideMode : long { Ready = -9223372036854775808L }
        public sealed class NumberBox { public int Value { get; set; } }
        public static class Program
        {
            private static NumberBox box;
            private static int calls = 0;
            public static NumberBox GetBox() { calls++; return box; }
            public static void print(int value) { Console.WriteLine("wrong print target"); }
            public static string to_string(int value) => "wrong string target";
            public static void ChangeReference(ref int value) { value += value; value *= 3; }
            public static void Main()
            {
                box = new NumberBox();
                int i = 2147483647;
                Console.WriteLine(i + 1);
                Console.WriteLine(i * i);
                i += 1;
                Console.WriteLine(i);
                Console.WriteLine(-i);
                Console.WriteLine(i % 3);
                Console.WriteLine(i--);
                Console.WriteLine(i);
                Console.WriteLine(++i);
                long l = 9223372036854775807L;
                Console.WriteLine(l + 1L);
                Console.WriteLine(l * l);
                l += 1L;
                Console.WriteLine(-l);
                Console.WriteLine(l % 3L);
                Console.WriteLine(l--);
                Console.WriteLine(l);
                Console.WriteLine(++l);
                uint u = 4294967295U;
                ulong wide = 18446744073709551615UL;
                Console.WriteLine(u + 1U);
                Console.WriteLine(wide + 1UL);
                Console.WriteLine(u / 3U);
                Console.WriteLine(u % 3U);
                Console.WriteLine(~u);
                Console.WriteLine(wide / 3UL);
                Console.WriteLine(wide % 3UL);
                Console.WriteLine(~wide);
                u += 2U;
                u *= 4294967295U;
                Console.WriteLine(u);
                u /= 3U;
                u %= 3U;
                Console.WriteLine(u);
                u = 4294967295U;
                wide += 2UL;
                wide *= 18446744073709551615UL;
                Console.WriteLine(wide);
                wide /= 3UL;
                wide %= 3UL;
                Console.WriteLine(wide);
                wide = 18446744073709551615UL;
                Console.WriteLine((int)u);
                Console.WriteLine((long)wide);
                Console.WriteLine((uint)i);
                Console.WriteLine((ulong)l);
                Console.WriteLine((byte)i);
                Console.WriteLine((sbyte)u);
                Console.WriteLine((ushort)wide);
                Console.WriteLine((short)wide);
                ByteMode byteMode = (ByteMode)u;
                Console.WriteLine((int)byteMode);
                byteMode = (ByteMode)1;
                Console.WriteLine((int)byteMode);
                WideMode wideMode = WideMode.Ready;
                Console.WriteLine((uint)wideMode);
                byte b = 255;
                Console.WriteLine(b++);
                Console.WriteLine(b);
                Console.WriteLine(--b);
                sbyte s = 127;
                Console.WriteLine(++s);
                short small = 32767;
                Console.WriteLine(++small);
                ushort us = 65535;
                Console.WriteLine(++us);
                byte amount = 1;
                b += amount;
                Console.WriteLine(b);
                int shift = 1;
                Console.WriteLine(shift << -1);
                Console.WriteLine(shift << 32);
                shift <<= 33;
                Console.WriteLine(shift);
                Console.WriteLine(-1 >> 33);
                Console.WriteLine(1L << -1);
                Console.WriteLine(1UL << 65);
                int order = 1;
                Console.WriteLine(order += (order = 5));
                Console.WriteLine(order);
                ChangeReference(ref order);
                Console.WriteLine(order);
                int captured = 1;
                Func<int> readCaptured = () => captured;
                Console.WriteLine(captured + (captured = 2));
                Console.WriteLine(readCaptured());
                Console.WriteLine(captured += (captured = 5));
                Console.WriteLine(readCaptured());
                box.Value = 2147483647;
                Console.WriteLine(GetBox().Value++);
                Console.WriteLine(box.Value);
                Console.WriteLine(calls);
                Console.WriteLine(GetBox().Value += (box.Value = 5));
                Console.WriteLine(box.Value);
                Console.WriteLine(calls);
                int[] items = { 2147483647, 1 };
                int index = 0;
                Console.WriteLine(items[index++] += 1);
                Console.WriteLine(index);
                int negative = -1;
                Console.WriteLine(negative = (int)((long)negative / u));
                uint positive = 3U;
                Console.WriteLine(positive = (uint)((long)positive / -1));
                int narrowed = 1;
                Console.WriteLine(narrowed = (int)(narrowed + 9223372036854775807L));
                Console.WriteLine(narrowed = (int)(narrowed * 9223372036854775807L));
                double whole = 1.0;
                Console.WriteLine(whole - 2.0 == -1.0 ? 1 : 0);
                float constantSingle = 4611686293305294849L;
                Console.WriteLine(constantSingle == 4611686293305294849f ? 1 : 0);
                float rounded = 16777216f;
                rounded += 1f;
                Console.WriteLine(rounded == 16777216f);
                Console.WriteLine(rounded++ == 16777216f);
                Console.WriteLine(rounded == 16777216f);
                Console.WriteLine(++rounded == 16777216f);
                int integerOne = 1;
                Console.WriteLine(rounded + integerOne == 16777216f);
                float opposite = -16777216f;
                int roundedInteger = 16777217;
                Console.WriteLine(opposite + roundedInteger == 0f);
                Console.WriteLine(roundedInteger == rounded);
                Console.WriteLine(roundedInteger > rounded);
                uint unsignedRoundedInteger = 16777217U;
                Console.WriteLine(unsignedRoundedInteger == rounded);
                Console.WriteLine(unsignedRoundedInteger > rounded);
                bool chooseInteger = true;
                Console.WriteLine((chooseInteger ? roundedInteger : rounded) == rounded);
                Console.WriteLine((chooseInteger ? rounded : unsignedRoundedInteger) == unsignedRoundedInteger);
                chooseInteger = false;
                Console.WriteLine((chooseInteger ? rounded : roundedInteger) == rounded);
                long longAlternative = -9223372036854775808L;
                Console.WriteLine(chooseInteger ? unsignedRoundedInteger : longAlternative);
                chooseInteger = true;
                Console.WriteLine(chooseInteger ? unsignedRoundedInteger : longAlternative);
                int branchCalls = 0;
                Console.WriteLine((chooseInteger ? ++branchCalls : (branchCalls += 100)) == 1);
                Console.WriteLine(branchCalls);
                Console.WriteLine();
                bool flag = true;
                Console.WriteLine(flag);
                Console.Write(flag);
                Console.WriteLine(false);
                Console.WriteLine("value=" + flag + ":" + i);
                Console.WriteLine("constant" + ":" + "string");
                string text = "old";
                Console.WriteLine(text += (text = "new"));
                text += flag;
                text += 42;
                Console.WriteLine(text);
                int zero = 0;
                uint unsignedZero = 0U;
                ulong wideZero = 0UL;
                try { Console.WriteLine(u / unsignedZero); }
                catch { Console.WriteLine("unsigned divide by zero caught"); }
                try { Console.WriteLine(wide % wideZero); }
                catch { Console.WriteLine("wide remainder by zero caught"); }
                try { Console.WriteLine(1 / zero); }
                catch { Console.WriteLine("divide by zero caught"); }
                try { Console.WriteLine(i % -1); }
                catch { Console.WriteLine("remainder overflow caught"); }
                try { Console.WriteLine(l / -1L); }
                catch { Console.WriteLine("division overflow caught"); }
            }
        }
        """;

	internal static void Run(string? executable)
	{
		CSharpTranspileResult result = CSharpTranspiler.TranspileSource(Source, "numeric-parity.cs",
			new() { ValidateGeneratedSource = executable is not null,
				VeroScriptExecutable = executable ?? "", GeneratedSourcePath = "numeric-parity.vs" });
		if (!result.Succeeded)
		{
			throw new InvalidOperationException("Numeric lowering failed: "
				+ string.Join(" | ", result.Diagnostics.Select(item => item.Message)));
		}
		foreach (string unsupported in new[]
		{
			"double a = 1.0, b = 0.0; double value = a / b;",
			"double a = 1.0, b = 2.0; double value = a % b;",
			"double a = 1.0; a /= 0.0;",
			"double a = 1.0; int value = (int)a;",
			"double a = 1.0; float value = (float)a;",
			"long a = 4611686293305294849L; float value = a;",
			"long a = 4611686293305294849L; float value = (float)a;",
			"long a = 4611686293305294849L; bool pick = true; float b = 0f; float value = pick ? a : b;",
			"long a = 4611686293305294849L; float value = 0f; value += a;",
			"ulong a = 9223372586610589697UL; float value = 0f; value *= a;",
			"System.Console.WriteLine(1.25);",
			"System.Console.WriteLine(\"{0}\", 42);",
			"string value = \"prefix\" + 1.25;",
			"string value = \"prefix\"; value += 1.25;",
			"int a = 1; int value = checked(a + 1);",
			"string value = checked(\"constant\" + \"text\");",
			"string value = unchecked(\"constant\" + \"text\");",
			"int a = 1; unchecked { a += 1; }"
		})
		{
			CSharpTranspileResult rejected = CSharpTranspiler.TranspileSource(
				"public static class Program { public static void Main() { " + unsupported + " } }",
				options: new() { ValidateGeneratedSource = false });
			if (rejected.Succeeded || !rejected.Diagnostics.Any(item => item.Code == "VSCT2001"))
			{
				throw new InvalidOperationException("Numeric semantic boundary was silently lowered: " + unsupported);
			}
		}
		CSharpTranspileResult unsupportedEnum = CSharpTranspiler.TranspileSource(
			"public enum Mode : ulong { Ready = 18446744073709551615UL }",
			options: new() { ValidateGeneratedSource = false });
		if (unsupportedEnum.Succeeded || !unsupportedEnum.Diagnostics.Any(item => item.Code == "VSCT2001"))
		{
			throw new InvalidOperationException("Unsigned 64-bit enum must fail before native validation.");
		}
		if (executable is null) return;

		string expected = RunCSharp();
		string directory = Path.Combine(Path.GetTempPath(), "veroscript-csharp-numeric-" + Guid.NewGuid().ToString("N"));
		Directory.CreateDirectory(directory);
		try
		{
			string path = Path.Combine(directory, "numeric-parity.vs");
			File.WriteAllText(path, result.Source);
			var start = new ProcessStartInfo(Path.GetFullPath(executable))
			{
				UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true
			};
			start.ArgumentList.Add(path);
			using Process process = Process.Start(start) ?? throw new InvalidOperationException("Failed to start VeroScript.");
			Task<string> output = process.StandardOutput.ReadToEndAsync();
			Task<string> error = process.StandardError.ReadToEndAsync();
			if (!process.WaitForExit(30_000))
			{
				process.Kill(entireProcessTree: true);
				process.WaitForExit();
				throw new InvalidOperationException("Numeric parity execution timed out.");
			}
			string actual = output.GetAwaiter().GetResult().Replace("\r\n", "\n", StringComparison.Ordinal);
			string diagnostic = error.GetAwaiter().GetResult();
			if (process.ExitCode != 0 || !string.Equals(actual, expected, StringComparison.Ordinal))
			{
				throw new InvalidOperationException($"C# and VeroScript numeric results differ (exit {process.ExitCode}).\n"
					+ diagnostic + "\nExpected:\n" + expected + "\nActual:\n" + actual);
			}
		}
		finally
		{
			Directory.Delete(directory, recursive: true);
		}
	}

	private static string RunCSharp()
	{
		string[] assemblies = ((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")
			?? throw new InvalidOperationException("CLR references are unavailable.")).Split(Path.PathSeparator);
		CSharpCompilation compilation = CSharpCompilation.Create("NumericParityReference",
			[CSharpSyntaxTree.ParseText(Source)], assemblies.Select(path => MetadataReference.CreateFromFile(path)),
			new CSharpCompilationOptions(OutputKind.ConsoleApplication));
		using var stream = new MemoryStream();
		Microsoft.CodeAnalysis.Emit.EmitResult emitted = compilation.Emit(stream);
		if (!emitted.Success)
		{
			throw new InvalidOperationException(string.Join(" | ", emitted.Diagnostics));
		}
		stream.Position = 0;
		var context = new AssemblyLoadContext("NumericParityReference", isCollectible: true);
		TextWriter previous = Console.Out;
		using var output = new StringWriter(CultureInfo.InvariantCulture);
		try
		{
			Console.SetOut(output);
			context.LoadFromStream(stream).EntryPoint!.Invoke(null, null);
			return output.ToString().Replace("\r\n", "\n", StringComparison.Ordinal);
		}
		finally
		{
			Console.SetOut(previous);
			context.Unload();
		}
	}
}
