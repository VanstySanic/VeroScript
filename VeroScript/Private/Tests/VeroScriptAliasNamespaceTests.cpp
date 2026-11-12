// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptVm.h"

#include "VeroScriptTestTemporaryDirectory.h"

#include "VeroScriptAst.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(const std::string& Source)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "alias_ns.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			std::cerr << "lex error\n";
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "alias_ns.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			std::cerr << "parse: " << Parser.GetError().Message << '\n';
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
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

	bool CompileFails(const std::string& Source, const char* Label, const char* Needle = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "alias_ns.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			if (Needle != nullptr
				&& Lexer.GetErrorMessage().find(Needle) == std::string::npos)
			{
				std::cerr << "lex error: " << Lexer.GetErrorMessage() << '\n';
				return Expect(false, Label);
			}
			return Expect(true, Label);
		}
		VeroScript::VeroScriptParser Parser(Tokens, "alias_ns.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			if (Needle != nullptr
				&& Parser.GetError().Message.find(Needle) == std::string::npos)
			{
				std::cerr << "parse error: " << Parser.GetError().Message << '\n';
				return Expect(false, Label);
			}
			return Expect(true, Label);
		}
		VeroScript::VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Expect(!Module || Compiler.HasError(), Label))
		{
			return false;
		}
		if (Needle != nullptr
			&& !Expect(
				Compiler.GetError().Message.find(Needle) != std::string::npos,
				Label))
		{
			std::cerr << "compile error: " << Compiler.GetError().Message << '\n';
			return false;
		}
		return true;
	}
}

