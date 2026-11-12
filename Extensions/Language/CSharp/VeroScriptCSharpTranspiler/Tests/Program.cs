// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text.Json;
using VeroScript.CSharpFrontend;
using VeroScript.CSharpTranspiler;

if (ValidationTimeoutTests.TryRunFixture(args))
{
	return;
}
string publicApiBaseline = Path.Combine(AppContext.BaseDirectory, "PublicApi.txt");
if (args is ["--write-public-api", string apiOutputPath])
{
	File.WriteAllLines(apiOutputPath,
		PublicApiContract.Generate(typeof(CSharpTranspiler).Assembly));
	return;
}
PublicApiContract.Verify(typeof(CSharpTranspiler).Assembly, publicApiBaseline,
	"VeroScriptCSharpTranspiler");
OutputPathIdentityTests.Run();
NumericParityTests.Run(args.Length == 0 ? null : args[0]);
ValidationTimeoutTests.Run();

static void Expect(bool condition, string message)
{
	if (!condition)
	{
		throw new InvalidOperationException(message);
	}
}

const string source = """
    using System;
    using Count = System.Int32;

    public sealed class Counter
    {
        public int Value { get; set; } = 0;

        public Counter(int initial)
        {
            Value = initial;
        }

        public int Increment(int amount)
        {
            Value += amount;
            return Value;
        }
    }

    public static class Program
    {
        private static int state = 1;

        public static ref int State() => ref state;

        public static ref int StateBlock()
        {
            return ref state;
        }

        public static int Add(int left, int right) => left + right;

        public static int Sum(params int[] values)
        {
            int total = 0;
            foreach (int value in values) total += value;
            return total;
        }

        public static int WithFinally()
        {
            try { return 4; }
            finally { Console.Write("done"); }
        }

        public static int Main()
        {
            Console.WriteLine("你好，世界");
            Helpers.Show();
            var counter = new Counter(2);
            const Count offset = 1;
            Func<int, int> transform = value => value + offset;
            Func<int, int> block = value => { return transform(value); };
            ref int alias = ref State();
            ref int blockAlias = ref StateBlock();
            alias = 2;
            int jumped = 0;
            int first = 0, second = 2;
            do { first += 1; } while (first < 2);
            goto ready;
            ready:
            jumped = 1;
            for (int index = 0; index < 1; ++index)
                counter.Increment(3);
            for (int left = 0, right = 3; left < right; left++, right--)
            {
                second <<= 1;
                second ^= left;
            }
            return Add(counter.Value, 4) == 9 && Helpers.Wide() == 5000000000L
                && block(1) == 2 && Sum(1, 2) == 3 && WithFinally() == 4
                && alias == 2 && blockAlias == 2 && jumped == 1
                && first == 2 && second == 9 ? 0 : 1;
        }
    }

    public static class Helpers
    {
        public static long Wide() => 5000000000L;
        public static void Show() => Console.WriteLine(string.Empty);
    }
    """;

var options = new CSharpTranspileOptions
{
	ValidateGeneratedSource = args.Length != 0,
	VeroScriptExecutable = args.Length == 0 ? "" : args[0],
	GeneratedSourcePath = "sample.vs"
};
CSharpTranspileResult result = CSharpTranspiler.TranspileSource(source, "sample.cs", options);
Expect(result.Succeeded, string.Join(Environment.NewLine,
	result.Diagnostics.Select(item => $"{item.Location.Line}:{item.Location.Column} {item.Code}: {item.Message}")));
Expect(result.Source.Contains("class Counter", StringComparison.Ordinal), "class emitted");
Expect(result.Source.Contains("int main()", StringComparison.Ordinal), "Main maps to main");
Expect(result.Source.Contains("for (int index = 0;", StringComparison.Ordinal), "for emitted");
Expect(result.Source.Contains("do", StringComparison.Ordinal)
	&& result.Source.Contains("while ((first < 2));", StringComparison.Ordinal), "do-while emitted");
Expect(result.Source.Contains("for (int left = 0, right = 3;", StringComparison.Ordinal)
	&& result.Source.Contains("post_inc_i32(ref left)", StringComparison.Ordinal)
	&& result.Source.Contains("post_dec_i32(ref right)", StringComparison.Ordinal),
	"multi-declarator and multi-increment for emitted");
