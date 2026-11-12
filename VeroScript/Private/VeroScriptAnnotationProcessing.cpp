// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAnnotationProcessing.h"
#include <algorithm>

namespace VeroScript
{
	void FAnnotationOutput::SubmitMethod(std::string Id, EMemberAccess Access, std::string Source)
	{
		FAnnotationGenerationOperation Op;
		Op.TargetId = std::move(Id);
		Op.Access = Access;
		Op.Source = std::move(Source);
		Operations.push_back(std::move(Op));
	}
	void FAnnotationOutput::CompleteMethodBody(std::string Id, std::string Source)
	{
		FAnnotationGenerationOperation Op;
		Op.Kind = EAnnotationOperation::CompleteMethodBody;
		Op.TargetId = std::move(Id);
		Op.Source = std::move(Source);
		Operations.push_back(std::move(Op));
	}
	void FAnnotationOutput::ReportDiagnostic(FScriptError Diagnostic)
	{
		Diagnostics.push_back(std::move(Diagnostic));
	}
	bool FAnnotationProcessorRegistry::Register(FAnnotationProcessorDesc Desc, std::string &Error)
	{
		Error.clear();
		const auto IsLetter = [](unsigned char C)
		{
			return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_';
		};
		std::size_t Start = !Desc.Name.empty() && Desc.Name[0] == '@' ? 1 : 0;
		bool ValidName = Start < Desc.Name.size() && IsLetter(static_cast<unsigned char>(Desc.Name[Start]));
		for (std::size_t I = Start + 1; I < Desc.Name.size(); ++I)
		{
			ValidName = ValidName &&
				(IsLetter(static_cast<unsigned char>(Desc.Name[I])) ||
					(Desc.Name[I] >= '0' && Desc.Name[I] <= '9'));
		}

		if (bFrozen)
		{
			Error = "annotation registry is frozen";
		}

		else if (!ValidName || Desc.ProcessorId.empty() || Desc.Version.empty() || !Desc.Factory ||
			!Desc.Targets || (Desc.Targets & ~AnnotationAllTargets) || (Desc.Operations & ~3u))
		{
			Error = "invalid annotation processor registration";
		}

		else if (std::any_of(Processors.begin(), Processors.end(),
					 [&](const auto& P)
					 {
						 return P.Name == Desc.Name && P.ProcessorId == Desc.ProcessorId;
					 }))
		{
			Error = "duplicate annotation processor name and identity";
		}

		if (!Error.empty())
		{
			return false;
		}

		Processors.push_back(std::move(Desc));
		return true;
	}
} // namespace VeroScript
