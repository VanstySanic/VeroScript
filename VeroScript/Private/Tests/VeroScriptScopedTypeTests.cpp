// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAst.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptPackage.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptTestTemporaryDirectory.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace
{
	std::string BackendPath;
	std::uint64_t BackendDispatches = 0, NativeEntries = 0;
	struct FScopedMemberGenerator : VeroScript::ICompileProcessor
	{
		void Process(const VeroScript::FCompileProcessingContext &Context,
		    VeroScript::FCompileProcessingOutput &Output) override
		{
			for (const auto &Declaration : Context.Declarations)
			{
				if (Declaration.bLocal && Declaration.Target == VeroScript::EAnnotationTarget::Struct &&
				    !Declaration.bGenerated)
				{
					if (Declaration.ScopeId.empty())
					{
						throw std::runtime_error("local declaration has no scope");
					}
					Output.AddDeclaration({Context.ModuleId, "", Declaration.Id, Context.Snapshot},
					    "public: Value generated(){Value result=7;return result;}");
				}
			}
		}
	};

	struct FCase
	{
		const char *Source;
		const char *Expected;
		const char *ErrorCategory = nullptr;
	};

	struct FTypeGenerator : VeroScript::ICompileProcessor
	{
		explicit FTypeGenerator(const int InMode) : Mode(InMode)
		{
		}
		int Mode;
		void Process(const VeroScript::FCompileProcessingContext &C,
		    VeroScript::FCompileProcessingOutput &O) override
		{
			using namespace VeroScript;
			if (Mode == 10)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
				    "class Generated{public:int read(){A::Hidden "
				    "value=A::Hidden();return 7;}};");
				return;
			}

			if (Mode == 3)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "class Generated{public:struct Nested{int "
				                                                   "value=7;};Nested make(){struct Local{int "
				                                                   "value=8;};return Nested();}};");
				return;
			}
			if (Mode == 2)
			{
				for (const auto &D : C.Declarations)
				{
					if (D.Name == "Outer")
					{
						O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot},
						    "public:struct Generated{int value=7;};");
					}
				}
				return;
			}
			if (Mode == 4 || Mode == 5 || Mode == 9)
			{
				for (const auto &D : C.Declarations)
				{
					if (D.Name != "compute")
					{
						continue;
					}
					if (Mode == 5)
					{
						O.ReplaceBody(D.Id, C.Snapshot,
						    "{struct Local{int value=7;};Local item=Local();return "
						    "item.value;}");
					}
					else
					{
						O.CompleteBody(D.Id, C.Snapshot,
						    Mode == 9 ? "{struct Local{int read(){return value;}};Local "
						                "item=Local();return item.read();}"
						              : "{struct Local{int data;};Local "
						                "item=Local();item.data=value;return item.data;}");
					}
				}
				return;
			}
			if (!C.Program || !C.Blocks)
			{
				throw std::runtime_error("missing block query context");
			}
			std::string Scope;
			if (Mode == 6)
			{
				for (const auto &Type : C.Program->Classes)
				{
					if (Type.Name == "Outer")
					{
						Scope = Type.Methods.front().Body->ScopeId;
					}
				}
				for (const auto &D : C.Declarations)
				{
					if (D.Name == "Outer")
					{
						O.ReplaceDeclaration(D.Id, C.Snapshot, "class Outer{};");
					}
				}
			}
			else
			{
				for (const auto &Function : C.Program->Functions)
				{
					if (Function.Name == (Mode == 8 ? "work" : "main") && Function.Body &&
					    Function.ModulePath == C.ModuleId)
					{
						Scope = Function.Body->ScopeId;
					}
				}
			}
			if (Scope.empty())
			{
				return;
			}
			for (const auto &Block : *C.Blocks)
			{
				if (Block.ScopeId != Scope)
				{
					continue;
				}
				FCompileScope Target{
				    C.ModuleId,
				    "",
				    "",
				    C.Snapshot,
				    Scope,
				    Mode == 1 ? Block.EndAnchorId : Mode == 7 ? "absent"
				                                              : Block.Statements.front().Id};
				O.AddDeclaration(Target, Mode == 8 ? "struct Generated{T value;};" : Mode == 1 ? "struct AtEnd{Value value=7;};"
				                                                                               : "struct Generated{int value=7;};");
				if (Mode == 0)
				{
					O.AddDeclaration(Target, "struct Following{Generated item;};");
				}
				return;
			}
			throw std::runtime_error("block query did not expose the target");
		}
	};

	bool Run(const VeroScript::FBytecodeModule &Module, const std::string &Expected, const std::size_t Case)
	{
		for (std::size_t I = 0; I < Module.Types.size(); ++I)
		{
			const auto &Type = Module.Types[I];
			if (!Type.Scope.bLocal && Type.Scope.OwnerTypeName.empty())
			{
				continue;
			}
			std::uint16_t Index = 0;
			if (!VeroScript::ModuleMeta::FindTypeIndex(Module, Type.Name, Index) || Index != I ||
			    (VeroScript::ModuleMeta::FindTypeIndex(Module, Type.Scope.SimpleName, Index) && Index == I))
			{
				std::cerr << "scoped type lookup must require its full identity: " << Type.Name << '\n';
				return false;
			}
		}
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!BackendPath.empty())
		{
			std::string Error;
			if (!Vm.SetJitBackendPath(BackendPath, Error))
			{
				std::cerr << "backend: " << Error << '\n';
				return false;
			}
			Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
			VeroScript::FJitPolicy Policy;
			Policy.MixedHotCallThreshold = 1;
			Policy.EagerCompileBudget = 256;
			Vm.SetJitPolicy(Policy);
		}
		if (!Vm.Execute(Module, VeroScript::ERunMode::Script) || Output.str() != Expected)
		{
			std::cerr << "case " << Case << ": " << Vm.GetError().Message << " expected [" << Expected
			          << "] got [" << Output.str() << "]\n";
			return false;
		}
		const auto Stats = Vm.GetJitStats();
		BackendDispatches += Stats.Dispatches;
		NativeEntries += Stats.NativeLeafEntries + Stats.NativeCfgEntries;
		return true;
	}

	bool TestIncrementalParserTypeLookup()
	{
		using namespace VeroScript;
		// Force repeated appends/reallocations after lookup has indexed earlier
		// declarations, including aliases and generic arities of the same name.
		std::string Source = "class Seed{public:int value=7;};";
		for (int Index = 0; Index < 256; ++Index)
		{
			const std::string Previous = Index == 0 ? "Seed" : "Node" + std::to_string(Index - 1);
			Source += "class Node" + std::to_string(Index) + "{public:" + Previous +
			    " make(){return " + Previous + "();}};";
		}
		Source += "using Tail=Node255;class Box<T>{public:T value;};"
		          "class Box<T,U>{public:T first;U second;};"
		          "class Final{public:Tail tail;Box<int> one;Box<int,string> two;};";
		VeroScriptLexer Lexer(Source, "indexed-types.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "indexed-types.vs");
		auto Program = Parser.ParseProgram();
		if (!Program || Lexer.HasError() || Parser.HasError() || Program->Classes.size() != 260 || Program->Classes[256].Methods.size() != 1 || Program->Classes[256].Methods.front().ReturnType != "Node254")
		{
			std::cerr << "incremental parser type lookup: " << Parser.GetError().Message << '\n';
			return false;
		}

		// Same-spelling hidden declarations must not mask a visible declaration.
		FProgram External;
		for (const bool Exported : {false, true})
		{
			FInterfaceDecl Interface;
			Interface.Name = "Contract";
			Interface.ModulePath = Exported ? "visible.vs" : "hidden.vs";
			Interface.bExported = Exported;
			External.Interfaces.push_back(std::move(Interface));
			FTypeAliasDecl Alias;
			Alias.Name = "ContractAlias";
			Alias.TypeName = Exported ? "Contract" : "Missing";
			Alias.ModulePath = Exported ? "visible.vs" : "hidden.vs";
			Alias.bExported = Exported;
			External.TypeAliases.push_back(std::move(Alias));
		}
		const std::string ConsumerSource = "class Consumer:ContractAlias{};";
		VeroScriptLexer ConsumerLexer(ConsumerSource, "consumer.vs");
		VeroScriptParser Consumer(ConsumerLexer.Tokenize(), "consumer.vs", &External, {"visible.vs"});
		auto ConsumerProgram = Consumer.ParseProgram();
		if (!ConsumerProgram || Consumer.HasError() || ConsumerProgram->Classes.size() != 1 || ConsumerProgram->Classes.front().InterfaceTypeNames != std::vector<std::string>{"ContractAlias"} || !ConsumerProgram->Classes.front().BaseTypeNames.empty())
		{
			std::cerr << "indexed parser visibility/alias lookup: " << Consumer.GetError().Message << '\n';
			return false;
		}
		return true;
	}

	bool TestCanonicalRuntimeLookup()
	{
		using namespace VeroScript;
		FBytecodeModule Module;
		const auto CheckTable = [&](auto &Table, auto Find)
		{
			using FDesc = typename std::decay_t<decltype(Table)>::value_type;
			for (const char *Name : {"one\x1f"
			                         "Item",
			         "Item", "two\x1f"
			                 "Item",
			         "owner\x1f"
			         "Scope.Inner"})
			{
				FDesc Desc;
				Desc.Name = Name;
				Table.push_back(std::move(Desc));
			}
			Table.back().Scope.OwnerTypeName = "Scope";
			std::uint16_t Index = 0;
			bool Ambiguous = true;
			if (!Find(Module, "Item", Index, &Ambiguous) || Index != 1 || Ambiguous ||
			    !Find(Module, "one\x1f"
			                  "Item",
			        Index, &Ambiguous) ||
			    Index != 0 || Ambiguous ||
			    !Find(Module, "Scope.Inner", Index, &Ambiguous) || Index != 3 || Ambiguous ||
			    Find(Module, "Inner", Index, &Ambiguous) || Index != 0xffff || Ambiguous)
			{
				return false;
			}
			// Exact names win over ambiguous aliases; duplicate exact names remain ambiguous.
			Table.push_back(Table[1]);
			if (Find(Module, "Item", Index, &Ambiguous) || Index != 0xffff || !Ambiguous) return false;
			Table.pop_back();
			Table[1].Name = "Other";
			if (Find(Module, "Item", Index, &Ambiguous) || Index != 0xffff || !Ambiguous) return false;
			Table[2].Name = "Different";
			if (!Find(Module, "Item", Index, &Ambiguous) || Index != 0 || Ambiguous ||
			    Find(Module, "", Index, &Ambiguous) || Index != 0xffff || Ambiguous ||
			    Find(Module, "Missing", Index, &Ambiguous) || Index != 0xffff || Ambiguous)
			{
				return false;
			}
			return true;
		};
		const bool Result = CheckTable(Module.Types, ModuleMeta::FindTypeIndex) &&
		    CheckTable(Module.Interfaces, ModuleMeta::FindInterfaceIndex);
		if (!Result)
		{
			std::cerr << "canonical runtime lookup changed exact, alias or ambiguity semantics\n";
		}
		return Result;
	}

	bool TestTypeCompatibilityReuse()
	{
		using namespace VeroScript;
		// Layout queries on borrowed metadata do not start an execution. They must
		// observe edits even when module address, type names and table size stay equal.
		{
			FBytecodeModule Metadata;
			Metadata.Types.resize(3);
			Metadata.Types[0].Name = "Base";
			Metadata.Types[1].Name = "Child";
			Metadata.Types[1].BaseTypeIndices = {0};
			Metadata.Types[2].Name = "Holder";
			for (auto &Type : Metadata.Types)
			{
				Type.Kind = EScriptObjectKind::Class;
			}
			FBytecodeFieldDesc Field;
			Field.Name = "item";
			Field.TypeName = "Base";
			Metadata.Types[2].Fields.push_back(Field);
			auto Child = std::make_shared<FScriptObject>();
			Child->SetTypeName("Child");
			auto Holder = std::make_shared<FScriptObject>();
			Holder->SetTypeName("Holder");
			Holder->Fields.push_back(Child);
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			if (!Vm.ValidateObjectLayout(Metadata, Holder)) return false;
			Metadata.Types[1].BaseTypeIndices.clear();
			if (Vm.ValidateObjectLayout(Metadata, Holder)) return false;
			Metadata.Types[1].BaseTypeIndices = {0};
			if (!Vm.ValidateObjectLayout(Metadata, Holder)) return false;
		}
		auto Compiled = VeroScriptCompile::CompileSource(
		    "class Base{public:int value=7;};class Child:public Base{};"
		    "void main(){array<Base> values=array<Base>();for(int i=0;i<4;i++){values.push(Child());}print(len(values));}",
		    "type-compatibility-reuse.vs");
		if (!Compiled.bSuccess)
		{
			std::cerr << Compiled.Error.Message << '\n';
			return false;
		}
		std::shared_ptr<FBytecodeModule> Module = std::move(Compiled.Module);
		std::uint16_t Child = 0;
		if (!ModuleMeta::FindTypeIndex(*Module, "Child", Child)) return false;
		const auto Bases = Module->Types[Child].BaseTypeIndices;
		for (const bool Owned : {false, true})
		{
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			std::string Error;
			if (!BackendPath.empty() && !Vm.SetJitBackendPath(BackendPath, Error)) return false;
			Vm.SetExecutionBackend(BackendPath.empty() ? EExecutionBackend::Vm : EExecutionBackend::Jit);
			if (Owned && !Vm.SetJitProgramOwner(Module, Error)) return false;
			if (!Vm.Execute(*Module, ERunMode::Script) || Output.str() != "4\n") return false;
			// Borrowed modules may change between calls. Owned modules must publish a
			// new generation before executing a changed layout at the same address.
			Module->Types[Child].BaseTypeIndices.clear();
			if (Owned)
			{
				Vm.SetProgramGeneration(2);
			}
			const bool Rejected = !Vm.Execute(*Module, ERunMode::Script);
			Module->Types[Child].BaseTypeIndices = Bases;
			if (Owned)
			{
				Vm.SetProgramGeneration(3);
			}
			Output.str("");
			if (!Rejected || !Vm.Execute(*Module, ERunMode::Script) || Output.str() != "4\n")
			{
				std::cerr << "type compatibility survived a changed module or generation\n";
				return false;
			}
		}
		return true;
	}

	bool TestSoftTypeValueIndices()
	{
		using namespace VeroScript;
		std::size_t AliasCount = 0;
		const auto *Aliases = StandardLibrary::GetTypeAliases(AliasCount);
		std::vector<std::string> Names;
		for (std::size_t I = 0; I < AliasCount; ++I)
		{
			for (const auto Name : {Aliases[I].QualifiedName, Aliases[I].CanonicalName})
			{
				if (Name.find('.') != std::string_view::npos)
				{
					continue;
				}
				VeroScriptLexer Lexer(std::string(Name), "soft-name.vs");
				const auto Tokens = Lexer.Tokenize();
				if (Tokens.size() != 2 || Tokens.front().Kind != ETokenKind::Identifier)
				{
					continue;
				}
				if (std::find(Names.begin(), Names.end(), Name) == Names.end())
				{
					Names.emplace_back(Name);
				}
			}
		}
		for (std::size_t I = 0; I < Names.size(); ++I)
		{
			const auto &Name = Names[I];
			const std::string Source = "class Item{public:int value=7;};array<Item> " + Name + "={Item(),Item()};void main(){" + Name + "[1].value=9;print(" + Name + "[0].value);print(" + Name + "[1].value);}";
			auto Result = VeroScriptCompile::CompileSource(Source, "soft-value-indices.vs");
			if (!Result.bSuccess || !Run(*Result.Module, "7\n9\n", I))
			{
				std::cerr << "soft type value indices " << Name << ": " << Result.Error.Message << '\n';
				return false;
			}
		}
		return true;
	}
} // namespace