Expect(result.Source.Contains("assign_shl_i32_i32(ref second", StringComparison.Ordinal)
	&& result.Source.Contains("assign_xor_i32_i32(ref second", StringComparison.Ordinal),
	"compound shift and bitwise assignments emitted");
Expect(result.Source.Contains("for (int value : values)", StringComparison.Ordinal), "foreach emitted");
Expect(result.Source.Contains("print(\"你好，世界\")", StringComparison.Ordinal), "Console.WriteLine emitted");
Expect(result.Source.Contains("std::console::write(\"done\")", StringComparison.Ordinal), "Console.Write emitted");
Expect(result.Source.Contains("finally", StringComparison.Ordinal), "finally emitted");
Expect(result.Source.Contains("[&](int value) -> int", StringComparison.Ordinal)
	&& result.Source.Contains("fn(int)->int block", StringComparison.Ordinal), "lambdas emitted");
Expect(result.Source.Contains("int... values", StringComparison.Ordinal), "params emitted");
Expect(result.Source.Contains("using Count = int;", StringComparison.Ordinal), "type alias emitted");
Expect(result.Source.Contains("const int offset", StringComparison.Ordinal), "const local emitted");
Expect(result.Source.Contains("int& State()", StringComparison.Ordinal)
	&& result.Source.Contains("int& StateBlock()", StringComparison.Ordinal)
	&& result.Source.Contains("int& alias = State();", StringComparison.Ordinal),
	"reference return and local alias emitted");
Expect(result.Source.Contains("goto ready;", StringComparison.Ordinal)
	&& result.Source.Contains("ready:", StringComparison.Ordinal), "labels and goto emitted");
Expect(result.Source.Contains("namespace Helpers", StringComparison.Ordinal)
	&& result.Source.Contains("Helpers::Wide()", StringComparison.Ordinal)
	&& result.Source.Contains("5000000000L", StringComparison.Ordinal),
	"static container and typed literal semantics");
using (JsonDocument map = JsonDocument.Parse(result.SourceMapJson))
{
	Expect(map.RootElement.GetProperty("generated").GetString() == "sample.vs", "source map");
}

CSharpTranspileResult unsupported = CSharpTranspiler.TranspileSource(
	"public static class Program { public static async System.Threading.Tasks.Task Main() { await System.Threading.Tasks.Task.Delay(1); } }",
	"async.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!unsupported.Succeeded && unsupported.Diagnostics.Any(item => item.Code == "VSCT2001"),
	"async must fail closed");
foreach (string initializerSource in new[]
{
	"public class Holder { public static int value = Holder.Compute(); "
		+ "public static int Compute() { System.Console.WriteLine(1); return 7; } } "
		+ "public static class Program { public static void Main() {} }",
	"public static class Program { public static int value = Compute(); "
		+ "public static int Compute() { System.Console.WriteLine(1); return 7; } public static void Main() {} }",
	"public class Holder { public static int Value { get; set; } = Holder.Compute(); "
		+ "public static int Compute() { System.Console.WriteLine(1); return 7; } } "
		+ "public static class Program { public static void Main() {} }",
	"public class Holder { public static int first = 7; public static int second = Holder.first; } "
		+ "public static class Program { public static void Main() {} }",
	"public class Holder { static Holder() { System.Console.WriteLine(1); } "
		+ "public static int Read() { return 7; } } "
		+ "public static class Program { public static void Main() {} }"
})
{
	foreach (bool validate in new[] { false, true })
	{
		CSharpTranspileResult initializerResult = CSharpTranspiler.TranspileSource(initializerSource,
			"static-initializer.cs", new() { ValidateGeneratedSource = validate });
		Expect(!initializerResult.Succeeded && initializerResult.Diagnostics.Any(item => item.Code == "VSCT2001"),
			"static initialization must preserve the CLR type-initialization boundary");
	}
}
CSharpTranspileResult constantInitializers = CSharpTranspiler.TranspileSource(
	"public class Holder { public static int value = 3 + 4; public static int Value { get; set; } = 7; } "
		+ "public static class Program { public static void Main() {} }",
	"constant-initializers.cs", new() { ValidateGeneratedSource = false });
