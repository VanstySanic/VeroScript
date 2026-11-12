// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using VeroScript.CSharpBindGen;
using VeroScript.CSharpFrontend;

const string source = """
namespace Demo;

[System.AttributeUsage(System.AttributeTargets.Method)]
internal sealed class VeroScriptIgnoreAttribute : System.Attribute {}

public enum Mode : byte { Off, On = 3 }

/// <summary>A counter exposed to script.</summary>
public sealed class Counter
{
    public const int Maximum = 10;
    public int Value { get; set; }
    public int ReadOnlyOutside { get; private set; }
    public int InitialOnly { get; init; }
    public Counter(int initial) { Value = initial; }
    public int Add(in int amount) => Value + amount;
    public void Bump(ref int value) => value += Value;
    public void Assign(out int value) => value = Value;
    public static string Label(int value) => value.ToString();
    [VeroScriptIgnore] public int Hidden() => 42;
}
""";

string publicApiBaseline = Path.Combine(AppContext.BaseDirectory, "PublicApi.txt");
if (args is ["--write-public-api", string outputPath])
{
	File.WriteAllLines(outputPath,
		PublicApiContract.Generate(typeof(CSharpBindGen).Assembly));
	return;
}
PublicApiContract.Verify(typeof(CSharpBindGen).Assembly, publicApiBaseline,
	"VeroScriptCSharpBindGen");

CSharpBindGenResult result = CSharpBindGen.GenerateBindingsFromSource(source, "Bindings.cs",
	bindGenOptions: new CSharpBindGenOptions { EmitAotBridgeSource = true });
Expect(result.Succeeded, string.Join(Environment.NewLine, result.Diagnostics.Select(x => x.Message)));
if (args.Length == 2 && args[0] == "--write-catalog")
{
	File.WriteAllText(args[1], result.CatalogJson);
}

using JsonDocument document = JsonDocument.Parse(result.CatalogJson);
JsonElement root = document.RootElement;
Expect(root.GetProperty("version").GetInt32() == 1, "catalog version mismatch");
JsonElement types = root.GetProperty("types");
Expect(types.GetArrayLength() == 2, "public enum and class must be exported");
JsonElement counter = types.EnumerateArray().Single(x => x.GetProperty("name").GetString() == "Demo::Counter");
Expect(counter.GetProperty("kind").GetString() == "class", "class kind mismatch");
JsonElement add = counter.GetProperty("members").EnumerateArray().Single(x => x.GetProperty("name").GetString() == "Add");
Expect(add.GetProperty("returnType").GetString() == "int"
	&& add.GetProperty("paramTypes")[0].GetString() == "int",
	"C# primitive value types must not be encoded as host objects");
Expect(add.GetProperty("paramFlags")[0].GetInt32() == 3, "in parameter flags must preserve readonly ref");
Expect(!counter.GetProperty("members").EnumerateArray().Any(x =>
		x.GetProperty("kind").GetString() == "propertySetter"
		&& x.GetProperty("name").GetString() == "ReadOnlyOutside"),
	"non-public setter must not be exported");
Expect(!counter.GetProperty("members").EnumerateArray().Any(x =>
		x.GetProperty("kind").GetString() == "propertySetter"
		&& x.GetProperty("name").GetString() == "InitialOnly"),
	"init-only setter must not be exported as runtime mutation");
Expect(!counter.GetProperty("inheritable").GetBoolean()
	&& result.RegistrationSource.Contains("Inheritable = false", StringComparison.Ordinal),
	"generated bindings must not advertise script inheritance without a subclass factory");
Expect(result.ApiReference.Contains("Demo::Counter", StringComparison.Ordinal), "API reference missing type");
Expect(!string.IsNullOrWhiteSpace(result.RegistrationSource), "registration source missing");
Expect(result.AotBridgeSource.Contains("internal static partial class VeroScriptGeneratedProgram",
		StringComparison.Ordinal)
	&& result.AotBridgeSource.Contains("private static partial object VsHost_",
		StringComparison.Ordinal)
	&& result.AotBridgeSource.Contains("global::Demo.Counter", StringComparison.Ordinal),
	"AOT bridge source missing typed direct-call implementations");
Expect(!counter.GetProperty("members").EnumerateArray().Any(x =>
		x.GetProperty("name").GetString() == "Hidden")
	&& !result.RegistrationSource.Contains(".@Hidden(", StringComparison.Ordinal),
	"ignored member must be absent from catalog and registration source");
