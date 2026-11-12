// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.CSharpFrontend;

string publicApiBaseline = Path.Combine(AppContext.BaseDirectory, "PublicApi.txt");
if (args is ["--write-public-api", string outputPath])
{
	File.WriteAllLines(outputPath,
		PublicApiContract.Generate(typeof(CSharpFrontend).Assembly));
	return;
}
PublicApiContract.Verify(typeof(CSharpFrontend).Assembly, publicApiBaseline,
	"VeroScriptCSharpFrontend");

static void Expect(bool condition, string message)
{
	if (!condition)
	{
		throw new InvalidOperationException($"FAILED: {message}");
	}
}

CSharpParseResult result = CSharpFrontend.ParseSources(
[
	new("Counter.cs", """
        using System;
        using System.Collections.Generic;
        namespace Demo;

        [Flags]
        public enum Mode : long { None = 0, Read = 1, Write = 2 }

        public interface ICounter { int Add(int delta); }

        /// <summary>Mutable counter.</summary>
        public partial class Counter : ICounter
        {
            public const int DefaultValue = 4;
            private int _value = DefaultValue;
            public int Value { get; private set; }
            public Counter(int value = DefaultValue) { _value = value; }
            public virtual int Add(int delta) => _value += delta;
        }

        public static class HostApi
        {
            public static Dictionary<string, int> Count(List<int> values,
                ref int total, in Mode mode) => throw new NotImplementedException();
        }
        """),
	new("Counter.Partial.cs", """
        namespace Demo;
        public partial class Counter
        {
            public void Reset() { }
        }
        """)
]);

Expect(result.Succeeded, "valid multi-file source: "
	+ string.Join(" | ", result.Diagnostics.Select(diagnostic => diagnostic.Message)));
Expect(result.Unit.SourcePaths.Count == 2, "source path preservation");
Expect(result.Unit.Types.Count == 4, "declaration collection");

CSharpTypeDeclaration mode = result.Unit.Types.Single(type => type.QualifiedName == "Demo.Mode");
Expect(mode.Kind == CSharpTypeKind.Enum && mode.EnumValues.Count == 3
	&& mode.EnumUnderlyingType?.DisplayName == "long", "enum semantics");
Expect(mode.Attributes.Any(attribute => attribute.TypeName == "System.FlagsAttribute"),
	"attribute identity");

CSharpTypeDeclaration counter = result.Unit.Types.Single(type => type.QualifiedName == "Demo.Counter");
Expect(counter.IsPartial && counter.Declarations.Count == 2, "partial type merge");
Expect(counter.Interfaces.Single().QualifiedName == "Demo.ICounter", "interface identity");
Expect(counter.Fields.Any(field => field.Name == "DefaultValue" && field.IsConst
	&& field.ConstantValue == "4"), "constant field");
Expect(counter.Properties.Single(property => property.Name == "Value").HasGetter,
	"property accessors");
Expect(counter.Properties.Single(property => property.Name == "Value").GetterAccessibility
		== CSharpAccessibility.Public
	&& counter.Properties.Single(property => property.Name == "Value").SetterAccessibility
		== CSharpAccessibility.Private,
	"property accessor visibility");
Expect(counter.Callables.Count(callable => callable.Kind == CSharpCallableKind.Constructor) == 1
	&& counter.Callables.Any(callable => callable.Name == "Reset"), "callable collection");
Expect(counter.DocumentationXml.Contains("Mutable counter", StringComparison.Ordinal),
	"XML documentation");

CSharpCallableDeclaration count = result.Unit.Types
	.Single(type => type.QualifiedName == "Demo.HostApi")
	.Callables.Single(callable => callable.Name == "Count");
Expect(count.Parameters[1].Type.RefKind == CSharpRefKind.Ref
	&& count.Parameters[2].Type.RefKind == CSharpRefKind.In, "parameter ref kinds");
Expect(CSharpTypeMapper.MapTypeToVeroScript(count.ReturnType).VeroType == "map<string,int>",
	"dictionary type mapping");