Expect(constantInitializers.Succeeded, "constant static field and property initializers remain supported");
foreach (string stringSource in new[]
{
	"public class Holder { public string Text; }",
	"public class Holder { public string Text { get; set; } }",
	"public static class Program { public static string text; public static void Main() {} }",
	"public class Holder { public string Text = null; }",
	"public class Holder { public string Text { get; set; } = null; }",
	"public static class Program { public static void Main() { string text = null; } }",
	"public class Holder { public int[] Values; }",
	"public class Holder { public int[] Values { get; set; } }",
	"public static class Program { public static int[] values; public static void Main() {} }",
	"public class Holder { public int[] Values = null; }",
	"public static class Program { public static void Main() { int[] values = null; } }"
})
{
	CSharpTranspileResult stringDefault = CSharpTranspiler.TranspileSource(stringSource,
		"string-default.cs", new() { ValidateGeneratedSource = false });
	Expect(!stringDefault.Succeeded && stringDefault.Diagnostics.Any(item => item.Code == "VSCT2001"),
		"CLR null string and array values must fail closed instead of becoming empty values");
}
CSharpTranspileResult nonnullStrings = CSharpTranspiler.TranspileSource(
	"public class Holder { public string Text = \"\"; public string Name { get; set; } = \"\"; "
		+ "public int[] Values = new int[] { 1 }; public int[] Items { get; set; } = new int[] { 2 }; } "
		+ "public static class Program { public static void Main() {} }",
	"nonnull-strings.cs", new() { ValidateGeneratedSource = false });
Expect(nonnullStrings.Succeeded, "explicit nonnull string and array initializers remain supported");
CSharpTranspileResult nullComparisons = CSharpTranspiler.TranspileSource(
	"public static class Program { public static void Main() { int[] values = new int[] { 1 }; "
		+ "System.Console.WriteLine(values == null); System.Console.WriteLine(((null)) != values); } }",
	"null-comparisons.cs", new() { ValidateGeneratedSource = args.Length != 0,
		VeroScriptExecutable = args.Length == 0 ? "" : args[0] });
Expect(nullComparisons.Succeeded, "nonnull array comparisons against null remain supported: "
	+ string.Join(" | ", nullComparisons.Diagnostics.Select(item => item.Message)));
foreach (string containerType in new[] { "List<int>", "Dictionary<int, int>",
	"SortedDictionary<int, int>", "HashSet<int>", "Queue<int>", "Stack<int>" })
{
	string imports = "using System.Collections.Generic; using Items = System.Collections.Generic."
		+ containerType + "; ";
	foreach (string declaration in new[]
	{
		"public class Holder { public Items Values; }",
		"public class Holder { public Items Values { get; set; } }",
		"public class Holder { public static Items Values; }",
		"public class Holder { public static Items Values { get; set; } }",
		"public static class Program { public static Items values; public static void Main() {} }",
		"public class Holder { public Items Values = null; }",
		"public class Holder { public Items Values { get; set; } = null; }",
		"public static class Program { public static void Main() { Items values = null; } }"
	})
	{
		foreach (bool validate in new[] { false, true })
		{
			CSharpTranspileResult nullContainer = CSharpTranspiler.TranspileSource(imports + declaration,
				"null-container.cs", new() { ValidateGeneratedSource = validate });
			Expect(!nullContainer.Succeeded && nullContainer.Diagnostics.Any(item => item.Code == "VSCT2001"),
				"CLR null collection values must fail closed even through aliases: " + containerType);
		}
	}
	CSharpTranspileResult nonnullContainer = CSharpTranspiler.TranspileSource(imports
		+ "public class Holder { public Items Values = new Items(); public Items Items { get; set; } = new Items(); } "
		+ "public static class Program { public static void Main() { Items values = new Items(); "
		+ "System.Console.WriteLine(values == null); System.Console.WriteLine(((null)) != values); } }",
		"nonnull-container.cs", new() { ValidateGeneratedSource = args.Length != 0,
			VeroScriptExecutable = args.Length == 0 ? "" : args[0] });
	Expect(nonnullContainer.Succeeded, "nonnull collection initialization and null comparisons remain supported: "
		+ containerType + " | " + string.Join(" | ", nonnullContainer.Diagnostics.Select(item => item.Message)));
}
CSharpTranspileResult unsupportedChar = CSharpTranspiler.TranspileSource(
	"public static class Program { public static int Main() { char value = '世'; return value; } }",
	"char.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!unsupportedChar.Succeeded && unsupportedChar.Diagnostics.Any(item => item.Code == "VSCT2001"),
	"System.Char width mismatch must fail closed");