CSharpParseResult generatedCompilation = CSharpFrontend.ParseSources(
	[new("Bindings.cs", source), new("Bindings.g.cs", result.RegistrationSource)],
	new CSharpParseOptions { ReferencePaths = [typeof(VeroScript.VeroScriptSession).Assembly.Location] });
Expect(generatedCompilation.Succeeded,
	"generated registration source must compile: " + string.Join(" | ", generatedCompilation.Diagnostics
		.Where(diagnostic => diagnostic.Severity == CSharpDiagnosticSeverity.Error)
		.Select(diagnostic => diagnostic.Message)));

const string abstractSource = """
    public abstract class AbstractHost
    {
        public AbstractHost() { }
        public abstract int Read();
        public static AbstractHost Create() => new Concrete();
        private sealed class Concrete : AbstractHost { public override int Read() => 42; }
    }
    """;
CSharpBindGenResult abstractHost = CSharpBindGen.GenerateBindingsFromSource(abstractSource,
	bindGenOptions: new CSharpBindGenOptions { EmitAotBridgeSource = true });
Expect(abstractHost.Succeeded, "abstract host factories and instance methods remain exportable");
using (JsonDocument abstractCatalog = JsonDocument.Parse(abstractHost.CatalogJson))
{
	JsonElement abstractMembers = abstractCatalog.RootElement.GetProperty("types")[0].GetProperty("members");
	Expect(abstractMembers.GetArrayLength() == 2
		&& !abstractMembers.EnumerateArray().Any(member => member.GetProperty("kind").GetString() == "constructor"),
		"abstract types must not expose constructors in the catalog");
}
CSharpParseResult abstractCompilation = CSharpFrontend.ParseSources(
	[new("AbstractHost.cs", abstractSource), new("AbstractHost.g.cs", abstractHost.RegistrationSource)],
	new CSharpParseOptions { ReferencePaths = [typeof(VeroScript.VeroScriptSession).Assembly.Location] });
Expect(abstractCompilation.Succeeded, "abstract host registration compiles: "
	+ string.Join(" | ", abstractCompilation.Diagnostics.Select(diagnostic => diagnostic.Message)));
foreach (bool registration in new[] { false, true })
{
	CSharpBindGenResult nestedGeneric = CSharpBindGen.GenerateBindingsFromSource("""
        [System.AttributeUsage(System.AttributeTargets.Class)]
        internal sealed class VeroScriptBindingAttribute : System.Attribute { }
        public class Outer<T>
        {
            [VeroScriptBinding] public sealed class Inner { public int Value { get; set; } }
        }
        """, bindGenOptions: new CSharpBindGenOptions
		{
			RequireBindingAttribute = true, EmitRegistrationSource = registration, EmitAotBridgeSource = true
		});
	Expect(!nestedGeneric.Succeeded && nestedGeneric.Diagnostics.Any(diagnostic => diagnostic.Code == "VSCB1001")
		&& nestedGeneric.CatalogJson.Length == 0 && nestedGeneric.RegistrationSource.Length == 0
		&& nestedGeneric.AotBridgeSource.Length == 0,
		"nested types that depend on outer generic parameters fail before returning artifacts");
}

CSharpBindGenResult unsupported = CSharpBindGen.GenerateBindingsFromSource(
	"public class Bad { public void Optional(int value = 1) {} }");
Expect(!unsupported.Succeeded && unsupported.Diagnostics.Any(x => x.Code == "VSCB1006"),
	"default parameters must fail closed");
foreach (string declaration in new[]
{
	"public static void Update(ref string value) {}",
	"public static void Read(in string value) {}",
	"public static void Assign(out char value) { value = 'a'; }",
	"public static void Update(ref Mode value) {}",
	"public static void Update(ref Host value) {}"
})
{
	string referenceSource = "public enum Mode { Ready } public sealed class Host { " + declaration + " }";
	CSharpBindGenResult referenceBridge = CSharpBindGen.GenerateBindingsFromSource(referenceSource,
		bindGenOptions: new() { EmitRegistrationSource = false, EmitAotBridgeSource = true });
	Expect(!referenceBridge.Succeeded && referenceBridge.Diagnostics.Any(diagnostic => diagnostic.Code == "VSCB3007")
		&& referenceBridge.CatalogJson.Length == 0 && referenceBridge.AotBridgeSource.Length == 0,
		"AOT references requiring representation conversion must fail before returning artifacts");
	CSharpBindGenResult referenceCatalog = CSharpBindGen.GenerateBindingsFromSource(referenceSource,
		bindGenOptions: new() { EmitRegistrationSource = false });
	Expect(referenceCatalog.Succeeded, "catalog metadata remains available for explicit reference adapters");
}
CSharpBindGenResult unsupportedEvent = CSharpBindGen.GenerateBindingsFromSource(
	"public class BadEvent { public event System.Action? Changed; }");
