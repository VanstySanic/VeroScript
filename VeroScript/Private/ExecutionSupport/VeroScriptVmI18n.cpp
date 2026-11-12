// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include <stdexcept>

namespace VeroScript
{
	I18n::FI18nWork VeroScriptVm::GetI18nWork()
	{
		const auto Work = GetMemoryWork();
		return {Work.Check, Work.UserData, I18nLimits.MaxOutputBytes};
	}
	void VeroScriptVm::SetI18nServices(std::shared_ptr<I18n::FI18nServices> Services, I18n::FI18nLimits Limits)
	{
		if (bMemoryExecutionActive)
		{
			throw std::logic_error("cannot replace active internationalization services");
		}

		I18nServices = std::move(Services);
		I18nLimits = Limits;
	}
	bool VeroScriptVm::NativeFailure(FScriptError Diagnostic, std::string Message,
		std::string_view Name, const FSourceLocation& Location)
	{
		if (bHasError || bHasPendingScriptException || bExitRequested || !CheckExecutionGovernance()) return false;
		if (!Diagnostic.MessageId.empty() || !Diagnostic.Message.empty())
		{
			Diagnostic.Location = RefineErrorLocation(Location);
			Diagnostic.Severity = ECompileDiagnosticSeverity::Error;
			Diagnostic.bPhysicalLocation = false;
			Error = std::move(Diagnostic);
			bHasError = true;
			return false;
		}
		if (Message.rfind("diag.memory_", 0) == 0)
		{
			ReportErrorId(Message, Location);
		}

		else
		{
			ReportError(
				Message.empty() ? "native call failed: " + std::string(Name) : std::move(Message), Location);
		}

		return false;
	}
}