CSharpTranspileResult supportedControls = CSharpTranspiler.TranspileSource(
	"public static class Program { public static void Main() { System.Console.WriteLine(\"\\0\\a\\b\\f\\v\\u001F\"); } }",
	"controls.cs", new()
	{
		ValidateGeneratedSource = args.Length != 0,
		VeroScriptExecutable = args.Length == 0 ? "" : args[0]
	});
Expect(supportedControls.Succeeded
	&& supportedControls.Source.Contains("\"\\0\\a\\b\\f\\v\\x1F\"", StringComparison.Ordinal),
	"C# string controls supported by VeroScript must be escaped losslessly: "
	+ string.Join(" | ", supportedControls.Diagnostics.Select(item => item.Message)));
CSharpTranspileResult unsupportedSurrogate = CSharpTranspiler.TranspileSource(
	"public static class Program { public static void Main() { string value = \"\\uD800\"; } }",
	"surrogate.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!unsupportedSurrogate.Succeeded
	&& unsupportedSurrogate.Diagnostics.Any(item => item.Code == "VSCT2001"),
	"unpaired UTF-16 surrogates must fail closed instead of changing value during UTF-8 emission");
CSharpTranspileResult unsupportedUnsignedShiftAssignment = CSharpTranspiler.TranspileSource(
	"public static class Program { public static int Main() { int value = -1; value >>>= 1; return value; } }",
	"unsigned-shift.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!unsupportedUnsignedShiftAssignment.Succeeded
	&& unsupportedUnsignedShiftAssignment.Diagnostics.Any(item => item.Code == "VSCT2001"),
	"C# unsigned shift assignment must fail closed without relying on output validation");
CSharpTranspileResult unsupportedNullForgiving = CSharpTranspiler.TranspileSource(
	"public static class Program { public static int Main() { string value = null!; return value == null ? 0 : 1; } }",
	"null-forgiving.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!unsupportedNullForgiving.Succeeded
	&& unsupportedNullForgiving.Diagnostics.Any(item => item.Code == "VSCT2001"),
	"null-forgiving postfix syntax must fail closed without relying on output validation");
CSharpTranspileResult unsupportedInterface = CSharpTranspiler.TranspileSource(
	"public interface IValue {} public sealed class Value : IValue {}",
	"interface.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!unsupportedInterface.Succeeded
	&& unsupportedInterface.Diagnostics.Any(item => item.Code == "VSCT2001"),
	"implemented interfaces must fail closed");
CSharpTranspileResult unsupportedRefStruct = CSharpTranspiler.TranspileSource(
	"public ref struct Value { public int Data; }",
	"ref-struct.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!unsupportedRefStruct.Succeeded
	&& unsupportedRefStruct.Diagnostics.Any(item => item.Code == "VSCT2001"),
	"ref structs must fail closed");

const string qualifiedSource = """
    namespace Models
    {
        public enum Mode { Ready = 1 }
        public sealed class User
        {
            public static int Read(Mode mode) => mode == Mode.Ready ? 1 : 0;
        }
    }
    namespace Application
    {
        public static class Program
        {
            public static int Main()
            {
                return Models.User.Read(Models.Mode.Ready) == 1 ? 0 : 1;
            }
        }
    }
    """;
CSharpTranspileResult qualified = CSharpTranspiler.TranspileSource(qualifiedSource,
	"qualified.cs", new()
	{
		ValidateGeneratedSource = args.Length != 0,
		VeroScriptExecutable = args.Length == 0 ? "" : args[0],
		GeneratedSourcePath = "qualified.vs"
	});
