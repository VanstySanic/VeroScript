// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTypes.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript
{
	enum class ECompileDiagnosticSeverity : std::uint8_t
	{
		Error,
		Warning,
		Information,
		Hint
	};
	/**
	 * Lexer/parser/compiler diagnostic with source location.
	 * Prefer MessageId (+ MessageArgs) for new diagnostics; Message keeps an English
	 * fallback for tooling/exit-code heuristics and older Translate() paths.
	 */
	struct FScriptError
	{
		struct FOwnedLocation
		{
			FOwnedLocation& operator=(const FSourceLocation& Loc)
			{
				FileLabel.assign(Loc.FileLabel.data(), Loc.FileLabel.size());
				Line = Loc.Line;
				Column = Loc.Column;
				return *this;
			}

			// View into this owned storage; valid only while this location object lives.
			FSourceLocation AsSourceLocation() const
			{
				return FSourceLocation{FileLabel, Line, Column};
			}

			std::string FileLabel;
			std::uint32_t Line = 1;
			std::uint32_t Column = 1;

		};

		std::string Message;
		std::string MessageId;
		std::vector<std::string> MessageArgs;
		FOwnedLocation Location;
		// True for raw-source positions (e.g. preprocessing/import errors).
		// Such positions must not be remapped from preprocessed code a second time.
		bool bPhysicalLocation = false;
		ECompileDiagnosticSeverity Severity = ECompileDiagnosticSeverity::Error;
		// Provisional diagnostic for a script exception propagating across an
		// execution boundary. An explicit exit during cleanup supersedes it.
		bool bUncaughtScriptException = false;
	};

	/**
	 * In-memory file content for import resolution (path -> source).
	 * Paths should use '/' separators; loaders normalize before lookup.
	 */
	struct FSourceOverlay
	{
		std::string Path;
		std::string Source;
	};

	/**
	 * Owning diagnostic-format argument.
	 *
	 * Diagnostics are normally emitted on failure paths, so owning the short-lived
	 * arguments is preferable to accepting views that may refer to temporary display
	 * strings. This also keeps the API safe if diagnostic formatting later becomes
	 * deferred.
	 */
	struct FDiagnosticArg
	{
		FDiagnosticArg(const char* const InValue)
			: Value(InValue ? InValue : "")
		{
		}

		FDiagnosticArg(const std::string_view InValue)
			: Value(InValue)
		{
		}

		FDiagnosticArg(const std::string& InValue)
			: Value(InValue)
		{
		}

		FDiagnosticArg(std::string&& InValue)
			: Value(std::move(InValue))
		{
		}

		std::string Value;
	};

	// Fill Error with MessageId/Args and English Message fallback from the en catalog.
	void FillDiagnostic(
		FScriptError& Error,
		std::string_view MessageId,
		const FSourceLocation& Location = {},
		std::initializer_list<FDiagnosticArg> Args = {});

	FScriptError MakeDiagnostic(
		std::string_view MessageId,
		const FSourceLocation& Location = {},
		std::initializer_list<FDiagnosticArg> Args = {});

	/**
	 * Store English Message (+ Location); if Message matches an en-US catalog template,
	 * also stamp MessageId / MessageArgs for locale-aware display and tooling codes.
	 */
	void FillFromEnglishMessage(
		FScriptError& Error,
		std::string Message,
		const FSourceLocation& Location = {});
}
