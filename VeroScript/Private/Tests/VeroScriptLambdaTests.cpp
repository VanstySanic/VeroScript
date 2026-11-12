// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptLambdaStaticFixture.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include "VeroScriptAst.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptHotReloadInternal.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(const std::string& Source,
	    const VeroScript::FConstantEvaluationLimits* Limits = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "lambda.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			std::cerr << "lex error\n";
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "lambda.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			std::cerr << "parse: " << Parser.GetError().Location.Line << ':'
			          << Parser.GetError().Location.Column << ": "
			          << Parser.GetError().Message << '\n';
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		if (Limits)
		{
			Compiler.SetConstantEvaluationLimits(*Limits);
		}
		auto Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			std::cerr << "compile: " << Compiler.GetError().Message << '\n';
			return nullptr;
		}
		return Module;
	}

	bool RunSource(const std::string& Source, const std::string& ExpectedOutput, const char* Label)
	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), Label))
		{
			return false;
		}

		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script), Label))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return false;
		}
		if (!Expect(Output.str() == ExpectedOutput, Label))
		{
			std::cerr << "expected:\n"
			          << ExpectedOutput << "got:\n"
			          << Output.str() << '\n';
			return false;
		}
		return true;
	}
}

int main()
{
	if (!RunSource(R"vs(
template<typename T> T identity(T value){return value;}
namespace N{template<typename T> T identity(T value){return value;}}
int use(fn()->int callback){return callback();}
void main()
{
	fn()->int qualified=[callback=N::identity<int>]()->int{return callback(42);};
	fn()->int unqualified=[callback=identity<int>]()->int{return callback(42);};
	auto inferredQualified=[callback=N::identity<int>]()->int{return callback(42);};
	auto inferredUnqualified=[callback=identity<int>]()->int{return callback(42);};
	assert(qualified()==42 && unqualified()==42);
	assert(inferredQualified()==42 && inferredUnqualified()==42);
	fn()->int length=[callback=N::identity<string>]()->int{return len(callback("test"));};
	assert(length()==4);
	fn(double)->int differentParameter=[callback=N::identity<int>](double ignored)->int{return callback(42);};
	assert(differentParameter(1.5)==42);
	assert(use([callback=N::identity<int>]()->int{return callback(42);})==42);
	print(42);
}
)vs",
	        "42\n", "capture initializer inference is independent of the enclosing lambda target"))
	{
		return EXIT_FAILURE;
	}
	for (const char* Type : {"auto", "fn()->int"})
	{
		if (!Expect(!CompileSource(std::string("void main(){") + Type + " f=[callback=[](){return 42;}]()->int{return callback();};}"),
		        "an untyped initializer lambda cannot borrow the enclosing lambda target"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!RunSource(R"vs(
int make(){return 99;}
template<typename T> T identity(T value){return value;}
namespace N
{
	struct Result{int value=42;}
	Result make_result(){return Result();}
	int calls=0;
	int make(){return 42 + calls++;}
	namespace Inner
	{
		int make(){return 7;}
		template<typename T> T identity(T value){return value;}
	}
	int& alias(int& value){return value;}
}
class Receiver
{
public:
	fn()->int make=[]()->int{return 21;};
}
void main()
{
	auto f=[callback=N::make]()->int{return callback();};
	assert(N::calls==0 && f()==42 && f()==43 && N::calls==2);
	auto direct=N::Inner::make;
	fn()->int typed=N.Inner.make;
	auto global=[callback=::N::Inner::make]()->int{return callback();};
	assert(direct()==7 && typed()==7 && global()==7);
	auto create=[callback=N::make_result]()->N::Result{return callback();};
	assert(create().value==42);
	auto generic=[callback=N::Inner::identity<int>](int value)->int{return callback(value);};
	auto ordinary=[callback=identity<int>](int value)->int{return callback(value);};
	assert(generic(8)==8 && ordinary(9)==9);
	auto reference=[callback=N::alias](int& value)->void
	{
		int& bound=callback(&value);
		bound+=2;
	};
	int value=40;
	reference(&value);
	assert(value==42);
	{
		Receiver N=Receiver();
		auto field=[callback=N.make]()->int{return callback();};
		assert(field()==21);
	}
	print(42);
}
)vs",
	        "42\n", "qualified function values support initialized captures and ordinary inference"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
namespace Super{int value(){return 42;}}
class Base{public:int value=7;}
class Derived:Base
{
public:
	int read(){auto callback=[copy=Super::value]()->int{return copy;};return callback();}
}
void main(){Derived value=Derived();assert(value.read()==7);}
)vs",
	        "", "qualified function values preserve Super receiver priority"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
namespace N
{
	int make(){return 99;}
	fn()->int make=[]()->int{return 42;};
}
void main(){auto f=[callback=N::make]()->int{return callback();};print(f());}
)vs",
	        "42\n", "qualified global values keep priority over same-named functions"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
namespace N
{
	int Inner(){return 99;}
	namespace Inner{int make(){return 42;}}
}
void main()
{
	assert(N::Inner()==99 && N::Inner::make()==42);
	auto callback=[make=N::Inner::make]()->int{return make();};
	print(callback());
}
)vs",
	        "42\n", "function values do not hide longer namespace paths"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
namespace N
{
	enum E{Inner=7};
	int Inner(){return 99;}
	namespace Inner{int make(){return 42;}}
}
void main(){auto callback=[make=N::Inner::make]()->int{return make();};print(callback());}
)vs",
	        "42\n", "same-named enum constants and functions preserve nested namespace paths"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(!CompileSource("namespace N{fn()->int Inner=[]()->int{return 7;};"
	                           "namespace Inner{int make(){return 42;}}}void main(){print(N::Inner::make());}"),
	        "actual callable globals still hide longer namespace paths"))
	{
		return EXIT_FAILURE;
	}
	for (const char* Declaration : {
	         "namespace N{int make(){return 1;}int make(int value){return value;}}",
	         "namespace N{int make()=delete;}"})
	{
		if (!Expect(!CompileSource(std::string(Declaration) + "void main(){auto f=[callback=N::make]()->int{return callback();};}"),
		        "qualified function captures reject ambiguous and deleted declarations"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		using namespace VeroScript;
		const std::string Expression = "[count=0]()mutable->int{return ++count;}";
		const std::string Source = "fn()->int make(){return " + Expression + ";}fn()->int saved=make();void main(){print(saved());}";
		auto Original = CompileSource(Source);
		auto Shifted = CompileSource("fn()->int make(){auto unused=" + Expression + ";return " + Expression + ";}fn()->int saved=make();void main(){print(saved());}");
		if (!Expect(Original && Shifted, "mutable reload identity fixtures compile")) return EXIT_FAILURE;
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		Test::Configure(Vm);
		FHotReloadState State;
		std::string Error;
		if (!Expect(Vm.ExecutePersistent(*Original, ERunMode::Script, State) && !MigrateHotReloadState(State, *Shifted, nullptr, Error) && Vm.ExecutePersistent(*Original, ERunMode::Script, State) && Output.str() == "1\n2\n", "an inserted lambda cannot steal a retained mutable environment"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.DiscardPersistentState(*Original, State), "mutable identity state clears")) return EXIT_FAILURE;
	}
	{
		using namespace VeroScript;
		const std::string Source = "fn()->const int& saved=[value=7]()->const int&{return value;};void main(){print(saved());}";
		auto Original = CompileSource(Source);
		auto Replacement = CompileSource("int unrelated(){return 1;}" + Source);
		if (!Expect(Original && Replacement, "readonly owner reload fixtures compile")) return EXIT_FAILURE;
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		Test::Configure(Vm);
		FHotReloadState State;
		std::string Error;
		if (!Expect(Vm.ExecutePersistent(*Original, ERunMode::Script, State) && MigrateHotReloadState(State, *Replacement, nullptr, Error) && Vm.ExecutePersistent(*Replacement, ERunMode::Script, State) && Output.str() == "7\n7\n", "readonly capture storage retains its identity on reload"))
		{
			std::cerr << Error << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.DiscardPersistentState(*Replacement, State), "readonly owner state clears")) return EXIT_FAILURE;
	}
	{
		using namespace VeroScript;
		const std::string Source = "fn()->int saved=[count=0]()mutable->int{return ++count;};"
		                           "int next(){static int n=0;return ++n;}void main(){print(saved());print(next());}";
		auto Original = CompileSource(Source);
		auto Replacement = CompileSource(Source + "int fail(){saved();next();throw 1;}int broken=fail();");
		if (!Expect(Original && Replacement, "capture and static rollback fixtures compile")) return EXIT_FAILURE;
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		Test::Configure(Vm);
		FHotReloadState State;
		FHotReloadRollback Rollback;
		std::string Error;
		if (!Expect(Vm.ExecutePersistent(*Original, ERunMode::Script, State) && MigrateHotReloadState(State, *Replacement, &Rollback, Error) && !Vm.ExecutePersistent(*Replacement, ERunMode::Script, State, &Rollback) && Vm.RollbackHotReloadState(*Replacement, State, Rollback) && Vm.ExecutePersistent(*Original, ERunMode::Script, State) && Output.str() == "1\n1\n2\n2\n", "failed module initialization restores mutable cells and static values"))
		{
			std::cerr << Error << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.DiscardPersistentState(*Original, State), "rollback state clears")) return EXIT_FAILURE;
	}

	{
		using namespace VeroScript;
		const std::string Lambda = "[]()->int{static int value=0;return ++value;}";
		const std::string Source = "fn()->int build(){return " + Lambda + ";}fn()->int saved=build();void main(){print(saved());}";
		auto Original = CompileSource(Source);
		auto Formatting = CompileSource("// Extra comment\n" + Source);
		auto Shifted = CompileSource("fn()->int build(){auto unused=" + Lambda + ";return " + Lambda + ";}fn()->int saved=build();void main(){print(saved());}");
		if (!Expect(Original && Formatting && Shifted, "lambda static identity fixtures compile")) return EXIT_FAILURE;
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		Test::Configure(Vm);
		FHotReloadState State;
		std::string Error;
		if (!Expect(Vm.ExecutePersistent(*Original, ERunMode::Script, State) && MigrateHotReloadState(State, *Formatting, nullptr, Error) && Vm.ExecutePersistent(*Formatting, ERunMode::Script, State), "formatting preserves lambda static identity"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!MigrateHotReloadState(State, *Shifted, nullptr, Error) && Error.find("clear the session") != std::string::npos && Vm.ExecutePersistent(*Formatting, ERunMode::Script, State) && Output.str() == "1\n2\n3\n", "inserting a lambda cannot alias retained static state"))
		{
			std::cerr << Error << '\n'
			          << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.DiscardPersistentState(*Formatting, State), "lambda static state cleanup")) return EXIT_FAILURE;
	}
	{
		using namespace VeroScript;
		auto Module = CompileSource("int attempts=0;bool spin=true;int init(){++attempts;while(spin){}return 42;}"
		                            "void main(){static int value=init();assert(value==42);}");
		if (!Expect(static_cast<bool>(Module), "interrupted initializer fixture compiles")) return EXIT_FAILURE;
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		Test::Configure(Vm);
		Vm.SetExecutionLimits({40, 0});
		FHotReloadState State;
		if (!Expect(!Vm.ExecutePersistent(*Module, ERunMode::Script, State) && !Vm.RequiresReset(),
		        "quota interruption cleans initializer guard with reusable state"))
		{
			return EXIT_FAILURE;
		}
		for (std::size_t Slot = 0; Slot < State.GlobalNames.size(); ++Slot)
		{
			if (State.GlobalNames[Slot] == "spin" || State.GlobalNames[Slot].find("\x1Fspin") != std::string::npos)
			{
				State.Globals[Slot] = false;
			}
		}
		Vm.SetExecutionLimits({10000, 0});
		if (!Expect(Vm.ExecutePersistent(*Module, ERunMode::Script, State), "interrupted static retries on next call"))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		for (std::size_t Slot = 0; Slot < State.GlobalNames.size(); ++Slot)
		{
			if ((State.GlobalNames[Slot] == "attempts" || State.GlobalNames[Slot].find("\x1F"
			                                                                           "attempts") != std::string::npos) &&
			    !Expect(std::get<std::int32_t>(State.Globals[Slot]) == 2, "failed static is retried exactly once"))
			{
				return EXIT_FAILURE;
			}
		}
		if (!Expect(Vm.DiscardPersistentState(*Module, State), "interrupted state clears")) return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
void main()
{
	int source = 7;
	auto outer = [=]() -> fn()->int { return [source]() -> int { return 42; }; };
	assert(outer()() == 42);
	static constexpr int width = 3;
	int values[width];
	assert(len(values) == 3);
	if constexpr (false) { static int unreachable = missing(); }
}
)vs",
	        "", "unused nested explicit captures and static constexpr"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