int main(const int Argc, char **Argv)
{
	if (Argc > 1)
	{
		BackendPath = Argv[1];
	}
	const FCase Cases[] = {
	    {"struct pair<T>{T value;};void main(){pair<int,string> pair=pair<int,string>();"
	     "pair<int> user=pair<int>();user.value=7;print(user.value);}",
	        "7\n"},
	    {"void main(){pair<int,string> pair=pair<int,string>();pair<int,int> other=pair<int,int>();"
	     "other.second=7;print(other.second);}",
	        "7\n"},
	    {"class Item{public:int value=42;};array<Item> shared={Item(),Item()};"
	     "int first(){return shared[0].value;}int second(){return shared[1].value;}"
	     "void main(){shared[1].value=7;print(first());print(second());}",
	        "42\n7\n"},
	    {"class Item{public:int value=7;};void main(){array<Item> items={Item(),Item()};"
	     "array<Item> weak=items;array<Item> readonly=items;int index=1;weak[index].value=9;"
	     "print(items[0].value);print(weak[0].value);print(readonly[0].value);"
	     "print(items[1].value);print(weak[1].value);print(readonly[index].value);}",
	        "7\n7\n7\n9\n9\n9\n"},
	    {"class Item{public:int value=7;};int read(array<Item> shared){return shared[0].value+shared[1].value;}"
	     "void main(){array<Item> items={Item(),Item()};print(read(items));}",
	        "14\n"},
	    {"class Item{public:int value=7;};void main(){shared<int> owner=shared<int>(3);"
	     "array<Item> shared={Item()};std::shared<int> named=std::shared<int>(4);"
	     "print(owner.value);print(named.value);print(shared[0].value);}",
	        "3\n4\n7\n"},
	    {"struct pair{int value=5;};void main(){auto values=pair[2]{pair(),pair()};"
	     "print(values[0].value);print(values[1].value);}",
	        "5\n5\n"},
	    {"class Item{public:int value=7;};array<Item> pair={Item()};void main(){"
	     "{using pair=Item;auto values=pair[2]{Item(),Item()};print(values[0].value);}print(pair[0].value);}",
	        "7\n7\n"},
	    {"void main(){int values[0];}", nullptr, "array"},
	    {"using Empty=int[0];void main(){}", nullptr, "array"},
	    {"struct pair{int value=5;};void main(){auto values=pair[0]{};}", nullptr, "array"},
	    {"struct attribute<T>{T tag;};struct task<T>{T tag;};struct task_group<T>{T tag;};"
	     "class Item{public:int value=7;};array<Item> attribute={Item()};array<Item> task={Item()};"
	     "array<Item> task_group={Item()};void main(){print(attribute[0].value+task[0].value+task_group[0].value);}",
	        "21\n"},
	    {"void main(){auto values=module_info[2]{module_info(),module_info()};print(len(values));}", "2\n"},
	    {"array<fn()->int> module_info={[]()->int{return 7;}};"
	     "array<fn()->int> shared={[]()->int{return 9;}};"
	     "void main(){print(module_info[0]());print(shared[0]());}",
	        "7\n9\n"},
	    {"class Item{public:int value=7;};array<Item> shared={Item()};array<int> readonly={0};"
	     "void main(){print(shared[readonly[readonly[0]]].value);}",
	        "7\n"},
	    {"void main(){auto values=module_info[0]{};}", nullptr, "array"},
	    {"struct Outer{struct Inner{int x=7;};};void main(){Outer::Inner "
	     "i=Outer::Inner();print(i.x);}",
	        "7\n"},
	    {"class Outer{public:struct Inner{int x=7;};Inner get(){return "
	     "Inner();}};void main(){Outer "
	     "o=Outer();auto "
	     "i=o.get();print(i.x);}",
	        "7\n"},
	    {"class Outer{public:enum class State{Ready=3};};void "
	     "main(){print(Outer::State::Ready);}",
	        "Outer.State(3)\n"},
	    {"class Outer{public:struct Inner{int x=7;};using Item=Inner;};void "
	     "main(){Outer::Item "
	     "i=Outer::Item();print(i.x);}",
	        "7\n"},
	    {"void main(){struct Local{int x=7;};Local a=Local();print(a.x);}", "7\n"},
	    {"void main(){struct Local{int x=7;};{struct Local{int x=9;};Local "
	     "a=Local();print(a.x);}Local "
	     "a=Local();print(a.x);}",
	        "9\n7\n"},
	    {"void main(){using Value=int;struct Local{Value x=7;};Local "
	     "a=Local();print(a.x);}",
	        "7\n"},
	    {"class Outer<T>{public:struct Inner{T x;};};void "
	     "main(){Outer<int>::Inner "
	     "i=Outer<int>::Inner();i.x=7;print(i.x);}",
	        "7\n"},
	    {"class Outer<T>{public:struct Inner<U>{T x;U y;};};void "
	     "main(){Outer<int>::Inner<string> "
	     "i=Outer<int>::Inner<string>();i.x=7;i.y=\"ok\";print(\"{} "
	     "{}\",i.x,i.y);}",
	        "7 ok\n"},
	    {"template<int N> class Outer{public:template<int M> struct Inner{int read(){return N+M;}};"
	     "int read(){Inner<4> value=Inner<4>();return value.read();}};"
	     "void main(){Outer<3> value=Outer<3>();print(value.read());}",
	        "7\n"},
	    {"class Outer<T>{public:struct Inner{T x=7;};int read(){Inner value=Inner();return value.x;}};"
	     "void main(){Outer<int> value=Outer<int>();print(value.read());}",
	        "7\n"},
	    {"template<int N> struct Outer{int read(){Inner value=Inner();return value.read();}"
	     "struct Inner{int read(){return N;}};};void main(){Outer<3> value=Outer<3>();print(value.read());}",
	        "3\n"},
	    {"struct Item{int x=9;};class Base{protected:struct Item{int x=7;};};class Derived:Base{public:"
	     "int read(){Item value=Item();return value.x;}};void main(){Derived value=Derived();print(value.read());}",
	        "7\n"},
	    {"template<int N> struct Base{struct Item{int read(){return N;}};};"
	     "template<int N> struct Derived:Base<N>{int read(){Item value=Item();return value.read();}};"
	     "void main(){Derived<3> value=Derived<3>();print(value.read());}",
	        "3\n"},
	    {"class Base{public:struct Item{int x=9;};};class Derived:Base{public:"
	     "int read(){Item value=Item();return value.x;}struct Item{int x=7;};};"
	     "void main(){Derived value=Derived();print(value.read());}",
	        "7\n"},
	    {"void work<T>(T input){struct Local{T x;};Local "
	     "a=Local();a.x=input;print(a.x);}void "
	     "main(){work<int>(7);}",
	        "7\n"},
	    {"void main(){auto f=[]()->int{struct Local{int x=7;};Local "
	     "a=Local();return a.x;};print(f());}",
	        "7\n"},
	    {"class Outer{private:int x=7;public:struct Reader{int read(Outer "
	     "o){return o.x;}};};void main(){Outer "
	     "o=Outer();Outer::Reader r=Outer::Reader();print(r.read(o));}",
	        "7\n"},
	    {"class Outer{private:struct Inner{int x=7;};};void main(){Outer::Inner "
	     "i=Outer::Inner();}",
	        nullptr},
	    {"void main(){int input=7;struct Local{int get(){return input;}};Local "
	     "a=Local();print(a.get());}",
	        nullptr},
	    {"class Outer<T>{public:struct Inner<U=string>{T x;U y;};};void "
	     "main(){Outer<int>::Inner<> "
	     "i=Outer<int>::Inner<>();i.x=7;i.y=\"ok\";print(\"{} {}\",i.x,i.y);}",
	        "7 ok\n"},
	    {"class A<T>{public:class B<U>{public:struct C<V>{T x;U y;V z;};};};void "
	     "main(){A<int>::B<string>::C<bool> "
	     "c=A<int>::B<string>::C<bool>();c.x=7;c.y=\"ok\";c.z=true;print(\"{} {} "
	     "{}\",c.x,c.y,c.z);}",
	        "7 ok true\n"},
	    {"void work<T>(T x){struct L<U>{T a;U b;};L<string> "
	     "l=L<string>();l.a=x;l.b=\"ok\";print(\"{} "
	     "{}\",l.a,l.b);}void main(){work<int>(7);}",
	        "7 ok\n"},
	    {"class A<T>{public:struct Marker{};};void main(){A<int>::Marker "
	     "a=A<int>::Marker();A<string>::Marker "
	     "b=a;}",
	        nullptr},
	    {"class A{public:class B{private:int x=7;};class C{int read(B b){return "
	     "b.x;}};};void main(){}",
	        nullptr},
	    {"class A{public:class B{private:int x=7;};int read(B b){return "
	     "b.x;}};void main(){}",
	        nullptr},
	    {"int value=9;void main(){int value=7;struct L{int get(){return "
	     "value;}};L l=L();print(l.get());}",
	        nullptr},
	    {"class Base{public:struct Item{int x=7;};};class Derived:public "
	     "Base{};void main(){Derived::Item "
	     "i=Derived::Item();print(i.x);}",
	        "7\n"},
	    {"class A{public:struct Item{};int Item(){return 1;}};void main(){}", nullptr},
	    {"void main(){if(true)struct Item{int x;};}", nullptr},
	    {"class A<T>{public:enum class State{Ready=3};};void "
	     "main(){A<int>::State "
	     "x=A<int>::State::Ready;print(x==A<int>::State::Ready);}",
	        "true\n"},
	    {"class A<T>{public:struct Item{T x=7;};using Alias=Item;};void "
	     "main(){A<int>::Alias "
	     "a=A<int>::Alias();print(a.x);}",
	        "7\n"},
	    {"using P=A<int>;class A<T>{public:struct Item{T x=7;};};void "
	     "main(){P::Item a=P::Item();print(a.x);}",
	        "7\n"},
	    {"class A{public:struct X{};private:struct Y{};public:int count(){return "
	     "type<A>().nested_type_count();}};void "
	     "main(){print(type<A>().nested_type_count());A a=A();print(a.count());}",
	        "1\n2\n"},
	    {"class A{public:struct X{};private:struct Y{};public:int count(){type<A> "
	     "descriptors[1];return descriptors[0].nested_type_count();}};void main(){"
	     "A value=A();print(value.count());type<A> descriptors[1];"
	     "print(descriptors[0].nested_type_count());}",
	        "2\n1\n"},
	    {"class A{public:struct X{};private:struct Y{};public:int count(){type<A> "
	     "descriptors[2][2]={};return descriptors[1][1].nested_type_count();}};"
	     "void main(){type<A> descriptors[2][2]={};print(descriptors[1][1].nested_type_count());"
	     "A value=A();print(value.count());}",
	        "1\n2\n"},
	    {"class A{private:struct Hidden{int value=7;};public:int count(){type<Hidden> "
	     "descriptors[1];return descriptors[0].field_count();}};void main(){A value=A();print(value.count());}",
	        "1\n"},
	    {"class A<T>{public:struct X<U>{U x;};struct Y{};};void main(){auto "
	     "t=type<A<int>>();print(t.nested_type_count());print(t.nested_type_name("
	     "0));print(t.nested_type_"
	     "parameter_"
	     "count(0));print(t.nested_type_declaration(0).is_valid());}",
	        "2\nX\n1\ntrue\n"},
	    {"void main(){struct Item{int x=7;};auto "
	     "t=type<Item>();print(t.is_local());print(t.name);}",
	        "true\nItem\n"},
	    {"class A{public:struct Item{int x=7;};};void main(){using P=A;P::Item "
	     "a=P::Item();print(a.x);}",
	        "7\n"},
	    {"void main(){struct L{Later x;};using Later=int;}", nullptr},
	    {"class A<T>{public:Later x;struct Later{T v;};};void main(){A<int> "
	     "a=A<int>();}",
	        ""},
	    {"static class A{public:struct Item{int x=7;};};void main(){A::Item "
	     "a=A::Item();print(a.x);}",
	        "7\n"},
	    {"void main(){{struct A{struct Item{int x=7;};};struct B{struct Item{int "
	     "x=9;};};A::Item "
	     "a=A::Item();B::Item "
	     "b=B::Item();print(\"{} {}\",a.x,b.x);}}",
	        "7 9\n"},
	    {"class A{private:struct Hidden{int x=7;};public:using "
	     "Item=Hidden;};void main(){A::Item "
	     "a=A::Item();print(a.x);}",
	        "7\n"},
	    {"class A{private:struct Hidden{};using Item=Hidden;};void "
	     "main(){A::Item a=A::Item();}",
	        nullptr},
	    {"struct Item{int x=5;};void main(){Item a=Item();struct Item{int "
	     "x=7;};Item b=Item();print(\"{} "
	     "{}\",a.x,b.x);}",
	        "5 7\n"},
	    {"void main(){struct Item{};struct Item{};}", nullptr},
	    {"void main(){struct Item{int get(int x){return x;}};Item "
	     "item=Item();print(item.get(7));}",
	        "7\n"},
	    {"class A<T>{public:struct T{};};void main(){}", nullptr},
	    {"void main(){@Note struct Item{int x=7;};Item i=Item();print(i.x);}", "7\n"},
	    {"interface I{public:struct Item{int x=7;};virtual int value()=0;};void "
	     "main(){I::Item "
	     "i=I::Item();print(i.x);}",
	        "7\n"},
	    {"void main(){auto f=[]()->int{struct Item{int x=7;};Item "
	     "i=Item();return i.x;};print(f());}",
	        "7\n"},
	    {"void main(){for(int x=0;x<2;x++){struct Item{int x=7;};Item "
	     "i=Item();print(i.x);}}",
	        "7\n7\n"},
	    {"void main(){if(true){struct Item{int x=7;};Item i=Item();print(i.x);}}", "7\n"},
	    {"void main(){switch(1){case 1:{struct Item{int x=7;};Item "
	     "i=Item();print(i.x);break;}default:break;}}",
	        "7\n"},
	    {"void main(){int[] a=[1];for(int x:a){struct Item{int get(){return "
	     "x;}};}}",
	        nullptr},
	    {"void main(){int x=7;struct Item{void set(){x=8;}};Item "
	     "i=Item();i.set();}",
	        nullptr},
	    {"class A{int x=7;public:void run(){struct Item{int get(){return "
	     "x;}};Item "
	     "i=Item();print(i.get());}};void "
	     "main(){}",
	        nullptr},
	    {"class A{private:int x=7;public:void run(){struct Item{int get(A "
	     "a){return a.x;}};Item "
	     "i=Item();print(i.get(this));}};void main(){A a=A();a.run();}",
	        "7\n"},
	    {"class A{private:struct Hidden{int x=7;};public:using "
	     "Item=Hidden;};using Item=A::Item;void main(){Item "
	     "i=Item();print(i.x);}",
	        "7\n"},
	    {"class A{public:struct Item{int x=7;};};class B:public A{};class "
	     "C:public A{};class D:public B,public "
	     "C{};void main(){D::Item i=D::Item();print(i.x);}",
	        nullptr},
	    {"class A{public:struct Item{};};class B{public:struct Item{};};class "
	     "D:public A,public B{};void "
	     "main(){D::Item i=D::Item();}",
	        nullptr},
	    {"class A{protected:struct Item{int x=7;};};class D:public "
	     "A{public:using A::Item;};void main(){D::Item "
	     "i=D::Item();print(i.x);}",
	        "7\n"},
	    {"class A{private:struct Item{};};class D:public A{public:using "
	     "A::Item;};void main(){D::Item "
	     "i=D::Item();}",
	        nullptr},
	    {"class A<T>{public:struct Item{T x=7;};};class D<T>:public A<T>{};void "
	     "main(){D<int>::Item "
	     "i=D<int>::Item();print(i.x);}",
	        "7\n"},
	    {"class A<T>{public:class B<U>{public:struct Item{int x=7;};};};using "
	     "P=A<int>::B<string>;void "
	     "main(){P::Item "
	     "i=P::Item();print(i.x);}",
	        "7\n"},
	    {"void main(){using Value=int;struct L{using Value=string;Value "
	     "x=\"ok\";};L l=L();print(l.x);}",
	        "ok\n"},
	    {"void main(){struct L{Later x;using Later=int;};L "
	     "l=L();l.x=7;print(l.x);}",
	        "7\n"},
	    {"struct Value{int value=7;};struct Box{Alias item;typedef Alias=Value;};"
	     "void main(){Box box=Box();print(box.item.value);}",
	        "7\n"},
	    {"struct Value{int value=7;};struct Box<T>{Alias item;typedef Alias=T;};"
	     "void main(){Box<Value> box=Box<Value>();print(box.item.value);}",
	        "7\n"},
	    {"void main(){struct L{int get(){auto f=[]()->int{return 7;};return "
	     "f();}};L l=L();print(l.get());}",
	        "7\n"},
	    {"void work<T>(T input){struct L<U>{U x;};L<int> a=L<int>();L<string> "
	     "b=L<string>();print(type<L<int>>().declaring_function_name()==type<L<"
	     "string>>().declaring_function_name("
	     "));}"
	     "void main(){work<int>(7);}",
	        "true\n"},
	    {"class A{public:struct L{int x=7;};};void main(){::A::L "
	     "a=::A::L();print(a.x);}",
	        "7\n"},
	    {"void main(){struct A{struct Item{int x=7;};};A::Item "
	     "a=A::Item();print(a.x);}",
	        "7\n"},
	    {"void main(){struct Item{};int Item=7;}", nullptr},
	    {"void main(){int Item=7;struct Item{};}", nullptr},
	    {"struct A{struct L{};};void main(){int A=7;A::L a=A::L();}", nullptr},
	    {"struct A{struct L{int value=7;};};void main(){int "
	     "A=A::L().value;print(A);}",
	        "7\n"},
	    {"struct A{struct L{int value=7;};};void main(){int "
	     "first=1,A=A::L().value;print(first+A);}",
	        "8\n"},
	    {"struct A{struct L{int value=7;};};void main(){for(int "
	     "first=1,A=A::L().value;first<2;++first)"
	     "{print(first+A);}}",
	        "8\n"},
	    {"struct A{struct L{int value=7;};};void main(){int "
	     "A=1,second=A::L().value;}",
	        nullptr},
	    {"class A{private:struct Item{};};void main(){using Item=A::Item;}", nullptr},
	    {"class A{private:struct Item{};public:Item get(){return Item();}};void "
	     "main(){A a=A();auto i=a.get();}",
	        ""},
	    {"class A{private:static int value=7;public:struct Reader{int "
	     "read(){return value;}};};void "
	     "main(){A::Reader "
	     "r=A::Reader();print(r.read());}",
	        "7\n"},
	    {"class A{private:static int value=7;public:void run(){struct Reader{int "
	     "read(){return value;}};Reader "
	     "r=Reader();print(r.read());}};void main(){A a=A();a.run();}",
	        "7\n"},
	    {"int value=9;class A{private:int value=7;public:struct Reader{int "
	     "read(){return value;}};};void "
	     "main(){A::Reader r=A::Reader();print(r.read());}",
	        nullptr},
	    {"class A{private:struct Hidden{int value=7;};friend class "
	     "Reader;};class Reader{public:int "
	     "read(){A::Hidden "
	     "h=A::Hidden();return h.value;}};void main(){Reader "
	     "r=Reader();print(r.read());}",
	        "7\n"},
	    {"class A{private:struct Hidden{int value=7;};friend int read();};int "
	     "read(){A::Hidden "
	     "h=A::Hidden();return "
	     "h.value;}void main(){print(read());}",
	        "7\n"},
	    {"class A{private:int value=7;friend int read(A a);};int read(A "
	     "a){struct Reader{int read(A a){return "
	     "a.value;}};Reader r=Reader();return r.read(a);}void "
	     "main(){print(read(A()));}",
	        "7\n"},
	    {"class A<T>{public:struct Item{T value;};};class B<T>:public "
	     "A<array<T>>{};void main(){B<int>::Item "
	     "x=B<int>::Item();x.value=array<int>{7};print(x.value[0]);}",
	        "7\n"},
	    {"class A{private:struct Hidden{struct Child{int "
	     "value=7;};};public:using Public=Hidden;};void "
	     "main(){using "
	     "P=A::Public;P::Child c=P::Child();print(c.value);}",
	        "7\n"},
	    {"class A{private:struct Hidden{struct Child{int "
	     "value=7;};};public:using Public=Hidden;};void "
	     "main(){A::Public::Child c=A::Public::Child();print(c.value);}",
	        "7\n"},
	    {"namespace N{struct T{int value=7;};}class A<T>{public:T tag;using "
	     "Item=N::T;};void main(){A<int>::Item "
	     "x=A<int>::Item();print(x.value);}",
	        "7\n"},
	    {"class A{public:struct L{};};class B{public:int L=7;};void main(){B "
	     "A=B();print(A.L);}",
	        "7\n"},
	    {"using Number=int;void main(){Number x=cast<::Number>(7);print(x);}", "7\n"},
	    {"void main(){enum class State{Ready=7};State "
	     "s=State::Ready;print(s==State::Ready);}",
	        "true\n"},
	    {"void main(){enum State{Ready=7};print(Ready);}", "7\n"},
	    {"void work<T>(T input){enum class "
	     "State{Ready=7};print(State::Ready==State::Ready);}void "
	     "main(){work<int>(7);}",
	        "true\n"},
	    {"interface I{virtual int read()=0;}class A<T> where T:I{public:struct "
	     "Item{T value;};};void "
	     "main(){A<int>::Item x=A<int>::Item();}",
	        nullptr},
	    {"interface I{virtual int read()=0;}class Value:public I{public:int "
	     "read(){return 7;}};class A<T> where "
	     "T:I{public:struct Item{T value;};};void main(){A<Value>::Item "
	     "x=A<Value>::Item();x.value=Value();print(x.value.read());}",
	        "7\n"},
	    {"int count(){struct State{static int value=0;};State::value+=1;return "
	     "State::value;}void "
	     "main(){print(count());print(count());}",
	        "1\n2\n"},
	    {"class A{private:struct Item{};public:struct B:public Item{};};void "
	     "main(){A::B b=A::B();}",
	        ""},
	    {"class A:public Inner{public:class Inner{};};void main(){}", nullptr},
	    {"class A<T>{private:static T value=7;public:struct B{T get(){return "
	     "value;}};};void main(){A<int>::B "
	     "b=A<int>::B();print(b.get());}",
	        "7\n"},
	    {"struct Base{public:int value=7;};void main(){int value=9;struct "
	     "L:public Base{int get(){return "
	     "value;}};L "
	     "x=L();print(x.get());}",
	        "7\n"},
	    {"class A{private:struct Hidden{};friend int read();};int read(){return "
	     "7;}void main(){A::Hidden "
	     "h=A::Hidden();}",
	        nullptr},
	    {"class A{private:int value=7;friend int read(A a);};int read(A "
	     "a){return 7;}int other(A a){struct L{int "
	     "read(A a){return a.value;}};L l=L();return l.read(a);}void main(){}",
	        nullptr},
	    {"class A{private:struct Hidden{private:struct Child{};};public:using "
	     "Public=Hidden;};void main(){using "
	     "P=A::Public;P::Child c=P::Child();}",
	        nullptr},
	    {"void main(){{enum State{Ready=7};print(Ready);}print(Ready);}", nullptr},
	    {"void main(){enum State{Ready=7};{enum "
	     "State{Ready=9};print(Ready);}print(Ready);}",
	        "9\n7\n"},
	    {"struct A{struct B{int read();};};int A::B::read(){struct L{int "
	     "value=7;};L l=L();return l.value;}void "
	     "main(){A::B b=A::B();print(b.read());}",
	        "7\n"},
	    {"struct A<T>{struct B<U>{T left;U right;T read();};};T "
	     "A<T>::B<U>::read(){struct L{T value=7;};L "
	     "l=L();return "
	     "l.value;}void main(){A<int>::B<string> "
	     "b=A<int>::B<string>();print(b.read());}",
	        "7\n"},
	    {"struct A<T>{struct B<U>{U tag;T read();};};T A<T,U>::B::read(){return "
	     "7;}void main(){}",
	        nullptr},
	    {"void main(){enum State{Ready=7};struct Reader{int get(){return "
	     "Ready;}};Reader "
	     "r=Reader();print(r.get());}",
	        "7\n"},
	    {"void main(){struct Reader{int get(){return Ready;}};enum "
	     "State{Ready=7};Reader "
	     "r=Reader();print(r.get());}",
	        nullptr},
	    {"class A{private:static int value(int x){return x+1;}public:struct "
	     "B{int get(){return value(6);}};};void "
	     "main(){A::B b=A::B();print(b.get());}",
	        "7\n"},
	    {"class A{private:int value(int x){return x+1;}public:struct B{int "
	     "get(){return value(6);}};};void "
	     "main(){A::B "
	     "b=A::B();print(b.get());}",
	        nullptr},
	    {"class A{public:struct Item{int value=7;};};class A<T>{public:T "
	     "tag;struct Item{T value=9;};};void "
	     "main(){A::Item a=A::Item();A<int>::Item "
	     "b=A<int>::Item();print(a.value);print(b.value);print(type<A>().nested_"
	     "type_count());print(type<A<int>>()"
	     "."
	     "nested_type_count());}",
	        "7\n9\n1\n1\n"},
	    {"interface I{virtual int read()=0;}class A<T> where T:I{public:T "
	     "value;using Alias=int;};void "
	     "main(){A<int>::Alias value=7;}",
	        nullptr},
	    {"class A{public:struct Item<U>{U value=7;};};class A<T>{public:T "
	     "tag;struct Item{T value=9;};};void "
	     "main(){A::Item<int> a=A::Item<int>();A<int>::Item "
	     "b=A<int>::Item();print(a.value);print(b.value);}",
	        "7\n9\n"},
	    {"class A{public:struct Item{};};class A<T>{public:T tag;private:struct "
	     "Item{};};void main(){A<int>::Item "
	     "x=A<int>::Item();}",
	        nullptr},
	    {"class A<T>{public:T tag;private:struct Item<U=string>{U value;};};void "
	     "main(){A<int>::Item<> "
	     "x=A<int>::Item<>();}",
	        nullptr},
	    {"interface I{virtual int read()=0;}class A<T> where T:I{public:T "
	     "tag;using Alias=int;};void work<T>(T "
	     "input){A<T>::Alias value=7;}void main(){work<int>(7);}",
	        nullptr},
	    {"class A{public:enum State{Ready=3};struct Reader{int get(){return "
	     "Ready;}};};class A<T>{public:T "
	     "tag;enum "
	     "State{Ready=7};struct Reader{int get(){return Ready;}};};void "
	     "main(){A::Reader "
	     "a=A::Reader();A<int>::Reader "
	     "b=A<int>::Reader();print(a.get());print(b.get());}",
	        "3\n7\n"},
	    {"namespace N{int value=7;class A{public:struct Reader{int get(){return "
	     "value;}};};}void "
	     "main(){N::A::Reader "
	     "r=N::A::Reader();print(r.get());}",
	        "7\n"},
	    {"class A{public:struct B{int x;B(int value);};};A::B::B(int "
	     "value){x=value;}void main(){A::B "
	     "b=A::B(7);print(b.x);}",
	        "7\n"},
	    {"class A{public:struct B{B();};};A::B::B(){struct L{int value=7;};L "
	     "l=L();print(l.value);}void "
	     "main(){A::B b=A::B();}",
	        "7\n"},
	    {"class A<T>{public:struct B<U>{T x;U y;B(T a,U b);};};A<T>::B<U>::B(T "
	     "a,U b){x=a;y=b;}void "
	     "main(){A<int>::B<string> "
	     "b=A<int>::B<string>(7,\"ok\");print(b.x);print(b.y);}",
	        "7\nok\n"},
	    {"void main(){struct L{int value;L(int "
	     "x){value=x;}~L(){print(value);}};L l=L(7);}",
	        "7\n"},
	    {"class B{public:virtual map<long,int> operator map<long,int>(){return "
	     "map<long,int>();}};class D:public "
	     "B{public:map<int64,int> operator map<int64,int>(){return "
	     "map<int64,int>();}};void main(){}",
	        ""},
	    {"class B{public:map<long,int> read(){return map<long,int>();}};void "
	     "main(){B b=B();auto "
	     "values=b.read();values[1l]=7;print(values[1l]);}",
	        "7\n"},
	    {"interface I{virtual int read()=0;}class V:public I{public:int "
	     "read(){return 7;}};class A<T> where "
	     "T:I{public:T v;using Alias=int;};void work<T>(T input){A<T>::Alias "
	     "value=7;print(value);}void "
	     "main(){work<V>(V());}",
	        "7\n"},
	    {"class A<T>{public:T v;struct B<U=string>{U v;};};void "
	     "main(){A<int>::B<> "
	     "b=A<int>::B<>();b.v=\"ok\";print(b.v);}",
	        "ok\n"},
	    {"class A<T>{public:T v;struct B<U>{U v;};};void main(){using "
	     "X=A<int>::B<string>;X "
	     "b=X();b.v=\"ok\";print(b.v);}",
	        "ok\n"},
	    {"class A{public:struct X{int v=7;};};class A<T>{public:T v;struct X{T "
	     "v=9;};};void "
	     "main(){print(type<A::X>().display_name());print(type<A<int>::X>()."
	     "display_name());}",
	        "A::X\nA<int>::X\n"},
	    {"interface I{virtual int read()=0;}class A<T> where T:I{public:T "
	     "v;using Alias=int;};void work<T>(T "
	     "input){A<T>::Alias value=7;}void main(){}",
	        ""},
	    {"interface I{virtual int read()=0;}class A<T,U=int> where U:I{public:T "
	     "x;U y;using Alias=int;};class "
	     "A<T>{public:T x;using Alias=int;};void main(){A<int>::Alias "
	     "value=7;print(value);}",
	        "7\n"},
	    {"class A{public:struct B{B(int value);};};A::B::B(int value){struct "
	     "L{int get(){return value;}};}void "
	     "main(){}",
	        nullptr, "capture"},
	    {"interface I{virtual int read()=0;}class A<T> where T:I{public:T "
	     "x;using Alias=int;};class B<U>{public:U "
	     "value;int read(){A<U>::Alias v=7;return v;}};void main(){B<int> "
	     "b=B<int>();print(b.read());}",
	        nullptr, "does not satisfy"},
	    {"void work<T>(T input){using T=int;}void main(){work<int>(7);}", nullptr, "generic parameter"},
	    {"void work<T>(T input){typedef T=int;}void main(){work<int>(7);}", nullptr, "generic parameter"}};
	bool Passed = TestIncrementalParserTypeLookup();
	Passed = TestCanonicalRuntimeLookup() && Passed;
	Passed = TestTypeCompatibilityReuse() && Passed;
	Passed = TestSoftTypeValueIndices() && Passed;
	for (const std::string Kind : {"class", "struct", "interface"})
	{
		const std::string Source = "template<int N> " + Kind + " Broken{int value=;};\n"
		                                                       "class Good{public:struct N{};N make(){return N();}};\n"
		                                                       "void bad(){print(; }\n";
		VeroScript::VeroScriptLexer Lexer(Source, "scoped-recovery.vs");
		VeroScript::VeroScriptParser Parser(Lexer.Tokenize(), "scoped-recovery.vs");
		Parser.SetCollectMultipleErrors(true, 16);
		(void)Parser.ParseProgram();
		if (Parser.GetDiagnostics().size() < 2 ||
		    std::any_of(Parser.GetDiagnostics().begin(), Parser.GetDiagnostics().end(),
		        [](const auto &Diagnostic)
		{
			return Diagnostic.Location.Line == 2;
		}))
		{
			std::cerr << "type parse error leaked its declaration scope: " << Kind << '\n';
			Passed = false;
		}
	}
	{
		const auto Duplicate = VeroScript::VeroScriptCompile::CompileSource(
		    "\n\nclass A{public:struct X{};\nusing X=int;};void main(){}", "scoped-duplicate.vs");
		if (Duplicate.bSuccess || Duplicate.Error.Location.Line != 4)
		{
			std::cerr << "nested duplicate must report its declaration location: " << Duplicate.Error.Message
			          << '\n';
			Passed = false;
		}
	}
	for (std::size_t Index = 0; Index < sizeof(Cases) / sizeof(Cases[0]); ++Index)
	{
		const auto &Case = Cases[Index];
		auto Result = VeroScript::VeroScriptCompile::CompileSource(Case.Source, "scoped-types.vs");
		if (!Case.Expected)
		{
			if (Result.bSuccess)
			{
				std::cerr << "case " << Index << " unexpectedly compiled\n";
				Passed = false;
			}
			if (!Result.bSuccess && Case.ErrorCategory &&
			    Result.Error.Message.find(Case.ErrorCategory) == std::string::npos)
			{
				std::cerr << "case " << Index << " expected " << Case.ErrorCategory << ": "
				          << Result.Error.Message << '\n';
				Passed = false;
			}
			continue;
		}
		if (!Result.bSuccess)
		{
			std::cerr << "case " << Index << ": " << Result.Error.Message << '\n'
			          << Case.Source << '\n';
			Passed = false;
			continue;
		}
		Passed = Run(*Result.Module, Case.Expected, Index) && Passed;
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Result.Module, 321, Bytes, Error))
		{
			std::cerr << "case " << Index << " serialization: " << Error << '\n';
			Passed = false;
			continue;
		}
		auto Loaded =
		    VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(VeroScript::FByteView(Bytes), 321, Error);
		if (!Loaded)
		{
			std::cerr << "case " << Index << " deserialization: " << Error << '\n';
			Passed = false;
			continue;
		}
		Passed = Run(*Loaded, Case.Expected, Index) && Passed;
		Bytes.back() ^= 1;
		if (VeroScript::VeroScriptBytecodeIO::DeserializeBytecode(VeroScript::FByteView(Bytes), 321, Error))
		{
			std::cerr << "case " << Index << " accepted a mismatching bytecode layout\n";
			Passed = false;
		}
	}
	const FCase Definitions[] = {{"export class A{private:struct Hidden{int value=7;};public:struct B{int "
	                              "read();};};",
	                                 "int A::B::read(){Hidden h=Hidden();return h.value;}"},
	    {"export class A<T>{private:struct Hidden{T value=7;};public:struct "
	     "B<U>{U tag;T read();};};",
	        "T A<T>::B<U>::read(){Hidden h=Hidden();struct L{T value;};L "
	        "l=L();l.value=h.value;return l.value;}"}};
	for (std::size_t I = 0; I < sizeof(Definitions) / sizeof(Definitions[0]); ++I)
	{
		VeroScript::FCompileOptions DefinitionOptions = VeroScript::Test::MakeCompileOptions();
		DefinitionOptions.SourceOverlays = {
		    {"decl.vs", Definitions[I].Source},
		    {"impl.vs", "import \"decl.vs\";" + std::string(Definitions[I].Expected)}};
		auto Result = VeroScript::VeroScriptCompile::CompileSource(
		    "import \"decl.vs\";import \"impl.vs\";void main(){" +
		        std::string(I == 0 ? "A::B b=A::B();" : "A<int>::B<string> b=A<int>::B<string>();") +
		        "print(b.read());}",
		    "scoped-definition.vs", DefinitionOptions);
		if (!Result.bSuccess)
		{
			std::cerr << "definition " << I << ": " << Result.Error.Message << '\n';
			Passed = false;
		}
		else
		{
			Passed = Run(*Result.Module, "7\n", 900 + I) && Passed;
		}
	}
	{
		VeroScript::FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.SourceOverlays = {
		    {"lookup-a.vs", "class Outer{public:struct Item{int value=7;};};"
		                    "export int read_a(){Outer::Item item=Outer::Item();return item.value;}"
		                    "export int count_a(){return type<Outer>().nested_type_count();}"},
		    {"lookup-b.vs", "class Outer{public:struct Item{int value=9;};};"
		                    "export int read_b(){Outer::Item item=Outer::Item();return item.value;}"
		                    "export int count_b(){return type<Outer>().nested_type_count();}"}};
		for (int Repetition = 0; Repetition < 2; ++Repetition)
		{
			auto Result = VeroScript::VeroScriptCompile::CompileSource(
			    "import \"lookup-a.vs\";import \"lookup-b.vs\";void main(){"
			    "print(read_a());print(read_b());print(count_a());print(count_b());}",
			    "lookup-consumer.vs", Options);
			if (!Result.bSuccess)
			{
				std::cerr << "module-local lookup snapshot: " << Result.Error.Message << '\n';
				Passed = false;
			}
			else
			{
				Passed = Run(*Result.Module, "7\n9\n1\n1\n", 950 + Repetition) && Passed;
			}
		}
		auto Hidden = VeroScript::VeroScriptCompile::CompileSource(
		    "import \"lookup-a.vs\";import \"lookup-b.vs\";void main(){Outer::Item item=Outer::Item();}",
		    "lookup-hidden-consumer.vs", Options);
		if (Hidden.bSuccess)
		{
			std::cerr << "private module names escaped a shared forward lookup\n";
			Passed = false;
		}
	}

	auto Registry = std::make_shared<VeroScript::FCompileProcessorRegistry>();
	VeroScript::FCompileProcessorDesc Processor;
	Processor.ProcessorId = "scoped-types-generator";
	Processor.Version = "0.1.1";
	Processor.Phases = VeroScript::CompilePhaseMask(VeroScript::ECompileProcessingPhase::Members);
	Processor.Operations = VeroScript::CompileOperationMask(VeroScript::ECompileOperation::AddDeclaration);
	Processor.Subscriptions.push_back({VeroScript::ECompileProcessingEvent::Module});
	Processor.Factory = []
	{
		return std::make_unique<FScopedMemberGenerator>();
	};
	std::string Error;
	if (!Registry->Register(std::move(Processor), Error))
	{
		std::cerr << Error << '\n';
		return 1;
	}
	Registry->Freeze();
	VeroScript::FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.CompileProcessing.Registry = Registry;
	auto Generated = VeroScript::VeroScriptCompile::CompileSource(
	    "void main(){using Value=int; @Generate struct Item{Value x;}; Item "
	    "i=Item();print(i.generated());}",
	    "scoped-generation.vs", Options);
	if (!Generated.bSuccess)
	{
		std::cerr << "local processor: " << Generated.Error.Message << '\n';
		Passed = false;
	}
	else
	{
		Passed = Run(*Generated.Module, "7\n", 1000) && Passed;
	}
	const VeroScript::Tests::FScopedTemporaryDirectory Directory("VeroScript-scoped-types-");
	if (!Directory)
	{
		return 1;
	}
	{
		std::ofstream Dependency(Directory.Get() / "dependency.vs");
		Dependency << "export int dependency(){return 1;}";
		if (!Dependency)
		{
			return EXIT_FAILURE;
		}
	}

	const FCase GenerationCases[] = {
	    {"void main(){Generated g=Generated();Following "
	     "f=Following();print(g.value);print(f.item.value);}",
	        "7\n7\n"},
	    {"void main(){using Value=int;}", ""},
	    {"class Outer{public:Generated item;};void main(){Outer "
	     "o=Outer();print(o.item.value);}",
	        "7\n"},
	    {"void main(){Generated::Nested "
	     "item=Generated::Nested();print(item.value);}",
	        "7\n"},
	    {"export int compute(int value);void main(){print(compute(7));}", "7\n"},
	    {"int compute(){struct Old{int value;};return 0;}void "
	     "main(){print(compute());}",
	        "7\n"},
	    {"class Outer{public:void run(){print(0);}};void main(){}", nullptr},
	    {"void main(){print(0);}", nullptr},
	    {"import \"dependency.vs\";export void work<T>(T input){Generated "
	     "g=Generated();g.value=input;print(g.value);}void "
	     "main(){work<int>(7);}",
	        "7\n"},
	    {"int value=9;int compute(int value);void main(){print(compute(7));}", nullptr},
	    {"class A{private:struct Hidden{};};void main(){}", nullptr}};
	for (int Mode = 0; Mode < static_cast<int>(sizeof(GenerationCases) / sizeof(GenerationCases[0])); ++Mode)
	{
		auto Processing = std::make_shared<VeroScript::FCompileProcessorRegistry>();
		VeroScript::FCompileProcessorDesc Description;
		Description.ProcessorId = "scoped-generation";
		Description.Version = "0.1.1";
		Description.Phases = VeroScript::CompilePhaseMask(Mode == 4 || Mode == 5 || Mode == 9
		        ? VeroScript::ECompileProcessingPhase::Bodies
		        : VeroScript::ECompileProcessingPhase::Types);
		Description.Operations = VeroScript::CompileOperationMask(
		    Mode == 4 || Mode == 9 ? VeroScript::ECompileOperation::CompleteBody
		        : Mode == 5        ? VeroScript::ECompileOperation::ReplaceBody
		                           : VeroScript::ECompileOperation::AddDeclaration);
		if (Mode == 6)
		{
			Description.Operations |=
			    VeroScript::CompileOperationMask(VeroScript::ECompileOperation::ReplaceDeclaration);
		}
		Description.Subscriptions.push_back({VeroScript::ECompileProcessingEvent::Module});
		Description.Factory = [Mode]
		{
			return std::make_unique<FTypeGenerator>(Mode);
		};
		if (!Processing->Register(std::move(Description), Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
		Processing->Freeze();
		VeroScript::FCompileOptions Configuration = VeroScript::Test::MakeCompileOptions();
		Configuration.CompileProcessing.Registry = Processing;
		auto Result = VeroScript::VeroScriptCompile::CompileSource(
		    GenerationCases[Mode].Source,
		    (Directory.Get() / ("generator-" + std::to_string(Mode) + ".vs")).generic_string(),
		    Configuration);
		if (!GenerationCases[Mode].Expected)
		{
			const auto Category = Mode == 6 ? "conflict"
			    : Mode == 7                 ? "anchor"
			    : Mode == 10                ? "access"
			                                : "capture";
			if (Result.bSuccess || Result.Error.Message.find(Category) == std::string::npos)
			{
				std::cerr << "generator " << Mode << " expected " << Category << ": " << Result.Error.Message
				          << '\n';
				Passed = false;
			}
			continue;
		}
		if (!Result.bSuccess)
		{
			std::cerr << "generator " << Mode << ": " << Result.Error.Message << '\n';
			Passed = false;
			continue;
		}
		Passed = Run(*Result.Module, GenerationCases[Mode].Expected, 3000 + Mode) && Passed;
		if (Mode == 5)
		{
			for (const auto &Type : Result.Module->Types)
			{
				if (Type.Scope.SimpleName == "Old")
				{
					std::cerr << "body replacement retained the old local type\n";
					Passed = false;
				}
			}
		}
		if (Mode == 4 || Mode == 8)
		{
			const std::string Source = GenerationCases[Mode].Source;
			auto Library = VeroScript::VeroScriptCompile::CompileSource(
			    Source.substr(0, Source.find("void main()")),
			    (Directory.Get() / ("generator-" + std::to_string(Mode) + ".vs")).generic_string(),
			    Configuration);
			if (!Library.bSuccess)
			{
				std::cerr << "generator " << Mode << " library: " << Library.Error.Message << '\n';
				Passed = false;
				continue;
			}
			VeroScript::FPackageBuildOptions Package;
			Package.Kind = VeroScript::EPackageKind::Library;
			Package.PackageName = "generated-scope";
			Package.PackageVersion = Mode;
			std::vector<std::uint8_t> Bytes;
			if (!VeroScript::BuildPackage(*Library.Module, Package, Bytes, Error))
			{
				std::cerr << "generator " << Mode << " package: " << Error << '\n';
				Passed = false;
				continue;
			}
			const auto Path = Directory.Get() / "generated.vslib";
			{
				std::ofstream Out(Path, std::ios::binary);
				Out.write(reinterpret_cast<const char *>(Bytes.data()),
				    static_cast<std::streamsize>(Bytes.size()));
				if (!Out)
				{
					return 1;
				}
			}
			auto Consumer = VeroScript::VeroScriptCompile::CompileSource(
			    "import \"" + Path.generic_string() + "\";void main(){" +
			        (Mode == 4 ? "print(compute(7));" : "work<string>(\"ok\");") + "}",
			    (Directory.Get() / "consumer.vs").generic_string());
			if (!Consumer.bSuccess)
			{
				std::cerr << "generator " << Mode << " consumer: " << Consumer.Error.Message << '\n';
				Passed = false;
			}
			else
			{
				Passed = Run(*Consumer.Module, Mode == 4 ? "7\n" : "ok\n", 4000 + Mode) && Passed;
			}
		}
	}
	const FCase Libraries[] = {
	    {"export class Outer{public:struct Item{int x=7;};};", "Outer::Item i=Outer::Item();print(i.x);"},
	    {"export class Outer<T>{public:struct Item<U>{T x=7;U y=\"ok\";};};",
	        "Outer<int>::Item<string> i=Outer<int>::Item<string>();print(\"{} "
	        "{}\",i.x,i.y);"},
	    {"export int value(){struct Local{int x=7;};Local i=Local();return i.x;}", "print(value());"},
	    {"export int value<T>(T input){struct Local{T x;};Local "
	     "i=Local();i.x=input;return i.x;}",
	        "print(value<int>(7));"},
	    {"export class Outer{private:struct Hidden{int x=7;};public:using "
	     "Item=Hidden;};",
	        "Outer::Item i=Outer::Item();print(i.x);"}};
	for (std::size_t Index = 0; Index < sizeof(Libraries) / sizeof(Libraries[0]); ++Index)
	{
		const auto SourcePath = (Directory.Get() / "library.vs").generic_string();
		auto Library = VeroScript::VeroScriptCompile::CompileSource(Libraries[Index].Source, SourcePath);
		if (!Library.bSuccess)
		{
			std::cerr << "library " << Index << " compile: " << Library.Error.Message << '\n';
			Passed = false;
			continue;
		}
		VeroScript::FPackageBuildOptions Package;
		Package.Kind = VeroScript::EPackageKind::Library;
		Package.PackageName = "scoped-types";
		Package.PackageVersion = Index;
		Package.bStripDebugInfo = true;
		std::vector<std::uint8_t> Bytes;
		if (!VeroScript::BuildPackage(*Library.Module, Package, Bytes, Error))
		{
			std::cerr << "library " << Index << " package: " << Error << '\n';
			Passed = false;
			continue;
		}
		const auto LibraryPath = Directory.Get() / "scoped.vslib";
		{
			std::ofstream Out(LibraryPath, std::ios::binary);
			Out.write(reinterpret_cast<const char *>(Bytes.data()),
			    static_cast<std::streamsize>(Bytes.size()));
			if (!Out)
			{
				return 1;
			}
		}
		auto Consumer = VeroScript::VeroScriptCompile::CompileSource(
		    "import \"" + LibraryPath.generic_string() + "\";void main(){" + Libraries[Index].Expected + "}",
		    (Directory.Get() / "consumer.vs").generic_string());
		if (!Consumer.bSuccess)
		{
			std::cerr << "library " << Index << " consumer: " << Consumer.Error.Message << '\n';
			Passed = false;
		}
		else
		{
			Passed = Run(*Consumer.Module, Index == 1 ? "7 ok\n" : "7\n", 2000 + Index) && Passed;
		}
	}
	if (!BackendPath.empty() && (!BackendDispatches || !NativeEntries))
	{
		std::cerr << "the requested JIT backend did not emit and dispatch native code\n";
		Passed = false;
	}
	return Passed ? 0 : 1;
}
