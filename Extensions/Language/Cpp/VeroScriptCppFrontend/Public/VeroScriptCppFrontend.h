// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::CppFrontend
{
	enum class EDiagnosticSeverity : std::uint8_t
	{
		Note = 0,
		Warning,
		Error,
	};

	enum class ETokenKind : std::uint8_t
	{
		Identifier = 0,
		Number,
		StringLiteral,
		CharacterLiteral,
		Symbol,
		Preprocessor,
		End,
	};

	enum class EReferenceKind : std::uint8_t
	{
		None = 0,
		LValue,
		RValue,
	};

	enum class EAccess : std::uint8_t
	{
		Public = 0,
		Protected,
		Private,
	};

	enum class ERecordKind : std::uint8_t
	{
		Struct = 0,
		Class,
	};

	struct FSourceLocation
	{
		std::string File;
		std::size_t Line = 1;
		std::size_t Column = 1;
		std::size_t Offset = 0;
	};

	struct FDiagnostic
	{
		EDiagnosticSeverity Severity = EDiagnosticSeverity::Error;
		std::string Code;
		std::string Message;
		FSourceLocation Location;
	};

	struct FToken
	{
		ETokenKind Kind = ETokenKind::End;
		std::string Text;
		FSourceLocation Location;
		std::size_t EndOffset = 0;
	};

	struct FAttributeArgument
	{
		std::string Name;
		std::string Value;
		bool bHasValue = false;
	};

	struct FAttribute
	{
		std::string NamespaceName;
		std::string Name;
		std::vector<FAttributeArgument> Arguments;
		FSourceLocation Location;
	};

	struct FType
	{
		std::string Spelling;
		std::string BaseName;
		std::vector<FType> TemplateArguments;
		bool bConst = false;
		bool bVolatile = false;
		std::uint8_t PointerDepth = 0;
		// Qualifiers on the outermost pointer itself (`T* const`), kept
		// separate from bConst/bVolatile, which qualify T (`const T*`).
		// Multi-level pointers are rejected by the strict type mapper.
		bool bPointerConst = false;
		bool bPointerVolatile = false;
		EReferenceKind Reference = EReferenceKind::None;
		// Namespace/type declaration scope used to resolve an unqualified user type.
		// Empty denotes the global scope.
		std::string DeclarationScope;
		FSourceLocation Location;
		// Bound template parameter, preserved through alias substitution and merge.
		// Its name must not be looked up again as an outer record or alias.
		bool bTemplateParameter = false;
	};

	struct FParameter
	{
		std::string Name;
		FType Type;
		std::string DefaultValue;
		std::vector<FToken> DefaultValueTokens;
		std::vector<FAttribute> Attributes;
		FSourceLocation Location;
		// False when Name is the generated ArgN placeholder for an unnamed C++
		// parameter. Merge/tooling can then prefer a real name from another declaration.
		bool bNameExplicit = false;
	};

	struct FTemplateParameter
	{
		std::string Name;
		FType DefaultType;
		FSourceLocation Location;
		bool bHasDefault = false;
	};

	struct FFunction
	{
		std::string Name;
		std::string QualifiedName;
		std::string OwnerQualifiedName;
		FType ReturnType;
		std::vector<FAttribute> Attributes;
		std::vector<FTemplateParameter> TemplateParameters;
		std::vector<FParameter> Parameters;
		std::vector<FToken> BodyTokens;
		FSourceLocation Location;
		EAccess Access = EAccess::Public;
		bool bConstructor = false;
		bool bDestructor = false;
		bool bOutOfClassMember = false;
		bool bStatic = false;
		bool bInline = false;
		bool bExtern = false;
		bool bCLinkage = false;
		bool bVirtual = false;
		bool bOverride = false;
		bool bFinal = false;
		bool bConstMethod = false;
		bool bExplicit = false;
		bool bConstexpr = false;
		bool bConsteval = false;
		bool bNoexcept = false;
		bool bDeleted = false;
		bool bDefaulted = false;
		bool bPureVirtual = false;
		bool bHasBody = false;
	};

	struct FField
	{
		std::string Name;
		FType Type;
		std::vector<FToken> InitializerTokens;
		std::vector<FAttribute> Attributes;
		FSourceLocation Location;
		EAccess Access = EAccess::Public;
		bool bStatic = false;
		bool bInline = false;
		bool bExtern = false;
		bool bConstexpr = false;
		bool bConstinit = false;
		bool bMutable = false;
		// A namespace-scope `T Owner::Field` storage definition was paired with
		// this static member (possibly from a sibling project input).
		bool bDefinedOutOfClass = false;
	};

	struct FBaseType
	{
		std::string TypeName;
		FSourceLocation Location;
		EAccess Access = EAccess::Private;
		bool bVirtual = false;
		// Preserve lookup scopes and template arguments through project merging.
		// An empty BaseName selects the legacy TypeName spelling for manual ASTs.
		FType Type;
	};

	struct FRecord
	{
		std::string Name;
		std::string QualifiedName;
		ERecordKind Kind = ERecordKind::Struct;
		bool bForwardDeclaration = false;
		bool bFinal = false;
		std::vector<FAttribute> Attributes;
		std::vector<FTemplateParameter> TemplateParameters;
		std::vector<FBaseType> BaseTypes;
		std::vector<std::vector<FToken>> FriendDeclarations;
		std::vector<FField> Fields;
		std::vector<FFunction> Methods;
		FSourceLocation Location;
	};

	struct FEnumValue
	{
		std::string Name;
		std::string Initializer;
		std::vector<FToken> InitializerTokens;
		std::vector<FAttribute> Attributes;
		FSourceLocation Location;
	};

	struct FEnum
	{
		std::string Name;
		std::string QualifiedName;
		std::string UnderlyingType = "int";
		std::vector<FAttribute> Attributes;
		std::vector<FEnumValue> Values;
		FSourceLocation Location;
		bool bScoped = false;
		bool bForwardDeclaration = false;
		// Unscoped initializers use their expression types unless ':' fixes the type.
		bool bHasExplicitUnderlyingType = false;
	};

	struct FVariable
	{
		std::string Name;
		std::string QualifiedName;
		std::string OwnerQualifiedName;
		FType Type;
		std::vector<FToken> InitializerTokens;
		std::vector<FAttribute> Attributes;
		FSourceLocation Location;
		bool bStatic = false;
		bool bInline = false;
		bool bExtern = false;
		bool bCLinkage = false;
		bool bConstexpr = false;
		bool bConstinit = false;
		bool bOutOfClassMember = false;
	};

	struct FUsingNamespace
	{
		std::string Scope;
		std::string NamespaceName;
		FSourceLocation Location;
	};

	struct FTypeAlias
	{
		std::string Name;
		std::string QualifiedName;
		FType TargetType;
		std::vector<FTemplateParameter> TemplateParameters;
		FSourceLocation Location;
	};

	struct FTranslationUnit
	{
		std::string SourcePath;
		std::vector<FEnum> Enums;
		std::vector<FRecord> Records;
		std::vector<FFunction> Functions;
		std::vector<FVariable> Variables;
		std::vector<FTypeAlias> TypeAliases;
		std::vector<FUsingNamespace> UsingNamespaces;
		// Named namespaces declared by this input. Used to distinguish qualified
		// namespace definitions (`N::Value`) from class-member definitions.
		std::vector<std::string> Namespaces;
	};

	struct FParseOptions
	{
		// #include and #pragma once are discovery/build concerns and are ignored by
		// the strict source parser. Other preprocessor directives fail by default.
		bool bAllowIncludeAndPragma = true;
		bool bRejectUnknownPreprocessorDirectives = true;
		bool bAllowFunctionBodies = true;
		// Project collectors set this while reading individual translation units so
		// an out-of-class member definition may be paired with a record declaration
		// supplied by another input. Public single-source parsing keeps the strict
		// default and diagnoses a missing owner immediately.
		bool bDeferOutOfClassMemberResolution = false;
	};

	struct FParseResult
	{
		bool Succeeded() const;

		FTranslationUnit Unit;
		std::vector<FDiagnostic> Diagnostics;

	};

	struct FTypeMapOptions
	{
		// Raw pointers have ownership/nullability semantics that a generator cannot
		// infer. Enabling this explicit policy permits only one pointer level to a
		// known host type; scalar, unknown, pointer-reference, and multi-level pointer
		// types remain rejected rather than having their indirection erased.
		bool bAllowRawHostPointers = false;
		bool bAllowRValueReferences = false;
		std::vector<std::string> KnownHostTypes;
		std::vector<std::string> KnownEnumTypes;
		std::vector<std::string> KnownTypeParameters;
		// Project aliases used when a declaration relies on an ignored #include.
		// Both ordinary and type-parameter aliases are resolved recursively.
		std::vector<FTypeAlias> KnownTypeAliases;
		// Source emission needs absolute user type names to prevent rebinding in
		// the output namespace. Catalogs retain canonical names without leading ::.
		bool bQualifyNamedTypes = false;
	};

	struct FTypeMapResult
	{
		bool bSuccess = false;
		std::string VeroType;
		std::string ErrorCode;
		std::string ErrorMessage;
	};

	/**
	 * Parse a deliberately bounded C++17 declaration/source subset. The parser is
	 * token/AST based and fails closed: unsupported constructs are diagnostics,
	 * never textually approximated. A future Clang provider can populate the same
	 * public AST without changing BindGen or Transpiler.
	 */
	FParseResult ParseSource(
		std::string_view Source,
		std::string_view SourcePath = "<cpp-source>",
		const FParseOptions& Options = {});

	FParseResult ParseFile(
		std::string_view FilePath,
		const FParseOptions& Options = {});

	FParseResult MergeTranslationUnits(
		const std::vector<FTranslationUnit>& Units);

	FParseResult ParseFiles(
		const std::vector<std::string>& FilePaths,
		const FParseOptions& Options = {});

	FTypeMapResult MapTypeToVeroScript(
		const FType& Type,
		const FTypeMapOptions& Options = {});

	// Resolve ordinary/generic project aliases while retaining use-site cv/ref
	// qualifiers. Cycles and arity mismatches remain unresolved for diagnostics.
	FType ResolveTypeAliases(
		FType Type,
		const std::vector<FTypeAlias>& Aliases);
	// Supply known records/enums and template parameters to preserve their
	// lexical precedence over aliases.
	FType ResolveTypeAliases(
		FType Type,
		const std::vector<FTypeAlias>& Aliases,
		const FTypeMapOptions& Lookup);

	/**
	 * Evaluate the C++17 integer-constant-expression subset accepted for enum
	 * initializers. Values may reference earlier enumerators by leaf, enum-qualified,
	 * or fully-qualified name. The caller remains responsible for checking the
	 * declared underlying type's range.
	 */
	bool EvaluateEnumValues(
		const FEnum& Enum,
		std::vector<std::int64_t>& OutValues,
		std::size_t& OutFailedIndex,
		std::string& OutError);

	// Representable int64 domain of a VeroScript-compatible C++ enum
	// underlying type. Full uint64 is intentionally outside the enum payload ABI.
	bool TryGetEnumValueRange(
		std::string_view UnderlyingType,
		std::int64_t& OutMinimum,
		std::int64_t& OutMaximum);

	// Canonical C++ signature identity used by project-level generators when
	// pairing declarations with definitions across translation units.
	std::string GetFunctionIdentity(const FFunction& Function);

	std::string FormatDiagnostic(const FDiagnostic& Diagnostic);
	std::string JoinTokens(const std::vector<FToken>& Tokens);
}