void main()
{
	auto explicitCallback = [count = 0]() mutable -> void { print(++count); };
	function() -> void contextualCallback = [count = 0]() mutable { print(++count); };
	auto alias = contextualCallback;
	explicitCallback();
	explicitCallback();
	contextualCallback();
	alias();
}
)vs",
	        "1\n2\n1\n2\n", "mutable callbacks retain explicit and contextual return types"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(!CompileSource("void main(){auto callback=[count=0]() mutable {++count;};}"),
	        "auto lambda still requires an explicit return type"))
	{
		return EXIT_FAILURE;
	}
	for (const auto* Source : {"void main(){static int x=1;int x=2;}",
	         "void main(){int x=1;static int x=2;}",
	         "void main(){static int x=1;enum E{x=2};}",
	         "void main(){enum E{x=1};static int x=2;}",
	         "template<int N> int f(){static int N=1;return N;}void main(){f<2>();}"})
	{
		if (!Expect(!CompileSource(Source), "static declarations obey duplicate and template binding rules")) return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
int source(){return 7;}
int callback(){return 8;}
class C
{
public:
	static int callback(){return 9;}
	int read()
	{
		static fn()->int callback=[]()->int{return 42;};
		return callback();
	}
}
void main()
{
	static int source=42;
	auto snapshot=[value=source]()->int{return value;};
	assert(source==42 && snapshot()==42);
	static fn()->int callback=[]()->int{return 42;};
	assert(callback()==42);
	C object=C();
	assert(object.read()==42);
	enum E{value=7};
	{
		static int value=40;
		int& alias=value;
		alias+=2;
		auto read=[]()->int{return value;};
		assert(value==42 && read()==42);
		{
			enum Inner{value=3};
			auto inner=[]()->int{return value;};
			assert(value==3 && inner()==3);
		}
	}
	print(42);
}
)vs",
	        "42\n", "local statics shadow enum and function names in values, captures and calls"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
void main()
{
	map<string,int> values=map<string,int>();
	values["key"]=7;
	for(ref pair<string,int> entry:values)
	{
		{
			static int entry=42;
			assert(entry==42);
			print(entry);
		}
		assert(entry.second==7);
	}
}
)vs",
	        "42\n", "local statics shadow map entry views without materializing the outer binding"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
constexpr int size(){return 2;}
void main()
{
	const int original=size();
	static int size=42;
	int originalValues[original];
	assert(len(originalValues)==2);
	enum E{extent=7};
	{
		static constexpr int extent=3;
		int values[extent];
		assert(len(values)==3);
		if constexpr(extent==3) print(extent); else missing();
		auto read=[]()->int
		{
			int nestedValues[extent];
			return len(nestedValues);
		};
		assert(read()==3);
	}
}
)vs",
	        "3\n", "static constexpr bounds and reads use the same lexical binding"))
	{
		return EXIT_FAILURE;
	}
	const std::string StaticConstSource = R"vs(
