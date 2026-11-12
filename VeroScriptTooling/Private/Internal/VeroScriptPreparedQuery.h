// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAstClone.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptPreprocessorQuery.h"
#include "VeroScriptProcessingSourceMap.h"
#include "Processing/VeroScriptProcessingSession.h"
#include <algorithm>

namespace VeroScript::Tooling::Detail
{
	inline FAnnotationSourcePosition GeneratedOriginLocation(const FProgram& Program,
		const std::string_view FileLabel)
	{
		if (FileLabel.rfind("veroscript-generated:", 0) != 0)
		{
			return {};
		}

		const auto Document =
			std::find_if(Program.GeneratedDocuments.begin(), Program.GeneratedDocuments.end(),
				[&](const auto& Doc)
				{
					return Doc.FileLabel == FileLabel;
				});
		if (Document == Program.GeneratedDocuments.end())
		{
			return {};
		}

		auto Origin = Document->Origin;
		if (const auto* Map = ProgramPreprocessorLineMap(Program, Origin.FileLabel))
		{
			std::uint32_t Line = 0, Column = 0;
			if (TryRemapPreprocessedToPhysical(*Map, Origin.Line, Origin.Column, Line, Column))
			{
				Origin.Line = Line;
				Origin.Column = Column;
			}
		}
		return Origin;
	}

	inline FSourceImplementationLocation SourceImplementationLocation(
		const FProgram& Program, const FBlockStmt* Body)
	{
		FSourceImplementationLocation Result;
		if (!Body ||
			(Body->DefinitionModulePath.empty() &&
				Body->Location.FileLabel.rfind("veroscript-generated:", 0) != 0))
		{
			return Result;
		}

		Result.FileLabel = Body->Location.FileLabel;
		Result.Line = Body->Location.Line;
		Result.Column = Body->Location.Column;
		Result.GeneratedOrigin = GeneratedOriginLocation(Program, Result.FileLabel);
		if (const auto* Map = ProgramPreprocessorLineMap(Program, Result.FileLabel))
		{
			std::uint32_t Line = 0, Column = 0;
			if (TryRemapPreprocessedToPhysical(*Map, Result.Line, Result.Column, Line, Column))
			{
				Result.Line = Line;
				Result.Column = Column;
			}
		}
		return Result;
	}

	template <class TOptions>
	bool PrepareQueryProgram(std::unique_ptr<FProgram> &Program, const TOptions &Options,
							 FScriptError *Error = nullptr, std::vector<FScriptError> *Diagnostics = nullptr,
							 std::vector<std::string> *Dependencies = nullptr,
							 std::vector<std::string> *ScriptBootstrapDependencies = nullptr)
	{
		if (!Program)
		{
			return false;
		}

		if (ScriptBootstrapDependencies)
		{
			ScriptBootstrapDependencies->clear();
			if (Program->ScriptCompileExtensionTask)
			{
				for (const auto& D :
					Program->ScriptCompileExtensionTask->GetProcessingOptions().InitialDependencies)
				{
					ScriptBootstrapDependencies->push_back(PathUtil::IdentityKey(D.Path));
				}
			}
		}
		if (!Program->ModuleEnvironment && !Options.AnnotationProcessing.Registry &&
			!Options.CompileProcessing.Registry && !Program->ScriptCompileExtensionTask &&
			Program->SourceDefinitions.empty() &&
			std::none_of(Program->Functions.begin(), Program->Functions.end(),
				[](const auto& F)
				{
					return !F.Body && !F.bDeleted && !F.bPrecompiledPackage;
				}) &&
			std::none_of(Program->PackageSourceModules.begin(), Program->PackageSourceModules.end(),
				[](const auto& S)
				{
					return !S.GeneratedOperations.empty() || !S.CompileGeneratedOperations.empty();
				}))
		{
			// Loading parses bodies but does not complete declaration extents.
			// Keep the preparation fast path while giving scope and outline queries
			// the same source metadata contract as a fully prepared program.
			CollectProgramSourceMetadata(*Program);
			return true;
		}

		auto Result = VeroScriptProgramPreparation::Prepare(
			std::move(Program), Options.AnnotationProcessing, nullptr, Options.Bindings);
		if (Dependencies)
		{
			for (const auto& D : Result.Dependencies)
			{
				Dependencies->push_back(D.Path);
			}
		}

		if (!Result.Succeeded())
		{
			if (Diagnostics)
			{
				*Diagnostics = Result.Diagnostics;
			}

			if (Error && !Result.Diagnostics.empty())
			{
				const auto It = std::find_if(Result.Diagnostics.begin(), Result.Diagnostics.end(),
					[](const auto& D)
					{
						return D.Severity == ECompileDiagnosticSeverity::Error;
					});
				*Error = It == Result.Diagnostics.end() ? Result.Diagnostics.front() : *It;
			}
			return false;
		}
		// Query consumers retain their existing ownership shape. The complete clone
		// carries preparation state and all generated source lifetime anchors.
		Program = CloneProgram(Result.Prepared->GetProgram());
		ApplyProcessingSourceMaps(Program->SourceMetadata.get(), Program->PreprocessLineMaps);
		return true;
	}
} // namespace VeroScript::Tooling::Detail
