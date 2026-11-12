// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <iostream>

using namespace VeroScript;

class FCardBuilderProcessor final : public ICompileProcessor
{
public:
	void Process(const FCompileProcessingContext& Context,FCompileProcessingOutput& Output) override
	{
		if (Context.Phase == ECompileProcessingPhase::Types)
		{
			Output.AddDeclaration({Context.ModuleId, "", "", Context.Snapshot},
				"class CardBuilder {public:Card value=Card();"
				"CardBuilder Text(string text){value.text=text;return this;}"
				"Card Build(){return value;}};");
		}
		else
		{
			Output.AddDeclaration({Context.ModuleId,"",Context.Declaration->Id,Context.Snapshot},
				"public:static CardBuilder Create(){return CardBuilder();}");
		}
	}
};

int main()
{
	auto Registry=std::make_shared<FCompileProcessorRegistry>();
	FCompileProcessorDesc Description;
	Description.ProcessorId="example.card-builder";
	Description.Version="0.1.1";
	Description.Phases=CompilePhaseMask(ECompileProcessingPhase::Types)|CompilePhaseMask(ECompileProcessingPhase::Members);
	Description.Operations=CompileOperationMask(ECompileOperation::AddDeclaration);
	FCompileSubscription Subscription;
	Subscription.Event=ECompileProcessingEvent::Annotation;
	Subscription.Targets=AnnotationTargetMask(EAnnotationTarget::Class);
	Subscription.AnnotationName="@Builder";
	Description.Subscriptions.push_back(std::move(Subscription));
	Description.Factory = []
	{
		return std::make_unique<FCardBuilderProcessor>();
	};
	std::string Error;
	if (!Registry->Register(std::move(Description), Error))
	{
		std::cerr << Error << '\n';
		return 1;
	}
	Registry->Freeze();
	FCompileOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Options.CompileProcessing.Registry=Registry;
	FAnnotationDefinition Builder;
	Builder.Id="example.annotation.builder";
	Builder.Name="@Builder";
	Builder.CaseSensitive=false;
	Builder.Targets=AnnotationTargetMask(EAnnotationTarget::Class);
	Options.CompileProcessing.AnnotationDefinitions.push_back(std::move(Builder));
	auto Result = VeroScriptCompile::CompileSource(
		"/// A card with generated builder support.\n"
		"@builder class Card {public:string text;};"
		"void main(){Card card=Card::Create().Text(\"Save\").Build();print(card.text);}",
		"card.vs", Options);
	if (!Result.bSuccess)
	{
		std::cerr << Result.Error.Message << '\n';
		return 1;
	}
	VeroScriptVm Vm(std::cout);
	VeroScript::BytecodeVm::Configure(Vm);
	if (!Vm.Execute(*Result.Module, ERunMode::Script))
	{
		std::cerr << Vm.GetError().Message << '\n';
		return 1;
	}
	return 0;
}