Expect(CSharpTypeMapper.MapTypeToVeroScript(count.Parameters[0].Type).VeroType == "array<int>",
	"list type mapping");
Expect(CSharpTypeMapper.MapTypeToVeroScript(count.Parameters[2].Type,
	new CSharpTypeMapOptions { KnownEnumTypes = ["Demo.Mode"] }).VeroType == "Demo::Mode",
	"known enum type mapping");

CSharpParseResult otherDictionaryArity = CSharpFrontend.ParseSource("""
    namespace System.Collections.Generic
    {
        public class Dictionary<T1, T2, T3> { }
    }
    namespace Demo
    {
        public class ThreeArgumentHost
        {
            public System.Collections.Generic.Dictionary<int, int, int> Values = new();
        }
    }
    """, "three-argument-dictionary.cs");
Expect(otherDictionaryArity.Succeeded, "valid custom dictionary of different arity");
CSharpTypeReference otherDictionaryType = otherDictionaryArity.Unit.Types
	.Single(type => type.QualifiedName == "Demo.ThreeArgumentHost").Fields.Single().Type;
Expect(!CSharpTypeMapper.MapTypeToVeroScript(otherDictionaryType).Success,
	"custom dictionary of different arity must not become a three-argument VeroScript map");

CSharpParseResult duplicate = CSharpFrontend.ParseSources(
[
	new("same.cs", "class First {}"),
	new("same.cs", "class Second {}")
]);
Expect(!duplicate.Succeeded && duplicate.Diagnostics.Any(diagnostic => diagnostic.Code == "VSCF0002"),
	"duplicate source rejection");

CSharpParseResult invalid = CSharpFrontend.ParseSource(
	"public class Broken { public Missing Value { get; } }", "broken.cs");
Expect(!invalid.Succeeded && invalid.Diagnostics.Any(diagnostic => diagnostic.Code == "CS0246"),
	"semantic diagnostics");

CSharpParseResult unsafeSource = CSharpFrontend.ParseSource(
	"public unsafe class UnsafeType { public int* Value; }", "unsafe.cs");
Expect(!unsafeSource.Succeeded, "unsafe source rejected by default");
CSharpParseResult allowedUnsafe = CSharpFrontend.ParseSource(
	"public unsafe class UnsafeType { public int* Value; }", "unsafe.cs",
	new CSharpParseOptions { AllowUnsafe = true });
Expect(allowedUnsafe.Succeeded, "explicit unsafe parsing policy");
CSharpTypeReference pointer = allowedUnsafe.Unit.Types.Single().Fields.Single().Type;
Expect(!CSharpTypeMapper.MapTypeToVeroScript(pointer).Success, "pointer mapping fails closed");

CSharpParseResult implicitApi = CSharpFrontend.ParseSource(
	"public class DefaultConstructible { public event System.Action? Changed; } "
	+ "public class TextUnit { public char Value; }");
Expect(implicitApi.Succeeded, "implicit API source");
Expect(implicitApi.Unit.Types.Single(type => type.Name == "DefaultConstructible").Callables
	.Any(callable => callable.Kind == CSharpCallableKind.Constructor
		&& callable.Accessibility == CSharpAccessibility.Public),
	"implicit public constructor collection");
Expect(implicitApi.Unit.Types.Single(type => type.Name == "DefaultConstructible").Events
	.Single().Name == "Changed", "event declaration collection");
Expect(CSharpTypeMapper.MapTypeToVeroScript(
	implicitApi.Unit.Types.Single(type => type.Name == "TextUnit").Fields.Single().Type).VeroType == "uint16",
	"C# char must preserve its unsigned UTF-16 code-unit domain");
Expect(!CSharpFrontend.ParseSource(
		"internal class Outer { public class Nested {} }").Unit.Types
	.Single(type => type.Name == "Nested").IsEffectivelyPublic,
	"nested type effective accessibility");

CSharpParseResult positionalRecord = CSharpFrontend.ParseSource(
	"namespace Demo; public record Point(int X, int Y); public record Explicit(int X) "
	+ "{ public int X { get; init; } = X; }");