Expect(qualified.Succeeded, string.Join(Environment.NewLine,
	qualified.Diagnostics.Select(item => $"{item.Code}: {item.Message}")));
Expect(qualified.Source.Contains("Models::Mode::Ready", StringComparison.Ordinal)
	&& qualified.Source.Contains("Models::Mode mode", StringComparison.Ordinal),
	"qualified types and static or enum member access emitted");

CSharpTranspileResult explicitGenericMember = CSharpTranspiler.TranspileSource("""
    public static class Helpers
    {
        public static int Pick<T>(T value) => 1;
        public static int Pick(int value) => 2;
    }
    public static class Program
    {
        public static int Main() => Helpers.Pick<long>(1);
    }
    """, "generic-member.cs", new()
{
	ValidateGeneratedSource = args.Length != 0,
	VeroScriptExecutable = args.Length == 0 ? "" : args[0],
	GeneratedSourcePath = "generic-member.vs"
});
Expect(explicitGenericMember.Succeeded
	&& explicitGenericMember.Source.Contains("Helpers::Pick<int64>(1)", StringComparison.Ordinal),
	"explicit generic member invocation preserves type arguments");

CSharpTranspileResult nestedType = CSharpTranspiler.TranspileSource("""
    namespace N
    {
        public class Outer
        {
            public enum Kind { Ready }
            public static Kind Read() => Kind.Ready;
        }
        public static class Program
        {
            public static int Main() => Outer.Read() == Outer.Kind.Ready ? 0 : 1;
        }
    }
    """, "nested-type.cs", new()
{
	ValidateGeneratedSource = args.Length != 0,
	VeroScriptExecutable = args.Length == 0 ? "" : args[0],
	GeneratedSourcePath = "nested-type.vs"
});
Expect(!nestedType.Succeeded
	&& nestedType.Diagnostics.Any(item => item.Code == "VSCT2001"
		&& item.Message.Contains("nested type declaration", StringComparison.Ordinal)),
	"nested type declarations fail closed until VeroScript supports them");

CSharpTranspileResult recursiveEntry = CSharpTranspiler.TranspileSource("""
    public static class Helpers
    {
        public static int Main() => MainCore();
        public static int MainCore() => Main();
    }
    """, "recursive-entry.cs", new() { ValidateGeneratedSource = false });
Expect(recursiveEntry.Succeeded
	&& recursiveEntry.Source.Contains("int main()", StringComparison.Ordinal)
	&& recursiveEntry.Source.Contains("return main();", StringComparison.Ordinal)
	&& !recursiveEntry.Source.Contains("Main();", StringComparison.Ordinal),
	"compiler-selected entry declaration and references must be renamed together");

CSharpTranspileResult programLibrary = CSharpTranspiler.TranspileSource("""
    public static class Program
    {
        public static int Read() => 7;
    }
    public static class Consumer
    {
        public static int Use() => Program.Read();
    }
    """, "program-library.cs", new() { ValidateGeneratedSource = false });
Expect(programLibrary.Succeeded
	&& programLibrary.Source.Contains("namespace Program", StringComparison.Ordinal)
	&& programLibrary.Source.Contains("Program::Read()", StringComparison.Ordinal)
	&& !programLibrary.Source.Contains("int main()", StringComparison.Ordinal),
	"a Program static class without a C# entry point must retain its namespace identity");

CSharpTranspileResult multipleEntries = CSharpTranspiler.TranspileSource("""
    public static class First { public static int Main() => 0; }
    public static class Second { public static int Main() => 0; }
    """, "multiple-entry.cs", new() { ValidateGeneratedSource = false });
Expect(!multipleEntries.Succeeded
	&& multipleEntries.Diagnostics.Any(item => item.Code == "VSCT2001"
		&& item.Message.Contains("more than one", StringComparison.Ordinal)),
	"multiple applicable C# entry points must fail without relying on output validation");

