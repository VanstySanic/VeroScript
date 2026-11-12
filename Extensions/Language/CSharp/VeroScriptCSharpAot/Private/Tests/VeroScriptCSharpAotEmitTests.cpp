// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptCSharpAot.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptAst.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#ifndef VEROSCRIPT_CSHARP_AOT_TEST_VERSION
#define VEROSCRIPT_CSHARP_AOT_TEST_VERSION "0.1.1"
#endif

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (Condition)
		{
			return true;
		}
		std::cerr << "FAIL: " << Message << '\n';
		return false;
	}

	std::size_t CountOccurrences(const std::string& Text, const std::string& Needle)
	{
		std::size_t Count = 0;
		for (std::size_t Position = 0;
		    (Position = Text.find(Needle, Position)) != std::string::npos;
		    Position += Needle.size())
		{
			++Count;
		}
		return Count;
	}

	bool TestInitializationInspectionDepth()
	{
		std::string DeepCalls = "bool initialized=start_0()==7;void main(){}";
		for (int Index = 0; Index < 4000; ++Index)
		{
			DeepCalls += "int start_" + std::to_string(Index) + "(){return start_" +
			    std::to_string(Index + 1) + "();}";
		}
		DeepCalls += "int start_4000(){return 7;}";
		// Scalar fields keep Core type-shape recursion out of this emitter regression.
		std::string MixedInitialization;
		for (int Index = 0; Index < 150; ++Index)
		{
			MixedInitialization += "class Node_" + std::to_string(Index) + "{public:int value=start_" +
			    std::to_string(Index + 1) + "();};int start_" + std::to_string(Index) +
			    "(){Node_" + std::to_string(Index) + " node=Node_" + std::to_string(Index) + "();return node.value;}";
		}
		MixedInitialization += "int start_150(){return 7;}bool initialized=start_0()==7;void main(){}";
		for (const auto* Source : {&DeepCalls, &MixedInitialization})
		{
			std::string Output = "unchanged", Error;
			if (!Expect(!VeroScript::EmitCSharpFromSource(*Source, "deep-startup.vs", Output, Error) &&
			                Output.empty() && Error.find("startup function/type inspection depth") != std::string::npos,
			        "deep startup calls and type initializers fail with a diagnostic instead of exhausting the stack"))
			{
				std::cerr << Error << '\n';
				return false;
			}
		}

		std::string Ordinary = "int leaf(){return 7;}int repeated(){";
		for (int Index = 0; Index < 400; ++Index)
		{
			Ordinary += "leaf();";
		}
		Ordinary += "return 7;}bool initialized=repeated()==7;void main(){}";
		std::string Output, Error;
		if (!Expect(VeroScript::EmitCSharpFromSource(Ordinary, "wide-startup.vs", Output, Error) && !Output.empty(),
		        "sequential startup calls do not accumulate inspection depth"))
		{
			std::cerr << Error << '\n';
			return false;
		}
		return true;
	}
}

