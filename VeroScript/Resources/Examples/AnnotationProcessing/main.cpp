// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <iostream>
using namespace VeroScript;

class FGetter final : public IAnnotationProcessor
{
public:
	void Process(const FAnnotationContext& C, FAnnotationOutput& Output) override
	{
		Output.SubmitMethod(C.Owner->Id, EMemberAccess::Public,
							C.Declaration.TypeName + " get_" + C.Declaration.Name + "() const { return " +
								C.Declaration.Name + "; }");
	}
};

int main()
{
	auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
	std::string Error;
	if (!Registry->Register(
			{"@Getter", "example.getter", "1", "", AnnotationTargetMask(EAnnotationTarget::Field),
				static_cast<std::uint8_t>(EAnnotationOperation::AddMethod), 0,
				[]
				{
					return std::make_unique<FGetter>();
				}},
			Error))
	{
		std::cerr << Error << '\n';
		return 1;
	}
	Registry->Freeze();
	FCompileOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::BytecodeVm::CreateFactory(VeroScript::GetVmHostServices());
	Options.AnnotationProcessing.Registry = Registry;
	auto Compiled = VeroScriptCompile::CompileSource(
		"struct Item { @Getter int value=42; }; void main(){ Item x=Item(); print(x.get_value()); }", "example.vs",
		Options);
	if (!Compiled.bSuccess)
	{
		std::cerr << Compiled.Error.Message << '\n';
		return 1;
	}
	VeroScriptVm Vm(std::cout);
	VeroScript::BytecodeVm::Configure(Vm);
	if (!Vm.Execute(*Compiled.Module, ERunMode::Script))
	{
		std::cerr << Vm.GetError().Message << '\n';
		return 1;
	}
	return 0;
}
