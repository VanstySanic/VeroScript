// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptAnnotationProcessing.h"

#include "VeroScriptMemory.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptPreprocessor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	struct FDebugVariable;
	class FModuleEnvironment;
}

namespace VeroScript::Tooling
{
	namespace Detail
	{
		struct FSemanticQuerySnapshot;
	}

	/**
	 * Diagnostic severity for editor / language-server consumers.
	 */
	enum class EDiagnosticSeverity : std::uint8_t
	{
		Error = 0,
		Warning,
		Information,
		Hint,
	};

	/**
	 * Outline symbol kinds (mirrors document-symbol query surface).
	 */
	enum class EOutlineSymbolKind : std::uint8_t
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
		// A user, imported, or standard-library namespace.
		Namespace,
	};

	enum class EDebugRunMode : std::uint8_t
	{
		Continue = 0,
		StepIn,
		StepOver,
		StepOut,
	};

	enum class EDebugStopReason : std::uint8_t
	{
		Breakpoint = 0,
		Step,
		Entry,
		Pause,
	};

	enum class EDocumentHighlightKind : std::uint8_t
	{
		Text = 0,
		Read,
		Write,
	};

	/**
	 * 1-based inclusive start; End may equal Start when the engine only provides a point.
	 */
	struct FTextRange
	{
		std::uint32_t StartLine = 1;
		std::uint32_t StartColumn = 1;
		std::uint32_t EndLine = 1;
		std::uint32_t EndColumn = 1;
	};

	// Owning physical source position of a separately supplied callable body.
	// An empty FileLabel means that no separate/generated body was bound.
	struct FSourceImplementationLocation
	{
		std::string FileLabel;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		// Physical annotation source, when this body belongs to a generated document.
		FAnnotationSourcePosition GeneratedOrigin;
	};

	/**
	 * Structured diagnostic independent of CLI string formatting.
	 */
	struct FDiagnostic
	{
		EDiagnosticSeverity Severity = EDiagnosticSeverity::Error;
		std::string Message;
		std::string Code;
		// Message template args when Code is a catalog MessageId (may be empty).
		std::vector<std::string> MessageArgs;
		std::string FileLabel;
		FTextRange Range;
		// Set when Range already refers to the original source.
		bool bPhysicalLocation = false;
	};

	/**
	 * Open-buffer contents that override disk during import resolution.
	 */
	struct FDocumentOverlay
	{
		std::string Path;
		std::string Source;
	};

	/**
	 * Options for AnalyzeSource / AnalyzeFile.
	 */
	struct FAnalyzeOptions
	{
		std::vector<std::string_view> ImportPaths;
		// `-D` / `#define` initial macros (`NAME` or `NAME=value`).
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		// Optional host bindings (RegisterNative / host types) for analysis. Not owned.
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		// Standard-library roots are semantically distinct from ordinary imports.
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	/**
	 * Analysis outcome. bSuccess means compilation succeeded; warnings may still be present.
	 * Failure always yields at least one diagnostic (may be several after parse recovery).
	 */
	struct FAnalyzeResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		// Read-only virtual documents used by generated-member navigation.
		std::vector<VeroScript::FGeneratedDeclarationDocument> GeneratedDocuments;
	};

	struct FWorkspaceOptions
	{
		std::vector<std::string> ImportPaths;
		// Additional stdlib roots; tooling resolves these as highest-priority import roots.
		std::vector<std::string> StdlibSearchPaths;
		// `-D` / `#define` initial macros (`NAME` or `NAME=value`).
		std::vector<std::string> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		// Optional host bindings shared by Analyze / Complete / … Not owned.
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		// Closed-workspace index resource limits.  These are not import limits and
		// apply only to tooling scans below WorkspaceRoots.
		std::size_t MaxWorkspaceFiles = 10000;
		std::size_t MaxWorkspaceFileBytes = 2u * 1024u * 1024u;
		// Explicit workspace roots used for closed-file indexing.  Open editor
		// buffers are always represented by SourceOverlays and take precedence.
		// Appended to preserve positional aggregate initialization of old callers.
		std::vector<std::string> WorkspaceRoots;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FOutlineSymbol
	{
		EOutlineSymbolKind Kind = EOutlineSymbolKind::Function;
		std::string Name;
		std::string Detail;
		std::string FileLabel;
		FTextRange Range;
		bool bExported = false;
		std::vector<FOutlineSymbol> Children;
		FSourceImplementationLocation SourceImplementation;
		FAnnotationSourcePosition GeneratedOrigin;
		// Complete physical declaration extent; Range retains its name-anchor semantics.
		// Appended for existing positional aggregate callers. Zero means unavailable.
		FTextRange DeclarationRange{0, 0, 0, 0};
	};

	struct FOutlineOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		bool bIncludeImports = true;
		// Soft host builtins (`print`, `pair<T,U>`, …). Off for document outline by default.
		bool bIncludeSoftBuiltins = false;
		/**
		 * Emit catalog host free functions / types as soft symbols (no source range).
		 * Off for document outline; workspace symbol search enables it.
		 */
		bool bIncludeHostBindings = false;
		// Host type names for parse + optional catalog symbols when bIncludeHostBindings.
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		// Used by implementation lookup to retain bodies bound to imported declarations.
		bool bIncludeImportedExports = false;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FOutlineResult
	{
		bool bSuccess = false;
		std::vector<FOutlineSymbol> Symbols;
		std::vector<FDiagnostic> Diagnostics;
	};

	namespace SemanticTokenModifiers
	{
		// Bit positions match the LSP semantic token modifier legend.
		inline constexpr std::uint32_t None = 0;
		inline constexpr std::uint32_t Readonly = 1u << 0;
	}

	/**
	 * Semantic classification for one physical-source identifier.
	 * Kind and modifiers are resolved from declarations, never casing or detail text.
	 */
	struct FSemanticToken
	{
		EOutlineSymbolKind Kind = EOutlineSymbolKind::Variable;
		FTextRange Range;
		std::uint32_t Modifiers = SemanticTokenModifiers::None;
	};

	struct FSemanticTokensResult
	{
		bool bSuccess = false;
		std::vector<FSemanticToken> Tokens;
		std::vector<FDiagnostic> Diagnostics;
		// Cancelled analysis returns no tokens; never publish/cache it as complete.
		bool bCancelled = false;
		std::vector<std::string> Dependencies;
		/**
		 * Opaque state for internal tooling reuse, not a public cache/reuse API.
		 * Owns the parsed program and its source storage. Internal reuse validates
		 * document/options/dependency revisions before each interactive query.
		 */
		std::shared_ptr<const Detail::FSemanticQuerySnapshot> Snapshot;
	};

	/**
	 * Callbacks are borrowed for one synchronous invocation and run on its analysis
	 * thread. Captures must remain valid until it returns. The workspace retains
	 * neither callback. Cancellation returns no tokens; observer exceptions are ignored.
	 */
	struct FSemanticTokensOptions
	{
		// True stops analysis.
		std::function<bool()> ShouldCancel;
		// Stage durations are elapsed milliseconds.
		std::function<void(std::string_view, double)> RecordStage;
	};

	/**

	 * 1-based caret position.

	 */
	struct FTextPosition
	{
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
	};

	struct FNavigationHit
	{
		EOutlineSymbolKind Kind = EOutlineSymbolKind::Function;
		std::string Name;
		std::string Detail;
		std::string ContainerName;
		std::string FileLabel;
		FTextRange DefinitionRange;
		bool bExported = false;
		FSourceImplementationLocation SourceImplementation;
		FAnnotationSourcePosition GeneratedOrigin;
	};

	struct FNavigationOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FNavigationResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		bool bHasToken = false;
		std::string TokenText;
		FTextRange TokenRange;
		std::vector<FNavigationHit> Definitions;
		// Cooperative workspace implementation lookup was cancelled. Appended to
		// preserve aggregate source compatibility for existing navigation callers.
		bool bCancelled = false;
	};

	struct FHoverHit
	{
		EOutlineSymbolKind Kind = EOutlineSymbolKind::Function;
		std::string Name;
		std::string Detail;
		// Contiguous line/block comment immediately above the declaration.
		std::string Documentation;
		std::string ContainerName;
		std::string FileLabel;
		FTextRange DefinitionRange;
		bool bExported = false;
	};

	struct FHoverOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FHoverResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		bool bHasToken = false;
		std::string TokenText;
		FTextRange TokenRange;
		std::vector<FHoverHit> Definitions;
		// Display text built from Definitions (kind, name, detail, documentation).
		std::string Contents;
	};

	struct FDebugStackFrame
	{
		std::string FunctionName;
		std::string FileLabel;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
	};

	struct FDebugVariable
	{
		std::string Name;
		std::string Value;
		FMemoryInspection Memory;
		FValue RawValue = std::int32_t{0};
		bool bHasRawValue = false;
		std::shared_ptr<::VeroScript::FDebugVariable> EngineVariable;
		// Preserve the core debugger's immutable inspection status through adapters.
		bool bReadOnly = false;
	};

	struct FDebugEvaluateResult
	{
		bool bOk = false;
		std::string Value;
		std::string Error;
		FValue RawValue = std::int32_t{0};
		bool bHasRawValue = false;
	};

	struct FDebugLoadOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		// Optional host bindings for compile/run. Not owned.
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
		std::shared_ptr<const VeroScript::IVmBackendFactory> VmBackendFactory;
		std::string VmBackendPath;
	};

	struct FDebugLoadResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
	};

	struct FDebugRunResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		std::string Output;
		// Appended to preserve positional aggregate initialization of older callers.
		// A successful run can still deliberately return a non-zero process result.
		int ExitCode = 1;
		// Captured stderr.  Kept separate so DAP can preserve the output category.
		// Appended to preserve positional aggregate initialization of older callers.
		std::string ErrorOutput;
	};

	struct FCompletionItem
	{
		EOutlineSymbolKind Kind = EOutlineSymbolKind::Function;
		std::string Label;
		std::string Detail;
		std::string InsertText;
		bool bSoftBuiltin = false;
		// Secondary contextual rank after MatchQuality; lower sorts earlier.
		std::uint16_t SortPriority = 500;
		// True when InsertText uses the LSP snippet grammar (`${1}` etc.).
		// Appended to preserve positional aggregate initialization of older callers.
		bool bInsertTextSnippet = false;
		// Primary textual match tier; lower sorts earlier than SortPriority.
		// Appended to preserve positional aggregate initialization of older callers.
		std::uint8_t MatchQuality = 6;
		// Optional qualified namespace for LSP labelDetails.description.
		// Appended to preserve aggregate initialization of existing callers.
		std::string LabelDescription;
		// Leading source documentation, when available.
		std::string Documentation;
		// Generated type-body implementation; also offered as a code action.
		bool bOverrideMember = false;
		// True when this generated method satisfies a pure interface requirement.
		bool bImplementsInterfaceMember = false;
		// Optional client matching text; empty uses Label. Appended for aggregates.
		std::string FilterText;
	};

	struct FCompletionOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		// Optional host bindings for identifier completion. Not owned.
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FCompletionResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		std::string Prefix;
		FTextRange ReplaceRange;
		bool bHasReplaceRange = false;
		std::vector<FCompletionItem> Items;
	};

	struct FSignatureParameter
	{
		std::string Label;
	};

	struct FSignatureInformation
	{
		std::string Label;
		std::vector<FSignatureParameter> Parameters;
		// Optional plain-text documentation, appended for aggregate compatibility.
		std::string Documentation;
	};

	struct FSignatureHelpOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FSignatureHelpResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		bool bActive = false;
		std::uint32_t ActiveSignature = 0;
		std::uint32_t ActiveParameter = 0;
		std::vector<FSignatureInformation> Signatures;
	};

	struct FReferenceHit
	{
		std::string FileLabel;
		FTextRange Range;
		bool bIsDefinition = false;
		// Assignment / compound / ++/-- target (when detectable lexically).
		bool bIsWrite = false;
	};

	struct FReferencesOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FReferencesResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		bool bHasToken = false;
		std::string TokenText;
		std::vector<FReferenceHit> References;
	};

	struct FDocumentHighlight
	{
		FTextRange Range;
		EDocumentHighlightKind Kind = EDocumentHighlightKind::Text;
	};

	struct FDocumentHighlightOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FDocumentHighlightResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		bool bHasToken = false;
		std::string TokenText;
		std::vector<FDocumentHighlight> Highlights;
	};

	struct FWorkspaceSymbol
	{
		EOutlineSymbolKind Kind = EOutlineSymbolKind::Function;
		std::string Name;
		std::string Detail;
		std::string ContainerName;
		std::string FileLabel;
		FTextRange Range;
		bool bExported = false;
		FSourceImplementationLocation SourceImplementation;
		FAnnotationSourcePosition GeneratedOrigin;
	};

	struct FWorkspaceSymbolsOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		bool bIncludeImports = true;
		// Include soft host builtins in workspace symbol search.
		bool bIncludeSoftBuiltins = true;
		// Include In-catalog host functions/types when Bindings is set.
		bool bIncludeHostBindings = true;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		// Defensive limits for servers indexing untrusted or very large workspaces.
		std::size_t MaxWorkspaceFiles = 10000;
		std::size_t MaxWorkspaceFileBytes = 2u * 1024u * 1024u;
		// Optional cooperative cancellation probe for long closed-workspace scans.
		// The callback is never retained beyond the query invocation.
		std::function<bool()> ShouldCancel;
		// Closed .vs documents are discovered below these roots.  Roots are kept
		// separate from import paths: a workspace can index several projects without
		// making all of their source directories implicit imports. Appended for
		// positional aggregate compatibility.
		std::vector<std::string_view> WorkspaceRoots;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		bool bIncludeImportedExports = false;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FWorkspaceSymbolsResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		std::vector<FWorkspaceSymbol> Symbols;
		// Appended for source compatibility.  A cancelled result is intentionally
		// partial/empty and must not be cached or published as a fresh index.
		bool bCancelled = false;
		// Dynamic processors can read resources outside the document event stream.
		// A default workspace index must rebuild these results on the next request.
		bool bCacheable = true;
	};

	/**

	 * One text replacement (1-based range, exclusive EndColumn on same line for rename).

	 */
	struct FTextEdit
	{
		std::string FileLabel;
		FTextRange Range;
		std::string NewText;
	};

	struct FRenameOptions
	{
		std::vector<std::string_view> ImportPaths;
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<FDocumentOverlay> SourceOverlays;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::vector<std::string_view> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string_view> StdlibSearchPaths;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
	};

	struct FRenameResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		// False when the name is ambiguous / invalid / not a renameable symbol.
		bool bCanRename = false;
		std::string Reason;
		std::string OldName;
		std::vector<FTextEdit> Edits;
	};

	struct FFormatOptions
	{
		// Visual columns per indent level when inserting spaces.
		std::uint32_t IndentWidth = 4;
		bool bInsertSpaces = true;
		/**
		 * `-D` macros for callers that format then re-analyze.
		 * The formatter itself is lexical (does not expand macros).
		 */
		std::vector<std::string_view> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
	};

	struct FFormatResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
		std::string FormattedSource;
	};

	/**
	 * Formatting result for a selection. Range is expanded to complete physical
	 * lines when needed: formatting a fragment of a token or a line would either
	 * produce invalid source or silently alter text outside the returned edit.
	 */
	struct FRangeFormatResult : FFormatResult
	{
		FTextRange FormattedRange;
		bool bHasFormattedRange = false;
	};
}
