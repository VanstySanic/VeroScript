// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptAnnotationProcessing.h"

#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptTypes.h"
#include "VeroScriptToolingTypes.h"
#include "VeroScriptTypeScope.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	struct FProgram;
}

namespace VeroScript::Tooling::Detail
{
	// Host enum members have their declaring enum type and an integer value,
	// rather than the unused native return type in the binding descriptor.
	std::string FormatHostMemberDetail(const FHostMemberDesc& Member, std::string_view OwnerType);

	/**
	 * Document outline / symbol tree kinds for tooling consumers.
	 */
	enum class EDocumentSymbolKind : std::uint8_t
	{
		Import = 0,
		Function,
		Method,
		Constructor,
		Struct,
		Class,
		Interface,
		Enum,
		EnumMember,
		Field,
		Variable,
		Constant,
		// `using` / `typedef` alias (Detail = target type).
		TypeAlias,
		// `name:` label (goto target).
		Label,
		// `~Type()` destructor (appended to preserve prior kind numeric values).
		Destructor,
		// Language keyword (`if` / `return` / `class` / …).
		Keyword,
		// Host declaration annotation name (`UFUNCTION`, `@rpc`).
		Annotation,
		// Source or host-predefined preprocessor macro.
		Macro,
		// Named argument/specifier inside a host declaration annotation.
		AnnotationArgument,
		// A user, imported, or standard-library namespace. Kept distinct from a
		// module import so LSP clients can classify namespace path segments exactly.
		Namespace,
	};

	/**
	 * One outline node. Children hold members/methods; End may equal Start (point range).
	 */
	struct FDocumentSymbol
	{
		EDocumentSymbolKind Kind = EDocumentSymbolKind::Function;
		std::string Name;
		std::string Detail;
		FTypeDeclarationScope TypeScope;
		std::string OutlineName, CallableOwner;
		// Host/catalog documentation for soft symbols.
		std::string Documentation;
		std::string FileLabel;
		std::uint32_t StartLine = 1;
		std::uint32_t StartColumn = 1;
		std::uint32_t EndLine = 1;
		std::uint32_t EndColumn = 1;
		bool bExported = false;
		// Soft host/compiler builtin with no source location (not renameable).
		bool bSoftBuiltin = false;
		// Synthetic delegate family spanning the supported generic arities.
		// Real declarations and standard aliases copied from them keep this false.
		bool bArityOverloadedEventType = false;
		/**
		 * True for `enum class` (scoped) types. Lookup/completion must not treat
		 * members as bare identifiers; Detail may also carry DeclMeta text.
		 */
		bool bScopedEnum = false;
		// Class/struct/interface member access; ignored for non-members.
		EMemberAccess Access = EMemberAccess::Public;
		std::vector<FDocumentSymbol> Children;
		// Field binding is immutable; independent of its property classification.
		bool bReadOnly = false;
		FSourceImplementationLocation SourceImplementation;
		FAnnotationSourcePosition GeneratedOrigin;
		bool bHasSourceDocumentation = false;
		// Complete declaration extent, separate from the historical name anchor.
		// A zero start line means that the parser supplied no complete range.
		FTextRange DeclarationRange{0, 0, 0, 0};
		// Keep member dispatch semantics independent of formatted signature prefixes.
		bool bIsStatic = false;
	};

	struct FDocumentSymbolOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<std::string_view> StdlibSearchPaths;
		std::vector<FSourceOverlay> SourceOverlays;
		// Include import declarations for the entry document.
		bool bIncludeImports = true;
		/**
		 * Also emit exported symbols from imported modules that are visible
		 * from the entry document (direct import + export-import closure).
		 * Outline leaves this false so the tree stays entry-document scoped.
		 */
		bool bIncludeImportedExports = false;
		/**
		 * Emit soft compiler builtins (pair / handle / delegate / multicast_delegate,
		 * host free functions, concurrent.vs wrappers) when the user did not declare
		 * an arity-matching type. Outline leaves this false; query APIs enable it.
		 */
		bool bIncludeSoftBuiltins = false;
		/**
		 * Emit In-catalog host free functions and types (soft, no source range).
		 * Workspace symbol search enables this; document outline leaves it false.
		 */
		bool bIncludeHostBindings = false;
		// When set, host type names are visible to the module loader / parser.
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FDocumentSymbolResult
	{
		bool bSuccess = false;
		FScriptError Error;
		// All loader/parser diagnostics when load failed (Error is the primary).
		std::vector<FScriptError> Diagnostics;
		std::vector<FDocumentSymbol> Symbols;
		FPreprocessLineMap PreprocessLineMap;
	};

	/**
	 * Collect document symbols from a parsed entry module (import merge filtered out).
	 * Walks AST only; does not require a successful compile.
	 */
	class VeroScriptDocumentSymbols
	{
	public:
		// Walk an already-loaded program (avoids a second module load).
		static FDocumentSymbolResult CollectFromProgram(
			const FProgram& Program,
			const FDocumentSymbolOptions& Options = {});

		static FDocumentSymbolResult CollectFromSource(
			std::string_view Source,
			std::string_view FileLabel = "<source>",
			const FDocumentSymbolOptions& Options = {});

		static FDocumentSymbolResult CollectFromFile(
			std::string_view FilePath,
			const FDocumentSymbolOptions& Options = {});
	};
}
