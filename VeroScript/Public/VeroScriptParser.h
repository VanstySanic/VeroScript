// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptTypes.h"
#include "VeroScriptTemplate.h"
#include "VeroScriptTypeScope.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VeroScript
{
	struct FBlockStmt;
	struct FFunctionParam;
	struct FExpr;
	struct FLambdaExpr;
	struct FProgram;
	class FNamespaceLookup;
	namespace ModuleVisibility
	{
		class FImportIndex;
	}
	struct FStmt;
	struct FToken;
	struct FFieldDecl;
	struct FMethodDecl;
	struct FFunctionDecl;
	struct FConstructorDecl;
	struct FDestructorDecl;
	struct FUsingMemberDecl;
	struct FFriendDecl;
	struct FDeferredSyntax;
	struct FStructDecl;
	struct FClassDecl;
	struct FInterfaceDecl;
	struct FTypeMemberBinding;
	struct FTypeMemberLookupView;

	/**
	 * Recursive-descent parser: token stream -> FProgram AST.
	 */
	class VeroScriptParser
	{
		using FTypeNameIndex = std::unordered_map<std::string, std::vector<std::size_t>>;

	public:
		VeroScriptParser(
			const std::vector<FToken>& InTokens,
			std::string_view InFileLabel,
			const FProgram* InExternalTypeProgram = nullptr,
			std::unordered_set<std::string> InVisibleModulePaths = {});

		/**
		 * Exported type declarations collected from an implicit-import cohort
		 * before any member of that cohort is parsed.
		 */
		void SetAdditionalForwardTypeNames(
			std::unordered_set<std::string> TypeNames,
			std::unordered_set<std::string> InterfaceNames);

		std::unique_ptr<FProgram> ParseProgram();
		void SetDeclarationOutline(bool Enabled, std::string_view Source = {})
		{
			bDeclarationOutline = Enabled;
			SetSourceText(Source);
		}
		void SetSourceText(std::string_view Source, std::uint32_t Line = 1, std::uint32_t Column = 1,
			std::size_t DocumentOffset = 0);
		std::unique_ptr<FExpr> ParseExpressionFragment(std::string Namespace, const std::vector<std::string>& TypeParams,
			std::string InitializerType = {}, bool VariableInitializer = false);
		std::unique_ptr<FStmt> ParseStatementFragment(std::string Namespace, const std::vector<std::string>& TypeParams);
		// Resolve source or canonical declaration types, including reference returns in nested function types.
		std::string ParseTypeFragment(std::string Namespace, const std::vector<std::string>& TypeParams);
		// Core preparation fragment entry points. They parse normal grammar in
		// the real declaration scope and require complete token consumption.
		bool ParseMethodFragment(std::string Namespace, const std::vector<std::string>& TypeParams,
			EMemberAccess Access, FMethodDecl& Method,
			FTemplateParameterList OwnTemplateParameters = {});
		std::unique_ptr<FBlockStmt> ParseBodyFragment(std::string Namespace,
			const std::vector<std::string>& TypeParams, const std::vector<FFunctionParam>* Parameters = nullptr);
		bool ParseFunctionFragment(std::string Namespace, std::vector<std::string> TypeParams,
			FFunctionDecl& Function, FTemplateParameterList Parameters = {});
		std::unordered_set<std::string> GetUsedCompilerBuiltinTypes() const;
		void RestoreDeclarationContext(const FDeferredSyntax& Context, bool Body);
		std::unique_ptr<FProgram> TakeFragmentDeclarations();
		std::unique_ptr<FProgram> ParseTypeFragment(const FTypeDeclarationScope& Owner,
			const FDeferredSyntax& DeclarationContext);
		std::unique_ptr<FProgram> ParseLocalTypeFragment(const FDeferredSyntax& DeclarationContext,
			std::unique_ptr<FStmt>& Statement);
		bool HasError() const
		{
			return bHasError || !Diagnostics.empty();
		}
		// First / primary error (same as Diagnostics.front() when non-empty).
		const FScriptError& GetError() const
		{
			return Error;
		}
		/**
		 * Committed diagnostics (one per recovered top-level failure when collecting).
		 * HasError() is true if any committed diagnostic exists OR an active parse error.
		 */
		const std::vector<FScriptError>& GetDiagnostics() const
		{
			return Diagnostics;
		}

		void SetCollectMultipleErrors(bool bEnabled, std::size_t MaxErrors = 32);

		/**
		 * Permit standard-library implementation intrinsics whose names begin with
		 * `__`. Normal source must leave this disabled; the module loader enables it
		 * only for modules resolved from a configured standard-library root and for
		 * already-validated package template sources.
		 */
		void SetAllowInternalBuiltins(const bool bAllow)
		{
			bAllowInternalBuiltins = bAllow;
		}

		/**
		 * Extra type names treated as user types during parse (host bindings).
		 * Enables `Counter c = Counter(1);` when Counter is registered on the host.
		 */
		void SetHostTypeNames(std::unordered_set<std::string> TypeNames);

	private:
		friend class FParserLookupContext;
		struct FParsedTypeLookup;
		struct FForwardTypeLookup;

		const FParsedTypeLookup& GetParsedTypeLookup(const FProgram& Target) const;
		const FForwardTypeLookup& GetExternalForwardTypeLookup(
			const ModuleVisibility::FImportIndex* ImportIndex = nullptr) const;
		FTypeMemberLookupView GetTypeMemberLookupView() const;
		bool IsForwardTypeName(const std::string& Name) const;
		bool IsForwardInterfaceName(const std::string& Name) const;


		FSourceRange CaptureSourceRange(std::size_t Begin, std::size_t End) const;
		std::unique_ptr<FExpr> FinishExpressionRange(std::unique_ptr<FExpr> Expression, std::size_t Begin);
		std::shared_ptr<const FDeferredSyntax> CaptureDeferredSyntax(std::size_t Begin, std::size_t End) const;
		std::unique_ptr<FExpr> ParseDeferredExpression(bool StopAtColon = false);
		void InitializeFragment(std::string Namespace, const std::vector<std::string>& TypeParams);
		const FToken& Peek() const;
		const FToken& PeekNext() const;
		const FToken& Previous() const;
		bool IsAtEnd() const;
		bool Check(const ETokenKind Kind) const;
		bool CheckSymbol(const char* Lexeme) const;
		// Matches the statement-only explicit result discard prefix `(void)`.
		// This is intentionally not a general C-style cast parser.
		bool MatchExplicitDiscardPrefix();
		// Matches `(T)` at unary-expression position and leaves the cursor after
		// the closing parenthesis. Failed speculative type parses are rolled back.
		bool TryMatchCStyleCastType(std::string& OutTypeName);
		bool CheckTypeKeyword() const;
		bool CheckTypeName() const;
		bool CheckTypeNameAt(std::size_t Index) const;
		bool MatchOptionalConst();
		bool MatchConstexprSpecifiers(bool &OutConstexpr);
		bool MatchOptionalStatic();
		// `mutable` is contextual so existing identifiers with this spelling remain valid.
		bool MatchOptionalMutable();
		// `ref` keyword or unary `&` (same meaning).
		bool MatchOptionalRef();
		// C++-style trailing `Type& name` after the type (sets bIsRef).
		bool MatchOptionalTrailingRefAmpersand(bool& bInOutIsRef);
		bool LooksLikeGenericFunctionDecl() const;
		bool FindFunctionTemplateHeader(std::size_t& Opening) const;
		std::unique_ptr<FExpr> ParseCallArgument();
		// Shared lambda body after capture/`fn`/`function` and opening `(`.
		std::unique_ptr<FExpr> ParseLambdaAfterOpenParen(
			std::unique_ptr<FLambdaExpr> Lambda,
			const FSourceLocation& Location);
		bool LooksLikeContainerTypeApplication() const;
		bool TrySkipTypeNameTokens(std::size_t& Index) const;
		bool TrySkipTypeNameTokens(std::size_t& Index, int& PendingCloseCount) const;
		// `Type name(...);` is a variable direct-initializer, while
		// `Type name(...) { ... }` remains a function declaration.
		bool LooksLikeTypedVarDecl() const;
		bool LooksLikeFunctionPrototype(bool bExplicitFunctionDeclaration = false) const;
		bool LooksLikeMethodDecl() const;
		bool LooksLikeConstructorDecl(std::string_view TypeName) const;
		const FToken& Advance();
		bool Match(const ETokenKind Kind);
		bool MatchName();
		bool MatchSymbol(const char* Lexeme);
		/**
		 * Match `.` or `::` as equivalent member / namespace / type separators.
		 * Internal AST and type strings always use `.` (normalization happens here).
		 */
		bool MatchMemberAccessOp();
		// True when the next token is `.` or `::`.
		bool CheckMemberAccessOp() const;
		bool MatchCloseGeneric();
		bool TrySkipCloseGeneric(std::size_t& Index, int& PendingCloseCount) const;
		// Parse `operator` + symbol/`[]`/`()`/conversion type into OutName.
		bool MatchOperatorMethodName(std::string& OutName);

		std::string ParseTypeName();
		std::string ParseTemplateArgumentName();
		std::string ParseTemplateValueSource();
		std::string ParseExpressionTypeArgumentName(bool bAllowReference = false);
		// Parses a callable return type. Internally references are canonicalized as
		// `ref T` / `const ref T`, while source syntax remains C++-style `T&`.
		std::string ParseReturnTypeName();
		std::unique_ptr<FStmt> ParseStatement();
		std::unique_ptr<FStmt> ParseStatementCore();
		std::unique_ptr<FBlockStmt> ParseBlock();
		std::unique_ptr<FBlockStmt> ParseBlockOrStatement();
		std::unique_ptr<FStmt> ParseDoWhileStatement(const FSourceLocation& Location);
		std::vector<std::unique_ptr<FExpr>> ParseForExpressionList();
		std::unique_ptr<FStmt> ParseSwitchStatement(const FSourceLocation& Location);
		std::unique_ptr<FExpr> ParseExpression();
		std::unique_ptr<FExpr> ParseAssignment();
		std::unique_ptr<FExpr> ParseConditional();
		std::unique_ptr<FExpr> ParseLogicalOr();
		std::unique_ptr<FExpr> ParseLogicalAnd();
		std::unique_ptr<FExpr> ParseBitOr();
		std::unique_ptr<FExpr> ParseBitXor();
		std::unique_ptr<FExpr> ParseBitAnd();
		std::unique_ptr<FExpr> ParseComparison();
		std::unique_ptr<FExpr> ParseShift();
		std::unique_ptr<FExpr> ParseAdditive();
		std::unique_ptr<FExpr> ParseMultiplicative();
		std::unique_ptr<FExpr> ParseUnary();
		std::unique_ptr<FExpr> ParsePrimary();
		std::unique_ptr<FExpr> ParseCaptureLambda();
		std::unique_ptr<FExpr> FinishCall(
			std::string Callee,
			const FSourceLocation& Location,
			std::vector<std::string> TypeArgs = {},
			bool bStandardLibraryQualified = false);
		std::unique_ptr<FExpr> FinishBraceCall(
			std::string Callee,
			const FSourceLocation& Location);
		std::unique_ptr<FExpr> FinishCallExpr(
			std::unique_ptr<FExpr> CalleeExpr,
			const FSourceLocation& Location);
		bool ParseFunctionDecl(
			bool bExported = false,
			FDeclMeta&& DeclMeta = {},
			std::vector<std::string> PrefixTypeParams = {},
			std::vector<std::string> PrefixTypeParamDefaults = {},
			FTemplateParameterList PrefixTemplateParameters = {});
		bool LooksLikeOutOfClassDefinition() const;
		bool ParseOutOfClassDefinition(
			std::vector<std::string> PrefixTypeParams = {},
			std::vector<std::string> PrefixTypeParamDefaults = {},
			std::vector<FTemplateParameterList> TemplateHeaders = {});
		bool ValidateOutOfClassDefinitions();
		bool ParseStructDecl(
			bool bExported = false,
			FDeclMeta&& DeclMeta = {},
			std::vector<std::string> PrefixTypeParams = {},
			std::vector<std::string> PrefixTypeParamDefaults = {},
			EMemberAccess DefaultMemberAccess = EMemberAccess::Public,
			bool bStaticStruct = false,
			FTemplateParameterList PrefixTemplateParameters = {});
		bool ParseClassDecl(
			bool bExported = false,
			FDeclMeta&& DeclMeta = {},
			std::vector<std::string> PrefixTypeParams = {},
			std::vector<std::string> PrefixTypeParamDefaults = {},
			EMemberAccess DefaultMemberAccess = EMemberAccess::Private,
			bool bStaticClass = false,
			FTemplateParameterList PrefixTemplateParameters = {});
		bool ParseTrailingTypeDeclarators(
			const std::string& TypeName,
			bool bExported,
			bool bGenericType);
		bool ParseInterfaceDecl(
			bool bExported = false,
			FDeclMeta&& DeclMeta = {},
			std::vector<std::string> PrefixTypeParams = {},
			std::vector<std::string> PrefixTypeParamDefaults = {},
			EMemberAccess DefaultMemberAccess = EMemberAccess::Public,
			FTemplateParameterList PrefixTemplateParameters = {});
		bool ParseEnumDecl(bool bExported = false, FDeclMeta&& DeclMeta = {});
		bool ParseImportDecl(bool bReExport = false);
		bool ParseUsingDecl(bool bExported = false);
		bool ParseUsingNamespaceDecl();
		bool ParseNamespaceDecl();
		// Parses `template<T, U = int>` and validates the declaration parameters.
		bool ParseTemplatePrefix(
			std::vector<std::string>& OutTypeParams,
			std::vector<std::string>& OutTypeParamDefaults,
			FTemplateParameterList* OutParameters = nullptr);
		bool ParseRemainingTemplateHeaders(const FTemplateParameterList& Prefix,
			std::vector<FTemplateParameterList>& Headers);
		// Parses a type-parameter list after its opening `<`.
		bool ParseTypeParameterList(
			std::vector<std::string>& OutTypeParams,
			std::vector<std::string>& OutTypeParamDefaults,
			FTemplateParameterList* OutParameters = nullptr);
		bool TryParseAccessSpecifier(EMemberAccess& OutAccess);
		// Parses `public` / `protected` / `private` before one member declaration.
		// Unlike TryParseAccessSpecifier, it deliberately does not consume `public:`
		// access sections and never changes the enclosing access scope.
		bool TryParseInlineMemberAccess(EMemberAccess& OutAccess);
		bool TryParseInheritanceAccess(EMemberAccess& OutAccess);
		/**
		 * Zero or more leading `Ident(...)` / `@Ident(...)` annotations at a declaration site.
		 * Appends flattened DeclMeta entries; rolls back on a non-annotation Ident(.
		 */
		void TryParseAnnotations(FDeclMeta& OutMeta);
		bool TryParseOneAnnotation(FDeclMeta& OutMeta);
		bool TryParseAnnotationArgList(FDeclMeta& OutMeta, const std::string& KeyPrefix,
			std::vector<FAnnotationArgument>& Arguments, std::size_t Depth = 0);
		bool TryParseAnnotationValue(std::string& OutValue);
		bool LooksLikeAnnotationStart() const;
		bool LooksLikeUsingMemberDecl() const;
		bool ParseUsingMemberDecl(
			EMemberAccess SectionAccess,
			std::vector<FUsingMemberDecl>& OutUsingMembers);
		bool ParseFriendDecl(std::vector<FFriendDecl>& OutFriends);
		std::unique_ptr<FStmt> ParseLocalTypeAliasStmt();
		bool IsScopedTypeDeclaration() const;
		bool ParseScopedTypeDeclaration(EMemberAccess Access, FDeclMeta&& Meta, bool Local);
		std::unique_ptr<FStmt> ParseLocalTypeDeclaration(FDeclMeta Meta = {});
		std::unique_ptr<FBlockStmt> ParseCallableBlock(const std::string& Name,
			const std::vector<struct FFunctionParam>& Parameters, bool Const = false,
			const FTemplateParameterList* TemplateParameters = nullptr);
		void EnterTypeDeclaration(const std::string& Name, bool Exported,
			const std::vector<std::string>& Parameters, FTypeDeclarationScope& Scope,
			FTemplateParameterList& TemplateParameters);
		void LeaveTypeDeclaration();
		FTypeDeclarationScope MakeTypeScope(const std::string& Name, std::size_t ParameterCount) const;
		std::shared_ptr<FDeferredSyntax> CaptureDeclarationContext() const;
		void PushAliasScope();
		void PopAliasScope();
		bool AddAliasBinding(const std::string& Name, const std::string& Type);
		void PushRuntimeScope();
		void PopRuntimeScope();
		void AddRuntimeBinding(const std::string& Name);
		std::shared_ptr<const struct FParserBindingsSnapshot> CaptureBindings() const;
		void RestoreBindings(const FDeferredSyntax& Context);
		std::string ResolveScopedTypeApplication(std::string Name, bool bCanonical = false);
		std::string QualifyTypeDeclName(const std::string& Name) const;
		std::string QualifyDeclName(const std::string& SimpleName) const;
		std::string TypeNameLeaf(std::string_view Name) const;
		bool TryBuildQualifiedTypeName(
			std::size_t Index,
			std::string& OutTypeName,
			std::size_t& OutEndIndex) const;
		std::string ResolveNamespacedTypeName(std::string TypeName) const;
		bool NamespaceExists(const std::string& Name, const std::string& ReferenceModulePath) const;
		std::vector<std::string> ResolveUsingNamespaceTargets(
			const struct FUsingNamespaceDecl& Directive) const;
		bool ParseFieldDecl(
			std::vector<FFieldDecl>& OutFields,
			EMemberAccess Access = EMemberAccess::Public,
			FDeclMeta&& DeclMeta = {},
			bool bAllowMutable = true);
		bool ParseMethodDecl(
			std::vector<FMethodDecl>& OutMethods,
			EMemberAccess Access = EMemberAccess::Public,
			FDeclMeta&& DeclMeta = {},
			std::vector<std::string> PrefixTypeParams = {},
			std::vector<std::string> PrefixTypeParamDefaults = {},
			FTemplateParameterList PrefixTemplateParameters = {});
		// Interface methods: explicit `virtual ... = 0;` pure declaration or a body.
		bool ParseInterfaceMethodDecl(
			std::vector<FMethodDecl>& OutMethods,
			EMemberAccess Access = EMemberAccess::Public,
			FDeclMeta&& DeclMeta = {},
			std::vector<std::string> PrefixTypeParams = {},
			std::vector<std::string> PrefixTypeParamDefaults = {},
			FTemplateParameterList PrefixTemplateParameters = {});
		struct FCallableTemplateHeader
		{
			std::size_t Opening = 0, End = 0;
			int PendingCloseCount = 0;
			bool bHasSuffix = false;
		};
		bool PrepareMethodTemplate(FMethodDecl& Method, FCallableTemplateHeader& Header,
			std::vector<std::string>& Activated);
		bool ConsumeMethodTemplate(const FCallableTemplateHeader& Header);
		bool ParseTemplateMember(std::vector<FMethodDecl>& Methods, EMemberAccess Access, FDeclMeta&& Meta,
			bool bInterface);

		bool ParseConstructorDecl(
			std::string_view TypeName,
			std::vector<FConstructorDecl>& OutConstructors,
			EMemberAccess Access = EMemberAccess::Public,
			FDeclMeta&& DeclMeta = {});
		bool ClassifyConstructorFieldInitializers(
			std::vector<FConstructorDecl>& Constructors,
			const std::vector<FFieldDecl>& Fields);
		bool ParseDestructorDecl(
			std::string_view TypeName,
			std::unique_ptr<FDestructorDecl>& OutDestructor,
			EMemberAccess Access,
			FDeclMeta&& DeclMeta = {});
		/** Classifies `: A, B, C` into BaseTypeNames + InterfaceTypeNames.
		 *  When bAllowMultipleClassBases is false (struct), at most one non-interface base. */
		bool ParseInheritanceClause(
			std::vector<std::string>& OutBaseTypeNames,
			std::vector<EMemberAccess>& OutBaseInheritanceAccess,
			std::vector<std::string>& OutInterfaceTypeNames,
			bool bAllowMultipleClassBases,
			EMemberAccess DefaultBaseAccess,
			const FSourceLocation& Location);
		bool IsUserTypeName(std::string_view Name, std::size_t AliasDepth = 0) const;
		bool IsInterfaceName(std::string_view Name) const;
		bool IsStructOrClassName(std::string_view Name) const;

		bool ParseArraySuffix(std::string& InOutTypeName);
		bool ParseParamArraySuffix(std::string& InOutTypeName);
		/**
		 * Optional `= literal-expr` after a parameter name (free functions / class methods).
		 * Rejects defaults on ref params. Call ValidateTrailingDefaults after the full list.
		 */
		bool ParseOptionalParamDefault(struct FFunctionParam& Param, bool bAllowDefaults);
		// Optional `...` after a parameter type (C# params style). Sets Param.bIsVariadic.
		bool ParseOptionalParamVariadic(struct FFunctionParam& Param, bool bAllowVariadic);
		/**
		 * Optional `where T : I, ...` (generic decls). GenericKindNoun is "function" / "type"
		 * for diagnostics.
		 */
		bool ParseOptionalWhereBounds(
			const std::vector<std::string>& TypeParams,
			std::vector<struct FTypeParamBound>& OutBounds,
			const char* GenericKindNoun);
		// Optional `where T : I, ...` after parameter list (generic free functions).
		bool ParseOptionalFunctionBounds(struct FFunctionDecl& Function);
		std::unique_ptr<FExpr> ParseArrayInitializer(
			const std::string& TypeName,
			bool bExplicitTypeSyntax = false);
		std::unique_ptr<FExpr> ParseMapInitializer(
			const std::string& TypeName,
			bool bExplicitTypeSyntax = false);
		std::unique_ptr<FExpr> ParseTypedBraceInitializer(
			const std::string& TypeName,
			bool bExplicitTypeSyntax = false);
		std::unique_ptr<FExpr> ParseTypedVarInitializer(const std::string& TypeName, bool bAllowOmitEquals);
		std::string ResolveBraceInitializerTypeName(std::string TypeName) const;
		bool TypeSupportsBraceInitializer(const std::string& TypeName) const;
		bool TryGetBraceElementType(const std::string& TypeName, std::string& OutElementType) const;
		bool TryGetBraceMapValueType(const std::string& TypeName, std::string& OutValueType) const;
		bool HasDependentTypeRoot(const std::string& TypeName);
		std::unique_ptr<FExpr> ParsePostfix();
		bool TryParseQualifiedTypeMember(std::unique_ptr<FExpr>& Expression, const std::string& Member,
			const FSourceLocation& Location);
		std::string CompleteTypeName(std::string Name, std::size_t TypeBegin);

		bool EnterExpressionNesting(const FSourceLocation& Location);
		void LeaveExpressionNesting();
		bool EnterUnaryNesting(const FSourceLocation& Location);
		void LeaveUnaryNesting();
		bool EnterStatementNesting(const FSourceLocation& Location);
		void LeaveStatementNesting();
		bool ConsumeExpressionChainNode(const FSourceLocation& Location);

		void ReportError(std::string Message);
		void ReportErrorAt(const FToken& Token, std::string Message);
		void ReportErrorAt(const FSourceLocation& Location, std::string Message);
		void ReportErrorId(const FToken& Token, std::string_view MessageId);
		void ReportErrorId(const FSourceLocation& Location, std::string_view MessageId);
		void ReportErrorId(
			const FSourceLocation& Location,
			std::string_view MessageId,
			std::initializer_list<FDiagnosticArg> Args);
		void CommitCurrentError();
		void SynchronizeToNextDeclaration();
		bool RecoverAfterDeclarationError();

		bool bDeclarationOutline = false;
		bool bCanonicalTypeFragment = false;
		std::string_view SourceText;
		std::size_t SourceDocumentOffset = 0;
		std::uint32_t SourceInitialLine = 1, SourceInitialColumn = 1;
		std::vector<std::size_t> SourceLineOffsets;

		const std::vector<FToken>& Tokens;
		std::string_view FileLabel;
		const FProgram* ExternalTypeProgram = nullptr;
		// Positions survive append/reallocation; visibility and lexical scope remain
		// query-time decisions rather than cached name-resolution results.
		mutable std::shared_ptr<FParsedTypeLookup> ParsedTypeLookup;
		mutable std::shared_ptr<FNamespaceLookup> NamespaceLookup;
		mutable std::size_t NamespaceDeclarationCount = 0;
		mutable std::shared_ptr<const FParsedTypeLookup> ExternalTypeLookup;
		mutable std::shared_ptr<const FForwardTypeLookup> ExternalForwardTypeLookup;
		bool bFragmentForwardLookup = false;
		std::unordered_set<std::string> VisibleModulePaths;
		std::unordered_set<std::string> ForwardTypeNames;
		std::unordered_set<std::string> ForwardInterfaceNames;
		std::unordered_set<std::string> AdditionalForwardTypeNames;
		std::unordered_set<std::string> AdditionalForwardInterfaceNames;
		std::unordered_set<std::string> HostTypeNames;
		std::unordered_set<std::string> ActiveTypeParams;
		std::unordered_map<std::string, FTemplateParameter> ActiveValueParams;
		std::vector<std::unordered_map<std::string, std::string>> LocalTypeAliasScopes;
		std::vector<std::unordered_set<std::string>> RuntimeBindingScopes;
		struct FTypeDeclarationGuard;
		struct FTypeFrame
		{
			FTypeDeclarationScope Scope;
			std::shared_ptr<const FTypeDeclarationScope> Snapshot;
			std::string Name;
			std::string Namespace;
			std::string DeclarationPrefix;
			std::vector<std::string> Parameters;
			FTemplateParameterList TemplateParameters;
			const FStructDecl* OpenStruct = nullptr;
			const FClassDecl* OpenClass = nullptr;
			const FInterfaceDecl* OpenInterface = nullptr;
			std::size_t AliasBoundary = 0;
			bool Exported = false;
			bool Local = false;
		};
		std::vector<FTypeFrame> TypeFrames;
		FTypeMemberBinding FindScopedTypeMember(const std::string& Owner,
			const std::vector<std::string>& Arguments, const std::string& Member) const;
		mutable std::shared_ptr<class FParserBindingStore> BindingStore;
		std::vector<std::string> BlockScopes;
		std::string FragmentScopeId;
		std::string RestoredBlockScope;
		std::string FunctionOwner;
		std::vector<std::string> FunctionTypeParameters;
		FTemplateParameterList CallableTemplateParameters;
		std::unordered_map<std::string, std::vector<const struct FGeneratedBlockTypeBinding*>> GeneratedBlockTypeIndex;
		bool bGeneratedBlockIndexBuilt = false;
		const std::vector<const struct FGeneratedBlockTypeBinding*>& GetGeneratedBlockBindings(const std::string& Scope);
		std::string LocalDeclarationPrefix;
		std::optional<FTypeDeclarationScope> FragmentOwnerScope;
		std::size_t FragmentOwnerDepth = 0;
		EMemberAccess DeclarationAccess = EMemberAccess::Public;
		// Current `namespace A.B` path (dot-separated); empty at file scope.
		std::string ActiveNamespacePrefix;
		std::size_t Current = 0;
		int PendingGenericCloseCount = 0;
		int TypeNameParseDepth = 0;
		int ExpressionTypeArgumentParseDepth = 0;
		int ExpressionParseDepth = 0;
		int UnaryParseDepth = 0;
		int StatementParseDepth = 0;
		int NamespaceParseDepth = 0;
		int InitializerParseDepth = 0;
		std::size_t ExpressionChainNodeCount = 0;
		bool bTypeNameDepthExceeded = false;
		bool bAllowInternalBuiltins = false;
		bool bHasError = false;
		bool bCollectMultipleErrors = true;
		std::size_t MaxCollectedErrors = 32;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;
		std::unique_ptr<FProgram> Program;
	};
}
