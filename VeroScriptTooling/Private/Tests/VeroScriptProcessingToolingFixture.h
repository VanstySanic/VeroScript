// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include <stdexcept>

namespace VeroScript::Tooling::Tests
{
	class FBuilderProcessor final : public ICompileProcessor
	{
	public:
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& Out) override
		{
			if (C.Phase == ECompileProcessingPhase::RawSource)
			{
				if (C.Source->Text.find("/*tooling-raw-shift*/") != std::string::npos)
				{
					Out.EditSource({C.Source->Id, 0, 0, {}}, "\n\n");
				}

				const auto At = C.Source->Text.find("record Card");
				if (At != std::string::npos)
				{
					Out.EditSource({C.Source->Id, At, At + 6, {}}, "class");
				}

				return;
			}
			if (C.Phase == ECompileProcessingPhase::ActiveSource)
			{
				if (C.Source->Text.find("/*tooling-active-shift*/") != std::string::npos)
				{
					Out.EditSource({C.Source->Id, 0, 0, {}}, "// generated prefix\n\n");
				}

				return;
			}
			std::string Name;
			if (!C.ReadDependency || !C.ReadDependency("builder-name", Name))
			{
				Name = "CardBuilder";
			}

			if (Name != "CardBuilder" && Name != "ChangedBuilder")
			{
				Name = "CardBuilder";
			}

			if (C.Phase == ECompileProcessingPhase::Types)
			{
				Out.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
					"/// Generated builder documentation.\nclass " + Name + " {public:Card value=Card();" +
						Name +
						" Text(string text){value.text=text;return this;}Card Build(){return value;}};");
			}

			else
			{
				Out.AddDeclaration({C.ModuleId, "", C.Declaration->Id, C.Snapshot},
					"public:static " + Name + " Create(){return " + Name + "();}");
			}
		}
	};

	inline std::shared_ptr<const FCompileProcessorRegistry> BuilderRegistry(bool Cacheable = true)
	{
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Desc;
		Desc.ProcessorId = "tooling.test.builder";
		Desc.Version = "1";
		Desc.Phases = CompilePhaseMask(ECompileProcessingPhase::RawSource)
			| CompilePhaseMask(ECompileProcessingPhase::ActiveSource)
			| CompilePhaseMask(ECompileProcessingPhase::Types) | CompilePhaseMask(ECompileProcessingPhase::Members);
		Desc.Operations = CompileOperationMask(ECompileOperation::EditSource) | CompileOperationMask(ECompileOperation::AddDeclaration);
		Desc.Subscriptions.push_back({ECompileProcessingEvent::Module, AnnotationAllTargets, "", "", {},
			CompilePhaseMask(ECompileProcessingPhase::RawSource) | CompilePhaseMask(ECompileProcessingPhase::ActiveSource)});
		Desc.Subscriptions.push_back({ECompileProcessingEvent::Annotation,
			AnnotationTargetMask(EAnnotationTarget::Class), "@Builder", "", {},
			CompilePhaseMask(ECompileProcessingPhase::Types) |
				CompilePhaseMask(ECompileProcessingPhase::Members)});
		Desc.Factory = []
		{
			return std::make_unique<FBuilderProcessor>();
		};
		Desc.Cacheable = Cacheable;
		std::string Error;
		if (!Registry->Register(std::move(Desc), Error))
		{
			throw std::runtime_error(Error);
		}

		Registry->Freeze();
		return Registry;
	}
}
