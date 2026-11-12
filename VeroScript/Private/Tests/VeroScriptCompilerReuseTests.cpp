// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAst.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptRuntime.h"
#include "Parser/VeroScriptParserLookup.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using namespace VeroScript;

	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	std::unique_ptr<FProgram> Parse(const char* Source, const char* FileLabel)
	{
		VeroScriptModuleLoader Loader("");
		auto Program = Loader.LoadSourceModule(Source, FileLabel);
		if (!Program)
		{
			std::cerr << Loader.GetError().Message << '\n';
		}
		return Program;
	}

	bool TestDiagnosticsAfterFailure()
	{
		auto Invalid = Parse(
		    "int first(){return missing_first;} int second(){return missing_second;}"
		    "int third(){return missing_third;}",
		    "invalid.vs");
		auto Valid = Parse("int main(){return 42;}", "valid.vs");
		if (!Invalid || !Valid)
		{
			return false;
		}
		for (const bool CollectMultiple : {false, true})
		{
			VeroScriptCompiler Compiler;
			Compiler.SetCollectMultipleErrors(CollectMultiple, 2);
			for (const char* ReservedName : {"optional", "weak"})
			{
				if (!Expect(!Compiler.Compile(*Invalid) && Compiler.HasError() && Compiler.GetDiagnostics().size() == (CollectMultiple ? 2u : 1u),
				        "diagnostic collection configuration survives compiler reuse"))
				{
					return false;
				}
				FProgram Reserved;
				FStructDecl Declaration;
				Declaration.Name = ReservedName;
				Reserved.Structs.push_back(std::move(Declaration));
				const std::string Expected =
				    std::string("reserved builtin type name cannot be redeclared: ") + ReservedName;
				if (!Expect(!Compiler.Compile(Reserved) && Compiler.HasError() && Compiler.GetError().Message == Expected && Compiler.GetDiagnostics().size() == 1 && Compiler.GetDiagnostics().front().Message == Expected,
				        "early validation replaces previous diagnostics"))
				{
					return false;
				}
				if (!Expect(Compiler.Compile(*Valid) && !Compiler.HasError() && Compiler.GetError().Message.empty() && Compiler.GetDiagnostics().empty(),
				        "successful compile clears previous errors"))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool TestDeterministicReuse()
	{
		auto Program = Parse(
		    "fn(int)->int make(){return [](int x)->int=>x+2;}"
		    "void main(){print(make()(40));}",
		    "reuse.vs");
		auto Invalid = Parse(
		    "fn(int)->int make(){return [](int x)->int=>x+2;}"
		    "int main(){return missing;}",
		    "invalid-lambda.vs");
		if (!Program || !Invalid)
		{
			return false;
		}
		VeroScriptCompiler Compiler;
		std::vector<std::uint8_t> Baseline;
		std::string Error;
		for (int Iteration = 0; Iteration < 3; ++Iteration)
		{
			if (Iteration == 2 && !Expect(!Compiler.Compile(*Invalid), "failed compile after emitting a lambda"))
			{
				return false;
			}
			auto Module = Compiler.Compile(*Program);
			if (!Expect(Module && !Compiler.HasError(), "reused compiler succeeds"))
			{
				return false;
			}
			std::vector<std::uint8_t> Bytes;
			if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(*Module, 123, Bytes, Error),
			        "reused compiler emits valid serializable bytecode"))
			{
				return false;
			}
			if (Iteration == 0)
			{
				Baseline = Bytes;
			}
			else if (!Expect(Bytes == Baseline,
			             "identical input preserves bytecode and anonymous function identity"))
			{
				return false;
			}
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			if (!Expect(Vm.Execute(*Module, ERunMode::Script) && Output.str() == "42\n",
			        "repeatedly compiled program executes correctly"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestWideForeach()
	{
		auto Program = Parse(
		    "void main(){array<int> a=array<int>();a.push(10);a.push(20);a.push(30);"
		    "int sum=0;for(int value:a){if(value==20)continue;sum+=value;}print(sum);}",
		    "wide-foreach.vs");
		if (!Program)
		{
			return false;
		}
		VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Expect(Module != nullptr, "wide foreach fixture compiles"))
		{
			return false;
		}
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		return Expect(Vm.Execute(*Module, ERunMode::Script) && Output.str() == "40\n",
		    "wide foreach indices preserve continue behavior");
	}

	bool TestRetainedClosureMigration()
	{
		auto Program = Parse(
		    "template<int N> struct Gate{template<typename T> T add(T value){return value+N;}};"
		    "fn(int)->int saved = [](int x)->int{Gate<2> gate=Gate<2>();return gate.add(x);};"
		    "void main(){print(saved(40));}",
		    "reload.vs");
		if (!Program)
		{
			return false;
		}
		VeroScriptCompiler Compiler;
		auto Original = Compiler.Compile(*Program);
		auto Replacement = Compiler.Compile(*Program);
		if (!Original || !Replacement)
		{
			return false;
		}
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		FHotReloadState State;
		if (!Expect(Vm.ExecutePersistent(*Original, ERunMode::Script, State),
		        "initialize retained closure"))
		{
			return false;
		}
		std::string Error;
		if (!Expect(MigrateHotReloadState(State, *Replacement, nullptr, Error),
		        "identical source from reused compiler preserves retained closures"))
		{
			std::cerr << Error << '\n';
			return false;
		}
		return Expect(Vm.ExecutePersistent(*Replacement, ERunMode::Script, State) && Output.str() == "42\n42\n" && Vm.DiscardPersistentState(*Replacement, State),
		    "migrated closure remains callable and can be released");
	}

	bool TestCompilationStateIsolation()
	{
		const char* Sources[] = {
		    "namespace Alpha { struct S { int value; }; }"
		    "int main(){Alpha.S s=Alpha.S();return s.value;}",
		    "struct Box<T>{T value;}; int main(){Box<int> b=Box<int>();b.value=42;return b.value;}",
		    "int answer=42; int main(){return answer;}",
		    "int broken=missing_global; int main(){return broken;}",
		    "int answer=7; int main(){return answer;}",
		    "class Counter{public: static int initial=5; int value=1;"
		    "int get(){return value+Counter.initial;}} int main(){Counter c=Counter();return c.get();}",
		    "int main(){try{throw 1;}catch(int x){return x;}return 0;}",
		    "int main(){goto absent;return 0;}",
		    "fn()->int saved=[]()->int=>41; int main(){return saved();}",
		    "class Entry{public:virtual int read()=0;};"
		    "class Entry<T>{public:virtual T read(){return 7;}};"
		    "int main(){Entry<int> value=Entry<int>();return value.read();}",
		    "class Entry{public:virtual int read(){return 7;}};"
		    "class Entry<T>{public:virtual T read()=0;};"
		    "int main(){Entry<int> value=Entry<int>();return 0;}"};
		VeroScriptCompiler Reused;
		std::size_t SuccessfulInputs = 0;
		for (const char* Source : Sources)
		{
			auto Program = Parse(Source, "state-isolation.vs");
			if (!Program)
			{
				return false;
			}
			VeroScriptCompiler Fresh;
			auto Expected = Fresh.Compile(*Program);
			auto Actual = Reused.Compile(*Program);
			if (!Expect(static_cast<bool>(Actual) == static_cast<bool>(Expected) && Reused.HasError() == Fresh.HasError() && Reused.GetError().Message == Fresh.GetError().Message && Reused.GetDiagnostics().size() == Fresh.GetDiagnostics().size(),
			        "reused and fresh compilers agree across different programs and failures"))
			{
				return false;
			}
			if (!Expected)
			{
				continue;
			}
			++SuccessfulInputs;
			std::string Error;
			std::vector<std::uint8_t> ExpectedBytes, ActualBytes;
			if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(*Expected, 0, ExpectedBytes, Error) && VeroScriptBytecodeIO::SerializeBytecode(*Actual, 0, ActualBytes, Error) && ActualBytes == ExpectedBytes,
			        "previous scopes, generics, globals and exception state do not affect bytecode"))
			{
				return false;
			}
		}
		return Expect(SuccessfulInputs == 8, "state isolation covers eight valid programs and three errors");
	}

	bool TestHardFailureStartsFreshCompilation()
	{
		auto Invalid = Parse(
		    "int broken(int value){while(value>0){try{value--;}catch(int error){"
		    "return missing;}}return value;}",
		    "hard-failure.vs");
		auto Valid = Parse(
		    "struct Box<T>{static int seed=2;T value;};"
		    "fn(int)->int make(int offset){return [offset](int value)->int=>value+offset;}"
		    "int main(){Box<int> box=Box<int>();box.value=40;return make(Box<int>.seed)(box.value);}",
		    "fresh-after-failure.vs");
		if (!Invalid || !Valid)
		{
			return false;
		}

		VeroScriptCompiler Reused;
		Reused.SetCollectMultipleErrors(false);
		if (!Expect(!Reused.Compile(*Invalid), "hard failure interrupts the first compilation"))
		{
			return false;
		}
		VeroScriptCompiler Fresh;
		Fresh.SetCollectMultipleErrors(false);
		auto Expected = Fresh.Compile(*Valid);
		auto Actual = Reused.Compile(*Valid);
		if (!Expect(Expected && Actual && !Reused.HasError(),
		        "hard failure does not leak compiler scope state"))
		{
			return false;
		}
		std::string Error;
		std::vector<std::uint8_t> ExpectedBytes, ActualBytes;
		return Expect(VeroScriptBytecodeIO::SerializeBytecode(*Expected, 0, ExpectedBytes, Error) && VeroScriptBytecodeIO::SerializeBytecode(*Actual, 0, ActualBytes, Error) && ExpectedBytes == ActualBytes,
		    "fresh and reused compilers agree after an interrupting failure");
	}

	bool TestTypeResolutionDepth()
	{
		std::string Dimensions;
		for (std::size_t Index = 0; Index < Runtime::MaxNativeArrayDimensions; ++Index)
		{
			Dimensions += "[1]";
		}
		std::string ValueDimensions;
		std::string Indices;
		for (std::size_t Index = 0; Index < 16; ++Index)
		{
			ValueDimensions += "[1]";
			Indices += "[0]";
		}
		const std::string BoundarySource = "struct NativeBoundary{int boundary" + Dimensions + ";int value" + ValueDimensions + ";};"
		    "class IndexedBoundary{public:int value=42;IndexedBoundary operator[](int i){return this;}};"
		    "int main(){int boundary" + Dimensions + ";int local" + ValueDimensions + ";NativeBoundary item=NativeBoundary();"
		    "IndexedBoundary cursor=IndexedBoundary();assert(cursor" + Indices + ".value==42);"
		    "local" + Indices + "=21;item.value" + Indices + "=21;return local" + Indices + "+item.value" + Indices + ";}";
		auto Boundary = Parse(BoundarySource.c_str(), "native-array-type-boundary.vs");
		if (!Boundary) return false;
		VeroScriptCompiler BoundaryCompiler;
		auto BoundaryModule = BoundaryCompiler.Compile(*Boundary);
		if (!Expect(BoundaryModule && !BoundaryCompiler.HasError(),
		        "the native-array dimension limit remains valid for locals and fields"))
		{
			std::cerr << BoundaryCompiler.GetError().Message << '\n';
			return false;
		}
		std::ostringstream BoundaryOutput;
		VeroScriptVm BoundaryVm(BoundaryOutput);
		Test::Configure(BoundaryVm);
		if (!Expect(BoundaryVm.Execute(*BoundaryModule, ERunMode::Script) && BoundaryVm.GetProcessExitCode() == 42,
				"boundary native arrays construct and indexed locals and fields retain values"))
		{
			return false;
		}
		auto Valid = Parse(
		    "class Link{public:Link next;int value=7;};"
		    "class Left{public:Right next;};class Right{public:Left next;};"
		    "class Base{public:int value=7;};class Derived:public Base{};"
		    "struct Box<T>{T value;};"
		    "int main(){Box<int> box=Box<int>();box.value=35;"
		    "Link link=Link();Derived item=Derived();return box.value+link.value;}",
		    "valid-after-type-depth.vs");
		if (!Valid) return false;
		VeroScriptCompiler Fresh;
		auto Expected = Fresh.Compile(*Valid);
		std::vector<std::uint8_t> ExpectedBytes;
		std::string Error;
		if (!Expect(Expected && VeroScriptBytecodeIO::SerializeBytecode(*Expected, 0, ExpectedBytes, Error),
				"ordinary inheritance, generics and reference cycles compile"))
		{
			return false;
		}
		for (const bool CollectMultiple : {false, true})
		{
			VeroScriptCompiler Reused;
			Reused.SetCollectMultipleErrors(CollectMultiple, 2);
			for (int Kind = 0; Kind < 6; ++Kind)
			{
				std::string Source;
				for (int Index = 0; Index <= 400; ++Index)
				{
					const bool Generic = Kind == 2 || Kind == 3;
					const bool Struct = Kind == 1 || Kind == 3;
					const auto Type = [&](const int Number)
					{
						return "Node_" + std::to_string(Number) + (Generic ? "<T>" : "");
					};
					Source += std::string(Struct ? "struct " : "class ") + Type(Index);
					if (Kind == 4 && Index < 400)
					{
						Source += ":public " + Type(Index + 1);
					}
					Source += "{public:";
					if (Index < 400 && Kind != 4)
					{
						const auto FieldType = Kind == 5 ? "array<" + Type(Index + 1) + ">" : Type(Index + 1);
						Source += FieldType + " next=" + FieldType + "();";
					}
					Source += "};\n";
				}
				Source += (Kind == 2 || Kind == 3) ? "Node_0<int> root=Node_0<int>();" : "Node_0 root=Node_0();";
				auto Invalid = Parse(Source.c_str(), "deep-type-dependencies.vs");
				if (!Invalid) return false;
				if (!Expect(!Reused.Compile(*Invalid) && Reused.HasError() &&
				        Reused.GetError().Message.find("nesting limit exceeded") != std::string::npos &&
				        Reused.GetError().Location.Line != 0,
				        "deep field, generic, base and container dependencies return a located diagnostic"))
				{
					std::cerr << "dependency kind " << Kind << ": " << Reused.GetError().Message << '\n';
					return false;
				}
				auto Actual = Reused.Compile(*Valid);
				std::vector<std::uint8_t> ActualBytes;
				if (!Expect(Actual && !Reused.HasError() && Reused.GetDiagnostics().empty() &&
								VeroScriptBytecodeIO::SerializeBytecode(*Actual, 0, ActualBytes, Error) && ActualBytes == ExpectedBytes,
						"type depth failures do not leak compilation or type-building state"))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool TestPersistentWeakAcrossVmInstances()
	{
		auto Program = Parse(
		    "class Model{public:int value=7;};Model owner=Model();"
		    "weak<Model> observer=weak<Model>(owner);"
		    "int Read(){Model target=observer.lock();return target==nullptr?-1:target.value;}"
		    "void Expire(){owner=nullptr;}",
		    "persistent-weak.vs");
		if (!Program)
		{
			return false;
		}
		VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Expect(Module != nullptr, "persistent weak fixture compiles"))
		{
			return false;
		}
		FHotReloadState State;
		std::ostringstream Output;
		{
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			if (!Expect(Vm.ExecutePersistent(*Module, ERunMode::EvalOnce, State), "initialize persistent weak state"))
			{
				return false;
			}
		}
		int Expected = 7;
		for (const auto* Function : {"Read", "Expire", "Read"})
		{
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			FValue Result;
			if (!Expect(Vm.ExecuteFreeFunction(*Module, Function, {}, &State, nullptr, &Result),
			        "weak references can be used by a new persistent VM"))
			{
				return false;
			}
			if (std::string_view(Function) == "Expire")
			{
				Expected = -1;
			}
			else if (!Expect(std::get<std::int32_t>(Result) == Expected, "weak target lifetime crosses VM calls"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestDoWhileFailureIsolation()
	{
		auto Invalid = Parse(
		    "void main(){do{continue;}while(1);}", "invalid-do-while.vs");
		auto Valid = Parse(
		    "void main(){int i=0;do{i++;}while(i<2);print(i);}",
		    "valid-do-while.vs");
		if (!Invalid || !Valid)
		{
			return false;
		}

		VeroScriptCompiler Reused;
		if (!Expect(!Reused.Compile(*Invalid) && Reused.HasError(),
		        "non-bool do-while condition fails compilation"))
		{
			return false;
		}
		auto Module = Reused.Compile(*Valid);
		if (!Expect(Module && !Reused.HasError(),
		        "do-while failure leaves no loop patches or diagnostics"))
		{
			return false;
		}
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		return Expect(
		    Vm.Execute(*Module, ERunMode::Script) && Output.str() == "2\n" && Errors.str().empty(),
		    "valid do-while executes after compiler reuse");
	}

	bool TestParserLookupSnapshotInvalidation()
	{
		const std::string Source = "class Base{public:struct Item{int value=7;};};"
		                           "class Owner:public Base{public:using Base::Item;};";
		VeroScriptLexer Lexer(Source, "lookup-snapshot.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "lookup-snapshot.vs");
		auto Program = Parser.ParseProgram();
		if (!Expect(Program && !Parser.HasError() && Program->Classes.size() == 2 && Program->Structs.size() == 1, "lookup snapshot fixture parses"))
		{
			return false;
		}
		FParserLookupContext Context(*Program, nullptr);
		const auto ParseType = [&](const std::string& Text, const std::string& Expected)
		{
			VeroScriptLexer FragmentLexer(Text, "lookup-snapshot.vs");
			VeroScriptParser Fragment(FragmentLexer.Tokenize(), "lookup-snapshot.vs", Program.get());
			Context.Attach(Fragment);
			const auto Type = Fragment.ParseTypeFragment(std::string{}, std::vector<std::string>{});
			return Expect(Expected.empty() ? Fragment.HasError() : !Fragment.HasError() && Type == Expected,
			    "shared parser snapshot preserves inherited children and refreshes after declaration changes");
		};
		if (!ParseType("Owner::Item", Program->Structs.front().Name))
		{
			return false;
		}
		for (int Index = 0; Index < 128; ++Index)
		{
			FClassDecl Noise;
			Noise.Name = "Noise" + std::to_string(Index);
			Noise.ModulePath = "lookup-snapshot.vs";
			Program->Classes.push_back(std::move(Noise));
		}
		FStructDecl Fresh;
		Fresh.Name = "Owner$arity0.Fresh";
		Fresh.ModulePath = "lookup-snapshot.vs";
		Fresh.Scope = Program->Structs.front().Scope;
		Fresh.Scope.ScopeId = Program->Classes[1].Scope.Id;
		Fresh.Scope.OwnerTypeName = "Owner";
		Fresh.Scope.SimpleName = "Fresh";
		Fresh.Scope.DisplayName = "Owner.Fresh";
		Fresh.Scope.QualifiedName = Fresh.Name;
		Fresh.Scope.Id = Program->Classes[1].Scope.Id + "/fresh";
		Program->Structs.push_back(std::move(Fresh));
		if (!ParseType("Owner::Fresh", "Owner$arity0.Fresh") || !ParseType("Owner::Item", Program->Structs.front().Name))
		{
			return false;
		}
		Program->Structs.pop_back();
		Program->Classes.resize(2);
		return ParseType("Owner::Fresh", {}) && ParseType("Owner::Item", Program->Structs.front().Name);
	}

	bool TestImportVisibilityReuse()
	{
		VeroScriptModuleLoader Loader("");
		Loader.SetSourceOverlay("visibility-reuse/facade.vs", "export import \"leaf.vs\";");
		Loader.SetSourceOverlay("visibility-reuse/leaf.vs",
		    "export class Probe{public:int Read() const{return 42;}};");
		auto Program = Loader.LoadSourceModule(
		    "import \"facade.vs\"; int main(){Probe value=Probe();return value.Read();}",
		    "visibility-reuse/main.vs");
		if (!Expect(Program != nullptr, "import visibility fixture loads")) return false;

		const auto ReExport = std::find_if(Program->Imports.begin(), Program->Imports.end(),
		    [](const FImportDecl& Import)
		{
			return Import.bReExport;
		});
		if (!Expect(ReExport != Program->Imports.end(), "fixture has a resolved re-export")) return false;

		VeroScriptCompiler Compiler;
		if (!Expect(Compiler.Compile(*Program) != nullptr, "transitive re-export exposes the type")) return false;
		ReExport->bReExport = false;
		if (!Expect(!Compiler.Compile(*Program) && Compiler.HasError(),
		        "same-size import graph edit revokes transitive visibility"))
		{
			return false;
		}
		ReExport->bReExport = true;
		return Expect(Compiler.Compile(*Program) != nullptr && !Compiler.HasError(),
		    "restoring the re-export works on the same compiler");
	}

	bool TestRuntimeTypeScopeReuse()
	{
		const char* Source =
		    "class Base{public:struct Inherited{int value=7;};};"
		    "class Outer<T>:public Base{public:struct Item<U>{T first;U second;};"
		    "enum class State{Ready=3};class Other{};};"
		    "void main(){Outer<int> outer=Outer<int>();"
		    "Outer<int>::Item<string> item=Outer<int>::Item<string>();"
		    "Outer<int>::State state=Outer<int>::State::Ready;"
		    "Outer<int>::Inherited inherited=Outer<int>::Inherited();}";
		auto First = Parse(Source, "scope-reuse-a.vs");
		auto Second = Parse(Source, "scope-reuse-b.vs");
		if (!First || !Second) return false;
		VeroScriptCompiler Reused;
		for (const auto* Program : {First.get(), Second.get(), First.get()})
		{
			auto Module = Reused.Compile(*Program);
			VeroScriptCompiler Fresh;
			auto Reference = Fresh.Compile(*Program);
			if (!Expect(Module && Reference, "runtime type scope fixtures compile across compiler reuse"))
			{
				std::cerr << Reused.GetError().Message << '\n';
				return false;
			}
			bool FoundOuter = false;
			for (const auto& Type : Module->Types)
			{
				if (Type.Scope.SimpleName != "Outer" || Type.Scope.ConcreteTypeArguments != std::vector<std::string>{"int"})
				{
					continue;
				}
				FoundOuter = true;
				std::vector<std::string> Names;
				for (const auto& Child : Type.NestedTypes)
				{
					Names.push_back(Child.Scope.SimpleName);
					if (!Expect(Child.Scope.ModulePath == Type.Scope.ModulePath && Child.Scope.ScopeId == Type.Scope.Id,
					        "nested runtime metadata retains exact module and scope ownership"))
					{
						return false;
					}
				}
				if (!Expect(Names == std::vector<std::string>{"Item", "State", "Other"},
				        "runtime child metadata retains source order across declaration kinds"))
				{
					return false;
				}
			}
			if (!Expect(FoundOuter, "runtime scope metadata retains concrete template arguments")) return false;
			std::vector<std::uint8_t> Bytes, ReferenceBytes;
			std::string Error;
			if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(*Module, 123, Bytes, Error) && VeroScriptBytecodeIO::SerializeBytecode(*Reference, 123, ReferenceBytes, Error) && Bytes == ReferenceBytes, "runtime scope indexes are isolated between compiler inputs"))
			{
				return false;
			}
		}
		return true;
	}
}

int main()
{
	bool Success = TestDiagnosticsAfterFailure();
	Success = TestDeterministicReuse() && Success;
	Success = TestRetainedClosureMigration() && Success;
	Success = TestCompilationStateIsolation() && Success;
	Success = TestHardFailureStartsFreshCompilation() && Success;
	Success = TestTypeResolutionDepth() && Success;
	Success = TestDoWhileFailureIsolation() && Success;
	Success = TestPersistentWeakAcrossVmInstances() && Success;
	Success = TestWideForeach() && Success;
	Success = TestParserLookupSnapshotInvalidation() && Success;
	Success = TestImportVisibilityReuse() && Success;
	Success = TestRuntimeTypeScopeReuse() && Success;
	if (!Success)
	{
		return EXIT_FAILURE;
	}
	std::cout << "VeroScriptCompilerReuseTests passed\n";
	return EXIT_SUCCESS;
}
