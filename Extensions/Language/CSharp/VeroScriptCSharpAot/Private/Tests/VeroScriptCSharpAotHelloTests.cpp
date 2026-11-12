// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCSharpAot.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptLambdaStaticFixture.h"


#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace
{
	bool WriteText(const std::filesystem::path& Path, const std::string& Text)
	{
		std::ofstream Output(Path, std::ios::out | std::ios::binary | std::ios::trunc);
		Output.write(Text.data(), static_cast<std::streamsize>(Text.size()));
		return static_cast<bool>(Output);
	}

	bool ReadText(const std::filesystem::path& Path, std::string& Text)
	{
		std::ifstream Input(Path, std::ios::in | std::ios::binary);
		if (!Input)
		{
			return false;
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		Text = Buffer.str();
		if (Text.size() >= 3
			&& static_cast<unsigned char>(Text[0]) == 0xef
			&& static_cast<unsigned char>(Text[1]) == 0xbb
			&& static_cast<unsigned char>(Text[2]) == 0xbf)
		{
			Text.erase(0, 3);
		}
		for (std::size_t Position = 0; (Position = Text.find("\r\n", Position)) != std::string::npos;)
		{
			Text.replace(Position, 2, "\n");
		}
		return true;
	}

	std::string Quote(const std::filesystem::path& Path)
	{
		return "\"" + Path.string() + "\"";
	}

	std::string EncodeModuleSuffix(const std::string_view Text)
	{
		static constexpr char Hex[] = "0123456789ABCDEF";
		std::string Result;
		for (const unsigned char Character : Text)
		{
			const bool bAsciiAlphaNumeric =
				(Character >= 'a' && Character <= 'z')
				|| (Character >= 'A' && Character <= 'Z')
				|| (Character >= '0' && Character <= '9');
			if (bAsciiAlphaNumeric)
			{
				Result.push_back(static_cast<char>(Character));
			}
			else
			{
				Result.push_back('_');
				Result.push_back(Hex[Character >> 4]);
				Result.push_back(Hex[Character & 0x0f]);
			}
		}
		return Result.empty() ? "local" : Result;
	}

	bool Run(const std::string& Command, const char* Label, const int ExpectedExitCode = 0)
	{
		#if defined(_WIN32)
		// cmd.exe removes the first quoted token unless the complete /C payload is
		// quoted as well. This matters for the default Program Files dotnet path.
		const std::string ShellCommand = "\"" + Command + "\"";
		#else
		const std::string& ShellCommand = Command;
		#endif
		const int Code = std::system(ShellCommand.c_str());
		#if defined(_WIN32)
		const int ExitCode = Code;
		#else
		const int ExitCode = Code >= 0 && WIFEXITED(Code) ? WEXITSTATUS(Code) : -1;
		#endif
		if (ExpectedExitCode < 0 ? ExitCode != 0 : ExitCode == ExpectedExitCode)
		{
			return true;
		}
		std::cerr << "FAIL: " << Label << " (exit=" << Code << ")\n"
				  << Command << '\n';
		return false;
	}

	bool VerifyNumericFailures(const std::string& Command, const std::filesystem::path& Root)
	{
		const std::array Cases =
		{
			std::pair{"negative-fraction", "to_unsigned overflow"},
			std::pair{"negative-compound", "to_unsigned overflow"},
			std::pair{"remainder-zero", "modulo is only defined for integers"},
			std::pair{"remainder-nonzero", "modulo is only defined for integers"},
			std::pair{"remainder-compound", "modulo is only defined for integers"},
			std::pair{"console-color", "std::console::set_color: unknown color name"},
			std::pair{"console-line", "std::console::clear_line: expected all, before or after"},
			std::pair{"console-position", "std::console::move_cursor coordinates must be in 0..32766"}
		};
		for (const auto& [Argument, Message] : Cases)
		{
			const auto Output = Root / (std::string(Argument) + "-failure.txt");
			if (!Run(Command + " " + Argument + " > " + Quote(Output) + " 2>&1",
					"reject invalid numeric execution", -1))
			{
				return false;
			}
			std::string Actual;
			if (!ReadText(Output, Actual) || Actual.find(Message) == std::string::npos)
			{
				std::cerr << "FAIL: numeric failure diagnostic: " << Argument << " [" << Actual << "]\n";
				return false;
			}
		}
		return true;
	}

	bool VerifyInitialization(const std::filesystem::path& Root)
	{
		struct FCase
		{
			const char* Name;
			const char* Source;
			const char* Output;
			const char* Error;
			int ExitCode;
		};
		const std::array<FCase, 7> Cases = {{
			{"Startup",
			    "int initialize(string label){print(label);return 7;}"
			    "namespace Scope{class Holder{public:static int value=initialize(\"static\");};"
			    "class Payload{public:int value=42;};Payload item=Payload();}"
			    "class Generic<__vs_initialize_S5_value>{public:static int value=initialize(\"generic\");};"
			    "enum E{A=2};void retained(){Generic<int> value=Generic<int>();"
			    "Generic<E> enum_value=Generic<E>();Generic<array<E>> array_value=Generic<array<E>>();}"
			    "int first=initialize(\"first\");const int second=initialize(\"second\");"
			    "int unused=5;class Private{static int unused=5;};"
			    "void main(){print(\"main\");}",
			    "static\ngeneric\ngeneric\ngeneric\nfirst\nsecond\nmain\n", "", 0},
			{"GlobalThrow",
			    "int initialize(){print(\"before\");throw 7;}int unused=initialize();"
			    "void main(){print(\"unexpected\");}",
			    "before\n", "uncaught script exception: 7\n", 1},
			{"GlobalExit",
			    "int initialize(){print(\"before\");exit(7);return 0;}int unused=initialize();"
			    "void main(){print(\"unexpected\");}",
			    "before\n", "", 7},
			{"StaticThrow",
			    "int initialize(){print(\"before\");throw 7;}"
			    "class Item{public:static int unused=initialize();};"
			    "void main(){print(\"unexpected\");}",
			    "before\n", "uncaught script exception: 7\n", 1},
			{"StaticExit",
			    "int initialize(){print(\"before\");exit(7);return 0;}"
			    "class Item{public:static int unused=initialize();};"
			    "void main(){print(\"unexpected\");}",
			    "before\n", "", 7},
			{"Deferred",
			    "fn()->string deferred=[]()->string{return later;};"
			    "int flag=1;int condition=flag!=0?1:2;"
			    "fn()->int ready=[]()->int{return 1;};int called=ready();"
			    "string later=\"ready\";int read(){return number;}int first=read();int number=5;"
			    "int setup(){written=\"written\";print(written);return 0;}int set=setup();"
			    "string written=\"final\";void main(){print(deferred());print(first);print(written);}",
			    "written\nready\n0\nfinal\n", "", 0},
			{"Rebound",
			    "fn()->string target=[]()->string{return \"safe\";};"
			    "string invoke(){return target();}class Item{public:string value=invoke();};"
			    "string setup(){string ignored=invoke();Item first=Item();"
			    "target=[]()->string{return \"second\";};string result=invoke();Item second=Item();"
			    "return result+\":\"+second.value;}string value=setup();"
			    "int fact(int n){if(n<=1)return 1;return n*fact(n-1);}int total=fact(4);"
			    "string later=\"pending\";void main(){print(value);print(total);}",
			    "second:second\n24\n", "", 0}
		}};
		for (const char* Source : {
		         "string read(){return later;}string first=read();string later=\"value\";void main(){}",
		         "fn()->string read=[]()->string{return later;};string first=read();string later=\"value\";void main(){}",
		         "fn()->string make(){return []()->string{return later;};}fn()->string read=make();string first=read();string later=\"value\";void main(){}",
		         "int setup(){return 0;later=\"unreachable\";}int first=setup();string read(){return later;}string second=read();string later=\"value\";void main(){}",
		         "fn()->string target=[]()->string{return \"safe\";};string invoke(){return target();}string setup(){string ignored=invoke();target=[]()->string{return later;};return invoke();}string first=setup();string later=\"value\";void main(){}",
		         "fn()->string target=[]()->string{return \"safe\";};class Item{public:string value=target();};string setup(){Item first=Item();target=[]()->string{return later;};Item second=Item();return second.value;}string first=setup();string later=\"value\";void main(){}",
		         "int calls=0;fn()->string target=[]()->string{return \"safe\";};string invoke(){string value=target();if(calls==0){calls=1;target=[]()->string{return later;};return invoke();}return value;}string first=invoke();string later=\"value\";void main(){}",
		         "string read(const string& value){return value;}string first=read(ref later);string later=\"value\";void main(){}",
		         "class Item{public:static string first=read();static string later=\"value\";static string read(){return later;}};void main(){}"})
		{
			VeroScript::FCSharpAotSourceBundle Rejected;
			std::string Error;
			if (VeroScript::EmitCSharpSourceBundleFromSource(Source, "forward.vs", "Rejected", Rejected, Error)
			    || !Rejected.Files.empty() || !Rejected.ManifestJson.empty()
			    || Error.find("uninitialized non-int global/static storage") == std::string::npos)
			{
				std::cerr << "FAIL: forward initialization read did not produce a clean AOT rejection: " << Error << '\n';
				return false;
			}
		}
		const std::filesystem::path DotNet(VEROSCRIPT_CSHARP_AOT_DOTNET);
		for (const auto& Test : Cases)
		{
			const auto Directory = Root / "initialization" / Test.Name;
			std::filesystem::create_directories(Directory);
			VeroScript::FCSharpAotSourceBundle Bundle;
			std::string Error;
			if (!VeroScript::EmitCSharpSourceBundleFromSource(Test.Source, "initialization.vs",
			        Test.Name, Bundle, Error))
			{
				std::cerr << "FAIL: emit initialization case " << Test.Name << ": " << Error << '\n';
				return false;
			}
			// Keep this consumer unmodified: a test-only static constructor would
			// hide lazy CLR initialization in the ordinary generated application.
			for (const auto& File : Bundle.Files)
			{
				if (!WriteText(Directory / File.RelativePath, File.Contents)) return false;
			}
			const auto Project = Directory / (std::string(Test.Name) + ".csproj");
			const auto VerifyOutput = [&](const std::string& Command, const std::string& Prefix)
			{
				const auto Output = Directory / (Prefix + "-output.txt");
				const auto Errors = Directory / (Prefix + "-error.txt");
				std::string ActualOutput, ActualError;
				if (!Run(Command + " > " + Quote(Output) + " 2> " + Quote(Errors),
				        "run initialization case", Test.ExitCode)
				    || !ReadText(Output, ActualOutput) || !ReadText(Errors, ActualError)
				    || ActualOutput != Test.Output || ActualError != Test.Error)
				{
					std::cerr << "FAIL: initialization case " << Test.Name << ' ' << Prefix
					          << " output=[" << ActualOutput << "] error=[" << ActualError << "]\n";
					return false;
				}
				return true;
			};
			if (!Run(Quote(DotNet) + " build " + Quote(Project) + " --configuration Release --nologo",
			        "build initialization case")
			    || !VerifyOutput(Quote(DotNet) + " run --no-build --project " + Quote(Project)
			        + " --configuration Release", "managed"))
			{
				return false;
			}
			#if VEROSCRIPT_CSHARP_AOT_TEST_NATIVE
			const auto Publish = Directory / "publish";
			if (!Run(Quote(DotNet) + " publish " + Quote(Project)
			        + " --configuration Release --runtime " VEROSCRIPT_CSHARP_AOT_RID
			        + " --self-contained true -p:PublishAot=true --output " + Quote(Publish) + " --nologo",
			        "publish initialization case"))
			{
				return false;
			}
			#if defined(_WIN32)
			const auto Executable = Publish / (std::string(Test.Name) + ".exe");
			#else
			const auto Executable = Publish / Test.Name;
			#endif
			if (!VerifyOutput(Quote(Executable), "native")) return false;
			#endif
		}
		return true;
	}
}

int main()
{
	const std::string SupportSource =
		"export enum Tone { Warm = 3 }\n"
		"namespace Shared { export int seed = 11; }\n"
		"using namespace Shared;\n"
		"export int copied_seed = seed;\n"
		"export T id<T>(T value) { return value; }\n"
		"export int sum_values() {\n"
		"    array<int> values = {1, 2, 3, 4, 5};\n"
		"    int total = 0;\n"
		"    for (int value : values) total += value;\n"
		"    return total;\n"
		"}\n"
		"export int classify(int value) {\n"
		"    switch (value) {\n"
		"    case 1: return 10;\n"
		"    case 2: return 20;\n"
		"    default: return -1;\n"
		"    }\n"
		"}\n";
	const std::string PrivateModuleA =
		"int private_state = 10;\n"
		"int private_helper() { return private_state; }\n"
		"export int module_a_value() { auto copy = [value=private_helper]()->int { return value(); }; return copy(); }\n";
	const std::string PrivateModuleB =
		"int private_state = 20;\n"
		"int private_helper() { return private_state; }\n"
		"export int module_b_value() { auto copy = [value=private_helper]()->int { return value(); }; return copy(); }\n";
	std::string Source = std::string(VeroScript::Test::LambdaStaticDefinitions) + R"vs(
int csharpGlobalNext(int seed) { static int value = seed; return ++value; }
int csharpGlobalFirst = csharpGlobalNext(10);
int csharpCaptureAnswer() { return 42; }
namespace CSharpFunctionValues
{
    int calls = 0;
    int answer() { return 42 + calls++; }
    namespace Inner { int value() { return 7; } }
    int Inner() { return 8; }
    int& alias(int& value) { return value; }
    int read(const int& value) { return value; }
    fn()->int variable = []()->int { return 31; };
    int variable() { return 99; }
}
class CSharpFunctionReceiver
{
public:
    fn()->int answer;
}
void checkCSharpFunctionValues()
{
    auto callback = [copy=CSharpFunctionValues::answer, offset=1]()->int { return copy()+offset; };
    assert(CSharpFunctionValues::calls==0 && callback()==43 && callback()==44 && CSharpFunctionValues::calls==2);
    auto direct = CSharpFunctionValues::Inner::value;
    fn()->int typed = CSharpFunctionValues.Inner.value;
    auto absolute = ::CSharpFunctionValues::Inner::value;
    assert(direct()==7 && typed()==7 && absolute()==7);
    auto variable = CSharpFunctionValues::variable;
    assert(variable()==31);
    auto ref_callback = [copy=CSharpFunctionValues::alias](int& value)->void
    {
        int& bound = copy(&value);
        bound += 2;
    };
    int value = 40;
    ref_callback(&value);
    auto const_callback = [copy=CSharpFunctionValues::read](const int& value)->int { return copy(&value); };
    assert(value==42 && const_callback(&value)==42);
    CSharpFunctionReceiver CSharpFunctionValues = CSharpFunctionReceiver();
    CSharpFunctionValues.answer = []()->int { return 5; };
    auto shadowed = [copy=CSharpFunctionValues.answer]()->int { return copy(); };
    assert(shadowed()==5);
}
class CSharpUnusedCaptureOwner<T>
{
public:
    T value;
    fn()->fn()->T nested()
    {
        return [=]() -> fn()->T { return [copy=value]() -> T { return copy; }; };
    }
    fn()->fn()->int shadowed()
    {
        return [=]() -> fn()->int { return [value=7]() -> int { return value; }; };
    }
}
fn()->int csharpReferenceSnapshot()
{
    int value = 0;
    int& reference = value;
    return [reference]() mutable -> int
    {
        auto bump = [&reference]() -> int { return ++reference; };
        return bump();
    };
}
int csharpStaticAttempts = 0;
int csharpStaticInitialize()
{
    if (++csharpStaticAttempts == 1) { throw 7; }
    return 11;
}
int csharpStaticRetry()
{
    for (int index = 0; index < 2; ++index)
    {
        try { static int value = csharpStaticInitialize(); return ++value; }
        catch (int error) { assert(error == 7); }
    }
    return -1;
}
)vs" +
		"import \"support.vs\";\n"
		"struct CaptureState { public: int n=1; };\n"
		"struct pair<T> { T custom; }\n"
		"using WideElement = long;\n"
		"import \"private_a.vs\";\n"
		"import \"private_b.vs\";\n"
		"import \"core.vs\";\n"
		"int length_kind(int value) { return 1; }\n"
		"int length_kind(int64 value) { return 2; }\n"
		"namespace Numbers { int choose(int value) { return 10; } }\n"
		"namespace Text { int choose(string value) { return 20; } }\n"
		"using namespace Numbers;\n"
		"using namespace Text;\n"
		"namespace A { int B_C() { return 1; } }\n"
		"namespace A_B { int C() { return 2; } }\n"
		"string greet(string value = \"你好，世界\") { return value; }\n"
		"T default_id<T>(T value, int count = 0) { print(count); return value; }\n"
		"T alias_identity<T>(T value) { using LocalValue = T; LocalValue result = value; return result; }\n"
		"T generic_name_collision<T>(T T) {\n"
		"    { int T = 2; assert(T == 2); }\n"
		"    return T;\n"
		"}\n"
		"int generic_vararg_name_collision<__VeroScriptVarargsArray0>(\n"
		"    __VeroScriptVarargsArray0... values) { return len(values); }\n"
		"pair<T,int> generic_pair<T>() { return pair<T,int>(); }\n"
		"namespace Tools {\n"
		"    enum class Level : int { High = 4 }\n"
		"    int global_value = 7;\n"
		"    int base_value() { return 4; }\n"
		"    int bump() { return base_value() + 1; }\n"
		"    int read_level(Level level) { return level == Level::High ? 4 : 0; }\n"
		"    int read_global() { return global_value; }\n"
		"    int with_default(int value = 9) { return value; }\n"
		"    int narrow_arg(int8 value) { return value; }\n"
		"}\n"
		"int string_kind(string value) { return 1; }\n"
		"int string_kind(wstring value) { return 2; }\n"
		"int extension_size(array<int> values, int extra = 0) { return len(values) + extra; }\n"
		"T extension_last<T>(array<T> values) { return values[len(values) - 1]; }\n"
		"void extension_bump(ref int value, int amount = 1) { value += amount; }\n"
		"int push(int value) { return value + 10; }\n"
		"void increment(ref int value) { value += 1; }\n"
		"int read_ref(const int& value) { return value; }\n"
		"int apply_callable(fn(int)->int callable, int value) { return callable(value); }\n"
		"int twice_callable(int value) { return value * 2; }\n"
		"void increment_callable(ref int value) { value += 2; }\n"
		"fn(int)->int make_adder(int amount) { return fn(int value) -> int => value + amount; }\n"
		"int ref_overload(int value) { return 1; }\n"
		"int ref_overload(ref int value) { return 2; }\n"
		"int readonly_overload(int value) { return 3; }\n"
		"int readonly_overload(const int& value) { return 4; }\n"
		"int cv_ref_overload(int& value) { return 5; }\n"
		"int cv_ref_overload(const int& value) { return 6; }\n"
		"int extension_ref_kind(int value) { return 7; }\n"
		"int extension_ref_kind(const int& value) { return 8; }\n"
		"array<int> make_overload_values() { return array<int>{1}; }\n"
		"const int overload_shadow = 1;\n"
		"int variadic_sum(int... values) { int total = 0; for (int value : values) total += value; return total; }\n"
		"int switch_scopes(int value) {\n"
		"    switch (value) {\n"
		"    case 1: int result = 21; return result;\n"
		"    case 2: int result = 22; return result;\n"
		"    default: return 0;\n"
		"    }\n"
		"}\n"
		"int switch_text(string value) {\n"
		"    switch (value) { case \"yes\": return 1; default: return 0; }\n"
		"}\n"
		"int read_block_shadow_initializer() {\n"
		"    int item = 13;\n"
		"    { int item = item + 1, spare = item + 1; return item + spare; }\n"
		"}\n"
		"int read_for_shadow_initializer() {\n"
		"    int item = 17;\n"
		"    for (int item = item + 1, spare = item + 1; item == 18;) return spare;\n"
		"    return 0;\n"
		"}\n"
		"class ShadowField {\n"
		"public:\n"
		"    int value;\n"
		"    ShadowField(int input) { value = input; }\n"
		"    int next() { int value = value + 1; return value; }\n"
		"}\n"
		"int read_lambda_parameter_shadow() {\n"
		"    int value = 4;\n"
		"    fn(int)->int next = [](int value) -> int => value + 1;\n"
		"    return next(8) + value;\n"
		"}\n"
		"int exception_flow() {\n"
		"    int result = 0;\n"
		"    try { try { throw 7; } catch (int value) { value = 8; throw; } }\n"
		"    catch (int value) { result += value; }\n"
		"    try { try { throw 10; } catch (int& value) { value = 11; throw; } }\n"
		"    catch (const int& value) { result += value; }\n"
		"    finally { result += 2; }\n"
		"    try { throw \"ignored\"; } catch (int) { result = -100; }\n"
		"    catch (...) { result += 1; }\n"
		"    return result;\n"
		"}\n"
		"int exception_return() {\n"
		"    try { return 5; } finally { print(\"exception-finally\"); }\n"
		"}\n"
		"int in_order = 0;\n"
		"fn(int)->int global_callable = fn(int value) -> int => value - 1;\n"
		"int in_left() { in_order = in_order * 10 + 1; return 3; }\n"
		"set<int> in_right(set<int> value) { in_order = in_order * 10 + 2; return value; }\n"
		"void emit_error_only() { eprint(\"error\"); }\n"
		"auto inferred_global = array<int>();\n"
		"class Counter {\n"
		"public:\n"
		"    int value = 0;\n"
		"    Counter(int initial) { value = initial; }\n"
		"    int add(int amount) { value += amount; return value; }\n"
		"    int choose(int input) { return 7; }\n"
		"    int choose(string input) { return 8; }\n"
		"    int with_default(int amount = 4) { return amount; }\n"
		"    static int static_value() { return 6; }\n"
		"    int& value_ref() { return value; }\n"
		"}\n"
		"struct ConstPayload {\n"
		"    int value;\n"
		"    ConstPayload(int input) { value = input; }\n"
		"}\n"
		"class ConstHolder {\n"
		"public:\n"
		"    const int id;\n"
		"    const ConstPayload payload;\n"
		"    ConstHolder(int input) : payload(input + 1), id(input) {}\n"
		"    int total() const { return id + payload.value; }\n"
		"}\n"
		"struct DefaultToken {\n"
		"    int value = 3;\n"
		"    DefaultToken() { value = 4; }\n"
		"}\n"
		"struct GenericFieldBox<T> {\n"
		"    const T value;\n"
		"    GenericFieldBox(T input) : value(input) {}\n"
		"}\n"
		"struct GenericMemberNameCollision<T> {\n"
		"    T payload;\n"
		"    int T;\n"
		"    GenericMemberNameCollision(T input, int value) { payload = input; T = value; }\n"
		"}\n"
		"struct GenericMethodNameCollision<T> {\n"
		"    T payload;\n"
		"    int T() { return 10; }\n"
		"}\n"
		"struct GenericRecordNameCollision<Equals> {\n"
		"    Equals payload;\n"
		"}\n"
		"struct RecordMemberNameCollision {\n"
		"    int Equals;\n"
		"    int GetHashCode() { return 4; }\n"
		"    int total() { return Equals + GetHashCode(); }\n"
		"    fn()->int getter() { return fn()->int => Equals; }\n"
		"}\n"
		"struct RecordOperatorNameCollision {\n"
		"    int value;\n"
		"    static bool op_Equality(RecordOperatorNameCollision left, "
		"RecordOperatorNameCollision right) { return left.value == right.value; }\n"
		"    static bool op_Inequality(RecordOperatorNameCollision left, "
		"RecordOperatorNameCollision right) { return left.value != right.value; }\n"
		"}\n"
		"struct GenericRecordOperatorNameCollision<op_Equality, op_Inequality> {\n"
		"    op_Equality left;\n"
		"    op_Inequality right;\n"
		"}\n"
		"class ObjectMemberNameCollision {\n"
		"public:\n"
		"    int Equals;\n"
		"    ObjectMemberNameCollision(int value) { Equals = value; }\n"
		"    int GetHashCode() { return 6; }\n"
		"    int total() { return Equals + GetHashCode(); }\n"
		"}\n"
		"struct FieldInitDefaults {\n"
		"    const int explicit_zero;\n"
		"    const int implicit_zero;\n"
		"    const string explicit_text;\n"
		"    int mutable_value;\n"
		"    const DefaultToken token;\n"
		"    FieldInitDefaults(int input) : token(), mutable_value(input), explicit_text(), explicit_zero() {}\n"
		"}\n"
		"int constructor_init_order = 0;\n"
		"int mark_constructor_init(int digit) {\n"
		"    constructor_init_order = constructor_init_order * 10 + digit;\n"
		"    return digit;\n"
		"}\n"
		"class InitOrderBase {\n"
		"public:\n"
		"    InitOrderBase(int ignored) {}\n"
		"}\n"
		"class InitOrderDerived : public InitOrderBase {\n"
		"public:\n"
		"    int first;\n"
		"    int second;\n"
		"    InitOrderDerived() : second(mark_constructor_init(2)), "
		"InitOrderBase(mark_constructor_init(9)), first(mark_constructor_init(1)) {}\n"
		"}\n"
		"struct Point {\n"
		"    int x = 1;\n"
		"    int y = 2;\n"
		"    int sum() { return x + y; }\n"
		"}\n"
		"class Holder<T> {\n"
		"public:\n"
		"    T value;\n"
		"    Holder(T initial) { value = initial; }\n"
		"    void set(T next) { value = next; }\n"
		"    T get() { return value; }\n"
		"}\n"
		"class LambdaCaptureBase {\n"
		"public:\n"
		"    int value;\n"
		"    LambdaCaptureBase(int initial) { value = initial; }\n"
		"    int read() { return value; }\n"
		"}\n"
		"class LambdaCaptureClass : public LambdaCaptureBase {\n"
		"public:\n"
		"    LambdaCaptureClass(int initial) : LambdaCaptureBase(initial) {}\n"
		"    fn()->int getter() { return fn()->int => value; }\n"
		"    fn()->int explicit_getter() { return fn()->int => this.value; }\n"
		"    fn()->int method_getter() { return fn()->int => read(); }\n"
		"    fn()->int mixed_getter(int amount) { int local = amount; "
		"return fn()->int => value + local; }\n"
		"    fn()->int shadow_getter(int value) { return fn()->int => value; }\n"
		"    fn(int)->int collision_getter() { "
		"return fn(int __vs_this)->int => this.value + __vs_this; }\n"
		"    fn()->int after_loop_getter() { "
		"for (int value = 0; value < 1; value++) {} return fn()->int => this.value; }\n"
		"    fn()->fn()->int nested_getter() { "
		"return fn()->fn()->int => fn()->int => value; }\n"
		"}\n"
		"struct LambdaCaptureStruct {\n"
		"    int value;\n"
		"    LambdaCaptureStruct(int initial) { value = initial; }\n"
		"    fn()->int getter() { return fn()->int => this.value; }\n"
		"}\n"
		"class LambdaCaptureBox<T> {\n"
		"public:\n"
		"    T value;\n"
		"    LambdaCaptureBox(T initial) { value = initial; }\n"
		"    fn()->T getter() { return fn()->T => value; }\n"
		"}\n"
		"class LambdaCaptureStatics {\n"
		"public:\n"
		"    static int value = 7;\n"
		"    static fn()->int getter() { "
		"return fn()->int => LambdaCaptureStatics.value; }\n"
		"}\n"
		"fn()->int make_lifetime_getter() { "
		"LambdaCaptureClass value = LambdaCaptureClass(42); return value.getter(); }\n"
		"T invoke_generic_ref<T>(fn(ref T)->T callback, ref T value) {\n"
		"    return callback(ref value);\n"
		"}\n"
		"int identity_generic_ref(ref int value) { return value; }\n"
		"class Animal { public: int base_field = 5; virtual int speak() { return 1; } "
		"int base_only(int result = 3) { return result; } }\n"
		"class Dog : public Animal { public: int speak() override { return 2; } "
		"int inherited_sum() { return base_field + base_only(); } }\n"
		"class AbstractBase { public: virtual int code() = 0; }\n"
		"class AbstractMid : public AbstractBase {}\n"
		"class Concrete : public AbstractMid { public: int code() override { return 11; } }\n"
		"class PrivateVirtual { private: virtual int hidden() { return 1; } "
		"public: int call_hidden() { return hidden(); } }\n"
		"class PrivateOverride : public PrivateVirtual { private: "
		"int hidden() override { return 2; } }\n"
		"class FinalVirtual final { public: virtual int stable() { return 13; } }\n"
		"shared<T> share_value<T>(T value) { return shared<T>(value); }\n"
		"shared<T> keep_shared<T>(handle<T> value) { return value; }\n"
		"shared<U> forward_shared<U>(handle<U> value) { return keep_shared<U>(value); }\n"
		"shared<T> forward_same_shared<T>(handle<T> value) { return keep_shared<T>(value); }\n"
		"bool same_shared<T>(shared<T> first, handle<T> second) { return first == second; }\n"
		"int count_shared<T>(shared<T>... values) { return len(values); }\n"
		"using SharedAlias = shared<int>;\n"
		"int reference_state = 40;\n"
		"int& reference_value() { return reference_state; }\n"
		"long wide_reference_state = 40i64;\n"
		"long& wide_reference_value() { return wide_reference_state; }\n"
		"const long& wide_readonly_value() { return wide_reference_state; }\n"
		"void main(string[] args) {\n"
		"    checkLambdaStaticSupport();\n"
		"    auto function_capture=[copy=csharpCaptureAnswer, offset=1]()->int{return copy()+offset;};\n"
		"    assert(function_capture()==43);\n"
		"    checkCSharpFunctionValues();\n"
		"    assert(csharpGlobalFirst == 11 && csharpGlobalNext(99) == 12);\n"
		"    auto reference_snapshot = csharpReferenceSnapshot();\n"
		"    assert(reference_snapshot() == 1 && reference_snapshot() == 2);\n"
		"    assert(csharpStaticRetry() == 12 && csharpStaticRetry() == 13 && csharpStaticAttempts == 2);\n"
		"    auto tuple_names = [Item2=1, Rest=2, ToString=3, Equals=4, GetHashCode=5, CompareTo=6, Item1=7, Item9=8, Item8=9]() mutable -> int { return ++Item2+Rest+ToString+Equals+GetHashCode+CompareTo+Item1+Item9+Item8; };\n"
		"    assert(tuple_names() == 46 && tuple_names() == 47);\n"
		"    int assignment_value=1; assert(10*(assignment_value=3)+2==32 && assignment_value==3);\n"
		"    assert((assignment_value+=2)==5 && ++assignment_value==6 && --assignment_value==5);\n"
		"    bool assignment_flag=false; assert(!(assignment_flag=true)==false && assignment_flag);\n"
		"    unsigned int assignment_unsigned=1u; assert(++assignment_unsigned==2u && --assignment_unsigned==1u);\n"
		"    double assignment_float=2.0; assert((assignment_float+=0.5)*2.0==5.0 && assignment_float==2.5);\n"
		"    fn()->void assignment_action=[&assignment_value]()->void => assignment_value=9; assignment_action(); assert(assignment_value==9);\n"
		"    assert(alias_identity<int>(7) == 7);\n"
		"    std::attribute<int> attribute_value=std::attribute<int>();\n"
		"    std::attribute<int> attribute_empty=std::attribute<int>();\n"
		"    int attribute_output=19;\n"
		"    assert(attribute_value.has_same_configuration(ref attribute_empty));\n"
		"    assert(!attribute_value.try_get(ref attribute_output) && attribute_output==19);\n"
		"    attribute_value.set(3); std::attribute<int> attribute_copy=attribute_value;\n"
		"    assert(attribute_value.has_same_configuration(ref attribute_copy));\n"
		"    std::attribute<int> attribute_separate=std::attribute<int>(3);\n"
		"    assert(!attribute_copy.has_same_configuration(ref attribute_separate));\n"
		"    assert(attribute_value.try_get(ref attribute_output) && attribute_output==3);\n"
		"    attribute_value.set(4);\n"
		"    assert(!attribute_value.has_same_configuration(ref attribute_copy) && attribute_copy.get()==3);\n"
		"    attribute_value.bind_optional([]()->optional<int>{return optional<int>();});\n"
		"    assert(!attribute_value.try_get(ref attribute_output) && attribute_output==3);\n"
		"    attribute_value.bind([]()->int{return 8;});\n"
		"    assert(attribute_value.try_get(ref attribute_output) && attribute_output==8);\n"
		"    attribute_value.reset(); assert(attribute_value.has_same_configuration(ref attribute_empty));\n"
		"    if (len(args) == 1 && args[0] == \"console-color\") {\n"
		"        std::console::set_color(\"invalid\", \"default\"); return;\n"
		"    }\n"
		"    if (len(args) == 1 && args[0] == \"console-line\") {\n"
		"        std::console::clear_line(\"invalid\"); return;\n"
		"    }\n"
		"    if (len(args) == 1 && args[0] == \"console-position\") {\n"
		"        std::console::move_cursor(0, 32767); return;\n"
		"    }\n"
		"    assert(!std::console::is_output_terminal());\n"
		"    assert(!std::console::supports_color() && !std::console::supports_cursor() && !std::console::supports_clear());\n"
		"    assert(std::console::width() == 0 && std::console::height() == 0);\n"
		"    assert(!std::console::set_color(\"bright_red\", \"default\") && !std::console::reset_style());\n"
		"    assert(!std::console::set_cursor_visible(false) && !std::console::move_cursor(5, 3));\n"
		"    assert(!std::console::clear() && !std::console::clear_line(\"after\"));\n"
		"    assert(std::sqrt(9.0) == 3.0);\n"
		"    fn(ref long)->void alias_bump = [](long& n)->void { n += 1i64; };\n"
		"    fn(ref int64)->void canonical_bump = alias_bump;\n"
		"    int64 alias_number = 7; canonical_bump(ref alias_number); assert(alias_number == 8);\n"
		"    fn(ref unsigned long)->void unsigned_bump = [](unsigned long& n)->void { n += 1u64; };\n"
		"    fn(ref uint64)->void canonical_unsigned = unsigned_bump;\n"
		"    uint64 alias_unsigned = 7u64; canonical_unsigned(ref alias_unsigned); assert(alias_unsigned == 8u64);\n"
		"    fn(const ref long)->long alias_read = [](const long& n)->long { return n; };\n"
		"    fn(const ref int64)->int64 canonical_read = alias_read; assert(canonical_read(ref alias_number) == 8);\n"
		"    fn(ref array<WideElement>)->void alias_push = [](array<long>& a)->void { a.push(9i64); };\n"
		"    fn(ref array<int64>)->void canonical_push = alias_push;\n"
		"    array<int64> alias_array = array<int64>(); canonical_push(ref alias_array); assert(alias_array[0] == 9);\n"
		"    fn(ref pair<long,int>)->void pair_bump = [](pair<int64,int>& p)->void { p.first += 1i64; };\n"
		"    fn(ref pair<int64,int>)->void canonical_pair_bump = pair_bump;\n"
		"    pair<int64,int> alias_pair = pair<int64,int>(); alias_pair.first = 7i64;\n"
		"    canonical_pair_bump(ref alias_pair); assert(alias_pair.first == 8i64);\n"
		"    fn(const ref pair<long,int>)->long pair_read = [](const pair<int64,int>& p)->long { return p.first; };\n"
		"    fn(const ref pair<int64,int>)->int64 canonical_pair_read = pair_read;\n"
		"    assert(canonical_pair_read(ref alias_pair) == 8i64);\n"
		"    fn(ref array<pair<long,int>>)->void pair_push = [](array<pair<int64,int>>& a)->void { a.push(pair<int64,int>()); };\n"
		"    fn(ref array<pair<int64,int>>)->void canonical_pair_push = pair_push;\n"
		"    array<pair<int64,int>> alias_pairs = array<pair<int64,int>>(); canonical_pair_push(ref alias_pairs);\n"
		"    assert(alias_pairs[0].first == 0i64);\n"
		"    fn()->long& original_reference = wide_reference_value;\n"
		"    fn()->int64& canonical_reference = original_reference;\n"
		"    int64& wide_alias = canonical_reference(); wide_alias += 1i64; assert(wide_reference_state == 41i64);\n"
		"    fn()->const long& original_readonly = wide_readonly_value;\n"
		"    fn()->const int64& canonical_readonly = original_readonly; assert(canonical_readonly() == 41i64);\n"
		"    fn(double)->double alias_root = std::sqrt;\n"
		"    assert(alias_root(9.0) == 3.0);\n"
		"    array<int> alias_values = {3, 1, 2};\n"
		"    std::algorithm::sort(alias_values);\n"
		"    assert(alias_values[0] == 1 && alias_values[2] == 3);\n"
		"    auto alias_delegate = std::delegate<int>();\n"
		"    alias_delegate.bind(fn()->int => 42);\n"
		"    assert(alias_delegate.execute() == 42);\n"
		"    if (len(args) == 1 && args[0] == \"negative-fraction\") {\n"
		"        double negative = -0.5; unsigned int invalid = negative; print(invalid); return;\n"
		"    }\n"
		"    if (len(args) == 1 && args[0] == \"negative-compound\") {\n"
		"        unsigned int invalid = 0u; invalid += -0.5; print(invalid); return;\n"
		"    }\n"
		"    if (len(args) == 1 && args[0] == \"remainder-zero\") {\n"
		"        double dividend = 1.0; double zero = 0.0; print(dividend % zero); return;\n"
		"    }\n"
		"    if (len(args) == 1 && args[0] == \"remainder-compound\") {\n"
		"        float dividend = 1.0f; float zero = 0.0f; dividend %= zero; print(dividend); return;\n"
		"    }\n"
		"    if (len(args) == 1 && args[0] == \"remainder-nonzero\") {\n"
		"        float dividend = 5.5f; float divisor = 2.0f; print(dividend % divisor); return;\n"
		"    }\n"
		"    shared<int> shared_owner = shared(7);\n"
		"    handle<int> shared_alias = shared_owner;\n"
		"    std::shared<int> shared_std = std::handle<int>(0);\n"
		"    shared_std = shared_alias; shared_std.value += 1;\n"
		"    assert(shared_owner.value == 8 && shared_owner == shared_alias);\n"
		"    SharedAlias shared_type_alias = shared_owner;\n"
		"    assert(shared_type_alias == shared_owner);\n"
		"    auto shared_auto = shared(3); assert(shared_auto.value == 3);\n"
		"    assert(shared_auto.keep_shared() == shared_auto && keep_shared(shared_auto) == shared_auto);\n"
		"    assert(keep_shared<int>(nullptr) == nullptr);\n"
		"    assert(!same_shared(nullptr, shared_auto) && !same_shared(shared_auto, nullptr));\n"
		"    assert(!shared_auto.same_shared(nullptr));\n"
		"    assert(count_shared<int>(nullptr, shared_auto) == 2);\n"
		"    assert(forward_shared(shared_auto) == shared_auto && forward_same_shared(shared_auto) == shared_auto);\n"
		"    shared<double> shared_widened = shared(7); assert(shared_widened.value == 7.0);\n"
		"    auto inferred_nested = shared(shared(2)); assert(inferred_nested.value.value == 2);\n"
		"    shared<shared<int>> contextual_nested = shared(shared(3)); assert(contextual_nested.value.value == 3);\n"
		"    Point shared_source = Point();\n"
		"    shared<Point> shared_point = share_value(shared_source);\n"
		"    handle<Point> shared_point_alias = shared_point;\n"
		"    assert(keep_shared(shared_point) == shared_point && keep_shared<Point>(shared_point) == shared_point);\n"
		"    assert(shared_point.keep_shared() == shared_point && shared_point.keep_shared<Point>() == shared_point);\n"
		"    shared_source.x = 99; shared_point_alias.value.x += 3;\n"
		"    assert(shared_point.value.x == 4 && shared_source.x == 99);\n"
		"    shared<Point> shared_implicit = shared_source; shared_source.x = 0;\n"
		"    assert(shared_implicit.value.x == 99);\n"
		"    shared<handle<int>> shared_nested = handle<shared<int>>(shared_owner);\n"
		"    assert(shared_nested.value == shared_alias);\n"
		"    shared<handle<int>> shared_implicit_nested = shared_owner;\n"
		"    assert(shared_implicit_nested.value == shared_owner);\n"
		"    shared<string> shared_text = shared<string>();\n"
		"    assert(shared_text != nullptr && shared_text.value == \"\");\n"
		"    shared<Counter> shared_class = shared<Counter>();\n"
		"    assert(shared_class != nullptr && shared_class.value == nullptr);\n"
		"    shared<multimap<string,int>> shared_map = shared<multimap<string,int>>();\n"
		"    shared_map.value.add(\"k\", 2); assert(len(shared_map.value) == 1);\n"
		"    shared_point_alias = nullptr; assert(shared_point != nullptr);\n"
		"    print(\"你好，世界\");\n"
		"    string escaped_byte = \"\\xFF\"; assert(len(escaped_byte) == 1);\n"
		"    assert(\"A\" + escaped_byte == \"A\\xFF\");\n"
		"    assert(\"\\xC3\\xA9\" == \"\\u00E9\");\n"
		"    assert(\"\\xF0\\x9F\\x98\\x80\" == \"\\U0001F600\");\n"
		"    assert(\"\\xC2\\x85\" == \"\\u0085\");\n"
		"    assert(\"\\xE2\\x80\\xA8\" == \"\\u2028\");\n"
		"    assert(\"\\xE2\\x80\\xA9\" == \"\\u2029\");\n"
		"    assert(len(\"\\xC0\\xAF\") == 2);\n"
		"    assert(\"\\xC0\\xAF\" == \"\\xC0\" + \"\\xAF\");\n"
		"    assert(r\"raw\\n\" == \"raw\\\\n\");\n"
		"    int escaped_case = 0; switch (escaped_byte) {\n"
		"    case \"\\xFF\": escaped_case = 1; break; default: break; }\n"
		"    assert(escaped_case == 1);\n"
		"    char character = 'A', high_character = '\\xFF';\n"
		"    print(to_int(character)); print(to_int(high_character));\n"
		"    int grouped_first = 1, grouped_second = grouped_first + 2;\n"
		"    fn()->int grouped_capture = [&]() -> int => grouped_second; print(grouped_capture());\n"
		"    fn()->int grouped_body = [=]() -> int { int local_first = grouped_second, "
		"local_second = local_first + 1; return local_second; }; print(grouped_body());\n"
		"    int group_storage = 4; ref int group_left = group_storage, group_right = group_left;\n"
		"    group_right = 9; print(group_storage);\n"
		"    int do_count = 0; do { do_count++; } while (do_count < 2); print(do_count);\n"
		"    int list_left = 0, list_right = 0, list_order = 0;\n"
		"    for (list_left = 1, list_right = list_left + 1; list_left < 4; "
		"list_left++, list_right += list_left) { list_order += list_left + list_right; }\n"
		"    print(list_order);\n"
		"    int grouped_total = 0; for (int group_index = 0, group_end = 3; "
		"group_index < group_end; group_index++) { grouped_total += group_index; }\n"
		"    print(grouped_total);\n"
		"    map<char, int> character_scores = {'A': 7}; print(character_scores['A']);\n"
		"    print(read_block_shadow_initializer());\n"
		"    print(read_for_shadow_initializer());\n"
		"    ShadowField shadow_field = ShadowField(20); print(shadow_field.next());\n"
		"    print(read_lambda_parameter_shadow());\n"
		"    int lock = 1; { int lock = lock + 1; int lock__vs_0 = 3;\n"
		"        print(lock + lock__vs_0); }\n"
		"    Counter counter = Counter(2); print(counter.add(3));\n"
		"    ConstHolder const_holder = ConstHolder(4); assert(const_holder.total() == 9);\n"
		"    GenericFieldBox<int> generic_field = GenericFieldBox<int>(7);\n"
		"    assert(generic_field.value == 7);\n"
		"    assert(generic_name_collision<int>(7) == 7);\n"
		"    assert(generic_vararg_name_collision<int>(1, 2) == 2);\n"
		"    GenericMemberNameCollision<int> generic_names = "
		"GenericMemberNameCollision<int>(8, 9);\n"
		"    assert(generic_names.payload == 8 && generic_names.T == 9);\n"
		"    GenericMethodNameCollision<int> generic_method_names = "
		"GenericMethodNameCollision<int>();\n"
		"    assert(generic_method_names.payload == 0 && generic_method_names.T() == 10);\n"
		"    GenericRecordNameCollision<int> generic_record_names = "
		"GenericRecordNameCollision<int>();\n"
		"    assert(generic_record_names.payload == 0);\n"
		"    RecordMemberNameCollision record_names = RecordMemberNameCollision();\n"
		"    record_names.Equals = 3;\n"
		"    RecordMemberNameCollision record_names_copy = record_names;\n"
		"    fn()->int record_name_getter = record_names.getter();\n"
		"    assert(record_names.Equals == 3 && record_names.GetHashCode() == 4);\n"
		"    assert(record_names.total() == 7 && record_name_getter() == 3);\n"
		"    assert(record_names == record_names_copy);\n"
		"    RecordOperatorNameCollision operator_names = RecordOperatorNameCollision();\n"
		"    assert(RecordOperatorNameCollision::op_Equality(operator_names, operator_names));\n"
		"    assert(!RecordOperatorNameCollision::op_Inequality(operator_names, operator_names));\n"
		"    GenericRecordOperatorNameCollision<int, int> generic_operator_names = "
		"GenericRecordOperatorNameCollision<int, int>();\n"
		"    assert(generic_operator_names.left == 0 && generic_operator_names.right == 0);\n"
		"    ObjectMemberNameCollision object_names = ObjectMemberNameCollision(5);\n"
		"    assert(object_names.Equals == 5 && object_names.GetHashCode() == 6);\n"
		"    assert(object_names.total() == 11);\n"
		"    FieldInitDefaults field_defaults = FieldInitDefaults(9);\n"
		"    assert(field_defaults.explicit_zero == 0 && field_defaults.implicit_zero == 0);\n"
		"    assert(len(field_defaults.explicit_text) == 0);\n"
		"    assert(field_defaults.mutable_value == 9 && field_defaults.token.value == 4);\n"
		"    InitOrderDerived init_order = InitOrderDerived();\n"
		"    assert(constructor_init_order == 912);\n"
		"    Point point = Point(); print(point.sum());\n"
		"    print(counter.choose(1)); print(counter.choose(\"x\"));\n"
		"    print(counter.with_default()); print(Counter::static_value());\n"
		"    Holder<int> holder = Holder<int>(9); print(holder.get()); "
		"holder.set(10); auto held = holder.get(); auto held_field = holder.value; "
		"assert(held == 10 && held_field == 10);\n"
		"    Animal animal = Dog(); print(animal.speak());\n"
		"    Dog dog = Dog(); print(dog.base_only()); print(dog.base_field); "
		"assert(dog.inherited_sum() == 8);\n"
		"    AbstractBase abstract_value = Concrete(); assert(abstract_value.code() == 11);\n"
		"    PrivateVirtual private_virtual = PrivateOverride(); "
		"assert(private_virtual.call_hidden() == 2); "
		"assert(FinalVirtual().stable() == 13);\n"
		"    int& state_alias = reference_value(); state_alias += 2; print(reference_state);\n"
		"    int& field_alias = counter.value_ref(); field_alias += 1; print(counter.value);\n"
		"    const int& readonly_alias = reference_value(); print(readonly_alias);\n"
		"    int generic_ref_value = 17;\n"
		"    assert(invoke_generic_ref<int>(identity_generic_ref, ref generic_ref_value) == 17);\n"
		"    fn()->int& reference_callable = reference_value;\n"
		"    int& callable_alias = reference_callable(); callable_alias += 1; print(reference_state);\n"
		"    print(exception_flow()); print(exception_return());\n"
		"    print(formatf(\"d=%+06d x=%#08X f=%.2f\", -42, 255U, 3.14159));\n"
		"    printf(\"s=%-5.3s c=%c %%\", \"abcdef\", 65);\n"
		"    printf(\"g=%#.0g e=%010.3e\", 12.0, 1.5);\n"
		"    printf(\"float-flags=[% G]/[%-#12.4g]/[% f]\", 1.5, 12.0, -0.0);\n"
		"    printf(\"g-boundaries=[%.3g]/[%.3g]/[%.1g]/[%.3g]/[%.3g]\",\n"
		"        999.5, 999.4, 9.99, 0.00009995, 0.00009994);\n"
		"    print(len(formatf(\"%.1024f\", 1.0)));\n"
		"    print(len(formatf(\"%.1s\", \"世界\")));\n"
		"    print(len(formatf(\"%.3s\", \"世界\")));\n"
		"    print(formatf(\"%.3s\", \"世界\"));\n"
		"    print(len(formatf(\"%c\", 255)));\n"
		"    printf(\"raw=%c\", 255);\n"
		"    print(len(\"𐂀\")); print(\"𐂀\");\n"
		"    array<string> printf_args = {\"7\", \"255\"};\n"
		"    printf(\"printf-array=%d/%x\", printf_args);\n"
		"    set<int> membership = set<int>(); membership.add(3);\n"
		"    print(3 in membership); print(4 in membership);\n"
		"    int8 narrow_membership = 3; print(narrow_membership in membership);\n"
		"    map<string,int> membership_map = map<string,int>(); membership_map[\"a\"] = 1;\n"
		"    print(\"a\" in membership_map); print(\"b\" in membership_map);\n"
		"    ordered_map<string,int> membership_ordered = ordered_map<string,int>();\n"
		"    membership_ordered[\"a\"] = 1; print(\"a\" in membership_ordered);\n"
		"    print(in_left() in in_right(membership)); print(in_order);\n"
		"    print(module_a_value()); print(module_b_value());\n"
		"    inferred_global.push(6); print(inferred_global[0]);\n"
		"    auto inferred_local = array<int>(); inferred_local.push(7); print(inferred_local[0]);\n"
		"    int inferred_total = 0; for (auto index = 0; index < 2; index++) inferred_total += index; print(inferred_total);\n"
		"    print(\"value={}\", 7);\n"
		"    print(len(\"世界\"));\n"
		"    print(len(L\"世界\"));\n"
		"    print(string_kind(\"text\"));\n"
		"    print(string_kind(L\"text\"));\n"
		"    print(L\"a\" < L\"b\");\n"
		"    print(id<string>(\"generic\"));\n"
		"    array<string> format_args = {\"8\"};\n"
		"    print(\"array={}\", format_args);\n"
		"    print(to_int(3.9));\n"
		"    print(to_int(-3.9));\n"
		"    print(to_unsigned(7));\n"
		"    assert(to_int(1.9999999999999998) == 1);\n"
		"    assert(to_int(-1.9999999999999998) == -1);\n"
		"    assert(to_int(2147483647.9999998) == 2147483647);\n"
		"    assert(to_int(2147483520.0f) == 2147483520);\n"
		"    assert(to_unsigned(4294967295.9999995) == 4294967295u);\n"
		"    assert(to_int64(9007199254740991.0) == 9007199254740991L);\n"
		"    assert(to_int64(-9007199254740991.0) == -9007199254740991L);\n"
		"    assert(to_int64(9223372036854774784.0) == 9223372036854774784L);\n"
		"    assert(to_int64(-9223372036854775808.0) == -9223372036854775808L);\n"
		"    assert(to_uint64(18446744073709549568.0) == 18446744073709549568UL);\n"
		"    int64 rounding_source = 4611686293305294849L;\n"
		"    uint64 unsigned_rounding_source = 9223372586610589697UL;\n"
		"    float rounded_assignment = rounding_source;\n"
		"    assert(to_float(rounding_source) == to_float(to_double(rounding_source)));\n"
		"    assert(rounded_assignment == to_float(to_double(rounding_source)));\n"
		"    assert(to_float(unsigned_rounding_source) == to_float(to_double(unsigned_rounding_source)));\n"
		"    float zero_float = 0.0f;\n"
		"    assert(zero_float + rounding_source == rounded_assignment);\n"
		"    zero_float += rounding_source; assert(zero_float == rounded_assignment);\n"
		"    assert(rounded_assignment != rounding_source && rounded_assignment < rounding_source);\n"
		"    float comparison_high = 4611686568183201792.0f;\n"
		"    assert(comparison_high != rounding_source && comparison_high > rounding_source);\n"
		"    double double_rhs = 0.5; float compound_float = 1.0f;\n"
		"    compound_float += double_rhs; assert(compound_float == 1.5f);\n"
		"    compound_float *= double_rhs; assert(compound_float == 0.75f);\n"
		"    compound_float /= double_rhs; assert(compound_float == 1.5f);\n"
		"    unsigned int truncated_unsigned = 4294967295.9999995; assert(truncated_unsigned == 4294967295u);\n"
		"    print(greet());\n"
		"    print(choose(1) + choose(\"x\"));\n"
		"    print(A::B_C());\n"
		"    print(A_B::C());\n"
		"    print(default_id(5)); print(default_id<int>(8));\n"
		"    int postfix = 1; print(postfix++); print(postfix);\n"
		"    int numeric_source = 127; int8 narrow = numeric_source;\n"
		"    double fractional = 12.75; int truncated = fractional; uint8 bits = 250u;\n"
		"    print(narrow); print(truncated); print(~bits);\n"
		"    uint8 shifted_byte = 3u; shifted_byte <<= 1; print(shifted_byte);\n"
		"    unsigned int wrapped = 4294967295u; wrapped += 1u; print(wrapped);\n"
		"    int shifted_int = 1; shifted_int <<= 3; print(shifted_int);\n"
		"    double quotient = 8.0; quotient /= 2.0; print(to_int(quotient));\n"
		"    int8 min_left = -5; int8 min_right = 2; print(min(min_left, min_right));\n"
		"    print(abs(4294967295u));\n"
		"    double precise = 1.2345678901234567; float approximate = 0.1f;\n"
		"    print(precise); print(approximate); print(to_string(precise));\n"
		"    print(to_int(std::sqrt(81.0))); print(to_int(std::sin(0.0))); print(to_int(std::cos(0.0)));\n"
		"    print(to_int(std::tan(0.0))); print(to_int(std::floor(3.9))); print(to_int(std::ceil(3.1)));\n"
		"    double huge = 1e308; double infinity = huge * huge; double invalid = std::sqrt(-1.0);\n"
		"    print(invalid); print(std::floor(invalid)); print(std::sin(infinity)); print(std::floor(infinity));\n"
		"    print(min(invalid, 2.0)); print(min(2.0, invalid));\n"
		"    print(max(invalid, 2.0)); print(max(2.0, invalid));\n"
		"    double negative_zero = -0.0; print(abs(invalid)); print(negative_zero); print(abs(negative_zero));\n"
		"    print(sum_values());\n"
		"    print(variadic_sum(1, 2, 3));\n"
		"    print(len(range(4)));\n"
		"    print(classify(2));\n"
		"    print(id<int>(len(args)));\n"
		"    array<int> empty = array<int>();\n"
		"    assert(length_kind(len(empty)) == 2, \"length type\");\n"
		"    assert(length_kind(empty.capacity()) == 2, \"capacity type\");\n"
		"    assert(len(range(4i64)) == 4i64, \"range count type\");\n"
		"    empty.reserve(4); std::print(empty.capacity() >= 4);\n"
		"    empty.push(7);\n"
		"    int read_index = read_ref(ref empty[0]);\n"
		"    print(len(empty));\n"
		"    print(empty.extension_last());\n"
		"    print(empty.extension_size(2));\n"
		"    int extension_value = 5; extension_value.extension_bump(); print(extension_value);\n"
		"    int extension_collision = 5; print(extension_collision.push());\n"
		"    empty.push(8); empty.remove_at(0); print(empty[0]);\n"
		"    print(empty);\n"
		"    print(to_int(Warm));\n"
		"    Tone tone = Warm; print(Warm); print(tone); print(\"tone=\" + Warm);\n"
		"    map<string, int> scores = {\"b\": 2, \"a\": 1};\n"
		"    print(scores.has(\"a\"));\n"
		"    print(scores[\"b\"]);\n"
		"    print(len(scores.keys()));\n"
		"    for (int score : scores) print(score);\n"
		"    int entry_total = 0;\n"
		"    for (pair<string, int> entry : scores) { print(entry); entry_total += entry.second; }\n"
		"    print(entry_total);\n"
		"    ordered_map<string, int> ordered = ordered_map<string, int>();\n"
		"    ordered.reserve(8);\n"
		"    ordered[\"b\"] = 2;\n"
		"    ordered[\"a\"] = 1;\n"
		"    print(ordered.keys()[0]);\n"
		"    print(ordered.keys()[1]);\n"
		"    ordered[\"b\"] = 20;\n"
		"    print(ordered.keys()[0]);\n"
		"    ordered.remove(\"b\");\n"
		"    ordered[\"b\"] = 3;\n"
		"    print(ordered.keys()[0]);\n"
		"    print(ordered.keys()[1]);\n"
		"    set<int> unique = {1, 2, 2};\n"
		"    print(len(unique));\n"
		"    queue<int> pending = {4, 5};\n"
		"    pending.push(6);\n"
		"    print(pending.front());\n"
		"    print(pending.pop());\n"
		"    stack<int> history = {8, 9};\n"
		"    print(history.top());\n"
		"    for (int value : history.values()) std::print(value);\n"
		"    for (int value : history) std::print(value);\n"
		"    print(history.pop());\n"
		"    deque<int> ends = {11, 12};\n"
		"    ends.push_front(10);\n"
		"    print(ends.pop_back());\n"
		"    array<int> growing = {1};\n"
		"    for (int value : growing) { growing.push(2); print(value); }\n"
		"    print(len(growing));\n"
		"    using LocalInt = int;\n"
		"    { using LocalInt = string; LocalInt nested = \"nested\"; print(nested); }\n"
		"    LocalInt local = 13;\n"
		"    print(local);\n"
		"    print(switch_scopes(1));\n"
		"    print(switch_text(\"yes\"));\n"
		"    switch (true ? \"yes\" : \"no\") { case \"yes\": print(1); break; default: print(0); break; }\n"
		"    print(Tools::bump());\n"
		"    print(Tools::read_level(Tools::Level::High));\n"
		"    print(Tools::Level::High);\n"
		"    print(Tools::read_global());\n"
		"    print(Tools::global_value);\n"
		"    print(Tools::with_default());\n"
		"    int qualified_source = 10; print(Tools::narrow_arg(qualified_source));\n"
		"    print(copied_seed);\n"
		"    pair<int,string> pair = pair<int,string>();\n"
		"    pair<int> user_pair = pair<int>(); user_pair.custom = 9; assert(user_pair.custom == 9);\n"
		"    ordered_map<int64,int> wide_keys={1:3}; for(pair<long,int> entry:wide_keys){assert(entry.second==3);}\n"
		"    print(pair.first); print(pair.second == \"\");\n"
		"    pair.first = 3; pair.second = \"pair\"; pair<int,string> pair_copy = pair;\n"
		"    print(pair == pair_copy); pair_copy.second = \"changed\"; print(pair == pair_copy); print(pair.second);\n"
		"    pair<array<int>,string> pair_defaults = pair<array<int>,string>();\n"
		"    pair_defaults.first.push(8); print(pair_defaults.first[0]); print(pair_defaults.second == \"\");\n"
		"    pair<array<int>,int> generic_defaults = generic_pair<array<int>>();\n"
		"    generic_defaults.first.push(13); print(generic_defaults.first[0]);\n"
		"    pair<wstring,int> generic_wide = generic_pair<wstring>(); print(generic_wide.first == L\"\");\n"
		"    array<int> array_a = array<int>(); array<int> array_b = array<int>();\n"
		"    print(array_a == array_b); array_b = array_a; array_a.push(7); print(array_a == array_b);\n"
		"    map<string,int> map_a = map<string,int>(); map<string,int> map_b = map<string,int>();\n"
		"    print(map_a == map_b); map_b = map_a; map_a[\"x\"] = 8; print(map_a == map_b);\n"
		"    set<int> set_a = set<int>(); set<int> set_b = set<int>();\n"
		"    print(set_a == set_b); set_b = set_a; set_a.add(9); print(set_a == set_b);\n"
		"    deque<int> deque_a = deque<int>(); deque<int> deque_b = deque<int>();\n"
		"    print(deque_a == deque_b); deque_b = deque_a; deque_a.push_back(10); print(deque_a == deque_b);\n"
		"    set<Tools::Level> levels = {Tools::Level::High}; print(levels.has(Tools::Level::High));\n"
		"    int referenced = 40; increment(ref referenced); print(referenced);\n"
		"    int overload_value = 1;\n"
		"    assert(ref_overload(overload_value) == 1);\n"
		"    assert(ref_overload(ref overload_value) == 2);\n"
		"    array<int> overload_values = {1};\n"
		"    assert(readonly_overload(overload_values[0]) == 3);\n"
		"    assert(readonly_overload(ref overload_values[0]) == 4);\n"
		"    const int overload_const = 1;\n"
		"    assert(cv_ref_overload(ref overload_value) == 5);\n"
		"    assert(cv_ref_overload(ref overload_const) == 6);\n"
		"    assert(cv_ref_overload(ref overload_shadow) == 6);\n"
		"    int overload_shadow = 2;\n"
		"    assert(cv_ref_overload(ref overload_shadow) == 5);\n"
		"    assert(overload_value.extension_ref_kind() == 8);\n"
		"    assert((overload_value + 1).extension_ref_kind() == 7);\n"
		"    assert(make_overload_values()[0].extension_ref_kind() == 7);\n"
		"    array<int> algorithm_values = {9, 2, 7, 2, 5};\n"
		"    std::algorithm::sort(algorithm_values); print(algorithm_values[0]);\n"
		"    print(std::algorithm::binary_search(algorithm_values, 7));\n"
		"    print(std::numeric::sum(algorithm_values));\n"
		"    print(std::numeric::accumulate(algorithm_values, 10));\n"
		"    print(std::numeric::average(algorithm_values));\n"
		"    array<int8> small_values = {1, 2}; print(std::numeric::sum(small_values));\n"
		"    array<string> pieces = {\"vero\", \"script\"};\n"
		"    print(std::numeric::accumulate(pieces, \"\"));\n"
		"    assert(std::strings::starts_with(\"abc\",\"ab\") && std::strings::ends_with(\"abc\",\"bc\"));\n"
		"    assert(std::strings::last_find(\"aaa\",\"aa\")==1); assert(std::strings::trim(\"\\v\\f a \\r\\n\")==\"a\");\n"
		"    assert(std::strings::repeat(\"ab\",3)==\"ababab\"); assert(std::strings::replace(\"aaaaa\",\"aa\",\"x\")==\"xxa\");\n"
		"    assert(std::clamp_int(9,0,8)==8); assert(format(\"{:x}\",18446744073709551615u64)==\"ffffffffffffffff\");\n"
		"    array<unsigned int> random_first = std::random::make_engine(123u);\n"
		"    array<unsigned int> random_second = std::random::make_engine(123u);\n"
		"    print(std::random::next_uint(random_first) == std::random::next_uint(random_second));\n"
		"    array<int> algorithm_copy = std::algorithm::copy(algorithm_values);\n"
		"    print(std::algorithm::contains(algorithm_copy, 7));\n"
		"    print(std::algorithm::equal(algorithm_values, algorithm_copy));\n"
		"    print(std::algorithm::is_sorted(algorithm_copy));\n"
		"    print(std::algorithm::min_index(algorithm_copy));\n"
		"    print(std::algorithm::max_index(algorithm_copy));\n"
		"    std::algorithm::rotate(algorithm_copy, 2); print(algorithm_copy[0]);\n"
		"    std::algorithm::fill(algorithm_copy, 6); print(algorithm_copy[4]);\n"
		"    array<int> shuffled = {1, 2, 3, 4};\n"
		"    std::algorithm::shuffle(shuffled, random_first); std::algorithm::sort(shuffled);\n"
		"    array<int> shuffled_expected = {1, 2, 3, 4};\n"
		"    print(std::algorithm::equal(shuffled, shuffled_expected));\n"
		"    unsigned int bounded = std::random::next_bounded_uint(random_first, 7u); print(bounded < 7u);\n"
		"    int ranged = std::random::next_int(random_first, -3, 4); print(ranged >= -3 && ranged < 4);\n"
		"    double ranged_double = std::random::next_double_range(random_first, 2.0, 3.0);\n"
		"    print(ranged_double >= 2.0 && ranged_double < 3.0);\n"
		"    array<double> large_values = {1e308, 1e308};\n"
		"    assert(std::numeric::average(large_values) == 1e308);\n"
		"    array<double> opposed_values = {1e308, -1e308};\n"
		"    assert(std::numeric::average(opposed_values) == 0.0);\n"
		"    array<unsigned int> largest_bound = std::random::make_engine(4071982377u);\n"
		"    assert(std::random::next_bounded_uint(largest_bound, 4294967295u) == 0u);\n"
		"    double narrow_limit = 1.0000000000000002;\n"
		"    double narrow_sample = std::random::next_double_range(random_first, 1.0, narrow_limit);\n"
		"    assert(narrow_sample >= 1.0 && narrow_sample < narrow_limit);\n"
		"    double wide_sample = std::random::next_double_range(random_first, -1e308, 1e308);\n"
		"    assert(wide_sample >= -1e308 && wide_sample < 1e308);\n"
		"    assert(std::filesystem::dirname(\"C:/foo.txt\") == \"C:/\");\n"
		"    assert(std::filesystem::dirname(\"é/foo.txt\") == \"é\");\n"
		"    assert(std::filesystem::basename(\"é/foo.txt\") == \"foo.txt\");\n"
		"    assert(std::filesystem::join_path(\"left\", \"/root\") == \"/root\");\n"
		"    assert(std::filesystem::join_path(\"C:\", \"relative\") == \"C:relative\");\n"
		"    delegate<int, int> standard_delegate = delegate<int, int>();\n"
		"    standard_delegate.bind(twice_callable);\n"
		"    assert(standard_delegate.execute(4) == 8);\n"
		"    standard_delegate.unbind();\n"
		"    assert(!standard_delegate.is_bound() && !standard_delegate.execute_if_bound(4));\n"
		"    delegate<void> void_delegate = delegate<void>();\n"
		"    fn()->void void_handler = fn() -> void { };\n"
		"    void_delegate.bind(void_handler);\n"
		"    assert(void_delegate.is_bound());\n"
		"    void_delegate.execute();\n"
		"    void_delegate.unbind();\n"
		"    assert(!void_delegate.is_bound() && !void_delegate.execute_if_bound());\n"
		"    delegate<void, int> void_arg_delegate = delegate<void, int>();\n"
		"    void_arg_delegate.bind(fn(int value) -> void { assert(value == 4); });\n"
		"    void_arg_delegate.execute(4);\n"
		"    delegate<void, int, int> void_two_args = delegate<void, int, int>();\n"
		"    void_two_args.bind(fn(int a, int b) -> void { assert(a + b == 5); });\n"
		"    void_two_args.execute(2, 3);\n"
		"    delegate<void, int, int, int> void_three_args = delegate<void, int, int, int>();\n"
		"    void_three_args.bind(fn(int a, int b, int c) -> void { assert(a + b + c == 6); });\n"
		"    void_three_args.execute(1, 2, 3);\n"
		"    std::multicast_delegate listeners = std::multicast_delegate();\n"
		"    fn()->void listener = fn()->void { assert(true); };\n"
		"    listeners += listener;\n"
		"    assert(listeners.count() == 1);\n"
		"    listeners.broadcast();\n"
		"    listeners -= listener;\n"
		"    assert(listeners.count() == 0);\n"
		"    int snapshot_seed = 1;\n"
		"    fn()->int explicit_snapshot = [snapshot_seed]() mutable ->int { snapshot_seed += 1; return snapshot_seed; };\n"
		"    fn()->int implicit_snapshot = fn() mutable ->int { snapshot_seed += 1; return snapshot_seed; };\n"
		"    assert(explicit_snapshot() == 2 && explicit_snapshot() == 3);\n"
		"    assert(implicit_snapshot() == 2 && implicit_snapshot() == 3 && snapshot_seed == 1);\n"
		"    fn(int)->int shadow = fn(int snapshot_seed)->int { snapshot_seed += 1; return snapshot_seed; };\n"
		"    assert(shadow(9) == 10 && snapshot_seed == 1);\n"
		"    CaptureState state=CaptureState();\n"
		"    fn()->int field_capture=[state]() mutable ->int { state.n+=1; return state.n; };\n"
		"    assert(field_capture()==2 && field_capture()==3 && state.n==1);\n"
		"    fn()->int nested_capture=[state]() mutable ->int {\n"
		"        fn()->int child=[state]() mutable ->int { state.n+=1; return state.n; };\n"
		"        return child()+child();\n"
		"    };\n"
		"    assert(nested_capture()==5 && nested_capture()==5 && state.n==1);\n"
		"    pair<fn(int)->int,int> callable_pair = pair<fn(int)->int,int>();\n"
		"    callable_pair.first = twice_callable; callable_pair.second = 3;\n"
		"    fn(int)->int paired_callable = callable_pair.first;\n"
		"    assert(paired_callable(4) == 8 && callable_pair.second == 3);\n"
		"    int capture_value = 10;\n"
		"    fn(int)->int by_value = fn(int value) -> int => value + capture_value;\n"
		"    capture_value = 20; print(by_value(1));\n"
		"    fn()->int by_reference = [&]() -> int => capture_value;\n"
		"    capture_value = 30; print(by_reference());\n"
		"    fn()->void mutate_reference = [&]() -> void { capture_value += 2; };\n"
		"    mutate_reference(); print(capture_value);\n"
		"    int mixed_value = 1; int mixed_reference = 2;\n"
		"    fn()->int mixed_capture = [mixed_value, &mixed_reference]() -> int { mixed_reference += 1; return mixed_value + mixed_reference; };\n"
		"    mixed_value = 10; print(mixed_capture()); print(mixed_reference);\n"
		"    fn(int)->int method_value = twice_callable; print(method_value(4));\n"
		"    print(global_callable(10));\n"
		"    print(apply_callable(fn(value) => value + 1, 41));\n"
		"    fn(int)->int add_five = make_adder(5); print(add_five(7));\n"
		"    print((fn(int value) -> int => value * 3)(3));\n"
		"    int ref_callable_value = 5; fn(ref int)->void ref_callable = increment_callable;\n"
		"    ref_callable(ref ref_callable_value); print(ref_callable_value);\n"
		"    fn(ref int)->void ref_lambda = fn(ref int value) -> void { value += 1; };\n"
		"    ref_lambda(ref ref_callable_value); print(ref_callable_value);\n"
		"    multimap<string, int> grouped(); grouped.add(\"a\", 1).add(\"a\", 2); print(len(grouped)); print(len(grouped[\"a\"]));\n"
		"    multimap<string, int> initialized = {\"a\": {4, 5}, \"b\": {6}}; print(len(initialized[\"a\"]));\n"
		"    ordered_multimap<string, int> ordered_grouped(); ordered_grouped.add(\"b\", 2).add(\"a\", 1).add(\"b\", 3); print(ordered_grouped.keys()[0]); print(ordered_grouped[\"b\"][1]);\n"
		"    multiset<int> bag = {2, 1, 2}; print(len(bag)); print(bag.count(2)); bag.remove(2); print(bag.count(2));\n"
		"    ordered_multiset<string> ordered_bag = {\"b\", \"a\", \"b\"}; print(ordered_bag.values()[0]); print(ordered_bag.values()[1]);\n"
		"    priority_queue<int> priorities = {3, 1, 5, 2}; print(priorities.top()); print(priorities.pop()); priorities.push(9); print(priorities.values()[0]); print(priorities.values()[1]); print(priorities.values()[2]); print(priorities.values()[3]); print(priorities.pop());\n"
		"    LambdaCaptureClass live_capture = LambdaCaptureClass(1);\n"
		"    fn()->int live_getter = live_capture.getter();\n"
		"    fn()->int explicit_getter = live_capture.explicit_getter();\n"
		"    fn()->int method_getter = live_capture.method_getter();\n"
		"    fn()->int mixed_getter = live_capture.mixed_getter(5);\n"
		"    fn()->int shadow_getter = live_capture.shadow_getter(11);\n"
		"    fn(int)->int collision_getter = live_capture.collision_getter();\n"
		"    fn()->int after_loop_getter = live_capture.after_loop_getter();\n"
		"    fn()->fn()->int nested_factory = live_capture.nested_getter();\n"
		"    fn()->int nested_getter = nested_factory();\n"
		"    live_capture.value = 2;\n"
		"    print(live_getter()); print(explicit_getter()); print(method_getter());\n"
		"    print(mixed_getter()); print(shadow_getter()); print(collision_getter(5)); "
		"print(after_loop_getter()); print(nested_getter());\n"
		"    LambdaCaptureStruct struct_capture = LambdaCaptureStruct(3);\n"
		"    fn()->int struct_getter = struct_capture.getter(); "
		"struct_capture.value = 4; print(struct_getter());\n"
		"    LambdaCaptureBox<int> generic_capture = LambdaCaptureBox<int>(9);\n"
		"    fn()->int generic_getter = generic_capture.getter(); "
		"generic_capture.value = 10; print(generic_getter());\n"
		"    fn()->int static_getter = LambdaCaptureStatics::getter(); "
		"LambdaCaptureStatics.value = 8; print(static_getter());\n"
		"    fn()->int lifetime_getter = make_lifetime_getter(); print(lifetime_getter());\n"
		"}\n";
	const std::filesystem::path CollisionEntry =
		std::filesystem::path(VEROSCRIPT_CSHARP_AOT_TEST_DIR) / "main.vs";
	const std::string CollisionName = "generated_symbol_collision_M_"
		+ EncodeModuleSuffix(CollisionEntry.string());
	Source += "int generated_symbol_collision(ref int value) { return value; }\n";
	Source += "export int " + CollisionName
		+ "(ref int value) { return value + 1; }\n";

	const std::filesystem::path Root(VEROSCRIPT_CSHARP_AOT_TEST_DIR);
	std::error_code FileError;
	std::filesystem::remove_all(Root, FileError);
	FileError.clear();
	std::filesystem::create_directories(Root, FileError);
	if (FileError)
	{
		std::cerr << "FAIL: create test directory: " << FileError.message() << '\n';
		return EXIT_FAILURE;
	}
	const std::filesystem::path Project = Root / "Generated.csproj";
	const std::filesystem::path EntryScript = Root / "main.vs";
	const std::filesystem::path SupportScript = Root / "support.vs";
	const std::filesystem::path PrivateScriptA = Root / "private_a.vs";
	const std::filesystem::path PrivateScriptB = Root / "private_b.vs";
	const std::filesystem::path Output = Root / "output.txt";
	#if VEROSCRIPT_CSHARP_AOT_TEST_NATIVE
	const std::filesystem::path NativeOutput = Root / "native-output.txt";
	const std::filesystem::path Publish = Root / "publish";
	#endif
	if (!WriteText(EntryScript, Source)
		|| !WriteText(SupportScript, SupportSource)
		|| !WriteText(PrivateScriptA, PrivateModuleA)
		|| !WriteText(PrivateScriptB, PrivateModuleB))
	{
		std::cerr << "FAIL: write AOT test inputs\n";
		return EXIT_FAILURE;
	}
	std::string Error;
	VeroScript::FCSharpAotSourceBundle Bundle;
	if (!VeroScript::EmitCSharpSourceBundleFromFile(
			EntryScript.string(), "Generated", Bundle, Error, {Root.string()})
		|| Bundle.Files.size() != 4)
	{
		std::cerr << "FAIL: emit multi-file C# from imported file graph\n"
				  << Error << '\n';
		return EXIT_FAILURE;
	}
	// Exercise synthetic counts so the range regression does not allocate gigabytes.
	const std::string LengthChecks = R"CS(
internal static partial class VeroScriptGeneratedProgram
{
    static VeroScriptGeneratedProgram()
    {
        CheckNumericConversions();
        CheckConsoleRestore();
        if (VsCheckedLength(0) != 0 || VsCheckedLength(int.MaxValue) != int.MaxValue
            || VsLen(new VsString("\u4f60\u597d")) != 6 || VsLen("\U0001f600") != 2)
        {
            throw new System.Exception("valid length was changed");
        }
        if (VsCheckedLength((long)int.MaxValue + 1) != 2147483648L
            || VsCheckedLength(long.MaxValue) != long.MaxValue)
            throw new System.Exception("wide length was narrowed");
        RequireLengthFailure(() => VsCheckedLength(-1), "len result exceeds int64 range");
        RequireLengthFailure(() => VsNativeSize(2147483648L), "size or index exceeds CLR container range");
        RequireLengthFailure(() => VsNativeIndex(ulong.MaxValue), "size or index exceeds CLR container range");
        RequireLengthFailure(() => VsNativeIndex(true), "array index must be an integer");
        RequireLengthFailure(() => VsNativeIndex(1.5), "array index must be an integer");
		if (VsFind(new VsString("abc"), new VsString("b")) != 1L
			|| VsFind(new VsString("abc"), new VsString("z")) != -1L
			|| VsSubstr(new VsString("abc"), 1L, long.MaxValue) != new VsString("bc")
			|| VsCharCode(new VsString("abc"), 1L) != 98)
			throw new System.Exception("wide string positions failed");

        VsMultiSet<int> unordered = new();
        unordered.Add(7);
        typeof(VsMultiSet<int>).GetField("count", System.Reflection.BindingFlags.Instance
            | System.Reflection.BindingFlags.NonPublic)!.SetValue(unordered, long.MaxValue);
        RequireLengthFailure(() => unordered.Add(7), "multiset size exceeds int64 range");
        RequireLengthFailure(() => unordered.Add(8), "multiset size exceeds int64 range");
        if (VsLen(unordered) != long.MaxValue || unordered.Occurrences(7) != 1 || unordered.Contains(8))
        {
            throw new System.Exception("multiset overflow changed its state");
        }

        VsOrderedMultiSet<int> ordered = new();
        ordered.Add(7);
        typeof(VsOrderedMultiSet<int>).GetField("count", System.Reflection.BindingFlags.Instance
            | System.Reflection.BindingFlags.NonPublic)!.SetValue(ordered, long.MaxValue);
        RequireLengthFailure(() => ordered.Add(7), "multiset size exceeds int64 range");
        RequireLengthFailure(() => ordered.Add(8), "multiset size exceeds int64 range");
        if (VsLen(ordered) != long.MaxValue || ordered.Occurrences(7) != 1 || ordered.Contains(8))
        {
            throw new System.Exception("ordered multiset overflow changed its state");
        }
    }

    private static void RequireLengthFailure(System.Action action, string message)
    {
        try
        {
            action();
        } catch (System.InvalidOperationException error)
        {
            if (error.Message != message)
            {
                throw;
            }
            return;
        }
        throw new System.Exception("out-of-range length was accepted");
    }
}
)CS";
	const std::string NumericChecks = R"CS(