int main(int Argc, char** Argv)
{
	if (!TestInitializationInspectionDepth())
	{
		return EXIT_FAILURE;
	}
	{
		std::string Output, Error;
		if (!Expect(VeroScript::EmitCSharpFromSource(
		        "class Link<T>{public:T value;Link<T> next=nullptr;Link<T> Get(){return next;}};"
		        "void main(){Link<int> first=Link<int>();Link<int> second=Link<int>();"
		        "second.value=42;first.next=second;assert(first.Get().value==42);first.next=nullptr;}",
		        "generic-self-reference.vs", Output, Error) && !Output.empty(),
		    "generic class reference fields remain representable in C# source emission"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output, Error;
		if (!VeroScript::EmitCSharpFromSource(R"VS(constexpr int plus(int n){if constexpr(true)return n+1;else return nonexistent();}constexpr int count=plus(3);constexpr uint64 maximum=18446744073709551615u64;enum class Mode{First=plus(1),Second};constexpr Mode selected=Mode::Second;struct Config{static constexpr int count=plus(4);};class Card{public:int value=9;int get(){auto reader=[=]()->int{if constexpr(true)return value;else return missing();};return reader();}};void main(){Card card=Card();assert(card.get()==9);if constexpr(false){struct Discarded{int read(){return missing();}};int discarded=nonexistent();}constexpr int n=plus(5);print(n);print(count);print(Config::count);int runtime=7;print(plus(runtime));print(maximum);assert(selected==Mode::Second);print(3);constexpr auto narrowed=cast<int16>(7);print(narrowed);})VS",
		        "constexpr-aot.vs", Output, Error))
		{
			std::cerr << "FAIL: constexpr source emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
		if (Argc > 1)
		{
			std::ofstream File(Argv[1], std::ios::binary);
			File << Output;
			return File ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		if (VeroScript::EmitCSharpFromSource("constexpr int size(){return 3;}void main(){int data[size()];}",
		        "constexpr-array-boundary.vs", Output, Error) ||
		    !Output.empty() || Error.find("fixed-size native arrays") == std::string::npos)
		{
			return EXIT_FAILURE;
		}
		if (VeroScript::EmitCSharpFromSource("int f(){return 3;}constexpr int n=f();int main(){return n;}",
		        "constexpr-invalid.vs", Output, Error) ||
		    !Output.empty() || Error.find("constexpr") == std::string::npos)
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto* Source : {
	         "class Box{public:template<typename T> T echo(T value){return value;}};int main(){return 0;}",
	         "template<int N> int value(){return N;}int main(){return value<3>();}",
	         "template<typename T> T identity(T value){return value;}int main(){return identity<>(7);}",
	         "template<typename T> T identity(T value){return value;}int main(){auto call=identity<int>;return call(7);}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitCSharpFromSource(Source, "template-aot.vs", Output, Error) || !Output.empty() || Error.find("Core AOT") == std::string::npos)
		{
			std::cerr << "unsupported templates must fail before source emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output, Error;
		if (!VeroScript::EmitCSharpFromSource(
		        "T identity<T>(T value){using Value=T;Value result=value;return result;}"
		        "int main(){using Value=int;Value result=identity<int>(7);return result;}",
		        "local-alias-aot.vs", Output, Error) ||
		    Output.empty())
		{
			std::cerr << "ordinary block aliases must remain eligible: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	for (const auto* Source : {"struct Outer{struct Item{};};int main(){return 0;}",
	         "int main(){struct Item{};return 0;}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitCSharpFromSource(Source, "scoped-aot.vs", Output, Error) || !Output.empty() || Error.find("nested and local types") == std::string::npos)
		{
			std::cerr << "scoped types must fail before standalone emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	for (const char* Source : {
	         "void main(){variant value=1;}",
	         "variant identity<T>(T value){return value;} void main(){}",
	         "T unused<T>(T value){variant local=value;return value;} void main(){}",
	         "using Dynamic=variant; void main(){}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitCSharpFromSource(Source, "variant-aot.vs", Output, Error) || !Output.empty() || Error.find("diag.aot_variant_unsupported") == std::string::npos)
		{
			std::cerr << "variant must fail before standalone emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const char* Source : {
	         "import \"string.vs\"; void main(){print(std::strings::parse<int>(\"42\"));}",
	         "import \"string.vs\"; void main(){fn(string,int64)->string f=std::strings::repeat; print(f(\"a\",1.0));}",
	         "import \"concurrent.vs\"; void main(){auto lock=std::concurrent::make_mutex();}",
	         "import \"concurrent.vs\"; int answer(){return 42;} void main(){auto t=std::concurrent::spawn_task<int>(answer); print(t.await());}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitCSharpFromSource(Source, "foundation-runtime.vs", Output, Error, {VEROSCRIPT_STDLIB_DIR}) || !Output.empty() || Error.find("standard-library foundation operations require") == std::string::npos)
		{
			std::cerr << "FAIL: runtime foundation must be rejected before standalone output: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		FModuleEnvironmentDesc D;
		D.EntryOwnerId = "app";
		D.Scopes = {{"app", {"library"}}, {"library", {}}};
		D.Modules = {
		    {"library.vs", "library.api", "veroscript-module:/library/api.vs", "library", "aot-example", std::string("export int answer(){return 42;}")},
		    {"main.vs", "app.main", "veroscript-module:/app/main.vs", "app", "aot-entry", std::string("import \"library.vs\"; int main(){return answer();}")}};
		std::string Error;
		FCodegenOptions O;
		O.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		O.ModuleEnvironment = FModuleEnvironment::Create(D, Error);
		FCSharpCodegenBackend Backend;
		FCodegenResult Result;
		if (!O.ModuleEnvironment ||
		    !Backend.EmitFromSource(
		        "import \"library.vs\"; int main(){return answer();}", "<environment-aot>", O, Result))
		{
			std::cerr << "module environment AOT failed: " << Error << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Backend.EmitFromFile("veroscript-module:/app/main.vs", O, Result))
		{
			std::cerr << "virtual module entry AOT failed: " << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		FCSharpAotSourceBundle Bundle;
		if (!EmitCSharpSourceBundleFromFile(
		        "veroscript-module:/app/main.vs", "Environment", O, Bundle, Error) ||
		    Bundle.Files.empty())
		{
			std::cerr << "virtual module entry bundle failed: " << Error << '\n';
			return EXIT_FAILURE;
		}
		FCodegenOptions Overlay;
		Overlay.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Overlay.SourceOverlays = {{"csharp-overlay-entry.vs", "int main(){return 13;}"}};
		if (!Backend.EmitFromFile("csharp-overlay-entry.vs", Overlay, Result))
		{
			std::cerr << "source overlay entry AOT failed: " << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		if (!EmitCSharpSourceBundleFromFile("csharp-overlay-entry.vs", "Overlay", Overlay, Bundle, Error))
		{
			std::cerr << "source overlay entry bundle failed: " << Error << '\n';
			return EXIT_FAILURE;
		}
		D.AvailableDependencies = {{"native", "0.1.1", "abi", "build", "content"}};
		D.RequiredDependencies = D.AvailableDependencies;
		O.ModuleEnvironment = FModuleEnvironment::Create(D, Error);
		if (!O.ModuleEnvironment ||
		    Backend.EmitFromSource("int main(){return 1;}", "<environment-aot>", O, Result) ||
		    Result.Error.find("native package contracts") == std::string::npos)
		{
			std::cerr << "native package contract was not rejected by standalone AOT: " << Error
			          << Result.Error << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const char* Source : {
	         "void main(){int value=1; auto f=[&copy=value]()->int{return ++copy;}; print(f());}",
	         "struct State{public:int value=1;}; int next(){static State value=State();return value.value;} int main(){return next();}",
	         "fn()->int next(){static int value=1;return []()->int{return ++value;};} int main(){return next()();}"})
	{
		std::string Output = "unchanged", Error;
		if (!Expect(!VeroScript::EmitCSharpFromSource(Source, "local-state-boundary.vs", Output, Error) && Output.empty() && Error.find("Core AOT") != std::string::npos,
		        "unsupported local state must fail before returning generated source"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const char* Source : {
	         "struct State { public: int n=1; }; void main(){ State s=State(); "
	         "fn()->int f=[s]()->int { s=State(); return s.n; }; print(f()); }",
	         "struct State { public: int n=1; }; void main(){ State s=State(); "
	         "fn()->int f=fn()->int { s=State(); return s.n; }; print(f()); }",
	         "struct State<T> { public: T n; }; void main(){ State<int> s=State<int>(); "
	         "fn()->int f=[s]()->int { s=State<int>(); return s.n; }; print(f()); }",
	         "struct State { public: int n=1; }; void replace(State& s){s=State();} "
	         "void main(){State s=State(); fn()->int f=[s]()->int {replace(&s); return s.n;}; print(f());}"})
	{
		std::string Output, Error;
		if (!Expect(!VeroScript::EmitCSharpFromSource(Source, "captured-struct.vs", Output, Error) && Error.find("const") != std::string::npos,
		        "captured struct bindings must not be silently rebound"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output, Error;
		if (!Expect(VeroScript::EmitCSharpFromSource(
		                "struct State { public: int n=1; }; void main(){ State s=State(); "
		                "fn()->int f=[s]()->int { State s=State(); s=State(); return s.n; }; print(f()); }",
		                "shadowed-captured-struct.vs", Output, Error),
		        "a shadowed local struct is not a captured binding"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		std::string Output, MemoryError;
		if (!Expect(
		        !VeroScript::EmitCSharpFromSource("void main(){std::memory::native_addresses_available();}",
		            "memory-aot.vs", Output, MemoryError) &&
		            MemoryError.find("std::memory requires") != std::string::npos,
		        "actual memory use is rejected by standalone AOT"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!VeroScript::EmitCSharpFromSource("void main(){auto b=new std::memory::buffer(4);}",
		                "memory-type-aot.vs", Output, MemoryError) &&
		            MemoryError.find("std::memory requires") != std::string::npos,
		        "memory types are rejected by standalone AOT"))
		{
			return EXIT_FAILURE;
		}
		for (const char* Source : {"void ignore(std::memory::readonly_view v) {} void main() {}",
		         "using V=std::memory::readonly_view; void ignore(V v) {} void main() {}",
		         "using namespace std::memory; void ignore(readonly_view v) {} void main() {}"})
		{
			if (!Expect(
			        !VeroScript::EmitCSharpFromSource(Source, "memory-signature.vs", Output, MemoryError) &&
			            MemoryError.find("std::memory requires") != std::string::npos,
			        "runtime-only signature is rejected"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const char* Source :
		    {"import \"memory.vs\"; void main(){print(1);}", "import \"core.vs\"; void main(){print(1);}"})
		{
			if (!Expect(VeroScript::EmitCSharpFromSource(
			                Source, "unused-memory.vs", Output, MemoryError, {VEROSCRIPT_STDLIB_DIR}),
			        "unused memory/core import remains supported"))
			{
				std::cerr << MemoryError << '\n';
				return EXIT_FAILURE;
			}
		}
		for (const char* Source :
		    {"import \"core.vs\"; void main(){print(std::sqrt(9.0));}",
		        "import \"core.vs\"; void main(){array<int> v={3,1,2}; std::algorithm::sort(v); print(v[0]);}",
		        "import \"core.vs\"; void main(){auto d=std::delegate<int>(); d.bind(fn()->int => 42); print(d.execute());}"})
		{
			if (!Expect(VeroScript::EmitCSharpFromSource(
			                Source, "used-core.vs", Output, MemoryError, {VEROSCRIPT_STDLIB_DIR}),
			        "public standard aliases retain their source implementations"))
			{
				std::cerr << MemoryError << '\n';
				return EXIT_FAILURE;
			}
		}
		for (const char* Source : {
		         "import \"core.vs\"; void main(){array<int> v={1}; print(v.capacity()); int pending=2; print(pending);}",
		         "import \"core.vs\"; void main(){print(\"task mutex make_channel capacity pending\");}",
		         "import \"core.vs\"; void main(){auto capacity=fn()->int => 3; print(capacity());}",
		         "import \"core.vs\"; void main(){int task=1; print(task);}",
		         "import \"core.vs\"; int task=1; void main(){print(task);}",
		         "import \"core.vs\"; int task=1; int after=task; void main(){print(after);}",
		         "import \"core.vs\"; void main(){fn(double)->double f=std::sqrt; print(f(9.0));}",
		         "import \"core.vs\"; double root<T>(T value){return std::sqrt(9.0);} void main(){print(1);}",
		         "import \"core.vs\"; class Box<T>{public: T payload; double value=std::sqrt(9.0);} void main(){print(1);}",
		         "import \"core.vs\"; class Box<T>{public: T payload; double value; Box():value(std::sqrt(9.0)) {}} void main(){print(1);}"})
		{
			if (!Expect(VeroScript::EmitCSharpFromSource(
			                Source, "core-symbol-dependencies.vs", Output, MemoryError, {VEROSCRIPT_STDLIB_DIR}),
			        "core dependency pruning distinguishes symbols from locals, members and strings"))
			{
				std::cerr << MemoryError << '\n';
				return EXIT_FAILURE;
			}
		}
		if (!Expect(!VeroScript::EmitCSharpFromSource(
		                "import \"core.vs\"; void main(){auto c=std::concurrent::make_channel<int>(1);}",
		                "used-core-concurrent.vs", Output, MemoryError, {VEROSCRIPT_STDLIB_DIR}) &&
		            MemoryError.find("channel and task lowering is not implemented") != std::string::npos,
		        "actual core concurrency use retains its actionable unsupported diagnostic"))
		{
			std::cerr << MemoryError << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::VeroScriptModuleLoader Loader("unused-core-ast.vs");
		Loader.AddStdlibSearchRoot(VEROSCRIPT_STDLIB_DIR);
		auto Program = Loader.LoadSourceModule("import \"core.vs\"; void main(){print(1);}", "unused-core-ast.vs");
		std::string Output, Error;
		if (!Expect(Program && VeroScript::EmitCSharpFromProgram(*Program, Output, Error),
		        "unused core import supports caller-owned AST without mutation"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		Program = Loader.LoadSourceModule(
		    "import \"core.vs\"; void main(){print(std::sqrt(9.0));}", "used-core-ast.vs");
		if (!Program)
		{
			return EXIT_FAILURE;
		}
		for (auto& Module : Program->PackageSourceModules)
		{
			if (Module.ModulePath == Program->EntryModulePath)
			{
				Module.Source = "import \"core.vs\"; void main(){print(1);}";
			}
		}
		if (!Expect(VeroScript::EmitCSharpFromProgram(*Program, Output, Error),
		        "dependency collection follows the checked AST even when retained source text differs"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		Program = Loader.LoadSourceModule(
		    "import \"core.vs\"; T g<T>(T v){print(std::sqrt(9.0));return v;} void main(){print(1);}",
		    "borrowed-core-ast.vs");
		if (!Program)
		{
			return EXIT_FAILURE;
		}
		bool bTestedBorrowedArguments = false;
		for (auto& Function : Program->Functions)
		{
			if (Function.Name != "g")
			{
				continue;
			}
			auto& Statement = static_cast<VeroScript::FExprStmt&>(*Function.Body->Statements.front());
			auto& Call = static_cast<VeroScript::FCallExpr&>(*Statement.Expression);
			auto Arguments = std::move(Call.Arguments);
			Call.BorrowArgumentsFrom(VeroScript::FExprArgumentView(nullptr, &Arguments));
			if (!Expect(VeroScript::EmitCSharpFromProgram(*Program, Output, Error),
			        "generic dependency collection follows borrowed argument views"))
			{
				std::cerr << Error << '\n';
				return EXIT_FAILURE;
			}
			bTestedBorrowedArguments = true;
		}
		if (!Expect(bTestedBorrowedArguments, "borrowed argument fixture exercises the generic body"))
		{
			return EXIT_FAILURE;
		}
	}

	const std::string Source =
	    "enum class Mode : int { First = 1, Second = 2 }\n"
	    "T id<T>(T value) { return value; }\n"
	    "int sum(int n) {\n"
	    "    int total = 0;\n"
	    "    for (int i = 0; i <= n; i++) total += i;\n"
	    "    return total;\n"
	    "}\n"
	    "void main(string[] args) {\n"
	    "    string text = \"你好，世界\";\n"
	    "    pair<int,string> pair = pair<int,string>(); pair.first = 7;\n"
	    "    print(text);\n"
	    "    print(id<int>(sum(len(args))));\n"
	    "    print(std::chrono::now() > 0.0);\n"
	    "    print(std::chrono::steady_now() > 0.0);\n"
	    "    std::console::write(\"x\"); std::console::flush();\n"
	    "    print(std::console::is_interactive());\n"
	    "    print(std::console::supports_color()); print(std::console::width());\n"
	    "    std::console::set_color(\"red\", \"default\"); std::console::move_cursor(1, 2);\n"
	    "}\n";
	std::string CSharp;
	std::string Error;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                Source, "csharp_emit_test.vs", CSharp, Error),
	        "emit checked C# source"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	for (const char* Fragment : {
	         "Generated by VeroScriptCSharpAot " VEROSCRIPT_CSHARP_AOT_TEST_VERSION,
	         "private enum S4_Mode : int",
	         "private struct VsPair<TFirst, TSecond>",
	         "VsPair<int, VsString> pair = new VsPair<int, VsString>(default!, new VsString(string.Empty))",
	         "private static T Vs_S2_id_M_",
	         "DynamicallyAccessedMemberTypes.PublicParameterlessConstructor",
	         "private static int Vs_S3_sum_M_",
	         "// VeroScript source: csharp_emit_test.vs:2",
	         "#line 2",
	         "\"你好，世界\"",
	         "System.DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()",
	         "System.Diagnostics.Stopwatch.GetTimestamp()",
	         "VsConsoleWrite(false, VsText(new VsString(\"x\")))",
	         "VsConsoleIsInteractive()",
	         "VsConsoleSupportsColor()",
	         "VsConsoleWidth()",
	         "VsConsoleSetColor(VsText(new VsString(\"red\")), VsText(new VsString(\"default\")))",
	         "VsConsoleMoveCursor(VsToInt64(1), VsToInt64(2))",
	         "public static int Main(string[] args)"})
	{
		if (!Expect(CSharp.find(Fragment) != std::string::npos, Fragment))
		{
			std::cerr << CSharp;
			return EXIT_FAILURE;
		}
	}

	std::string CoexistCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "class handle { public: int value; handle(int n) { value = n + 1; } } "
	                "void main() { auto local = handle(2); assert(local.value == 3); "
	                "shared<int> owner = shared<int>(7); handle<int> alias = owner; "
	                "assert(alias == owner && owner.value == 7); }",
	                "shared-coexist.vs", CoexistCSharp, Error),
	        "emit non-generic handle beside builtin handle<T>") ||
	    !Expect(CoexistCSharp.find("S6_handle local = new S6_handle(2)") != std::string::npos,
	        "bare handle constructor uses the user type") ||
	    !Expect(CoexistCSharp.find("VsShared<int> owner = new VsShared<int>(7)") != std::string::npos,
	        "explicit handle<T> constructor uses the builtin wrapper"))
	{
		std::cerr << Error << '\n'
		          << CoexistCSharp;
		return EXIT_FAILURE;
	}

	std::string EscapedNewlineCSharp;
	{
		std::string PairCSharp;
		if (!Expect(VeroScript::EmitCSharpFromSource(
		                "class pair { public: int value=9; } void main(){pair p=pair();pair<int,int> q=pair<int,int>();print(p.value);print(q.first);}",
		                "pair-coexist.vs", PairCSharp, Error),
		        "emit non-generic pair beside the builtin template") ||
		    !Expect(PairCSharp.find("S4_pair p = new S4_pair()") != std::string::npos, "bare pair uses the declared user class"))
		{
			std::cerr << Error << '\n'
			          << PairCSharp;
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::EmitCSharpFromSource(
		                "struct pair<T,U> { T custom; U extra; } void main(){pair<int,string> p=pair<int,string>();p.custom=7;p.extra=\"user\";print(p.custom);}",
		                "pair-shadow.vs", PairCSharp, Error),
		        "emit user pair template") ||
		    !Expect(PairCSharp.find("S4_pair<int, VsString> p") != std::string::npos, "user pair template takes precedence over the soft builtin"))
		{
			std::cerr << Error << '\n'
			          << PairCSharp;
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::EmitCSharpFromSource(
		                "struct pair<T> { T custom; } void main(){pair<int> local=pair<int>();"
		                "local.custom=9;pair<int,string> entry=pair<int,string>();"
		                "print(local.custom);print(entry.first);print(entry.second);}",
		                "pair-arity-coexist.vs", PairCSharp, Error),
		        "emit user pair<T> beside builtin pair<T,U>") ||
		    !Expect(PairCSharp.find("S4_pair<int> local = new S4_pair<int>()") != std::string::npos,
		        "single-parameter pair uses the declared user type") ||
		    !Expect(PairCSharp.find("VsPair<int, VsString> entry = new VsPair<int, VsString>(default!, new VsString(string.Empty))") != std::string::npos,
		        "two-parameter pair retains builtin field defaults beside a user template"))
		{
			std::cerr << Error << '\n'
			          << PairCSharp;
			return EXIT_FAILURE;
		}
	}
	const std::string EscapedNewlineLabel = std::string("line") + "\xE2\x80\xA8" + "label.vs";
	for (const char* PairSource : {
	         "struct pair<T,U>{T first;U second;}void main(){ordered_map<string,int> m={\"a\":3};auto entries=m.entries();print(entries[0].second);}",
	         "struct pair<T,U>{T first;U second;}void main(){ordered_map<string,int> m={\"a\":3};for(pair<string,int> entry:m){print(entry.second);}}"})
	{
		std::string PairCSharp;
		if (!Expect(!VeroScript::EmitCSharpFromSource(PairSource, "pair-shadow-entries.vs", PairCSharp, Error) && Error.find("map entries with a user pair<T,U> template require the VM backend") != std::string::npos,
		        "overridden pair map entries have an explicit AOT boundary"))
		{
			std::cerr << Error << '\n'
			          << PairCSharp;
			return EXIT_FAILURE;
		}
	}
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "void main() { string text = \"\\u0085\\u2028\\u2029\"; print(text); }",
	                EscapedNewlineLabel,
	                EscapedNewlineCSharp,
	                Error),
	        "escape C# newline scalars in generated source") ||
	    !Expect(EscapedNewlineCSharp.find(
	                "new VsString(\"\\u0085\\u2028\\u2029\")") != std::string::npos,
	        "C# newline scalars are escaped inside string literals") ||
	    !Expect(EscapedNewlineCSharp.find(
	                "// VeroScript source: line\\u2028label.vs:1") != std::string::npos,
	        "C# newline scalars are escaped inside source comments"))
	{
		std::cerr << Error << '\n'
		          << EscapedNewlineCSharp;
		return EXIT_FAILURE;
	}

	std::string ConstructorInitCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "struct Payload { int value; Payload(int input) { value = input; } } "
	                "class Holder { public: const int id; const Payload payload; "
	                "Holder(int input) : payload(input + 1), id(input) {} } "
	                "struct GenericFieldBox<T> { const T value; "
	                "GenericFieldBox(T input) : value(input) {} } "
	                "void main() { Holder value = Holder(4); "
	                "GenericFieldBox<int> generic = GenericFieldBox<int>(7); "
	                "print(value.id + generic.value); }",
	                "constructor-field-init.vs", ConstructorInitCSharp, Error),
	        "emit C# constructor field initializers") ||
	    !Expect(ConstructorInitCSharp.find("this.id =") != std::string::npos,
	        "C# constructor initializes scalar const field") ||
	    !Expect(ConstructorInitCSharp.find("this.payload =") != std::string::npos,
	        "C# constructor initializes user-type const field") ||
	    !Expect(ConstructorInitCSharp.find(
	                "S15_GenericFieldBox<[DynamicallyAccessedMembers(") != std::string::npos,
	        "generic user types preserve VsDefault NativeAOT member requirements") ||
	    !Expect(ConstructorInitCSharp.find("this.value = input;") != std::string::npos,
	        "C# constructor directly initializes an open generic field"))
	{
		std::cerr << Error << '\n'
		          << ConstructorInitCSharp;
		return EXIT_FAILURE;
	}

	std::string RecordOperatorNameCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "struct OperatorNames<op_Equality> { op_Equality value; "
	                "static bool op_Equality(OperatorNames<op_Equality> left, "
	                "OperatorNames<op_Equality> right) { return true; } "
	                "static bool op_Inequality(OperatorNames<op_Equality> left, "
	                "OperatorNames<op_Equality> right) { return false; } } "
	                "void main() { OperatorNames<int> value = OperatorNames<int>(); "
	                "assert(value.value == 0); }",
	                "record-operator-names.vs", RecordOperatorNameCSharp, Error),
	        "record equality metadata names are emitted without collisions") ||
	    !Expect(RecordOperatorNameCSharp.find("op_Equality__vs_type_") != std::string::npos, "record operator metadata cannot collide with type parameters") || !Expect(RecordOperatorNameCSharp.find("op_Equality__vs_member_") != std::string::npos, "record equality metadata cannot collide with source methods") || !Expect(RecordOperatorNameCSharp.find("op_Inequality__vs_member_") != std::string::npos, "record inequality metadata cannot collide with source methods"))
	{
		std::cerr << Error << '\n'
		          << RecordOperatorNameCSharp;
		return EXIT_FAILURE;
	}

	std::string GenericRefDelegateCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "T invoke<T>(fn(ref T)->T callback, ref T value) { "
	                "return callback(ref value); } void main() {}",
	                "generic-ref-delegate.vs", GenericRefDelegateCSharp, Error),
	        "generic ref delegate lowering succeeds") ||
	    !Expect(GenericRefDelegateCSharp.find(
	                "private delegate __VsType0 VsFn_") != std::string::npos,
	        "custom delegate closes over its generic return type") ||
	    !Expect(GenericRefDelegateCSharp.find(
	                "_GS1_T<__VsType0>(ref __VsType0 __arg0)") != std::string::npos,
	        "custom delegate closes over its generic ref parameter"))
	{
		std::cerr << Error << '\n'
		          << GenericRefDelegateCSharp;
		return EXIT_FAILURE;
	}

	std::string UnsupportedGenericFieldInit;
	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "struct GenericBuiltField<T> { const T value; "
	                "GenericBuiltField(int left, int right) : value(left, right) {} } "
	                "void main() {}",
	                "unsupported-generic-field-init.vs", UnsupportedGenericFieldInit, Error),
	        "reject unsupported C# open generic field construction") ||
	    !Expect(Error.find("requires exactly one argument of the same type") != std::string::npos, "diagnose unsupported C# open generic field construction"))
	{
		std::cerr << Error << '\n'
		          << UnsupportedGenericFieldInit;
		return EXIT_FAILURE;
	}

	std::string ExceptionCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "int flow() { int result = 0; try { try { throw 4; } "
	                "catch (int& value) { value = 5; throw; } } "
	                "catch (const int& value) { result = value; } "
	                "finally { result += 1; } return result; } "
	                "void main() { print(flow()); }",
	                "exception-lowering.vs", ExceptionCSharp, Error),
	        "emit C# script-value exception lowering") ||
	    !Expect(ExceptionCSharp.find("private sealed class VsExceptionCarrier") != std::string::npos, "script exception carrier is generated") || !Expect(ExceptionCSharp.find(".Rebind(value);") != std::string::npos, "reference catch rebinds before rethrow") || !Expect(ExceptionCSharp.find("finally") != std::string::npos, "finally block is generated"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	std::string PrintfCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "void main() { print(formatf(\"x=%#08X\", 255U)); "
	                "array<string> args = {\"7\"}; printf(\"v=%d\", args); }",
	                "printf-lowering.vs", PrintfCSharp, Error),
	        "emit C# printf-style formatting lowering") ||
	    !Expect(PrintfCSharp.find("VsFormatPrintf(") != std::string::npos,
	        "formatf uses printf formatter") ||
	    !Expect(PrintfCSharp.find("VsPrintfFormat(") != std::string::npos,
	        "printf uses printing formatter") ||
	    !Expect(PrintfCSharp.find("private static VsString VsFormatPrintfCore") != std::string::npos, "printf runtime helper is generated"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	VeroScript::FCSharpCodegenBackend Backend;
	VeroScript::FCodegenResult Result;
	if (!Expect(Backend.GetName() == "csharp" && Backend.GetFileExtension() == ".cs",
	        "backend identity") ||
	    !Expect(Backend.EmitFromSource(Source, "backend.vs", {}, Result) && Result.bSuccess && !Result.Output.empty(), "backend adapter"))
	{
		std::cerr << Result.Error << '\n';
		return EXIT_FAILURE;
	}

	VeroScript::FHostBindingRegistry Bindings;
	VeroScript::FHostTypeId CounterId;
	VeroScript::FHostTypeId MathId;
	std::string BindingError;
	if (!Expect(Bindings.RegisterType("Counter", VeroScript::EHostTypeKind::Class,
	                CounterId, BindingError),
	        "register AOT Counter host type") ||
	    !Expect(Bindings.RegisterType("HostMath", VeroScript::EHostTypeKind::Class,
	                MathId, BindingError),
	        "register AOT HostMath host type"))
	{
		std::cerr << BindingError << '\n';
		return EXIT_FAILURE;
	}
	const auto RegisterMember = [&](const VeroScript::FHostTypeId TypeId,
	                                VeroScript::FHostMemberDesc Member)
	{
		Member.Callback = VeroScript::BindingCatalogStubNative;
		return Bindings.RegisterMember(TypeId, std::move(Member), BindingError);
	};
	VeroScript::FHostMemberDesc Constructor;
	Constructor.Kind = VeroScript::EHostMemberKind::Constructor;
	Constructor.ReturnType = VeroScript::ENativeType::HostObject;
	Constructor.ParamTypes = {VeroScript::ENativeType::Int};
	VeroScript::FHostMemberDesc RefConstructor = Constructor;
	RefConstructor.ParamFlags = {VeroScript::NativeParamFlagRef};
	VeroScript::FHostMemberDesc Getter;
	Getter.Kind = VeroScript::EHostMemberKind::PropertyGetter;
	Getter.Name = "value";
	Getter.ReturnType = VeroScript::ENativeType::Int;
	Getter.NativeName = "Demo::Counter::Value.get()";
	VeroScript::FHostMemberDesc Setter;
	Setter.Kind = VeroScript::EHostMemberKind::PropertySetter;
	Setter.Name = "value";
	Setter.ParamTypes = {VeroScript::ENativeType::Int};
	Setter.NativeName = "Demo::Counter::Value.set()";
	VeroScript::FHostMemberDesc Add;
	Add.Kind = VeroScript::EHostMemberKind::Method;
	Add.Name = "add";
	Add.ReturnType = VeroScript::ENativeType::Int;
	Add.ParamTypes = {VeroScript::ENativeType::Int};
	Add.NativeName = "Demo::Counter::Add(System.Int32)";
	VeroScript::FHostMemberDesc Bump;
	Bump.Kind = VeroScript::EHostMemberKind::Method;
	Bump.Name = "bump";
	Bump.ReturnType = VeroScript::ENativeType::Void;
	Bump.ParamTypes = {VeroScript::ENativeType::Int};
	Bump.ParamFlags = {VeroScript::NativeParamFlagRef};
	Bump.NativeName = "Demo::Counter::Bump(System.Int32&)";
	VeroScript::FHostMemberDesc Get;
	Get.Kind = VeroScript::EHostMemberKind::StaticMethod;
	Get.Name = "get";
	Get.ReturnType = VeroScript::ENativeType::HostObject;
	Get.ReturnHostTypeName = "Counter";
	Get.NativeName = "Demo::Counter::Get()";
	VeroScript::FHostMemberDesc Twice;
	Twice.Kind = VeroScript::EHostMemberKind::StaticMethod;
	Twice.Name = "twice";
	Twice.ReturnType = VeroScript::ENativeType::Int;
	Twice.ParamTypes = {VeroScript::ENativeType::Int};
	Twice.NativeName = "Demo::HostMath::Twice(System.Int32)";
	if (!Expect(RegisterMember(CounterId, Constructor) && RegisterMember(CounterId, RefConstructor) && RegisterMember(CounterId, Getter) && RegisterMember(CounterId, Setter) && RegisterMember(CounterId, Add) && RegisterMember(CounterId, Bump) && RegisterMember(CounterId, Get) && RegisterMember(MathId, Twice), "register AOT host members"))
	{
		std::cerr << BindingError << '\n';
		return EXIT_FAILURE;
	}
	VeroScript::FCodegenOptions BindingOptions;
	BindingOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	BindingOptions.Bindings = &Bindings;
	VeroScript::FCodegenResult BindingResult;
	const std::string BindingSource =
	    "void main() { Counter counter = Counter(4); int seed = 5; Counter other = Counter(ref seed); "
	    "counter.value += 2; print(counter.add(3)); "
	    "print(HostMath.twice(counter.value)); }";
	if (!Expect(Backend.EmitFromSource(BindingSource, "host-bindings.vs",
	                BindingOptions, BindingResult) &&
	            BindingResult.bSuccess,
	        "C# AOT host binding lowering") ||
	    !Expect(BindingResult.Output.find("Host-call contracts") != std::string::npos,
	        "host contracts are emitted") ||
	    !Expect(BindingResult.Output.find("private static partial int VsHost_") != std::string::npos, "typed host partial methods are emitted") || !Expect(BindingResult.Output.find("ref seed") != std::string::npos, "ref constructor overload is selected and emitted") || !Expect(BindingResult.Output.find("host binding catalog lowering is not implemented") == std::string::npos, "legacy blanket rejection is removed"))
	{
		std::cerr << BindingResult.Error << '\n';
		return EXIT_FAILURE;
	}

	{
		VeroScript::FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "app";
		Description.Scopes = {{"app", {}}};
		Description.Bindings = std::make_shared<VeroScript::FHostBindingRegistry>(Bindings);
		const std::string Label = "veroscript-module:/app/host-bindings.vs";
		Description.Modules = {{"host-bindings.vs", "app.bindings", Label, "app", "bindings.1", BindingSource}};
		VeroScript::FCodegenOptions EnvironmentOptions;
		EnvironmentOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		EnvironmentOptions.ModuleEnvironment = VeroScript::FModuleEnvironment::Create(Description, Error);
		VeroScript::VeroScriptModuleLoader Loader(Label, EnvironmentOptions.ModuleEnvironment);
		Loader.SetHostTypeNames({"Counter", "HostMath"});
		auto Program = Loader.LoadSourceModule(BindingSource, Label);
		VeroScript::FCSharpAotSourceBundle EnvironmentBundle, ExplicitBundle;
		VeroScript::FCodegenOptions ExplicitOptions;
		ExplicitOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		ExplicitOptions.Bindings = Description.Bindings.get();
		if (!Expect(EnvironmentOptions.ModuleEnvironment && Program && !Loader.HasError(),
		        "load caller-owned AST with the environment catalog") ||
		    !Expect(VeroScript::EmitCSharpSourceBundleFromProgram(*Program, "Bindings", ExplicitOptions, ExplicitBundle, Error),
		        "caller-owned AST emission accepts an explicit environment binding catalog") ||
		    !Expect(VeroScript::EmitCSharpSourceBundleFromProgram(*Program, "Bindings", EnvironmentOptions, EnvironmentBundle, Error),
		        "caller-owned AST emission inherits the module environment binding catalog"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(EnvironmentBundle.Files.size() == ExplicitBundle.Files.size() &&
		            EnvironmentBundle.ManifestJson == ExplicitBundle.ManifestJson,
		        "environment and explicit binding catalogs produce the same bundle"))
		{
			return EXIT_FAILURE;
		}

		VeroScript::FCSharpAotSourceBundle SourceBundle, FileBundle;
		if (!Expect(VeroScript::EmitCSharpSourceBundleFromSource(
		                BindingSource, Label, "Bindings", EnvironmentOptions, SourceBundle, Error) &&
		            VeroScript::EmitCSharpSourceBundleFromFile(
		                Label, "Bindings", EnvironmentOptions, FileBundle, Error) &&
		            SourceBundle.ManifestJson == ExplicitBundle.ManifestJson && FileBundle.ManifestJson == ExplicitBundle.ManifestJson && SourceBundle.Files.size() == ExplicitBundle.Files.size() && FileBundle.Files.size() == ExplicitBundle.Files.size(),
		        "source and virtual file entry points inherit the same environment catalog"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		for (std::size_t Index = 0; Index < ExplicitBundle.Files.size(); ++Index)
		{
			if (!Expect(
			        EnvironmentBundle.Files[Index].RelativePath == ExplicitBundle.Files[Index].RelativePath &&
			            EnvironmentBundle.Files[Index].Contents == ExplicitBundle.Files[Index].Contents &&
			            SourceBundle.Files[Index].RelativePath == ExplicitBundle.Files[Index].RelativePath &&
			            SourceBundle.Files[Index].Contents == ExplicitBundle.Files[Index].Contents &&
			            FileBundle.Files[Index].RelativePath == ExplicitBundle.Files[Index].RelativePath &&
			            FileBundle.Files[Index].Contents == ExplicitBundle.Files[Index].Contents,
			        "environment binding lowering preserves every compilation unit"))
			{
				return EXIT_FAILURE;
			}
		}
		if (!Expect(!EnvironmentOptions.Bindings && !EnvironmentOptions.CompileProcessing.ModuleEnvironment,
		        "environment normalization leaves caller options unchanged"))
		{
			return EXIT_FAILURE;
		}

		EnvironmentOptions.Bindings = &Bindings;
		if (!Expect(!VeroScript::EmitCSharpSourceBundleFromProgram(
		                *Program, "Bindings", EnvironmentOptions, EnvironmentBundle, Error) &&
		            Error.find("conflicting module environment or host binding catalog") != std::string::npos && EnvironmentBundle.Files.empty() && EnvironmentBundle.ManifestJson.empty(),
		        "caller-owned AST emission rejects conflicting catalogs and clears prior output"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	VeroScript::FCodegenResult InvalidRefResult;
	if (!Expect(!Backend.EmitFromSource(
	                "void main() { Counter counter = Counter(1); array<int> values = {2}; "
	                "counter.bump(ref values[0]); }",
	                "host-ref-index.vs", BindingOptions, InvalidRefResult),
	        "writable host ref List index is rejected before invalid C# is emitted") ||
	    !Expect(InvalidRefResult.Error.find("local or global variable") != std::string::npos,
	        "writable ref diagnostic is actionable"))
	{
		std::cerr << InvalidRefResult.Error << '\n';
		return EXIT_FAILURE;
	}

	VeroScript::FCodegenResult SideEffectHostResult;
	if (!Expect(!Backend.EmitFromSource(
	                "void main() { Counter.get().value += 1; }",
	                "host-property-side-effect.vs", BindingOptions, SideEffectHostResult),
	        "side-effecting host-property receiver is rejected instead of evaluated twice") ||
	    !Expect(SideEffectHostResult.Error.find("local or global receiver") != std::string::npos,
	        "host-property side-effect diagnostic is actionable"))
	{
		std::cerr << SideEffectHostResult.Error << '\n';
		return EXIT_FAILURE;
	}

	std::string RefFunctionCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "int read(const int& value) { return value; } void bump(int& value) { value++; } "
	                "void main() { array<int> values = {1}; print(read(ref values[0])); "
	                "int value = 2; bump(ref value); }",
	                "script-ref-lowering.vs", RefFunctionCSharp, Error),
	        "const and mutable script ref parameters lower to valid C# shapes") ||
	    !Expect(RefFunctionCSharp.find("in int") != std::string::npos,
	        "const script refs use C# in parameters") ||
	    !Expect(RefFunctionCSharp.find("ref value") != std::string::npos,
	        "mutable local refs preserve C# ref passing"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	std::string CollisionCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "export int collision_M_collision_2Evs(ref int value) { return 1; } "
	                "int collision(ref int value) { return 2; } "
	                "export int state_M_collision_2Evs = 1; int state = 2; "
	                "void main() { int value = 0; "
	                "assert(collision_M_collision_2Evs(ref value) == 1); "
	                "assert(collision(ref value) == 2); "
	                "assert(state_M_collision_2Evs == 1); assert(state == 2); }",
	                "collision.vs", CollisionCSharp, Error),
	        "generated C# identities cannot be forged by script symbols") ||
	    !Expect(CollisionCSharp.find(
	                "Vs_S9_collision_M_collision_2Evs_Ref_0") != std::string::npos,
	        "private function identity retains an isolated module component") ||
	    !Expect(CollisionCSharp.find(
	                "Vs_S26_collision_5FM_5Fcollision_5F2Evs_Ref_0") != std::string::npos,
	        "exported function identity escapes suffix-like source text") ||
	    !Expect(CollisionCSharp.find(
	                "VsGlobal_S5_state_M_collision_2Evs") != std::string::npos,
	        "private global identity retains an isolated module component") ||
	    !Expect(CollisionCSharp.find(
	                "VsGlobal_S22_state_5FM_5Fcollision_5F2Evs") != std::string::npos,
	        "exported global identity escapes suffix-like source text"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	std::string Unsupported;
	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "int broken() { return \"not an int\"; } void main() {}",
	                "semantic-error.vs", Unsupported, Error),
	        "compiler semantic error is rejected") ||
	    !Expect(!Error.empty(), "compiler semantic diagnostic is preserved"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "class Box { public: int value = 1; Box(int initial) { value = initial; } "
	                "int add(int amount) { value += amount; return value; } } "
	                "void main() { Box box(2); assert(box.add(3) == 5); }",
	                "objects.vs", Unsupported, Error),
	        "class lowering succeeds") ||
	    !Expect(Unsupported.find("class S3_Box") != std::string::npos,
	        "class declaration is emitted") ||
	    !Expect(Unsupported.find("int add(") != std::string::npos,
	        "class method is emitted"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	std::string ReceiverCaptureCSharp;
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "class LambdaBase { public: int value; LambdaBase(int v) { value = v; } "
	                "int read() { return value; } } "
	                "class LambdaCounter : public LambdaBase { public: "
	                "LambdaCounter(int v) : LambdaBase(v) {} "
	                "fn()->int getter() { return fn()->int => value; } "
	                "fn()->int explicit_getter() { return fn()->int => this.value; } "
	                "fn()->int method_getter() { return fn()->int => read(); } "
	                "fn()->int mixed(int amount) { int local = amount; "
	                "return fn()->int => value + local; } "
	                "fn(int)->int collision_getter() { "
	                "return fn(int __vs_this)->int => this.value + __vs_this; } "
	                "fn()->fn()->int nested_getter() { "
	                "return fn()->fn()->int => fn()->int => value; } } "
	                "struct LambdaSnapshot { int value; "
	                "fn()->int getter() { return fn()->int => this.value; } } "
	                "class LambdaBox<T> { public: T value; LambdaBox(T v) { value = v; } "
	                "fn()->T getter() { return fn()->T => value; } } "
	                "class LambdaStatics { public: static int value = 7; "
	                "static fn()->int getter() { return fn()->int => LambdaStatics.value; } } "
	                "void main() {}",
	                "receiver-captures.vs", ReceiverCaptureCSharp, Error),
	        "class and struct receiver lambda lowering succeeds") ||
	    !Expect(CountOccurrences(ReceiverCaptureCSharp,
	                "VsCapture(this, static __capture") >= 5,
	        "instance lambdas capture the receiver explicitly") ||
	    !Expect(ReceiverCaptureCSharp.find("return __capture.value;") != std::string::npos, "captured receiver fields remain live") || !Expect(ReceiverCaptureCSharp.find("return __capture.read();") != std::string::npos, "captured receiver methods use the retained receiver") || !Expect(ReceiverCaptureCSharp.find("VsCapture((local, this), static __capture") != std::string::npos, "local values and this use one stable capture tuple") || !Expect(ReceiverCaptureCSharp.find("__capture.Item2.value") != std::string::npos, "tuple receiver fields use the receiver component") || !Expect(ReceiverCaptureCSharp.find("__capture.Item1") != std::string::npos, "tuple local values use their own component") || !Expect(ReceiverCaptureCSharp.find("__capture.value + __vs_this") != std::string::npos, "receiver capture names cannot shadow lambda parameters") || !Expect(ReceiverCaptureCSharp.find("VsCapture(__capture") != std::string::npos, "nested lambdas retain the already captured receiver") || !Expect(CountOccurrences(ReceiverCaptureCSharp, "VsCapture(") == 9, "static fields are not mistaken for local value captures"))
	{
		std::cerr << Error << '\n'
		          << ReceiverCaptureCSharp;
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class GenericCaptureless<T> { public: T value; "
	                "fn()->T getter() { return []()->T => value; } } void main() {}",
	                "generic-captureless-this.vs", Unsupported, Error),
	        "unused generic captureless lambda cannot bypass this-capture validation") ||
	    !Expect(Error.find("captureless lambda cannot capture this") != std::string::npos,
	        "generic captureless receiver diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "interface Readable { virtual int read() = 0; } void main() {}",
	                "unsupported-interface.vs", Unsupported, Error),
	        "unsupported interface lowering is rejected") ||
	    !Expect(Error.find("interface lowering") != std::string::npos,
	        "unsupported interface diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class Base {} class Hidden : Base {} void main() {}",
	                "unsupported-private-inheritance.vs", Unsupported, Error),
	        "non-public class inheritance is rejected") ||
	    !Expect(Error.find("private class inheritance") != std::string::npos,
	        "non-public inheritance diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class RefShape { public: int read(ref int value) { return value; } "
	                "int read(const int& value) { return value; } } void main() {}",
	                "unsupported-member-ref-overload.vs", Unsupported, Error),
	        "member ref-direction-only overloads are rejected") ||
	    !Expect(Error.find("const/ref direction") != std::string::npos,
	        "member ref overload diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "void main() { multimap<string, int> values = multimap<string, int>(); "
	                "multiset<int> bag = {2, 2}; priority_queue<int> queue = {3, 1}; }",
	                "supported-containers.vs", Unsupported, Error),
	        "new core containers lower to C#") ||
	    !Expect(Unsupported.find("VsMultiMap<") != std::string::npos && Unsupported.find("VsMultiSet<") != std::string::npos && Unsupported.find("VsPriorityQueue<") != std::string::npos,
	        "new core container runtime types are emitted"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "void main() { channel<int> values = channel<int>(); }",
	                "unsupported-channel.vs", Unsupported, Error),
	        "unsupported channel lowering is rejected") ||
	    !Expect(Error.find("channel and task lowering is not implemented in C# AOT") != std::string::npos,
	        "unsupported channel diagnostic selects the managed VM host"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "import \"concurrent.vs\"; void main() {auto ch=std::concurrent::make_channel<int>();}",
	                "unsupported-concurrent-stdlib.vs", Unsupported, Error),
	        "unsupported concurrency standard library lowering is rejected") ||
	    !Expect(Error.find("channel and task lowering is not implemented") != std::string::npos,
	        "concurrency standard library diagnostic selects the managed VM host"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class Reflected { public: int value; } "
	                "void inspect(field_info<Reflected> field) {} void main() {}",
	                "unsupported-reflection-descriptor-type.vs", Unsupported, Error),
	        "reflection descriptor type in a signature is rejected") ||
	    !Expect(Error.find("reflection descriptors are not supported by C# AOT") != std::string::npos,
	        "reflection descriptor type diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class Reflected { public: int value; } "
	                "void main() { Reflected object = Reflected(); "
	                "field_info<Reflected> field = field_info<Reflected>(\"Reflected\", 0); "
	                "print(field.get<int>(object)); }",
	                "unsupported-executable-reflection-field.vs", Unsupported, Error),
	        "executable reflection field access is rejected") ||
	    !Expect(Error.find("reflection descriptors are not supported by C# AOT") != std::string::npos,
	        "executable reflection field diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class Reflected { public: int value() { return 7; } } "
	                "void main() { Reflected object = Reflected(); "
	                "method_info<Reflected> method = method_info<Reflected>(\"Reflected\", 0); "
	                "print(method.invoke<int>(object)); }",
	                "unsupported-executable-reflection-method.vs", Unsupported, Error),
	        "executable reflection method invocation is rejected") ||
	    !Expect(Error.find("reflection descriptors are not supported by C# AOT") != std::string::npos,
	        "executable reflection method diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class Reflected { public: int value; } "
	                "void read(field_info<Reflected> field, Reflected object) { "
	                "print(field.get<int>(object)); } void main() {}",
	                "unsupported-executable-reflection-field-parameter.vs", Unsupported, Error),
	        "executable reflection field access through a parameter is rejected") ||
	    !Expect(Error.find("reflection descriptors are not supported by C# AOT") != std::string::npos,
	        "executable reflection field call diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class Reflected { public: int value() { return 7; } } "
	                "void invoke(method_info<Reflected> method, Reflected object) { "
	                "print(method.invoke<int>(object)); } void main() {}",
	                "unsupported-executable-reflection-method-parameter.vs", Unsupported, Error),
	        "executable reflection method call through a parameter is rejected") ||
	    !Expect(Error.find("reflection descriptors are not supported by C# AOT") != std::string::npos,
	        "executable reflection method call diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "void main() { int values[2] = {1, 2}; }",
	                "unsupported-native-array.vs", Unsupported, Error),
	        "fixed-size native array lowering is rejected") ||
	    !Expect(Error.find("fixed-size native arrays") != std::string::npos,
	        "fixed-size native array diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "int value = 1; int& value_ref() { return value; } void main() {}",
	                "ref-return.vs", Unsupported, Error),
	        "reference return lowering succeeds") ||
	    !Expect(Unsupported.find("private static ref int") != std::string::npos,
	        "reference return signature is emitted") ||
	    !Expect(Unsupported.find("return ref") != std::string::npos,
	        "reference return body preserves aliasing"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "fn()->int retain_ref(ref int value) { return [&]() -> int => value; } void main() {}",
	                "unsupported-ref-parameter-capture.vs", Unsupported, Error),
	        "escaping reference capture of a ref parameter is rejected") ||
	    !Expect(Error.find("escaping reference capture of ref parameter 'value'") != std::string::npos, "ref-parameter capture diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "fn()->int shadow_ref(ref int value) { return [&]()->int { "
	                "int value = 2; return value; }; } "
	                "fn()->fn(ref int)->int shadow_nested(ref int value) { "
	                "return [&]()->fn(ref int)->int => fn(ref int value)->int => value; } "
	                "void main() {}",
	                "shadowed-ref-captures.vs", Unsupported, Error),
	        "shadowed ref names are not mistaken for escaping captures"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "int next() { return 0; } void main() { array<int> a = {1}; a[next()] += 1; }",
	                "side-effect-target.vs", Unsupported, Error),
	        "compound target side effects are rejected instead of duplicated") ||
	    !Expect(Error.find("compound assignment target has side effects") != std::string::npos,
	        "compound target diagnostic is actionable"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	if (!Expect(!VeroScript::EmitCSharpFromSource(
	                "class F { public: void operator()(int& x) { x++; } }; void main() { F f = F(); int x = 1; f(x); }",
	                "reference-operator.vs", Unsupported, Error) &&
	            Error.find("reference operators") != std::string::npos,
	        "reference operator source emission is rejected explicitly"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "class F { public: void operator()(int& x) = delete; }; void main() { print(1); }",
	                "deleted-reference-operator.vs", Unsupported, Error),
	        "unused deleted reference operator does not block source emission"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(VeroScript::EmitCSharpFromSource(
	                "void operator1(int& n, int delta = 4) { n += delta; } "
	                "class F { public: static void operator_helper(int& n) { n++; } }; "
	                "void main() { int n = 2; F::operator_helper(ref n); operator1(ref n); print(n); }",
	                "ordinary-operator-prefix.vs", Unsupported, Error),
	        "ordinary names with operator prefix retain reference parameter support"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}

	for (const char* SharedDefaultSource : {
	         "struct Point { int x = 3; } void main() { shared<Point> p = shared<Point>(); }",
	         "shared<T> make_shared<T>() { return shared<T>(); } void main() { shared<int> p = make_shared<int>(); }"})
	{
		if (!Expect(!VeroScript::EmitCSharpFromSource(SharedDefaultSource, "unsupported-shared-default.vs", Unsupported, Error) && Error.find("supply an explicit payload") != std::string::npos,
		        "shared defaults that C# cannot preserve are rejected explicitly"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptCSharpAotEmitTests passed.\n";
	return EXIT_SUCCESS;
}