CSharpTranspileResult argumentEntry = CSharpTranspiler.TranspileSource(
	"public static class Program { public static int Main(string[] args) => args.Length; }",
	"argument-entry.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!argumentEntry.Succeeded
	&& argumentEntry.Diagnostics.Any(item => item.Code == "VSCT2001"
		&& item.Message.Contains("parameterless", StringComparison.Ordinal)),
	"unsupported C# entry signatures must fail before VeroScript validation");

CSharpTranspileResult instanceContainerEntry = CSharpTranspiler.TranspileSource(
	"public sealed class Program { public static void Main() { } }",
	"instance-container-entry.cs", new()
	{
		ValidateGeneratedSource = false
	});
Expect(!instanceContainerEntry.Succeeded
	&& instanceContainerEntry.Diagnostics.Any(item => item.Code == "VSCT2001"
		&& item.Message.Contains("static type", StringComparison.Ordinal)),
	"entry points in instance-capable types must fail with an explicit diagnostic");

string root = Path.Combine(Path.GetTempPath(), "veroscript-csharp-transpiler-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(root);
try
{
	NumericNamespaceTests.Run(root);
	string modelPath = Path.Combine(root, "Models.cs");
	string servicePath = Path.Combine(root, "Service.cs");
	File.WriteAllText(modelPath, "namespace Sample; public sealed class User { public string Name { get; set; } = \"\"; }");
	File.WriteAllText(servicePath, "namespace Sample; public static class Service { public static string Display(User user) => user.Name; }");
	CSharpTranspileProjectResult project = CSharpTranspiler.TranspileProject(
		[modelPath, servicePath], new()
		{
			ProjectRoot = root,
			ValidateGeneratedSources = args.Length != 0,
			VeroScriptExecutable = args.Length == 0 ? "" : args[0]
		});
	Expect(project.Succeeded, string.Join(Environment.NewLine,
		project.Diagnostics.Select(item => $"{item.Code}: {item.Message}")));
	Expect(project.Files.Count == 2, "two project outputs");
	CSharpTranspiledFile service = project.Files.Single(item => item.InputPath == servicePath);
	Expect(service.Source.Contains("import \"Models.vs\";", StringComparison.Ordinal),
		"semantic cross-file import");
	Expect(project.ManifestJson.Contains("veroscript.csharp-transpiled-project", StringComparison.Ordinal),
		"project manifest");

	string collisionRoot = Path.Combine(root, "collisions");
	Directory.CreateDirectory(collisionRoot);
	string firstPath = Path.Combine(collisionRoot, "Item.first");
	string secondPath = Path.Combine(collisionRoot, "Item.second");
	File.WriteAllText(firstPath,
		"public static class First { public static int Read() => Second.Read(); }");
	File.WriteAllText(secondPath,
		"public static class Second { public static int Read() => 42; }");
	CSharpTranspileProjectResult initialNames = CSharpTranspiler.TranspileProject(
		[firstPath, secondPath], new() { ProjectRoot = collisionRoot, ValidateGeneratedSources = false });
	Expect(initialNames.Succeeded, "prepare colliding project output names");
	string reservedName = initialNames.Files.Single(file => file.InputPath == secondPath).OutputName;
	string reservedPath = Path.Combine(collisionRoot, Path.ChangeExtension(reservedName, ".cs"));
	File.WriteAllText(reservedPath, "public static class Reserved { public static int Read() => 7; }");
	CSharpTranspileProjectResult colliding = CSharpTranspiler.TranspileProject(
		[firstPath, secondPath, reservedPath], new()
		{
			ProjectRoot = collisionRoot,
			ValidateGeneratedSources = args.Length != 0,
			VeroScriptExecutable = args.Length == 0 ? "" : args[0]
		});
	Expect(colliding.Succeeded && colliding.Files.Count == 3
		&& colliding.Files.Select(file => file.OutputName).Distinct(StringComparer.OrdinalIgnoreCase).Count() == 3,
		"fallback output names must stay unique when an input already uses a hashed stem: "
		+ string.Join(" | ", colliding.Diagnostics.Select(item => item.Message)));
	CSharpTranspiledFile firstFile = colliding.Files.Single(file => file.InputPath == firstPath);
	CSharpTranspiledFile secondFile = colliding.Files.Single(file => file.InputPath == secondPath);
	Expect(firstFile.Source.Contains("import \"" + secondFile.OutputName + "\";", StringComparison.Ordinal),
		"semantic imports must use the final unique output names");
	CSharpTranspileProjectResult reversed = CSharpTranspiler.TranspileProject(
		[reservedPath, secondPath, firstPath], new() { ProjectRoot = collisionRoot, ValidateGeneratedSources = false });
	Expect(reversed.Succeeded && colliding.Files.All(file => reversed.Files.Single(
		other => other.InputPath == file.InputPath).OutputName == file.OutputName),
		"collision resolution must be stable when project inputs are reordered");

	string caseRoot = Path.Combine(root, "case-collisions");
	Directory.CreateDirectory(caseRoot);
	string upperPath = Path.Combine(caseRoot, "Record.first");
	string lowerPath = Path.Combine(caseRoot, "record.second");
	File.WriteAllText(upperPath,
		"public static class Upper { public static int Read() => Lower.Read(); }");
	File.WriteAllText(lowerPath,
		"public static class Lower { public static int Read() => 42; }");
	CSharpTranspileProjectResult caseNames = CSharpTranspiler.TranspileProject(
		[upperPath, lowerPath], new()
		{
			ProjectRoot = caseRoot,
			ValidateGeneratedSources = args.Length != 0,
			VeroScriptExecutable = args.Length == 0 ? "" : args[0]
		});
	Expect(caseNames.Succeeded && caseNames.Files.Count == 2
		&& caseNames.Files.Select(file => file.OutputName).Distinct(StringComparer.OrdinalIgnoreCase).Count() == 2,
		"different input extensions must not produce colliding outputs on case-insensitive filesystems: "
		+ string.Join(" | ", caseNames.Diagnostics.Select(item => item.Message)));

	string unicodeRoot = Path.Combine(root, "unicode-collisions");
	Directory.CreateDirectory(unicodeRoot);
	string composedPath = Path.Combine(unicodeRoot, "Caf\u00e9.first");
	string decomposedPath = Path.Combine(unicodeRoot, "Cafe\u0301.second");
	File.WriteAllText(composedPath,
		"public static class Composed { public static int Read() => Decomposed.Read(); }");
	File.WriteAllText(decomposedPath,
		"public static class Decomposed { public static int Read() => 42; }");
	CSharpTranspileProjectResult unicodeNames = CSharpTranspiler.TranspileProject(
		[composedPath, decomposedPath], new()
		{
			ProjectRoot = unicodeRoot,
			ValidateGeneratedSources = args.Length != 0,
			VeroScriptExecutable = args.Length == 0 ? "" : args[0]
		});
	Expect(unicodeNames.Succeeded && unicodeNames.Files.Count == 2
		&& unicodeNames.Files.Select(file => file.OutputName.Normalize()).Distinct(StringComparer.OrdinalIgnoreCase).Count() == 2,
		"Unicode-equivalent stems must not produce colliding outputs on normalization-insensitive filesystems: "
		+ string.Join(" | ", unicodeNames.Diagnostics.Select(item => item.Message)));
	CSharpTranspiledFile composedFile = unicodeNames.Files.Single(file => file.InputPath == composedPath);
	CSharpTranspiledFile decomposedFile = unicodeNames.Files.Single(file => file.InputPath == decomposedPath);
	Expect(composedFile.Source.Contains("import \"" + decomposedFile.OutputName + "\";", StringComparison.Ordinal),
		"semantic imports must use the final Unicode-safe output names");
	CSharpTranspileProjectResult reversedUnicode = CSharpTranspiler.TranspileProject(
		[decomposedPath, composedPath], new() { ProjectRoot = unicodeRoot, ValidateGeneratedSources = false });
	Expect(reversedUnicode.Succeeded && unicodeNames.Files.All(file => reversedUnicode.Files.Single(
		other => other.InputPath == file.InputPath).OutputName == file.OutputName),
		"Unicode collision resolution must be stable when project inputs are reordered");
}
finally
{
	Directory.Delete(root, true);
}

Console.WriteLine("VeroScriptCSharpTranspiler tests passed.");
