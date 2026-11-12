// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript.CSharpFrontend;

public enum CSharpDiagnosticSeverity
{
	Info,
	Warning,
	Error
}

public sealed record CSharpSourceLocation(
	string File,
	int Line,
	int Column,
	int Offset,
	int Length)
{
	public static CSharpSourceLocation None { get; } = new("", 1, 1, 0, 0);
}

public sealed record CSharpDiagnostic(
	CSharpDiagnosticSeverity Severity,
	string Code,
	string Message,
	CSharpSourceLocation Location);

public enum CSharpTypeKind
{
	Unknown,
	Class,
	Struct,
	Interface,
	Enum,
	Delegate,
	TypeParameter,
	Array,
	Pointer,
	Dynamic,
	Error
}

public enum CSharpRefKind
{
	None,
	Ref,
	Out,
	In,
	RefReadOnly
}

public enum CSharpAccessibility
{
	NotApplicable,
	Private,
	ProtectedAndInternal,
	Protected,
	Internal,
	ProtectedOrInternal,
	Public
}

public enum CSharpCallableKind
{
	Method,
	Constructor,
	Operator,
	Conversion
}

public sealed record CSharpAttribute(
	string TypeName,
	IReadOnlyList<string> ConstructorArguments,
	IReadOnlyDictionary<string, string> NamedArguments,
	CSharpSourceLocation Location);

public sealed record CSharpTypeReference(
	string DisplayName,
	string QualifiedName,
	CSharpTypeKind Kind,
	bool IsNullable,
	int ArrayRank,
	CSharpRefKind RefKind,
	IReadOnlyList<CSharpTypeReference> TypeArguments,
	CSharpSourceLocation Location);

public sealed record CSharpParameterDeclaration(
	string Name,
	CSharpTypeReference Type,
	bool HasDefaultValue,
	string DefaultValue,
	bool IsParams,
	IReadOnlyList<CSharpAttribute> Attributes,
	CSharpSourceLocation Location);

public sealed record CSharpCallableDeclaration(
	string Name,
	string QualifiedName,
	CSharpCallableKind Kind,
	CSharpTypeReference ReturnType,
	IReadOnlyList<CSharpParameterDeclaration> Parameters,
	IReadOnlyList<string> TypeParameters,
	CSharpAccessibility Accessibility,
	bool IsStatic,
	bool IsAbstract,
	bool IsVirtual,
	bool IsOverride,
	bool IsSealed,
	bool IsExtern,
	bool IsAsync,
	string BodyText,
	string DocumentationXml,
	IReadOnlyList<CSharpAttribute> Attributes,
	CSharpSourceLocation Location);

public sealed record CSharpFieldDeclaration(
	string Name,
	string QualifiedName,
	CSharpTypeReference Type,
	CSharpAccessibility Accessibility,
	bool IsStatic,
	bool IsReadOnly,
	bool IsConst,
	string ConstantValue,
	string InitializerText,
	string DocumentationXml,
	IReadOnlyList<CSharpAttribute> Attributes,
	CSharpSourceLocation Location)
{
	public bool IsRequired { get; init; }
}

public sealed record CSharpPropertyDeclaration(
	string Name,
	string QualifiedName,
	CSharpTypeReference Type,
	CSharpAccessibility Accessibility,
	bool IsStatic,
	CSharpAccessibility GetterAccessibility,
	CSharpAccessibility SetterAccessibility,
	bool HasGetter,
	bool HasSetter,
	bool IsInitOnly,
	bool IsIndexer,
	IReadOnlyList<CSharpParameterDeclaration> Parameters,
	string DocumentationXml,
	IReadOnlyList<CSharpAttribute> Attributes,
	CSharpSourceLocation Location)
{
	public bool IsRequired { get; init; }
}

public sealed record CSharpEventDeclaration(
	string Name,
	string QualifiedName,
	CSharpTypeReference Type,
	CSharpAccessibility Accessibility,
	bool IsStatic,
	string DocumentationXml,
	IReadOnlyList<CSharpAttribute> Attributes,
	CSharpSourceLocation Location);

public sealed record CSharpEnumValueDeclaration(
	string Name,
	string QualifiedName,
	string ConstantValue,
	string DocumentationXml,
	IReadOnlyList<CSharpAttribute> Attributes,
	CSharpSourceLocation Location);

public sealed record CSharpTypeDeclaration(
	string Name,
	string QualifiedName,
	CSharpTypeKind Kind,
	CSharpAccessibility Accessibility,
	bool IsEffectivelyPublic,
	bool IsStatic,
	bool IsAbstract,
	bool IsSealed,
	bool IsPartial,
	CSharpTypeReference? BaseType,
	IReadOnlyList<CSharpTypeReference> Interfaces,
	IReadOnlyList<string> TypeParameters,
	IReadOnlyList<CSharpFieldDeclaration> Fields,
	IReadOnlyList<CSharpPropertyDeclaration> Properties,
	IReadOnlyList<CSharpEventDeclaration> Events,
	IReadOnlyList<CSharpCallableDeclaration> Callables,
	CSharpTypeReference? EnumUnderlyingType,
	IReadOnlyList<CSharpEnumValueDeclaration> EnumValues,
	string DocumentationXml,
	IReadOnlyList<CSharpAttribute> Attributes,
	IReadOnlyList<CSharpSourceLocation> Declarations)
{
	public bool IsRefLike { get; init; }
}

public sealed record CSharpTranslationUnit(
	IReadOnlyList<string> SourcePaths,
	IReadOnlyList<CSharpTypeDeclaration> Types);

public sealed class CSharpParseOptions
{
	public string LanguageVersion { get; init; } = "latest";
	public bool AllowUnsafe { get; init; }
	public bool TreatWarningsAsErrors { get; init; }
	public IReadOnlyList<string> PreprocessorSymbols { get; init; } = [];
	public IReadOnlyList<string> ReferencePaths { get; init; } = [];
}

public sealed record CSharpSourceFile(string Path, string Text);

public sealed record CSharpParseResult(
	CSharpTranslationUnit Unit,
	IReadOnlyList<CSharpDiagnostic> Diagnostics)
{
	public bool Succeeded => Diagnostics.All(diagnostic =>
		diagnostic.Severity != CSharpDiagnosticSeverity.Error);
}

public sealed class CSharpTypeMapOptions
{
	public IReadOnlyList<string> KnownHostTypes { get; init; } = [];
	public IReadOnlyList<string> KnownEnumTypes { get; init; } = [];
}

public sealed record CSharpTypeMapResult(
	bool Success,
	string VeroType,
	string ErrorCode,
	string ErrorMessage);