internal static partial class VeroScriptGeneratedProgram
{
    private static void CheckNumericConversions()
    {
        // Narrow conversion helpers are compiler-internal; user scripts cannot
        // call their reserved builtin names. Exercise the generated helpers here.
        if (VsToInt8(127.99999999999999) != 127 || VsToUInt8(255.99999999999997) != 255
            || VsToInt16(32767.999999999996) != 32767 || VsToUInt16(65535.99999999999) != 65535)
        {
            throw new System.Exception("narrow numeric conversion rounded across its upper bound");
        }
        RequireNumericFailure(() => VsToInt8(128.0), "to_int8 overflow");
        RequireNumericFailure(() => VsToUInt8(256.0f), "to_uint8 overflow");
        RequireNumericFailure(() => VsToInt16(-32769.0), "to_int16 overflow");
        RequireNumericFailure(() => VsToUInt16(-1.0f), "to_uint16 overflow");
        RequireNumericFailure(() => VsToUInt8(-0.5), "to_uint8 overflow");
        RequireNumericFailure(() => VsToUInt16(-0.5f), "to_uint16 overflow");
        RequireNumericFailure(() => VsToUnsigned(-0.9999999999999999), "to_unsigned overflow");
        RequireNumericFailure(() => VsToUInt64(-double.Epsilon), "to_uint64 overflow");
        RequireNumericFailure(() => VsToInt(2147483648.0), "to_int overflow");
        RequireNumericFailure(() => VsToUnsigned(4294967296.0), "to_unsigned overflow");
        RequireNumericFailure(() => VsToInt64(9223372036854775808.0), "to_int64 overflow");
        RequireNumericFailure(() => VsToInt64(-9223372036854777856.0), "to_int64 overflow");
        RequireNumericFailure(() => VsToUInt64(18446744073709551616.0), "to_uint64 overflow");
        RequireNumericFailure(() => VsToInt(double.NaN), "to_int overflow");
        RequireNumericFailure(() => VsToInt64(float.PositiveInfinity), "to_int64 overflow");
        RequireNumericFailure(() => VsToUInt64(double.NegativeInfinity), "to_uint64 overflow");
        if (VsToUInt64(ulong.MaxValue) != ulong.MaxValue || VsToInt64(long.MinValue) != long.MinValue
            || VsToInt(true) != 1 || VsToInt(false) != 0 || VsToUnsigned(-0.0) != 0)
        {
            throw new System.Exception("integer or boolean conversion changed");
        }
    }

