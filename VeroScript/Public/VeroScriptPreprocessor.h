// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptMacro.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VeroScript
{
	enum class EPreprocessMode
	{
		Standalone,
		Module
	};

	// A resumable file/expansion state. Module loading drives this on a heap stack;
	// no dependency is loaded from inside a recursive macro expansion.
	enum class EPreprocessStep
	{
		Complete,
		NeedModule,
		Error
	};

	/**
	 * Function-like macro supplied by an embedding host.
	 *
	 * Predefined macros seed every translation unit independently. Source
	 * `#undef` may remove one for the rest of that unit; redefining one without
	 * first undefining it is rejected.
	 */
	struct FPredefinedMacro
	{
		std::string Name;
		std::vector<std::string> Parameters;
		bool bVariadic = false;
		std::string Body;
		// Used by diagnostics/tooling only; empty is displayed as "<host>".
		std::string SourceName = "<host>";
	};

	/**
	 * Optional side channel from PreprocessSource for IDE / tooling.
	 * Physical lines are 1-based (index 0 unused).
	 */
	struct FPreprocessMacroDef
	{
		std::string Name;
		// Replacement list (after the parameter list for function-like macros).
		std::string Body;
		bool bFunctionLike = false;
		// Parameter list ends with `...` (replacement may use `__VA_ARGS__`).
		bool bVariadic = false;
		/**
		 * For `#define` events: true. For `#undef` timeline tombstones: false.
		 * Hover/Complete/Goto resolve the last event at or before the caret line
		 * whose name matches; success only when that event is a `#define`.
		 */
		bool bActive = true;
		// True for a core builtin or host-seeded macro rather than a source declaration.
		bool bPredefined = false;
		std::string SourceName;
		std::vector<std::string> Parameters;
		// Physical line/column of the macro name (1-based).
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		// Inclusive physical span of the whole `#define` directive (handles `\` joins).
		std::uint32_t DirectiveStartLine = 1;
		std::uint32_t DirectiveEndLine = 1;
		std::size_t ActivationOffset = 0;
		std::uint32_t ActivationLine = 0;
		std::uint32_t ActivationColumn = 0;
		std::uint64_t EventSequence = 0;
		bool bImported = false;
		bool bPublic = false;
	};

	struct FPreprocessSourcePos
	{
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
	};

	struct FPreprocessLineMap
	{
		/**
		 * True when the physical line is active non-directive code
		 * (false for inactive `#if` bodies and for directive lines themselves).
		 */
		std::vector<bool> PhysicalLineActiveCode;
		/**
		 * Timeline of active-branch `#define` / `#undef` events (object- and function-like).
		 * `#define` pushes `bActive == true`; `#undef` pushes a tombstone (`bActive == false`)
		 * so earlier definition sites remain discoverable for references while lookup at a
		 * physical line uses the last event at or before that line.
		 */
		std::vector<FPreprocessMacroDef> ActiveMacros;
		/**
		 * Parallel to the preprocessed output: each byte maps to a physical source
		 * position (1-based). Empty when mapping was not requested / failed mid-way.
		 * Macro expansions map every expanded byte to the macro name's first character.
		 */
		std::vector<FPreprocessSourcePos> OutByteToPhysical;
		// Preprocessed text corresponding to OutByteToPhysical (same size).
		std::string PreprocessedSource;
	};

	struct FPreprocessImport
	{
		std::string Path;
		std::string ResolvedPath;
		bool bReExport = false;
		FPreprocessSourcePos Position;
		FPreprocessSourcePos ActivationPosition;
		std::size_t ActivationOffset = 0;
	};

	struct FPreprocessResult
	{
		std::vector<FPublicMacro> PublicMacros;
		std::vector<FPreprocessImport> Imports;
		bool bModuleImportsResolved = true;
		bool bContainsExportDirective = false;
		bool bComplete = false;
		FScriptError Diagnostic;
	};

	// Share one work budget across the files in a compilation request. Output is
	// bounded separately for each file, including intermediate replacement text.
	struct FPreprocessBudget
	{
		std::size_t MaxOutputBytes = 32u * 1024u * 1024u;
		std::uint64_t MaxWork = 256u * 1024u * 1024u;
		std::uint64_t Work = 0;
	};

	struct FPreprocessContext
	{
		// Stable logical module path used by the dynamic __FILE__ macro.
		// Empty produces the deterministic literal "<memory>".
		std::string LogicalPath;
		EPreprocessMode Mode = EPreprocessMode::Standalone;
		std::function<bool(FPreprocessImport &, std::vector<FPublicMacro> &, std::string &)>
			ResolveModule;
		FPreprocessResult *Result = nullptr;
		// Maps dynamic macro and emitted positions after a host source conversion.
		std::function<FPreprocessSourcePos(FPreprocessSourcePos)> MapSourcePosition;
		std::shared_ptr<FPreprocessBudget> Budget;
	};

	class FPreprocessorSession
	{
	public:
		FPreprocessorSession(std::string Source,
							 const std::unordered_map<std::string, std::string> &InitialDefines,
							 const std::vector<FPredefinedMacro> &PredefinedMacros,
							 FPreprocessContext Context, bool bBuildLineMap = false);
		~FPreprocessorSession();
		EPreprocessStep Advance();
		const FPreprocessImport &PendingImport() const;
		bool ResumeImport(std::string ResolvedPath, const std::vector<FPublicMacro> &Macros,
						  std::shared_ptr<const void> Owner = {});
		// Import a completed interface before the first Advance(), without emitting
		// source or re-exporting its macros. Uses ordinary import conflict checks.
		bool ImportImplicitMacros(const std::vector<FPublicMacro>& Macros,
			std::shared_ptr<const void> Owner = {});
		const std::string &Source() const;
		const std::string &Error() const;
		std::uint32_t ErrorLine() const;
		const FPreprocessResult &Result() const;
		const FPreprocessLineMap &LineMap() const;

	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	/**
	 * Line-oriented conditional compilation + macro replacement.
	 *
	 * Supported directives (optional leading whitespace, then '#'):
	 *   #define NAME
	 *   #define NAME replacement...
	 *   #define NAME(params) replacement...   (function-like; `(` must follow NAME)
	 *   #define_export NAME[(params)] replacement... (public module macro)
	 *   #undef NAME
	 *   #include "module.vs"                  (re-exporting module import alias)
	 *   #ifdef NAME / #ifndef NAME
	 *   #if EXPR / #elif EXPR
	 *   #else / #endif
	 *
	 * Macros: object-like and function-like (C-style). Function-like invocation
	 * requires `(` after the name (whitespace allowed). Arguments are collected
	 * with nested parentheses; comments inside arguments become spaces. Non-variadic
	 * arity must match; `F()` with one named parameter is one empty argument.
	 * Variadic forms `#define F(...)` / `#define F(a, ...)` bind remaining args
	 * (comma-joined) to `__VA_ARGS__`; `__VA_OPT__(tokens)` contributes its balanced
	 * token sequence only when the expanded variadic argument contains tokens and
	 * composes with `#` stringization and `##` token pasting. Empty
	 * `F()` with named+variadic params fills
	 * every named slot and `__VA_ARGS__` with empty (GNU-friendly). Replacement:
	 * parameter substitution, `#` stringize, `##` token paste, then rescan (the
	 * expanding macro name is suppressed). If a macro expands to a single
	 * identifier that names a function-like macro, a following `(` in the source
	 * is glued as that call (object-like aliases / `F()()` chains), except the
	 * name(s) just expanded in this chain are not re-glued (`#define F() F` +
	 * `F()()` -> `F()`). Nested object/function macros expand; arguments are fully
	 * expanded before substitution unless used with `#` / `##`.
	 * Dynamic builtins: `__FILE__` expands to the normalized logical module path
	 * (`<memory>` without context); `__LINE__` expands to the 1-based invocation line.
	 * `__FILE__`, `__LINE__`, `__VA_ARGS__`, and `__VA_OPT__` are reserved and cannot
	 * be macro names, named parameters, initial defines, or host macro names.
	 * Nested expansion depth is capped; runaway nesting fails with an error.
	 * An empty argument adjacent to `##` (before and/or after) drops those paste
	 * operators; if the left side then ends with `,`, that comma is dropped too
	 * (GNU placemarker / `, ## __VA_ARGS__`; also `x ## EMPTY ## y` -> `xy`).
	 * #if EXPR: integer constant expression after macro expand; `defined NAME` /
	 * `defined(NAME)`; undefined identifiers are 0. Decimal / octal / hex ints.
	 * Ops: ! ~ + - * / % << >> < <= > >= == != & ^ | && || ?: and parentheses.
	 * `&&` / `||` / `?:` short-circuit (skipped side ignores div-by-zero / shift range).
	 * Empty `#if` / `#elif` expressions are always rejected (even in skipped branches).
	 * Skipped `#if` / `#elif` still require a well-formed expression (syntax check);
	 * arithmetic traps on the skipped side are ignored.
	 * Line/block comments in #if expressions are ignored.
	 * Backslash-newline (`\` at end of a physical line) is deleted and lines are
	 * truly joined (C phase 2); no padding blank lines.
	 * An unclosed slash-star block-comment opener on a directive opens a file
	 * comment for subsequent lines;
	 * if that directive is in an inactive branch, the opener is not emitted (inactive
	 * lines are blanked) but directive recognition still respects the comment state.
	 *
	 * Inactive lines are blanked (newlines kept). Without `\` joins, that keeps
	 * lexer lines aligned with the source; after true `\` join, one logical line
	 * may span multiple physical lines (same as C).
	 * Each TU starts from the same host seeds. #define stays private; #define_export
	 * exposes the final active definition through import. export import and #include
	 * also re-export imported public macros. Standalone mode performs no module I/O;
	 * Module mode requires a resolver or explicit FPreprocessorSession resumption.
	 * `#include "module.vs"` lowers to `export import "module.vs";`: only quoted literal
	 * paths are accepted. Includes are therefore transitively visible, but only declarations
	 * exported by the target module are forwarded. Module identity already provides duplicate
	 * suppression. Circular dependencies follow the module loader policy (allowed by default,
	 * rejected when strict circular-import checking is enabled). Normal module resolution and
	 * cache rules apply.
	 * InitialDefines / CLI `-D`: bare `NAME` or `NAME=value` (object-like only).
	 * PredefinedMacros: structured function-like host macros, seeded into every TU.
	 * A source file must `#undef` a predefined macro before redefining it; that change
	 * is local to the current TU and never mutates the host configuration.
	 *
	 * OutLineMap (optional): physical-line activity + `#define`/`#undef` timeline for tooling.
	 */
	bool PreprocessSource(
		std::string_view Source,
		const std::unordered_map<std::string, std::string>& InitialDefines,
		const std::vector<FPredefinedMacro>& PredefinedMacros,
		const FPreprocessContext& Context,
		std::string& OutSource,
		std::string& OutError,
		std::uint32_t* OutErrorLine = nullptr,
		FPreprocessLineMap* OutLineMap = nullptr);

	// Backwards-compatible overload using LogicalPath == "<memory>".
	bool PreprocessSource(
		std::string_view Source,
		const std::unordered_map<std::string, std::string>& InitialDefines,
		const std::vector<FPredefinedMacro>& PredefinedMacros,
		std::string& OutSource,
		std::string& OutError,
		std::uint32_t* OutErrorLine = nullptr,
		FPreprocessLineMap* OutLineMap = nullptr);

	// Backwards-compatible object-like-only entry point.
	bool PreprocessSource(
		std::string_view Source,
		const std::unordered_map<std::string, std::string>& InitialDefines,
		std::string& OutSource,
		std::string& OutError,
		std::uint32_t* OutErrorLine = nullptr,
		FPreprocessLineMap* OutLineMap = nullptr);

	/**
	 * Map a 1-based position in the preprocessed output back to physical source.
	 * Returns false when the map is empty or the position is out of range.
	 */
	bool MapPreprocessedToPhysical(
		const FPreprocessLineMap& Map,
		std::uint32_t PreprocessedLine,
		std::uint32_t PreprocessedColumn,
		std::uint32_t& OutPhysicalLine,
		std::uint32_t& OutPhysicalColumn);

	/**
	 * Map a 1-based position in physical source to preprocessed output.
	 * Picks the closest mapped byte on the same physical line when there is no exact hit.
	 * Returns false when the map is empty or no byte maps to that physical line.
	 */
	bool MapPhysicalToPreprocessed(
		const FPreprocessLineMap& Map,
		std::uint32_t PhysicalLine,
		std::uint32_t PhysicalColumn,
		std::uint32_t& OutPreprocessedLine,
		std::uint32_t& OutPreprocessedColumn);

	// First preprocessed line emitted from PhysicalLine (for breakpoints).
	bool MapPhysicalLineToPreprocessedLine(
		const FPreprocessLineMap& Map,
		std::uint32_t PhysicalLine,
		std::uint32_t& OutPreprocessedLine);

	// A physical line that produced PreprocessedLine (for stack display).
	bool MapPreprocessedLineToPhysicalLine(
		const FPreprocessLineMap& Map,
		std::uint32_t PreprocessedLine,
		std::uint32_t& OutPhysicalLine);

	bool ParseDefineSpec(
		std::string_view Spec,
		std::string& OutName,
		std::string& OutBody,
		std::string& OutError);

	bool BuildDefineMap(
		const std::vector<std::string_view>& Specs,
		std::unordered_map<std::string, std::string>& OutDefines,
		std::string& OutError);

	bool ValidatePredefinedMacros(
		const std::vector<FPredefinedMacro>& Macros,
		std::string& OutError);

	// Parse transport form `NAME(param, ...)=replacement` used by CLI/tool hosts.
	bool ParsePredefinedMacroSpec(
		std::string_view Spec,
		FPredefinedMacro& OutMacro,
		std::string& OutError);

	std::vector<FPredefinedMacro> NormalizePredefinedMacros(
		const std::vector<FPredefinedMacro>& Macros);

	std::vector<std::string> NormalizeDefineList(const std::vector<std::string_view>& Defines);
}