Expect(!unsupportedEvent.Succeeded && unsupportedEvent.Diagnostics.Any(x => x.Code == "VSCB1008"),
	"public events must not disappear silently");
CSharpBindGenResult unsupportedBase = CSharpBindGen.GenerateBindingsFromSource(
	"public class Base {} public class Derived : Base {}");
Expect(!unsupportedBase.Succeeded && unsupportedBase.Diagnostics.Any(x => x.Code == "VSCB1009"),
	"host inheritance must not disappear silently");
CSharpBindGenResult structCatalog = CSharpBindGen.GenerateBindingsFromSource(
	"public struct Point { public int X; }",
	bindGenOptions: new CSharpBindGenOptions { EmitRegistrationSource = false });
Expect(structCatalog.Succeeded && structCatalog.CatalogJson.Contains("\"kind\": \"struct\"", StringComparison.Ordinal),
	"struct catalog generation must not treat System.ValueType as host inheritance");
CSharpBindGenResult refReturn = CSharpBindGen.GenerateBindingsFromSource(
	"public class RefHost { private int _value; public ref int Current => ref _value; "
	+ "public ref readonly int ReadonlyCurrent => ref _value; "
	+ "public ref int GetCurrent() => ref _value; }");
Expect(!refReturn.Succeeded && refReturn.Diagnostics.Count(x => x.Code == "VSCB1010") == 3,
	"ref/ref-readonly-return properties and methods must fail closed");
CSharpBindGenResult refStorage = CSharpBindGen.GenerateBindingsFromSource("""
    public ref struct RefStorage
    {
        public ref int Current;
        public RefStorage(ref int current) { Current = ref current; }
    }
    """);
Expect(!refStorage.Succeeded && refStorage.Diagnostics.Any(x => x.Code == "VSCB1014"),
	"ref-like storage must fail before object-based bridge generation");
CSharpBindGenResult overloadedMember = CSharpBindGen.GenerateBindingsFromSource(
	"public class Overloaded { public int Read(int value) => value; "
	+ "public string Read(string value) => value; }");
Expect(!overloadedMember.Succeeded
	&& overloadedMember.Diagnostics.Any(x => x.Code == "VSCB1012"),
	"member overloads unsupported by Binding Catalog v1 must fail before catalog import");
CSharpBindGenResult externalEnum = CSharpBindGen.GenerateBindingsFromSource(
	"public class EnumHost { public System.DayOfWeek Read() => System.DayOfWeek.Monday; }");
Expect(!externalEnum.Succeeded && externalEnum.Diagnostics.Any(x => x.Code == "VSCB1013"),
	"enum references without a catalog declaration must fail closed");
CSharpBindGenResult invalidMapKey = CSharpBindGen.GenerateBindingsFromSource(
	"public static class BadMap { public static System.Collections.Generic.Dictionary<double, int> Read() => []; }",
	bindGenOptions: new CSharpBindGenOptions { EmitRegistrationSource = false });
Expect(!invalidMapKey.Succeeded && invalidMapKey.Diagnostics.Any(x => x.Code == "VSCB1016"),
	"map keys outside the VeroScript runtime key domain must fail before catalog import");
CSharpBindGenResult invalidNestedSetKey = CSharpBindGen.GenerateBindingsFromSource(
	"public static class BadSet { public static System.Collections.Generic.List<System.Collections.Generic.HashSet<byte>> Read() => []; }",
	bindGenOptions: new CSharpBindGenOptions { EmitRegistrationSource = false });
Expect(!invalidNestedSetKey.Succeeded && invalidNestedSetKey.Diagnostics.Any(x => x.Code == "VSCB1016"),
	"unsupported nested set keys must be diagnosed recursively");
CSharpBindGenResult invalidSystemCharKey = CSharpBindGen.GenerateBindingsFromSource(
	"public static class BadCharMap { public static System.Collections.Generic.Dictionary<char, int> Read() => []; }",
	bindGenOptions: new CSharpBindGenOptions { EmitRegistrationSource = false });