Expect(positionalRecord.Succeeded, "positional record source");
Expect(positionalRecord.Unit.Types.Single(type => type.Name == "Point").Properties
		.Select(property => property.Name).SequenceEqual(["X", "Y"]),
	"positional record properties");
Expect(positionalRecord.Unit.Types.Single(type => type.Name == "Explicit").Properties.Count == 1,
	"explicit positional record property de-duplication");

CSharpParseResult valueAndReferenceSemantics = CSharpFrontend.ParseSource("""
    public struct ValueHost { public int Value; }
    public partial class PartialHost
    {
        private int _value;
        public partial int Twice(int value);
        public ref int Current => ref _value;
        public ref readonly int ReadonlyCurrent => ref _value;
    }
    public partial class PartialHost
    {
        public partial int Twice(int value) => value * 2;
    }
    public delegate int Transform(int value);
    public ref struct RefStorage
    {
        public ref int Current;
        public ref readonly int ReadonlyCurrent;
        public RefStorage(ref int current)
        {
            Current = ref current;
            ReadonlyCurrent = ref current;
        }
    }
    """);
Expect(valueAndReferenceSemantics.Succeeded, "value/ref/partial source semantics");
Expect(valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "ValueHost").BaseType is null,
	"struct System.ValueType base must remain implicit");
Expect(valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "PartialHost").Callables
		.Count(callable => callable.Name == "Twice") == 1,
	"partial method definition and implementation de-duplication");
CSharpParseResult overloads = CSharpFrontend.ParseSource(
   "public class Overloads { public int Read(int value) => value; "
   + "public string Read(string value) => value; }");
Expect(overloads.Succeeded
	&& overloads.Unit.Types.Single().Callables.Count(callable => callable.Name == "Read") == 2,
	"overloaded callables must retain distinct semantic identities");
Expect(valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "PartialHost").Properties
		.Single(property => property.Name == "Current").Type.RefKind == CSharpRefKind.Ref,
	"ref-return property semantics");
Expect(valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "PartialHost").Properties
		.Single(property => property.Name == "ReadonlyCurrent").Type.RefKind == CSharpRefKind.RefReadOnly,
	"ref-readonly-return property semantics");
Expect(valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "Transform").Callables
		.Count(callable => callable.Kind == CSharpCallableKind.Constructor) == 0,
	"delegate synthetic constructor exclusion");
Expect(valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "RefStorage").Fields
		.Single(field => field.Name == "Current").Type.RefKind == CSharpRefKind.Ref
	&& valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "RefStorage").Fields
		.Single(field => field.Name == "ReadonlyCurrent").Type.RefKind == CSharpRefKind.RefReadOnly,
	"ref/ref-readonly field storage semantics");
Expect(valueAndReferenceSemantics.Unit.Types.Single(type => type.Name == "RefStorage").IsRefLike,
	"ref-like type semantics");

CSharpParseResult requiredMembers = CSharpFrontend.ParseSource(
	"public class RequiredHost { public required int Field; public required string Name { get; init; } }");
Expect(requiredMembers.Succeeded
	&& requiredMembers.Unit.Types.Single().Fields.Single().IsRequired
	&& requiredMembers.Unit.Types.Single().Properties.Single().IsRequired,
	"required member semantics");

string fileProjectRoot = Path.Combine(Path.GetTempPath(),
	"veroscript-csharp-frontend-files-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(fileProjectRoot);
string fileProjectSource = Path.Combine(fileProjectRoot, "Input.cs");
File.WriteAllText(fileProjectSource, "public sealed class Input {}");
try
{
	CSharpParseResult duplicateFile = CSharpFrontend.ParseFiles(
		[fileProjectSource, Path.Combine(fileProjectRoot, ".", "Input.cs")]);
	Expect(!duplicateFile.Succeeded
		&& duplicateFile.Diagnostics.Any(diagnostic => diagnostic.Code == "VSCF0002"),
		"duplicate physical multi-file inputs");
}
finally
{
	Directory.Delete(fileProjectRoot, recursive: true);
}

Console.WriteLine("VeroScriptCSharpFrontend tests passed.");
