// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptLambdaStaticFixture.h"
#include "VeroScriptCppCoreAot.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptPlatform.h"
#include "VeroScriptBytecodeIO.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace VeroScript;

int main(int Argc, char** Argv)
{
	FCoreAotOptions Options;
	Options.Target = GetHostPlatformInfo().Platform;
	FCoreAotSourceBundle Bundle;
	std::string Error;
	if (!CompileSourceToCoreAot(std::string(Test::LambdaStaticDefinitions) + "void observe(int n){if(n<0)print(n);} T identity<T>(T value){return value;} "
	                                                                         "class QueuedStaticValue<T>{public:static int value=7;"
	                                                                         "int copy=QueuedStaticValue<T>::value;"
	                                                                         "int callCopy=QueuedStaticValue<T>::read(1);"
	                                                                         "struct Inner{static int seed=3;int copy=QueuedStaticValue<T>::Inner::seed;};"
	                                                                         "static int read(int add=0){return QueuedStaticValue<T>::value+add;}};"
	                                                                         "class QueuedStaticDerived<T>:public QueuedStaticValue<T>{};"
	                                                                         "class QueuedStaticWrapper{public:QueuedStaticValue<int> item=QueuedStaticValue<int>();"
	                                                                         "int copy=QueuedStaticValue<int>::value;};"
	                                                                         "class QueuedStaticWrapperOf<T>{public:QueuedStaticValue<T> item=QueuedStaticValue<T>();};"
	                                                                         "class QueuedStaticState{public:QueuedStaticDerived<int> early=QueuedStaticDerived<int>();"
	                                                                         "QueuedStaticWrapperOf<int> wrapped=QueuedStaticWrapperOf<int>();"
	                                                                         "static QueuedStaticWrapper ordinary=QueuedStaticWrapper();"
	                                                                         "static QueuedStaticWrapperOf<int> generic=QueuedStaticWrapperOf<int>();"
	                                                                         "static int result=QueuedStaticDerived<int>::read(3);};"
	                                                                         "class ArrayCleanupOrder{public:static int trace=0;static int observed=-1;};"
	                                                                         "int cleanupMark(int value){ArrayCleanupOrder::trace=ArrayCleanupOrder::trace*10+value;return value;}"
	                                                                         "class ArrayCleanupNeeded<T>{public:static int seed=cleanupMark(1)+6;};"
	                                                                         "struct ArrayCleanupItem{~ArrayCleanupItem(){ArrayCleanupOrder::observed=ArrayCleanupNeeded<int>::seed;cleanupMark(2);}};"
	                                                                         "class ArrayCleanupWrapper{public:ArrayCleanupItem values[1];};"
	                                                                         "int cleanupWork(){ArrayCleanupWrapper value=ArrayCleanupWrapper();return 0;}"
	                                                                         "class ArrayCleanupState{public:static int value=cleanupWork();static int later=cleanupMark(3);};"
	                                                                         "using InitCallback=fn()->int;class CallbackNeeded<T>{public:static int seed=7;};"
	                                                                         "int callbackZero(){return 0;}int firstCallback(){return CallbackNeeded<int>::seed;}"
	                                                                         "int secondCallback(){return CallbackNeeded<bool>::seed;}"
	                                                                         "class CallbackConfig{public:static InitCallback read=callbackZero;};"
	                                                                         "InitCallback currentCallback(){return CallbackConfig::read;}"
	                                                                         "void selectFirstCallback(){CallbackConfig::read=firstCallback;}"
	                                                                         "void selectZeroCallback(){CallbackConfig::read=callbackZero;}"
	                                                                         "void selectSecondCallback(InitCallback& callback){callback=secondCallback;}"
	                                                                         "int returnedCallbackWork(){auto before=currentCallback();selectFirstCallback();selectZeroCallback();"
	                                                                         "selectFirstCallback();auto read=currentCallback();return read();}"
	                                                                         "int refCallbackWork(){CallbackConfig::read=callbackZero;auto before=CallbackConfig::read;"
	                                                                         "selectSecondCallback(ref CallbackConfig::read);auto read=CallbackConfig::read;return read();}"
	                                                                         "class CallbackState{public:static int first=returnedCallbackWork();static int second=refCallbackWork();};"
	                                                                         "template<typename T,int N=4> struct Buffer{T values[N];};"
	                                                                         "class Echo{public:template<typename T> T echo(T input){return input;}};"
	                                                                         "class RecursiveLink<T>{public:T value;RecursiveLink<T> next;"
	                                                                         "RecursiveLink<T> Get(){return next;}};"
	                                                                         "struct Outer<T=int>{struct Item{T value;};};"
	                                                                         "int scoped(int input){struct Local{int value;};Local item=Local();item.value=input;"
	                                                                         "Outer<>::Item result=Outer<>::Item();result.value=item.value;return result.value;}"
	                                                                         "int work(int n) { int x = 0; for(int i=0; i<n; i++) x += i;"
	                                                                         "Buffer<int,3> buffer=Buffer<int,3>();buffer.values[0]=scoped(x);Echo object=Echo();"
	                                                                         "fn(int)->int apply=identity<>;return apply(object.echo<int>(buffer.values[0]));} "
	                                                                         "int main() { assert(QueuedStaticState::result==10);assert(QueuedStaticValue<int>::value==7);"
	                                                                         "RecursiveLink<int> head=RecursiveLink<int>();"
	                                                                         "RecursiveLink<int> tail=RecursiveLink<int>();assert(head.next==nullptr);"
	                                                                         "tail.value=42;head.next=tail;assert(head.Get().value==42);head.next=nullptr;"
	                                                                         "assert(ArrayCleanupOrder::observed==7 && ArrayCleanupOrder::trace==123);"
	                                                                         "assert(CallbackState::first==7 && CallbackState::second==7);"
	                                                                         "assert(QueuedStaticState::ordinary.item.copy==7 && QueuedStaticState::ordinary.copy==7);"
	                                                                         "assert(QueuedStaticState::generic.item.copy==7);"
	                                                                         "QueuedStaticState state=QueuedStaticState();assert(state.early.copy==7);"
	                                                                         "assert(state.early.callCopy==8);"
	                                                                         "QueuedStaticValue<int>::value=12;QueuedStaticValue<int> later=QueuedStaticValue<int>();"
	                                                                         "assert(later.copy==12);assert(state.early.copy==7);"
	                                                                         "assert(later.callCopy==13);"
	                                                                         "QueuedStaticWrapper wrapperLater=QueuedStaticWrapper();"
	                                                                         "QueuedStaticWrapperOf<int> genericLater=QueuedStaticWrapperOf<int>();"
	                                                                         "assert(wrapperLater.item.copy==12 && wrapperLater.copy==12);"
	                                                                         "assert(genericLater.item.copy==12 && QueuedStaticValue<int>::value==12);"
	                                                                         "assert(QueuedStaticState::ordinary.item.copy==7 && QueuedStaticState::generic.item.copy==7);"
	                                                                         "QueuedStaticValue<int>::Inner nested=QueuedStaticValue<int>::Inner();assert(nested.copy==3);"
	                                                                         "checkLambdaStaticSupport(); observe(8); print(work(8)); return 0; }",
	        "core-aot.vs", {}, Options, Bundle, Error))
	{
		std::cerr << Error << '\n';
		return 1;
	}
	if (Bundle.Bytecode.empty() || Bundle.Source.find("goto L") == std::string::npos ||
	    Bundle.ManifestJson.find("core-linked-aot") == std::string::npos ||
	    !std::any_of(Bundle.Coverage.begin(), Bundle.Coverage.end(), [](const auto& C)
	{
		return C.bNative;
	}))
	{
		return 2;
	}
	const auto ScopedModule = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bundle.Bytecode), 0, Error);
	if (!ScopedModule || !std::any_of(ScopedModule->Types.begin(), ScopedModule->Types.end(), [](const auto& Type)
	{
		return Type.Scope.bLocal && Type.Scope.SimpleName == "Local";
	}) ||
	    !std::any_of(ScopedModule->Types.begin(), ScopedModule->Types.end(), [](const auto& Type)
	{
		return Type.Scope.SimpleName == "Item" && !Type.Scope.OwnerTypeName.empty();
	}))
	{
		std::cerr << "Core AOT lost scoped type metadata: " << Error << '\n';
		return 18;
	}
	if (std::none_of(ScopedModule->TemplateDeclarations.begin(), ScopedModule->TemplateDeclarations.end(), [](const auto& Declaration)
	{
		return Declaration.Name == "Buffer" && Declaration.Parameters.GetParameters().size() == 2 && Declaration.Parameters.GetParameters()[1].Kind == ETemplateParameterKind::Value;
	}) ||
	    std::none_of(ScopedModule->Functions.begin(), ScopedModule->Functions.end(), [](const auto& Function)
	{
		return !Function.TemplateInstance.DeclarationId.empty() && !Function.TemplateInstance.Arguments.empty();
	}))
	{
		std::cerr << "Core AOT lost template declaration or instance metadata\n";
		return 19;
	}
	FCoreAotSourceBundle ConstantBundle;
	if (!CompileSourceToCoreAot("constexpr int twice(int n){if constexpr(true)return n*2;else return missing();}constexpr int count=twice(3);"
	                            "int main(){int runtime=4;return count+twice(runtime);}",
	        "constexpr-core-aot.vs", {}, Options, ConstantBundle, Error))
	{
		std::cerr << "constexpr Core AOT: " << Error << '\n';
		return 20;
	}
	const auto ConstantModule = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(ConstantBundle.Bytecode), 0, Error);
	if (!ConstantModule || std::none_of(ConstantModule->GlobalMetadata.begin(), ConstantModule->GlobalMetadata.end(), [](const auto& Global)
	{
		return Global.bConstexpr && Global.ConstantValue && Global.ConstantValue->Bits == 6;
	}) ||
	    std::none_of(ConstantModule->Functions.begin(), ConstantModule->Functions.end(), [](const auto& Function)
	{
		return Function.bConstexpr;
	}))
	{
		std::cerr << "constexpr Core AOT metadata: " << Error << '\n';
		return 21;
	}
	FCoreAotSourceBundle TargetBundle;
	auto TargetOptions = Options;
	TargetOptions.Target = "ios-arm64";
	if (!CompileSourceToCoreAot("#if VEROSCRIPT_TARGET_IOS\nint main(){return 7;}\n#else\n#error target context was not applied\n#endif\n",
	        "target.vs", {}, TargetOptions, TargetBundle, Error))
	{
		std::cerr << Error << '\n';
		return 3;
	}
	if (TargetBundle.Source.find("D.Target = \"ios-arm64\"") == std::string::npos) return 4;
	{
		FCoreAotSourceBundle AliasedBundle;
		AliasedBundle.Source = std::string(128, ' ') + "int main(){return 6;}";
		const std::string Label = "core-aot-aliased-source-and-label.vs";
		AliasedBundle.ManifestJson = Label;
		if (!CompileSourceToCoreAot(AliasedBundle.Source, AliasedBundle.ManifestJson,
		        {}, Options, AliasedBundle, Error))
		{
			std::cerr << "Core AOT invalidated its aliased inputs: " << Error << '\n';
			return 22;
		}
		const auto AliasedModule = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(AliasedBundle.Bytecode), 0, Error);
		if (!AliasedModule || AliasedModule->EntryModulePath.find(Label) == std::string::npos)
		{
			std::cerr << "Core AOT lost its aliased source label: " << Error << '\n';
			return 23;
		}
	}
	if (!CompileSourceToCoreAot("int main(){ try { throw 1; } catch(int n) { return n; } }", "eh.vs", {}, Options, TargetBundle, Error))
	{
		std::cerr << Error << '\n';
		return 5;
	}
	if (std::none_of(TargetBundle.Coverage.begin(), TargetBundle.Coverage.end(), [](const auto& C)
	{
		return !C.bNative;
	}))
	{
		return 6;
	}
	FHostBindingRegistry Catalog;
	FHostTypeId HostType;
	if (!Catalog.RegisterType("HostThing", EHostTypeKind::Class, HostType, Error)) return 8;
	FCompileOptions HostOptions;
	VeroScript::Test::Configure(HostOptions);
	HostOptions.Bindings = &Catalog;
	std::vector<FNativeFunction> EmptyNatives;
	auto CatalogOptions = Options;
	CatalogOptions.Natives = &EmptyNatives;
	if (!CompileSourceToCoreAot("int main(){HostThing value = nullptr;return 0;}", "host-types.vs", HostOptions, CatalogOptions, TargetBundle, Error))
	{
		std::cerr << Error << '\n';
		return 9;
	}
	for (const auto Parameter : {ENativeType::Int, ENativeType::String})
	{
		const auto Callback = [](const FNativeCallContext&, FValue& Result, std::string&)
		{
			Result = std::int32_t{1};
			return true;
		};
		if (!Catalog.RegisterNative("host_overload", ENativeType::Int, {Parameter}, Callback, nullptr, Error))
		{
			return 15;
		}
		for (const auto Kind : {EHostMemberKind::Method, EHostMemberKind::StaticMethod})
		{
			FHostMemberDesc Method;
			Method.Kind = Kind;
			Method.Name = Kind == EHostMemberKind::Method ? "read" : "make";
			Method.ReturnType = ENativeType::Int;
			Method.ParamTypes = {Parameter};
			Method.Callback = Callback;
			if (!Catalog.RegisterMember(HostType, Method, Error)) return 16;
		}
	}
	auto OverloadOptions = Options;
	OverloadOptions.Natives = &Catalog.GetNativeFunctions();
	if (!CompileSourceToCoreAot(
	        "int main(){HostThing h=nullptr;return host_overload(1)+host_overload(\"x\")"
	        "+h.read(1)+h.read(\"x\")+HostThing::make(1)+HostThing::make(\"x\");}",
	        "host-overloads.vs", HostOptions, OverloadOptions, TargetBundle, Error) ||
	    TargetBundle.Bytecode.empty())
	{
		std::cerr << "Core AOT host overload generation failed: " << Error << '\n';
		return 17;
	}
	CatalogOptions.ExecutionCheck = {[](void*) noexcept
	{
		return false;
	}, nullptr};
	if (CompileSourceToCoreAot("int main(){return 0;}", "cancel.vs", {}, CatalogOptions, TargetBundle, Error))
	{
		return 10;
	}
	std::size_t Checks = 0;
	auto CancelDuringCompile = Options;
	CancelDuringCompile.ExecutionCheck = {[](void* Context) noexcept
	{
		return ++*static_cast<std::size_t*>(Context) < 2;
	}, &Checks};
	// Cancellation must reach the front end before it reports this syntax error.
	if (CompileSourceToCoreAot("invalid; int main(){return 0;}", "cancel-front-end.vs", {}, CancelDuringCompile, TargetBundle, Error) || Checks < 2 || Error.find("stopped by host") == std::string::npos || !TargetBundle.Source.empty())
	{
		std::cerr << "Core AOT cancellation did not reach source compilation: " << Error << '\n';
		return 12;
	}
	{
		std::size_t Stages = 0;
		FAnalysisControlScope Parent([]
		{
			return false;
		}, [&](std::string_view, double)
		{
			++Stages;
		});
		if (!CompileSourceToCoreAot("int main(){return 0;}", "parent-analysis.vs", {}, Options, TargetBundle, Error) || ActiveAnalysisControl != &Parent || !Stages)
		{
			return 13;
		}
		Parent.ShouldCancel = []
		{
			return true;
		};
		if (CompileSourceToCoreAot("int main(){return 0;}", "parent-cancel.vs", {}, Options, TargetBundle, Error) || ActiveAnalysisControl != &Parent || Error.find("stopped by host") == std::string::npos)
		{
			return 14;
		}
	}
	Options.bRequireAllNative = true;
	if (CompileSourceToCoreAot("int main(){ try { throw 1; } catch(int n) { return n; } }", "eh.vs", {}, Options, TargetBundle, Error))
	{
		return 7;
	}
	auto BadName = Options;
	BadName.FactoryName = "class";
	if (CompileSourceToCoreAot("int main(){return 0;}", "name.vs", {}, BadName, TargetBundle, Error) || !TargetBundle.Source.empty())
	{
		return 11;
	}
	if (Argc == 2)
	{
		const auto Root = std::filesystem::u8path(Argv[1]);
		std::filesystem::create_directories(Root);
		std::ofstream(Root / "generated.cpp", std::ios::binary) << Bundle.Source;
		std::ofstream File(Root / "program.vsbc", std::ios::binary);
		File.write(reinterpret_cast<const char*>(Bundle.Bytecode.data()), static_cast<std::streamsize>(Bundle.Bytecode.size()));
		std::ofstream App(Root / "program.vsapp", std::ios::binary);
		App.write(reinterpret_cast<const char*>(Bundle.Application.data()), static_cast<std::streamsize>(Bundle.Application.size()));
		std::ofstream(Root / "aot.json", std::ios::binary) << Bundle.ManifestJson;
	}
	std::cout << "Core AOT generation passed\n";
	return 0;
}
