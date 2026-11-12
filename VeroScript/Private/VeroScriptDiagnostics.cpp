// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDiagnostics.h"

#include "VeroScriptLocale.h"

#include <initializer_list>
#include <vector>

namespace VeroScript
{
	void FillDiagnostic(
		FScriptError& Error,
		const std::string_view MessageId,
		const FSourceLocation& Location,
		const std::initializer_list<FDiagnosticArg> Args)
	{
		Error.MessageId.assign(MessageId.data(), MessageId.size());
		Error.MessageArgs.clear();
		Error.MessageArgs.reserve(Args.size());
		std::vector<std::string_view> Views;
		Views.reserve(Args.size());
		for (const FDiagnosticArg& Arg : Args)
		{
			Error.MessageArgs.push_back(Arg.Value);
			Views.push_back(Error.MessageArgs.back());
		}
		Error.Message = Locale::FormatEnglish(MessageId, Views);
		Error.Location = Location;
		Error.bPhysicalLocation = false;
		Error.bUncaughtScriptException = false;
	}

	FScriptError MakeDiagnostic(
		const std::string_view MessageId,
		const FSourceLocation& Location,
		const std::initializer_list<FDiagnosticArg> Args)
	{
		FScriptError Error;
		FillDiagnostic(Error, MessageId, Location, Args);
		return Error;
	}

	void FillFromEnglishMessage(
		FScriptError& Error,
		std::string Message,
		const FSourceLocation& Location)
	{
		Error.Message = std::move(Message);
		Error.Location = Location;
		Error.bPhysicalLocation = false;
		Error.bUncaughtScriptException = false;
		Error.MessageId.clear();
		Error.MessageArgs.clear();
		Locale::TryResolveEnglish(Error.Message, Error.MessageId, Error.MessageArgs);
	}
}
