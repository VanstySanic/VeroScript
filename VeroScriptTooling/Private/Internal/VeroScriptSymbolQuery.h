// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptAnnotationProcessing.h"

#include "VeroScriptDiagnostics.h"
#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptPreprocessor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	struct FSemanticQuerySnapshot;
	bool IsSemanticQuerySnapshotCurrent(const std::shared_ptr<const FSemanticQuerySnapshot>& Snapshot);
	bool IsSemanticQuerySnapshotCacheable(const std::shared_ptr<const FSemanticQuerySnapshot>& Snapshot);
	/**
	 * 1-based caret position (matches FSourceLocation).
	 */
	struct FTextPosition
	{
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
	};

	/**
	 * One definition candidate for a name under the caret.
	 */
	struct FSymbolQueryHit
	{
		EDocumentSymbolKind Kind = EDocumentSymbolKind::Function;
		std::string Name;
		std::string Detail;
		// Host/catalog documentation. Source declarations are populated from comments.
		std::string Documentation;
		std::string ContainerName;
		std::string FileLabel;
		std::uint32_t DefLine = 1;
		std::uint32_t DefColumn = 1;
		// Definition coordinates have already been mapped to the original source.
		bool bPhysicalLocation = false;
		bool bExported = false;
		// Soft host/compiler builtin — hover/CallHelp only; no rename / no Go-to.
		bool bSoftBuiltin = false;
		// Completion-only lexical distance: 0 is the innermost active scope.
		// 255 means this hit was not collected for completion ranking.
		std::uint8_t CompletionScopeDistance = 255;
		bool bReadOnly = false;
		bool bLocalStatic = false;
		bool bTypeDefinition = false;
		FSourceImplementationLocation SourceImplementation;
		FAnnotationSourcePosition GeneratedOrigin;
		bool bHasSourceDocumentation = false;
		// Owned coordinates for resolving an auto local while its program is alive.
		FSourceRange InitializerRange;
		// The parsed declaration header bounds the local's name token. Statement
		// Location may refer to `const`/`static`/the type rather than that name.
		FSourceRange DeclarationRange;
	};

	struct FSymbolQueryOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FSourceOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FSymbolQueryResult
	{
		bool bSuccess = false;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;
		// True when an identifier / operator token covers the caret.
		bool bHasToken = false;
		// Resolved from a callable/block scope, rather than a module or type.
		bool bLocalSymbol = false;
		std::string TokenText;
		std::uint32_t TokenLine = 1;
		std::uint32_t TokenColumn = 1;
		std::uint32_t TokenEndColumn = 1;
		// Zero or more definitions matching TokenText (simple name lookup).
		std::vector<FSymbolQueryHit> Definitions;
	};

	struct FSemanticSymbolToken
	{
		EDocumentSymbolKind Kind = EDocumentSymbolKind::Variable;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		std::uint32_t EndColumn = 1;
		// SemanticTokenModifiers bit mask, preserved through workspace/LSP encoding.
		std::uint32_t Modifiers = 0;
	};

	struct FSemanticSymbolResult
	{
		bool bSuccess = false;
		FScriptError Error;
		std::vector<FScriptError> Diagnostics;
		std::vector<FSemanticSymbolToken> Tokens;
		std::vector<std::string> Dependencies;
		std::shared_ptr<const FSemanticQuerySnapshot> Snapshot;
	};

	/**
	 * Exact enclosing callable identity used by friend-aware completion/help.
	 */
	struct FCallableAccessContext
	{
		std::string Name;
		std::string OwnerTypeName;
		std::string ReturnType;
		// CallableParameterTypeName spellings, before resolving aliases.
		std::vector<std::string> ParameterTypes;
		bool bIsConstMethod = false;
		// Declaration identity is independent of an out-of-file definition body.
		// Empty module paths preserve the core helper's unknown-context fallback.
		std::string ModulePath;
		bool bExported = false;
	};

	// Keeps the immutable snapshot alive for synchronous queries on this thread.
	class FSemanticQueryScope
	{
	public:
		explicit FSemanticQueryScope(std::shared_ptr<const FSemanticQuerySnapshot> Snapshot);
		~FSemanticQueryScope();
		FSemanticQueryScope(const FSemanticQueryScope&) = delete;
		FSemanticQueryScope& operator=(const FSemanticQueryScope&) = delete;

	private:
		std::shared_ptr<const FSemanticQuerySnapshot> Snapshot;
		const FSemanticQuerySnapshot* Previous;
	};

	/**
	 * Extract a type name from a symbol Detail (`const multicast_delegate<map<K, V>>`,
	 * `T (int)`, `ref array<int>`). Respects nested `<…>` depth.
	 * Strips DeclMeta display brackets (`[UCLASS()]`, trailing `[UPROPERTY(...)]`).
	 */
	std::string TypeNameFromDetail(std::string_view Detail);

	/**
	 * Remove leading/trailing DeclMeta display segments (`[Name(...)]`) from Detail
	 * so signature/type parsers see only the language type/signature text.
	 */
	std::string_view StripDeclMetaDisplay(std::string_view Detail);

	/**
	 * Position-based symbol query for hover / go-to-definition data.
	 * Identifier token hit, then:
	 * 1) parameters / locals in the enclosing function or method (shadow document symbols)
	 * 2) entry-module declarations (including nested fields/methods/enum members by simple name)
	 */
	class VeroScriptSymbolQuery
	{
	public:
		static FSymbolQueryResult QueryAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FSymbolQueryOptions& Options = {},
			// Optional exact, reusable program for a request-local batch. Recovery
			// and processor-driven queries leave it empty.
			std::shared_ptr<const FSemanticQuerySnapshot>* OutSnapshot = nullptr);

		static FSymbolQueryResult QueryAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FSymbolQueryOptions& Options = {});


		/** Query an already-loaded program with the caller's physical line map. */
		static FSymbolQueryResult QueryInProgram(
			const FProgram& Program, const FDocumentSymbolResult& Symbols,
			std::string_view Source, std::string_view FileLabel, FTextPosition Position,
			const IHostBindingCatalog* Bindings, const FPreprocessLineMap& LineMap);

		/** Conservative type inference for complete value receiver expressions. */
		static std::string ResolveMemberReceiverTypeInProgram(
			const FProgram& Program, const FDocumentSymbolResult& Symbols,
			std::string_view Source, std::string_view FileLabel,
			FTextPosition PhysicalPosition, FTextPosition AstPosition, const IHostBindingCatalog* Bindings);

		/**
		 * Classify every resolvable identifier from one shared parsed program.
		 * This is the semantic-token path; it intentionally does not infer from casing.
		 */
		static FSemanticSymbolResult SemanticTokensAtSource(
			std::string_view Source,
			std::string_view FileLabel = "<source>",
			const FSymbolQueryOptions& Options = {});

		/**
		 * Collect locals / parameters visible at Position from an already-loaded program
		 * (avoids a second module load — used by SignatureHelp after soft recovery).
		 */
		static void CollectVisibleLocalsAt(
			const FProgram& Program,
			FTextPosition Position,
			std::vector<FSymbolQueryHit>& OutHits);

		static void CollectVisibleLocalsAt(
			const FProgram& Program,
			std::string_view Source,
			const std::vector<std::string_view>& Defines,
			const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
			FTextPosition PhysicalPosition,
			std::vector<FSymbolQueryHit>& OutHits);

		/**
		 * Resolve a receiver expression's type at Position (preprocessed / AST line space).
		 */
		static std::string ResolveReceiverTypeInProgram(
			const FProgram& Program,
			const FDocumentSymbolResult& SymbolsResult,
			FTextPosition Position,
			std::string_view ReceiverName);

		// Resolve receiver type from physical caret coordinates using the full macro environment.
		static std::string ResolveReceiverTypeAtPhysical(
			const FProgram& Program,
			const FDocumentSymbolResult& SymbolsResult,
			std::string_view Source,
			const std::vector<std::string_view>& Defines,
			const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
			FTextPosition PhysicalPosition,
			std::string_view ReceiverName);

		/**
		 * Declaring type of the enclosing method/constructor/destructor at Position
		 * (AST line space). Empty outside type member bodies.
		 */
		static std::string ResolveEnclosingTypeName(
			const FProgram& Program,
			FTextPosition Position);

		// Out-of-namespace definitions retain the declaration's lookup scope.
		static std::string ResolveEnclosingNamespaceName(
			const FProgram& Program,
			FTextPosition Position,
			std::string_view LexicalNamespace);

		static bool ResolveEnclosingCallableAccessContext(
			const FProgram& Program,
			FTextPosition Position,
			FCallableAccessContext& OutContext);

		// Enclosing instance owner, empty for free/static functions.
		static std::string ResolveImplicitThisTypeName(
			const FProgram& Program,
			FTextPosition Position);
	};
}
