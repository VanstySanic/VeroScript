// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptPackage.h"
#include "VeroScriptTestTemporaryDirectory.h"
#include "VeroScriptCodegen.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptAst.h"
#include "VeroScriptVariant.h"
#include "../VeroScriptHotReloadInternal.h"
#include <iostream>
#include <sstream>
#include <fstream>

namespace
{
	std::string BackendPath;
	bool Run(const char* Label, const char* Source, const char* Expected, const int ExpectedExitCode = 0,
	    const bool bControlled = false, const char* ExpectedRuntimeFailure = nullptr,
	    const std::size_t MaxCallDepth = 0, const char* MutationField = nullptr,
	    const std::size_t ExpectedMutations = 0, const std::vector<std::string_view>& ScriptArgs = {})
	{
		auto Result = VeroScript::VeroScriptCompile::CompileSource(Source, "special-members.vs");
		if (!Result.bSuccess || !Result.Module)
		{
			std::cerr << Label << ": compile: " << Result.Error.Message << '\n';
			return false;
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 123, Bytes, Error))
		{
			std::cerr << Label << ": serialize: " << Error << '\n';
			return false;
		}
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (BackendPath.empty() && Backend != VeroScript::EExecutionBackend::Vm)
			{
				continue;
			}
			auto Module = VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(VeroScript::FByteView(Bytes), 123, Error);
			if (!Module)
			{
				std::cerr << Label << ": deserialize: " << Error << '\n';
				return false;
			}
			if (Backend == VeroScript::EExecutionBackend::Mixed || BackendPath.empty())
			{
				VeroScript::FPackageBuildOptions Options;
				Options.PackageName = "special-members";
				VeroScript::FPackageData Package;
				std::vector<std::uint8_t> PackageBytes;
				if (!VeroScript::BuildPackage(*Module, Options, PackageBytes, Error) || !VeroScript::ReadPackage(VeroScript::FByteView(PackageBytes), Package, Error))
				{
					std::cerr << Label << ": package: " << Error << '\n';
					return false;
				}
				Module = std::move(Package.Module);
			}
			std::ostringstream Out, Errors;
			std::istringstream Input;
			std::size_t Mutations = 0;
			VeroScript::VeroScriptVm Vm(Out, Input, Errors);
			VeroScript::Test::Configure(Vm);
			if (MutationField)
			{
				Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
				{
					Mutations += Event.FieldName == MutationField && Event.Kind == VeroScript::EMutationKind::ContainerSet ? 1u : 0u;
				});
			}
			Vm.SetExecutionBackend(Backend);
			if (MaxCallDepth != 0)
			{
				Vm.SetMaxCallDepth(MaxCallDepth);
			}
			if (bControlled)
			{
				VeroScript::FExecutionLimits Limits;
				Limits.MaxInstructions = 1000000;
				Vm.SetExecutionLimits(Limits);
			}
			if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error))
			{
				std::cerr << Label << ": backend: " << Error << '\n';
				return false;
			}
			const bool bExecuted = Vm.Execute(*Module, VeroScript::ERunMode::Script, ScriptArgs);
			if (ExpectedRuntimeFailure)
			{
				if (bExecuted || !Vm.HasError() || Vm.GetError().Message.find(ExpectedRuntimeFailure) == std::string::npos)
				{
					std::cerr << Label << ": expected runtime failure [" << ExpectedRuntimeFailure << "] got [" << Vm.GetError().Message << "]\n";
					return false;
				}
			}
			else if (!bExecuted)
			{
				std::cerr << Label << ": run: " << Vm.GetError().Message << '\n';
				return false;
			}
			if (Out.str() != Expected || Vm.GetProcessExitCode() != ExpectedExitCode)
			{
				std::cerr << Label << ": expected [" << Expected << "] got [" << Out.str() << "] exit=" << Vm.GetProcessExitCode() << '\n';
				return false;
			}
			if (MutationField && Mutations != ExpectedMutations)
			{
				std::cerr << Label << ": expected " << ExpectedMutations << " container mutations, got " << Mutations << '\n';
				return false;
			}
		}
		return true;
	}
	bool Reject(const char* Label, const char* Source)
	{
		auto Result = VeroScript::VeroScriptCompile::CompileSource(Source, "special-members.vs");
		if (Result.bSuccess)
		{
			std::cerr << Label << ": unexpectedly compiled\n";
			return false;
		}
		return true;
	}
	bool HostServices()
	{
		auto Result = VeroScript::VeroScriptCompile::CompileSource(R"(
int init(){print("init");return 4;}
struct R{int x=init();R()=default;explicit R(const R& o)=default;R& operator=(const R& o)=default;};
struct S{int x=2;S()=default;S(const S& o)=delete;void operator=(const S& o){x=o.x+1;}};
struct H{S value=S();};int read(const S& s){return s.x;}
void main(){}
)",
		    "host-special-members.vs");
		if (!Result.bSuccess)
		{
			std::cerr << "host services compile: " << Result.Error.Message << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VeroScript::FObjectHandle Original, Copy, Source, Holder;
		bool Ok = Vm.ConstructObject(*Result.Module, "R", {}, Original, nullptr) && Vm.ConstructObject(*Result.Module, "R", {Original}, Copy, nullptr) && Copy && Copy != Original && Output.str() == "init\n" && Vm.ConstructObject(*Result.Module, "S", {}, Source, nullptr) && Vm.ConstructObject(*Result.Module, "H", {}, Holder, nullptr);
		if (!Ok)
		{
			std::cerr << "host construction: " << Vm.GetError().Message << '\n';
			return false;
		}
		const auto Identity = std::get<VeroScript::FObjectHandle>(Holder->Fields.at(0));
		Ok = Vm.WriteObjectField(*Result.Module, Holder, "value", Source) && Identity == std::get<VeroScript::FObjectHandle>(Holder->Fields.at(0)) && std::get<std::int32_t>(Identity->Fields.at(0)) == 3;
		Ok = Ok && Vm.WriteObjectFields(*Result.Module, Holder, {{"value", Source}}) && Identity == std::get<VeroScript::FObjectHandle>(Holder->Fields.at(0));
		VeroScript::FValue Return;
		Ok = Ok && Vm.ExecuteFreeFunction(*Result.Module, "read", {Source}, nullptr, nullptr, &Return) && std::get<std::int32_t>(Return) == 2;
		if (!Ok)
		{
			std::cerr << "host assignment/ref call: " << Vm.GetError().Message << '\n';
		}
		return Ok;
	}
	bool HostDefaultConstruction()
	{
		auto Result = VeroScript::VeroScriptCompile::CompileSource(R"(
struct S{int value=0;S(){value=42;}};struct Holder{S member;};
struct Base{int value=0;Base(){value=42;}};struct Derived:Base{};
struct Deleted{Deleted()=delete;Deleted(int input){}};
struct DeletedField{Deleted member;};struct DeletedBase:Deleted{};
struct NoDefault{NoDefault(int input){}};
struct NoDefaultField{NoDefault member;};struct NoDefaultBase:NoDefault{};
void main(){}
)",
		    "host-default-construction.vs");
		if (!Result.bSuccess)
		{
			std::cerr << "host default construction compile: " << Result.Error.Message << '\n';
			return false;
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 123, Bytes, Error))
		{
			std::cerr << "host default construction serialize: " << Error << '\n';
			return false;
		}
		auto Module = VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(VeroScript::FByteView(Bytes), 123, Error);
		if (!Module)
		{
			std::cerr << "host default construction deserialize: " << Error << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VeroScript::FObjectHandle Holder, Derived;
		if (!Vm.ConstructObject(*Module, "Holder", {}, Holder, nullptr) || !Vm.ConstructObject(*Module, "Derived", {}, Derived, nullptr) || !Holder || !Derived || Holder->Fields.empty() || Derived->Fields.empty())
		{
			std::cerr << "host default construction: " << Vm.GetError().Message << '\n';
			return false;
		}
		const auto* Member = std::get_if<VeroScript::FObjectHandle>(&Holder->Fields.front());
		if (!Member || !*Member || (*Member)->Fields.empty() || std::get_if<std::int32_t>(&(*Member)->Fields.front()) == nullptr || std::get<std::int32_t>((*Member)->Fields.front()) != 42 || std::get_if<std::int32_t>(&Derived->Fields.front()) == nullptr || std::get<std::int32_t>(Derived->Fields.front()) != 42)
		{
			std::cerr << "host default construction skipped a field/base constructor\n";
			return false;
		}
		for (const char* Type : {"Deleted", "DeletedField", "DeletedBase", "NoDefault", "NoDefaultField", "NoDefaultBase"})
		{
			VeroScript::FObjectHandle Rejected;
			if (Vm.ConstructObject(*Module, Type, {}, Rejected, nullptr) || Rejected || !Vm.HasError())
			{
				std::cerr << "host default construction bypassed an unavailable constructor: " << Type << '\n';
				return false;
			}
		}
		return true;
	}
	bool ConstructorMutationCallbacks()
	{
		auto Result = VeroScript::VeroScriptCompile::CompileSource(R"(
class Box{public:int Value=3;array<int> Items=array<int>();Box(){Value=7;Items.push(1);}};
void main(){Box value=Box();assert(value.Value==7 && len(value.Items)==1);}
)",
		    "constructor-mutation.vs");
		if (!Result.bSuccess)
		{
			std::cerr << "constructor mutation compile: " << Result.Error.Message << '\n';
			return false;
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 123, Bytes, Error))
		{
			std::cerr << "constructor mutation serialize: " << Error << '\n';
			return false;
		}
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (BackendPath.empty() && Backend != VeroScript::EExecutionBackend::Vm)
			{
				continue;
			}
			auto Module = VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(VeroScript::FByteView(Bytes), 123, Error);
			if (!Module)
			{
				std::cerr << "constructor mutation deserialize: " << Error << '\n';
				return false;
			}
			std::ostringstream Output;
			VeroScript::VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error))
			{
				std::cerr << "constructor mutation backend: " << Error << '\n';
				return false;
			}
			std::uint32_t ValueWrites = 0, ItemWrites = 0, ItemAdds = 0;
			Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
			{
				ValueWrites += Event.FieldName == "Value" && Event.Kind == VeroScript::EMutationKind::FieldWrite ? 1u : 0u;
				ItemWrites += Event.FieldName == "Items" && Event.Kind == VeroScript::EMutationKind::FieldWrite ? 1u : 0u;
				ItemAdds += Event.FieldName == "Items" && Event.Kind == VeroScript::EMutationKind::ContainerAdd ? 1u : 0u;
			});
			if (!Vm.Execute(*Module, VeroScript::ERunMode::Script) || ValueWrites != 1 || ItemWrites != 0 || ItemAdds != 1)
			{
				std::cerr << "constructor mutation must keep body writes and container origins: " << Vm.GetError().Message
				          << " values=" << ValueWrites << " fields=" << ItemWrites << " adds=" << ItemAdds << '\n';
				return false;
			}
		}
		return true;
	}
	bool MultipleInheritanceConstruction()
	{
		const std::string Declarations = R"(
bool failA=false;bool failB=false;bool failD=false;int cleanup=0;
struct Token{int value;Token(int input){value=input;}~Token(){print("field{}",value);}};
class Ancestor{public:Token root=Token(10);Ancestor(){}~Ancestor(){print("R{}",root.value);}};
class A:Ancestor{public:Token left=Token(20);A(){if(failA)throw 6;}~A(){print("A{}",left.value);}};
class B{public:Token right=Token(30);B(){if(failB)throw 7;}~B(){print("B{}",right.value);if(cleanup==1)throw 55;if(cleanup==2)exit(7);}};
class D:A,B{public:Token own=Token(40);D(){if(failD)throw 9;}~D(){print("D{}",own.value);}};
void work(){D value=D();print("ready");}
)";
		struct FCase
		{
			const char* Label;
			const char* Main;
			const char* Expected;
			int ExitCode = 0;
		};
		const FCase Cases[] =
		    {
		        {"multiple inherited destructors retain their own fields",
		            "void main(){work();print(\"end\");}",
		            "ready\nD40\nfield40\nB30\nfield30\nA20\nfield20\nR10\nfield10\nend\n"},
		        {"failed derived construction preserves completed base field slices",
		            "void main(){failD=true;try{work();}catch(int error){print(\"caught{}\",error);}print(\"end\");}",
		            "field40\nB30\nfield30\nA20\nfield20\nR10\nfield10\ncaught9\nend\n"},
		        {"failed first base releases its fields before its completed ancestor",
		            "void main(){failA=true;try{work();}catch(int error){print(\"caught{}\",error);}print(\"end\");}",
		            "field20\nR10\nfield10\ncaught6\nend\n"},
		        {"failed later base releases its fields before completed sibling bases",
		            "void main(){failB=true;try{work();}catch(int error){print(\"caught{}\",error);}print(\"end\");}",
		            "field30\nA20\nfield20\nR10\nfield10\ncaught7\nend\n"},
		        {"multiple inherited cleanup throws retain field destruction order",
		            "void main(){failD=true;cleanup=1;try{work();}catch(int error){print(\"caught{}\",error);}print(\"end\");}",
		            "field40\nB30\nfield30\nA20\nfield20\nR10\nfield10\ncaught55\nend\n"},
		        {"multiple inherited cleanup exit drains completed bases",
		            "void main(){failD=true;cleanup=2;work();print(\"after\");}",
		            "field40\nB30\nfield30\nA20\nfield20\nR10\nfield10\n", 7},
		};
		for (const auto& Case : Cases)
		{
			const auto Source = Declarations + Case.Main;
			if (!Run(Case.Label, Source.c_str(), Case.Expected, Case.ExitCode)) return false;
		}
		return true;
	}
	bool ControlledCopy()
	{
		auto Result = VeroScript::VeroScriptCompile::CompileSource(R"(
struct S{S()=default;S(const S& o){while(true){}}};
struct R{S value=S();R()=default;R(const R& o)=default;};
void main(){R a=R();R b=a;}
)",
		    "controlled-copy.vs");
		if (!Result.bSuccess)
		{
			std::cerr << "controlled copy compile: " << Result.Error.Message << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		VeroScript::FExecutionLimits Limits;
		Limits.MaxInstructions = 2000;
		Vm.SetExecutionLimits(Limits);
		if (Vm.Execute(*Result.Module, VeroScript::ERunMode::Script) || !Vm.HasError())
		{
			std::cerr << "copy constructor bypassed execution limits\n";
			return false;
		}
		std::string Error;
		std::vector<std::uint8_t> Bytes;
		for (auto& Function : Result.Module->Functions)
		{
			if (Function.SpecialMemberKind == VeroScript::ESpecialMemberKind::CopyConstructor && Function.bDefaulted)
			{
				Function.SpecialMemberKind = VeroScript::ESpecialMemberKind::Equality;
				if (VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 123, Bytes, Error))
				{
					std::cerr << "contradictory copy metadata serialized\n";
					return false;
				}
				return true;
			}
		}
		return false;
	}
	bool InterruptedCopyHookPlanning()
	{
		using namespace VeroScript;
		std::string Source = "int copies=0;int finished=-1;";
		for (int Index = 0; Index < 96; ++Index)
		{
			Source += "struct Dummy" + std::to_string(Index) + "{int value;};";
		}
		Source +=
		    "struct Item{int value=7;Item()=default;Item(const ref Item other){copies+=1;value=other.value;}};"
		    "Item original=Item();map<int,Item> values=map<int,Item>();"
		    "void exercise(){try{values[1]=original;}finally{values[2]=original;finished=copies;}}"
		    "int read_finished(){return finished;}"
		    "int copy_again(){map<int,Item> fresh=map<int,Item>();fresh[3]=original;return copies;}"
		    "void main(){}";
		auto Compilation = VeroScriptCompile::CompileSource(Source, "interrupted-copy-hook-plan.vs");
		if (!Compilation.bSuccess)
		{
			std::cerr << "copy hook interruption compile: " << Compilation.Error.Message << '\n';
			return false;
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (BackendPath.empty() && Backend != EExecutionBackend::Vm)
			{
				continue;
			}
			bool SawInterruptedCopy = false;
			// Sweep the declaration-plan work rather than depend on one bytecode
			// offset. Interruptions inside an already-running finally require reset.
			for (std::uint64_t Quota = 1; Quota <= Compilation.Module->Types.size() + 24; ++Quota)
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				Test::Configure(Vm);
				Vm.SetExecutionBackend(Backend);
				std::string Error;
				const auto Fail = [&](const char* Stage)
				{
					std::cerr << "interrupted copy plan backend=" << static_cast<int>(Backend)
					          << " quota=" << Quota << " stage=" << Stage << " error=" << Vm.GetError().Message
					          << " backendError=" << Error << '\n';
					return false;
				};
				if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error)) return Fail("backend");
				FHotReloadState State;
				if (!Vm.ExecutePersistent(*Compilation.Module, ERunMode::Script, State)) return Fail("initialize");
				Vm.SetExecutionLimits({Quota, 0});
				const bool Succeeded = Vm.ExecuteFreeFunction(*Compilation.Module, "exercise", {}, &State);
				const auto Failure = Vm.GetError();
				const bool Reset = Vm.RequiresReset();
				Vm.SetExecutionLimits({0, 0});
				if (!Succeeded && !Reset)
				{
					FValue Finished = std::int32_t{-1}, CopiedAgain = std::int32_t{-1};
					if (Failure.MessageId != "diag.runtime_execution_instruction_limit_exceeded" ||
						!Vm.ExecuteFreeFunction(*Compilation.Module, "read_finished", {}, &State, nullptr, &Finished))
					{
						return Fail("read cleanup");
					}
					const auto Marker = std::get<std::int32_t>(Finished);
					if (Marker < 1)
					{
						std::cerr << "interrupted copy hook plan skipped finally's copy constructor at quota " << Quota << '\n';
						return false;
					}
					SawInterruptedCopy |= Marker == 1;
					if (!Vm.ExecuteFreeFunction(*Compilation.Module, "copy_again", {}, &State, nullptr, &CopiedAgain) ||
						std::get<std::int32_t>(CopiedAgain) != Marker + 1 || Vm.RequiresReset())
					{
						return Fail("copy after cleanup");
					}
				}
				if (!Reset && !Vm.DiscardPersistentState(*Compilation.Module, State)) return Fail("discard");
			}
			if (!SawInterruptedCopy)
			{
				std::cerr << "interrupted copy plan never reached cleanup before its first hook, backend=" << static_cast<int>(Backend) << '\n';
				return false;
			}
		}
		return true;
	}
	bool ReentrantCopyHookPlanning()
	{
		using namespace VeroScript;
		std::string Source = "int copies=0;int finished=-1;";
		for (int Index = 0; Index < 32; ++Index)
		{
			Source += "struct Dummy" + std::to_string(Index) + "{int value;};";
		}
		Source +=
		    "struct Item{int value=7;Item()=default;Item(const ref Item other){copies+=1;value=other.value;}};"
		    "struct Aggregate{Item child;};Aggregate original=Aggregate();map<int,Aggregate> values=map<int,Aggregate>();"
		    "void exercise(){try{values[1]=original;}finally{values[2]=original;finished=copies;}}"
		    "int nested_copy(){map<int,Aggregate> fresh=map<int,Aggregate>();fresh[3]=original;return copies;}"
		    "int read_finished(){return finished;}void main(){}";
		auto Compilation = VeroScriptCompile::CompileSource(Source, "reentrant-copy-hook-plan.vs");
		if (!Compilation.bSuccess)
		{
			std::cerr << "reentrant copy plan compile: " << Compilation.Error.Message << '\n';
			return false;
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (BackendPath.empty() && Backend != EExecutionBackend::Vm)
			{
				continue;
			}
			bool SawInterruptedCopy = false;
			for (std::size_t CancelAt = Compilation.Module->Types.size(); CancelAt < Compilation.Module->Types.size() + 25; ++CancelAt)
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				Test::Configure(Vm);
				Vm.SetExecutionBackend(Backend);
				std::string Error;
				const auto Fail = [&](const char* Stage)
				{
					std::cerr << "reentrant copy plan backend=" << static_cast<int>(Backend)
					          << " cancelAt=" << CancelAt << " stage=" << Stage << " error=" << Vm.GetError().Message
					          << " backendError=" << Error << '\n';
					return false;
				};
				if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error)) return Fail("backend");
				FHotReloadState State;
				if (!Vm.ExecutePersistent(*Compilation.Module, ERunMode::Script, State)) return Fail("initialize");
				std::size_t Polls = 0;
				bool InsideNested = false, NestedOk = false, Cancelled = false;
				Vm.SetExecutionCheck([&]
				{
					if (InsideNested) return true;
					// JIT preparation also polls before there is an active script call.
					// InvokeFunction reentry requires the actual execution frame.
					if (Vm.DebugGetCallDepth() == 0) return true;
					if (++Polls == 8)
					{
						InsideNested = true;
						std::vector<FValue> Arguments;
						FValue Returned;
						std::string Failure;
						NestedOk = Vm.InvokeFunction("nested_copy", Arguments, Returned, Failure) &&
						    std::get<std::int32_t>(Returned) == 1;
						InsideNested = false;
					}
					if (Polls == CancelAt)
					{
						Cancelled = true;
						return false;
					}
					return true;
				});
				const bool Succeeded = Vm.ExecuteFreeFunction(*Compilation.Module, "exercise", {}, &State);
				const auto Failure = Vm.GetError();
				const bool Reset = Vm.RequiresReset();
				Vm.SetExecutionCheck({});
				if (!Succeeded && Cancelled && NestedOk && !Reset)
				{
					FValue Finished = std::int32_t{-1}, CopiedAgain = std::int32_t{-1};
					if (Failure.MessageId != "diag.execution_cancelled_by_host" ||
						!Vm.ExecuteFreeFunction(*Compilation.Module, "read_finished", {}, &State, nullptr, &Finished))
					{
						return Fail("read cleanup");
					}
					const auto Marker = std::get<std::int32_t>(Finished);
					if (Marker < 2)
					{
						std::cerr << "reentrant copy hook plan skipped Aggregate's member hook at poll " << CancelAt << '\n';
						return false;
					}
					SawInterruptedCopy = true;
					if (!Vm.ExecuteFreeFunction(*Compilation.Module, "nested_copy", {}, &State, nullptr, &CopiedAgain) ||
						std::get<std::int32_t>(CopiedAgain) != Marker + 1 || Vm.RequiresReset())
					{
						return Fail("copy after cleanup");
					}
				}
				if (!Reset && !Vm.DiscardPersistentState(*Compilation.Module, State)) return Fail("discard");
			}
			if (!SawInterruptedCopy)
			{
				std::cerr << "reentrant copy plan never reached recoverable cancellation after nested copy, backend=" << static_cast<int>(Backend) << '\n';
				return false;
			}
		}
		return true;
	}
	bool CrossPackage()
	{
		VeroScript::Tests::FScopedTemporaryDirectory Directory("veroscript-defaulted-");
		if (!Directory) return false;
		auto Library = VeroScript::VeroScriptCompile::CompileSource(R"(
export struct R<T>{T value;R()=default;R(const R<T>& other)=default;R<T>& operator=(const R<T>& other)=default;bool operator==(const R<T>& other)const=default;};
export struct Closed{int value=7;Closed()=default;Closed(const Closed& other)=default;Closed& operator=(const Closed& other)=default;};
export Closed make(){return Closed();}
)",
		    (Directory.Get() / "library.vs").generic_string());
		if (!Library.bSuccess)
		{
			std::cerr << "library compile: " << Library.Error.Message << '\n';
			return false;
		}
		VeroScript::FPackageBuildOptions Options;
		Options.Kind = VeroScript::EPackageKind::Library;
		Options.PackageName = "special.members";
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScript::BuildPackage(*Library.Module, Options, Bytes, Error))
		{
			std::cerr << "library package: " << Error << '\n';
			return false;
		}
		{
			std::ofstream File(Directory.Get() / "library.vslib", std::ios::binary);
			File.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			if (!File) return false;
		}
		auto Consumer = VeroScript::VeroScriptCompile::CompileSource(R"(
import "library.vslib";
void main(){R<int> a=R<int>();a.value=9;R<int> b=a;b=a;Closed c=make();Closed d=c;d=c;print("{} {} {}",b.value,a==b,d.value);}
)",
		    (Directory.Get() / "main.vs").generic_string());
		if (!Consumer.bSuccess)
		{
			std::cerr << "library consumer: " << Consumer.Error.Message << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		const bool Ok = Vm.Execute(*Consumer.Module, VeroScript::ERunMode::Script) && Output.str() == "9 true 7\n";
		if (!Ok)
		{
			std::cerr << "library execution: " << Vm.GetError().Message << " output: " << Output.str() << '\n';
		}
		return Ok;
	}
	bool ContextFreeVariantSnapshot()
	{
		for (const bool HasUserCopy : {false, true})
		{
			VeroScript::FValue Box;
			{
				const char* Source = HasUserCopy
				    ? "struct R{int x=7;R()=default;R(const R& o){x=o.x;}};variant make(){R a=R();return a;}"
				    : "struct R{int x=7;};variant make(){return R();}";
				auto Result = VeroScript::VeroScriptCompile::CompileSource(Source, "variant-snapshot.vs");
				if (!Result.bSuccess)
				{
					std::cerr << "snapshot compile: " << Result.Error.Message << '\n';
					return false;
				}
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				if (!Vm.ExecuteFreeFunction(*Result.Module, "make", {}, nullptr, nullptr, &Box))
				{
					std::cerr << "snapshot make: " << Vm.GetError().Message << '\n';
					return false;
				}
			}
			// Neither the VM nor its module survives. A snapshot must not inspect
			// an object's non-owning RuntimeTypeModule pointer.
			VeroScript::FValue Snapshot = std::int32_t(99);
			std::string Error;
			const bool Copied = VeroScript::Variant::Snapshot(Box, Snapshot, Error);
			if (HasUserCopy)
			{
				if (Copied || Error.find("owning VM/session") == std::string::npos || !std::holds_alternative<std::int32_t>(Snapshot) || std::get<std::int32_t>(Snapshot) != 99)
				{
					std::cerr << "snapshot bypassed a user copy without its module\n";
					return false;
				}
			}
			else
			{
				const auto* Object = std::get_if<VeroScript::FObjectHandle>(&Snapshot);
				if (!Copied || !Object || !*Object || std::get<std::int32_t>((*Object)->Fields.at(0)) != 7)
				{
					std::cerr << "trivial snapshot after module release: " << Error << '\n';
					return false;
				}
			}
		}
		return true;
	}
	bool ReentrantContainerCopy()
	{
		for (const auto& Source : {
		         "struct R{int x=7;R()=default;R(const R& other){x=other.x;if(change){change=false;source.clear();}}};map<string,R> source=map<string,R>();bool change=false;void main(){R value=R();change=true;source[\"a\"]=value;}",
		         "struct R{R()=default;void operator=(const R& other){source.clear();}};map<int64,R> source=map<int64,R>();void main(){source[1i64]=R();source[1i64]=R();}",
		         "struct R{R()=default;R(const R& other){if(change){change=false;source[2i64]=R();}}};ordered_map<int64,R> source=ordered_map<int64,R>();bool change=false;void main(){R value=R();change=true;source[1i64]=value;}",
		         "struct R{R()=default;R(const R& other){if(change){change=false;source.clear();}}};map<int,R> source=map<int,R>();bool change=false;void main(){source[1]=R();source[2]=R();change=true;auto values=source.values();}",
		         "struct R{R()=default;R(const R& other){if(change){change=false;source[3]=R();}}};ordered_map<int,R> source=ordered_map<int,R>();bool change=false;void main(){source[1]=R();source[2]=R();change=true;auto values=source.entries();}",
		         "struct R{R()=default;R(const R& other){if(change){change=false;source.push(R());}}};queue<R> source=queue<R>();bool change=false;void main(){source.push(R());source.push(R());change=true;auto values=source.values();}"})
		{
			auto Result = VeroScript::VeroScriptCompile::CompileSource(Source, "container-copy.vs");
			if (!Result.bSuccess)
			{
				std::cerr << "container copy compile: " << Result.Error.Message << '\n';
				return false;
			}
			for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
			{
				if (BackendPath.empty() && Backend != VeroScript::EExecutionBackend::Vm)
				{
					continue;
				}
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Vm.SetExecutionBackend(Backend);
				std::string Error;
				if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error)) return false;
				if (Vm.Execute(*Result.Module, VeroScript::ERunMode::Script) || Vm.GetError().Message.find("while its values are being copied") == std::string::npos)
				{
					std::cerr << "container copy did not reject invalidating reentry: " << Vm.GetError().Message << '\n';
					return false;
				}
			}
		}
		return true;
	}
	bool StandaloneSourceExport()
	{
		const std::string Source = "struct R{R(const R& other)=default;};void main(){}";
		VeroScript::VeroScriptLexer Lexer(Source, "source-export.vs");
		const auto& Tokens = Lexer.Tokenize();
		VeroScript::VeroScriptParser Parser(Tokens, "source-export.vs");
		auto Program = Parser.ParseProgram();
		if (!Program || Lexer.HasError() || Parser.HasError()) return false;
		std::string Error;
		auto Prepared = VeroScript::PrepareCodegenProgram(std::move(Program), {}, nullptr, Error);
		if (Prepared || Error.find("Core AOT") == std::string::npos)
		{
			std::cerr << "unsupported standalone source export was not rejected: " << Error << '\n';
			return false;
		}
		return true;
	}
}
int main(int Argc, char** Argv)
{
	if (Argc > 1)
	{
		BackendPath = Argv[1];
	}
	bool Ok = true;
	Ok &= HostServices();
	Ok &= HostDefaultConstruction();
	Ok &= ConstructorMutationCallbacks();
	Ok &= MultipleInheritanceConstruction();
	Ok &= InterruptedCopyHookPlanning();
	Ok &= ReentrantCopyHookPlanning();
	const char* ScalarDefaultConstruction = R"(
class Parent{public:int count=31;bool enabled=true;};
class Child:public Parent{public:double rate=1.5;};
void main(){Child first=Child();Child second=Child();first.count=2;
assert(first.enabled && second.rate==1.5);print(second.count);}
)";
	Ok &= Run("scalar inherited defaults keep independent instances", ScalarDefaultConstruction, "31\n");
	Ok &= Run("scalar inherited defaults preserve the constructor depth limit",
	    ScalarDefaultConstruction, "", 0, false, "call stack overflow", 2);
	Ok &= Run("copy constructor exit stops value construction", R"(
struct S{S()=default;S(const S& source){print("copy");exit(7);}};
void main(){S original=S();S copy=original;print("after");}
)",
	    "copy\n", 7);
	Ok &= Run("copy constructor exit stops defaulted base construction", R"(
struct S{S()=default;S(const S& source){print("copy");exit(7);}};
struct B{S member;B()=default;B(const B& source)=default;};
struct D:B{D()=default;D(const D& source)=default;};
void main(){D original=D();D copy=original;print("after");}
)",
	    "copy\n", 7);
	Ok &= Run("copy constructor exit stops optional payload construction", R"(
bool stop=false;struct S{S()=default;S(const S& source){if(stop){print("copy");exit(7);}}};
void main(){optional<S> original=optional<S>(S());stop=true;optional<S> copy=original;print("after");}
)",
	    "copy\n", 7);
	Ok &= ControlledCopy();
	Ok &= CrossPackage();
	Ok &= StandaloneSourceExport();
	Ok &= ContextFreeVariantSnapshot();
	Ok &= ReentrantContainerCopy();
	Ok &= Run("implicit value subobjects run their default constructors", R"(
int constructed=0;struct S{int value=0;S(){constructed+=1;value=42;}};
struct H{S member;};
void main(){H holder=H();print("{} {}",constructed,holder.member.value);
S fixed[2];print("{} {} {}",constructed,fixed[0].value,fixed[1].value);
handle<S> boxed=handle<S>();print("{} {}",constructed,boxed.value.value);}
)",
	    "1 42\n3 42 42\n4 42\n");
	Ok &= Run("declaration field initializer does not capture a constructor parameter", R"(
int seed=9;struct S{int value=seed;S(int seed){}}
void main(){S value=S(3);print(value.value);}
)",
	    "9\n");
	Ok &= Run("declaration field initializers keep namespace and generic bindings", R"(
int seed=40;
namespace N{int seed=9;using Number=int;
struct S<T>{T value=seed;Number alias=seed;T samples[2]={seed,seed+1};
 map<int,T> entries={seed:seed+2};T selected;
 S(T seed):selected(seed){selected+=seed;}};}
void main(){N::S<int> value=N::S<int>(3);
 print("{} {} {} {} {}",value.value,value.alias,value.samples[1],value.entries[9],value.selected);}
)",
	    "9 9 10 11 6\n");
	Ok &= Reject("implicit member cannot bypass deleted default construction", R"(
struct S{S()=delete;S(int value){}};struct H{S member;};void main(){H holder=H();}
)");
	Ok &= Reject("native array cannot bypass deleted default construction", R"(
struct S{S()=delete;S(int value){}};void main(){S fixed[1];}
)");
	Ok &= Reject("default handle cannot bypass deleted payload construction", R"(
struct S{S()=delete;S(int value){}};void main(){handle<S> boxed=handle<S>();}
)");
	Ok &= Run("unused field declarations do not require default construction", R"(
struct S{S()=delete;S(int value){}};struct H{S member;};void main(){}
)",
	    "");
	Ok &= Run("explicit field construction can bypass a deleted default overload", R"(
struct S{int value;S()=delete;S(int input){value=input;}};
struct H{S member;H():member(7){}};void main(){H holder=H();print(holder.member.value);}
)",
	    "7\n");
	Ok &= Run("member initializer constructs only its final subobject", R"(
struct S{int value=-1;S(int x){value=x;print("C{}",value);}~S(){print("D{}",value);}};
struct H{S member;H():member(7){print("body");}};
void main(){H holder=H();print(holder.member.value);}
)",
	    "C7\nbody\n7\nD7\n");
	Ok &= Run("native array initializer constructs only final elements", R"(
struct S{int value=-1;S(int x){value=x;print("C{}",value);}~S(){print("D{}",value);}};
void main(){S fixed[2]={S(7),S(8)};print("{} {}",fixed[0].value,fixed[1].value);}
)",
	    "C7\nC8\n7 8\nD8\nD7\n");
	Ok &= Run("native array default tail follows explicit elements", R"(
struct S{int value=-1;S(){value=0;print("C0");}S(int x){value=x;print("C{}",value);}~S(){print("D{}",value);}};
void main(){S fixed[2]={S(7)};print("{} {}",fixed[0].value,fixed[1].value);}
)",
	    "C7\nC0\n7 0\nD0\nD7\n");
	Ok &= Run("failed native array list releases completed elements before catch", R"(
struct S{int value=-1;S(int x){if(x==2)throw 9;value=x;}~S(){print(value);}};
void main(){try{S values[2]={S(1),S(2)};}catch(int error){print("caught");}print("end");}
)",
	    "1\ncaught\nend\n");
	Ok &= Run("handle payload initialization does not require assignment", R"(
struct S{int value=0;S(int x){value=x;}S(const S& source)=default;S& operator=(const S& source)=delete;};
void main(){S source=S(7);handle<S> boxed=handle<S>(source);print(boxed.value.value);}
)",
	    "7\n");
	Ok &= Run("readonly payload initialization does not require default construction or assignment", R"(
struct S{int value;S()=delete;S(int x){value=x;}S(const S& source)=default;S& operator=(const S& source)=delete;};
void main(){S source=S(7);readonly<S> frozen=readonly<S>(source);print(frozen.get().value);}
)",
	    "7\n");
	Ok &= Run("empty optional payloads are never constructed or destroyed", R"(
int constructed=0;int destroyed=0;struct S{S(){constructed+=1;}S(const S& source){constructed+=1;}~S(){destroyed+=1;}};
void discard(){optional<S> empty=optional<S>();optional<S> copy=empty;copy=empty;}
void main(){discard();print("{} {}",constructed,destroyed);}
)",
	    "0 0\n");
	Ok &= Run("optional payload initialization does not require default construction or assignment", R"(
struct S{int value;S()=delete;S(int x){value=x;}S(const S& source)=default;S& operator=(const S& source)=delete;};
void main(){optional<S> value=optional<S>(S(7));optional<S> copy=value;print(copy.get().value);
copy.reset();assert(!copy.has_value());copy=value;print(copy.get().value);
optional<S> empty=optional<S>();copy=empty;assert(!copy.has_value());copy.reset();empty.reset();
optional<int> number=optional<int>(3);number.reset();assert(!number.has_value());
number=optional<int>(4);assert(number.get()==4);number.reset();assert(!number.has_value());}
)",
	    "7\n7\n");
	Ok &= Run("checked cast empty optional retains unconstructed payloads", R"(
int constructed=0;int destroyed=0;struct S{S(){constructed+=1;}~S(){destroyed+=1;}};
void check(){variant source=3;optional<S> missing=try_cast<S>(source);assert(!missing.has_value());}
void main(){check();print("{} {}",constructed,destroyed);}
)",
	    "0 0\n");
	Ok &= Run("failed optional payload copy does not publish a present value", R"(
bool fail=false;struct S{int value;S(int x){value=x;}S(const S& other){if(fail)throw 9;value=other.value;}};
void main(){optional<S> source=optional<S>(S(7));optional<S> target=optional<S>();fail=true;
try{target=source;}catch(int error){print("caught");}print(target.has_value());}
)",
	    "caught\nfalse\n");
	Ok &= Run("attribute with a nondefault payload retains optional state", R"(
import "attribute.vs";
int constructed=0;struct S{int value;S(int x){value=x;constructed+=1;}};
void main(){std::attribute<S> value=std::attribute<S>();assert(constructed==0);
value.set(S(7));print(value.get().value);value.reset();assert(!value.try_get().has_value());}
)",
	    "7\n");
	Ok &= Run("failed ordinary construction releases only completed subobjects", R"(
struct Member{~Member(){print("field");}};
struct S{Member member=Member();S(){print("construct");throw 9;}~S(){print("complete");}};
void main(){try{S value=S();}catch(int error){print("caught{}",error);}}
)",
	    "construct\nfield\ncaught9\n");
	Ok &= Run("native scalar constructor completes before object destruction", R"(
struct C{int value=0;C(){value=6;}~C(){print("destroyed{}",value);}};
void work(){C value=C();print(value.value);}void main(){work();}
)",
	    "6\ndestroyed6\n");
	Ok &= Run("constructor and destructor virtual dispatch follows the current lifetime phase", R"(
import "attribute.vs";
class B{public:B(){probe(1,"Bctor");}virtual int read(){return 1;}
void probe(int expected,string phase){assert(read()==expected);assert(helper(this)==expected);
fn()->int closure=[=]()->int{return helper(this);};assert(closure()==expected);
type<B> info=type<B>();bool found=false;
for(int i=0;i<info.method_count();++i){method_info<B> method(info.qualified_name,i);
if(method.name()=="read"){assert(method.invoke<int>(this)==expected);found=true;}}
assert(found);string name="read";auto bound=std::bind_method<fn()->int>(this,name);assert(bound()==expected);print(phase);}
~B(){probe(1,"Bdtor");}};
class D:public B{public:string text="ok";D(){probe(2,"Dctor");}void ready(){probe(2,"ready");}
int read()override{assert(text=="ok");return 2;}~D(){probe(2,"Ddtor");}};
int helper(B value){return value.read();}
void work(){D value=D();value.ready();}void main(){work();print("end");}
)",
	    "Bctor\nDctor\nready\nDdtor\nBdtor\nend\n");
	Ok &= Run("pure virtual construction failure releases completed fields", R"(
struct Member{~Member(){print("field");}};
class B{public:Member member;B(){print(helper(this));}virtual int read()=0;~B(){print("B");}};
class D:B{public:int read()override{print("derived");return 2;}~D(){print("D");}};
int helper(B value){return value.read();}
void main(){D value=D();print("after");}
)",
	    "field\n", 0, false, "method has no executable implementation");
	Ok &= Run("constructor locals and member cleanup retain and restore the current virtual phase", R"(
bool failBase=false;weak<B> observed=weak<B>();
struct Probe{~Probe(){B owner=observed.lock();print("local{}",helper(owner));}};
struct Watcher{string name;Watcher(string input):name(input){}~Watcher(){B owner=observed.lock();if(owner!=nullptr)print("{}{}",name,helper(owner));else print("{}expired",name);}};
class B{public:Watcher baseMember=Watcher("b");B(){observed=weak<B>(this);Probe local=Probe();
try{throw 5;}catch(int error){assert(helper(this)==1);}if(failBase)throw 7;}
virtual int read(){return 1;}~B(){print("B{}",helper(this));}};
class D:public B{public:string text="ok";Watcher derivedMember=Watcher("d");D(bool fail){if(fail)throw 9;}
int read()override{assert(text=="ok");return 2;}~D(){print("D{}",helper(this));}};
int helper(B value){return value.read();}
void work(){D value=D(false);print("live{}",helper(value));}
void main(){try{D broken=D(true);}catch(int error){print("caught{}",error);}
failBase=true;try{D broken=D(false);}catch(int error){print("caught{}",error);}failBase=false;
work();print("end");}
)",
	    "local1\ndexpired\nB1\nbexpired\ncaught9\nlocal1\nbexpired\ncaught7\nlocal1\nlive2\nD2\ndexpired\nB1\nbexpired\nend\n");
	Ok &= Run("controlled constructors retain live receivers through normal return", R"(
int constructed=0;
class Single{public:Single(){int sum=0;for(int i=0;i<4;++i)sum+=i;}~Single(){print("single");}};
class Root{public:Root(){constructed+=1;}~Root(){print("root");}};
class Leaf:Root{public:Leaf(){constructed+=1;}~Leaf(){print("leaf");}};
void work(){Single single=Single();Leaf leaf=Leaf();print(constructed);}void main(){work();print("done");}
)",
	    "2\nleaf\nroot\nsingle\ndone\n", 0, true);
	Ok &= Run("native scalar base constructor completes before derived failure", R"(
struct B{int value=0;B(){value=6;}~B(){print("base{}",value);}};
struct D:B{D(){throw 9;}~D(){print("derived");}};
void work(){try{D value=D();}catch(int error){print("caught{}",error);}}void main(){work();}
)",
	    "base6\ncaught9\n");
	Ok &= Run("constructor can catch a nested method exception", R"(
struct S{int value=0;void fail(){throw 9;}S(){try{fail();}catch(int error){value=error;}}~S(){print("D{}",value);}};
void main(){S value=S();print(value.value);}
)",
	    "9\nD9\n");
	Ok &= Run("constructor local cleanup failure releases completed subobjects", R"(
struct Member{~Member(){print("field");}};
struct Bomb{~Bomb(){throw 9;}};
struct S{Member member;S(){Bomb local=Bomb();}~S(){print("complete");}};
void main(){try{S value=S();}catch(int error){print("caught");}print("end");}
)",
	    "field\ncaught\nend\n");
	Ok &= Run("failed derived construction destroys a completed base", R"(
struct B{B(){print("base");}~B(){print("base destroyed");}};
struct D:B{D(){print("derived");throw 9;}~D(){print("derived destroyed");}};
void main(){try{D value=D();}catch(int error){print("caught{}",error);}}
)",
	    "base\nderived\nbase destroyed\ncaught9\n");
	Ok &= Run("failed map insertion publishes no entry", R"(
bool fail=false;struct S{int value=7;S()=default;S(const S& other){if(fail)throw 9;value=other.value;}};
map<string,S> entries=map<string,S>();ordered_map<int64,S> ordered=ordered_map<int64,S>();S source=S();
void insert_map(){entries["a"]=source;}void insert_ordered(){ordered[1i64]=source;}
void main(){int caught=0;fail=true;try{insert_map();}catch(int n){assert(n==9);caught+=1;}
try{insert_ordered();}catch(int n){assert(n==9);caught+=1;}
assert(caught==2 && len(entries)==0 && !entries.has("a") && len(ordered)==0 && len(ordered.keys())==0);
fail=false;insert_map();insert_ordered();assert(entries["a"].value==7 && ordered[1i64].value==7);
assert(len(entries)==1 && len(ordered.keys())==1);}
)",
	    "");
	Ok &= Run("failed insertion preserves all scalar map key kinds", R"(
bool fail=false;struct S{int value=7;S()=default;S(const S& other){if(fail)throw 9;value=other.value;}};
enum class Key{First};
void verify<K>(K key){map<K,S> entries=map<K,S>();ordered_map<K,S> ordered=ordered_map<K,S>();S source=S();
fail=true;int caught=0;try{entries[key]=source;}catch(int n){caught+=1;}
try{ordered[key]=source;}catch(int n){caught+=1;}
assert(caught==2 && len(entries)==0 && len(ordered.keys())==0);
fail=false;entries[key]=source;ordered[key]=source;
assert(entries[key].value==7 && ordered[key].value==7 && len(ordered.keys())==1);}
void main(){verify<char>(cast<char>(1));verify<int>(2);verify<unsigned int>(cast<unsigned int>(3));
verify<int64>(4i64);verify<uint64>(cast<uint64>(5));verify<string>("a");verify<wstring>(L"b");verify<Key>(Key::First);}
)",
	    "");
	Ok &= Run("map access checks the coerced assignment", R"(
struct S{int value=1;S()=default;S(int n){value=n;}
public:void operator=(const S& other){value=other.value;}private:void operator=(int n){value=n;}};
void main(){map<string,S> entries=map<string,S>();ordered_map<int64,S> ordered=ordered_map<int64,S>();
entries["a"]=3;entries["a"]=5;ordered[1i64]=7;ordered[1i64]=9;
assert(entries["a"].value==5 && ordered[1i64].value==9);}
)",
	    "");
	Ok &= Reject("coercion cannot bypass private stored-value assignment", R"(
struct S{S()=default;S(int n){}public:void operator=(int n){}private:void operator=(const S& other){}};
void main(){map<string,S> entries=map<string,S>();entries["a"]=3;}
)");
	Ok &= Reject("map insertion checks copy constructor access", R"(
struct R{R()=default;private:R(const R& other){}};
void main(){map<int,R> entries=map<int,R>();R value=R();entries[1]=value;}
)");
	Ok &= Reject("map assignment checks operator access", R"(
struct R{R()=default;private:void operator=(const R& other){}};
void main(){map<int,R> entries=map<int,R>();entries[1]=R();entries[1]=R();}
)");
	Ok &= Run("owner map assignment retains private access", R"(
struct R{int value=1;R()=default;private:R(const R& other){value=other.value;}
void operator=(const R& other){value=other.value;}public:
static void test(){map<int,R> entries=map<int,R>();R source=R();entries[1]=source;
source.value=8;entries[1]=source;assert(entries[1].value==8);}};
void main(){R::test();}
)",
	    "");
	Ok &= Run("first struct map assignment and overwrite", R"(
struct Entry{int value=2;};
void main(){map<string,Entry> entries=map<string,Entry>();entries["a"]=Entry();
assert(entries["a"].value==2);Entry value=Entry();value.value=9;entries["a"]=value;
value.value=4;assert(entries["a"].value==9 && len(entries)==1);}
)",
	    "");
	Ok &= Run("first weak map assignment and expiration", R"(
class Item{};
void main(){map<int64,weak<Item>> entries=map<int64,weak<Item>>();
Item item=Item();entries[1i64]=weak<Item>(item);assert(!entries[1i64].expired());item=nullptr;
assert(entries[1i64].expired());Item other=Item();entries[1i64]=weak<Item>(other);
assert(!entries[1i64].expired() && len(entries)==1);}
)",
	    "");
	Ok &= Run("first weak global assignment resolves its concrete owner", R"(
class Item{};weak<Item> observed=weak<Item>();
void main(){Item value=Item();observed=weak<Item>(value);print(observed.expired());}
)",
	    "false\n");
	Ok &= Run("first generic struct global assignment resolves its concrete owner", R"(
struct W<T>{T slot;W(){}W(T value){slot=value;}};W<int> observed=W<int>();
void main(){observed=W<int>(5);print(observed.slot);}
)",
	    "5\n");
	Ok &= Run("weak assignment preserves storage and reference return chains", R"(
class Item{public:int value;Item(int input):value(input){}};
weak<Item> global=weak<Item>();
class Holder{public:weak<Item> observer=weak<Item>();void set(Item target){observer=weak<Item>(target);}};
weak<Item>& stored(){return global;}
weak<Item>& assign(ref weak<Item> target,const ref weak<Item> source){return target=source;}
void main(){Item first=Item(7);Item second=Item(9);
weak<Item> local=weak<Item>();local=weak<Item>(first);assert(local.lock()==first);
global=weak<Item>(second);assert(global.lock()==second);
Holder holder=Holder();holder.observer=weak<Item>(first);assert(holder.observer.lock()==first);
holder.set(second);assert(holder.observer.lock()==second);
array<weak<Item>> values=array<weak<Item>>();values.push(weak<Item>());values[0]=weak<Item>(first);assert(values[0].lock()==first);
weak<Item> fixed[1];fixed[0]=weak<Item>(second);assert(fixed[0].lock()==second);
weak<Item> left=weak<Item>();weak<Item> right=weak<Item>();left=right=weak<Item>(first);assert(left.lock()==first && right.lock()==first);
weak<Item>& alias=(stored()=weak<Item>(second));alias.reset();assert(global.expired());
assign(ref local,ref right)=weak<Item>(second);assert(local.lock()==second && right.lock()==first);
first=nullptr;assert(right.expired() && left.expired() && values[0].expired());
second=nullptr;assert(local.expired() && holder.observer.expired() && fixed[0].expired());print("assigned");}
)",
	    "assigned\n");
	Ok &= Run("generic struct assignment preserves storage and reference return chains", R"(
struct W<T>{T slot;W(){}W(T value){slot=value;}};
W<int> global=W<int>();
class Holder{public:W<int> member;void set(int value){member=W<int>(value);}};
W<int>& stored(){return global;}
W<int>& assign(ref W<int> target,const ref W<int> source){return target=source;}
void main(){W<int> local=W<int>();local=W<int>(7);assert(local.slot==7);
global=W<int>(9);assert(global.slot==9);
Holder holder=Holder();holder.member=W<int>(11);assert(holder.member.slot==11);holder.set(13);assert(holder.member.slot==13);
array<W<int>> values=array<W<int>>();values.push(W<int>());values[0]=W<int>(15);assert(values[0].slot==15);
W<int> fixed[1];fixed[0]=W<int>(17);assert(fixed[0].slot==17);
W<int> left=W<int>();W<int> right=W<int>();left=right=W<int>(19);assert(left.slot==19 && right.slot==19);
W<int>& alias=(stored()=W<int>(21));alias.slot=23;assert(global.slot==23);
assign(ref local,ref right)=W<int>(25);assert(local.slot==25 && right.slot==19);print("assigned");}
)",
	    "assigned\n");
	Ok &= Run("map insertion constructs and overwrite assigns", R"(
int copies=0;int assignments=0;
struct S{int value=7;S()=default;S(const S& other){copies+=1;value=other.value;}
void operator=(const S& other){assignments+=1;value=other.value;}};
void main(){S value=S();map<string,S> a=map<string,S>();ordered_map<int64,S> b=ordered_map<int64,S>();
a["a"]=value;b[1i64]=value;assert(copies==2 && assignments==0);
copies=0;assignments=0;value.value=9;a["a"]=value;b[1i64]=value;
assert(copies==0 && assignments==2 && a["a"].value==9 && b[1i64].value==9);}
)",
	    "");
	Ok &= Run("indexed copy permits reads and unrelated writes", R"(
int observed=0;bool change=false;map<int,int> log=map<int,int>();
struct R{int x=7;R()=default;R(const R& other){x=other.x;if(change){observed=len(source);log[1]=x;}}};
map<string,R> source=map<string,R>();void main(){R value=R();change=true;source["a"]=value;
assert(source["a"].x==7 && observed==0 && log[1]==7);source["b"]=R();assert(len(source)==2);}
)",
	    "");
	Ok &= Run("container copy permits reads and unrelated writes", R"(
struct R{int x=7;R()=default;R(const R& other){x=other.x;if(change){observed=len(source);log.push(x);}}};
map<int,R> source=map<int,R>();array<int> log=array<int>();bool change=false;int observed=0;
void main(){source[1]=R();source[2]=R();change=true;auto values=source.values();print("{} {} {}",len(values),observed,len(log));}
)",
	    "2 2 2\n");
	Ok &= Run("map entries initialize prepared pair fields once", R"(
int defaults=0;int copies=0;struct R{int x=4;R(){defaults+=1;}R(const R& other){copies+=1;x=other.x;}};
void main(){map<int,R> a=map<int,R>();a[1]=R();ordered_map<int,R> b=ordered_map<int,R>();b[1]=R();defaults=0;copies=0;
auto first=a.entries();auto second=b.entries();print("{} {} {} {}",defaults,copies,len(first),len(second));}
)",
	    "0 2 1 1\n");
	Ok &= Run("failed container export destroys completed values", R"(
int copies=0;int destroyed=0;bool fail=false;
struct R{R()=default;R(const R& other){copies+=1;if(fail && copies==2)throw 9;}~R(){destroyed+=1;}};
void main(){map<int,R> source=map<int,R>();source[1]=R();source[2]=R();copies=0;destroyed=0;fail=true;
try{auto values=source.values();}catch(int error){print("caught{}",destroyed);}}
)",
	    "caught1\n");
	Ok &= Run("default operations", R"(
struct R {
 int x=3; string s="a";
 R()=default; R(const R& o)=default;
 R& operator=(const R& o)=default;
 bool operator==(const R& o) const=default;
 bool operator!=(const R& o) const=default;
};
void main(){ R a=R(); R b=a; b.x=7; a=b; print("{} {} {}",a.x,a==b,a!=b); }
)",
	    "7 true false\n");
	Ok &= Run("constructor temporaries have one cleanup owner", R"(
struct R{R(){print("constructed");}~R(){print("destroyed");}};
void main(){R value=R();print("ready");}
)",
	    "constructed\nready\ndestroyed\n");
	Ok &= Run("copy hook and elision", R"(
int copies=0; int initializers=0;
int init(){initializers+=1;return 5;}
struct R {int x=init(); R()=default; R(const R& o){copies+=1;x=o.x;} };
R pass(R v){return v;}
void main(){R a=R();R b=a;R c=pass(R());print("{} {}",copies,initializers);}
)",
	    "2 4\n");
	Ok &= Run("default copy skips initializers", R"(
int n=0; int init(){n+=1;return n;}
struct R {int x=init();R()=default;R(const R& o)=default;};
void main(){R a=R();R b=a;R c=R(a);print("{} {} {}",n,b.x,c.x);}
)",
	    "1 1 1\n");
	Ok &= Run("nested assignment uses assignment", R"(
int assigns=0;
struct S {int x=0;void operator=(const S& o){assigns+=1;x=o.x+1;} };
struct R {S s=S();R& operator=(const R& o)=default;};
void main(){R a=R();R b=R();b.s.x=8;a=b;print("{} {}",assigns,a.s.x);}
)",
	    "1 9\n");
	Ok &= Run("unused unavailable assignment", R"(
struct R {const int x=1;R& operator=(const R& o)=default;};
void main(){R a=R();R b=a;print("{}",b.x);}
)",
	    "1\n");
	Ok &= Reject("used unavailable assignment", R"(
struct R {const int x=1;R& operator=(const R& o)=default;};
void main(){R a=R();R b=R();a=b;}
)");
	Ok &= Reject("invalid ordinary default method", "struct R{int f()=default;};void main(){}");
	Ok &= Reject("invalid default equality", "struct R{bool operator==(const R& o)=default;};void main(){}");
	Ok &= Reject("invalid default copy", "struct R{R(int x)=default;};void main(){}");
	Ok &= Run("class value equality", R"(
class R {public:int x=1;bool operator==(const R& o) const=default;};
void main(){R a=R();R b=R();print("{}",a==b);b.x=2;print("{}",a==b);}
)",
	    "true\nfalse\n");
	Ok &= Run("generic default operations", R"(
struct R<T>{T x;R()=default;R(const R<T>& o)=default;bool operator==(const R<T>& o)const=default;};
void main(){R<int> a=R<int>();a.x=4;R<int>b=a;print("{} {}",b.x,a==b);}
)",
	    "4 true\n");
	Ok &= Run("deleted copy permits prvalue construction", R"(
struct R{int x=5;R()=default;R(const R& o)=delete;};
R make(){return R();} void take(R x){print("{}",x.x);}
void main(){R a=R();take(R());R b=make();print("{} {}",a.x,b.x);}
)",
	    "5\n5 5\n");
	Ok &= Reject("deleted copy rejects lvalue construction", R"(
struct R{R()=default;R(const R& o)=delete;};void main(){R a=R();R b=a;}
)");
	Ok &= Reject("explicit copy rejects implicit initialization", R"(
struct R{R()=default;explicit R(const R& o)=default;};void main(){R a=R();R b=a;}
)");
	Ok &= Run("explicit direct copy", R"(
struct R{int x=7;R()=default;explicit R(const R& o)=default;};void main(){R a=R();R b=R(a);print("{}",b.x);}
)",
	    "7\n");
	Ok &= Run("explicit cast constructs once", R"(
int copies=0;struct R{int x=7;R()=default;explicit R(const R& o){copies+=1;x=o.x;}};
void main(){R a=R();R b=cast<R>(a);print("{} {}",copies,b.x);}
)",
	    "1 7\n");
	Ok &= Run("variant boxing and extraction construct once", R"(
int copies=0;struct R{int x=7;R()=default;R(const R& o){copies+=1;x=o.x;}};
void main(){R a=R();variant v=a;variant w=v;R b=cast<R>(w);print("{} {}",copies,b.x);}
)",
	    "2 7\n");
	Ok &= Run("qualified enum values retain their copy identity", R"(
enum class State{Ready=4};struct R{State value=State::Ready;R()=default;R(const R& o)=default;};
void main(){R a=R();R b=a;variant v=State::Ready;print("{} {}",cast<int>(b.value),cast<int>(v));}
)",
	    "4 4\n");
	Ok &= Run("deleted copy independent of assignment and equality", R"(
struct R{int x=2;R()=default;R(const R& o)=delete;R& operator=(const R& o)=default;bool operator==(const R& o)const=default;};
void main(){R a=R();R b=R();a=b;print("{}",a==b);}
)",
	    "true\n");
	Ok &= Run("self assignment is a default no-op", R"(
int n=0;struct S{void operator=(const S& o){n+=1;}};struct R{S s=S();R& operator=(const R& o)=default;};
void main(){R a=R();a=a;print("{}",n);}
)",
	    "0\n");
	Ok &= Run("default assignment returns the original object", R"(
struct R{int x=2;R& operator=(const R& o)=default;};
void main(){R a=R();R b=R();R& alias=(a=b);alias.x=9;print("{} {}",a.x,b.x);}
)",
	    "9 2\n");
	Ok &= Run("fixed array assignment and equality", R"(
int n=0;struct S{int x=2;void operator=(const S& o){n+=1;x=o.x;}bool operator==(const S& o)const{return x==o.x;}};
struct R{S a[2];R& operator=(const R& o)=default;bool operator==(const R& o)const=default;};
void main(){R a=R();R b=R();a=b;print("{} {}",n,a==b);}
)",
	    "2 true\n");
	Ok &= Run("conditional and indexed values materialize once", R"(
int n=0;struct R{int x=3;R()=default;R(const R& o){n+=1;x=o.x;}};
void main(){R a=R();R b=true?a:R();print("{}",n);R xs[1];R c=xs[0];print("{}",n);}
)",
	    "1\n2\n");
	Ok &= Run("indexed script return uses the backend copy service", R"(
int copies=0;struct R{int x=4;R()=default;R(const R& o){copies+=1;x=o.x;}};
R fetch(array<R> values){return values[0];}
void main(){R a=R();array<R> values={a};copies=0;R b=fetch(values);print("{} {}",copies,b.x);}
)",
	    "1 4\n");
	Ok &= Run("indexed receivers borrow the stored object", R"(
struct R{int x=2;R()=default;R(const R& o)=delete;R& operator=(const R& o)=default;void set(int v){x=v;}};
void main(){R values[1];values[0]=R();values[0].set(7);values[0].x+=1;print("{}",values[0].x);}
)",
	    "8\n");
	Ok &= Run("default base and own fields copied once", R"(
int n=0;struct B{int x=1;B()=default;B(const B& o){n+=1;x=o.x;}};
struct D: B{int y=2;D()=default;D(const D& o)=default;};
void main(){D a=D();D b=a;print("{} {} {}",n,b.x,b.y);}
)",
	    "1 1 2\n");
	Ok &= Reject("private copy rejected", R"(
struct R{R()=default;private:R(const R& o)=default;};void main(){R a=R();R b=a;}
)");
	Ok &= Run("private copy is usable inside its type", R"(
struct R{int x=8;R()=default;static R clone(const R& source){return source;}private:R(const R& o)=default;};
void main(){R a=R();R b=R::clone(&a);print("{}",b.x);}
)",
	    "8\n");
	Ok &= Run("copy does not require a default constructor", R"(
struct R{int x=0;R()=delete;R(int v){x=v;}R(const R& o)=default;};
void main(){R a=R(8);R b=a;print("{}",b.x);}
)",
	    "8\n");
	Ok &= Run("capturing an existing struct constructs once", R"(
int copies=0;struct R{int x=8;R()=default;R(const R& o){copies+=1;x=o.x;}};
void main(){R a=R();fn()->int f=[a]()->int{return a.x;};print("{} {}",copies,f());}
)",
	    "1 8\n");
	Ok &= Run("slicing a fresh return releases the source owner", R"(
struct B{int x=2;};struct D:B{~D(){print("D");}};B make(){return D();}
void main(){B b=make();print("{}",b.x);}
)",
	    "D\n2\n");
	Ok &= Run("base value view selects base copy", R"(
int baseCopies=0;int derivedCopies=0;
struct B{int x=4;B()=default;B(const B& o){baseCopies+=1;x=o.x;}};
struct D:B{int y=8;D()=default;D(const D& o){derivedCopies+=1;}};
int read(B v){return v.x;} B snapshot(const B& b){return b;}
void main(){D d=D();print("{}",read(d));const B& b=d;B c=snapshot(&b);print("{} {} {}",baseCopies,derivedCopies,c.x);}
)",
	    "4\n3 0 4\n");
	Ok &= Run("conversion results do not require another copy", R"(
struct R{int x=0;R(int v){x=v;}R(const R& o)=delete;};
void take(R r){print("{}",r.x);} void main(){R a=3;take(7);print("{}",a.x);}
)",
	    "7\n3\n");
	Ok &= Run("fresh direct construction bypasses deleted copy", R"(
struct R{int x=6;R()=default;R(const R& o)=delete;};void main(){R a=R(R());print("{}",a.x);}
)",
	    "6\n");
	Ok &= Run("default inequality uses builtin equality", R"(
struct R{int x=2;bool operator!=(const R& o)const=default;};void main(){R a=R();R b=R();print("{}",a!=b);b.x=3;print("{}",a!=b);}
)",
	    "false\ntrue\n");
	Ok &= Run("reflection reports defaults and copies descriptor identity", R"(
struct R{int x=1;int y=9;R()=default;R(const R& o)=default;R& operator=(const R& o)=default;};
void main(){type<R> t=type<R>();int defaults=0;for(int i=0;i<t.method_count();i++){method_info<R> m(t.qualified_name,i);if(m.is_defaulted())defaults+=1;}
R r=R();field_info<R> a(t.qualified_name,0);field_info<R> b(t.qualified_name,1);a=b;print("{} {}",defaults,a.get<int>(r));}
)",
	    "3 9\n");
	Ok &= Run("native array and field initialization copy once", R"(
int copies=0;int assignments=0;struct S{int x=2;S()=default;S(const S& o){copies+=1;x=o.x;}S& operator=(const S& o){assignments+=1;x=o.x;return this;}};
S original=S();struct R{S s=original;};void main(){R r=R();S a[1]={original};print("{} {}",copies,assignments);}
)",
	    "2 0\n");
	Ok &= Run("temporary field retains its owner", R"(
struct S{int x=7;};struct R{S s=S();~R(){print("owner destroyed");}};
void main(){S value=R().s;print("{}",value.x);}
)",
	    "7\nowner destroyed\n");
	Ok &= Run("default equality short circuits and uses member operators", R"(
int n=0;struct S{int x=1;bool operator==(const S& o)const{n+=1;return x==o.x;}};
struct R{S a=S();S b=S();bool operator==(const R& o)const=default;};
void main(){R a=R();R b=R();b.a.x=2;print("{} {}",a==b,n);}
)",
	    "false 1\n");
	Ok &= Run("nested implicit copy uses member constructor", R"(
int n=0;struct S{int x=3;S()=default;S(const S& o){n+=1;x=o.x;}};struct R{S s=S();};
void main(){R a=R();R b=a;print("{} {}",n,b.s.x);}
)",
	    "1 3\n");
	Ok &= Run("interface-valued fields preserve dynamic copy hooks", R"(
int copies=0;interface I{virtual int read()const=0;};
struct S:I{int x=7;S()=default;S(const S& other){copies+=1;x=other.x;}int read()const{return x;}};
struct R{I value;};void main(){R a=R();a.value=S();copies=0;R b=a;print("{} {}",copies,b.value.read());}
)",
	    "1 7\n");
	Ok &= Run("reflected field write uses assignment without copying", R"(
struct S{int x=2;S()=default;S(const S& o)=delete;void operator=(const S& o){x=o.x+1;}};
struct H{S value=S();};void main(){S s=S();H h=H();type<H> t=type<H>();field_info<H> f(t.qualified_name,0);f.set<S>(h,s);print("{}",h.value.x);}
)",
	    "3\n");
	Ok &= Run("failed member copy releases only completed subobjects", R"(
int step=0;struct S{int x=0;S()=default;S(const S& o){step+=1;if(step==2)throw 9;x=o.x;}~S(){print("S{}",x);}};
struct R{S a=S();S b=S();R()=default;R(const R& o)=default;~R(){print("R");}};
void main(){R a=R();a.a.x=1;a.b.x=2;try{R b=a;}catch(int n){print("caught{}",n);}}
)",
	    "S1\ncaught9\nR\nS2\nS1\n");
	{
		std::ifstream File(VEROSCRIPT_SPECIAL_MEMBER_EXAMPLE_PATH);
		std::ostringstream Source;
		Source << File.rdbuf();
		if (!File)
		{
			std::cerr << "missing special member example\n";
			Ok = false;
		}
		else
		{
			Ok &= Run("shipped example", Source.str().c_str(), "7 true false\n");
		}
	}
	Ok &= Run("deep global object chain releases without native recursion", R"(
class Node { public: Node next = nullptr; };
Node root = nullptr;
void main(){for(int i=0;i<4096;i+=1){Node node=Node();node.next=root;root=node;}print(42);}
)", "42\n");
	Ok &= Run("deep scoped object chain keeps destructor order", R"(
int cleaned=0;
class Node{public:int id=0;Node next=nullptr;~Node(){if(id!=2047-cleaned)throw 9;cleaned+=1;}};
void release(){Node root=nullptr;for(int i=0;i<2048;i+=1){Node node=Node();node.id=i;node.next=root;root=node;}}
void main(){release();print(cleaned);}
)", "2048\n");
	Ok &= Run("deep object ownership through an array", R"(
int cleaned=0;
class Node{public:array<Node> next={};~Node(){cleaned+=1;}};
void release(){Node root=nullptr;for(int i=0;i<2048;i+=1){Node node=Node();node.next.push(root);root=node;}}
void main(){release();print(cleaned);}
)", "2048\n");
	Ok &= Run("array element reference retains original identity", R"(
void main(){array<int> current={10,11};array<int> original=current;int& first=current[0];
array<int> replacement={20,21};current=replacement;first=42;print(original[0]);print(replacement[0]);}
)", "42\n20\n");
	Ok &= Run("fixed array element writes preserve borrows and independent value copies", R"(
void main(){int values[1]={7};int& first=values[0];int copy[1]=values;
values[0]=9;print(first);print(copy[0]);values[0]+=2;print(first);print(values[0]++);print(first);}
)", "9\n7\n11\n11\n12\n");
	Ok &= Run("entry argument element writes preserve existing borrows", R"(
void main(string[] args){string& first=args[0];args[0]="changed";print(first);}
)", "changed\n", 0, false, nullptr, 0, nullptr, 0, {"hello"});
	Ok &= Run("entry argument element writes retain right operand mutations", R"(
string rewrite(ref string[] values){values[1]="side";return "changed";}
void main(string[] args){args[0]=rewrite(ref args);print(args[0]);print(args[1]);}
)", "changed\nside\n", 0, false, nullptr, 0, nullptr, 0, {"hello", "world"});
	Ok &= Run("captured entry argument writes retain right operand mutations", R"(
void main(string[] args){auto right=[&args]()->string{args[1]="side";return "changed";};
args[0]=right();print(args[0]);print(args[1]);}
)", "changed\nside\n", 0, false, nullptr, 0, nullptr, 0, {"hello", "world"});
	Ok &= Run("negative entry argument index preserves right operand effects", R"(
string right(){print("right");return "changed";}void main(string[] args){args[-1]=right();}
)", "right\n", 0, false, "invalid ref binding", 0, nullptr, 0, {"hello"});
	Ok &= Run("fixed array field and global element writes preserve borrows", R"(
int values[1]={7};
class Holder{public:int fields[1];static int shared[1];void update(){fields[0]+=1;}};
void main(){Holder holder=Holder();holder.fields[0]=7;Holder.shared[0]=7;
int& global=values[0];int& field=holder.fields[0];int& shared=Holder.shared[0];
values[0]=8;holder.fields[0]=9;Holder.shared[0]=10;holder.update();
print(global);print(field);print(shared);}
)", "8\n10\n10\n");
	Ok &= Run("nested fixed array element writes preserve borrows", R"(
void main(){int values[2][2];values[0][0]=7;int& first=values[0][0];
values[0][0]=9;values[0][0]+=2;print(first);}
)", "11\n");
	Ok &= Run("fixed array writes retain changes made by the right operand", R"(
int values[2]={7,8};int change(){values[1]+=1;return 2;}
void main(){print(values[0]=change());print(values[1]);print(values[0]+=change());print(values[1]);}
)", "2\n9\n4\n10\n");
	Ok &= Run("fixed array negative plain index preserves right operand side effects", R"(
int right(){print("right");return 2;}void main(){int values[1];values[-1]=right();}
)", "right\n", 0, false, "invalid ref binding");
	Ok &= Run("fixed array negative compound index fails before the right operand", R"(
int right(){print("right");return 2;}void main(){int values[1];values[-1]+=right();}
)", "", 0, false, "invalid ref binding");
	Ok &= Run("fixed array indexed writes notify their field owner", R"(
class Holder{public:int values[1];void change(){values[0]=7;values[0]+=1;int& first=values[0];first+=1;}};
void main(){Holder value=Holder();value.change();}
)", "", 0, false, nullptr, 0, "values", 3);
	Ok &= Run("an element borrow notifies its field owner without a prior read", R"(
class Holder{public:array<int> values={7};void change(){int& first=values[0];first+=1;}};
void main(){Holder value=Holder();value.change();}
)", "", 0, false, nullptr, 0, "values", 1);
	Ok &= Run("indexed assignment paths preserve nested and qualified writeback", R"(
array<array<int>> values={{7}};array<array<int>> original=values;
int replace(){values=array<array<int>>();array<int> row={20};values.push(row);return 2;}
class C{public:static array<int> values={7};};namespace N{array<int> values={7};}
int replace_static(){C.values=array<int>();C.values.push(20);return 2;}
int replace_namespace(){N.values=array<int>();N.values.push(20);return 2;}
void main(){print(values[0][0]=replace());print(values[0][0]);print(original[0][0]);
print(C.values[0]=replace_static());print(C.values[0]);print(N.values[0]=replace_namespace());print(N.values[0]);}
)", "2\n2\n7\n2\n2\n2\n2\n");
	Ok &= Run("explicit nested borrows keep original identity across later arguments", R"(
array<array<int>> values={{7}};array<array<int>> original=values;
int replace(){values=array<array<int>>();array<int> row={20};values.push(row);return 2;}
void change(ref int value,int unused){value=42;}
void main(){change(ref values[0][0],replace());print(original[0][0]);print(values[0][0]);}
)", "42\n20\n");
	Ok &= Run("map element reference retains original identity", R"(
void main(){map<string,int> current=map<string,int>();current["a"]=10;map<string,int> original=current;int& first=current["a"];
map<string,int> replacement=map<string,int>();replacement["a"]=20;current=replacement;first=42;print(original["a"]);print(replacement["a"]);}
)", "42\n20\n");
	Ok &= Run("ordered map element reference retains original identity", R"(
void main(){ordered_map<string,int> current=ordered_map<string,int>();current["a"]=10;ordered_map<string,int> original=current;int& first=current["a"];
ordered_map<string,int> replacement=ordered_map<string,int>();replacement["a"]=20;current=replacement;first=42;print(original["a"]);print(replacement["a"]);}
)", "42\n20\n");
	Ok &= Run("deque element reference retains original identity", R"(
void main(){deque<int> current=deque<int>();current.push_back(10);deque<int> original=current;int& first=current[0];
deque<int> replacement=deque<int>();replacement.push_back(20);current=replacement;first=42;print(original[0]);print(replacement[0]);}
)", "42\n20\n");
	Ok &= Run("element reference survives in-place update and tail append", R"(
void main(){array<int> values={10};int& first=values[0];values[0]=20;values.push(30);first=42;print(values[0]);}
)", "42\n");
	Ok &= Run("reference forwarding normalizes aliases", R"(
int count(ref int value,int remaining){if(remaining==0){value+=1;return value;}return count(ref value,remaining-1);}
void main(){for(int n=62;n<=80;n+=1){int value=41;if(count(ref value,n)!=42||value!=42)throw 9;}print(42);}
)", "42\n");
	Ok &= Run("weak lock observes script destruction", R"(
weak<Item> observer=weak<Item>();
Item escaped=nullptr;
class Item{public:int value=9;~Item(){escaped=observer.lock();print(escaped!=nullptr);}};
void main(){Item item=Item();observer=weak<Item>(item);item=nullptr;print(observer.expired());print(escaped!=nullptr);}
)", "false\ntrue\nfalse\n");
	Ok &= Run("object ownership through variant", R"(
int cleaned=0;
class Node{public:variant next;~Node(){cleaned+=1;}};
void release(){Node root=nullptr;for(int i=0;i<32;i+=1){Node node=Node();node.next=root;root=node;}}
void main(){release();print(cleaned);}
)", "32\n");
	Ok &= Run("dynamic static initialization follows selected callback", R"(
class Value<T>{public:static int value=7;};class Value2<T>{public:static int value=8;};
int get(bool condition){fn()->int f=condition?[](){return Value<int>::value;}:[](){return Value2<int>::value;};return f();}
class State{public:Value<int> first=nullptr;Value2<int> second=nullptr;static int result=get(false);};
void main(){print(State::result);print(Value<int>::value);print(Value2<int>::value);}
)", "8\n7\n8\n");
	Ok &= Run("dynamic static initialization leaves untaken dependency in order", R"(
class Needed<T>{public:static int value=7;};class Unused<T>{public:static int value=State::result+1;};
int get(bool condition){fn()->int f=condition?[](){return Needed<int>::value;}:[](){return Unused<int>::value;};return f();}
class State{public:Needed<int> first=nullptr;Unused<int> second=nullptr;static int result=get(true);};
void main(){print(State::result);print(Needed<int>::value);print(Unused<int>::value);}
)", "7\n7\n8\n");
	Ok &= Run("dynamic static initialization follows virtual target", R"(
class Value<T>{public:static int value=7;};class Base{public:virtual int read(){return 9;}};
class Derived:public Base{public:int read()override{return Value<int>::value;}};
int get(Base value){return value.read();}
class State{public:Value<int> first=nullptr;static int result=get(Derived());};
void main(){print(State::result);print(Value<int>::value);}
)", "7\n7\n");
	Ok &= Run("dynamic static initialization follows interface target", R"(
class Value<T>{public:static int value=7;};interface I{virtual int read()=0;};
class Derived:public I{public:int read()override{return Value<int>::value;}};
int get(I value){return value.read();}
class State{public:Value<int> first=nullptr;static int result=get(Derived());};
void main(){print(State::result);print(Value<int>::value);}
)", "7\n7\n");
	Ok &= Run("dynamic static initialization precedes reference writes and runs once", R"(
class Count{public:static int calls=0;};int initialize(){Count::calls+=1;return 7;}
class Value<T>{public:static int value=initialize();};
int get(bool condition){fn()->int f=condition?[](){int& v=Value<int>::value;v+=2;return v;}:[](){return 0;};return f();}
class State{public:Value<int> first=nullptr;static int result=get(true);};
void main(){print(State::result);print(Value<int>::value);print(Count::calls);}
)", "9\n9\n1\n");
	for (const bool bControlled : {false, true})
	{
		Ok &= Run("caught dynamic static initialization retries before the next read", R"(
class Count{public:static int calls=0;};
int initialize(){Count::calls+=1;if(Count::calls==1)throw 71;return 9;}
class Value<T>{public:static int value=initialize();};class Other<T>{public:static int value=8;};
int get(bool condition)
{
	fn()->int read=condition?[](){return Value<int>::value;}:[](){return Other<int>::value;};
	try{print(read());}catch(int error){print(error);}
	return read();
}
class State{public:Value<int> first=nullptr;Other<int> second=nullptr;static int result=get(true);};
void main(){print(State::result);print(Value<int>::value);print(Count::calls);}
)", "71\n9\n9\n2\n", 0, bControlled);
		Ok &= Run("dynamic static initialization can fail and unwind repeatedly", R"(
class Count{public:static int calls=0;static int cleaned=0;};
int initialize()
{
	Count::calls+=1;
	try{if(Count::calls<3)throw 70+Count::calls;return 9;}
	finally{Count::cleaned+=1;}
}
class Value<T>{public:static int value=initialize();};class Other<T>{public:static int value=8;};
int get(bool condition)
{
	fn()->int read=condition?[](){return Value<int>::value;}:[](){return Other<int>::value;};
	for(int i=0;i<2;i+=1){try{print(read());}catch(int error){print(error);}}
	return read();
}
class State{public:Value<int> first=nullptr;Other<int> second=nullptr;static int result=get(true);};
void main(){print(State::result);print(Value<int>::value);print(Count::calls);print(Count::cleaned);}
)", "71\n72\n9\n9\n3\n3\n", 0, bControlled);
		Ok &= Run("failed dynamic static initialization retains its completed dependency", R"(
class Count{public:static int calls=0;static int dependencies=0;};
int dependency(){Count::dependencies+=1;return 7;}
class Needed<T>{public:static int value=dependency();};
int initialize()
{
	Count::calls+=1;
	fn()->int read=Count::calls>0?[](){return Needed<int>::value;}:[](){return 0;};
	int value=read();
	if(Count::calls==1)throw 71;
	return value+2;
}
class Value<T>{public:static int value=initialize();};class Other<T>{public:static int value=8;};
int get(bool condition)
{
	fn()->int read=condition?[](){return Value<int>::value;}:[](){return Other<int>::value;};
	try{print(read());}catch(int error){print(error);}
	return read();
}
class State
{
public:
	Value<int> first=nullptr;
	Needed<int> dependency=nullptr;
	Other<int> second=nullptr;
	static int result=get(true);
};
void main(){print(State::result);print(Value<int>::value);print(Needed<int>::value);print(Count::dependencies);print(Count::calls);}
)", "71\n9\n9\n7\n1\n2\n", 0, bControlled);
	}
	Ok &= Run("expired array element reference rejects a replacement", R"(
void main(){array<int> values={1};int& element=values[0];values.clear();values.push(2);element=42;}
)", "", 0, false, "invalid ref binding");
	Ok &= Run("removed map entry reference cannot bind a reinserted key", R"(
void main(){map<string,int> values=map<string,int>();values["a"]=1;int& element=values["a"];
values.remove("a");values["a"]=2;element=42;}
)", "", 0, false, "invalid ref binding");
	Ok &= Run("removed ordered map reference cannot bind a reinserted key", R"(
void main(){ordered_map<string,int> values=ordered_map<string,int>();values["a"]=1;int& element=values["a"];
values.remove("a");values["a"]=2;element=42;}
)", "", 0, false, "invalid ref binding");
	Ok &= Run("shifted deque reference cannot bind a replacement element", R"(
void main(){deque<int> values=deque<int>();values.push_back(1);int& element=values[0];values.push_front(2);element=42;}
)", "", 0, false, "invalid ref binding");
	Ok &= Run("map reference survives an unrelated insertion and original entry assignment", R"(
void main(){map<string,int> values=map<string,int>();values["a"]=1;int& element=values["a"];
values["b"]=2;values["a"]=3;element=42;print(values["a"]);print(values["b"]);}
)", "42\n2\n");
	Ok &= Run("destroyed escaped object rejects a field read", R"(
Item escaped=nullptr;class Item{public:int value=9;~Item(){escaped=this;}};
void main(){Item item=Item();item=nullptr;print(escaped.value);}
)", "", 0, false, "destroyed");
	Ok &= Run("destroyed escaped object rejects a field write", R"(
Item escaped=nullptr;class Item{public:int value=9;~Item(){escaped=this;}};
void main(){Item item=Item();item=nullptr;escaped.value=42;}
)", "", 0, false, "destroyed");
	return Ok ? 0 : 1;
}