Expect(!invalidSystemCharKey.Succeeded
	&& invalidSystemCharKey.Diagnostics.Any(x => x.Code == "VSCB1016"),
	"System.Char must remain rejected because it is wider than VeroScript signed char");
CSharpBindGenResult validEnumKey = CSharpBindGen.GenerateBindingsFromSource(
	"public enum Key { One } public static class EnumMap { "
	+ "public static System.Collections.Generic.Dictionary<Key, int> Read() => []; }",
	bindGenOptions: new CSharpBindGenOptions { EmitRegistrationSource = false });
Expect(validEnumKey.Succeeded, "declared enums must remain valid map keys: "
	+ string.Join(" | ", validEnumKey.Diagnostics.Select(x => x.Message)));
CSharpBindGenResult validSignedByteKeys = CSharpBindGen.GenerateBindingsFromSource(
	"public sealed class ByteContainers { "
	+ "public System.Collections.Generic.Dictionary<sbyte, int> Map("
	+ "System.Collections.Generic.Dictionary<sbyte, int> values) => values; "
	+ "public System.Collections.Generic.HashSet<sbyte> Set("
	+ "System.Collections.Generic.HashSet<sbyte> values) => values; }",
	bindGenOptions: new CSharpBindGenOptions
	{
		EmitRegistrationSource = true,
		EmitAotBridgeSource = true
	});
Expect(validSignedByteKeys.Succeeded
	&& validSignedByteKeys.CatalogJson.Contains("\"returnKeyTypeName\": \"int8\"", StringComparison.Ordinal)
	&& validSignedByteKeys.CatalogJson.Contains("\"returnElementTypeName\": \"int8\"", StringComparison.Ordinal)
	&& validSignedByteKeys.RegistrationSource.Contains("GetInt8()", StringComparison.Ordinal)
	&& validSignedByteKeys.RegistrationSource.Contains("Dictionary<sbyte, int>", StringComparison.Ordinal)
	&& validSignedByteKeys.AotBridgeSource.Contains("Dictionary<sbyte, int>", StringComparison.Ordinal),
	"sbyte map keys and set elements must follow VeroScript signed char support: "
	+ string.Join(" | ", validSignedByteKeys.Diagnostics.Select(x => x.Message)));
CSharpBindGenResult refLikeType = CSharpBindGen.GenerateBindingsFromSource(
	"public ref struct RefLikeHost { public int Value; }");
Expect(!refLikeType.Succeeded && refLikeType.Diagnostics.Any(x => x.Code == "VSCB1014"),
	"ref-like host types must fail before object-based bridge generation");
CSharpBindGenResult requiredType = CSharpBindGen.GenerateBindingsFromSource(
	"public class RequiredHost { public required int Value { get; init; } }");
Expect(!requiredType.Succeeded && requiredType.Diagnostics.Any(x => x.Code == "VSCB1015"),
	"required members must fail before generating constructor calls without object initializers");
CSharpBindGenResult aotContainer = CSharpBindGen.GenerateBindingsFromSource(
	"public static class Lists { public static System.Collections.Generic.List<int> Echo("
	+ "System.Collections.Generic.List<int> values) => values; }",
	bindGenOptions: new CSharpBindGenOptions
	{
		EmitRegistrationSource = false,
		EmitAotBridgeSource = true
	});
Expect(aotContainer.Succeeded
	&& aotContainer.AotBridgeSource.Contains("global::System.Collections.Generic.List<int>",
		StringComparison.Ordinal),
	"AOT bridge must preserve directly compatible primitive containers");
CSharpBindGenResult unsupportedAotContainer = CSharpBindGen.GenerateBindingsFromSource(
	"public static class TextLists { public static System.Collections.Generic.List<string> Echo("
	+ "System.Collections.Generic.List<string> values) => values; }",
	bindGenOptions: new CSharpBindGenOptions
	{
		EmitRegistrationSource = false,
		EmitAotBridgeSource = true
	});
Expect(!unsupportedAotContainer.Succeeded
	&& unsupportedAotContainer.Diagnostics.Any(x => x.Code == "VSCB3003"),
	"AOT bridge containers requiring element conversion must fail closed");