int main()
{
	int Failures = 0;
	{
		std::string Source = "namespace Deep{";
		std::string Path = "Deep";
		constexpr int Depth = 16;
		for (int Index = 0; Index < Depth; ++Index)
		{
			Source += "int X(){return 0;}namespace X{";
			Path += "::X";
		}
		Source += "int make(){return 42;}";
		Source.append(Depth + 1, '}');
		Source += "void main(){auto f=[callback=" + Path
			+ "::make]()->int{return callback();};assert(f()==42);print(" + Path + "::make());}";
		if (!RunSource(Source, "42\n", "deep namespace/function prefixes resolve without recursive type expansion"))
		{
			++Failures;
		}
	}
	if (!CompileFails("namespace N{fn()->int value=[]()->int{return 7;};"
		"namespace value{namespace Missing{int make(){return 42;}}}}"
		"void main(){print(N::value::Missing::make());}",
		"real callable value prefixes do not fall back through missing fields to a namespace"))
	{
		++Failures;
	}
	const VeroScript::Tests::FScopedTemporaryDirectory TemporaryDirectory(
		"VeroScript-alias-namespace-");
	if (!Expect(static_cast<bool>(TemporaryDirectory),
		"create unique alias/namespace test directory"))
	{
		return EXIT_FAILURE;
	}
	const std::filesystem::path& TemporaryRoot = TemporaryDirectory.Get();
	if (!Expect(
			VeroScript::StandardLibrary::IsNamespace("std::chrono")
				&& VeroScript::StandardLibrary::IsNamespace("std::io")
				&& VeroScript::StandardLibrary::IsNamespace("std::filesystem")
				&& VeroScript::StandardLibrary::IsNamespace("std::strings")
				&& VeroScript::StandardLibrary::IsNamespace("std::concurrent")
				&& VeroScript::StandardLibrary::IsNamespace("std::algorithm")
				&& VeroScript::StandardLibrary::IsNamespace("std::numeric")
				&& VeroScript::StandardLibrary::IsNamespace("std::random")
				&& VeroScript::StandardLibrary::IsNamespace("std::environment")
				&& VeroScript::StandardLibrary::IsNamespace("std::console")
				&& VeroScript::StandardLibrary::IsNamespace("std::process")
				&& VeroScript::StandardLibrary::ResolveTypeName("std::array") == "array"
				&& VeroScript::StandardLibrary::ResolveTypeName("shared") == "handle"
				&& VeroScript::StandardLibrary::ResolveTypeName("std::shared") == "handle"
				&& VeroScript::StandardLibrary::ResolveTypeName("std.shared") == "handle"
				&& VeroScript::StandardLibrary::ResolveTypeName("std::handle") == "handle"
				&& VeroScript::StandardLibrary::ResolveTypeName("Handle").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::print") == "print"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::io::read_file") == "__std_read_file"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.filesystem.exists") == "__std_exists"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::strings::contains") == "__std_contains"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.concurrent.spawn_task") == "__std_spawn_task"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.concurrent.make_channel") == "__std_make_channel"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.concurrent.make_mutex") == "__std_make_mutex"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.concurrent.make_semaphore") == "__std_make_semaphore"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.concurrent.make_synchronized") == "__std_make_synchronized"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::concurrent::when_all") == "__std_when_all"
				&& VeroScript::StandardLibrary::ResolveTypeName("std::concurrent::task_group") == "task_group"
				&& VeroScript::StandardLibrary::ResolveTypeName("std::concurrent::mutex") == "mutex"
				&& VeroScript::StandardLibrary::ResolveTypeName("std::concurrent::counting_semaphore") == "counting_semaphore"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::algorithm::sort") == "algorithm_sort"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::algorithm::shuffle") == "algorithm_shuffle"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.numeric.accumulate") == "numeric_accumulate"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::numeric::average") == "numeric_average"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.random.next_uint") == "random_next_uint"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.random.next_bounded_uint") == "random_next_bounded_uint"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::random::next_int") == "random_next_int"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::random::next_double_range") == "random_next_double_range"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::environment::getenv") == "getenv"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::console::write") == "console_write"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std.console.clear") == "console_clear"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::chrono::steady_now") == "steady_now"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::round") == "__std_round"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::strings::last_find") == "__std_last_find"
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::process::exit") == "exit"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("now") == "std::chrono::now"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("steady_now") == "std::chrono::steady_now"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("read_int") == "std::io::read_int"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("console_write") == "std::console::write"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("exit").empty()
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("sqrt") == "std::sqrt"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("read_file") == "std::io::read_file"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("contains") == "std::strings::contains"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("spawn_task") == "std::concurrent::spawn_task"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("algorithm_sort") == "std::algorithm::sort"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("algorithm_shuffle") == "std::algorithm::shuffle"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("numeric_accumulate") == "std::numeric::accumulate"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("random_next_uint") == "std::random::next_uint"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("random_next_bounded_uint") == "std::random::next_bounded_uint"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("random_next_int") == "std::random::next_int"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("random_next_double_range") == "std::random::next_double_range"
				&& VeroScript::StandardLibrary::RequiredNamespaceForFunction("print").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::contains").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::read_file").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::channel_send").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::now").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::sleep").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::getenv").empty()
				&& VeroScript::StandardLibrary::ResolveFunctionName("std::setenv").empty(),
			"standard namespace public spelling normalization"))
	{
		++Failures;
	}

	if (!CompileFails(
			"void main() { print(__sqrt(4.0)); }\n",
			"raw internal builtin rejected",
			"reserved for standard library implementation"))
	{
		++Failures;
	}
	if (!CompileFails(
			"void main() { int value = __read_int(); print(value); }\n",
			"raw input intrinsic rejected",
			"reserved for standard library implementation"))
	{
		++Failures;
	}
	for (const std::string& Source : {
			 "void main() { now(); }\n",
			"void main() { sleep(0); }\n",
			"void main() { getenv(\"NAME\"); }\n",
			"void main() { setenv(\"NAME\", \"VALUE\"); }\n",
			"void main() { read_token(); }\n",
			"void main() { read_string(); }\n",
			"void main() { read_int(); }\n",
			"void main() { read_double(); }\n",
			"void main() { read_bool(); }\n"})
	{
		if (!CompileFails(
				Source,
				"module-owned builtin requires its standard namespace",
				"function requires its standard namespace"))
		{
			++Failures;
		}
	}
	if (!RunSource(
			"void main() { std::process::exit(0); print(\"unreachable\"); }\n",
			"",
			"process exit alias preserves the existing exit semantics"))
	{
		++Failures;
	}
	for (const std::string& Source : {
			 "void main() { console_write(\"x\"); }\n",
			"void main() { console_error(\"x\"); }\n",
			"void main() { console_flush(); }\n",
			"void main() { console_clear(); }\n",
			"void main() { console_is_interactive(); }\n"})
	{
		if (!CompileFails(
				Source,
				"module-owned builtin requires its standard namespace",
				"function requires its standard namespace"))
		{
			++Failures;
		}
	}
	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(
			"void main() {\n"
			"    std::console::write(\"A\");\n"
			"    std::console::flush();\n"
			"    std::console::error(\"E\");\n"
			"    print(\"B\");\n"
			"    print(std::console::is_interactive());\n"
			"    print(std::console::clear());\n"
			"}\n");
		if (!Expect(static_cast<bool>(Module), "compile console standard functions"))
		{
			++Failures;
		}
		else
		{
			std::ostringstream Output;
			std::istringstream Input;
			std::ostringstream Errors;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			if (!Expect(
					Vm.Execute(*Module, VeroScript::ERunMode::Script)
						&& Output.str() == "AB\nfalse\nfalse\n"
						&& Errors.str() == "E",
					"console standard functions use host streams and safely degrade"))
			{
				++Failures;
			}
		}
	}
	for (const std::string& Source : {
			 "void main() { sqrt(4.0); }\n",
			"void main() { read_file(\"missing\"); }\n",
			"void main() { contains(\"abc\", \"a\"); }\n",
			"void main() { basename(\"a/b\"); }\n",
			"void main() { spawn_task<int>(\"work\"); }\n"})
	{
		if (!CompileFails(
				Source,
				"module standard function requires its namespace",
				"function requires its standard namespace"))
		{
			++Failures;
		}
	}
	if (!RunSource(
			"double now() { return 7.0; }\n"
			"int read_int() { return 11; }\n"
			"void main() { print(now()); print(read_int()); }\n",
			"7\n11\n",
			"user functions may shadow namespace-only standard names"))
	{
		++Failures;
	}
	if (!RunSource(
			"void main() {\n"
			"    std::array<int> values = {3, 4};\n"
			"    values.push(5);\n"
			"    std::map<string, int> scores = {\"ready\": 7};\n"
			"    std::optional<int> answer = std::optional<int>(42);\n"
			"    std::string label = std::format(\"sum={}\", values[0] + values[1] + values[2]);\n"
			"    std::print(label);\n"
			"    std::print(scores[\"ready\"]);\n"
			"    std::print(answer.get());\n"
			"    std::print(std::max(4, 9));\n"
			"    std::print(std::chrono::now() > 0.0);\n"
			"    std::environment::setenv(\"VERO_STD_NAMESPACE_TEST\", \"ready\");\n"
			"    std::print(std::environment::getenv(\"VERO_STD_NAMESPACE_TEST\"));\n"
			"}\n",
			"sum=12\n7\n42\n9\ntrue\nready\n",
			"qualified standard namespace types and functions"))
	{
		++Failures;
	}

	if (!RunSource(
			"using namespace std;\n"
			"void main() {\n"
			"    pair<int, int> value = pair<int, int>();\n"
			"    value.first = 8;\n"
			"    value.second = 13;\n"
			"    print(value.first + value.second);\n"
			"}\n",
			"21\n",
			"using standard namespace type aliases"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace std { void replacement() {} }\n"
			"void main() {}\n",
			"standard namespace is reserved",
			"namespace 'std' is reserved"))
	{
		++Failures;
	}
	if (!RunSource(
			"void main() {\n"
			"    pair<int, string> value = pair<int, string>(); value.first = 8; value.second = \"ready\";\n"
			"    std::pair<int, string> copy = value; print(copy.first); print(copy.second); print(copy); print(type<std::pair<int,string>>().field_count());\n"
			"    ordered_map<string, int> scores = {\"a\": 3};\n"
			"    for (pair<string, int>& entry : scores) { entry.second += 2; }\n"
			"    array<pair<string, int>> entries = scores.entries(); print(entries[0].second);\n"
			"}\n",
			"8\nready\npair#3:int6:string\n2\n5\n", "lowercase pair and std::pair share the map entry type"))
	{
		++Failures;
	}

	if (!CompileFails(
			"void main(){Pair<int, int> value = Pair<int, int>();}", "uppercase Pair is not a builtin alias"))
	{
		++Failures;
	}

	if (!RunSource(
			"struct pair<T, U> { T custom; U extra; }\n"
			"void main(){pair<int, string> p=pair<int, string>();p.custom=7;p.extra=\"user\";print(p.custom);print(p.extra);}",
			"7\nuser\n", "lowercase pair yields to a user template of the same arity"))
	{
		++Failures;
	}

	if (!RunSource(
			"class pair { public: int value=9; }\n"
			"void main(){pair p=pair();pair<int, int> q=pair<int, int>();q.first=4;print(p.value);print(q.first);}",
			"9\n4\n", "non-generic user pair coexists with the soft builtin template"))
	{
		++Failures;
	}

	if (!RunSource(
			"int main(string[] args) {\n"
			"    string value = std::environment::getenv(\"VERO_STD_NAMESPACE_TEST\");\n"
			"    return value == \"ready\" ? 0 : 1;\n"
			"}\n",
			"",
			"environment namespace works in argument-taking main"))
	{
		++Failures;
	}

	if (!RunSource(
			"struct Gauge { int value = 7; };\n"
			"namespace Visible { int inspect(Gauge self) { return self.value; } }\n"
			"namespace Hidden { int inspect(Gauge self) { return -1; } }\n"
			"using namespace Visible;\n"
			"void main() { Gauge gauge(); print(gauge.inspect()); }\n",
			"7\n",
			"extension calls respect using namespace visibility"))
	{
		++Failures;
	}

	if (!RunSource(
			"struct Counter {\n"
			"    int value = 1;\n"
			"    int score() const { return 100; }\n"
			"};\n"
			"int add(ref Counter self, int amount) {\n"
			"    self.value += amount;\n"
			"    return self.value;\n"
			"}\n"
			"int inspect(const Counter& self) { return self.value; }\n"
			"int score(Counter self) { return -1; }\n"
			"int route(ref Counter self) { return 11; }\n"
			"int route(Counter self) { return 22; }\n"
			"enum class Token { Ready = 1 };\n"
			"int enum_route(ref Token self) { return 33; }\n"
			"int enum_route(Token self) { return 44; }\n"
			"Counter make_counter() { return Counter(); }\n"
			"T choose<T>(T self, T other) { return other; }\n"
			"void main() {\n"
			"    Counter counter();\n"
			"    print(counter.add(4));\n"
			"    print(counter.inspect());\n"
			"    print(counter.score());\n"
			"    print(counter.route());\n"
			"    print(make_counter().route());\n"
			"    print(Token::Ready.enum_route());\n"
			"    int value = 3;\n"
			"    print(value.choose<int>(9));\n"
			"}\n",
			"5\n5\n100\n11\n22\n44\n9\n",
			"uniform extension calls preserve refs, generics, and member priority"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace std::io { void replacement() {} }\n"
			"void main() {}\n",
			"standard child namespace is reserved",
			"namespace 'std' is reserved"))
	{
		++Failures;
	}

	if (!RunSource(
			"typedef IntAlias = int;\n"
			"void main()\n"
			"{\n"
			"	IntAlias x = 42;\n"
			"	print(x);\n"
			"}\n",
			"42\n",
			"typedef top-level"))
	{
		++Failures;
	}

	if (!RunSource(
			"export typedef ExportedAlias = string;\n"
			"void main()\n"
			"{\n"
			"	ExportedAlias s = \"hi\";\n"
			"	print(s);\n"
			"}\n",
			"hi\n",
			"export typedef"))
	{
		++Failures;
	}

	if (!RunSource(
			"void main()\n"
			"{\n"
			"	using LocalInt = int;\n"
			"	LocalInt a = 7;\n"
			"	{\n"
			"		typedef Shadowed = string;\n"
			"		Shadowed s = \"ok\";\n"
			"		print(s);\n"
			"	}\n"
			"	print(a);\n"
			"}\n",
			"ok\n7\n",
			"local using/typedef"))
	{
		++Failures;
	}

	if (!RunSource(
			"using GlobalNum = int;\n"
			"void main()\n"
			"{\n"
			"	using GlobalNum = string;\n"
			"	GlobalNum s = \"shadow\";\n"
			"	print(s);\n"
			"}\n",
			"shadow\n",
			"local shadows global using"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace N\n"
			"{\n"
			"	class C\n"
			"	{\n"
			"		public:\n"

			"		int value;\n"
			"		C(int v)\n"
			"		{\n"
			"			value = v;\n"
			"		}\n"
			"		int get()\n"
			"		{\n"
			"			return value;\n"
			"		}\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	N.C obj = N.C(11);\n"
			"	print(obj.get());\n"
			"}\n",
			"11\n",
			"namespace N.C qualified"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace N\n"
			"{\n"
			"	class C\n"
			"	{\n"
			"		public:\n"
			"		int value;\n"
			"		C(int v) { value = v; }\n"
			"		int get() { return value; }\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	N::C obj = N::C(11);\n"
			"	print(obj.get());\n"
			"}\n",
			"11\n",
			"namespace N::C qualified equivalent"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Util\n"
			"{\n"
			"	export class Pack\n"
			"	{\n"
			"		public:\n"

			"		int x;\n"
			"		Pack(int v) { x = v; }\n"
			"		int get() { return x; }\n"
			"	}\n"
			"}\n"
			"using Pack = Util::Pack;\n"
			"void main()\n"
			"{\n"
			"	Pack b = Pack(99);\n"
			"	print(b.get());\n"
			"}\n",
			"99\n",
			"namespace via using alias with ::"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Util\n"
			"{\n"
			"	export class Pack\n"
			"	{\n"
			"		public:\n"

			"		int x;\n"
			"		Pack(int v) { x = v; }\n"
			"		int get() { return x; }\n"
			"	}\n"
			"}\n"
			"using Pack = Util.Pack;\n"
			"void main()\n"
			"{\n"
			"	Pack b = Pack(99);\n"
			"	print(b.get());\n"
			"}\n",
			"99\n",
			"namespace via using alias"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Outer\n"
			"{\n"
			"	namespace Inner\n"
			"	{\n"
			"		struct Point\n"
			"		{\n"
			"			int x;\n"
			"			int y;\n"
			"		}\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Outer.Inner.Point p = Outer.Inner.Point();\n"
			"	p.x = 3;\n"
			"	p.y = 4;\n"
			"	print(p.x + p.y);\n"
			"}\n",
			"7\n",
			"nested namespace"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Outer::Inner\n"
			"{\n"
			"	struct Point\n"
			"	{\n"
			"		int x;\n"
			"		int y;\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Outer.Inner.Point p = Outer.Inner.Point();\n"
			"	p.x = 3;\n"
			"	p.y = 4;\n"
			"	print(p.x + p.y);\n"
			"}\n",
			"7\n",
			"namespace a::b sugar"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace A::B::C\n"
			"{\n"
			"	struct Deep\n"
			"	{\n"
			"		int n;\n"
			"	}\n"
			"}\n"
			"namespace A\n"
			"{\n"
			"	namespace B\n"
			"	{\n"
			"		struct Mid\n"
			"		{\n"
			"			int n;\n"
			"		}\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	A.B.C.Deep d = A.B.C.Deep();\n"
			"	A.B.Mid m = A.B.Mid();\n"
			"	d.n = 11;\n"
			"	m.n = 2;\n"
			"	print(d.n + m.n);\n"
			"}\n",
			"13\n",
			"namespace a::b::c merges with nested braces"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Calls::Nested\n"
			"{\n"
			"\tint select(int value, int extra = 2) { return value + extra; }\n"
			"\tint select(string value) { return 20; }\n"
			"\tT identity<T>(T value) { return value; }\n"
			"\tvoid bump(ref int value) { value += 1; }\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tint value = Calls.Nested.select(3);\n"
			"\tCalls::Nested::bump(ref value);\n"
			"\tprint(value + Calls::Nested::select(\"x\") + Calls::Nested::identity<int>(4));\n"
			"}\n",
			"30\n",
			"nested namespace free calls share overload/default/ref/generic semantics"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Library\n"
			"{\n"
			"    struct Point { int value; }\n"
			"    int base_value = 4;\n"
			"    int twice(int value) { return value * 2; }\n"
			"    enum class Mode { On = 3 }\n"
			"    enum Flag { Enabled = 5 }\n"
			"}\n"
			"using namespace Library;\n"
			"void main()\n"
			"{\n"
			"    Point point = Point();\n"
			"    point.value = twice(base_value);\n"
			"    print(point.value);\n"
			"    print(Mode::On == Mode::On);\n"
			"    print(Enabled);\n"
			"}\n",
			"8\ntrue\n5\n",
			"using namespace resolves types functions globals and enums"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Numbers { int choose(int value) { return 10; } }\n"
			"namespace Text { int choose(string value) { return 20; } }\n"
			"using namespace Numbers;\n"
			"using namespace Text;\n"
			"void main() { print(choose(1) + choose(\"x\")); }\n",
			"30\n",
			"using namespace combines free overload sets"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Outer\n"
			"{\n"
			"    namespace Shared\n"
			"    {\n"
			"        struct Value { int number; }\n"
			"        int read(Value value) { return value.number; }\n"
			"    }\n"
			"    namespace Client\n"
			"    {\n"
			"        using namespace Shared;\n"
			"        int run() { Value value = Value(); value.number = 7; return read(value); }\n"
			"    }\n"
			"}\n"
			"void main() { print(Outer::Client::run()); }\n",
			"7\n",
			"namespace-scoped using resolves relative namespace"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Helpers { int answer() { return 13; } }\n"
			"namespace Outer\n"
			"{\n"
			"    using namespace Helpers;\n"
			"    namespace Child { int run() { return answer(); } }\n"
			"}\n"
			"void main() { print(Outer::Child::run()); }\n",
			"13\n",
			"parent namespace using applies to descendant namespace"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Helpers { int answer() { return 14; } }\n"
			"namespace Reopened { using namespace Helpers; }\n"
			"namespace Reopened { int run() { return answer(); } }\n"
			"void main() { print(Reopened::run()); }\n",
			"14\n",
			"using applies to later reopening of the same namespace"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Helpers { int initial() { return 15; } }\n"
			"namespace Model\n"
			"{\n"
			"    using namespace Helpers;\n"
			"    class Box\n"
			"    {\n"
			"    public:\n"
			"        int value;\n"
			"        Box() { value = initial(); }\n"
			"    }\n"
			"}\n"
			"void main() { Model::Box box = Model::Box(); print(box.value); }\n",
			"15\n",
			"namespace using applies inside constructors"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Helpers { int answer() { return 16; } }\n"
			"namespace Model\n"
			"{\n"
			"    using namespace Helpers;\n"
			"    int run()\n"
			"    {\n"
			"        function()->int callback = function() -> int => answer();\n"
			"        return callback();\n"
			"    }\n"
			"}\n"
			"void main() { print(Model::run()); }\n",
			"16\n",
			"namespace using applies inside lambda bodies"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Imported { int value() { return 1; } }\n"
			"using namespace Imported;\n"
			"int value() { return 9; }\n"
			"void main() { print(value()); }\n",
			"9\n",
			"ordinary declaration hides using namespace candidate"))
	{
		++Failures;
	}

	if (!RunSource(
			"struct Thing { int root_value; }\n"
			"namespace Inner\n"
			"{\n"
			"    struct Thing { int inner_value; }\n"
			"    int run()\n"
			"    {\n"
			"        Thing value = Thing();\n"
			"        value.inner_value = 12;\n"
			"        return value.inner_value;\n"
			"    }\n"
			"}\n"
			"void main() { print(Inner::run()); }\n",
			"12\n",
			"current namespace type hides root type"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace A { int same(int value) { return 1; } }\n"
			"namespace B { int same(int value) { return 2; } }\n"
			"using namespace A;\n"
			"using namespace B;\n"
			"void main() { print(same(1)); }\n",
			"using namespace ambiguous overload",
			"ambiguous call"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace A { struct Item {} }\n"
			"namespace B { struct Item {} }\n"
			"using namespace A;\n"
			"using namespace B;\n"
			"void main() { Item value = Item(); }\n",
			"using namespace ambiguous type",
			"ambiguous type"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace A { int value = 1; }\n"
			"namespace B { int value = 2; }\n"
			"using namespace A;\n"
			"using namespace B;\n"
			"void main() { print(value); }\n",
			"using namespace ambiguous global",
			"ambiguous global"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace Helpers { int answer() { return 1; } }\n"
			"namespace Owner { using namespace Helpers; }\n"
			"void main() { print(answer()); }\n",
			"namespace-scoped using does not leak to root",
			"unknown function"))
	{
		++Failures;
	}

	if (!CompileFails(
			"using namespace Missing;\n"
			"void main() {}\n",
			"unknown using namespace rejected",
			"unknown namespace"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace Later { int read() { return 1; } }\n"
			"int before() { return read(); }\n"
			"using namespace Later;\n"
			"void main() { print(before()); }\n",
			"using namespace starts at declaration point",
			"unknown function"))
	{
		++Failures;
	}

	{
		const std::filesystem::path TempRoot =
			TemporaryRoot / "using-namespace-import";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		std::ofstream(TempRoot / "library.vs")
			<< "namespace Shared {\n"
			<< "  export struct Value { int number; }\n"
			<< "  export int offset = 3;\n"
			<< "  export int read(Value value) { return value.number + offset; }\n"
			<< "  struct Hidden { int number; }\n"
			<< "}\n";
		std::ofstream(TempRoot / "main.vs")
			<< "import \"library.vs\";\n"
			<< "using namespace Shared;\n"
			<< "void main() { Value value = Value(); value.number = 4; print(read(value)); }\n";

		std::ostringstream Output;
		std::ostringstream Errors;
		VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(), Output, Errors);
		if (!Expect(Result.bSuccess && Output.str() == "7\n",
				"using namespace respects imported exported declarations"))
		{
			std::cerr << Errors.str();
			++Failures;
		}

		std::ofstream(TempRoot / "main.vs", std::ios::trunc)
			<< "import \"library.vs\";\n"
			<< "using namespace Shared;\n"
			<< "void main() { Hidden value = Hidden(); }\n";
		Output.str({});
		Output.clear();
		Errors.str({});
		Errors.clear();
		Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(), Output, Errors);
		if (!Expect(!Result.bSuccess,
				"using namespace does not bypass export visibility"))
		{
			++Failures;
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	{
		const std::filesystem::path TempRoot =
			TemporaryRoot / "template-prefix-import";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		std::ofstream(TempRoot / "library.vs")
			<< "export template<T>\n"
			<< "class Box\n"
			<< "{\n"
			<< "public:\n"
			<< "  T value;\n"
			<< "  Box(T input) { value = input; }\n"
			<< "}\n";
		std::ofstream(TempRoot / "main.vs")
			<< "import \"library.vs\";\n"
			<< "void main() { Box<int> box = Box<int>(9); print(box.value); }\n";

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(), Output, Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess && Output.str() == "9\n",
				"template-prefix generic type survives export/import loading"))
		{
			std::cerr << Errors.str();
			++Failures;
		}
	}

	if (!RunSource(
			"namespace StaticApi::Nested\n"
			"{\n"
			"\tclass Holder\n"
			"\t{\n"
			"\tpublic:\n"
			"\t\tstatic int Value = 4;\n"
			"\t\tstatic int twice(int value) { return value * 2; }\n"
			"\t}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tStaticApi::Nested::Holder::Value += 1;\n"
			"\tprint(StaticApi.Nested.Holder.twice(\n"
			"\t\tStaticApi::Nested::Holder::Value));\n"
			"}\n",
			"10\n",
			"namespaced type static fields and methods"))
	{
		++Failures;
	}

	if (!CompileFails(
			"namespace Incomplete::\n"
			"{\n"
			"}\n"
			"void main() {}\n",
			"namespace trailing :: rejected",
			"expected identifier after '::'"))
	{
		++Failures;
	}

	// Forward scan must qualify types under `a::b` as a.b.T (not a.T).
	if (!RunSource(
			"namespace Ns::Deep\n"
			"{\n"
			"	class Child : public Ns.Deep.Base\n"
			"	{\n"
			"	}\n"
			"	class Base\n"
			"	{\n"
			"	public:\n"
			"		int n;\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Ns.Deep.Child c = Ns.Deep.Child();\n"
			"	c.n = 9;\n"
			"	print(c.n);\n"
			"}\n",
			"9\n",
			"namespace a::b forward type names"))
	{
		++Failures;
	}

	if (!CompileFails(
			"void main()\n"
			"{\n"
			"	using A = int;\n"
			"	typedef A = string;\n"
			"	A x = 1;\n"
			"}\n",
			"duplicate local type alias",
			"duplicate type alias"))
	{
		++Failures;
	}

	{
		const std::filesystem::path TempRoot =
			TemporaryRoot / "alias-prefer-module";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "lib.vs")
				<< "export using Num = string;\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"lib.vs\";\n"
				<< "using Num = int;\n"
				<< "void main()\n"
				<< "{\n"
				<< "	Num n = 21;\n"
				<< "	print(n);\n"
				<< "}\n";
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "reference module alias preferred over import"))
		{
			std::cerr << Errors.str();
			++Failures;
		}
		else if (!Expect(Output.str() == "21\n", "reference module alias preferred over import"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			++Failures;
		}
	}

	{
		const std::filesystem::path TempRoot = TemporaryRoot / "imported-default-rest-alias";
		std::filesystem::create_directories(TempRoot);
		std::ofstream(TempRoot / "lib.vs")
			<< "using Amount = long;\n"
			<< "export class Accumulator { public: long operator()(const Amount& n = 4) { return n; }\n"
			<< "  Amount read(const Amount& n = 6) { return n; }\n"
			<< "  array<Amount> pack(Amount... items) { return items; }\n"
			<< "  static Amount read_static(const Amount& n = 10) { return n; }\n"
			<< "  int pick(int n) { return n; } string pick(string s) { return s; } };\n"
			<< "export long read(const Amount& n = 5) { return n; }\n"
			<< "export class Collector { public: array<Amount> operator()(Amount... items) { return items; } };\n"
			<< "export array<Amount> collect(Amount... items) { return items; }\n"
			<< "export class GenericBase<T> { public: T read(const T& n = 9) { return n; }\n"
			<< "  static T read_static(const T& n = 11) { return n; } };\n";
		std::ofstream(TempRoot / "main.vs")
			<< "import \"lib.vs\"; using Amount = string;\n"
			<< "class Child : public Accumulator {}; class GenericChild<U> : public GenericBase<U> {};\n"
			<< "void main() { Accumulator f = Accumulator(); print(f()); print(read());\n"
			<< "  long explicit_value = 8i64; print(f(explicit_value)); print(read(ref explicit_value));\n"
			<< "  Collector collector = Collector(); array<long> a = collector(2i64, 3i64); print(a[0] + a[1]);\n"
			<< "  array<long> b = collector(); b.push(7i64); print(b[0]); print(collector(a)[1]);\n"
			<< "  array<long> c = collect(2i64, 3i64); print(c[0] + c[1]);\n"
			<< "  array<long> d = collect(); d.push(7i64); print(d[0]); print(collect(c)[1]);\n"
			<< "  Child child = Child(); long e = child.read(); print(e);\n"
			<< "  array<long> packed = child.pack(2i64, 3i64); print(packed[0] + packed[1]);\n"
			<< "  array<long> empty = child.pack(); empty.push(7i64); print(empty[0]); print(child.pack(packed)[1]);\n"
			<< "  long g = Child.read_static(); print(g);\n"
			<< "  GenericChild<long> generic_child = GenericChild<long>(); long h = generic_child.read(); print(h);\n"
			<< "  long i = GenericChild<long>.read_static(); print(i);\n"
			<< "  string selected = child.pick(\"ok\"); print(selected); }\n";
		for (const VeroScript::EExecutionBackend Backend : {
			VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main.vs").generic_string(), Output, Errors,
				{}, {}, false, nullptr, {}, {}, {}, Backend);
			if (!Expect(Result.bSuccess, "imported default/rest aliases use the declaring module")
				|| !Expect(Output.str() == "4\n5\n8\n8\n5\n7\n3\n5\n7\n3\n6\n5\n7\n3\n10\n9\n11\nok\n",
					"imported operator/function/method defaults, rest arguments and return types"))
			{
				std::cerr << Errors.str() << Output.str();
				++Failures;
			}
		}
	}

	if (!RunSource(
			"namespace\n"
			"{\n"
			"\tint transparent_helper() { return 40; }\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tprint(transparent_helper() + 2);\n"
			"}\n",
			"42\n",
			"anonymous namespace is a transparent declaration group"))
	{
		++Failures;
	}
	if (!RunSource(
			"namespace Outer\n"
			"{\n"
			"\tnamespace\n"
			"\t{\n"
			"\t\tint nested_helper() { return 9; }\n"
			"\t}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tprint(Outer.nested_helper());\n"
			"}\n",
			"9\n",
			"anonymous namespace retains enclosing namespace scope"))
	{
		++Failures;
	}

	if (!RunSource(
			"interface ITag\n"
			"{\n"
			"	virtual int tag() = 0;\n"
			"}\n"
			"class Box\n"
			"{\n"
			"\tpublic:\n"

			"	int tag() { return 0; }\n"
			"}\n"
			"class Box<T> : public ITag\n"
			"{\n"
			"\tpublic:\n"

			"	T v;\n"
			"	Box(T x) { v = x; }\n"
			"	int tag() { return 42; }\n"
			"}\n"
			"class Cell : public Box<int>\n"
			"{\n"
			"\tpublic:\n"

			"	Cell() : Box(7) {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Cell c = Cell();\n"
			"	print(c.tag());\n"
			"}\n",
			"42\n",
			"arity-safe bases for Box/Box<T>"))
	{
		++Failures;
	}

	if (!RunSource(R"(
namespace Outer{namespace Inner{int data=40;int make(){return 2;}}int run(){fn()->int callback=Inner::make;return Inner::data+Inner::make()+callback();}}
void main(){print(Outer::run());}
)",
			"44\n", "qualified functions, variables and callable values share lexical namespace lookup"))
	{
		++Failures;
	}
	if (!RunSource(R"(
namespace Inner{struct Item{int value=1;};}
namespace Outer{namespace Inner{struct Item{int value=2;};}int run(){Inner::Item value=Inner::Item();return value.value;}}
void main(){print(Outer::run());::Inner::Item root=::Inner::Item();print(root.value);}
)",
			"2\n1\n", "qualified type lookup selects the nearest prefix and preserves explicit root scope"))
	{
		++Failures;
	}
	if (!RunSource(R"(
namespace Parent{namespace Child{struct Item{int value=40;};int make(){return 2;}}}
using namespace Parent;
void main(){Child::Item value=Child::Item();print(value.value+Child::make());}
)",
			"42\n", "using directives expose child namespaces for both types and functions"))
	{
		++Failures;
	}
	if (!CompileFails(R"(
namespace Imported{namespace Lib{int value(){return 22;}}}
using namespace Imported;
namespace N{namespace Lib{int other=1;}int run(){return Lib::value();}}
void main(){print(N::run());}
)",
			"a missing qualified function cannot rebind its lexical namespace through using"))
	{
		++Failures;
	}
	if (!CompileFails(R"(
namespace Imported{namespace Lib{int value=22;}}
using namespace Imported;
namespace N{namespace Lib{int other=1;}int run(){return Lib::value;}}
void main(){print(N::run());}
)",
			"a missing qualified global cannot rebind its lexical namespace through using"))
	{
		++Failures;
	}
	if (!CompileFails(R"(
namespace Imported{namespace Lib{int value(){return 22;}}}
using namespace Imported;
namespace N{namespace Lib{int other=1;}int run(){fn()->int callback=Lib::value;return callback();}}
void main(){print(N::run());}
)",
			"a missing qualified callable cannot rebind its lexical namespace through using"))
	{
		++Failures;
	}
	if (!CompileFails(R"(
namespace Imported{namespace Lib{struct Value{int value=22;};}}
using namespace Imported;
namespace N{namespace Lib{int other=1;}int run(){Lib::Value value=Lib::Value();return value.value;}}
void main(){print(N::run());}
)",
			"a missing qualified type cannot rebind its lexical namespace through using"))
	{
		++Failures;
	}
	if (!CompileFails(R"(
namespace Lib{struct Value{int value=22;};}
namespace N{namespace Lib{int other=1;}int run(){Lib::Value value=Lib::Value();return value.value;}}
void main(){print(N::run());}
)",
			"a missing qualified type cannot rebind its lexical namespace to root"))
	{
		++Failures;
	}
	if (!RunSource(R"(
namespace Imported{namespace Lib{struct Value{int value=22;};int data=20;int make(){return 2;}}}
using namespace Imported;
namespace N{int run(){Lib::Value value=Lib::Value();fn()->int callback=Lib::make;return value.value+Lib::data+Lib::make()+callback();}}
void main(){print(N::run());}
)",
			"46\n", "using directives resolve qualified types and values when no lexical prefix is bound"))
	{
		++Failures;
	}
	if (!RunSource(R"(
namespace Lib{int make(){return 42;}}
namespace Imported{namespace Lib{int make(){return 22;}}}
using namespace Imported;
namespace N{namespace Lib{int other=1;}int run(){return ::Lib::make();}}
void main(){print(N::run());}
)",
			"42\n", "explicit root qualification ignores lexical and using namespace prefixes"))
	{
		++Failures;
	}
	if (!RunSource(R"(
namespace Util{int helper(int value){return value+2;}int add(int value);}
int helper(int value){return value+1;}
int Util::add(int value){return helper(value);}
void main(){print(Util::add(40));}
)",
			"42\n", "out-of-namespace function bodies use their declaration's namespace"))
	{
		++Failures;
	}
	if (!RunSource(R"(
namespace Util{int Util();}
int Util::Util(){return 42;}
void main(){print(Util::Util());}
)",
			"42\n", "a free function matching its namespace name still has a return type"))
	{
		++Failures;
	}


	{
		const auto Root = TemporaryRoot / "deferred-imported-namespace-alias";
		std::filesystem::create_directories(Root);
		std::ofstream(Root / "library.vs")
			<< "namespace Shared { export using Count=int; export typedef Label=string;"
			   "using Hidden=int; export Count add(Count a,Count b){return a+b;} }"
			   "namespace PrivateSpace { using PrivateCount=int; }\n";
		std::ofstream(Root / "facade.vs") << "export import \"library.vs\";\n";
		std::ofstream(Root / "other.vs") << "namespace Other { export using Count=int; }\n";
		std::ofstream(Root / "secret.vs") << "namespace Secret { export using Token=int; }\n";
		std::ofstream(Root / "private-import.vs")
			<< "import \"secret.vs\"; export int placeholder(){return 0;}\n";
		std::ofstream(Root / "foreign-using.vs")
			<< "export import \"library.vs\"; using namespace Shared;\n";
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed})
		{
			const auto Check = [&](const char* Source, const bool Success, const char* Expected,
			                       const char* Label, const char* ErrorNeedle = nullptr)
			{
				std::ofstream(Root / "main.vs", std::ios::trunc) << Source;
				std::ostringstream Output, Errors;
				const auto Result = VeroScript::Test::FEngine::RunFile(
					(Root / "main.vs").generic_string(), Output, Errors,
					{}, {}, false, nullptr, {}, {}, {}, Backend);
				if (!Expect(Result.bSuccess == Success && (!Success || Output.str() == Expected) &&
				        (!ErrorNeedle || Errors.str().find(ErrorNeedle) != std::string::npos), Label))
				{
					std::cerr << Errors.str() << Output.str();
					++Failures;
				}
			};
			Check("import \"library.vs\"; using namespace Shared;"
			      "void main(){Count n=add(20,22);Label label=\"direct\";print(n);print(label);}",
				true, "42\ndirect\n", "deferred bodies resolve imported using and typedef aliases");
			Check("import \"facade.vs\"; using namespace Shared;"
			      "void main(){Count n=add(20,22);Label label=\"facade\";print(n);print(label);}",
				true, "42\nfacade\n", "deferred bodies resolve aliases through export imports");
			Check("import \"library.vs\";"
			      "void main(){Shared::Count n=Shared::add(20,22);print(n);}",
				true, "42\n", "qualified imported alias lookup remains available");
			Check("import \"library.vs\";namespace Local {using namespace Shared;"
			      "int read(){Count n=42;return n;}}void main(){print(Local::read());}",
				true, "42\n", "namespace-scoped using directives resolve imported aliases in fragments");
			Check("import \"library.vs\";using namespace Shared;void main(){Hidden n=1;}",
				false, "", "fragment namespace lookup cannot expose a private alias");
			Check("import \"library.vs\";using namespace PrivateSpace;void main(){PrivateCount n=1;}",
				false, "", "fragment namespace lookup cannot expose a private namespace");
			Check("import \"private-import.vs\";using namespace Secret;void main(){Token n=1;}",
				false, "", "fragment namespace lookup cannot bypass a non-exported import");
			Check("import \"library.vs\";import \"other.vs\";using namespace Shared;using namespace Other;"
			      "void main(){Count n=1;}", false, "", "fragment aliases retain using ambiguity",
				"ambiguous type");
			Check("import \"foreign-using.vs\";void main(){Count n=1;}",
				false, "", "another module's using directive does not leak into fragments");
		}
	}

	if (Failures != 0)
	{
		std::cerr << Failures << " test(s) failed\n";
		return 1;
	}
	std::cout << "VeroScriptAliasNamespaceTests passed\n";
	return 0;
}
