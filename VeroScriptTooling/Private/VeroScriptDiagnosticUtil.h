// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptLocale.h"
#include "VeroScriptToolingTypes.h"

#include <string>
#include <string_view>

namespace VeroScript::Tooling
{
	// Map engine FScriptError → tooling FDiagnostic (includes MessageId as Code).
	inline FDiagnostic ToToolingDiagnostic(
		const VeroScript::FScriptError& Error,
		const std::string_view FallbackFileLabel,
		const std::string_view FallbackMessage = "operation failed")
	{
		Locale::InitializeModuleFromEnvironment("VeroScriptTooling");
		FDiagnostic Diagnostic;
		Diagnostic.Severity = static_cast<EDiagnosticSeverity>(Error.Severity);
		const std::string_view EnglishFallback =
			Error.Message.empty() ? FallbackMessage : std::string_view(Error.Message);
		// Prefer MessageId so LSP/DAP respect VERO_LANG / --lang like the CLI.
		Diagnostic.Message = Locale::FormatDiagnosticMessage(
			Error.MessageId,
			Error.MessageArgs,
			EnglishFallback);
		Diagnostic.Code = Error.MessageId;
		Diagnostic.MessageArgs = Error.MessageArgs;
		Diagnostic.FileLabel = !Error.Location.FileLabel.empty()
			? Error.Location.FileLabel
			: std::string(FallbackFileLabel);
		Diagnostic.Range.StartLine = Error.Location.Line;
		Diagnostic.Range.StartColumn = Error.Location.Column;
		Diagnostic.Range.EndLine = Error.Location.Line;
		Diagnostic.Range.EndColumn = Error.Location.Column;
		Diagnostic.bPhysicalLocation = Error.bPhysicalLocation;
		return Diagnostic;
	}

	// Prefer multi-diag list when present; otherwise map Primary alone.
	inline void AppendToolingDiagnostics(
		std::vector<FDiagnostic>& Out,
		const VeroScript::FScriptError& Primary,
		const std::vector<VeroScript::FScriptError>& Diagnostics,
		const std::string_view FallbackFileLabel,
		const std::string_view FallbackMessage = "operation failed")
	{
		if (!Diagnostics.empty())
		{
			Out.reserve(Out.size() + Diagnostics.size());
			for (const VeroScript::FScriptError& Error : Diagnostics)
			{
				Out.push_back(ToToolingDiagnostic(Error, FallbackFileLabel, FallbackMessage));
			}
			return;
		}
		Out.push_back(ToToolingDiagnostic(Primary, FallbackFileLabel, FallbackMessage));
	}
}