CSharpBindGenResult unsupportedLiveStaticContainer = CSharpBindGen.GenerateBindingsFromSource(
	"public static class Lists { public static System.Collections.Generic.List<int> Echo("
	+ "System.Collections.Generic.List<int> values) => values; }",
	bindGenOptions: new CSharpBindGenOptions { EmitAotBridgeSource = false });
Expect(!unsupportedLiveStaticContainer.Succeeded
	&& unsupportedLiveStaticContainer.Diagnostics.Any(x => x.Code == "VSCB3004"),
	"live static container members must fail before runtime type inference");
CSharpBindGenResult unsupportedIndexer = CSharpBindGen.GenerateBindingsFromSource(
	"public sealed class Indexed { public int this[int index] { get => index; set { } } }");
Expect(!unsupportedIndexer.Succeeded
	&& unsupportedIndexer.Diagnostics.Count(x => x.Code == "VSCB1017") == 1
	&& string.IsNullOrEmpty(unsupportedIndexer.CatalogJson)
	&& string.IsNullOrEmpty(unsupportedIndexer.RegistrationSource),
	"indexers must fail before catalog or live-registration output is emitted");
CSharpBindGenResult unsupportedAotStructMutation = CSharpBindGen.GenerateBindingsFromSource(
	"public struct MutablePoint { public int X; public void Move(int amount) => X += amount; }",
	bindGenOptions: new CSharpBindGenOptions
	{
		EmitRegistrationSource = false,
		EmitAotBridgeSource = true
	});
Expect(!unsupportedAotStructMutation.Succeeded
	&& unsupportedAotStructMutation.Diagnostics.Any(x => x.Code == "VSCB3006"),
	"AOT mutable struct receivers must fail instead of losing boxed write-back");
CSharpBindGenResult unsupportedSortedDictionary = CSharpBindGen.GenerateBindingsFromSource(
	"public sealed class Ordered { public System.Collections.Generic.SortedDictionary<int, int> Echo("
	+ "System.Collections.Generic.SortedDictionary<int, int> values) => values; }");
Expect(!unsupportedSortedDictionary.Succeeded
	&& unsupportedSortedDictionary.Diagnostics.Any(x => x.Code == "VSCB3002"),
	"SortedDictionary must not silently change ordered_map insertion-order semantics");
CSharpBindGenResult staticPrivateSetter = CSharpBindGen.GenerateBindingsFromSource(
	"public static class Settings { public static int Value { get; private set; } = 7; }",
	bindGenOptions: new CSharpBindGenOptions { EmitAotBridgeSource = true });
Expect(staticPrivateSetter.Succeeded
	&& staticPrivateSetter.RegistrationSource.Contains("global::Settings.@Value", StringComparison.Ordinal)
	&& staticPrivateSetter.AotBridgeSource.Contains("global::Settings.@Value", StringComparison.Ordinal),
	"a public static getter with a non-public setter must remain exportable as a static constant");
CSharpBindGenResult refConstructorOverloads = CSharpBindGen.GenerateBindingsFromSource(
	"public sealed class RefCtor { public RefCtor(int value) {} public RefCtor(ref int value) { value++; } }",
	bindGenOptions: new CSharpBindGenOptions
	{
		EmitRegistrationSource = false,
		EmitAotBridgeSource = true
	});
Expect(refConstructorOverloads.Succeeded, "value/ref constructor overloads: "
	+ string.Join(" | ", refConstructorOverloads.Diagnostics.Select(x => x.Message)));
using (JsonDocument refConstructorCatalog = JsonDocument.Parse(refConstructorOverloads.CatalogJson))
{
	string[] nativeNames = refConstructorCatalog.RootElement.GetProperty("types")[0]
		.GetProperty("members").EnumerateArray()
		.Where(member => member.GetProperty("kind").GetString() == "constructor")
		.Select(member => member.GetProperty("nativeName").GetString() ?? "")
		.ToArray();
	Expect(nativeNames.Length == 2 && nativeNames.All(name => name.StartsWith("\u001Fhostctor.", StringComparison.Ordinal))
		&& nativeNames.Distinct(StringComparer.Ordinal).Count() == 2,
		"constructor native identities must be canonical and preserve ref shape");
}
CSharpBindGenResult invalidGeneratedName = CSharpBindGen.GenerateBindingsFromSource(
	"public class Valid {}", bindGenOptions: new CSharpBindGenOptions
	{
		RegistrationNamespace = "invalid namespace"
	});
Expect(!invalidGeneratedName.Succeeded
	&& invalidGeneratedName.Diagnostics.Any(x => x.Code == "VSCB3000"),
	"invalid generated identifiers must be diagnosed");
