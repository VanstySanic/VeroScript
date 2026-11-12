// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptTestJitCapabilities.h"
#include "../VeroScriptMethodBindingInternal.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptMethodBinding.h"
#include "VeroScriptVm.h"

#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

using namespace VeroScript;

namespace
{
	bool Check(const bool bCondition, const std::string& Message)
	{
		if (!bCondition)
		{
			std::cerr << Message << '\n';
		}
		return bCondition;
	}

	bool DeepInheritance()
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "export class Root{public:int read(){return 7;}};void main(){}", "deep-binding.vs");
		if (!Check(Compiled.bSuccess, Compiled.Error.Message)) return false;
		std::uint16_t Root = 0;
		if (!Check(ModuleMeta::FindTypeIndex(*Compiled.Module, "Root", Root), "missing root type")) return false;
		const auto FirstDerived = Compiled.Module->Types.size();
		std::uint16_t Previous = Root;
		std::string Last;
		constexpr std::size_t Depth = 32768;
		for (std::size_t Index = 0; Index < Depth; ++Index)
		{
			FBytecodeTypeDesc Type;
			Type.Name = "Derived" + std::to_string(Index);
			Last = Type.Name;
			Type.Kind = EScriptObjectKind::Class;
			Type.BaseTypeIndices.push_back(Previous);
			Type.BaseInheritanceAccess.push_back(EMemberAccess::Public);
			Previous = static_cast<std::uint16_t>(Compiled.Module->Types.size());
			Compiled.Module->Types.push_back(std::move(Type));
			FBytecodeInterfaceDesc Interface;
			Interface.Name = "I" + std::to_string(Index);
			if (Index != 0)
			{
				Interface.BaseInterfaceIndices.push_back(static_cast<std::uint16_t>(Index - 1));
			}
			Compiled.Module->Interfaces.push_back(std::move(Interface));
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!Check(VeroScriptBytecodeIO::SerializeBytecode(*Compiled.Module, 0, Bytes, Error), Error)) return false;
		auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 0, Error);
		if (!Check(bool(Loaded), Error)) return false;
		std::unordered_set<std::uint16_t> Visited;
		using namespace MethodBindingDetail;
		bool Ok = Check(HasPublicBasePath(*Loaded, Previous, Root, Visited), "deep public class path");
		Visited.clear();
		Ok &= Check(HasBasePath(*Loaded, Previous, Root, Visited), "deep class path");
		Visited.clear();
		Ok &= Check(HasInterfaceBasePath(*Loaded, Depth - 1, 0, Visited), "deep interface path");
		std::vector<FMethodBindingSelection> Candidates;
		FMethodBindingSelection Selection;
		const FMethodBindingRequest Request{Last, "read", "fn()->int", false};
		Ok &= Check(CollectMethodBindingCandidates(*Loaded, nullptr, Last, Candidates, Error) && SelectBindableMethod(Request, Candidates, Selection, Error), "deep inherited method binding: " + Error);

		Loaded->Types[FirstDerived + Depth / 2].BaseInheritanceAccess[0] = EMemberAccess::Private;
		Visited.clear();
		Ok &= Check(!HasPublicBasePath(*Loaded, Previous, Root, Visited), "private inheritance exposed as public");
		Visited.clear();
		Ok &= Check(HasBasePath(*Loaded, Previous, Root, Visited), "private path lost from ancestry check");
		Ok &= Check(CollectMethodBindingCandidates(*Loaded, nullptr, Last, Candidates, Error) && !SelectBindableMethod(Request, Candidates, Selection, Error), "binding bypassed private inheritance");
		Loaded->Types[Previous].BaseTypeIndices.push_back(Root);
		Loaded->Types[Previous].BaseInheritanceAccess.push_back(EMemberAccess::Public);
		Visited.clear();
		Ok &= Check(HasPublicBasePath(*Loaded, Previous, Root, Visited), "alternate public path rejected");
		return Ok;
	}

	bool InvalidAndCyclicPaths()
	{
		FBytecodeModule Module;
		Module.Types.resize(3);
		Module.Interfaces.resize(3);
		for (std::uint16_t Index = 0; Index < 2; ++Index)
		{
			const auto Other = static_cast<std::uint16_t>(1 - Index);
			Module.Types[Index].BaseTypeIndices = {Other};
			Module.Types[Index].BaseInheritanceAccess = {EMemberAccess::Public};
			Module.Interfaces[Index].BaseInterfaceIndices = {Other};
		}
		std::unordered_set<std::uint16_t> Visited;
		using namespace MethodBindingDetail;
		bool Ok = Check(!HasPublicBasePath(Module, 0, 2, Visited), "cyclic public path did not terminate");
		Visited.clear();
		Ok &= Check(!HasBasePath(Module, 0, 2, Visited), "cyclic ancestry did not terminate");
		Visited.clear();
		Ok &= Check(!HasInterfaceBasePath(Module, 0, 2, Visited), "cyclic interface path did not terminate");
		Visited.clear();
		constexpr auto Invalid = std::numeric_limits<std::uint16_t>::max();
		Ok &= Check(!HasPublicBasePath(Module, Invalid, Invalid, Visited) && !HasBasePath(Module, Invalid, Invalid, Visited) && !HasInterfaceBasePath(Module, Invalid, Invalid, Visited), "invalid inheritance identity accepted");
		return Ok;
	}

	bool GraphPaths()
	{
		std::mt19937 Random(731);
		constexpr std::size_t Size = 12;
		for (std::size_t Iteration = 0; Iteration < 128; ++Iteration)
		{
			FBytecodeModule Module;
			Module.Types.resize(Size);
			Module.Interfaces.resize(Size);
			std::array<std::array<bool, Size>, Size> Any{}, Public{};
			for (std::size_t From = 0; From < Size; ++From)
			{
				Any[From][From] = Public[From][From] = true;
				for (std::size_t To = 0; To < Size; ++To)
				{
					if (From != To && Random() % 5 == 0)
					{
						const auto Access = static_cast<EMemberAccess>(Random() % 3);
						Module.Types[From].BaseTypeIndices.push_back(static_cast<std::uint16_t>(To));
						Module.Types[From].BaseInheritanceAccess.push_back(Access);
						Module.Interfaces[From].BaseInterfaceIndices.push_back(static_cast<std::uint16_t>(To));
						Any[From][To] = true;
						Public[From][To] = Access == EMemberAccess::Public;
					}
				}
			}
			// Compare the traversal with an independent transitive closure.
			for (std::size_t Via = 0; Via < Size; ++Via)
			{
				for (std::size_t From = 0; From < Size; ++From)
				{
					for (std::size_t To = 0; To < Size; ++To)
					{
						Any[From][To] = Any[From][To] || (Any[From][Via] && Any[Via][To]);
						Public[From][To] = Public[From][To] || (Public[From][Via] && Public[Via][To]);
					}
				}
			}
			for (std::uint16_t From = 0; From < Size; ++From)
			{
				for (std::uint16_t To = 0; To < Size; ++To)
				{
					std::unordered_set<std::uint16_t> Visited;
					if (!Check(MethodBindingDetail::HasPublicBasePath(Module, From, To, Visited) == Public[From][To], "public graph path mismatch"))
					{
						return false;
					}
					Visited.clear();
					if (!Check(MethodBindingDetail::HasBasePath(Module, From, To, Visited) == Any[From][To], "graph path mismatch"))
					{
						return false;
					}
					Visited.clear();
					if (!Check(MethodBindingDetail::HasInterfaceBasePath(Module, From, To, Visited) == Any[From][To], "interface graph path mismatch"))
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	bool ExecuteModule(const FBytecodeModule& Module, const std::string& Backend,
	    const std::string& Expected, const bool bSuccess = true, const std::string& ErrorId = {},
	    const int ExitCode = 0, const bool bRequireNative = false)
	{
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!Check(VeroScriptBytecodeIO::SerializeBytecode(Module, 101, Bytes, Error), Error)) return false;
		for (const auto Mode : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Mode != EExecutionBackend::Jit || !Backend.empty())
			{
				for (const bool bControlled : {false, true})
				{
					auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 101, Error);
					if (!Check(bool(Loaded), Error)) return false;
					std::shared_ptr<const FBytecodeModule> Owner = std::move(Loaded);
					std::ostringstream Output;
					VeroScriptVm Vm(Output);
					VeroScript::Test::Configure(Vm);
					Vm.SetExecutionBackend(Mode);
					FJitPolicy Policy;
					Policy.MixedHotCallThreshold = 1;
					Policy.EagerCompileBudget = 256;
					Vm.SetJitPolicy(Policy);
					if (bControlled)
					{
						Vm.SetExecutionLimits({1000000, 0});
					}
					if (!Backend.empty() && !Check(Vm.SetJitBackendPath(Backend, Error) && Vm.SetJitProgramOwner(Owner, Error), Error))
					{
						return false;
					}
					const bool bOk = Vm.Execute(*Owner, ERunMode::Script);
					const FScriptError& Diagnostic = Vm.GetError();
					// Legacy backend errors retain English text when the host does not
					// load a diagnostic catalogue for reverse lookup of message IDs.
					const bool bMatchingError = ErrorId.empty() || Diagnostic.MessageId == ErrorId || (Diagnostic.MessageId.empty() && ErrorId == "diag.division_by_zero" && Diagnostic.Message == "division by zero");
					if (!Check(bOk == bSuccess && Output.str() == Expected && Vm.HasError() == !bSuccess && bMatchingError && Vm.GetProcessExitCode() == ExitCode,
					        "inheritance execution: " + Diagnostic.Message + "; id=" + Diagnostic.MessageId + "; backend=" + std::to_string(static_cast<int>(Mode)) + "; output=" + Output.str()))
					{
						return false;
					}
					const auto Stats = Vm.GetJitStats();
					if (bRequireNative && Mode == EExecutionBackend::Jit && Test::SupportsAsmJitControlFlow(Vm.GetJitBackendFactory()->GetDescription()) && !Check(Stats.NativeLeafEntries + Stats.NativeCfgEntries != 0, "no native execution"))
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	bool DeepExecution(const std::string& Stdlib, const std::string& Backend, const bool bInterface)
	{
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.StdlibSearchPaths = {Stdlib};
		const std::string Source = "import \"attribute.vs\";" + std::string(bInterface ? "export interface IRoot{int read(){return 7;}};export interface ILeaf:IRoot{};"
		                                                                                 "class Item:public ILeaf{public:~Item(){print(1);}};"
		                                                                               : "export class Root{public:int read(){return 7;}~Root(){print(2);}};"
		                                                                                 "export class Leaf:public Root{public:~Leaf(){print(1);}};") +
		    "int marker(int n){return n+1;}void main(){print(marker(8));" + (bInterface ? "Item item=Item();type<ILeaf> t=type<ILeaf>();" : "Leaf item=Leaf();type<Root> t=type<Root>();") + "for(int i=0;i<t.method_count();++i){" + (bInterface ? "method_info<ILeaf>" : "method_info<Root>") + " m=" + (bInterface ? "method_info<ILeaf>" : "method_info<Root>") + "(t.qualified_name,i);if(m.name()==\"read\")print(m.invoke<int>(item));}"
		                                                                                                                                                                                                                                                                                                                                                              "string name=\"read\";auto callback=std::bind_method<fn()->int>(item,name);print(callback());}";
		auto Compiled = VeroScriptCompile::CompileSource(Source, "deep-runtime.vs", Options);
		if (!Check(Compiled.bSuccess, Compiled.Error.Message)) return false;
		std::uint16_t Root = 0, Leaf = 0;
		if (!Check(bInterface
		            ? ModuleMeta::FindInterfaceIndex(*Compiled.Module, "IRoot", Root) && ModuleMeta::FindInterfaceIndex(*Compiled.Module, "ILeaf", Leaf)
		            : ModuleMeta::FindTypeIndex(*Compiled.Module, "Root", Root) && ModuleMeta::FindTypeIndex(*Compiled.Module, "Leaf", Leaf),
		        "missing runtime types"))
		{
			return false;
		}
		std::uint16_t Previous = Root;
		for (std::size_t Index = 0; Index < 32768; ++Index)
		{
			if (bInterface)
			{
				FBytecodeInterfaceDesc Type;
				Type.Name = "ILayer" + std::to_string(Index);
				Type.BaseInterfaceIndices = {Previous};
				Previous = static_cast<std::uint16_t>(Compiled.Module->Interfaces.size());
				Compiled.Module->Interfaces.push_back(std::move(Type));
			}
			else
			{
				FBytecodeTypeDesc Type;
				Type.Name = "Layer" + std::to_string(Index);
				Type.Kind = EScriptObjectKind::Class;
				Type.BaseTypeIndices = {Previous};
				Type.BaseInheritanceAccess = {EMemberAccess::Public};
				Previous = static_cast<std::uint16_t>(Compiled.Module->Types.size());
				Compiled.Module->Types.push_back(std::move(Type));
			}
		}
		if (bInterface)
		{
			Compiled.Module->Interfaces[Leaf].BaseInterfaceIndices = {Previous};
		}
		else
		{
			Compiled.Module->Types[Leaf].BaseTypeIndices = {Previous};
		}
		return ExecuteModule(*Compiled.Module, Backend, bInterface ? "9\n7\n7\n1\n" : "9\n7\n7\n1\n2\n",
		    true, {}, 0, true);
	}

	bool CleanupOrder(const std::string& Backend)
	{
		auto Fields = VeroScriptCompile::CompileSource(
		    "class Field{public:~Field(){print(11);}};class Last{public:~Last(){print(13);}};"
		    "class Root{public:Field field=Field();~Root(){print(1);}};"
		    "class Middle:public Root{public:~Middle(){print(2);}};"
		    "class Leaf:public Middle{public:Last last=Last();~Leaf(){print(3);}};"
		    "void work(){Leaf item=Leaf();throw 9;}void main(){try{work();}catch(int n){print(n);}}",
		    "cleanup-fields.vs");
		if (!Check(Fields.bSuccess, Fields.Error.Message) || !ExecuteModule(*Fields.Module, Backend, "3\n13\n2\n1\n11\n9\n"))
		{
			return false;
		}
		for (int Case = 0; Case < 5; ++Case)
		{
			const std::string Left = Case == 2 ? "int n=0;print(1/n);"
			    : Case == 3                    ? "exit(7);"
			    : Case == 0                    ? ""
			                                   : "throw 88;";
			const std::string Right = Case == 4 ? "int n=0;print(1/n);" : Case == 0 ? ""
			                                                                        : "throw 66;";
			auto Compiled = VeroScriptCompile::CompileSource(
			    "class Left{public:~Left(){print(1);" + Left + "}};"
			                                                   "class Right{public:~Right(){print(2);" +
			        Right + "}};"
			                "class Leaf:public Left,public Right{public:~Leaf(){print(3);}};"
			                "void work(){Leaf item=Leaf();}void main(){try{work();}catch(int n){print(n);}}",
			    "cleanup-bases.vs");
			if (!Check(Compiled.bSuccess, Compiled.Error.Message) || !ExecuteModule(*Compiled.Module, Backend, Case == 1 ? "3\n2\n1\n88\n" : "3\n2\n1\n", Case != 2 && Case != 4, Case == 2 || Case == 4 ? "diag.division_by_zero" : "", Case == 3 ? 7 : 0))
			{
				return false;
			}
		}
		return true;
	}
}

int main(int Argc, char** Argv)
{
	const std::string Backend = Argc > 2 ? Argv[2] : "";
	return Argc >= 2 && DeepInheritance() && InvalidAndCyclicPaths() && GraphPaths() && DeepExecution(Argv[1], Backend, false) && DeepExecution(Argv[1], Backend, true) && CleanupOrder(Backend) ? 0 : 1;
}