    private static void RequireNumericFailure(System.Action action, string message)
    {
        try
        {
            action();
        } catch (System.InvalidOperationException error)
        {
            if (error.Message != message)
            {
                throw;
            }
            return;
        }
        throw new System.Exception("invalid numeric conversion was accepted");
    }
}
)CS";
	const std::string ConsoleChecks = R"CS(
internal static partial class VeroScriptGeneratedProgram
{
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool AllocConsole();
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool FreeConsole();
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern IntPtr GetConsoleWindow();
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr window, int command);
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern IntPtr CreateConsoleScreenBuffer(uint access, uint sharing, IntPtr security,
        uint flags, IntPtr reserved);
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool SetStdHandle(int identifier, IntPtr handle);
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);

    private static void CheckConsoleRestore()
    {
        ConsoleCleanupFallbackChecks.Run();
        if (!OperatingSystem.IsWindows()) return;
        IntPtr previous = GetStdHandle(-11);
        IntPtr previousInput = GetStdHandle(-10);
        IntPtr previousError = GetStdHandle(-12);
        bool allocated = AllocConsole();
        if (allocated)
        {
            ShowWindow(GetConsoleWindow(), 0);
        }
        IntPtr buffer = CreateConsoleScreenBuffer(0xc0000000, 3, IntPtr.Zero, 1, IntPtr.Zero);
        try
        {
            if (buffer == new IntPtr(-1) || !SetStdHandle(-11, buffer))
            {
                throw new System.Exception("cannot create an isolated console restoration fixture");
            }
            // Test both boundary re-capture and a non-default initially hidden
            // cursor. The fixture never activates or writes to the user's buffer.
            foreach (ushort original in new ushort[] { 0x2e, 0x4b })
            {
                var cursor = new VsConsoleCursorInfo { Size = 31, Visible = 0 };
                if (!SetConsoleTextAttribute(buffer, original) || !SetConsoleCursorInfo(buffer, in cursor))
                {
                    throw new System.Exception("cannot initialize the console restoration fixture");
                }
                VsConsoleMark(true, true);
                var changed = new VsConsoleCursorInfo { Size = 44, Visible = 1 };
                SetConsoleTextAttribute(buffer, 7);
                SetConsoleCursorInfo(buffer, in changed);
                VsConsoleRestore();
                if (!GetConsoleScreenBufferInfo(buffer, out var restoredBuffer)
                    || restoredBuffer.Attributes != original
                    || !GetConsoleCursorInfo(buffer, out var restoredCursor)
                    || restoredCursor.Visible != cursor.Visible || restoredCursor.Size != cursor.Size
                    || VsConsoleDirtyStyle || VsConsoleDirtyCursor)
                {
                    throw new System.Exception("Windows console attributes and cursor were not restored");
                }
            }
        }
        finally
        {
            if (buffer != new IntPtr(-1))
            {
                CloseHandle(buffer);
            }
            if (allocated)
            {
                FreeConsole();
            }
            SetStdHandle(-11, previous);
            SetStdHandle(-10, previousInput);
            SetStdHandle(-12, previousError);
        }
    }
}
)CS";
	// Run the emitted cleanup helper against controllable native/text failures.
	// The helper itself is copied from the generated runtime below, not reimplemented.
	const std::string ConsoleFallbackPrefix = R"CS(