constexpr int width(){return 3;}
template<int N> int extent(){return N;}
void main()
{
	{
	{
	{
	static const int literal=3;
	int first[literal];
	assert(len(first)==3 && extent<literal>()==3);
	const int original=width();
	using Count=int;
	static const Count deferred=original;
	static int width=42;
	int second[deferred];
	assert(len(second)==3 && extent<deferred>()==3);
	auto outer=[]()->fn()->int
	{
		return []()->int
		{
			int nested[deferred];
			return len(nested)+extent<literal>();
		};
	};
	assert(outer()()==6);
	if constexpr(deferred==3) print(deferred); else missing();
	}
	}
	}
}
)vs";
	if (!RunSource(StaticConstSource, "3\n", "const statics retain literal and deferred declaration constants across closures")) return EXIT_FAILURE;
	if (!RunSource(R"vs(
constexpr int width(){return 3;}
template<typename T> fn()->int typed()
{
	using Count=T;
	static const Count n=3;
	return []()->int{int values[n];return len(values);};
}
void main()
{
	static const int original=width();
	static int width=42;
	auto outer=[]()->fn()->int{return []()->int{int values[original];return len(values);};};
	assert(outer()()==3 && width==42);
	assert(typed<int>()()==3 && typed<int64>()()==3);
}
)vs",
	        "", "deferred static constants exclude later statics sharing a local slot and retain generic alias bindings"))
	{
		return EXIT_FAILURE;
	}
	{
		auto Module = CompileSource(StaticConstSource);
		if (!Expect(static_cast<bool>(Module), "const static serialization fixture compiles")) return EXIT_FAILURE;
		for (const auto& Global : Module->GlobalMetadata)
		{
			if (Global.bLocalStatic && !Global.bConstexpr && !Expect(!Global.ConstantValue, "ordinary const static does not acquire constexpr bytecode metadata"))
			{
				return EXIT_FAILURE;
			}
		}
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		if (!Expect(VeroScript::VeroScriptBytecodeIO::SerializeBytecode(*Module, 0, Bytes, Error),
		        "ordinary const static constants preserve serialization invariants"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const auto* Source : {
	         "int f(int seed){static const int n=seed;int values[n];return len(values);}void main(){f(3);}",
	         "template<int N> int extent(){return N;}int f(int seed){static const int n=seed;return extent<n>();}void main(){f(3);}",
	         "constexpr int value(int n){return n;}int f(int seed){static const int n=value(seed);int values[n];return len(values);}void main(){f(3);}"})
	{
		if (!Expect(!CompileSource(Source), "runtime-dependent const statics are not compilation constants")) return EXIT_FAILURE;
	}
	{
		VeroScript::FConstantEvaluationLimits Limits;
		Limits.MaxWork = 1000;
		const std::string Costly = "constexpr int costly(){int n=0;for(int i=0;i<10000;++i){n+=1;}return n;}";
		if (!Expect(static_cast<bool>(CompileSource(Costly +
		                    "void main(){static const int unused=costly();constexpr int needed=3;int values[needed];}",
		                &Limits)),
		        "unused const static calls do not consume the required constant evaluation budget"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!CompileSource(Costly +
		                    "void main(){static const int needed=costly();int values[needed];}",
		                &Limits),
		        "required const static calls retain the constant evaluation work limit"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const auto* Binding : {"int f=42;", "static int f=42;", "enum E{f=42};",
	         "fn()->int f=[]()->int{return 42;};", "static fn()->int f=[]()->int{return 42;};"})
	{
		const std::string Source = "template<typename T> int f(){return 7;}void main(){" + std::string(Binding);
		if (!Expect(!CompileSource(Source + "auto value=f<int>;}"),
		        "explicit template function references obey lexical value shadowing"))
		{
			return EXIT_FAILURE;
		}
		if (!RunSource(Source + "auto value=::f<int>;assert(value()==7);}", "",
		        "explicit global qualification bypasses local value shadowing"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!Expect(!CompileSource("template<typename T> int f(){return 7;}class C{public:int f=42;int read(){auto value=f<int>;return value();}}void main(){C c=C();c.read();}"),
	        "explicit template function references respect implicit field bindings"))
	{
		return EXIT_FAILURE;
	}
	for (const auto* Source : {
	         "constexpr int source(){return 7;}void main(){static int source=42;constexpr int value=source();}",
	         "void main(){enum E{extent=7};{static int extent=3;int values[extent];}}"})
	{
		if (!Expect(!CompileSource(Source), "runtime static bindings cannot fall through to constexpr outer symbols")) return EXIT_FAILURE;
	}

	if (!RunSource(std::string(VeroScript::Test::LambdaStaticDefinitions) + "void main(){checkLambdaStaticSupport();}", "", "shared new language fixture")) return EXIT_FAILURE;
	{
		using namespace VeroScript;
		const std::string Source =
		    "fn()->int saved=[count=0]() mutable ->int{return ++count;};"
		    "int next(){static int value=10;return ++value;}"
		    "void main(){print(saved());print(next());}";
		auto Original = CompileSource(Source);
		auto Replacement = CompileSource("int unrelated(){return 7;}" + Source);
		auto Changed = CompileSource("fn()->int saved=[count=0]() mutable ->int{return ++count;};"
		                             "int next(){static int value=20;return ++value;}void main(){print(saved());print(next());}");
		if (!Expect(Original && Replacement && Changed, "persistent capture and static modules compile")) return EXIT_FAILURE;
		std::ostringstream Output, Errors;
		std::istringstream Input;
		VeroScriptVm Vm(Output, Input, Errors);
		Test::Configure(Vm);
		FHotReloadState State;
		std::string Error;
		if (!Expect(Vm.ExecutePersistent(*Original, ERunMode::Script, State) && Vm.ExecutePersistent(*Original, ERunMode::Script, State), "capture and static state survive calls"))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(MigrateHotReloadState(State, *Replacement, nullptr, Error), "unrelated edit preserves initialized statics"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.ExecutePersistent(*Replacement, ERunMode::Script, State) && Output.str() == "1\n11\n2\n12\n3\n13\n", "reload retains mutable cells and local static values"))
		{
			std::cerr << Vm.GetError().Message << '\n'
			          << Output.str();
			return EXIT_FAILURE;
		}
		if (!Expect(!MigrateHotReloadState(State, *Changed, nullptr, Error) && Error.find("clear the session") != std::string::npos && Vm.ExecutePersistent(*Replacement, ERunMode::Script, State), "incompatible static reload is atomic"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Vm.DiscardPersistentState(*Replacement, State), "persistent static state clears")) return EXIT_FAILURE;
		std::vector<std::uint8_t> Bytes;
		if (!Expect(VeroScriptBytecodeIO::SerializeBytecode(*Original, 0, Bytes, Error), "capture and static bytecode serializes"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}

		{
			auto Damaged = *Original;
			for (auto& Function : Damaged.Functions)
			{
				if (Function.LocalStaticInitializers.empty())
				{
					continue;
				}
				Function.LocalStaticInitializers.front().ScopeEndOffset = 0;
				break;
			}
			if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Damaged, Error), "invalid local static scope metadata is rejected")) return EXIT_FAILURE;
			Damaged = *Original;
			for (auto& Function : Damaged.Functions)
			{
				if (Function.LocalStaticInitializers.empty())
				{
					continue;
				}
				Function.LocalStaticInitializers.clear();
				Function.LocalStaticSemanticIdentity = 0;
				break;
			}
			if (!Expect(!VeroScriptBytecodeIO::ValidateModule(Damaged, Error), "unowned static opcodes are rejected")) return EXIT_FAILURE;
		}
		auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 0, Error);
		if (!Expect(Loaded && Vm.Execute(*Loaded, ERunMode::Script), "capture and static bytecode round trip executes"))
		{
			std::cerr << Error << '\n'
			          << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}
	if (!RunSource(R"vs(
int iterations()
{
	for (static int count=0; count<3; ++count) {}
	static const int answer=42;
	return answer;
}
fn()->const int& view()
{
	return [value=7]()->const int& { return value; };
}
void main()
{
	assert(iterations()==42 && iterations()==42);
	auto first=view(); auto second=view();
	assert(first()==7 && first==second);
	const int& held=view()();
	assert(held==7);
	int outer=9;
	auto renamed=[outer=outer+1, other=outer]()->int{return outer+other;};
	assert(renamed()==19 && outer==9);
}
)vs",
	        "", "static for header, readonly captured reference and initializer outer scope"))
	{
		return EXIT_FAILURE;
	}
	for (const auto* Source : {
	         "void main(){static int value=value;}",
	         "void main(){static ref int value=1;}",
	         "void main(){auto f=[x=1,x=2]()->int{return x;};}",
	         "void main(){auto f=[x=1](int x)->int{return x;};}",
	         "void main(){auto f=[&x=1]()->int{return x;};}",
	         "void main(){const int x=1;auto f=[x]()mutable->int{return ++x;};}",
	         "void main(){const int x=1;auto f=[&y=x]()mutable->int{return ++y;};}",
	         "constexpr int f(){static int value=0;return ++value;}void main(){}"})
	{
		if (!Expect(!CompileSource(Source), "invalid captures and static declarations are rejected")) return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
function()->int makeCounter()
{
	return [count = 0]() mutable -> int { return ++count; };
}
int initialized = 0;
int initializeCapture() { return ++initialized; }
void main()
{
	auto first = makeCounter();
	auto alias = first;
	auto other = makeCounter();
	assert(first() == 1 && alias() == 2 && other() == 1);
	int total = 3;
	auto advance = [&target = total, step = 2]() -> int { target += step; return target; };
	assert(advance() == 5 && advance() == 7 && total == 7);
	auto unused = [left = initializeCapture(), right = initializeCapture()]() -> int { return 0; };
	assert(initialized == 2 && unused() == 0 && initialized == 2);
	int& reference = total;
	auto snapshot = [reference]() -> int { return reference; };
	total = 9;
	assert(snapshot() == 7);
	print(first());
	print(advance());
}
)vs",
	        "3\n11\n", "initialized captures and persistent mutable environment"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(!CompileSource("void main(){int value=0;auto f=[value]()->int{return ++value;};}"),
	        "ordinary value capture is readonly"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
int initialized = 0;
int initializeStatic(int seed) { initialized += 1; return seed; }
int next(int seed)
{
	static int value = initializeStatic(seed);
	return ++value;
}
function()->int makeStaticCounter(int seed)
{
	return [seed]() -> int
	{
		static int value = seed;
		return ++value;
	};
}
void main()
{
	assert(initialized == 0);
	assert(next(10) == 11 && next(99) == 12 && initialized == 1);
	auto first = makeStaticCounter(20);
	auto second = makeStaticCounter(30);
	assert(first() == 21 && second() == 22 && first() == 23);
	print(next(0));
}
)vs",
	        "13\n", "local static initialization is shared by the concrete function"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
int attempts = 0;
int initializeRetry()
{
	attempts += 1;
	if (attempts == 1) { throw 7; }
	return 11;
}
int retryWithinCall()
{
	for (int index = 0; index < 2; ++index)
	{
		try
		{
			static int value = initializeRetry();
			return ++value;
		}
		catch (int error)
		{
			assert(error == 7);
		}
	}
	return -1;
}
void main()
{
	assert(retryWithinCall() == 12);
	assert(retryWithinCall() == 13 && attempts == 2);
	print(attempts);
}
)vs",
	        "2\n", "failed local static retries after a same-frame catch"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
class StaticLifetime
{
public:
	StaticLifetime(int input) { id = input; }
	~StaticLifetime() { print(id); }
	int id;
};
void initializeObjects()
{
	static StaticLifetime first = StaticLifetime(1);
	static StaticLifetime second = StaticLifetime(2);
}
void main()
{
	initializeObjects();
	initializeObjects();
}
)vs",
	        "2\n1\n", "local static roots release in reverse completion order"))
	{
		return EXIT_FAILURE;
	}
	{
		const auto Module = CompileSource(
		    "fn(int)->int make_worker(int factor) {\n"
		    "    return [factor](int value) -> int => value * factor;\n"
		    "}\n"
		    "void main() {}\n");
		if (!Expect(static_cast<bool>(Module), "worker callable source compiles"))
		{
			return EXIT_FAILURE;
		}
		std::uint16_t FactoryIndex = 0xFFFF;
		for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
		{
			if (Module->Functions[Index].Name.find("make_worker#fn#") != std::string::npos)
			{
				FactoryIndex = static_cast<std::uint16_t>(Index);
				break;
			}
		}
		if (!Expect(FactoryIndex != 0xFFFF, "worker callable factory resolves"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Caller(Output, Input, Errors);
		VeroScript::Test::Configure(Caller);
		VeroScript::FValue FunctionValue;
		if (!Expect(Caller.ExecuteFunction(
		                *Module, FactoryIndex, {std::int32_t{3}}, nullptr,
		                &FunctionValue),
		        "worker callable is created"))
		{
			std::cerr << Caller.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		const auto* Handle = std::get_if<VeroScript::FFunctionHandle>(&FunctionValue);
		if (!Expect(Handle && *Handle, "worker callable has a function handle"))
		{
			return EXIT_FAILURE;
		}
		const std::vector<VeroScript::FValue> CaptureSnapshot = (*Handle)->Captures;
		(*Handle)->Captures[0] = std::int32_t{7};
		VeroScript::VeroScriptVm Worker(Output, Input, Errors);
		VeroScript::Test::Configure(Worker);
		VeroScript::FValue Result;
		if (!Expect(Worker.ExecuteFunction(
		                *Module, (*Handle)->FunctionIndex, {std::int32_t{14}},
		                nullptr, &Result, &CaptureSnapshot),
		        "isolated worker executes copied by-value captures") ||
		    !Expect(std::get<std::int32_t>(Result) == 42,
		        "isolated worker returns captured result"))
		{
			std::cerr << Worker.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::VeroScriptVm MissingCapture(Output, Input, Errors);
		VeroScript::Test::Configure(MissingCapture);
		if (!Expect(!MissingCapture.ExecuteFunction(
		                *Module, (*Handle)->FunctionIndex, {std::int32_t{14}}),
		        "isolated worker rejects missing captures"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Module = CompileSource(
		    "fn()->int make() { return []() -> int => 42; }\n"
		    "int use(fn()->int callback) { return callback(); }\n"
		    "void main() {}\n");
		if (!Expect(static_cast<bool>(Module), "function owner test compiles"))
		{
			return EXIT_FAILURE;
		}
		std::uint16_t MakeIndex = 0xFFFF;
		std::uint16_t UseIndex = 0xFFFF;
		for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
		{
			const std::string& Name = Module->Functions[Index].Name;
			if (Name.find("make#fn#") != std::string::npos)
			{
				MakeIndex = static_cast<std::uint16_t>(Index);
			}
			if (Name.find("use#fn#") != std::string::npos)
			{
				UseIndex = static_cast<std::uint16_t>(Index);
			}
		}
		if (!Expect(MakeIndex != 0xFFFF && UseIndex != 0xFFFF,
		        "function owner test resolves targets"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm OwnerVm(Output, Input, Errors);
		VeroScript::Test::Configure(OwnerVm);
		VeroScript::FValue Callable;
		if (!Expect(OwnerVm.ExecuteFunction(
		                *Module, MakeIndex, {}, nullptr, &Callable),
		        "owner VM creates callable"))
		{
			std::cerr << OwnerVm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FValue Result;
		if (!Expect(OwnerVm.ExecuteFunction(
		                *Module, UseIndex, {Callable}, nullptr, &Result) &&
		            std::get_if<std::int32_t>(&Result) && *std::get_if<std::int32_t>(&Result) == 42,
		        "same VM may invoke its callable across calls"))
		{
			std::cerr << OwnerVm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::VeroScriptVm OtherVm(Output, Input, Errors);
		VeroScript::Test::Configure(OtherVm);
		if (!Expect(!OtherVm.ExecuteFunction(
		                *Module, UseIndex, {Callable}, nullptr, &Result),
		        "different VM rejects a callable despite matching index and generation"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		auto Cell = std::make_shared<VeroScript::FDynamicArray>();
		Cell->TypeName.assign(VeroScript::ClosureReferenceCellTypeName);
		Cell->ElementTypeName = "__internal";
		Cell->Elements.emplace_back(Cell);
		VeroScript::FValue Forged = Cell;
		const VeroScript::FValue& ConstForged = Forged;
		if (!Expect(
		        VeroScript::UnwrapClosureReferenceCell(&Forged) == nullptr && VeroScript::UnwrapClosureReferenceCell(&ConstForged) == nullptr,
		        "cyclic forged reference cells are rejected with a bounded walk"))
		{
			return EXIT_FAILURE;
		}
		// Break the intentionally forged ownership cycle before leaving the test.
		Cell->Elements.clear();
	}

	if (!RunSource(
	        "@asset(primary)\n"
	        "export const int ReflectedGlobal = 7;\n"
	        "@route(api)\n"
	        "export int reflected_free(const int& value) { return value; }\n"
	        "namespace ReflectNs {\n"
	        "    @route(namespaced) export int nested_free(int value) { return value; }\n"
	        "}\n"
	        "struct ReflectedBase { int inherited; int base_call() { return 1; } };\n"
	        "@entity(sample)\n"
	        "struct Reflected : ReflectedBase {\n"
	        "    @data(primary) mutable string label;\n"
	        "    @call(test) virtual int add(const int& value) const { return value + 1; }\n"
	        "    static int ping(int value) { return value; }\n"
	        "};\n"
	        "enum class ReflectedKind : uint16 {\n"
	        "    @label(primary)\n"
	        "    First, Second\n"
	        "};\n"
	        "void main() {\n"
	        "    type<Reflected> info = type<Reflected>();\n"
	        "    print(info.name);\n"
	        "    print(info.kind());\n"
	        "    print(info.base_count());\n"
	        "    print(info.field_count());\n"
	        "    print(info.has_annotation(\"entity\"));\n"
	        "    print(info.has_metadata(\"__annot\"));\n"
	        "    print(info.metadata(\"__annot\"));\n"
	        "    bool found_field = false;\n"
	        "    for (int i = 0; i < info.field_count(); i++) {\n"
	        "        field_info<Reflected> field_meta = field_info<Reflected>(info.qualified_name, i);\n"
	        "        if (field_meta.name() == \"label\") {\n"
	        "            found_field = field_meta.declared_type() == \"string\"\n"
	        "                && field_meta.runtime_type() == \"string\"\n"
	        "                && field_meta.is_mutable()\n"
	        "                && field_meta.is_declared_on_type()\n"
	        "                && field_meta.metadata_count() > 0\n"
	        "                && field_meta.has_annotation(\"@data\")\n"
	        "                && field_meta.metadata(\"primary\") == \"\";\n"
	        "        }\n"
	        "    }\n"
	        "    print(found_field);\n"
	        "    bool found_method = false;\n"
	        "    bool found_static = false;\n"
	        "    for (int i = 0; i < info.method_count(); i++) {\n"
	        "        method_info<Reflected> method_meta = method_info<Reflected>(info.qualified_name, i);\n"
	        "        if (method_meta.name() == \"add\") {\n"
	        "            found_method = method_meta.return_type() == \"int\"\n"
	        "                && method_meta.parameter_count() == 1\n"
	        "                && method_meta.parameter_type(0) == \"int\"\n"
	        "                && method_meta.parameter_name(0) == \"value\"\n"
	        "                && method_meta.parameter_is_ref(0)\n"
	        "                && method_meta.parameter_is_const(0)\n"
	        "                && method_meta.is_virtual()\n"
	        "                && method_meta.is_const()\n"
	        "                && method_meta.metadata_count() > 0\n"
	        "                && method_meta.has_annotation(\"call\")\n"
	        "                && method_meta.has_metadata(\"test\");\n"
	        "        }\n"
	        "        if (method_meta.name() == \"ping\") { found_static = method_meta.is_static(); }\n"
	        "    }\n"
	        "    print(found_method);\n"
	        "    print(found_static);\n"
	        "    type<ReflectedKind> enum_info = type<ReflectedKind>();\n"
	        "    print(enum_info.kind());\n"
	        "    print(enum_info.enum_member_count());\n"
	        "    print(enum_info.enum_member_name(1));\n"
	        "    print(enum_info.enum_member_value(1));\n"
	        "    print(enum_info.enum_underlying_type());\n"
	        "    print(enum_info.is_scoped_enum());\n"
	        "    enum_member_info<ReflectedKind> first = enum_member_info<ReflectedKind>(enum_info.qualified_name, 0);\n"
	        "    print(first.name() == \"First\" && first.value() == 0 && first.has_annotation(\"label\") && first.has_metadata(\"primary\"));\n"
	        "    module_info module = module_info();\n"
	        "    print(module.function_count() >= 2 && module.global_count() == 1 && module.type_count() > 0);\n"
	        "    bool found_function = false;\n"
	        "    bool found_namespaced_function = false;\n"
	        "    bool found_namespaced_metadata = false;\n"
	        "    bool found_static_as_free = false;\n"
	        "    for (int i = 0; i < module.function_count(); i++) {\n"
	        "        function_info function_meta = function_info(i);\n"
	        "        if (function_meta.name() == \"reflected_free\") {\n"
	        "            found_function = function_meta.return_type() == \"int\"\n"
	        "                && function_meta.parameter_count() == 1\n"
	        "                && function_meta.parameter_type(0) == \"int\"\n"
	        "                && function_meta.parameter_name(0) == \"value\"\n"
	        "                && function_meta.parameter_is_ref(0)\n"
	        "                && function_meta.parameter_is_const(0)\n"
	        "                && function_meta.is_exported()\n"
	        "                && function_meta.has_annotation(\"route\")\n"
	        "                && function_meta.has_metadata(\"api\");\n"
	        "        }\n"
	        "        if (function_meta.name() == \"nested_free\") {\n"
	        "            found_namespaced_function = function_meta.has_annotation(\"route\");\n"
	        "            found_namespaced_metadata = function_meta.has_metadata(\"namespaced\");\n"
	        "        }\n"
	        "        if (function_meta.name() == \"ping\") { found_static_as_free = true; }\n"
	        "    }\n"
	        "    print(found_function);\n"
	        "    print(found_namespaced_function);\n"
	        "    print(found_namespaced_metadata);\n"
	        "    print(!found_static_as_free);\n"
	        "    global_info global_meta = global_info(0);\n"
	        "    print(global_meta.name() == \"ReflectedGlobal\"\n"
	        "        && global_meta.declared_type() == \"int\"\n"
	        "        && global_meta.runtime_type() == \"int\"\n"
	        "        && global_meta.is_const() && global_meta.is_exported()\n"
	        "        && global_meta.has_annotation(\"asset\")\n"
	        "        && global_meta.has_metadata(\"primary\"));\n"
	        "    print(type<int>().kind());\n"
	        "    print(type<uint16>().kind());\n"
	        "    type<readonly<int>> readonly_info = type<readonly<int>>();\n"
	        "    bool found_readonly_value = false;\n"
	        "    bool found_readonly_get = false;\n"
	        "    for (int i = 0; i < readonly_info.field_count(); i++) {\n"
	        "        field_info<readonly<int>> field_meta = field_info<readonly<int>>(readonly_info.qualified_name, i);\n"
	        "        if (field_meta.name() == \"value\") { found_readonly_value = field_meta.declared_type() == \"int\"; }\n"
	        "    }\n"
	        "    for (int i = 0; i < readonly_info.method_count(); i++) {\n"
	        "        method_info<readonly<int>> method_meta = method_info<readonly<int>>(readonly_info.qualified_name, i);\n"
	        "        if (method_meta.name() == \"get\") { found_readonly_get = method_meta.return_type() == \"const ref int\" && method_meta.is_const(); }\n"
	        "    }\n"
	        "    print(readonly_info.kind());\n"
	        "    print(readonly_info.field_count());\n"
	        "    print(readonly_info.method_count());\n"
	        "    print(found_readonly_value);\n"
	        "    print(found_readonly_get);\n"
	        "}\n",
	        "Reflected\nstruct\n1\n2\ntrue\ntrue\n@entity\ntrue\ntrue\ntrue\nenum\n2\nSecond\n1\nunsigned short\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\nprimitive\nprimitive\nstruct\n1\n2\ntrue\ntrue\n",
	        "script reflection exposes module, type, function, global, field, method, and enum-member metadata"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        !CompileSource(
	            "void main() { print(__reflection_string(\"int\", 0, 0, 0, \"\")); }\n"),
	        "reflection implementation intrinsics must not be callable from source"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(
	        "class First { public: int a; };\n"
	        "class Second { public: int b; };\n"
	        "class Combined : public First, public Second { public: int c; };\n"
	        "class Base { public: virtual int value() { return 1; } };\n"
	        "class Derived : public Base { public: int value() override { return 2; } };\n"
	        "void main() {\n"
	        "    type<Second> ft = type<Second>();\n"
	        "    field_info<Second> field = field_info<Second>(ft.qualified_name, 0);\n"
	        "    Combined object = Combined();\n"
	        "    field.set<int>(object, 19);\n"
	        "    print(field.get<int>(object));\n"
	        "    print(object.b);\n"
	        "    type<Base> mt = type<Base>();\n"
	        "    method_info<Base> method = method_info<Base>(mt.qualified_name, 0);\n"
	        "    Derived receiver = Derived();\n"
	        "    print(method.invoke<int>(receiver));\n"
	        "}\n",
	        "19\n19\n2\n",
	        "executable reflection handles inherited field offsets and virtual calls"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(
	        "class AbstractBase { public: virtual int score() = 0; };\n"
	        "class Concrete : public AbstractBase { public: int score() override { return 7; } };\n"
	        "void main() {\n"
	        "    type<AbstractBase> info = type<AbstractBase>();\n"
	        "    method_info<AbstractBase> method = method_info<AbstractBase>(info.qualified_name, 0);\n"
	        "    Concrete value = Concrete();\n"
	        "    print(method.invoke<int>(value));\n"
	        "}\n",
	        "7\n",
	        "public pure virtual reflection dispatches to the concrete override"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(
	        "namespace N { class Base { protected: int value; }; }\n"
	        "class Exposed : private N::Base { public: using N::Base::value; };\n"
	        "void main() {\n"
	        "    type<Exposed> info = type<Exposed>();\n"
	        "    field_info<Exposed> field = field_info<Exposed>(info.qualified_name, 0);\n"
	        "    Exposed value = Exposed();\n"
	        "    field.set<int>(value, 19);\n"
	        "    print(field.get<int>(value));\n"
	        "}\n",
	        "19\n",
	        "qualified public using permits a protected field through private inheritance"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(
	        "struct MutableBox { public: mutable int value; int locked; };\n"
	        "void main() {\n"
	        "    const MutableBox box = MutableBox();\n"
	        "    field_info<MutableBox> field = field_info<MutableBox>(type<MutableBox>().qualified_name, 0);\n"
	        "    field.set<int>(box, 3);\n"
	        "    print(field.get<int>(box));\n"
	        "}\n",
	        "3\n",
	        "reflected mutable field write respects a const receiver"))
	{
		return EXIT_FAILURE;
	}
	for (const std::string Source : {
	         std::string(
	             "struct ConstBox { public: const int value = 1; };\n"
	             "void main() {\n"
	             "    ConstBox box = ConstBox();\n"
	             "    field_info<ConstBox> field = field_info<ConstBox>(type<ConstBox>().qualified_name, 0);\n"
	             "    field.set<int>(box, 9);\n"
	             "}\n"),
	         std::string(
	             "class ConstBase { public: const int value = 1; };\n"
	             "class ConstChild : public ConstBase { };\n"
	             "void main() {\n"
	             "    ConstChild child = ConstChild();\n"
	             "    field_info<ConstBase> field = field_info<ConstBase>(type<ConstBase>().qualified_name, 0);\n"
	             "    field.set<int>(child, 9);\n"
	             "}\n")})
	{
		const auto Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), "const field reflection probe compiles"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script) && Vm.GetError().Message.find("const") != std::string::npos,
		        "executable reflection rejects writes to declared const fields"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Module = CompileSource(
		    "struct MutableBox { public: mutable int value; int locked; };\n"
		    "void main() {\n"
		    "    const MutableBox box = MutableBox();\n"
		    "    field_info<MutableBox> field = field_info<MutableBox>(type<MutableBox>().qualified_name, 1);\n"
		    "    field.set<int>(box, 3);\n"
		    "}\n");
		if (!Expect(static_cast<bool>(Module), "const reflection probe compiles"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script) && Vm.GetError().Message.find("const") != std::string::npos,
		        "executable reflection rejects non-mutable field writes through const"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const auto Module = CompileSource(
		    "class Hidden { private: int secret; };\n"
		    "void main() {\n"
		    "    type<Hidden> t = type<Hidden>();\n"
		    "    field_info<Hidden> f = field_info<Hidden>(t.qualified_name, 0);\n"
		    "    Hidden value = Hidden();\n"
		    "    print(f.get<int>(value));\n"
		    "}\n");
		if (!Expect(static_cast<bool>(Module), "private reflection probe compiles"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script) && Vm.GetError().Message.find("inaccessible") != std::string::npos,
		        "executable reflection rejects private fields"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(R"vs(
interface IFieldValue { virtual void change() = 0; virtual int read() const = 0; };
struct FieldValue : public IFieldValue
{
public:
    int value = 1;
    void change() { value++; }
    int read() const { return value; }
};
class InterfaceHolder { public: IFieldValue value; };
void main()
{
    InterfaceHolder owner = InterfaceHolder(); owner.value = FieldValue();
    IFieldValue ordinary = owner.value; ordinary.change(); print(owner.value.read());
    field_info<InterfaceHolder> field = field_info<InterfaceHolder>(type<InterfaceHolder>().qualified_name, 0);
    IFieldValue reflected = field.get<IFieldValue>(owner);
    reflected.change(); print(owner.value.read());
    InterfaceHolder other = InterfaceHolder(); IFieldValue replacement = owner.value;
    field.set<IFieldValue>(other, replacement);
    replacement.change(); print(other.value.read());
    FieldValue source = FieldValue();
    field.set<IFieldValue>(owner, source);
    source.change(); print(owner.value.read());
}
)vs",
	        "2\n3\n4\n1\n", "reflection field access preserves boxed identity and boxes struct sources by value"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
interface ICopyValue { virtual void change() = 0; };
struct CopyValue : public ICopyValue
{
public:
    int value = 1;
    void change() { value = 9; }
};
CopyValue original = CopyValue();
int mutate_source() { original.value = 2; return 0; }
CopyValue& borrow_original() { return original; }
const CopyValue& read_original() { return original; }
CopyValue copy_original() { return borrow_original(); }
class CopyOps
{
public:
    void change(CopyValue value) { value.change(); }
    static void change_static(CopyValue value) { value.value = 12; }
    void change_ref(CopyValue& value) { value.value = 3; }
    void change_interface(ICopyValue value) { value.change(); }
    void observe(CopyValue value, int unused) { print(value.value); }
    void change_array(CopyValue values[1]) { values[0].value = 13; }
};
method_info<CopyOps> copy_method(string name)
{
    type<CopyOps> info = type<CopyOps>();
    for (int i = 0; i < info.method_count(); i++)
    {
        method_info<CopyOps> method = method_info<CopyOps>(info.qualified_name, i);
        if (method.name() == name) return method;
    }
    assert(false);
    return method_info<CopyOps>(info.qualified_name, -1);
}
void main()
{
    CopyOps object = CopyOps();
    method_info<CopyOps> change = copy_method("change");
    change.invoke<void>(object, original); print(original.value);
    method_info<CopyOps> change_static = copy_method("change_static");
    change_static.invoke_static<void>(original); print(original.value);
    method_info<CopyOps> change_interface = copy_method("change_interface");
    change_interface.invoke<void>(object, original); print(original.value);
    ICopyValue boxed = original;
    change_interface.invoke<void>(object, boxed);
    boxed.change(); print(original.value);
    method_info<CopyOps> observe = copy_method("observe");
    observe.invoke<void>(object, original, mutate_source()); print(original.value);
    method_info<CopyOps> change_ref = copy_method("change_ref");
    change_ref.invoke<void>(object, &original); print(original.value);
    object.change(borrow_original()); print(original.value);
    change.invoke<void>(object, borrow_original()); print(original.value);
    CopyValue local_copy = borrow_original(); local_copy.value = 4; print(original.value);
    CopyValue const_copy = read_original(); const_copy.value = 5; print(original.value);
    CopyValue returned_copy = copy_original(); returned_copy.value = 6; print(original.value);
    CopyValue array_copy[1] = {original};
    method_info<CopyOps> change_array = copy_method("change_array");
    change_array.invoke<void>(object, array_copy); print(array_copy[0].value);
}
)vs",
	        "1\n1\n1\n1\n1\n2\n3\n3\n3\n3\n3\n3\n3\n",
	        "reflected value parameters copy at evaluation time while reference and interface bindings retain identity"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
interface IArgument { virtual int read() const = 0; };
interface IChildArgument : IArgument {};
struct ArgumentValue : public IChildArgument { public: int n = 7; int read() const { return n; } };
class ArgumentBase { public: int n = 5; };
class ArgumentDerived : public ArgumentBase {};
class ArgumentOps
{
public:
    int upcast(ArgumentBase base, IArgument view) { return base.n + view.read(); }
    static bool nulls(ArgumentBase base, IArgument view) { return base == nullptr && view == nullptr; }
    void copy(ArgumentValue value) { value.n = 9; }
};
method_info<ArgumentOps> argument_method(string name)
{
    type<ArgumentOps> info = type<ArgumentOps>();
    for (int i = 0; i < info.method_count(); i++)
    {
        method_info<ArgumentOps> method = method_info<ArgumentOps>(info.qualified_name, i);
        if (method.name() == name) return method;
    }
    assert(false);
    return method_info<ArgumentOps>(info.qualified_name, -1);
}
void main()
{
    ArgumentOps object = ArgumentOps();
    IChildArgument child = ArgumentValue();
    method_info<ArgumentOps> upcast = argument_method("upcast");
    print(upcast.invoke<int>(object, ArgumentDerived(), child));
    method_info<ArgumentOps> nulls = argument_method("nulls");
    print(nulls.invoke_static<bool>(nullptr, nullptr));
    method_info<ArgumentOps> copy = argument_method("copy");
    copy.invoke<void>(object, cast<ArgumentValue>(child));
    print(child.read());
}
)vs",
	        "12\ntrue\n7\n", "reflected object arguments allow upcasts, null handles, and explicit value casts"))
	{
		return EXIT_FAILURE;
	}
	{
		const std::string Declarations =
		    "interface IArgument { virtual int read() const = 0; };\n"
		    "interface IChildArgument : IArgument {};\n"
		    "struct ArgumentValue : public IChildArgument { public: int n = 7; int read() const { return n; } };\n"
		    "class ArgumentBase {}; class ArgumentDerived : public ArgumentBase {};\n";
		struct FObjectConversionProbe
		{
			const char* TargetType;
			const char* Setup;
		};
		const FObjectConversionProbe Probes[] = {
		    {"ArgumentValue", "IArgument value = ArgumentValue();"},
		    {"ArgumentDerived", "ArgumentBase value = ArgumentDerived();"},
		    {"IChildArgument", "IArgument value = ArgumentValue();"}};
		for (const bool bStatic : {false, true})
		{
			for (const FObjectConversionProbe& Probe : Probes)
			{
				const std::string Source = Declarations + "class ArgumentOps { public: " + (bStatic ? "static " : "") + "void accept(" + Probe.TargetType + " value) { print(\"entered\"); } };\nvoid main() { " + Probe.Setup + " ArgumentOps object = ArgumentOps(); method_info<ArgumentOps> method = "
				                                                                                                                                                                                                                 "method_info<ArgumentOps>(type<ArgumentOps>().qualified_name, 0); " +
				    (bStatic ? "method.invoke_static<void>(value);" : "method.invoke<void>(object, value);") + " }\n";
				const auto Module = CompileSource(Source);
				if (!Expect(static_cast<bool>(Module), "reflected implicit downcast probe compiles")) return EXIT_FAILURE;
				std::ostringstream Output;
				std::istringstream Input;
				std::ostringstream Errors;
				VeroScript::VeroScriptVm Vm(Output, Input, Errors);
				VeroScript::Test::Configure(Vm);
				if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str().empty() && Vm.GetError().Message.find("invalid implicit object conversion") != std::string::npos,
				        "reflected interface/base downcasts must fail before method entry"))
				{
					return EXIT_FAILURE;
				}
			}
		}
		if (!Expect(!CompileSource(Declarations + "class Holder { public: ArgumentValue value; };\n"
		                                          "void main() { Holder owner = Holder(); IArgument source = ArgumentValue(); "
		                                          "field_info<Holder> field = field_info<Holder>(type<Holder>().qualified_name, 0); "
		                                          "field.set<ArgumentValue>(owner, source); }\n"),
		        "reflected field assignment rejects implicit interface-to-value conversion"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!RunSource(R"vs(
class RefOps
{
public:
    int value = 10;
    void add(int& target, const int& amount) { target += amount; }
    int& forward(int& target) { return target; }
    const int& read(const int& target) const { return target; }
    int& field() { return value; }
    static int& increment(int& target) { target++; return target; }
    void fail(int& target) { target = 99; throw "stop"; }
};
method_info<RefOps> find_method(string name)
{
    type<RefOps> info = type<RefOps>();
    for (int i = 0; i < info.method_count(); i++)
    {
        method_info<RefOps> candidate = method_info<RefOps>(info.qualified_name, i);
        if (candidate.name() == name) return candidate;
    }
    assert(false);
    return method_info<RefOps>(info.qualified_name, -1);
}
class RefBase { public: virtual int& get(int& target) { return target; } };
class RefChild : public RefBase { public: int stored = 20; int& get(int& target) override { return stored; } };
interface IRef { virtual int& get(int& target) = 0; };
class RefImpl : public IRef { public: int& get(int& target) { return target; } };
void main()
{
    RefOps object = RefOps();
    int n = 3;
    method_info<RefOps> add = find_method("add");
    add.invoke<void>(object, &n, &n);
    print(n);
    method_info<RefOps> forward = find_method("forward");
    forward.invoke<int&>(object, &n) += 2;
    print(n);
    array<int> values = array<int>();
    values.push(4);
    int step = 3;
    add.invoke<void>(object, &values[0], &step);
    print(values[0]);
    method_info<RefOps> read = find_method("read");
    const int& view = read.invoke<const int&>(object, &n);
    n++;
    print(view);
    method_info<RefOps> increment = find_method("increment");
    increment.invoke_static<int&>(&n) += 2;
    print(n);
    method_info<RefOps> field = find_method("field");
    int& retained = field.invoke<int&>(object);
    retained += 5;
    print(object.value);
    object = nullptr;
    retained++;
    print(retained);
    method_info<RefBase> base = method_info<RefBase>(type<RefBase>().qualified_name, 0);
    RefChild child = RefChild();
    base.invoke<int&>(child, &n) = 21;
    print(child.stored);
    method_info<IRef> im = method_info<IRef>(type<IRef>().qualified_name, 0);
    RefImpl implementation = RefImpl();
    im.invoke<int&>(implementation, &n)++;
    print(n);
    method_info<RefOps> fail = find_method("fail");
    RefOps other = RefOps();
    try { fail.invoke<void>(other, &n); }
    catch (string error) { print(error); }
    print(n);
}
)vs",
	        "6\n8\n7\n9\n12\n15\n16\n21\n13\nstop\n99\n",
	        "reflection preserves ref aliases, const/ref returns, virtual/interface dispatch, and exceptions"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(R"vs(
int global = 5;
struct RefRecord
{
public:
    int value = 7;
    int& update(int& target) { target++; value++; return value; }
    static const int& read(const int& target) { return target; }
};
void main()
{
    RefRecord record = RefRecord();
    method_info<RefRecord> update = method_info<RefRecord>(type<RefRecord>().qualified_name, 0);
    method_info<RefRecord> read = method_info<RefRecord>(type<RefRecord>().qualified_name, 1);
    int& member = update.invoke<int&>(record, &global);
    member++;
    print(global); print(record.value);
    map<int,int> values = map<int,int>(); values[0] = 10;
    int index = 0;
    update.invoke<int&>(record, &values[index++])++;
    print(index); print(values[0]); print(record.value);
    const int frozen = 12;
    print(read.invoke_static<const int&>(&frozen));
    const int& view = read.invoke_static<const int&>(&global);
    global++;
    print(view);
}
)vs",
	        "6\n9\n1\n11\n11\n12\n7\n",
	        "reflected struct, global, map, static const-ref calls preserve storage and single evaluation"))
	{
		return EXIT_FAILURE;
	}
	for (const bool bStatic : {false, true})
	{
		const std::string Source = std::string("class Factory { public: ") + (bStatic ? "static " : "") + "fn(int)->int get() { return [](int n) -> int => n + 1; } };\n"
		                                                                                                  "void main() { Factory object = Factory();\n"
		                                                                                                  "method_info<Factory> method = method_info<Factory>(type<Factory>().qualified_name, 0);\n"
		                                                                                                  "fn(int)->int work = " +
		    (bStatic ? "method.invoke_static<fn(int)->int>()" : "method.invoke<fn(int)->int>(object)") + "; print(work(41)); }\n";
		if (!RunSource(Source, "42\n", "reflected function-valued return uses full call signature"))
		{
			return EXIT_FAILURE;
		}
		for (const std::string& InvalidOperand : {std::string("fn(int)->int"), std::string("@invoke:invalid")})
		{
			auto Module = CompileSource(Source);
			if (!Expect(static_cast<bool>(Module), "reflection signature rejection probe compiles")) return EXIT_FAILURE;
			bool bReplaced = false;
			for (std::string& Text : Module->StringPool)
			{
				if (Text == "@invoke:fn()->fn(int)->int")
				{
					Text = InvalidOperand;
					bReplaced = true;
				}
			}
			if (!Expect(bReplaced, "reflection signature rejection probe finds typed signature")) return EXIT_FAILURE;
			std::ostringstream Output;
			std::istringstream Input;
			std::ostringstream Errors;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str().empty() && Vm.GetError().Message.find("invalid reflected call signature") != std::string::npos,
			        "untagged or malformed reflection call signatures must be rejected"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	for (const bool bStatic : {false, true})
	{
		const std::string Source = std::string("void nothing() {} class VoidOps { public: ") + (bStatic ? "static " : "") + "void accept(int value) { print(\"entered\"); } };\n"
		                                                                                                                    "void main() { VoidOps object = VoidOps(); method_info<VoidOps> method = "
		                                                                                                                    "method_info<VoidOps>(type<VoidOps>().qualified_name, 0); " +
		    (bStatic ? "method.invoke_static<void>(nothing());" : "method.invoke<void>(object, nothing());") + " }\n";
		if (!Expect(!CompileSource(Source), "void expressions cannot be reflected arguments")) return EXIT_FAILURE;
		std::string ValidSource = Source;
		const std::size_t VoidArgument = ValidSource.find("nothing()", ValidSource.find("void main()"));
		if (!Expect(VoidArgument != std::string::npos, "void argument probe finds call")) return EXIT_FAILURE;
		ValidSource.replace(VoidArgument, std::string("nothing()").size(), "0");
		bool bAllRejected = true;
		for (const std::string& InvalidSignature : {
		         std::string("@invoke:fn(void)->void"),
		         std::string("@invoke:fn(int,)->void"),
		         std::string("@invoke:fn(string)->void"),
		         std::string("@invoke:fn(nullptr)->void"),
		         std::string("@invoke:fn(unknown_type)->void")})
		{
			auto Module = CompileSource(ValidSource);
			if (!Expect(static_cast<bool>(Module), "signature consistency rejection probe compiles")) return EXIT_FAILURE;
			bool bReplaced = false;
			for (std::string& Text : Module->StringPool)
			{
				if (Text == "@invoke:fn(int)->void")
				{
					Text = InvalidSignature;
					bReplaced = true;
				}
			}
			if (!Expect(bReplaced, "signature consistency rejection probe finds signature")) return EXIT_FAILURE;
			std::ostringstream Output;
			std::istringstream Input;
			std::ostringstream Errors;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			const std::string Label = "inconsistent signature must fail before method entry: " + InvalidSignature;
			if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str().empty(), Label.c_str()))
			{
				bAllRejected = false;
			}
		}
		if (!bAllRejected) return EXIT_FAILURE;
	}
	for (const std::string& InvalidReflectionCall : {
	         "const int n = 1; method.invoke<void>(object, &n);",
	         "int n = 1; const int& view = n; method.invoke<void>(object, &view);",
	         "const RefCheck readonly = object; method.invoke<void>(object, &readonly.field);",
	         "int n = 1; method.invoke<void>(object, n);",
	         "double n = 1.0; method.invoke<void>(object, &n);",
	         "int n = 1; method.invoke<int&>(object, &n);"})
	{
		const auto Module = CompileSource(
		    "class RefCheck { public: int field = 1; void update(int& value) { print(\"entered\"); value++; } };\n"
		    "void main() { RefCheck object = RefCheck(); "
		    "method_info<RefCheck> method = method_info<RefCheck>(type<RefCheck>().qualified_name, 0); " +
		    InvalidReflectionCall + " }\n");
		if (!Expect(static_cast<bool>(Module), "dynamic reference reflection rejection probe compiles"))
		{
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(!Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str().empty(), "invalid reflected reference signature fails before method entry"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const std::string& InvalidReferenceSource : {
	         "class Read { public: int value; const int& get() const { return value; } }; "
	         "void main() { Read object = Read(); method_info<Read> method = "
	         "method_info<Read>(type<Read>().qualified_name, 0); "
	         "method.invoke<const int&>(object) = 1; }\n",
	         "class Ref { public: int& get(int& value) { return value; } }; "
	         "void main() { Ref object = Ref(); method_info<Ref> method = "
	         "method_info<Ref>(type<Ref>().qualified_name, 0); "
	         "method.invoke<int&>(object, &42); }\n",
	         "struct Value { public: int field; }; Value make() { return Value(); } "
	         "class Ref { public: void update(int& value) { value++; } }; "
	         "void main() { Ref object = Ref(); method_info<Ref> method = "
	         "method_info<Ref>(type<Ref>().qualified_name, 0); "
	         "method.invoke<void>(object, &make().field); }\n",
	         "void main() { int& alias = 1; }\n",
	         "void main() { int value = 1; const int& alias = value; alias = 2; }\n",
	         "void main() { double value = 1; int& alias = value; }\n"})
	{
		if (!Expect(
		        !CompileSource(InvalidReferenceSource),
		        "invalid reference binding or const-reference write must fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "function(int)->int forward(function(int)->int value) { return value; }\n"
	        "void main() {\n"
	        "    function(int)->int inc = function(int x) -> int => x + 1;\n"
	        "    print(forward(inc)(41));\n"
	        "}\n",
	        "42\n",
	        "function spelling works in declaration signatures"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    function(int)->int inc = function(int x) -> int => x + 1;\n"
	        "    fn(int)->int legacy = inc;\n"
	        "    print(legacy(41));\n"
	        "}\n",
	        "42\n",
	        "function spelling canonicalizes to fn"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource(
	        "void main() {\n"
	        "    function(int&, int)->void add = function(int& value, int amount) -> void { value = value + amount; };\n"
	        "    int value = 7;\n"
	        "    add(&value, 5);\n"
	        "    print(value);\n"
	        "    function(const int&)->int read = function(item) => item;\n"
	        "    print(read(&value));\n"
	        "}\n",
	        "12\n12\n",
	        "function alias supports ampersand reference signatures"))
	{
		return EXIT_FAILURE;
	}

	{
		VeroScript::VeroScriptLexer Lexer(
		    "class Secret { private: int value; };\n"
		    "void main() { Secret secret = Secret(); int& leaked = secret.value; }\n",
		    "private_ref.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		VeroScript::VeroScriptParser Parser(Tokens, "private_ref.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
		    Program ? Compiler.Compile(*Program) : nullptr;
		if (!Expect(
		        !Module || Compiler.HasError(),
		        "reference binding must preserve private-field access control"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "struct Point { int x; };\n"
	        "void main() {\n"
	        "    int value = 4;\n"
	        "    int& alias = value;\n"
	        "    alias = 9;\n"
	        "    print(value);\n"
	        "    const int& readonly = value;\n"
	        "    print(readonly);\n"
	        "    auto& inferred = value;\n"
	        "    inferred = 9;\n"
	        "    const auto& inferred_readonly = inferred;\n"
	        "    print(inferred_readonly);\n"
	        "    const double& temporary = 2;\n"
	        "    print(temporary);\n"
	        "    array<int> values = {3};\n"
	        "    int& element = values[0];\n"
	        "    element = 7;\n"
	        "    print(values[0]);\n"
	        "    Point point = Point();\n"
	        "    int& field = point.x;\n"
	        "    field = 11;\n"
	        "    print(point.x);\n"
	        "    for (int& loop_alias = value; loop_alias < 10; loop_alias++) {}\n"
	        "    print(value);\n"
	        "}\n",
	        "9\n9\n9\n2\n7\n11\n10\n",
	        "ordinary mutable and const reference variables"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "function() -> int make_counter() {\n"
	        "    int value = 40;\n"
	        "    return [&]() -> int { value++; return value; };\n"
	        "}\n"
	        "void main() {\n"
	        "    function() -> int counter = make_counter();\n"
	        "    print(counter());\n"
	        "    print(counter());\n"
	        "    int value = 1;\n"
	        "    int step = 10;\n"
	        "    function() -> int mixed = [=, &value]() -> int { value += step; return value; };\n"
	        "    step = 20;\n"
	        "    print(mixed());\n"
	        "    print(value);\n"
	        "    int other = 2;\n"
	        "    function() -> int reverse = [&, other]() -> int { value += other; return value; };\n"
	        "    other = 100;\n"
	        "    print(reverse());\n"
	        "    function() -> int first = [&value]() -> int { value++; return value; };\n"
	        "    function() -> int second = [&value]() -> int { value += 2; return value; };\n"
	        "    print(first());\n"
	        "    print(second());\n"
	        "    print(first());\n"
	        "}\n",
	        "41\n42\n11\n11\n13\n14\n16\n17\n",
	        "reference captures survive their defining frame and support mixed defaults"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "struct RefRecord { int value; };\n"
	        "void main() {\n"
	        "    RefRecord original = RefRecord();\n"
	        "    original.value = 7;\n"
	        "    int& alias = original.value;\n"
	        "    function()->int update = [&alias]() -> int { alias++; return alias; };\n"
	        "    RefRecord copied = original;\n"
	        "    print(update());\n"
	        "    print(original.value);\n"
	        "    print(copied.value);\n"
	        "}\n",
	        "8\n8\n7\n",
	        "reference-cell representation stays transparent to struct copying"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    array<int> values = {1};\n"
	        "    int& element = values[0];\n"
	        "    function()->int read_element = [&element]() -> int { return element; };\n"
	        "    values[0] = 5;\n"
	        "    print(read_element());\n"
	        "    map<int,int> mapped = map<int,int>();\n"
	        "    mapped[1] = 2;\n"
	        "    int& mapped_value = mapped[1];\n"
	        "    function()->int read_map = [&mapped_value]() -> int { return mapped_value; };\n"
	        "    mapped[1] = 8;\n"
	        "    print(read_map());\n"
	        "    ordered_map<int,int> ordered = ordered_map<int,int>();\n"
	        "    ordered[1] = 3;\n"
	        "    int& ordered_value = ordered[1];\n"
	        "    function()->int read_ordered = [&ordered_value]() -> int { return ordered_value; };\n"
	        "    ordered[1] = 9;\n"
	        "    print(read_ordered());\n"
	        "    deque<int> deque_values = {4};\n"
	        "    int& deque_value = deque_values[0];\n"
	        "    function()->int read_deque = [&deque_value]() -> int { return deque_value; };\n"
	        "    deque_values[0] = 10;\n"
	        "    print(deque_values.front());\n"
	        "    print(deque_values.back());\n"
	        "    print(deque_values.pop_front());\n"
	        "    print(read_deque());\n"
	        "}\n",
	        "5\n8\n9\n10\n10\n10\n10\n",
	        "container writes preserve escaped reference capture cells"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    fn(int)->int inc = fn(int x) -> int => x + 1;\n"
	        "    int y = inc(41);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "basic literal assign and call"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "int apply(fn(int)->int f, int x) {\n"
	        "    return f(x);\n"
	        "}\n"
	        "void main() {\n"
	        "    int y = apply(fn(int x) -> int { return x * 2; }, 21);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "pass as argument"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "fn(int)->int make_adder(int n) {\n"
	        "    return fn(int x) -> int => x + n;\n"
	        "}\n"
	        "void main() {\n"
	        "    fn(int)->int add5 = make_adder(5);\n"
	        "    int y = add5(37);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "return function with capture"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    int base = 40;\n"
	        "    fn(int)->int add = fn(int x) -> int => x + base;\n"
	        "    int y = add(2);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "capture local int"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    int base = 40;\n"
	        "    fn(int)->int a = [=](int x) -> int => x + base;\n"
	        "    fn(int)->int b = [base](int x) -> int => x + base;\n"
	        "    fn(int)->int c = [](int x) -> int => x + 2;\n"
	        "    assert(a(2) == 42);\n"
	        "    assert(b(2) == 42);\n"
	        "    assert(c(40) == 42);\n"
	        "    print(a(2));\n"
	        "    print(b(2));\n"
	        "    print(c(40));\n"
	        "}\n",
	        "42\n42\n42\n",
	        "capture-list sugar [=] / [names] / []"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "int g = 40;\n"
	        "void main() {\n"
	        "    fn(int)->int add = [g](int x) -> int => x + g;\n"
	        "    assert(add(2) == 42);\n"
	        "    print(add(2));\n"
	        "}\n",
	        "42\n",
	        "explicit capture list may name globals"))
	{
		return EXIT_FAILURE;
	}

	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(
		    "void main() {\n"
		    "    int a = 1;\n"
		    "    int b = 2;\n"
		    "    fn()->int f = [a]() -> int => a + b;\n"
		    "}\n");
		if (!Expect(!Module, "explicit list must include every captured local"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(
		    "void main() {\n"
		    "    int a = 1;\n"
		    "    fn()->int f = [a, a]() -> int => a;\n"
		    "}\n");
		if (!Expect(!Module, "duplicate capture name must fail"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const std::string& RedundantCaptureSource : {
	         "void main() { int a = 1; function()->int f = [=, a]() -> int => a; }\n",
	         "void main() { int a = 1; function()->int f = [&, &a]() -> int => a; }\n"})
	{
		if (!Expect(
		        !CompileSource(RedundantCaptureSource),
		        "lambda capture defaults must reject redundant explicit capture modes"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "int twice(int x) {\n"
	        "    return x * 2;\n"
	        "}\n"
	        "void main() {\n"
	        "    fn(int)->int f = twice;\n"
	        "    int y = f(21);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "named free function as value"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    int x = 40;\n"
	        "    fn()->int outer = fn() -> int {\n"
	        "        fn(int)->int inner = fn(int x) -> int => x;\n"
	        "        assert(inner(1) == 1);\n"
	        "        return x + 2;\n"
	        "    };\n"
	        "    int y = outer();\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "nested lambda param does not suppress outer capture"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    int x = 40;\n"
	        "    fn()->int outer = [=]() -> int {\n"
	        "        fn()->int inner = [=]() -> int => x;\n"
	        "        return inner();\n"
	        "    };\n"
	        "    int y = outer();\n"
	        "    assert(y == 40);\n"
	        "    print(y);\n"
	        "}\n",
	        "40\n",
	        "outer packs grandparent local used only in nested lambda"))
	{
		return EXIT_FAILURE;
	}

	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(
		    "void main() {\n"
		    "    int x = 1;\n"
		    "    fn()->int outer = []() -> int {\n"
		    "        fn()->int inner = [=]() -> int => x;\n"
		    "        return inner();\n"
		    "    };\n"
		    "}\n");
		if (!Expect(!Module, "captureless outer cannot forward nested grandparent capture"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "fn(int)->int g = fn(int x) -> int => x + 1;\n"
	        "void main() {\n"
	        "    int y = g(41);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "global function value CallIndirect"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "class Box {\n"
	        "    public:\n"
	        "    int n;\n"
	        "    Box(int v) { n = v; }\n"
	        "    fn()->int getter() { return fn() -> int => n; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Box b = Box(42);\n"
	        "    fn()->int g = b.getter();\n"
	        "    int y = g();\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "lambda captures this field"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "class ImplicitLambdaBase {\n"
	        "    public:\n"
	        "    int Pick(int value) { return value + 1; }\n"
	        "    int Pick(string value) { return len(value) + 10; }\n"
	        "}\n"
	        "class ImplicitLambdaDerived : ImplicitLambdaBase {\n"
	        "    public:\n"
	        "    fn(int)->int getter() {\n"
	        "        return [=](int value) -> int => Pick(value);\n"
	        "    }\n"
	        "}\n"
	        "void main() {\n"
	        "    ImplicitLambdaDerived value = ImplicitLambdaDerived();\n"
	        "    fn(int)->int g = value.getter();\n"
	        "    print(g(41));\n"
	        "}\n",
	        "42\n",
	        "lambda bare inherited overload captures this"))
	{
		return EXIT_FAILURE;
	}

	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(
		    "class ImplicitLambdaCaptureless {\n"
		    "    public:\n"
		    "    int Value() { return 1; }\n"
		    "    fn()->int getter() { return []() -> int => Value(); }\n"
		    "}\n"
		    "void main() {}\n");
		if (!Expect(!Module, "captureless lambda rejects bare implicit-this method"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "int PickFree() { return 42; }\n"
	        "class ImplicitLambdaShadow {\n"
	        "    public:\n"
	        "    int PickFree() { return 1; }\n"
	        "    fn()->int getter() { return []() -> int => PickFree(); }\n"
	        "}\n"
	        "void main() {\n"
	        "    ImplicitLambdaShadow value = ImplicitLambdaShadow();\n"
	        "    fn()->int g = value.getter();\n"
	        "    print(g());\n"
	        "}\n",
	        "42\n",
	        "free function shadows method without forcing this capture"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    int y = (fn(int x) -> int => x + 1)(41);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "expression callee lambda call"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    int n = 0;\n"
	        "again:\n"
	        "    n = n + 1;\n"
	        "    if (n >= 3) {\n"
	        "        print(n);\n"
	        "        return;\n"
	        "    }\n"
	        "    goto again;\n"
	        "}\n",
	        "3\n",
	        "goto same scope depth"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    array<int> a = array<int>();\n"
	        "    a.push(1);\n"
	        "    a.push(2);\n"
	        "    a.push(3);\n"
	        "    a.remove_at(1);\n"
	        "    assert(len(a) == 2);\n"
	        "    assert(a[0] == 1);\n"
	        "    assert(a[1] == 3);\n"
	        "    assert(a.remove(3));\n"
	        "    assert(len(a) == 1);\n"
	        "    print(a[0]);\n"
	        "}\n",
	        "1\n",
	        "array remove_at and remove"))
	{
		return EXIT_FAILURE;
	}

	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(
		    "void main() {\n"
		    "    goto skip;\n"
		    "    int x = 1;\n"
		    "skip:\n"
		    "    print(x);\n"
		    "}\n");
		if (!Expect(!Module, "goto over local init must fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "class Box {\n"
	        "    public:\n"
	        "    int n;\n"
	        "    Box() {\n"
	        "        n = 0;\n"
	        "    again:\n"
	        "        n = n + 1;\n"
	        "        if (n >= 3) {\n"
	        "            return;\n"
	        "        }\n"
	        "        goto again;\n"
	        "    }\n"
	        "}\n"
	        "void main() {\n"
	        "    Box b = Box();\n"
	        "    assert(b.n == 3);\n"
	        "    print(b.n);\n"
	        "}\n",
	        "3\n",
	        "goto inside constructor body"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "class Counter {\n"
		    "    public:\n"
		    "    int n;\n"
		    "    Counter() { n = 0; }\n"
		    "}\n"
		    "void main() {\n"
		    "    multicast_delegate a = multicast_delegate();\n"
		    "    Counter c = Counter();\n"
		    "    fn()->void h = fn() mutable -> void { c.n = c.n + 1; };\n"
		    "    a += h;\n"
		    "    a.broadcast();\n"
		    "    assert(a.count() == 1);\n"
		    "    assert(c.n == 1);\n"
		    "    a -= h;\n"
		    "    assert(a.count() == 0);\n"
		    "    a.broadcast();\n"
		    "    assert(c.n == 1);\n"
		    "    print(c.n);\n"
		    "}\n",
		    "<lambda-events>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "multicast_delegate +=/-= add/remove"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "1\n", "multicast_delegate +=/-= output"))
		{
			std::cerr << "got:\n"
			          << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "void main() {\n"
		    "    delegate<int, int, int, int> d = delegate<int, int, int, int>();\n"
		    "    d.bind(function(int a, int b, int c)->int => a + b + c);\n"
		    "    print(d.execute(1, 2, 3));\n"
		    "    multicast_delegate<int, int, int> e = multicast_delegate<int, int, int>();\n"
		    "    function(int, int, int)->void h = function(int a, int b, int c)->void { print(a * b * c); };\n"
		    "    e.add(h);\n"
		    "    e.broadcast(2, 3, 4);\n"
		    "}\n",
		    "<lambda-delegate-three-params>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "three-parameter typed delegates"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "6\n24\n", "three-parameter delegate output"))
		{
			std::cerr << "got:\n"
			          << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "void main() {\n"
		    "    delegate<int, int> d = delegate<int, int>();\n"
		    "    assert(!d.execute_if_bound(2));\n"
		    "    d.bind(function(int x) -> int => x * 3);\n"
		    "    assert(d.execute_if_bound(4));\n"
		    "}\n",
		    "<lambda-delegate-execute-if-bound>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "delegate execute_if_bound"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    R"Vero(import "delegate.vs";
void main() {
    delegate<int> d = delegate<int>();
    d.bind(fn() -> int => 7);
    assert(d.is_bound());
    assert(d.execute() == 7);
    d.unbind();
    assert(!d.is_bound() && !d.execute_if_bound());
    d.bind(fn() -> int => 11);
    assert(d.execute() == 11);
}
)Vero",
		    "<lambda-delegate-unbind>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "delegate unbind clears binding and allows rebinding"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "class Counter {\n"
		    "    public:\n"
		    "    int n;\n"
		    "    Counter() { n = 0; }\n"
		    "}\n"
		    "void main() {\n"
		    "    multicast_delegate a = multicast_delegate();\n"
		    "    Counter c = Counter();\n"
		    "    fn()->void later = fn() mutable -> void { c.n = c.n + 10; };\n"
		    "    fn()->void first = fn() mutable -> void {\n"
		    "        c.n = c.n + 1;\n"
		    "        a.remove(later);\n"
		    "    };\n"
		    "    a.add(first);\n"
		    "    a.add(later);\n"
		    "    a.broadcast();\n"
		    "    assert(a.count() == 1);\n"
		    "    assert(c.n == 11);\n"
		    "    a.broadcast();\n"
		    "    assert(c.n == 12);\n"
		    "    print(c.n);\n"
		    "}\n",
		    "<lambda-events-reentrancy>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "multicast_delegate broadcast snapshot survives remove"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "12\n", "multicast_delegate broadcast reentrancy output"))
		{
			std::cerr << "got:\n"
			          << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "void main() {\n"
		    "    delegate<int, int> d = delegate<int, int>();\n"
		    "    d.bind(fn(int x) -> int => x + 1);\n"
		    "    assert(d.is_bound());\n"
		    "    assert(d.execute(41) == 42);\n"
		    "    d.bind(fn(int x) -> int => x * 2);\n"
		    "    print(d.execute(21));\n"
		    "}\n",
		    "<lambda-delegate-ret>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "delegate<int,int> bind/execute return value"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "42\n", "delegate<int,int> execute output"))
		{
			std::cerr << "got:\n"
			          << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "void main() {\n"
		    "    delegate<int> d = delegate<int>();\n"
		    "    d.execute();\n"
		    "}\n",
		    "<lambda-delegate-unbound>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unbound delegate execute must fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "void main() {\n"
		    "    delegate<void> d = delegate<void>();\n"
		    "    d.bind(fn() -> void => print(9));\n"
		    "    d.execute();\n"
		    "}\n",
		    "<lambda-delegate-void>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "delegate<void> bind/execute"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "9\n", "delegate<void> output"))
		{
			std::cerr << "got:\n"
			          << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
		    "import \"delegate.vs\";\n"
		    "using ScoreEvent = multicast_delegate<int>;\n"
		    "using Mitigate = delegate<int, int>;\n"
		    "void main() {\n"
		    "    ScoreEvent onScore = ScoreEvent();\n"
		    "    fn(int)->void h = fn(int x) -> void => print(x);\n"
		    "    onScore += h;\n"
		    "    onScore.broadcast(7);\n"
		    "    Mitigate m = Mitigate();\n"
		    "    m.bind(fn(int x) -> int => x + 1);\n"
		    "    print(m.execute(41));\n"
		    "}\n",
		    "<using-alias>",
		    Output,
		    Errors,
		    VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "using type alias for delegate types"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "7\n42\n", "using alias output"))
		{
			std::cerr << "got:\n"
			          << Output.str();
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "void main() {\n"
	        "    array<int> a = array<int>();\n"
	        "    a.push(1).push(2).remove_at(0);\n"
	        "    assert(len(a) == 1);\n"
	        "    assert(a[0] == 2);\n"
	        "    print(a[0]);\n"
	        "}\n",
	        "2\n",
	        "array remove_at chains like clear"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "void main() {\n"
	        "    fn(int)->int f = fn(x) => x * 2;\n"
	        "    assert(f(21) == 42);\n"
	        "    print(f(21));\n"
	        "}\n",
	        "42\n",
	        "contextual lambda param types from var type"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "int apply(fn(int)->int g, int x) {\n"
	        "    return g(x);\n"
	        "}\n"
	        "void main() {\n"
	        "    int y = apply(fn(x) => x + 1, 41);\n"
	        "    assert(y == 42);\n"
	        "    print(y);\n"
	        "}\n",
	        "42\n",
	        "contextual lambda from call argument type"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(
	        "class Holder {\n"
	        "    public:\n"
	        "    fn(int)->int bind(fn(int)->int f) { return f; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Holder h = Holder();\n"
	        "    fn(int)->int f = h.bind(fn(x) => x + 1);\n"
	        "    assert(f(41) == 42);\n"
	        "    print(f(41));\n"
	        "}\n",
	        "42\n",
	        "contextual lambda from method argument type"))
	{
		return EXIT_FAILURE;
	}

	{
		VeroScript::VeroScriptLexer Lexer(
		    "void main() {\n"
		    "    auto f = fn(x) => x;\n"
		    "}\n",
		    "lambda_infer.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		VeroScript::VeroScriptParser Parser(Tokens, "lambda_infer.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
		    Program ? Compiler.Compile(*Program) : nullptr;
		if (!Expect(!Module || Compiler.HasError(), "untyped lambda without context fails"))
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::VeroScriptLexer Lexer(
		    "void main() {\n"
		    "    fn(int)->int f = fn(int x, int y) -> int => x + y;\n"
		    "}\n",
		    "lambda_arity.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		VeroScript::VeroScriptParser Parser(Tokens, "lambda_arity.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
		    Program ? Compiler.Compile(*Program) : nullptr;
		if (!Expect(
		        !Module || Compiler.HasError(),
		        "fully typed lambda arity mismatch vs expected fails"))
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::VeroScriptLexer Lexer(
		    "void main() {\n"
		    "    fn(int)->int f = fn(double x) -> int => 0;\n"
		    "}\n",
		    "lambda_param_conflict.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		VeroScript::VeroScriptParser Parser(Tokens, "lambda_param_conflict.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
		    Program ? Compiler.Compile(*Program) : nullptr;
		if (!Expect(
		        !Module || Compiler.HasError(),
		        "fully typed lambda param conflict vs expected fails"))
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!RunSource(
	        "class CallMe {\n"
	        "    public:\n"
	        "    int operator()(int n) { return n + 1; }\n"
	        "}\n"
	        "void main() {\n"
	        "    CallMe a = CallMe();\n"
	        "    fn()->int f = [=]() mutable => a(3);\n"
	        "    print(f());\n"
	        "}\n",
	        "4\n",
	        "lambda captures local used as operator() callee"))
	{
		return EXIT_FAILURE;
	}

	if (!RunSource(R"VS(
class ConstLambda {
public:
int value=0;
mutable int cached=0;
int pick(){return 1;}
int pick()const{return 2;}
int read()const{
int local=0;
auto bare=[&]()->int{local++;cached++;return pick();};
auto direct=[=]()->int{return this.pick();};
auto nested=[=]()->int{auto inner=[&]()->int{return pick();};return inner();};
int result=bare()+direct()+nested();assert(local==1 && cached==1);return result;}
int write(){auto f=[&]()->int{value++;return pick();};return f();}
};
class Parent {public:int read()const{return 7;}};
class Child:Parent{public:int read()const{auto f=[=]()->int{return Super.read();};return f();}};
void main(){const ConstLambda c=ConstLambda();ConstLambda m=ConstLambda();
assert(c.read()==6 && m.write()==1 && m.value==1);
const Child child=Child();assert(child.read()==7);
Parent Super=Parent();auto f=[Super]()->int{return Super.read();};assert(f()==7);
auto parameter=[](Parent Super)->int{return Super.read();};assert(parameter(Parent())==7);}
)VS",
	        "", "lambda preserves captured-this constness and Super context"))
	{
		return EXIT_FAILURE;
	}
	if (!RunSource("int Super(){return 7;}void main(){auto f=[]()->int{fn()->int target=Super;return target();};print(f());}",
	        "7\n", "ordinary function named Super is not a receiver alias"))
	{
		return EXIT_FAILURE;
	}
	for (const auto* Body : {"value=1;", "this.value=1;", "mutate();"})
	{
		const std::string Source = std::string("class C{public:int value=0;void mutate(){value++;}"
		                                       "void read()const{auto f=[&]()->void{") +
		    Body + "};f();}};void main(){}";
		if (!Expect(!CompileSource(Source), "const-this lambda rejects member mutation")) return EXIT_FAILURE;
	}
	if (!Expect(!CompileSource("class Parent{public:int read(){return 1;}};"
	                           "class Child:Parent{public:int read()const{auto f=[&]()->int{return Super.read();};return f();}};"
	                           "void main(){}"),
	        "const-this lambda rejects mutable base method"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(!CompileSource("class Parent{public:int read()const{return 1;}};"
	                           "class Child:Parent{public:int read()const{auto f=[]()->int{return Super.read();};return f();}};"
	                           "void main(){}"),
	        "captureless lambda rejects implicit Super capture"))
	{
		return EXIT_FAILURE;
	}
	std::cout << "VeroScriptLambdaTests passed\n";
	return EXIT_SUCCESS;
}