CSharpBindGenResult deterministic = CSharpBindGen.GenerateBindingsFromSource(source, "Bindings.cs",
	bindGenOptions: new CSharpBindGenOptions { EmitAotBridgeSource = true });
Expect(deterministic.CatalogJson == result.CatalogJson
	&& deterministic.ApiReference == result.ApiReference
	&& deterministic.RegistrationSource == result.RegistrationSource
	&& deterministic.AotBridgeSource == result.AotBridgeSource,
	"binding generation must be deterministic");

string projectRoot = Path.Combine(Path.GetTempPath(),
	"veroscript-csharp-bindgen-project-" + Guid.NewGuid().ToString("N"));
string firstDirectory = Path.Combine(projectRoot, "first");
string secondDirectory = Path.Combine(projectRoot, "second");
Directory.CreateDirectory(firstDirectory);
Directory.CreateDirectory(secondDirectory);
string firstPath = Path.Combine(firstDirectory, "Shared.cs");
string secondPath = Path.Combine(secondDirectory, "Shared.cs");
File.WriteAllText(firstPath,
	"namespace Project; public sealed class First { public Second Echo(Second value) => value; } "
	+ "public sealed partial class Split { public int A { get; set; } }");
File.WriteAllText(secondPath,
	"namespace Project; public sealed class Second { public int Value { get; set; } } "
	+ "public sealed partial class Split { public int B() => A; }");