internal static class ConsoleCleanupFallbackChecks
{
    private static readonly object VsConsoleSync = new();
    private static bool VsConsoleDirtyStyle, VsConsoleDirtyCursor;
    private static IntPtr VsConsoleRestoreHandle;
    private struct BufferInfo { internal ushort Attributes; }
    private struct CursorInfo { internal int Visible; }
    private static BufferInfo VsConsoleOriginalBuffer;
    private static CursorInfo VsConsoleOriginalCursor;
    private static bool VsConsoleHaveAttributes, VsConsoleHaveCursor;
    private static readonly System.Collections.Generic.List<string> Commands = new();
    private static bool NativeSuccess, FailReset;
    private static bool SetConsoleTextAttribute(IntPtr handle, ushort value)
    { Commands.Add("attributes"); return NativeSuccess; }
    private static bool SetConsoleCursorInfo(IntPtr handle, in CursorInfo value)
    { Commands.Add("cursor"); return NativeSuccess; }
    private static bool VsConsoleResetStyle()
    { Commands.Add("reset"); if (FailReset) throw new System.IO.IOException("reset failed"); return true; }
    private static bool VsConsoleSequence(string value)
    { Commands.Add("show"); return true; }
)CS";
	const std::string ConsoleFallbackSuffix = R"CS(
    private static void Check(bool attributes, bool cursor, bool success, bool failReset, string expected)
    {
        Commands.Clear(); NativeSuccess = success; FailReset = failReset;
        VsConsoleDirtyStyle = VsConsoleDirtyCursor = true;
        VsConsoleHaveAttributes = attributes; VsConsoleHaveCursor = cursor;
        VsConsoleRestoreHandle = new IntPtr(123);
        VsConsoleOriginalBuffer = new BufferInfo { Attributes = 7 };
        VsConsoleOriginalCursor = new CursorInfo { Visible = 0 };
        VsConsoleRestore();
        if (string.Join(",", Commands) != expected || VsConsoleDirtyStyle || VsConsoleDirtyCursor
            || VsConsoleHaveAttributes || VsConsoleHaveCursor || VsConsoleRestoreHandle != IntPtr.Zero)
            throw new System.Exception("incomplete or repeated console cleanup: " + string.Join(",", Commands));
    }
    private sealed class FailedFlush : System.IO.StringWriter
    { public override void Flush() => throw new System.IO.IOException("flush failed"); }
    internal static void Run()
    {
        bool windows = OperatingSystem.IsWindows();
        Check(false, true, true, false, windows ? "cursor,reset" : "reset,show");
        Check(true, false, true, false, windows ? "attributes,show" : "reset,show");
        Check(true, true, false, false, windows ? "attributes,cursor,reset,show" : "reset,show");
        Check(false, false, true, true, "reset,show");
        var saved = Console.Out;
        try { Console.SetOut(new FailedFlush()); Check(false, false, true, true, "reset,show"); }
        finally { Console.SetOut(saved); }
    }
}
)CS";
	bool bHasLengthChecks = false;
	for (const VeroScript::FCSharpAotSourceFile& File : Bundle.Files)
	{
		const bool bIsRuntime = File.Contents.find("private static long VsCheckedLength(")
			!= std::string::npos;
		bHasLengthChecks = bHasLengthChecks || bIsRuntime;
		std::string ConsoleFallbackChecks;
		if (bIsRuntime)
		{
			const auto Start = File.Contents.find("\tprivate static void VsConsoleRestore()");
			const auto End = File.Contents.find("\tprivate static string VsText(", Start);
			if (Start == std::string::npos || End == std::string::npos)
			{
				std::cerr << "FAIL: generated console cleanup helper missing\n";
				return EXIT_FAILURE;
			}
			ConsoleFallbackChecks = ConsoleFallbackPrefix + File.Contents.substr(Start, End - Start)
				+ ConsoleFallbackSuffix;
		}
		if (!WriteText(Root / File.RelativePath,
				bIsRuntime ? File.Contents + LengthChecks + NumericChecks + ConsoleChecks + ConsoleFallbackChecks : File.Contents))
		{
			std::cerr << "FAIL: write generated C# bundle file\n";
			return EXIT_FAILURE;
		}
	}
	if (!bHasLengthChecks)
	{
		std::cerr << "FAIL: generated runtime did not receive length boundary probes\n";
		return EXIT_FAILURE;
	}
	if (!WriteText(Root / "Generated.manifest.json", Bundle.ManifestJson))
	{
		std::cerr << "FAIL: write generated C# bundle manifest\n";
		return EXIT_FAILURE;
	}

	const std::filesystem::path DotNet(VEROSCRIPT_CSHARP_AOT_DOTNET);
	const std::string RunCommand = Quote(DotNet) + " run --project " + Quote(Project)
		+ " --configuration Release -- alpha beta > " + Quote(Output);
	if (!Run(RunCommand, "run generated C#"))
	{
		return EXIT_FAILURE;
	}
	std::string Actual;
	#if defined(_WIN32)
	const std::string DomainNan = "-nan(ind)\n";
	#elif defined(__APPLE__)
	const std::string DomainNan = "nan\n";
	#else
	const std::string DomainNan = "-nan\n";
	#endif
	const std::string RawPrintf = std::string("raw=")
		+ static_cast<char>(0xff) + "\n";
	const std::string Expected =
		"你好，世界\n65\n-1\n3\n4\n9\n2\n19\n3\n7\n29\n19\n21\n13\n5\n"
		"5\n3\n7\n8\n4\n6\n9\n2\n3\n5\n42\n6\n42\n43\n21\nexception-finally\n5\n"
		"d=-00042 x=0X0000FF f=3.14\ns=abc   c=A %\n"
		"g=1.e+01 e=01.500e+00\n"
		"float-flags=[ 1.5]/[12.00       ]/[-0.000000]\n"
		"g-boundaries=[1e+03]/[999]/[1e+01]/[0.0001]/[9.99e-05]\n"
		"1026\n1\n3\n世\n1\n" +
		RawPrintf + "4\n𐂀\nprintf-array=7/ff\n"
		"true\nfalse\ntrue\ntrue\nfalse\ntrue\ntrue\n12\n"
		"10\n20\n6\n7\n1\nvalue=7\n6\n2\n1\n2\ntrue\ngeneric\narray=8\n3\n-3\n7\n你好，世界\n30\n1\n2\n"
		"0\n5\n0\n8\n1\n2\n127\n12\n4294967045\n6\n0\n8\n4\n-5\n4294967295\n1.23456789012346\n0.100000001\n1.23456789012346\n"
		"9\n0\n1\n0\n3\n4\n" +
		DomainNan + DomainNan + DomainNan + "inf\n2\n"
		+ DomainNan + "2\n" + DomainNan
		+ "nan\n-0\n0\n"
		+ "15\n6\n4\n20\n2\ntrue\n1\n7\n3\n6\n15\n8\narray<int>[1]\n3\n3\n3\ntone=3\ntrue\n2\n2\n2\n1\npair#6:string3:int\npair#6:string3:int\n3\nb\na\nb\na\nb\n2\n4\n4\n9\n8\n9\n8\n9\n9\n12\n1\n2\nnested\n13\n21\n1\n1\n5\n4\n"
		"Tools.Level(4)\n7\n7\n9\n10\n11\n0\ntrue\ntrue\nfalse\npair\n8\ntrue\n13\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse\ntrue\ntrue\n41\n"
		"2\ntrue\n25\n35\n5\n3\nveroscript\ntrue\n"
		"true\ntrue\ntrue\n0\n4\n5\n6\ntrue\ntrue\ntrue\ntrue\n"
		"11\n30\n32\n4\n3\n8\n9\n42\n12\n9\n7\n8\n"
		"1\n2\n2\nb\n3\n3\n2\n1\nb\nb\n5\n5\n9\n3\n2\n1\n9\n"
		"2\n2\n2\n7\n11\n7\n2\n2\n3\n10\n8\n42\n";
	if (!ReadText(Output, Actual) || Actual != Expected)
	{
		std::cerr << "FAIL: generated C# output\nactual=[" << Actual
			<< "]\nexpected=[" << Expected << "]\n";
		return EXIT_FAILURE;
	}
	if (!VerifyNumericFailures(Quote(DotNet) + " run --no-build --project " + Quote(Project)
			+ " --configuration Release --", Root))
	{
		return EXIT_FAILURE;
	}

	#if VEROSCRIPT_CSHARP_AOT_TEST_NATIVE
	const std::string PublishCommand = Quote(DotNet) + " publish " + Quote(Project)
		+ " --configuration Release --runtime " VEROSCRIPT_CSHARP_AOT_RID
		+ " --self-contained true -p:PublishAot=true --output " + Quote(Publish)
		+ " --nologo";
	if (!Run(PublishCommand, "publish generated C# with NativeAOT"))
	{
		return EXIT_FAILURE;
	}
	#if defined(_WIN32)
	const std::filesystem::path NativeExe = Publish / "Generated.exe";
	#else
	const std::filesystem::path NativeExe = Publish / "Generated";
	#endif
	const std::string NativeCommand = Quote(NativeExe) + " alpha beta > " + Quote(NativeOutput);
	if (!Run(NativeCommand, "run generated NativeAOT application"))
	{
		return EXIT_FAILURE;
	}
	Actual.clear();
	if (!ReadText(NativeOutput, Actual) || Actual != Expected)
	{
		std::cerr << "FAIL: generated NativeAOT output\nactual=[" << Actual
			<< "]\nexpected=[" << Expected << "]\n";
		return EXIT_FAILURE;
	}
	if (!VerifyNumericFailures(Quote(NativeExe), Root))
	{
		return EXIT_FAILURE;
	}
	#endif

	const std::filesystem::path PairRoot = Root / "pair-template";
	std::filesystem::create_directories(PairRoot);
	VeroScript::FCSharpAotSourceBundle PairBundle;
	if (!VeroScript::EmitCSharpSourceBundleFromSource(
			"struct pair<T,U>{T custom;U extra;T read(){return custom;}}"
			"void main(){pair<int,string> value=pair<int,string>();value.custom=7;value.extra=\"user\";"
			"print(value.read());print(value.extra);ordered_map<string,pair<int,string>> values=ordered_map<string,pair<int,string>>();"
			"values[\"a\"]=value;for(pair<int,string> item:values){print(item.read());}}",
			"pair-template.vs", "Pair", PairBundle, Error))
	{
		std::cerr << "FAIL: emit user pair template: " << Error << '\n';
		return EXIT_FAILURE;
	}
	for (const auto& File : PairBundle.Files)
	{
		if (!WriteText(PairRoot / File.RelativePath, File.Contents)) return EXIT_FAILURE;
	}
	const std::filesystem::path PairOutput = PairRoot / "output.txt";
	if (!Run(Quote(DotNet) + " run --project " + Quote(PairRoot / "Pair.csproj")
			+ " --configuration Release > " + Quote(PairOutput), "run user pair template")
		|| !ReadText(PairOutput, Actual) || Actual != "7\nuser\n7\n")
	{
		std::cerr << "FAIL: user pair template and map value iteration: " << Actual << '\n';
		return EXIT_FAILURE;
	}

	if (!VerifyInitialization(Root)) return EXIT_FAILURE;
	std::cout << "VeroScriptCSharpAotHelloTests passed.\n";
	return EXIT_SUCCESS;
}
