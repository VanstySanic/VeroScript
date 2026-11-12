// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <iostream>
#include <sstream>

int main()
{
	using namespace VeroScript;
	const auto Directory = std::filesystem::current_path() / "__native_call_site__";
	const auto Main = PathUtil::ToUtf8(Directory / "main.vs");
	const auto Helper = PathUtil::ToUtf8(Directory / "helper.vs");
	std::vector<std::pair<std::string, std::string>> Seen;
	FNativeFunction Probe;
	Probe.Name = "probe_call_site";
	Probe.ReturnType = ENativeType::Int;
	Probe.UserData = &Seen;
	Probe.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string& Error)
	{
		if (!Call.ScriptInvoker)
		{
			Error = "missing invoker";
			return false;
		}
		auto& Sites = *static_cast<decltype(Seen)*>(Call.UserData);
		Sites.emplace_back(Call.ScriptInvoker->GetCallingModulePath(0), Call.ScriptInvoker->GetCallingModulePath(1));
		if (!Call.ScriptInvoker->GetCallingModulePath(SIZE_MAX).empty())
		{
			Error = "invalid call depth was accepted";
			return false;
		}
		Out = std::int32_t{0};
		return true;
	};
	std::vector<FNativeFunction> Natives{Probe};
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.Natives = &Natives;
	Options.SourceOverlays.push_back(
		{Helper, "int helper_initialized=probe_call_site(); export int relay(){return probe_call_site();}"});
	auto Compiled = VeroScriptCompile::CompileSource(
		"import \"helper.vs\"; int initialized=probe_call_site(); int main(){return relay();}", Main, Options);
	if (!Compiled.bSuccess || !Compiled.Module)
	{
		std::cerr << Compiled.Error.Message;
		return 1;
	}
	for (const bool Strip : {false, true})
	{
		auto Module = *Compiled.Module;
		if (Strip)
		{
			Module.SourceMetadata.reset();
			Module.InitDebugLines.clear();
			Module.DebugFiles.clear();
			for (auto& Function : Module.Functions)
			{
				Function.Location = {};
				Function.DebugLines.clear();
			}
		}
		Seen.clear();
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(Module, ERunMode::Script, {}, &Natives) || Seen.size() != 3 || Seen[0].first != Helper ||
			!Seen[0].second.empty() || Seen[1].first != Main || !Seen[1].second.empty() || Seen[2].first != Helper ||
			Seen[2].second != Main)
		{
			std::cerr << "native caller modules or stripped-module origin failed: " << Vm.GetError().Message;
			return 1;
		}
	}
	std::cout << "Native caller modules and source-free origins passed\n";
	FCompileOptions GenericOptions = VeroScript::Test::MakeCompileOptions();
	GenericOptions.SourceOverlays.push_back(
		{Helper, "export struct Box<T>{T Value; Box(T value){Value=value;} T Get(){return Value;}} "
				 "export Box<T> wrap<T>(T value){return Box<T>(value);}"});
	auto Generic =
		VeroScriptCompile::CompileSource("import \"helper.vs\"; export class PublicValue {public: int Value=7;} "
										 "class PrivateValue {public: int Value=9;} int main(){"
										 "auto first=wrap(PublicValue()); auto second=wrap(PrivateValue());"
										 "assert(first.Get().Value==7); assert(second.Get().Value==9); return 0;}",
										 Main, GenericOptions);
	if (!Generic.bSuccess || !Generic.Module)
	{
		std::cerr << Generic.Error.Message;
		return 1;
	}
	std::ostringstream GenericOutput;
	VeroScriptVm GenericVm(GenericOutput);
		VeroScript::Test::Configure(GenericVm);
	if (!GenericVm.Execute(*Generic.Module, ERunMode::Script))
	{
		std::cerr << GenericVm.GetError().Message;
		return 1;
	}
	GenericOptions.SourceOverlays[0].Source =
		"export struct Box<T>{T Value; Box(T value){Value=value;}} "
		"export Box<T> wrap<T>(T value){Unrelated forbidden=Unrelated(); return Box<T>(value);}";
	auto Invisible =
		VeroScriptCompile::CompileSource("import \"helper.vs\"; export class PublicValue {} export class Unrelated {} "
										 "int main(){auto value=wrap(PublicValue()); return 0;}",
										 Main, GenericOptions);
	if (Invisible.bSuccess)
	{
		std::cerr << "generic instantiation exposed an unrelated caller type";
		return 1;
	}
	auto FieldDefault =
		VeroScriptCompile::CompileSource("export struct Measure{public:int Value;Measure(int value){Value=value;}} "
										 "class Card{public:Measure Value=Measure(17);} "
										 "int main(){Card card=Card();assert(card.Value.Value==17);return 0;}",
										 Main);
	if (!FieldDefault.bSuccess || !FieldDefault.Module)
	{
		std::cerr << FieldDefault.Error.Message;
		return 1;
	}
	VeroScriptVm DefaultVm(GenericOutput);
		VeroScript::Test::Configure(DefaultVm);
	if (!DefaultVm.Execute(*FieldDefault.Module, ERunMode::Script))
	{
		std::cerr << DefaultVm.GetError().Message;
		return 1;
	}
	auto ArrayArgument = VeroScriptCompile::CompileSource(
		"struct Collection{public:int operator[](array<int> values){int result=0;for(int v:values){result+=v;}return "
		"result;}} "
		"int main(){Collection value=Collection();assert(value[{1,2}]==3);assert(value[{}]==0);return 0;}",
		Main);
	if (!ArrayArgument.bSuccess || !ArrayArgument.Module)
	{
		std::cerr << ArrayArgument.Error.Message;
		return 1;
	}
	VeroScriptVm ArrayVm(GenericOutput);
		VeroScript::Test::Configure(ArrayVm);
	if (!ArrayVm.Execute(*ArrayArgument.Module, ERunMode::Script))
	{
		std::cerr << ArrayVm.GetError().Message;
		return 1;
	}
	auto AmbiguousArray = VeroScriptCompile::CompileSource(
		"struct Collection{public:array<int> operator[](array<int> values){return values;} "
		"array<int> operator[](array<string> values){array<int> result={2};return result;}} "
		"int main(){Collection value=Collection();array<int> result=value[{}];return 0;}",
		Main);
	if (AmbiguousArray.bSuccess)
	{
		std::cerr << "outer container type affected list parameter overload selection: "
				  << AmbiguousArray.Error.Message;
		return 1;
	}
	return 0;
}