try
{
	var projectOptions = new CSharpBindGenOptions
	{
		EmitApiReference = true,
		EmitAotBridgeSource = true
	};
	var projectLayout = new CSharpBindGenProjectLayout
	{
		CatalogPath = "generated/bindings.json",
		RegistrationPath = "generated/Bindings.g.cs",
		AotBridgePath = "generated/Bindings.Aot.g.cs",
		ApiReferenceDirectory = "generated/api"
	};
	CSharpBindGenProjectResult project = CSharpBindGen.GenerateBindingProject(
		[firstPath, secondPath], bindGenOptions: projectOptions, layout: projectLayout);
	Expect(project.Succeeded, "multi-file binding project: "
		+ string.Join(" | ", project.Diagnostics.Select(diagnostic => diagnostic.Message)));
	Expect(project.ApiReferences.Count == 2
		&& project.ApiReferences.Select(reference => reference.OutputName)
			.Distinct(StringComparer.OrdinalIgnoreCase).Count() == 2,
		"same-stem project outputs must have stable collision suffixes");
	Expect(project.ApiReferences[0].Contents.Contains("class Project::First {", StringComparison.Ordinal)
		&& project.ApiReferences[0].Contents.Contains("class Project::Split {", StringComparison.Ordinal)
		&& !project.ApiReferences[0].Contents.Contains("class Project::Second {", StringComparison.Ordinal)
		&& project.ApiReferences[1].Contents.Contains("class Project::Second {", StringComparison.Ordinal)
		&& !project.ApiReferences[1].Contents.Contains("class Project::Split {", StringComparison.Ordinal),
		"API references must retain per-input ownership");
	Expect(project.ManifestJson.Contains("veroscript.csharp-binding-project", StringComparison.Ordinal)
		&& project.ManifestJson.Contains("generated/api/", StringComparison.Ordinal)
		&& project.CatalogJson.Contains("Project::First", StringComparison.Ordinal)
		&& project.RegistrationSource.Contains("Project::Second", StringComparison.Ordinal)
		&& project.AotBridgeSource.Contains("global::Project.Second", StringComparison.Ordinal)
		&& project.ManifestJson.Contains("generated/Bindings.Aot.g.cs", StringComparison.Ordinal),
		"project artifacts and manifest");
	using JsonDocument projectCatalog = JsonDocument.Parse(project.CatalogJson);
	Expect(projectCatalog.RootElement.GetProperty("types").EnumerateArray()
		.Count(type => type.GetProperty("name").GetString() == "Project::Split") == 1,
		"partial project type must be merged exactly once");
	CSharpBindGenProjectResult repeatedProject = CSharpBindGen.GenerateBindingProject(
		[firstPath, secondPath], bindGenOptions: projectOptions, layout: projectLayout);
	Expect(repeatedProject.CatalogJson == project.CatalogJson
		&& repeatedProject.RegistrationSource == project.RegistrationSource
		&& repeatedProject.AotBridgeSource == project.AotBridgeSource
		&& repeatedProject.ManifestJson == project.ManifestJson
		&& repeatedProject.ApiReferences.SequenceEqual(project.ApiReferences),
		"project generation must be deterministic");
	CSharpBindGenProjectResult duplicateProject = CSharpBindGen.GenerateBindingProject(
		[firstPath, Path.Combine(firstDirectory, ".", "Shared.cs")]);
	Expect(!duplicateProject.Succeeded
		&& duplicateProject.Diagnostics.Any(diagnostic => diagnostic.Code == "VSCB2001"),
		"duplicate project inputs must fail before generation");

	const string apiExtension = ".api.vs.txt";
	string reservedStem = project.ApiReferences[0].OutputName[..^apiExtension.Length];
	string reservedPath = Path.Combine(projectRoot, reservedStem + ".cs");
	File.WriteAllText(reservedPath, "namespace Project; public static class Reserved { public static int Read() => 7; }");
	CSharpBindGenProjectResult collidingProject = CSharpBindGen.GenerateBindingProject(
		[firstPath, secondPath, reservedPath], bindGenOptions: projectOptions, layout: projectLayout);
	Expect(collidingProject.Succeeded && collidingProject.ApiReferences.Count == 3
		&& collidingProject.ApiReferences.Select(reference => reference.OutputName)
			.Distinct(StringComparer.OrdinalIgnoreCase).Count() == 3,
		"API reference names must stay unique when an input already uses a hashed stem");
	Expect(collidingProject.ApiReferences.Single(reference => reference.InputPath == reservedPath)
		.Contents.Contains("class Project::Reserved {", StringComparison.Ordinal),
		"collision resolution must retain per-input API reference ownership");
	CSharpBindGenProjectResult reversedProject = CSharpBindGen.GenerateBindingProject(
		[reservedPath, secondPath, firstPath], bindGenOptions: projectOptions, layout: projectLayout);
	Expect(reversedProject.Succeeded && collidingProject.ApiReferences.All(reference =>
		reversedProject.ApiReferences.Single(other => other.InputPath == reference.InputPath)
			.OutputName == reference.OutputName),
		"API reference collision resolution must be stable when project inputs are reordered");

	string composedPath = Path.Combine(firstDirectory, "caf\u00e9.cs");
	string decomposedPath = Path.Combine(secondDirectory, "cafe\u0301.cs");
	File.WriteAllText(composedPath, "namespace Portable; public sealed class Composed {}");
	File.WriteAllText(decomposedPath, "namespace Portable; public sealed class Decomposed {}");
	CSharpBindGenProjectResult unicodeProject = CSharpBindGen.GenerateBindingProject(
		[composedPath, decomposedPath], bindGenOptions: projectOptions, layout: projectLayout);
	Expect(unicodeProject.Succeeded && unicodeProject.ApiReferences.Count == 2
		&& unicodeProject.ApiReferences.Select(reference => reference.OutputName.Normalize(NormalizationForm.FormC))
			.Distinct(StringComparer.OrdinalIgnoreCase).Count() == 2,
		"API reference names must remain distinct on Unicode-normalizing filesystems");
	Expect(unicodeProject.ApiReferences.Single(reference => reference.InputPath == composedPath)
			.Contents.Contains("class Portable::Composed {", StringComparison.Ordinal)
		&& unicodeProject.ApiReferences.Single(reference => reference.InputPath == decomposedPath)
			.Contents.Contains("class Portable::Decomposed {", StringComparison.Ordinal),
		"Unicode filename collision resolution must retain per-input API reference ownership");
	CSharpBindGenProjectResult reversedUnicodeProject = CSharpBindGen.GenerateBindingProject(
		[decomposedPath, composedPath], bindGenOptions: projectOptions, layout: projectLayout);
	Expect(reversedUnicodeProject.Succeeded && unicodeProject.ApiReferences.All(reference =>
		reversedUnicodeProject.ApiReferences.Single(other => other.InputPath == reference.InputPath)
			.OutputName == reference.OutputName),
		"Unicode filename collision resolution must remain stable when inputs are reordered");
}
finally
{
	Directory.Delete(projectRoot, recursive: true);
}

Console.WriteLine("VeroScriptCSharpBindGen tests passed.");

static void Expect(bool condition, string message)
{
	if (!condition)
	{
		throw new InvalidOperationException(message);
	}
}
