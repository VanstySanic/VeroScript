// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPythonAot.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLocale.h"
#include "VeroScriptPath.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace
{
	class FPythonAotGeneralProcessor final : public VeroScript::ICompileProcessor
	{
	public:
		void Process(const VeroScript::FCompileProcessingContext& Context,
		    VeroScript::FCompileProcessingOutput& Output) override
		{
			Output.AddDeclaration({Context.ModuleId, "", "", Context.Snapshot},
			    "int GeneralGenerated(){return 63;}");
		}
	};

	bool Expect(const bool Condition, const char* Label)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Condition;
	}

	bool ReadFile(const std::filesystem::path& Path, std::string& Out)
	{
		std::ifstream Input(Path, std::ios::binary);
		if (!Input)
		{
			return false;
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		Out = Buffer.str();
		return true;
	}

	std::string NormalizeNewlines(std::string Text)
	{
		std::string Out;
		for (std::size_t Index = 0; Index < Text.size(); ++Index)
		{
			if (Text[Index] == '\r')
			{
				if (Index + 1 < Text.size() && Text[Index + 1] == '\n')
				{
					continue;
				}
				Out.push_back('\n');
			}
			else
			{
				Out.push_back(Text[Index]);
			}
		}
		return Out;
	}

	bool RunPython(
	    const std::string& Python,
	    const std::string& ExpectedOutput,
	    const char* Label,
	    const std::string& InputText = {})
	{
		#ifndef VEROSCRIPT_PYTHON_EXECUTABLE
		std::cout << "SKIP: " << Label << " (Python 3.11-3.15 interpreter not found)\n";
		return true;
		#else
		namespace fs = std::filesystem;
		const fs::path Root = fs::temp_directory_path() / ("veroscript_python_aot_" + std::to_string(std::rand()));
		std::error_code ErrorCode;
		fs::create_directories(Root, ErrorCode);
		const fs::path Script = Root / "generated.py";
		const fs::path Output = Root / "stdout.txt";
		const fs::path Errors = Root / "stderr.txt";
		const fs::path Input = Root / "stdin.txt";
		{
			std::ofstream Stream(Script, std::ios::binary);
			Stream << Python;
			if (!Stream)
			{
				std::cerr << "FAIL: " << Label << " (cannot write generated file)\n";
				return false;
			}
		}
		{
			std::ofstream Stream(Input, std::ios::binary);
			Stream << InputText;
		}
		#if defined(_WIN32)
		const fs::path Runner = Root / "run.bat";
		{
			std::ofstream Stream(Runner, std::ios::binary);
			Stream << "@echo off\r\n\"" << VEROSCRIPT_PYTHON_EXECUTABLE << "\" \""
			       << Script.string() << "\" < \"" << Input.string() << "\" > \""
			       << Output.string() << "\" 2> \""
			       << Errors.string() << "\"\r\n";
		}
		const std::string Command = "\"" + Runner.string() + "\"";
		#else
		const std::string Command = std::string("\"") + VEROSCRIPT_PYTHON_EXECUTABLE + "\" \"" + Script.string() + "\" < \"" + Input.string() + "\" > \"" + Output.string() + "\" 2> \"" + Errors.string() + "\"";
		#endif
		const int ExitCode = std::system(Command.c_str());
		std::string Actual;
		std::string ErrorText;
		ReadFile(Output, Actual);
		ReadFile(Errors, ErrorText);
		fs::remove_all(Root, ErrorCode);
		if (ExitCode != 0)
		{
			std::cerr << "FAIL: " << Label << " (Python exit " << ExitCode << ")\n"
			          << ErrorText << '\n';
			return false;
		}
		Actual = NormalizeNewlines(std::move(Actual));
		if (Actual != ExpectedOutput)
		{
			std::cerr << "FAIL: " << Label << "\nactual=[" << Actual
			          << "] expected=[" << ExpectedOutput << "]\n"
			          << ErrorText;
			return false;
		}
		return true;
		#endif
	}

	bool RunPythonPath(
	    const std::filesystem::path& Target,
	    const std::string& ExpectedOutput,
	    const char* Label,
	    const std::string& RequiredError = {})
	{
		#ifndef VEROSCRIPT_PYTHON_EXECUTABLE
		std::cout << "SKIP: " << Label << " (Python 3.11-3.15 interpreter not found)\n";
		return true;
		#else
		namespace fs = std::filesystem;
		const fs::path Root = fs::temp_directory_path() / ("veroscript_python_aot_path_" + std::to_string(std::rand()));
		std::error_code ErrorCode;
		fs::create_directories(Root, ErrorCode);
		const fs::path Output = Root / "stdout.txt";
		const fs::path Errors = Root / "stderr.txt";
		#if defined(_WIN32)
		const fs::path Runner = Root / "run.bat";
		{
			std::ofstream Stream(Runner, std::ios::binary);
			Stream << "@echo off\r\n\"" << VEROSCRIPT_PYTHON_EXECUTABLE << "\" \""
			       << Target.string() << "\" > \"" << Output.string()
			       << "\" 2> \"" << Errors.string() << "\"\r\n";
		}
		const std::string Command = "\"" + Runner.string() + "\"";
		#else
		const std::string Command = std::string("\"") + VEROSCRIPT_PYTHON_EXECUTABLE + "\" \"" + Target.string() + "\" > \"" + Output.string() + "\" 2> \"" + Errors.string() + "\"";
		#endif
		const int ExitCode = std::system(Command.c_str());
		std::string Actual;
		std::string ErrorText;
		ReadFile(Output, Actual);
		ReadFile(Errors, ErrorText);
		fs::remove_all(Root, ErrorCode);
		if (!RequiredError.empty())
		{
			return Expect(ExitCode != 0, Label) &&
			    Expect(ErrorText.find(RequiredError) != std::string::npos, Label) &&
			    Expect(NormalizeNewlines(Actual) == ExpectedOutput, Label);
		}
		if (ExitCode != 0)
		{
			std::cerr << "FAIL: " << Label << " (Python exit " << ExitCode << ")\n"
			          << ErrorText << '\n';
			return false;
		}
		Actual = NormalizeNewlines(std::move(Actual));
		if (Actual != ExpectedOutput)
		{
			std::cerr << "FAIL: " << Label << "\nactual=[" << Actual
			          << "] expected=[" << ExpectedOutput << "]\n"
			          << ErrorText;
			return false;
		}
		return true;
		#endif
	}

	bool EmitAndRun(
	    const std::string& Source,
	    const std::string& ExpectedOutput,
	    const char* Label,
	    const std::string& InputText = {}, bool RequiresRuntime = false,
	    const std::string& PythonChecks = {})
	{
		std::ostringstream VmOutput;
		std::ostringstream VmErrors;
		std::istringstream VmInput(InputText);
		const VeroScript::FRunResult VmResult = VeroScript::Test::FEngine::RunSource(
		    Source,
		    "python-aot-test.vs",
		    VmOutput,
		    VmErrors,
		    VeroScript::ERunMode::Script,
		    {},
		    {},
		    InputText.empty() ? nullptr : &VmInput,
		    {},
		    {},
		    VeroScript::EExecutionBackend::Vm);
		if (!Expect(VmResult.bSuccess, Label) || !Expect(VmErrors.str().empty(), "VM differential run has no errors") || !Expect(NormalizeNewlines(VmOutput.str()) == ExpectedOutput, "VM differential output matches expectation"))
		{
			std::cerr << VmErrors.str() << "vm=[" << VmOutput.str()
			          << "] expected=[" << ExpectedOutput << "]\n";
			return false;
		}
		std::string Python;
		std::string Error;
		const bool Emitted = VeroScript::EmitPythonFromSource(Source, "python-aot-test.vs", Python, Error);
		if (RequiresRuntime)
		{
			return Expect(!Emitted && Python.empty() && Error.find("standard-library foundation operations require") != std::string::npos,
			    Label);
		}
		if (!Expect(Emitted, Label))
		{
			std::cerr << Error << '\n';
			return false;
		}
		if (!Expect(Python.find("_veroscript") == std::string::npos, "no native extension dependency") || !Expect(Python.find("_vs_script_main") != std::string::npos, "entry emitted") || !Expect(Python.find("Requires Python 3.11-3.15") != std::string::npos, "runtime contract emitted") || !Expect(Python.find("_VS_INTEGER_RANGES") != std::string::npos, "integer metadata tables are emitted once") || !Expect(Python.find("@_vs_functools.cache\ndef _vs_base_type") != std::string::npos, "type parsing uses the generated-runtime cache"))
		{
			return false;
		}
		return RunPython(Python + PythonChecks, ExpectedOutput, Label, InputText);
	}

	bool EmitAndRunWithBundle(
	    const std::string& Source, const std::string& ExpectedOutput, const char* Label)
	{
		if (!EmitAndRun(Source, ExpectedOutput, Label)) return false;
		VeroScript::FPythonAotSourceBundle Bundle;
		std::string Error;
		if (!VeroScript::EmitPythonSourceBundleFromSource(Source, "python-aot-test.vs", "assignment", Bundle, Error))
		{
			std::cerr << "FAIL: " << Label << " (bundle emission)\n" << Error << '\n';
			return false;
		}
		const auto Root = std::filesystem::temp_directory_path() /
		    ("veroscript_python_aot_assignment_" + std::to_string(std::rand()));
		const bool Written = VeroScript::WritePythonSourceBundle(Bundle, Root.string(), Error);
		const bool Passed = Expect(Written, Label) && RunPythonPath(Root, ExpectedOutput, Label);
		std::error_code RemoveError;
		std::filesystem::remove_all(Root, RemoveError);
		return Passed;
	}

	bool EmitAndRequirePythonFailure(
	    const std::string& Source,
	    const std::string& RequiredError,
	    const char* Label,
	    const std::string& InputText = {})
	{
		#ifndef VEROSCRIPT_PYTHON_EXECUTABLE
		std::cout << "SKIP: " << Label << " (Python 3.11-3.15 interpreter not found)\n";
		return true;
		#else
		std::string Python;
		std::string EmitError;
		if (!VeroScript::EmitPythonFromSource(Source, "python-aot-failure.vs", Python, EmitError))
		{
			std::cerr << "FAIL: " << Label << " (emit)\n"
			          << EmitError << '\n';
			return false;
		}
		namespace fs = std::filesystem;
		const fs::path Root = fs::temp_directory_path() / ("veroscript_python_aot_failure_" + std::to_string(std::rand()));
		std::error_code ErrorCode;
		fs::create_directories(Root, ErrorCode);
		const fs::path Script = Root / "generated.py";
		const fs::path Errors = Root / "stderr.txt";
		const fs::path Input = Root / "stdin.txt";
		const fs::path Runner = Root / "run.bat";
		{
			std::ofstream Stream(Script, std::ios::binary);
			Stream << Python;
		}
		{
			std::ofstream Stream(Input, std::ios::binary);
			Stream << InputText;
		}
		#if defined(_WIN32)
		{
			std::ofstream Stream(Runner, std::ios::binary);
			Stream << "@echo off\r\n\"" << VEROSCRIPT_PYTHON_EXECUTABLE << "\" \""
			       << Script.string() << "\" < \"" << Input.string()
			       << "\" > nul 2> \"" << Errors.string() << "\"\r\n";
		}
		const std::string Command = "\"" + Runner.string() + "\"";
		#else
		const std::string Command = std::string("\"") + VEROSCRIPT_PYTHON_EXECUTABLE + "\" \"" + Script.string() + "\" < \"" + Input.string() + "\" >/dev/null 2> \"" + Errors.string() + "\"";
		#endif
		const int ExitCode = std::system(Command.c_str());
		std::string ErrorText;
		ReadFile(Errors, ErrorText);
		fs::remove_all(Root, ErrorCode);
		return Expect(ExitCode != 0, Label) && Expect(ErrorText.find(RequiredError) != std::string::npos, Label);
		#endif
	}

	bool EmitAndRequireBothFailure(
	    const std::string& Source,
	    const std::string& RequiredVmError,
	    const std::string& RequiredPythonError,
	    const char* Label,
	    const std::string& InputText = {})
	{
		std::ostringstream VmOutput;
		std::ostringstream VmErrors;
		std::istringstream VmInput(InputText);
		const VeroScript::FRunResult VmResult = VeroScript::Test::FEngine::RunSource(
		    Source,
		    "python-aot-failure.vs",
		    VmOutput,
		    VmErrors,
		    VeroScript::ERunMode::Script,
		    {},
		    {},
		    InputText.empty() ? nullptr : &VmInput,
		    {},
		    {},
		    VeroScript::EExecutionBackend::Vm);
		if (!Expect(!VmResult.bSuccess, Label) || !Expect(RequiredVmError.compare(0, 5, "diag.") == 0 ? VmResult.Error.MessageId == RequiredVmError : VmErrors.str().find(RequiredVmError) != std::string::npos, Label))
		{
			std::cerr << "VM output=[" << VmOutput.str() << "] errors=["
			          << VmErrors.str() << "]\n";
			return false;
		}
		return EmitAndRequirePythonFailure(Source, RequiredPythonError, Label, InputText);
	}

	bool EmitAndRequireBothFailureWithBundle(
	    const std::string& Source, const std::string& RequiredVmError,
	    const std::string& RequiredPythonError, const char* Label)
	{
		if (!EmitAndRequireBothFailure(Source, RequiredVmError, RequiredPythonError, Label)) return false;
		VeroScript::FPythonAotSourceBundle Bundle;
		std::string Error;
		if (!VeroScript::EmitPythonSourceBundleFromSource(Source, "python-aot-failure.vs", "comparison", Bundle, Error))
		{
			std::cerr << "FAIL: " << Label << " (bundle emission)\n" << Error << '\n';
			return false;
		}
		const auto Root = std::filesystem::temp_directory_path() /
		    ("veroscript_python_aot_comparison_failure_" + std::to_string(std::rand()));
		const bool Written = VeroScript::WritePythonSourceBundle(Bundle, Root.string(), Error);
		const bool Passed = Expect(Written, Label) && RunPythonPath(Root, {}, Label, RequiredPythonError);
		std::error_code RemoveError;
		std::filesystem::remove_all(Root, RemoveError);
		return Passed;
	}

	bool EmitMustFail(
	    const std::string& Source,
	    const std::string& RequiredError,
	    const char* Label)
	{
		std::string Python = "unchanged";
		std::string Error;
		const bool Success = VeroScript::EmitPythonFromSource(
		    Source, "python-aot-rejection.vs", Python, Error);
		return Expect(!Success, Label) && Expect(Python.empty(), "rejected emission has no output") && Expect(Error.find(RequiredError) != std::string::npos, Label);
	}
}

int main()
{
	if (!EmitAndRun(
	        "namespace N{int read(){return 42;}}void main(){fn()->int callback=N.read;print(callback());}",
	        "42\n", "qualified namespace function values use emitted function symbols"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "int increment(int value){return value+1;}namespace N{namespace Inner{int read(){return 42;}}}"
	        "void main(){fn()->int direct=::N::Inner::read;fn(int)->int ordinary=increment;print(direct());print(ordinary(41));}",
	        "42\n42\n", "qualified namespace function values preserve declared signatures"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "namespace N{int target(){return 42;}int read(){return 99;}fn()->int read=target;}void main(){print(N.read());}",
	        "42\n", "namespace value bindings retain priority over function declarations"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "namespace N{int read(){return 42;}}class Receiver{public:int read=21;}"
	        "void main(){Receiver N=Receiver();print(N.read);}",
	        "21\n", "local receivers retain priority over namespace function values"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "namespace N{int read(){return 42;}}class Receiver{public:int read=21;}"
	        "class Holder{public:static Receiver N=Receiver();int invoke(){return N.read;}}"
	        "void main(){Holder value=Holder();print(value.invoke());}",
	        "21\n", "static field receivers retain priority over namespace function values"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "class Receiver{public:int read=21;}"
	        "class Caller{public:int invoke(){Receiver Super=Receiver();return Super.read;}}"
	        "void main(){Caller value=Caller();print(value.invoke());}",
	        "21\n", "ordinary Super value bindings retain member access"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitMustFail(
	        "namespace Super{int value(){return 42;}}class Base{public:int value=7;}"
	        "class Derived:Base{public:int read(){return Super::value;}}void main(){Derived value=Derived();print(value.read());}",
	        "Super receivers are not supported", "Super receivers are not treated as namespaces"))
	{
		return EXIT_FAILURE;
	}
	for (const char* Source : {
	         "int count(int... values){return cast<int>(len(values));}void main(){fn(array<int>)->int callback=count;print(callback(array<int>{1,2,3}));}",
	         "namespace N{int count(int... values){return cast<int>(len(values));}}void main(){fn(array<int>)->int callback=N.count;print(callback(array<int>{1,2,3}));}"})
	{
		if (!EmitMustFail(Source, "variadic function values are not supported", "variadic function values do not silently repack arrays"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!EmitAndRun(
	        "int count(int... values){return cast<int>(len(values));}void main(){print(count(1,2,3));}",
	        "3\n", "direct variadic calls remain supported"))
	{
		return EXIT_FAILURE;
	}
	for (const char* Source : {
	         "int next(int seed){static int count=seed;return ++count;}void main(){print(next(10));print(next(99));}",
	         "class Counter{public:int next(){static int count=0;return ++count;}};void main(){Counter value=Counter();print(value.next());}",
	         "void main(){int n=0;while(n<2){static int count=0;print(++count);++n;}}",
	         "void main(){static int first=1,second=2;print(first+second);}"})
	{
		if (!EmitMustFail(Source, "local static storage requires", "local static storage is not emitted as an ordinary Python local"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const char* Source : {
	         "void main(){int seed=40;auto next=[count=seed]() mutable -> int{return ++count;};print(next());}",
	         "function()->int make(){int seed=40;return [&value=seed]()->int{return ++value;};}void main(){auto next=make();print(next());}"})
	{
		if (!EmitMustFail(Source, "source lambdas are not supported", "initialized captures require the runtime"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!EmitAndRun(
	        "bool first=read();auto second=true;bool read(){return second;}"
	        "bool pass(bool value){return value;}void main(){"
	        "print(first);print(cast<bool>(first));print(pass(first));print(second);}",
	        "0\n0\n0\ntrue\n", "forward bool slots retain VM values through calls and same-type casts"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "bool first=read();bool second=true;bool read(){return second;}"
	        "void main(){print(first==false);print(false==first);print(first!=false);"
	        "print(false&&first);print(true||first);}",
	        "false\nfalse\ntrue\nfalse\ntrue\n", "reserved bool values preserve actual equality and logical short circuit"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "template<typename T>bool equal(T left,T right){return left==right;}"
	        "bool first=read();bool second=true;bool read(){return second;}"
	        "void main(){print(equal<bool>(first,false));print(equal<bool>(false,first));}",
	        "false\nfalse\n", "generic equality validates the actual bool payload"))
	{
		return EXIT_FAILURE;
	}

	for (const char* Body : {
	         "print(!first);", "if(first)print(1);", "while(first){break;}",
	         "for(;first;){break;}", "print(first?1:2);", "print(first&&true);",
	         "print(first||true);", "assert(first);"})
	{
		const std::string Source = "bool first=read();bool second=true;bool read(){return second;}void main(){" + std::string(Body) + "}";
		if (!EmitAndRequirePythonFailure(Source, "condition must be bool", "reserved integer zero is not a bool condition"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!EmitAndRun(
	        "enum class Mode{A=0,B=7};Mode first=read();Mode second=Mode::B;Mode read(){return second;}"
	        "void main(){print(first==Mode::A);print(Mode::A==first);print(cast<Mode>(first));}",
	        "false\nfalse\nMode(0)\n", "scoped enum equality and explicit casts use actual reserved slot values"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "enum Mode{A=0,B=7};Mode first=read();Mode second=B;Mode read(){return second;}"
	        "void main(){print(first==A);print(A==first);print(cast<Mode>(first));}",
	        "true\ntrue\n0\n", "unscoped enums retain integer equality and same-type casts"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "namespace N{class A{public:static bool value=read();};"
	        "class B{public:static bool value=true;};bool read(){return B::value;}}"
	        "void main(){print(N::A::value);print(N::B::value);}",
	        "0\ntrue\n", "namespace-local static field lookup and forward bool initialization"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "class Base{public:static int value=7;};class Derived:Base{public:"
	        "int read(){return value;}void change(){value=9;}}void main(){"
	        "Derived d=Derived();print(d.read());d.change();print(Base::value);print(d.read());}",
	        "7\n9\n9\n", "inherited static fields use their declaring type for reads and writes"))
	{
		return EXIT_FAILURE;
	}

	for (const char* Source : {
	         "class Base{public:static int value=7;};class Derived:Base{public:int change(){return (value=9);}}void main(){Derived d=Derived();print(d.change());print(Base::value);}",
	         "class Base{public:static int value=7;};class Derived:Base{public:int value=3;int change(){value=9;return value;}}void main(){Derived d=Derived();print(d.change());print(Base::value);}",
	         "class Base{public:static int value=7;};class Derived:Base{public:int value=3;int change(){return(value=9);}}void main(){Derived d=Derived();print(d.change());print(Base::value);}"})
	{
		const std::string Expected = std::string(Source).find("int value=3") == std::string::npos
		    ? "9\n9\n" : "9\n7\n";
		if (!EmitAndRun(Source, Expected, "inherited static writes and instance field hiding use the same read/write owner"))
		{
			return EXIT_FAILURE;
		}
	}

	// Default storage equality retains actual FValue tags, including nested values.
	for (const auto& Case : {
	         std::pair<const char*, const char*>{"struct Item{public:bool flag;};Item first=make_item();bool later=false;bool read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=false;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"pair<bool,int> first=make_item();bool later=false;bool read_flag(){return later;}pair<bool,int> make_item(){pair<bool,int> value=pair<bool,int>();value.first=read_flag();value.second=1;return value;}void main(){pair<bool,int> other=pair<bool,int>();other.first=false;other.second=1;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"struct Item{public:int8 flag;};Item first=make_item();int8 later=0;int8 read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=0;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"struct Item{public:int64 flag;};Item first=make_item();int64 later=0;int64 read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=0;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"struct Item{public:float flag;};Item first=make_item();float later=0.0;float read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=0.0;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"struct Item{public:double flag;};Item first=make_item();double later=0.0;double read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=0.0;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"struct Item{public:string flag;};Item first=make_item();string later=\"x\";string read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=\"x\";print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"struct Item{public:wstring flag;};Item first=make_item();wstring later=L\"x\";wstring read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=L\"x\";print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"enum E{Zero=0};struct Item{public:E flag;};Item first=make_item();E later=E::Zero;E read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=E::Zero;print(first==other);print(first!=other);}", "true\nfalse\n"},
	         {"enum E:uint8{Zero=0};struct Item{public:E flag;};Item first=make_item();E later=E::Zero;E read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=E::Zero;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"enum class E{Zero=0};struct Item{public:E flag;};Item first=make_item();E later=E::Zero;E read_flag(){return later;}Item make_item(){Item value=Item();value.flag=read_flag();return value;}void main(){Item other=Item();other.flag=E::Zero;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"struct Inner{public:bool flag;};struct Outer{public:Inner value;};Outer first=make_item();bool later=false;bool read_flag(){return later;}Outer make_item(){Outer value=Outer();value.value.flag=read_flag();return value;}void main(){Outer other=Outer();other.value.flag=false;print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"void main(){optional<int>a=optional<int>();optional<int>b=optional<int>();print(a==b);a=optional<int>(1);b=optional<int>(1);print(a==b);optional<int>c=a;print(a==c);}", "true\ntrue\ntrue\n"},
	         {"void main(){optional<int>a=optional<int>(5);a.reset();optional<int>b=optional<int>();print(a==b);print(a!=b);}", "true\nfalse\n"},
	         {"optional<bool> first=make_item();bool later=false;bool read_flag(){return later;}optional<bool> make_item(){return optional<bool>(read_flag());}void main(){optional<bool> other=optional<bool>(false);print(first==other);print(first!=other);}", "false\ntrue\n"},
	         {"class C{public:bool operator<(array<int> other){return true;}};void main(){C c=C();array<int>values={1};print(c<values);}", "true\n"}})
	{
		if (!EmitAndRunWithBundle(Case.first, Case.second, "default record and optional storage equality preserves Core FValue alternatives"))
		{
			return EXIT_FAILURE;
		}
	}

	// Explicit member ordering preserves Core dispatch and worker snapshots preserve optional state.
	for (const auto& Case : {
	         std::pair<const char*, const char*>{"class C{public:bool operator<(array<int> other){return true;}};void main(){C c=C();array<int>values={1};print(values<c);}", "true\n"},
	         {"class C{public:bool operator<(bool other){return other;}};void main(){C c=C();print(true<c);print(false<c);}", "true\nfalse\n"},
	         {"struct C{public:bool operator<(array<int> other){return true;}};void main(){C c=C();array<int>a={1};print(a<c);}", "true\n"},
	         {"class C{public:bool operator<(C other){return false;}};class D:public C{public:bool operator<(C other){return true;}};void main(){C c=C();D d=D();print(c<d);print(d<c);}", "false\ntrue\n"},
	         {"class A{public:bool operator<(int value){return false;}};class C{public:bool operator<(A other){return true;}};void main(){A a=A();C c=C();print(a<c);}", "true\n"},
	         {"class B{};class D:public B{};class C{public:bool operator<(B other){return true;}};void main(){D d=D();C c=C();print(d<c);}", "true\n"},
	         {"class C{public:bool operator<(int value){return false;}};class D:public C{public:bool operator<(int value){return true;}};void main(){C c=D();print(c<1);print(1<c);print(1>c);}", "false\nfalse\nfalse\n"},
	         {"class C{public:virtual bool operator<(int value){return false;}};class D:public C{public:bool operator<(int value) override{return true;}};void main(){C c=D();print(c<1);print(1<c);print(1>c);}", "true\ntrue\ntrue\n"},
	         {"int order=0;class C{public:bool operator<(array<int> other){return true;}};array<int> left(){order=order*10+1;return array<int>{1};}C right(){order=order*10+2;return C();}void main(){print(left()<right());print(order);}", "true\n12\n"},
	         {"struct Item{public:int value=1;};Item item=Item();class C{public:bool operator<(Item other){return other.value==1;}};C receiver(){item.value=2;return C();}void main(){print(item<receiver());print(item.value);}", "true\n2\n"},
	         {"class Item{};class Base{public:bool operator<(Item other){return true;}};class Derived:public Base{public:bool operator<(int other){return false;}};void main(){Derived d=Derived();Item item=Item();print(d<item);print(item<d);}", "true\ntrue\n"},
	         {"import \"concurrent.vs\";optional<int> empty=optional<int>();export void check_empty(channel<bool> output){output.send(empty.has_value());}void main(){channel<bool> output=channel<bool>();int id=std::concurrent::spawn(\"check_empty\",output);std::concurrent::join_task(id);print(empty.has_value());print(output.recv());}", "false\nfalse\n"},
	         {"import \"concurrent.vs\";class State{public:static optional<int> empty=optional<int>();};export void check_empty(channel<bool> output){output.send(State::empty.has_value());}void main(){channel<bool> output=channel<bool>();int id=std::concurrent::spawn(\"check_empty\",output);std::concurrent::join_task(id);print(State::empty.has_value());print(output.recv());}", "false\nfalse\n"},
	         {"import \"concurrent.vs\";struct State{public:optional<int> empty;};State state=State();export void check_empty(channel<bool> output){output.send(state.empty.has_value());}void main(){channel<bool> output=channel<bool>();int id=std::concurrent::spawn(\"check_empty\",output);std::concurrent::join_task(id);print(state.empty.has_value());print(output.recv());}", "false\nfalse\n"},
	         {"import \"concurrent.vs\";optional<int> present=optional<int>(7);export void check_present(channel<int> output){output.send(present.get());}void main(){channel<int> output=channel<int>();int id=std::concurrent::spawn(\"check_present\",output);std::concurrent::join_task(id);print(present.get());print(output.recv());}", "7\n7\n"},
	         {"import \"concurrent.vs\";class Box{public:int value=7;};Box shared=Box();optional<Box> held=optional<Box>(shared);export void check_alias(channel<int> output){held.get().value=9;output.send(shared.value);}void main(){channel<int> output=channel<int>();int id=std::concurrent::spawn(\"check_alias\",output);std::concurrent::join_task(id);print(output.recv());print(shared.value);print(held.get().value);}", "9\n7\n7\n"},
	         {"class C{public:int value=0;bool operator<(int other){return value<other;}};void main(){C c=C();print((c=C())<1);c.value=2;print((c=C())<1);print(c.value);}", "true\ntrue\n0\n"},
	         {"class C{public:bool operator<(int value){return value==2;}};void main(){C c=C();int x=1;print((x=2)<c);print(x);}", "true\n2\n"},
	         {"class C{public:bool operator<(int value){return value==3;}};void main(){C c=C();int x=1;print((x+=2)<c);print(x);}", "true\n3\n"},
	         {"class B{public:virtual bool operator<(int x){return false;}};class D:public B{public:bool operator<(int x){return false;}};class E:public D{public:bool operator<(int x){return true;}};void main(){D d=E();print(d<1);print(1<d);}", "true\ntrue\n"},
	         {"class B{public:virtual bool operator<(int x){return false;}};class D:public B{public:bool operator<(bool x){return true;}};void main(){B b=D();print(b<1);print(1<b);}", "false\nfalse\n"},
	         {"namespace N{class A{};class C{public:bool operator<(A other){return true;}};}void main(){N::A a=N::A();N::C c=N::C();print(a<c);}", "true\n"}})
	{
		if (!EmitAndRunWithBundle(Case.first, Case.second, "member ordering and optional worker snapshots match Core"))
		{
			return EXIT_FAILURE;
		}
	}

	// Null literals participate in checked member selection without losing static dispatch.
	for (const auto& Case : {
	         std::pair<const char*, const char*>{"class A{};class C{public:bool operator<(A x){return x==nullptr;}};void main(){C c=C();print(c<nullptr);print(nullptr<c);print(nullptr>c);}", "true\ntrue\ntrue\n"},
	         {"class A{};class B{public:bool operator<(A x){return false;}};class D:public B{public:bool operator<(A x){return true;}};void main(){B b=D();A a=nullptr;print(b<a);print(a<b);print(a>b);print(b<nullptr);print(nullptr<b);print(nullptr>b);}", "false\nfalse\nfalse\nfalse\nfalse\nfalse\n"},
	         {"class A{};class B{public:virtual bool operator<(A x){return false;}};class D:public B{public:bool operator<(A x){return true;}};void main(){B b=D();print(b<nullptr);print(nullptr<b);print(nullptr>b);}", "true\ntrue\ntrue\n"},
	         {"class A{};class C{public:bool operator<(A x){return x==nullptr;}};void main(){C c=C();bool flag=true;print(c<(flag?nullptr:nullptr));print((flag?nullptr:nullptr)<c);flag=false;print((flag?nullptr:nullptr)>c);}", "true\ntrue\ntrue\n"},
	         {"class A{};class B{public:bool operator<(A x){return false;}};class D:public B{public:bool operator<(A x){return true;}};void main(){B b=D();A a=nullptr;bool flag=true;print(b<(flag?a:a));print((flag?a:a)<b);print((flag?a:a)>b);}", "false\nfalse\nfalse\n"}})
	{
		if (!EmitAndRunWithBundle(Case.first, Case.second, "null operands retain static operator dispatch"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!EmitAndRequireBothFailureWithBundle(
	        "class A{};class C{public:bool operator<(A x){return true;}};void main(){C c=nullptr;print(c<nullptr);}",
	        "diag.bound_method_call_requires_object", "bound method call requires object",
	        "typed null receivers retain the bound method object requirement"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailureWithBundle(
	        "bool first=early();C later=C();class C{public:bool operator<(int other){return true;}};bool early(){return later<1;}void main(){print(first);}",
	        "diag.bound_method_call_requires_object", "bound method call requires object",
	        "reserved record receiver does not use integer ordering"))
	{
		return EXIT_FAILURE;
	}

	// Python built-in container ordering must not bypass the Core numeric guard.
	for (const char* Source : {
	         "void main(){array<int> a={1};array<int> b={2};print(a<b);}",
	         "template<typename T> bool less(T a,T b){return a<b;}void main(){array<int> a={1};array<int> b={2};print(less<array<int>>(a,b));}",
	         "void main(){deque<int> a=deque<int>();a.push_back(1);deque<int>b=deque<int>();b.push_back(2);print(a<b);}",
	         "void main(){set<int>a=set<int>();a.add(1);set<int>b=set<int>();b.add(1);b.add(2);print(a<b);}",
	         "void main(){queue<int>a=queue<int>();a.push(1);queue<int>b=queue<int>();b.push(2);print(a<b);}",
	         "void main(){stack<int>a=stack<int>();a.push(1);stack<int>b=stack<int>();b.push(2);print(a<b);}"})
	{
		if (!EmitAndRequireBothFailureWithBundle(Source, "diag.comparison_operands_must_be_numeric",
		        "comparison operands must be numeric", "container ordering fails in VM, single file and bundle"))
		{
			return EXIT_FAILURE;
		}
	}

	// Static type hints must not replace the actual payload or generic text ordering.
	for (const auto& Case : {
	         std::pair<const char*, const char*>{"string first=read_a();string second=read_b();string a=\"x\";string b=\"y\";string read_a(){return a;}string read_b(){return b;}void main(){print(first==second);print(first!=second);}", "true\nfalse\n"},
	         {"string first=read_a();string second=read_b();string a=\"x\";string b=\"y\";string read_a(){return a;}string read_b(){return b;}template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(eq<string>(first,second));print(ne<string>(first,second));}", "true\nfalse\n"},
	         {"wstring first=read_a();wstring second=read_b();wstring a=L\"x\";wstring b=L\"y\";wstring read_a(){return a;}wstring read_b(){return b;}void main(){print(first==second);print(first!=second);print(first<second);print(first<=second);print(first>second);print(first>=second);}", "true\nfalse\nfalse\ntrue\nfalse\ntrue\n"},
	         {"wstring first=read_a();wstring second=read_b();wstring a=L\"x\";wstring b=L\"y\";wstring read_a(){return a;}wstring read_b(){return b;}template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(eq<wstring>(first,second));print(ne<wstring>(first,second));print(lt<wstring>(first,second));print(le<wstring>(first,second));print(gt<wstring>(first,second));print(ge<wstring>(first,second));}", "true\nfalse\nfalse\ntrue\nfalse\ntrue\n"},
	         {"template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(eq<wstring>(L\"𐀀\",L\"\"));print(ne<wstring>(L\"𐀀\",L\"\"));print(lt<wstring>(L\"𐀀\",L\"\"));print(le<wstring>(L\"𐀀\",L\"\"));print(gt<wstring>(L\"𐀀\",L\"\"));print(ge<wstring>(L\"𐀀\",L\"\"));}", "false\ntrue\ntrue\ntrue\nfalse\nfalse\n"},
	         {"void main(){print(L\"𐀀\"==L\"\");print(L\"𐀀\"!=L\"\");print(L\"𐀀\"<L\"\");print(L\"𐀀\"<=L\"\");print(L\"𐀀\">L\"\");print(L\"𐀀\">=L\"\");}", "false\ntrue\ntrue\ntrue\nfalse\nfalse\n"},
	         {"template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(eq<int>(1,2));print(ne<int>(1,2));print(lt<int>(1,2));print(le<int>(1,2));print(gt<int>(1,2));print(ge<int>(1,2));print(eq<double>(1.5,2.0));print(ne<double>(1.5,2.0));print(lt<double>(1.5,2.0));print(le<double>(1.5,2.0));print(gt<double>(1.5,2.0));print(ge<double>(1.5,2.0));}", "false\ntrue\ntrue\ntrue\nfalse\nfalse\nfalse\ntrue\ntrue\ntrue\nfalse\nfalse\n"},
	         {"bool first=read_a();bool second=read_b();bool a=true;bool b=false;bool read_a(){return a;}bool read_b(){return b;}template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(eq<bool>(first,second));print(ne<bool>(first,second));print(lt<bool>(first,second));print(le<bool>(first,second));print(gt<bool>(first,second));print(ge<bool>(first,second));}", "true\nfalse\nfalse\ntrue\nfalse\ntrue\n"},
	         {"class C{public:bool operator<(bool other){return other;}};void main(){C c=C();print(c<true);print(c<false);}", "true\nfalse\n"},
	         {"enum E{A=0,B=1};template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(eq<E>(E::A,E::B));print(ne<E>(E::A,E::B));print(lt<E>(E::A,E::B));print(le<E>(E::A,E::B));print(gt<E>(E::A,E::B));print(ge<E>(E::A,E::B));}", "false\ntrue\ntrue\ntrue\nfalse\nfalse\n"}})
	{
		if (!EmitAndRunWithBundle(Case.first, Case.second, "text and generic comparisons preserve Core runtime payloads"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto& Case : {
	         std::tuple<const char*, const char*, const char*>{"string first=read_a();string second=read_b();string a=\"x\";string b=\"y\";string read_a(){return a;}string read_b(){return b;}void main(){print(first==\"x\");}", "diag.cannot_compare_mismatched_string_types", "cannot compare strings with different runtime types"},
	         {"wstring first=read_a();wstring second=read_b();wstring a=L\"x\";wstring b=L\"y\";wstring read_a(){return a;}wstring read_b(){return b;}void main(){print(L\"x\"<first);}", "diag.comparison_operands_must_be_numeric", "cannot compare strings with different runtime types"},
	         {"wstring first=read_a();wstring second=read_b();wstring a=L\"x\";wstring b=L\"y\";wstring read_a(){return a;}wstring read_b(){return b;}template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(eq<wstring>(first,L\"x\"));}", "diag.cannot_compare_mismatched_string_types", "cannot compare strings with different runtime types"},
	         {"wstring first=read_a();wstring second=read_b();wstring a=L\"x\";wstring b=L\"y\";wstring read_a(){return a;}wstring read_b(){return b;}template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(ge<wstring>(L\"x\",first));}", "diag.comparison_operands_must_be_numeric", "cannot compare strings with different runtime types"},
	         {"void main(){bool x=false;bool y=true;print(x<y);}", "diag.comparison_operands_must_be_numeric", "comparison operands must be numeric"},
	         {"template<typename T>bool eq(T x,T y){return x==y;}template<typename T>bool ne(T x,T y){return x!=y;}template<typename T>bool lt(T x,T y){return x<y;}template<typename T>bool le(T x,T y){return x<=y;}template<typename T>bool gt(T x,T y){return x>y;}template<typename T>bool ge(T x,T y){return x>=y;}void main(){print(le<bool>(false,true));}", "diag.comparison_operands_must_be_numeric", "comparison operands must be numeric"}})
	{
		if (!EmitAndRequireBothFailureWithBundle(std::get<0>(Case), std::get<1>(Case), std::get<2>(Case),
		        "invalid actual comparison payloads fail in VM, single file and bundle"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto& Case : {
	         std::pair<const char*, const char*>{
	             "class B{public:static int value=7;};B object=B();int count=0;"
	             "B receiver(){count++;B::value=8;return object;}"
	             "void main(){print(receiver().value);print(count);}", "8\n1\n"},
	         {"class B{public:static array<int> values={7};};class D:public B{};D object=D();int order=0;"
	          "D receiver(){order=order*10+1;return object;}int index(){order=order*10+2;return 0;}"
	          "void main(){print(receiver().values[index()]);print(order);}", "7\n12\n"},
	         {"class Item{public:int read(){return 7;}};class B{public:static Item value=Item();};"
	          "B object=B();int count=0;B receiver(){count++;return object;}"
	          "void main(){print(receiver().value.read());print(count);}", "7\n1\n"},
	         {"class B{public:static int value=7;};class Root{public:static B child=B();};"
	          "Root object=Root();int count=0;Root receiver(){count++;return object;}"
	          "void main(){print(receiver().child.value);print(count);}", "7\n1\n"},
	         {"class B{public:static int value=7;};class D:public B{};"
	          "void main(){print(B::value);print(D::value);}", "7\n7\n"}})
	{
		if (!EmitAndRunWithBundle(Case.first, Case.second, "static field reads evaluate and discard value receivers exactly once"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const auto& Case : {
	         std::pair<const char*, const char*>{"array<int> values={3};values.reserve(first);print(values[0]);", "3\n"},
	         {"map<int,int> values={1:3};values.reserve(first);print(values[1]);", "3\n"},
	         {"set<int> values={3};values.reserve(first);print(values.has(3));", "true\n"}})
	{
		const std::string Source = "int first=read();int second=5;int read(){return second;}void main(){" +
		    std::string(Case.first) + "}";
		if (!EmitAndRunWithBundle(Source, Case.second, "reserve accepts the actual integer zero from a reserved slot"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!EmitAndRequirePythonFailure(
	        "void main(){string text=\"abc\";print(text[0]);}",
	        "string indexing is unsupported in Python AOT", "string indexing does not silently use Python semantics"))
	{
		return EXIT_FAILURE;
	}

	for (const char* Body : {
	         "array<bool> values=array<bool>{first};",
	         "array<bool> values=array<bool>();values.push(first);",
	         "array<bool> values=array<bool>{true};values[0]=first;",
	         "array<bool> values=array<bool>{true};values.remove(first);",
	         "map<int,bool> values=map<int,bool>();values[0]=first;",
	         "deque<bool> values=deque<bool>();values.push_back(first);",
	         "queue<bool> values=queue<bool>();values.push(first);",
	         "stack<bool> values=stack<bool>();values.push(first);"})
	{
		const std::string Source = "bool first=read();bool second=true;bool read(){return second;}void main(){" + std::string(Body) + "}";
		if (!EmitAndRequirePythonFailure(Source, "value type mismatch", "dynamic containers reject a reserved integer zero as bool"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const char* Body : {
	         "map<string,int> values=map<string,int>();values.has(first);",
	         "map<string,int> values=map<string,int>();values.remove(first);",
	         "set<string> values=set<string>();values.has(first);",
	         "set<string> values=set<string>();values.remove(first);"})
	{
		const std::string Source = "string first=read();string second=\"ok\";string read(){return second;}void main(){" + std::string(Body) + "}";
		if (!EmitAndRequirePythonFailure(Source, "value type mismatch", "container queries validate the actual reserved key value"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!EmitAndRequirePythonFailure(
	        "import \"concurrent.vs\";long first=read();int64 second=7i64;long read(){return second;}"
	        "void main(){channel<long> c=channel<long>();c.send(first);}",
	        "channel_send value type mismatch", "integer type aliases preserve reserved slot identity"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "bool first=read();bool second=true;bool read(){return second;}"
	        "void main(){pair<int,bool> p=pair<int,bool>();p.second=first;print(p.second);"
	        "optional<bool> value=optional<bool>(first);print(value.get());}",
	        "0\n0\n", "pair and optional retain same-type zero values"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "import \"concurrent.vs\";bool first=read();bool second=true;"
	        "bool read(){return second;}void main(){channel<bool> c=channel<bool>();c.send(first);}",
	        "channel_send value type mismatch", "reserved integer zero is not a bool channel payload"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(R"VS(constexpr int plus(int n){if constexpr(true)return n+1;else return nonexistent();}constexpr int count=plus(3);constexpr uint64 maximum=18446744073709551615u64;enum class Mode{First=plus(1),Second};constexpr Mode selected=Mode::Second;struct Config{static constexpr int count=plus(4);};void main(){if constexpr(false){struct Discarded{int read(){return missing();}};int discarded=nonexistent();}constexpr int n=plus(5);print(n);print(count);print(Config::count);int runtime=7;print(plus(runtime));print(maximum);print(cast<int>(selected));constexpr auto narrowed=cast<int16>(7);print(narrowed);})VS",
	        "6\n4\n5\n8\n18446744073709551615\n3\n7\n", "constexpr source emission and ordinary calls"))
	{
		return EXIT_FAILURE;
	}
	{
		std::string Output, Error;
		if (VeroScript::EmitPythonFromSource("constexpr int size(){return 3;}int length(int values[size()]){return len(values);}void main(){}",
		        "constexpr-array-boundary.vs", Output, Error) ||
		    !Output.empty() || Error.find("not supported") == std::string::npos)
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
		if (VeroScript::EmitPythonFromSource(Source, "template-aot.vs", Output, Error) || !Output.empty() || Error.find("Core AOT") == std::string::npos)
		{
			std::cerr << "unsupported templates must fail before source emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	for (const auto* Source : {"struct Outer{struct Item{};};int main(){return 0;}",
	         "int main(){struct Item{};return 0;}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitPythonFromSource(Source, "scoped-aot.vs", Output, Error) || !Output.empty() || Error.find("nested and local types") == std::string::npos)
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
		if (VeroScript::EmitPythonFromSource(Source, "variant-aot.vs", Output, Error) || !Output.empty() || Error.find("diag.aot_variant_unsupported") == std::string::npos)
		{
			std::cerr << "variant must fail before standalone emission: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!EmitAndRun(R"VS(import "io.vs"; void main(){
assert(std::io::read_line()=="a\rb"); assert(std::io::read_line()=="last"); print("input passed");
})VS",
	        "input passed\n", "input retains interior CR and strips one terminal CR", "a\rb\r\nlast\r"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(R"VS(import "string.vs"; import "math.vs"; import "algorithm.vs"; import "random.vs";
void main(){
assert(std::strings::starts_with("abc","ab") && std::strings::ends_with("abc","bc"));
assert(std::strings::last_find("aaa","aa")==1 && std::strings::trim("\v\f a \r\n")=="a");
assert(std::strings::repeat("ab",3)=="ababab" && std::strings::replace("aaaaa","aa","x")=="xxa");
assert(std::clamp_int(9,0,8)==8);
array<int> values={1,2,3}; std::algorithm::reverse(values); assert(values[0]==3);
std::algorithm::rotate(values,1); assert(values[0]==2 && values[2]==3);
auto engine=std::random::make_engine(123u); assert(std::random::next_uint(engine)==31682556u);
std::algorithm::shuffle(values,engine); std::algorithm::sort(values); assert(values[0]==1 && values[2]==3);
assert(format("{:x}",18446744073709551615u64)=="ffffffffffffffff"); print("portable passed");
})VS",
	        "portable passed\n", "portable foundation helpers match VM"))
	{
		return EXIT_FAILURE;
	}
	for (const char* Source : {
	         "import \"string.vs\"; void main(){print(std::strings::parse<int>(\"42\"));}",
	         "import \"string.vs\"; void main(){fn(string,int64)->string f=std::strings::repeat; print(f(\"a\",1.0));}",
	         "import \"concurrent.vs\"; void main(){auto lock=std::concurrent::make_mutex();}",
	         "import \"concurrent.vs\"; int answer(){return 42;} void main(){auto t=std::concurrent::spawn_task<int>(answer); print(t.await());}"})
	{
		std::string Output = "unchanged", Error;
		if (VeroScript::EmitPythonFromSource(Source, "foundation-runtime.vs", Output, Error, {VEROSCRIPT_STDLIB_DIR}) || !Output.empty() || Error.find("standard-library foundation operations require") == std::string::npos)
		{
			std::cerr << "FAIL: runtime foundation must be rejected before standalone output: " << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		const std::string Source = "void main(){print(\"alias-ok\");}";
		std::string Python = Source, Error;
		if (!Expect(EmitPythonFromSource(Python, "alias-source.vs", Python, Error),
		        "source may borrow the output string") ||
		    !RunPython(Python, "alias-ok\n", "aliased source emission executes"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		std::string InputAndError = Source, Generated;
		if (!Expect(EmitPythonFromSource(InputAndError, "alias-error.vs", Generated, InputAndError),
		        "source may borrow the error string") ||
		    !Expect(InputAndError.empty(), "successful emission clears the error"))
		{
			return EXIT_FAILURE;
		}

		for (const bool WithEnvironment : {false, true})
		{
			FCodegenOptions Options;
			Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			if (WithEnvironment)
			{
				FModuleEnvironmentDesc Desc;
				Desc.EntryOwnerId = "app";
				Desc.Scopes = {{"app", {}}};
				Desc.Modules = {{"entry.vs", "app.entry", "veroscript-module:/alias/entry.vs", "app", "v1", Source}};
				Options.ModuleEnvironment = FModuleEnvironment::Create(std::move(Desc), Error);
				if (!Expect(Options.ModuleEnvironment != nullptr, "create alias test environment")) return EXIT_FAILURE;
			}
			const std::string Path = WithEnvironment ? "veroscript-module:/alias/entry.vs" : "alias-entry.vs";
			Options.SourceOverlays = {{Path, Source}};
			FPythonCodegenBackend Backend;
			FCodegenResult Result;
			Result.Output = Source;
			Result.Error = Path;
			if (!Expect(Backend.EmitFromSource(Result.Output, Result.Error, Options, Result),
			        "backend source and label may borrow the previous result") ||
			    !RunPython(Result.Output, "alias-ok\n", "aliased backend source executes"))
			{
				std::cerr << Result.Error << '\n';
				return EXIT_FAILURE;
			}
			Result.Error = Path;
			if (!Expect(Backend.EmitFromFile(Result.Error, Options, Result),
			        "backend path may borrow the previous result") ||
			    !RunPython(Result.Output, "alias-ok\n", "aliased backend file executes"))
			{
				std::cerr << Result.Error << '\n';
				return EXIT_FAILURE;
			}
		}
		const auto Root = std::filesystem::temp_directory_path() / ("python-aot-alias-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Root);
		struct FCleanup
		{
			~FCleanup()
			{
				std::error_code E;
				std::filesystem::remove_all(Path, E);
			}

			std::filesystem::path Path;
		} Cleanup{Root};
		const auto Entry = Root / "main.vs";
		{
			std::ofstream File(Entry, std::ios::binary);
			File << Source;
		}
		const std::string InputPath = PathUtil::ToUtf8(Entry);
		for (const bool WithOptions : {false, true})
		{
			FPythonAotSourceBundle Bundle;
			Bundle.Files = {{"old.vs", Source}};
			Bundle.ManifestJson = "alias-source.vs";
			Error = "alias_bundle";
			FCodegenOptions Options;
			Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			const bool Emitted = WithOptions
			    ? EmitPythonSourceBundleFromSource(Bundle.Files.front().Contents, Bundle.ManifestJson, Error, Options, Bundle, Error)
			    : EmitPythonSourceBundleFromSource(Bundle.Files.front().Contents, Bundle.ManifestJson, Error, Bundle, Error);
			if (!Expect(Emitted, "bundle source, label and name may borrow output storage"))
			{
				std::cerr << Error << '\n';
				return EXIT_FAILURE;
			}
			const auto Output = Root / (WithOptions ? "options" : "legacy");
			Error = PathUtil::ToUtf8(Output);
			if (!Expect(WritePythonSourceBundle(Bundle, Error, Error),
			        "bundle destination may borrow the error string") ||
			    !RunPythonPath(Output, "alias-ok\n", "aliased bundle executes"))
			{
				std::cerr << Error << '\n';
				return EXIT_FAILURE;
			}
			Bundle.ManifestJson = "file_bundle";
			Error = InputPath;
			const bool FromFile = WithOptions
			    ? EmitPythonSourceBundleFromFile(Error, Bundle.ManifestJson, Options, Bundle, Error)
			    : EmitPythonSourceBundleFromFile(Error, Bundle.ManifestJson, Bundle, Error);
			if (!Expect(FromFile, "bundle file path and name may borrow output storage") ||
			    !Expect(Bundle.ManifestJson.find("\"name\":\"file_bundle\"") != std::string::npos,
			        "aliased file bundle name is retained"))
			{
				std::cerr << Error << '\n';
				return EXIT_FAILURE;
			}
		}
		FPythonAotSourceBundle Reused;
		Reused.Files = {{"old.py", Python}};
		Reused.ManifestJson = "reused_bundle";
		if (!Expect(
		        BuildPythonSourceBundle(Reused.Files.front().Contents, Reused.ManifestJson, Reused, Error),
		        "repack inputs may borrow the bundle being replaced") ||
		    !Expect(Reused.Files.back().Contents == Python, "repacking retains aliased program bytes"))
		{
			return EXIT_FAILURE;
		}

		Error = InputPath;
		if (!Expect(EmitPythonFromFile(Error, PathUtil::ToUtf8(Root / "output.py"), Error),
		        "file input path may borrow the error string"))
		{
			return EXIT_FAILURE;
		}

		Error = PathUtil::ToUtf8(Root / "output.py");
		if (!Expect(EmitPythonFromFile(InputPath, Error, Error),
		        "file output path may borrow the error string") ||
		    !RunPythonPath(Root / "output.py", "alias-ok\n", "aliased file output executes"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		FModuleEnvironmentDesc D;
		D.EntryOwnerId = "app";
		D.Scopes = {{"app", {"library"}}, {"library", {}}};
		D.Modules = {{"library.vs", "library.api", "veroscript-module:/library/api.vs", "library", "aot-example", std::string("export int answer(){return 42;}")}};
		std::string Error;
		FCodegenOptions O;
		O.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		O.ModuleEnvironment = FModuleEnvironment::Create(D, Error);
		FPythonCodegenBackend Backend;
		FCodegenResult Result;
		if (!O.ModuleEnvironment ||
		    !Backend.EmitFromSource(
		        "import \"library.vs\"; int main(){return answer();}", "<environment-aot>", O, Result))
		{
			std::cerr << "module environment AOT failed: " << Error << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		D.Modules.push_back({"main.vs", "app.main", "veroscript-module:/app/main.vs", "app", "entry-v1",
		    std::string("import \"library.vs\"; void main(){print(answer());}")});
		O.ModuleEnvironment = FModuleEnvironment::Create(D, Error);
		FPythonAotSourceBundle Bundle;
		if (!Expect(O.ModuleEnvironment && Backend.EmitFromFile("veroscript-module:/app/main.vs", O, Result),
		        "file backend reads a virtual environment entry") ||
		    !RunPython(Result.Output, "42\n", "virtual environment entry execution") ||
		    !Expect(EmitPythonSourceBundleFromFile(
		                "veroscript-module:/app/main.vs", "environment", O, Bundle, Error),
		        "file bundle reads a virtual environment entry") ||
		    !Expect(!Bundle.Files.empty() && !Bundle.ManifestJson.empty(), "virtual bundle is complete"))
		{
			std::cerr << Error << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		O.SourceOverlays = {{"veroscript-module:/app/main.vs", "void main(){print(51);}"}};
		if (!Expect(Backend.EmitFromFile("veroscript-module:/app/main.vs", O, Result),
		        "file backend honors entry overlay") ||
		    !RunPython(Result.Output, "51\n", "entry overlay execution"))
		{
			return EXIT_FAILURE;
		}

		O.SourceOverlays.clear();
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
		if (!Expect(!EmitPythonSourceBundleFromFile(
		                "veroscript-module:/app/main.vs", "environment", O, Bundle, Error) &&
		            Bundle.Files.empty() && Error.find("native package contracts") != std::string::npos,
		        "file bundle rejects native contracts without partial output"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		const auto Path = (std::filesystem::temp_directory_path() / "veroscript-python-overlay-entry.vs").generic_string();
		Options.SourceOverlays = {{Path, "void main(){print(GeneralGenerated());}"}};
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Descriptor;
		Descriptor.ProcessorId = "python.aot.general";
		Descriptor.Version = "1";
		Descriptor.Phases = CompilePhaseMask(ECompileProcessingPhase::Members);
		Descriptor.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
		FCompileSubscription Subscription;
		Subscription.Event = ECompileProcessingEvent::Module;
		Descriptor.Subscriptions = {Subscription};
		Descriptor.Factory = []
		{
			return std::make_unique<FPythonAotGeneralProcessor>();
		};
		std::string Error;
		if (!Expect(Registry->Register(std::move(Descriptor), Error), "register AOT general processor")) return EXIT_FAILURE;
		Registry->Freeze();
		Options.CompileProcessing.Registry = Registry;
		FPythonCodegenBackend Backend;
		FCodegenResult Result;
		FPythonAotSourceBundle Bundle;
		if (!Expect(Backend.EmitFromFile(Path, Options, Result),
		        "file backend accepts an overlay without a disk file") ||
		    !RunPython(Result.Output, "63\n", "physical entry overlay execution") ||
		    !Expect(EmitPythonSourceBundleFromFile(Path, "overlay", Options, Bundle, Error),
		        "file bundle accepts an overlay without a disk file"))
		{
			std::cerr << Error << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(Backend.EmitFromSource(
		                "void main(){print(GeneralGenerated());}", "<general-processing>", Options, Result),
		        "source backend preserves general compilation processing") ||
		    !RunPython(Result.Output, "63\n", "general processor output execution") ||
		    !Expect(EmitPythonSourceBundleFromSource("void main(){print(GeneralGenerated());}",
		                "<general-processing>", "general", Options, Bundle, Error),
		        "source bundle preserves general compilation processing"))
		{
			std::cerr << Error << Result.Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(!Backend.EmitFromFile({}, Options, Result) && Result.Output.empty() &&
		            Result.Error.find("path must not be empty") != std::string::npos,
		        "empty file backend entry is rejected") ||
		    !Expect(!EmitPythonSourceBundleFromFile({}, "empty", Options, Bundle, Error) &&
		            Bundle.Files.empty() && Error.find("path must not be empty") != std::string::npos,
		        "empty file bundle entry is rejected"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::string Output, MemoryError;
		if (!Expect(
		        !VeroScript::EmitPythonFromSource("void main(){std::memory::native_addresses_available();}",
		            "memory-aot.vs", Output, MemoryError) &&
		            MemoryError.find("std::memory requires") != std::string::npos,
		        "actual memory use is rejected by standalone AOT"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!VeroScript::EmitPythonFromSource("void main(){auto b=new std::memory::buffer(4);}",
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
			        !VeroScript::EmitPythonFromSource(Source, "memory-signature.vs", Output, MemoryError) &&
			            MemoryError.find("std::memory requires") != std::string::npos,
			        "runtime-only signature is rejected"))
			{
				return EXIT_FAILURE;
			}
		}
		for (const char* Source :
		    {"import \"memory.vs\"; void main(){print(1);}", "import \"core.vs\"; void main(){print(1);}"})
		{
			if (!Expect(VeroScript::EmitPythonFromSource(
			                Source, "unused-memory.vs", Output, MemoryError, {VEROSCRIPT_STDLIB_DIR}),
			        "unused memory/core import remains supported"))
			{
				std::cerr << MemoryError << '\n';
				return EXIT_FAILURE;
			}
		}
	}

	(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
	if (!EmitAndRun(R"VS(import "string.vs";
void main()
{
    array<int> values = {4, 7};
    int64 index = 1i64;
    uint64 unsignedIndex = 0u64;
    int16 smallIndex = 0;
    assert(len(values) == 2i64, "len must return int64");
    assert(len("abc") == 3i64, "string length must return int64");
    assert(values.capacity() >= 2i64, "capacity must return int64");
    values.reserve(16i64);
    values[index] += 3;
    values[unsignedIndex] = values[smallIndex] + 1;
    assert(values[index] == 10 && values[unsignedIndex] == 5, "wide indexing failed");
    int total = 0;
    for (int value : values) { total += value; }
    assert(total == 15, "wide foreach failed");
    values.remove_at(0i64);
    int narrowed = len(values);
    assert(narrowed == 1, "checked implicit narrowing failed");
    assert(std::strings::substring("abc", 1i64, 9223372036854775807i64) == "bc", "wide substring failed");
    assert(std::strings::find("abc", "b") == 1i64 && std::strings::find("abc", "z") == -1i64, "wide find failed");
    assert(std::strings::char_code("abc", 1i64) == 98, "wide character index failed");
    print("wide lengths passed");
}
)VS",
	        "wide lengths passed\n", "wide length and index contract"))
	{
		return EXIT_FAILURE;
	}

	std::string LengthPython;
	std::string LengthError;
	if (!Expect(VeroScript::EmitPythonFromSource(
	                "void main() {}", "length-bounds.vs", LengthPython, LengthError),
	        "emit length boundary probes"))
	{
		std::cerr << LengthError << '\n';
		return EXIT_FAILURE;
	}
	// Probe synthetic lengths to cover the boundary without allocating gigabytes.
	LengthPython += R"PY(
class LengthProbe:
    def __init__(self, length: int) -> None:
        self.length = length

    def __len__(self) -> int:
        return self.length


for length in (0, 2147483647, 2147483648, 4294967295):
    if length <= _vs_sys.maxsize:
        assert _vs_len(LengthProbe(length)) == length
    assert _vs_checked_length(length) == length
assert _vs_checked_length(9223372036854775807) == 9223372036854775807
for length in (-1, 9223372036854775808):
    try:
        _vs_checked_length(length)
    except OverflowError as error:
        assert "len result exceeds int64 range" in str(error)
    else:
        raise AssertionError("out-of-range length conversion was accepted")
assert _vs_len("\u4f60\u597d") == 6
assert _vs_wlen("\U0001f600") == 2
print("length bounds passed")
)PY";
	if (!RunPython(LengthPython, "length bounds passed\n", "checked length boundaries"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Payload { int value; Payload(int input) { value = input; } }\n"
	        "class Holder { public: const int id; const Payload payload; "
	        "Holder(int input) : payload(input + 1), id(input) {} }\n"
	        "void main() { Holder value = Holder(4); print(value.id); print(value.payload.value); }\n",
	        "4\n5\n",
	        "constructor field initializers"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "using Tiny = int8; "
	        "struct InvalidAliasFieldInit { const Tiny value; "
	        "InvalidAliasFieldInit() : value(1, 2) {} } "
	        "void main() {}",
	        "scalar field initializer for 'value' expects at most one argument",
	        "scalar alias constructor field initializer rejects excess arguments"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "enum class Mode { Off = 0, On = 1 } "
	        "struct InvalidEnumFieldInit { const Mode value; "
	        "InvalidEnumFieldInit() : value(0, 1) {} } "
	        "void main() {}",
	        "scalar field initializer for 'value' expects at most one argument",
	        "enum constructor field initializer rejects excess arguments"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "int constructor_init_order = 0;\n"
	        "int mark_constructor_init(int digit) {\n"
	        "    constructor_init_order = constructor_init_order * 10 + digit;\n"
	        "    return digit;\n"
	        "}\n"
	        "class InitOrderBase {\n"
	        "public:\n"
	        "    InitOrderBase(int ignored) {}\n"
	        "}\n"
	        "class InitOrderDerived : InitOrderBase {\n"
	        "public:\n"
	        "    int first;\n"
	        "    int second;\n"
	        "    InitOrderDerived() : second(mark_constructor_init(2)), "
	        "InitOrderBase(mark_constructor_init(9)), first(mark_constructor_init(1)) {}\n"
	        "}\n"
	        "void main() {\n"
	        "    InitOrderDerived value = InitOrderDerived();\n"
	        "    print(constructor_init_order);\n"
	        "}\n",
	        "912\n",
	        "constructor initializer execution order"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct DefaultToken {\n"
	        "    int value = 3;\n"
	        "    DefaultToken() { value = 4; }\n"
	        "}\n"
	        "using Tiny = int8;\n"
	        "struct FieldInitDefaults {\n"
	        "    const Tiny explicit_byte;\n"
	        "    const Tiny explicit_alias_zero;\n"
	        "    const Tiny explicit_alias_value;\n"
	        "    const int explicit_zero;\n"
	        "    const int implicit_zero;\n"
	        "    const string explicit_text;\n"
	        "    const wstring explicit_wide_text;\n"
	        "    int mutable_value;\n"
	        "    const DefaultToken token;\n"
	        "    FieldInitDefaults(int input, Tiny alias_input) : token(), mutable_value(input), "
	        "explicit_wide_text(), explicit_text(), explicit_zero(), explicit_alias_value(alias_input), "
	        "explicit_alias_zero(), explicit_byte(input) {}\n"
	        "}\n"
	        "void main() {\n"
	        "    FieldInitDefaults defaults = FieldInitDefaults(9, 8);\n"
	        "    print(defaults.explicit_byte);\n"
	        "    print(defaults.explicit_alias_zero);\n"
	        "    print(defaults.explicit_alias_value);\n"
	        "    print(defaults.explicit_zero);\n"
	        "    print(defaults.implicit_zero);\n"
	        "    print(len(defaults.explicit_text));\n"
	        "    print(len(defaults.explicit_wide_text));\n"
	        "    print(defaults.mutable_value);\n"
	        "    print(defaults.token.value);\n"
	        "}\n",
	        "9\n0\n8\n0\n0\n0\n0\n9\n4\n",
	        "constructor field initializer default semantics"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"algorithm.vs\";\n"
	        "import \"numeric.vs\";\n"
	        "import \"random.vs\";\n"
	        "void main() {\n"
	        "    array<int> values = {9, 2, 7, 2, 5};\n"
	        "    std::algorithm::sort(values);\n"
	        "    print(values[0]);\n"
	        "    print(std::algorithm::binary_search(values, 7));\n"
	        "    print(std::numeric::sum(values));\n"
	        "    print(std::numeric::accumulate(values, 10));\n"
	        "    print(std::numeric::average(values));\n"
	        "    array<string> pieces = {\"vero\", \"script\"};\n"
	        "    print(std::numeric::accumulate(pieces, \"\"));\n"
	        "    array<unsigned int> first = std::random::make_engine(123u);\n"
	        "    array<unsigned int> second = std::random::make_engine(123u);\n"
	        "    print(std::random::next_uint(first) == std::random::next_uint(second));\n"
	        "    array<int> copied = std::algorithm::copy(values);\n"
	        "    print(std::algorithm::equal(values, copied));\n"
	        "    print(std::algorithm::contains(copied, 9));\n"
	        "    print(std::algorithm::is_sorted(copied));\n"
	        "    print(std::algorithm::min_index(copied));\n"
	        "    print(std::algorithm::max_index(copied));\n"
	        "    std::algorithm::rotate(copied, 2);\n"
	        "    print(copied[0]);\n"
	        "    std::algorithm::fill(copied, 6);\n"
	        "    array<int> filled = {6, 6, 6, 6, 6};\n"
	        "    print(std::algorithm::equal(copied, filled));\n"
	        "    array<unsigned int> bounded_a = std::random::make_engine(2026u);\n"
	        "    array<unsigned int> bounded_b = std::random::make_engine(2026u);\n"
	        "    print(std::random::next_bounded_uint(bounded_a, 17u)\n"
	        "        == std::random::next_bounded_uint(bounded_b, 17u));\n"
	        "    int ranged = std::random::next_int(bounded_a, -5, 6);\n"
	        "    print(ranged >= -5 && ranged < 6);\n"
	        "    double ranged_double = std::random::next_double_range(bounded_a, 2.0, 3.0);\n"
	        "    print(ranged_double >= 2.0 && ranged_double < 3.0);\n"
	        "    print(std::chrono::now() > 0.0);\n"
	        "    print(std::chrono::steady_now() > 0.0);\n"
	        "}\n",
	        "2\ntrue\n25\n35\n5\nveroscript\ntrue\ntrue\ntrue\ntrue\n0\n4\n5\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n",
	        "generic standard-library modules"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"algorithm.vs\";\n"
	        "class OrderedValue {\n"
	        "public:\n"
	        "    int key;\n"
	        "    OrderedValue(int value) { key = value; }\n"
	        "    bool operator<(OrderedValue other) { return key < other.key; }\n"
	        "}\n"
	        "void main() {\n"
	        "    array<OrderedValue> values = {OrderedValue(1), OrderedValue(3)};\n"
	        "    assert(std::algorithm::binary_search(values, OrderedValue(3)));\n"
	        "    assert(std::algorithm::is_sorted(values));\n"
	        "    assert(!std::algorithm::binary_search(values, OrderedValue(2)));\n"
	        "    print(\"ok\");\n"
	        "}\n",
	        "ok\n",
	        "ordered algorithms use user-defined operator less"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"core.vs\";\n"
	        "void main() {\n"
	        "    array<unsigned int> full = std::random::make_engine(4071982377u);\n"
	        "    assert(std::random::next_bounded_uint(full, 4294967295u) == 0u);\n"
	        "    array<unsigned int> narrow = std::random::make_engine(12345u);\n"
	        "    double hi = 1.0000000000000002;\n"
	        "    double sample = std::random::next_double_range(narrow, 1.0, hi);\n"
	        "    assert(sample >= 1.0 && sample < hi);\n"
	        "    double wide = std::random::next_double_range(narrow, -1e308, 1e308);\n"
	        "    assert(std::isfinite(wide) && wide >= -1e308 && wide < 1e308);\n"
	        "    array<double> large = {1e308, 1e308};\n"
	        "    assert(std::numeric::average(large) == 1e308);\n"
	        "    array<double> opposed = {1e308, -1e308};\n"
	        "    assert(std::numeric::average(opposed) == 0.0);\n"
	        "    assert(std::filesystem::dirname(\"C:/foo.txt\") == \"C:/\");\n"
	        "    print(\"ok\");\n"
	        "}\n",
	        "ok\n",
	        "standard library numeric and path boundaries"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"numeric.vs\";\n"
	        "void main() {\n"
	        "    array<int> values = {2147483647, 1, -1};\n"
	        "    print(std::numeric::sum(values));\n"
	        "}\n",
	        "integer overflow",
	        "OverflowError",
	        "generic standard-library arithmetic preserves fixed-width overflow"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"delegate.vs\";\n"
	        "int increment(int value) { return value + 1; }\n"
	        "int increment_twice(int value) { return value + 2; }\n"
	        "void report(int value) { print(value); }\n"
	        "void main() {\n"
	        "    delegate<int, int> handler = delegate<int, int>();\n"
	        "    handler.bind(increment);\n"
	        "    assert(handler.is_bound());\n"
	        "    assert(handler.execute(6) == 7);\n"
	        "    handler.unbind();\n"
	        "    assert(!handler.is_bound());\n"
	        "    assert(!handler.execute_if_bound(6));\n"
	        "    handler.bind(increment_twice);\n"
	        "    assert(handler.execute(6) == 8);\n"
	        "    multicast_delegate<int> listeners = multicast_delegate<int>();\n"
	        "    listeners += report;\n"
	        "    listeners.broadcast(3);\n"
	        "    listeners -= report;\n"
	        "    assert(listeners.count() == 0);\n"
	        "    print(\"ok\");\n"
	        "}\n",
	        "3\nok\n",
	        "events delegate bind and unbind"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() { std::console::write(\"x\"); std::console::flush(); "
	        "print(std::console::is_interactive()); print(std::console::clear()); }\n",
	        "xfalse\nfalse\n",
	        "namespace-only console helpers preserve raw output and terminal guards"))
	{
		return EXIT_FAILURE;
	}

	{
		std::string Python, Error;
		if (!Expect(VeroScript::EmitPythonFromSource("void main() {}", "console-race.vs", Python, Error),
		        "emit concurrent console runtime probe"))
		{
			return EXIT_FAILURE;
		}

		Python += R"PY(
import io, threading, time, types
class Terminal(io.StringIO):
    def isatty(self):
        return True
class Kernel:
    mode = active = maximum = 0
    def SetConsoleMode(self, handle, mode):
        self.mode = mode
        if mode & 4:
            self.active += 1
            self.maximum = max(self.maximum, self.active)
            first.set()
            assert attempted.wait(2)
            time.sleep(0.02)
        else:
            self.active -= 1
        return 1
first, attempted = threading.Event(), threading.Event()
kernel = Kernel()
original_output, original_platform = _vs_sys.stdout, _vs_sys.platform
original_windows, original_support = _vs_console_windows, _vs_console_supports_color
results = []
try:
    _vs_sys.stdout, _vs_sys.platform = Terminal(), "win32"
    _vs_console_supports_color = lambda: True
    _vs_console_windows = lambda: (kernel, 1, types.SimpleNamespace(value=kernel.mode))
    def control(second):
        if second:
            attempted.set()
        results.append(_vs_console_sequence("control"))
    one = threading.Thread(target=control, args=(False,))
    two = threading.Thread(target=control, args=(True,))
    one.start()
    assert first.wait(2)
    two.start()
    one.join(2)
    two.join(2)
    assert not one.is_alive() and not two.is_alive()
    assert results == [True, True]
    assert kernel.maximum == 1 and kernel.mode == 0 and kernel.active == 0
finally:
    _vs_sys.stdout, _vs_sys.platform = original_output, original_platform
    _vs_console_windows, _vs_console_supports_color = original_windows, original_support
print("ok")
)PY";
		if (!RunPython(Python, "ok\n", "concurrent controls restore the original Windows console mode"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::string Python, Error;
		if (!Expect(VeroScript::EmitPythonFromSource("void main() {}", "console-cleanup.vs", Python, Error),
		        "emit console failure cleanup probe"))
		{
			return EXIT_FAILURE;
		}

		Python += R"PY(
import ctypes, io, types
events = []
class Output(io.StringIO):
    failed = False
    def flush(self):
        events.append("flush")
        if self.failed:
            raise OSError("text output failed")
class CursorInfo(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint32), ("visible", ctypes.c_int32)]
class Kernel:
    fail_style = False
    def GetConsoleScreenBufferInfo(self, handle, pointer):
        pointer._obj.attributes = 7
        return 1
    def SetConsoleTextAttribute(self, handle, attributes):
        events.append(("style", attributes))
        if self.fail_style:
            raise OSError("style restore failed")
        return 1
    def SetConsoleCursorInfo(self, handle, pointer):
        events.append(("cursor", pointer._obj.visible))
        return 1
output, kernel = Output(), Kernel()
original_output = _vs_sys.stdout
original_platform = _vs_sys.platform
original_mark, original_support = _vs_console_mark, _vs_console_supports_color
original_windows = _vs_console_windows
try:
    _vs_sys.stdout = output
    _vs_sys.platform = "win32"
    _vs_console_supports_color = lambda: True
    _vs_console_mark = lambda style, cursor: None
    _vs_console_windows = lambda: (kernel, 1, types.SimpleNamespace(value=0))
    _vs_console_windows_state = (kernel, 1, types.SimpleNamespace(attributes=7), CursorInfo(25, 0), True, True)
    assert _vs_console_set_color("red", "default")
    assert events == ["flush", ("style", 4)]
    events.clear()
    output.failed, kernel.fail_style = True, True
    _vs_console_dirty_style = _vs_console_dirty_cursor = True
    _vs_console_restore()
    assert events == ["flush", ("style", 7), ("cursor", 0)]
    assert not _vs_console_dirty_style and not _vs_console_dirty_cursor
    assert _vs_console_windows_state is None
finally:
    _vs_sys.stdout = original_output
    _vs_sys.platform = original_platform
    _vs_console_mark, _vs_console_supports_color = original_mark, original_support
    _vs_console_windows = original_windows
print("ok")
)PY";
		if (!RunPython(Python, "ok\n",
		        "pending text flushes before styling and failures do not block cursor restoration"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::string Python, Error;
		if (!Expect(VeroScript::EmitPythonFromSource(
		                "void main() {}", "console-native-windows.vs", Python, Error),
		        "emit native Windows console probe"))
		{
			return EXIT_FAILURE;
		}

		Python += R"PY(
import io, types
events = []
class Terminal(io.StringIO):
    def isatty(self):
        return True
    def flush(self):
        events.append("flush")
class Kernel:
    short_write = fail_buffer = fail_cursor = False
    mode_changes = 0
    attributes = 0x127
    last_style_handle = None
    queries = fail_on_query = 0
    def GetConsoleScreenBufferInfo(self, handle, pointer):
        self.queries += 1
        if self.fail_buffer or self.queries == self.fail_on_query:
            return 0
        value = pointer._obj
        value.size.x, value.size.y = 100, 200
        value.position.x, value.position.y = 8, 11
        value.attributes = self.attributes
        value.window.left, value.window.top = 5, 10
        value.window.right, value.window.bottom = 14, 12
        return 1
    def FillConsoleOutputCharacterW(self, handle, character, count, position, written):
        events.append(("characters", position.x, position.y, count.value, character.value))
        written._obj.value = count.value - int(self.short_write)
        return 1
    def FillConsoleOutputAttribute(self, handle, attributes, count, position, written):
        events.append(("attributes", position.x, position.y, count.value, attributes.value))
        written._obj.value = count.value
        return 1
    def SetConsoleCursorPosition(self, handle, position):
        events.append(("position", position.x, position.y))
        return 1
    def GetConsoleCursorInfo(self, handle, pointer):
        if self.fail_cursor:
            return 0
        pointer._obj.size, pointer._obj.visible = 37, 1
        return 1
    def SetConsoleCursorInfo(self, handle, pointer):
        events.append(("visibility", pointer._obj.size, pointer._obj.visible))
        return 1
    def SetConsoleTextAttribute(self, handle, attributes):
        events.append(("style", attributes))
        self.last_style_handle = handle
        self.attributes = attributes
        return 1
    def SetConsoleMode(self, *args):
        self.mode_changes += 1
        raise AssertionError("native controls must not enable ANSI mode")
kernel = Kernel()
original_output, original_platform, original_windows = _vs_sys.stdout, _vs_sys.platform, _vs_console_windows
original_input = _vs_sys.stdin
try:
    terminal = Terminal()
    _vs_sys.stdout, _vs_sys.platform = terminal, "win32"
    current_handle = [1]
    _vs_console_windows = lambda: (kernel, current_handle[0], types.SimpleNamespace(value=0))
    _vs_sys.stdin = None
    assert not _vs_console_is_interactive()
    _vs_sys.stdin = io.StringIO()
    _vs_sys.stdin.close()
    assert not _vs_console_is_interactive()
    ctypes, Coord, BufferInfo, CursorInfo = _vs_console_windows_types()
    assert (ctypes.sizeof(Coord), ctypes.sizeof(BufferInfo), ctypes.sizeof(CursorInfo)) == (4, 22, 8)
    assert _vs_console_supports_color() and _vs_console_supports_cursor() and _vs_console_supports_clear()
    assert (_vs_console_width(), _vs_console_height()) == (10, 3)
    assert _vs_console_clear()
    assert events == ["flush",
        ("characters", 5, 10, 10, " "), ("attributes", 5, 10, 10, 0x127),
        ("characters", 5, 11, 10, " "), ("attributes", 5, 11, 10, 0x127),
        ("characters", 5, 12, 10, " "), ("attributes", 5, 12, 10, 0x127), ("position", 5, 10)]
    for mode, column, count in (("after", 8, 7), ("before", 5, 4), ("all", 5, 10)):
        events.clear()
        assert _vs_console_clear_line(mode)
        assert events == ["flush", ("characters", column, 11, count, " "),
                          ("attributes", column, 11, count, 0x127)]
    events.clear()
    assert _vs_console_move_cursor(2, 1)
    assert events == ["flush", ("position", 7, 11)]
    events.clear()
    assert not _vs_console_move_cursor(10, 0) and not _vs_console_move_cursor(0, 3)
    assert events == []
    assert _vs_console_set_cursor_visible(False)
    assert events == ["flush", ("visibility", 37, 0)]
    events.clear()
    _vs_console_restore()
    assert events == ["flush", ("visibility", 37, 1)]
    kernel.fail_cursor = True
    assert not _vs_console_set_cursor_visible(False)
    _vs_console_restore()
    assert kernel.mode_changes == 0
    kernel.fail_cursor = False
    kernel.attributes = 7
    assert _vs_console_set_color("red", "default")
    kernel.attributes |= 0x4000
    current_handle[0] = 2
    assert _vs_console_set_color("blue", "default")
    assert kernel.attributes == 0x4001
    assert kernel.last_style_handle == 2
    assert _vs_console_reset_style()
    assert kernel.attributes == 0x4007
    _vs_console_restore()
    assert kernel.attributes == 7
    assert kernel.last_style_handle == 1
    current_handle[0] = 1
    # A failed original-state capture must keep using native APIs. The native
    # Core uses the last known per-thread palette, or palette 7 on a new thread.
    kernel.attributes = 0x4056
    kernel.fail_on_query = kernel.queries + 2
    assert _vs_console_set_color("red", "default")
    assert kernel.attributes == 0x4004
    assert _vs_console_reset_style()
    assert kernel.attributes == 0x4007
    _vs_console_restore()
    assert kernel.attributes == 0x4007 and kernel.mode_changes == 0
    saved_palette = _vs_console_windows_defaults.palette
    _vs_console_windows_defaults.palette = 0x27
    kernel.attributes = 0x4056
    kernel.fail_on_query = kernel.queries + 2
    outcomes = []
    def fresh_thread():
        try:
            result = _vs_console_set_color("default", "default")
            outcomes.append((result, kernel.attributes))
            _vs_console_restore()
        except Exception as error:
            outcomes.append(error)
    worker = _vs_threading.Thread(target=fresh_thread)
    worker.start()
    worker.join(2)
    assert not worker.is_alive() and outcomes == [(True, 0x4007)]
    assert _vs_console_windows_defaults.palette == 0x27 and kernel.mode_changes == 0
    _vs_console_windows_defaults.palette = saved_palette
    kernel.fail_on_query = 0
    kernel.attributes = 0x127
    events.clear()
    kernel.short_write = True
    assert not _vs_console_clear()
    assert events == ["flush", ("characters", 5, 10, 10, " ")]
    kernel.short_write, kernel.fail_buffer = False, True
    assert not _vs_console_supports_clear() and _vs_console_size() == (0, 0)
    assert not _vs_console_clear() and not _vs_console_set_cursor_visible(False)
    _vs_sys.stdout = io.StringIO()
    kernel.fail_buffer = False
    assert not _vs_console_clear() and not _vs_console_move_cursor(0, 0)
    assert _vs_sys.stdout.getvalue() == "" and terminal.getvalue() == ""
finally:
    _vs_sys.stdout, _vs_sys.platform, _vs_console_windows = original_output, original_platform, original_windows
    _vs_sys.stdin = original_input
print("ok")
)PY";
		if (!RunPython(Python, "ok\n",
		        "Windows console controls use native APIs and visible-window coordinates without ANSI mode"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const std::string& Call : {
	         std::string("supports_color()"), std::string("supports_cursor()"),
	         std::string("supports_clear()"), std::string("is_output_terminal()"),
	         std::string("set_color(\"red\", \"default\")"), std::string("reset_style()"),
	         std::string("clear_line(\"all\")"), std::string("move_cursor(0, 0)"),
	         std::string("set_cursor_visible(true)"), std::string("width()"), std::string("height()")})
	{
		if (!EmitAndRun("void main() { print(std::console::" + Call + "); }\n",
		        (Call == "width()" || Call == "height()") ? "0\n" : "false\n",
		        "terminal builtins match the VM for redirected output"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!EmitAndRun(
	        "import \"concurrent.vs\";\n"
	        "export void echo_worker(channel<int> values) {\n"
	        "    int value = values.recv();\n"
	        "    values.send(value + 1);\n"
	        "}\n"
	        "void main() {\n"
	        "    channel<int> values = channel<int>();\n"
	        "    int task_id = std::concurrent::spawn(\"echo_worker\", values);\n"
	        "    print(task_id);\n"
	        "    values.send(40);\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "    print(values.recv());\n"
	        "}\n",
	        "0\n41\n",
	        "raw task ids and channel round trip match the VM"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"concurrent.vs\";\n"
	        "export void cooperative(channel<int> unused) {\n"
	        "    for (int i = 0; i < 5000 && !std::concurrent::cancel_requested(); i++) { std::this_thread::sleep(1); }\n"
	        "    assert(std::concurrent::cancel_requested());\n"
	        "}\n"
	        "void main() {\n"
	        "    print(!std::concurrent::cancel_requested());\n"
	        "    channel<int> ch = channel<int>();\n"
	        "    int id = std::concurrent::spawn(\"cooperative\", ch);\n"
	        "    std::concurrent::cancel_task(id);\n"
	        "    std::concurrent::join_task(id);\n"
	        "}\n",
	        "true\n",
	        "cooperative cancellation flag matches the VM"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun("void main() { print(\"ok\"); }", "ok\n",
	        "startup interruption preserves live tasks and failed creation releases shutdown", {}, false,
	        R"PY(
original_thread = _vs_threading.Thread
worker_entered = _vs_threading.Event()
release_worker = _vs_threading.Event()
created_threads = []

class InterruptedThread(original_thread):
	def start(self):
		created_threads.append(self)
		original_wait = self._started.wait
		def interrupt(timeout=None):
			raise KeyboardInterrupt("interrupted after native thread creation")
		self._started.wait = interrupt
		try:
			super().start()
		finally:
			self._started.wait = original_wait

def worker(channel):
	worker_entered.set()
	assert release_worker.wait(5)

shutdown_done = _vs_threading.Event()
shutdown_errors = []
def shutdown():
	try:
		_vs_shutdown()
	except BaseException as error:
		shutdown_errors.append(error)
	finally:
		shutdown_done.set()

try:
	_vs_threading.Thread = InterruptedThread
	try:
		_vs_spawn(worker, _VsChannel("int"))
	except KeyboardInterrupt:
		pass
	else:
		raise AssertionError("startup interruption was swallowed")
	finally:
		_vs_threading.Thread = original_thread
	assert worker_entered.wait(5)
	assert len(_VS_ALL_TASKS) == 1, "live worker lost after interrupted startup"
	state = _VS_ALL_TASKS[0]
	assert not state.done.is_set()
	waiting = _vs_threading.Event()
	original_wait = state.done.wait
	def observe_wait(timeout=None):
		waiting.set()
		return original_wait(timeout)
	state.done.wait = observe_wait
	cleanup = original_thread(target=shutdown)
	cleanup.start()
	assert waiting.wait(5)
	assert not shutdown_done.is_set()
	release_worker.set()
	cleanup.join(5)
	assert shutdown_done.is_set() and not shutdown_errors, shutdown_errors
	assert state.done.is_set() and state.joined and state.error is None
finally:
	_vs_threading.Thread = original_thread
	release_worker.set()
	for thread in created_threads:
		thread._started.wait(5)
		thread.join(5)

failure_entered = _vs_threading.Event()
release_failure = _vs_threading.Event()
spawn_errors = []
shutdown_done.clear()
shutdown_errors.clear()

class FailingThread(original_thread):
	def start(self):
		failure_entered.set()
		assert release_failure.wait(5)
		raise RuntimeError("native thread creation failed")

def failed_spawn():
	try:
		_vs_spawn(worker, _VsChannel("int"))
	except RuntimeError as error:
		spawn_errors.append(str(error))

try:
	_vs_threading.Thread = FailingThread
	creator = original_thread(target=failed_spawn)
	creator.start()
	assert failure_entered.wait(5)
	state = _VS_ALL_TASKS[-1]
	waiting.clear()
	original_wait = state.done.wait
	state.done.wait = observe_wait
	cleanup = original_thread(target=shutdown)
	cleanup.start()
	assert waiting.wait(5)
	release_failure.set()
	creator.join(5)
	cleanup.join(5)
	assert spawn_errors == ["native thread creation failed"], spawn_errors
	assert shutdown_done.is_set() and not shutdown_errors, shutdown_errors
	assert state.done.is_set() and state.joined and state.thread is None
	assert state not in _VS_ALL_TASKS
finally:
	_vs_threading.Thread = original_thread
	release_failure.set()
)PY"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(R"VS(
import "concurrent.vs";
void leaf(channel<int> unused) { std::this_thread::sleep(20); }
void middle(channel<int> unused) {
    std::this_thread::sleep(20);
    channel<int> child = channel<int>();
    std::concurrent::spawn("leaf", child);
}
void worker(channel<int> unused) {
    std::this_thread::sleep(20);
    channel<int> child = channel<int>();
    std::concurrent::spawn("middle", child);
}
void main() {
    channel<int> child = channel<int>();
    std::concurrent::spawn("worker", child);
    print("main");
}
)VS", "main\n", "shutdown waits for descendant task generations", {}, false,
	        "\nassert len(_VS_ALL_TASKS) == 3\n"
	        "assert all(task.done.is_set() and task.joined and task.error is None for task in _VS_ALL_TASKS)\n"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(R"VS(
import "concurrent.vs";
void leaf(channel<int> child) { child.recv_for(10000); }
void worker(channel<int> unused) {
    unused.recv_for(10000);
    channel<int> child = channel<int>();
    assert(child.is_closed());
    int task = std::concurrent::spawn("leaf", child);
    std::concurrent::join_task(task);
}
void main() {
    channel<int> child = channel<int>();
    std::concurrent::spawn("worker", child);
    print("main");
}
)VS", "main\n", "shutdown closes channels created by descendant tasks", {}, false,
	        "\nassert len(_VS_ALL_TASKS) == 2\n"
	        "assert all(task.done.is_set() and task.joined and task.error is None for task in _VS_ALL_TASKS)\n"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"concurrent.vs\";\n"
	        "export void answer(channel<int> output) { output.send(42); }\n"
	        "string worker_name() { return \"answer\"; }\n"
	        "void main() {\n"
	        "    string name = worker_name();\n"
	        "    task<int> work = std::concurrent::spawn_task<int>(name);\n"
	        "    print(work.await());\n"
	        "}\n",
	        "42\n",
	        "string variable task entry remains supported in Python AOT (Core VM; standalone AOT requires runtime)", {}, true))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "import \"concurrent.vs\";\n"
	        "int twice(int value) { return value * 2; }\n"
	        "void main() {\n"
	        "    task<int> work = std::concurrent::spawn_task<int>(twice, 21);\n"
	        "    print(work.await());\n"
	        "}\n",
	        "standard-library foundation operations require",
	        "callable typed task requires Core runtime in Python AOT"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "import \"concurrent.vs\";\n"
	        "int twice(int value) { return value * 2; }\n"
	        "void main() {\n"
	        "    fn(int)->int stored = twice;\n"
	        "    task<int> work = std::concurrent::spawn_task<int>(stored, 21);\n"
	        "    print(work.await());\n"
	        "}\n",
	        "standard-library foundation operations require",
	        "stored callable typed task requires Core runtime in Python AOT"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "import \"concurrent.vs\";\n"
	        "class Ops { public: static int plus(int value) { return value + 1; } };\n"
	        "void main() {\n"
	        "    task<int> work = std::concurrent::spawn_task<int>(Ops::plus, 41);\n"
	        "    print(work.await());\n"
	        "}\n",
	        "standard-library foundation operations require",
	        "static method typed task requires Core runtime in Python AOT"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "import \"concurrent.vs\";\n"
	        "void notify(channel<int> output, int value) { output.send(value); }\n"
	        "void main() {\n"
	        "    channel<int> output = channel<int>();\n"
	        "    std::concurrent::spawn(notify, output, 9);\n"
	        "}\n",
	        "callable task entry is not supported",
	        "callable spawn entry fails closed in Python AOT"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "class Counter { public: int value = 7; };\n"
	        "void main() {\n"
	        "    Counter item = Counter();\n"
	        "    field_info<Counter> field = field_info<Counter>(\n"
	        "        type<Counter>().qualified_name, 0);\n"
	        "    print(field.get<int>(item));\n"
	        "}\n",
	        "field_info<Counter>",
	        "executable reflection fails closed in Python AOT"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"concurrent.vs\";\n"
	        "export void variadic_worker(channel<int>... outputs) {\n"
	        "    print(len(outputs));\n"
	        "}\n"
	        "void main() {\n"
	        "    channel<int> output = channel<int>();\n"
	        "    int task_id = std::concurrent::spawn(\"variadic_worker\", output);\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "}\n",
	        "spawn target must take exactly one channel parameter",
	        "spawn target not found",
	        "variadic channel functions are not valid workers"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"concurrent.vs\";\n"
	        "export void string_worker(channel<string> values) {}\n"
	        "void main() {\n"
	        "    channel<int> values = channel<int>();\n"
	        "    int task_id = std::concurrent::spawn(\"string_worker\", values);\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "}\n",
	        "spawn channel type does not match",
	        "spawn channel type mismatch",
	        "spawn validates the worker channel element type"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"concurrent.vs\";\n"
	        "using Values = channel<int>;\n"
	        "export void alias_worker(Values values) { values.send(9); }\n"
	        "void main() {\n"
	        "    Values values = Values();\n"
	        "    int task_id = std::concurrent::spawn(\"alias_worker\", values);\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "    print(values.recv());\n"
	        "}\n",
	        "9\n",
	        "spawn channel validation canonicalizes type aliases"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"concurrent.vs\";\n"
	        "export void exiting_worker(channel<int> output) { exit(7); }\n"
	        "void main() {\n"
	        "    channel<int> output = channel<int>();\n"
	        "    int task_id = std::concurrent::spawn(\"exiting_worker\", output);\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "}\n",
	        "spawned task called exit(7)",
	        "spawned task called exit(7)",
	        "worker exit requests fail the task without exiting the process"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"concurrent.vs\";\n"
	        "export void self_join_worker(channel<int> commands) {\n"
	        "    int task_id = commands.recv();\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "}\n"
	        "void main() {\n"
	        "    channel<int> commands = channel<int>();\n"
	        "    int task_id = std::concurrent::spawn(\"self_join_worker\", commands);\n"
	        "    commands.send(task_id);\n"
	        "    std::concurrent::join_task(task_id);\n"
	        "}\n",
	        "invalid task id",
	        "invalid task id",
	        "worker task registries are isolated from parent task ids"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"concurrent.vs\";\n"
	        "export void nested_worker(channel<int> output) { output.send(7); }\n"
	        "export void parent_worker(channel<int> output) {\n"
	        "    channel<int> nested_output = channel<int>();\n"
	        "    int nested_id = std::concurrent::spawn(\"nested_worker\", nested_output);\n"
	        "    std::concurrent::join_task(nested_id);\n"
	        "    output.send(nested_id);\n"
	        "    output.send(nested_output.recv());\n"
	        "}\n"
	        "void main() {\n"
	        "    channel<int> output = channel<int>();\n"
	        "    int parent_id = std::concurrent::spawn(\"parent_worker\", output);\n"
	        "    print(parent_id);\n"
	        "    std::concurrent::join_task(parent_id);\n"
	        "    print(output.recv());\n"
	        "    print(output.recv());\n"
	        "}\n",
	        "0\n0\n7\n",
	        "nested workers use an independent task id registry"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "import \"concurrent.vs\";\n"
	        "export void fail_worker(channel<int> output) { throw \"worker failed\"; }\n"
	        "void main() {\n"
	        "    channel<int> output=std::concurrent::make_channel<int>();\n"
	        "    int id=std::concurrent::spawn(\"fail_worker\",output); std::concurrent::join_task(id);\n"
	        "}\n",
	        "spawned task failed",
	        "worker failure propagates through join"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"concurrent.vs\";\n"
	        "int worker_global = 7;\n"
	        "class WorkerState { public: static int value = 5; };\n"
	        "export void answer(channel<int> output) { output.send(42); }\n"
	        "export void delayed(channel<int> output) { std::this_thread::sleep(20); output.send(9); }\n"
	        "export void first(channel<int> output) { output.send(1); }\n"
	        "export void second(channel<int> output) { output.send(2); }\n"
	        "export void wait_for_cancel(channel<int> output) { int ignored = output.recv(); }\n"
	        "export void mutate_isolated(channel<int> output) {\n"
	        "    worker_global = 99;\n"
	        "    WorkerState.value = 20;\n"
	        "    output.send(worker_global + WorkerState.value);\n"
	        "}\n"
	        "void main() {\n"
	        "    task<int> immediate = std::concurrent::spawn_task<int>(\"answer\");\n"
	        "    print(immediate.await());\n"
	        "    task<int> later = std::concurrent::spawn_task<int>(\"delayed\");\n"
	        "    print(later.await_for(0).has_value());\n"
	        "    print(later.await());\n"
	        "    task_group<int> group = task_group<int>();\n"
	        "    group.add(std::concurrent::spawn_task<int>(\"first\"));\n"
	        "    group.add(std::concurrent::spawn_task<int>(\"second\"));\n"
	        "    array<int> results = group.await_all();\n"
	        "    print(results[0] + results[1]);\n"
	        "    task<int> isolated = std::concurrent::spawn_task<int>(\"mutate_isolated\");\n"
	        "    print(isolated.await());\n"
	        "    print(worker_global);\n"
	        "    print(WorkerState.value);\n"
	        "    channel<int> bounded = std::concurrent::make_channel<int>(1);\n"
	        "    print(bounded.capacity());\n"
	        "    print(bounded.try_send(8));\n"
	        "    print(bounded.try_send(9));\n"
	        "    print(bounded.recv());\n"
	        "    bounded.close();\n"
	        "    print(bounded.is_closed());\n"
	        "    task<int> cancelled = std::concurrent::spawn_task<int>(\"wait_for_cancel\");\n"
	        "    cancelled.cancel();\n"
	        "    print(cancelled.valid());\n"
	        "}\n",
	        "42\nfalse\n9\n3\n119\n7\n5\n1\ntrue\nfalse\n8\ntrue\nfalse\n",
	        "concurrent tasks, channels, and isolated worker state (Core VM; standalone AOT requires runtime)", {}, true))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"concurrent.vs\";\n"
	        "export void increment_synchronized(channel<channel<int>> setup) {\n"
	        "    channel<int> value = setup.recv();\n"
	        "    std::concurrent::synchronized_fetch_add(value, 1);\n"
	        "}\n"
	        "void main() {\n"
	        "    channel<channel<int>> setup = channel<channel<int>>();\n"
	        "    channel<int> value = std::concurrent::make_synchronized<int>(4);\n"
	        "    int work = std::concurrent::spawn(\"increment_synchronized\", setup);\n"
	        "    setup.send(value);\n"
	        "    std::concurrent::join_task(work);\n"
	        "    print(std::concurrent::synchronized_load(value));\n"
	        "    print(std::concurrent::synchronized_exchange(value, 9));\n"
	        "    std::concurrent::synchronized_store(value, 11);\n"
	        "    print(std::concurrent::synchronized_load(value));\n"
	        "    std::concurrent::mutex gate = std::concurrent::make_mutex();\n"
	        "    print(std::concurrent::mutex_try_lock(gate));\n"
	        "    print(std::concurrent::mutex_try_lock(gate));\n"
	        "    std::concurrent::mutex_unlock(gate);\n"
	        "    std::concurrent::counting_semaphore permits =\n"
	        "        std::concurrent::make_semaphore(1, 2);\n"
	        "    print(std::concurrent::semaphore_try_acquire(permits));\n"
	        "    print(std::concurrent::semaphore_try_acquire(permits));\n"
	        "    std::concurrent::semaphore_release(permits);\n"
	        "    print(std::concurrent::semaphore_acquire_for(permits, 0));\n"
	        "}\n",
	        "5\n5\n11\ntrue\nfalse\ntrue\nfalse\ntrue\n",
	        "nested channels, synchronized values, mutexes, and semaphores (Core VM; standalone AOT requires runtime)", {}, true))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "int add(int a, int b) { return a + b; }\n"
	        "void main() {\n"
	        "    int total = 0;\n"
	        "    for (int i = 0; i < 5; i += 1) {\n"
	        "        if (i == 2) { continue; }\n"
	        "        total += i;\n"
	        "    }\n"
	        "    array<int> values = {add(total, 1), 9};\n"
	        "    map<string,int> scores = {\"answer\": values[0]};\n"
	        "    set<int> selected = {2, 4};\n"
	        "    print(scores[\"answer\"]);\n"
	        "    print(selected.has(4));\n"
	        "    print(\"n={}\", len(values));\n"
	        "}\n",
	        "9\ntrue\nn=2\n",
	        "functions, control flow, and containers"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Value { int number = 3; }\n"
	        "class Box { public: int number = 4; }\n"
	        "struct Holder { Box box = Box(); Value nested; }\n"
	        "void change_value(Value value) { value.number = 9; }\n"
	        "void change_box(Box value) { value.number = 10; }\n"
	        "void main() {\n"
	        "    Value value();\n"
	        "    Box box();\n"
	        "    change_value(value);\n"
	        "    change_box(box);\n"
	        "    print(value.number);\n"
	        "    print(box.number);\n"
	        "    Holder original();\n"
	        "    Holder copy = original;\n"
	        "    copy.box.number = 12;\n"
	        "    copy.nested.number = 13;\n"
	        "    print(original.box.number);\n"
	        "    print(original.nested.number);\n"
	        "}\n",
	        "3\n10\n12\n3\n",
	        "value and reference parameter copying"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "void main() { int value = 2147483647; value += 1; }\n",
	        "OverflowError",
	        "checked int32 overflow"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "class Box { public: int value = 2147483647; }\n"
	        "void main() { Box box(); box.value += 1; }\n",
	        "OverflowError",
	        "checked external field overflow"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "class Box { public: static int value = 2147483647; }\n"
	        "void main() { Box.value += 1; }\n",
	        "OverflowError",
	        "checked static field overflow"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    int64 maximum = 9223372036854775807i64;\n"
	        "    int64 negative = -9223372036854775807i64;\n"
	        "    print(maximum / 3); print(maximum % 3);\n"
	        "    print(negative / 3); print(negative % 3);\n"
	        "}\n",
	        "3074457345618258602\n1\n-3074457345618258602\n-1\n",
	        "lossless int64 division and modulo"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "double rem(double left, double right) { return left % right; }\n"
	        "void main() { print(rem(5.5, 2.0)); }\n",
	        "modulo is only defined for integers",
	        "TypeError: modulo is only defined for integers",
	        "double modulo matches the Core runtime rejection"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRequireBothFailure(
	        "float rem(float left, float right) { return left % right; }\n"
	        "void main() { print(rem(5.5f, 2.0f)); }\n",
	        "modulo is only defined for integers",
	        "TypeError: modulo is only defined for integers",
	        "float modulo matches the Core runtime rejection"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"math.vs\";\n"
	        "double rounded(double value) { return std::round(value); }\n"
	        "void main() {\n"
	        "    assert(rounded(0.0) == 0.0);\n"
	        "    assert(rounded(-0.0) == 0.0);\n"
	        "    assert(rounded(0.5) == 1.0);\n"
	        "    assert(rounded(-0.5) == -1.0);\n"
	        "    assert(rounded(0.49999999999999994) == 0.0);\n"
	        "    assert(rounded(-0.49999999999999994) == 0.0);\n"
	        "    assert(rounded(0.5000000000000001) == 1.0);\n"
	        "    assert(rounded(-0.5000000000000001) == -1.0);\n"
	        "    assert(rounded(4503599627370497.0) == 4503599627370497.0);\n"
	        "    assert(rounded(-4503599627370497.0) == -4503599627370497.0);\n"
	        "    assert(rounded(1e308) == 1e308);\n"
	        "    print(rounded(-0.25)); print(rounded(-0.0)); print(rounded(0.0));\n"
	        "}\n",
	        "-0\n-0\n0\n",
	        "round preserves signed zero and integral values and rounds half boundaries correctly"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"math.vs\";\n"
	        "void main() {\n"
	        "    print(std::floor(-0.0)); print(std::ceil(-0.0)); print(std::trunc(-0.0));\n"
	        "    print(std::ceil(-0.25)); print(std::trunc(-0.25));\n"
	        "    print(std::floor(0.0)); print(std::ceil(0.25)); print(std::trunc(0.25));\n"
	        "}\n",
	        "-0\n-0\n-0\n-0\n-0\n0\n1\n0\n",
	        "floor ceil and trunc preserve the sign of zero"))
	{
		return EXIT_FAILURE;
	}

	#if defined(_WIN32)
	const std::string InitialBoolCapacity = "32\n";
	#else
	const std::string InitialBoolCapacity = "64\n";
	#endif
	if (!EmitAndRun(
	        "struct Value { int number = 1; }\n"
	        "void main() {\n"
	        "    array<int> scalar = {1, 2, 3};\n"
	        "    print(scalar.capacity());\n"
	        "    scalar.reserve(12); scalar.push(4); print(scalar.capacity());\n"
	        "    scalar.clear(); print(scalar.capacity());\n"
	        "    array<bool> flags = {true}; print(flags.capacity());\n"
	        "    flags.reserve(33); print(flags.capacity());\n"
	        "    array<Value> objects = {Value()};\n"
	        "    objects.reserve(9); objects.clear(); print(objects.capacity());\n"
	        "    map<string,int> scores = map<string,int>(); scores.reserve(4);\n"
	        "    set<int> selected = set<int>(); selected.reserve(4);\n"
	        "    print(len(scores)); print(len(selected));\n"
	        "}\n",
	        std::string("3\n12\n12\n") + InitialBoolCapacity + "64\n0\n0\n0\n",
	        "container reserve, capacity, and clear semantics"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() { array<int> values = {1}; print(values[-1]); }\n",
	        "array index out of bounds",
	        "array index out of bounds",
	        "negative array indices fail in both runtimes"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() { array<int> values = {1}; values[-1] = 2; }\n",
	        "array index out of bounds",
	        "array index out of bounds",
	        "negative array assignment indices fail in both runtimes"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() { array<int> values = {1}; values.remove_at(-1); }\n",
	        "remove_at index out of range",
	        "remove_at index out of bounds",
	        "negative remove_at indices fail in both runtimes"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() { deque<int> values = {1}; print(values[-1]); }\n",
	        "deque index out of bounds",
	        "deque index out of bounds",
	        "negative deque indices fail in both runtimes"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() { set<int> values = set<int>(); values.reserve(-1); }\n",
	        "reserve capacity cannot be negative",
	        "reserve capacity cannot be negative",
	        "negative reserve capacity fails in both runtimes"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "int calls = 0;\n"
	        "int next_index() { calls += 1; return 0; }\n"
	        "void main() {\n"
	        "    array<int> values = {3};\n"
	        "    values[next_index()] += 2;\n"
	        "    print(calls); print(values[0]);\n"
	        "}\n",
	        "1\n5\n",
	        "compound assignment evaluates lvalue once"))
	{
		return EXIT_FAILURE;
	}

	{
		const std::pair<const char*, const char*> Cases[] = {
		    {R"VS(array<int> values={3};int replace(){values=array<int>{9,9};return 5;}void main(){values[0]=replace();print(len(values));print(values[0]);})VS", "1\n5\n"},
		    {R"VS(void main(){array<int> values={3};values[0]=(values=array<int>{9,9})[0];print(len(values));print(values[0]);})VS", "1\n9\n"},
		    {R"VS(class Holder{public:static array<int> values={3};static int replace(){values=array<int>{9,9};return 5;}static void run(){values[0]=replace();}}void main(){Holder::run();print(len(Holder::values));print(Holder::values[0]);})VS", "1\n5\n"},
		    {R"VS(class Holder{public:array<int> values={3};int replace(){values=array<int>{9,9};return 5;}void run(){values[0]=replace();}}void main(){Holder h=Holder();h.run();print(len(h.values));print(h.values[0]);})VS", "1\n5\n"},
		    {R"VS(class Holder{public:array<int> values={3};int replace(){values=array<int>{9,9};return 5;}}void main(){Holder h=Holder();h.values[0]=h.replace();print(len(h.values));print(h.values[0]);})VS", "1\n5\n"},
		    {R"VS(array<array<int>> values={array<int>{3}};int replace(){values[0]=array<int>{9,9};return 5;}void main(){values[0][0]=replace();print(len(values[0]));print(values[0][0]);})VS", "2\n5\n"},
		    {R"VS(int replace(array<array<int>> values){values[0]=array<int>{9,9};return 5;}void main(){array<array<int>> values={array<int>{3}};values[0][0]=replace(values);print(len(values[0]));print(values[0][0]);})VS", "2\n5\n"},
		    {R"VS(array<array<int>> values={array<int>{3}};int replace(){values=array<array<int>>{array<int>{9,9},array<int>{9,9}};return 5;}void main(){values[0][0]=replace();print(len(values));print(len(values[0]));print(values[0][0]);})VS", "2\n2\n5\n"},
		    {R"VS(array<int> values={3};int replace(){values=array<int>{9,9};return 5;}void main(){values[0]+=replace();print(len(values));print(values[0]);})VS", "1\n8\n"},
		    {R"VS(void main(){array<int> values={3};values[0]+=(values=array<int>{9,9})[0];print(len(values));print(values[0]);})VS", "1\n12\n"},
		    {R"VS(class Holder{public:static array<int> values={3};static int replace(){values=array<int>{9,9};return 5;}static void run(){values[0]+=replace();}}void main(){Holder::run();print(len(Holder::values));print(Holder::values[0]);})VS", "1\n8\n"},
		    {R"VS(class Holder{public:array<int> values={3};int replace(){values=array<int>{9,9};return 5;}void run(){values[0]+=replace();}}void main(){Holder h=Holder();h.run();print(len(h.values));print(h.values[0]);})VS", "1\n8\n"},
		    {R"VS(class Holder{public:array<int> values={3};int replace(){values=array<int>{9,9};return 5;}}void main(){Holder h=Holder();h.values[0]+=h.replace();print(len(h.values));print(h.values[0]);})VS", "1\n8\n"},
		    {R"VS(array<array<int>> values={array<int>{3}};int replace(){values[0]=array<int>{9,9};return 5;}void main(){values[0][0]+=replace();print(len(values[0]));print(values[0][0]);})VS", "2\n8\n"},
		    {R"VS(int replace(array<array<int>> values){values[0]=array<int>{9,9};return 5;}void main(){array<array<int>> values={array<int>{3}};values[0][0]+=replace(values);print(len(values[0]));print(values[0][0]);})VS", "2\n8\n"},
		    {R"VS(array<array<int>> values={array<int>{3}};int replace(){values=array<array<int>>{array<int>{9,9},array<int>{9,9}};return 5;}void main(){values[0][0]+=replace();print(len(values));print(len(values[0]));print(values[0][0]);})VS", "2\n2\n8\n"},
		    {R"VS(class Holder{public:array<int> values={3};};Holder h=Holder();int replace(){h=Holder();h.values=array<int>{9,9};return 5;}void main(){h.values[0]=replace();print(len(h.values));print(h.values[0]);})VS", "2\n9\n"}};
		for (const auto& [Source, Expected] : Cases)
		{
			if (!EmitAndRunWithBundle(Source, Expected, "indexed writes preserve Core binding and path writeback"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	if (!EmitAndRunWithBundle(
	        "int index=read();int later=7;int read(){return later;}"
	        "void main(){array<int> values={3};values[index]=5;print(values[index]);}",
	        "5\n", "reserved integer-zero slots remain valid array indices"))
	{
		return EXIT_FAILURE;
	}

	for (const char* Operation : {"=", "+="})
	{
		const std::string Expected = Operation[0] == '=' ? "12\n5\n" : "12\n8\n";
		const std::string Source = "int order=0;int index(){order=order*10+1;return 0;}"
		    "int value(){order=order*10+2;return 5;}void main(){array<int> values={3};"
		    "values[index()]" + std::string(Operation) + "value();print(order);print(values[0]);}";
		if (!EmitAndRunWithBundle(Source, Expected, "indexed assignments evaluate the target before the value exactly once"))
		{
			return EXIT_FAILURE;
		}
		for (const char* Storage : {"", "static "})
		{
			const std::string MemberSource = "class Box{public:" + std::string(Storage) + "int value=3;};"
			    "Box box=Box();int order=0;Box receiver(){order=order*10+1;return box;}"
			    "int value(){order=order*10+2;return 5;}void main(){receiver().value" + std::string(Operation) +
			    "value();print(order);print(" + (Storage[0] ? std::string("Box::value") : "box.value") + ");}";
			if (!EmitAndRunWithBundle(MemberSource, Expected, "field assignments preserve receiver and value evaluation order"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	if (!EmitAndRun(
	        "class Base { public: int value = 1; };\n"
	        "class Counter : public Base { public:\n"
	        "    int Assign() {\n"
	        "        int first = (value = 5);\n"
	        "        int second = (value += 3);\n"
	        "        int third = (value = (value = 9) + 1);\n"
	        "        int value = 20;\n"
	        "        int local = (value = 21);\n"
	        "        return first + second + third + local + this.value;\n"
	        "    }\n"
	        "};\n"
	        "void main() { Counter item = Counter(); print(item.Assign()); print(item.value); }\n",
	        "54\n10\n",
	        "implicit inherited field assignment expressions preserve values and local shadowing"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"io.vs\";\n"
	        "void main() {\n"
	        "    print(std::io::read_token());\n"
	        "    print(std::io::read_int());\n"
	        "    print(std::io::read_bool());\n"
	        "    print(std::io::read_line());\n"
	        "    print(std::io::read_line());\n"
	        "}\n",
	        "alpha\n42\ntrue\n\nline\n",
	        "token and line input boundaries",
	        "alpha 42 true\nline\n"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"io.vs\";\n"
	        "void main() { print(std::io::read_int()); print(std::io::read_double()); }\n",
	        "17\n-125\n",
	        "strict signed integer and finite double input",
	        "+17 -1.25e2\n"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"io.vs\";\n"
	        "void main() { print(std::io::read_int()); }\n",
	        "diag.stdlib_range",
	        "out of range",
	        "read_int rejects values outside int32 range",
	        "2147483648\n"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"io.vs\";\n"
	        "void main() { print(std::io::read_int()); }\n",
	        "diag.stdlib_text",
	        "integer conversion",
	        "read_int rejects Python-only numeric syntax",
	        "1_0\n"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "import \"io.vs\";\n"
	        "void main() { print(std::io::read_double()); }\n",
	        "diag.stdlib_text",
	        "finite numeric value",
	        "read_double rejects non-finite values",
	        "nan\n"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRequireBothFailure(
	        "import \"io.vs\"; void main(){print(std::io::read_double());}",
	        "diag.stdlib_range", "finite numeric value", "read_double rejects underflow", "1e-9999\n"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"string.vs\";\n"
	        "void main() {\n"
	        "    string text = \"A环境\";\n"
	        "    print(len(text));\n"
	        "    print(std::strings::char_code(text, 1));\n"
	        "    print(std::strings::substring(text, 1, 6));\n"
	        "    print(std::strings::find(text, \"境\"));\n"
	        "    print(std::strings::to_upper(\"Abé\"));\n"
	        "    print(std::strings::join(std::strings::split(\"a,b\", \",\"), \"-\"));\n"
	        "    print(format(\"hex={:X} bool={}\", 255, true));\n"
	        "    array<int> values = {1, 2};\n"
	        "    print(values);\n"
	        "}\n",
	        "7\n231\n环境\n4\nABé\na-b\nhex=FF bool=true\narray<int>[2]\n",
	        "UTF-8 byte semantics, string helpers, and formatting"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"string.vs\";\n"
	        "void main() {\n"
	        "    string text = \"é\";\n"
	        "    string lead = std::strings::substring(text, 0, 1);\n"
	        "    string tail = std::strings::substring(text, 1, 1);\n"
	        "    array<string> pieces = {lead, tail};\n"
	        "    assert(lead + tail == text);\n"
	        "    assert(std::strings::join(pieces, \"\") == text);\n"
	        "    array<string> parts = std::strings::split(text, lead);\n"
	        "    assert(len(parts) == 2 && parts[0] == \"\" && parts[1] == tail);\n"
	        "    assert(tail != text && text == text);\n"
	        "    map<string,int> text_keys = {text: 7};\n"
	        "    assert(text_keys.has(format(\"{}{}\", lead, tail)));\n"
	        "    assert(text_keys.has(formatf(\"%c%c\", 195, 169)));\n"
	        "    string raw = \"\\xFF\\x00\\\"\\\\\";\n"
	        "    assert(len(raw) == 4 && std::strings::char_code(raw, 0) == 255);\n"
	        "    assert(std::strings::char_code(raw, 1) == 0);\n"
	        "    assert(std::strings::char_code(raw, 2) == 34 && std::strings::char_code(raw, 3) == 92);\n"
	        "    wstring wide = L\"A\";\n"
	        "    assert(wide + L\"环境\" == L\"A环境\");\n"
	        "    assert(len(wide + L\"环境\") == 3);\n"
	        "    wstring supplementary = L\"\U00010000\";\n"
	        "    wstring bmp = L\"\uE000\";\n"
	        "    assert(len(supplementary) == 2 && len(bmp) == 1);\n"
	        "    assert(supplementary < bmp && bmp > supplementary);\n"
	        "    print(lead + tail);\n"
	        "}\n",
	        "é\n",
	        "partial UTF-8 operations and UTF-16 comparisons preserve Core text semantics"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    print(format(\"neg={:x} fixed={:.2} text={:5}\", -1, 3.14159, \"x\"));\n"
	        "    print(formatf(\"d=%+06d x=%#08X o=%#o\", -42, 255U, 8U));\n"
	        "    print(formatf(\"f=%.2f s=%-5.3s c=%c %%\", 3.14159, \"abcdef\", 65));\n"
	        "    printf(\"wide=%lld\", 9223372036854775807i64);\n"
	        "    array<string> args = {\"7\", \"255\"};\n"
	        "    print(formatf(\"array=%d/%x\", args));\n"
	        "}\n",
	        "neg=ffffffffffffffff fixed=3.14 text=    x\n"
	        "d=-00042 x=0X0000FF o=010\n"
	        "f=3.14 s=abc   c=A %\n"
	        "wide=9223372036854775807\n"
	        "array=7/ff\n",
	        "format and printf-style formatting match the VM parser"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    int total = 0;\n"
	        "    for (int index = 0; index < 20000; index++) total += index;\n"
	        "    print(total);\n"
	        "}\n",
	        "199990000\n",
	        "cached integer metadata preserves hot-loop arithmetic"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() { print(formatf(\"%n\", 1)); }\n",
	        "unsupported conversion",
	        "unsupported conversion",
	        "unsafe printf conversion fails in both runtimes"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() { print(formatf(\"%*d\", 4, 1)); }\n",
	        "dynamic width",
	        "dynamic width",
	        "dynamic printf width fails in both runtimes"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"io.vs\";\n"
	        "import \"string.vs\";\n"
	        "void main() {\n"
	        "    string path = \".veroscript-python-aot-byte-io.tmp\";\n"
	        "    std::filesystem::delete_file(path);\n"
	        "    string raw = formatf(\"%c\", 255);\n"
	        "    std::io::write_file(path, raw); print(std::filesystem::exists(path));\n"
	        "    print(std::filesystem::exists(path + \"/child\"));\n"
	        "    std::io::append_file(path, \"\\n\");\n"
	        "    string loaded = std::io::read_file(path);\n"
	        "    print(len(loaded)); print(std::strings::char_code(loaded, 0));\n"
	        "    print(std::strings::char_code(loaded, 1)); print(std::filesystem::delete_file(path));\n"
	        "}\n",
	        "true\nfalse\n2\n255\n10\ntrue\n",
	        "binary file helpers preserve raw bytes and newlines"))
	{
		return EXIT_FAILURE;
	}

	const std::filesystem::path FsEmptyDir =
	    std::filesystem::temp_directory_path() / ("VeroScriptPythonAotFs-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
	if (!Expect(!std::filesystem::exists(FsEmptyDir),
	        "filesystem AOT fixture must start absent"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "import \"io.vs\"; import \"path.vs\"; import \"string.vs\";\n"
	        "void main() {\n"
	        "    string path = \"" +
	            FsEmptyDir.generic_string() + "\";\n"
	                                          "    std::filesystem::mkdir(path);\n"
	                                          "    assert(std::filesystem::is_directory(path));\n"
	                                          "    assert(!std::filesystem::is_file(path));\n"
	                                          "    assert(std::filesystem::remove_dir(path));\n"
	                                          "    assert(!std::filesystem::is_directory(path));\n"
	                                          "    assert(std::filesystem::join_path(\"a\", \"/b\") == \"/b\");\n"
	                                          "    assert(std::filesystem::join_path(\"C:\", \"b\") == \"C:b\");\n"
	                                          "    assert(std::strings::trim(\"\\v\\f x \\v\\f\") == \"x\");\n"
	                                          "}\n",
	        "",
	        "filesystem type and empty directory AOT parity"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    array<int> values = {1, 2, 3};\n"
	        "    values.remove_at(0); print(values[0]);\n"
	        "    print(values.remove(9));\n"
	        "    map<string,int> scores = {\"x\": 1};\n"
	        "    print(scores.remove(\"x\")); print(scores.has(\"x\"));\n"
	        "    set<int> selected = {4, 5};\n"
	        "    print(selected.remove(4)); print(selected.has(4));\n"
	        "    deque<int> deque_values = {6}; deque_values.push_front(5);\n"
	        "    print(deque_values.pop_back()); print(deque_values.front());\n"
	        "    queue<int> queued = queue<int>(); queued.push(7); print(queued.pop());\n"
	        "    stack<int> stacked = stack<int>(); stacked.push(8); print(stacked.top());\n"
	        "}\n",
	        "2\nfalse\ntrue\nfalse\ntrue\nfalse\n6\n5\n7\n8\n",
	        "container mutation and access methods"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "enum class Color { Red = 1, Green = 2 }\n"
	        "struct Point {\n"
	        "    int value = 1;\n"
	        "    Point(int input) { value = input; }\n"
	        "    int doubled() const { return value * 2; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Point point(7);\n"
	        "    print(point.doubled());\n"
	        "    Color color = Color.Green;\n"
	        "    switch (color) {\n"
	        "        case Color.Red: print(1); break;\n"
	        "        case Color.Green: print(2); break;\n"
	        "        default: print(0);\n"
	        "    }\n"
	        "}\n",
	        "14\n2\n",
	        "struct, method, enum, and switch"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "class Base {\n"
	        "public:\n"
	        "    int value = 3;\n"
	        "    int read() { return value; }\n"
	        "}\n"
	        "class Derived : public Base {\n"
	        "public:\n"
	        "    int bump() { value += 1; return read(); }\n"
	        "}\n"
	        "void main() { Derived value(); print(value.bump()); }\n",
	        "4\n",
	        "inherited field and method resolution"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "class Derived : public Base { public: int read() { return value; } }\n"
	        "class Base { public: int value = 6; }\n"
	        "void main() { Derived value(); print(value.read()); }\n",
	        "6\n",
	        "forward-declared inheritance ordering"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "class NamedMethods {\n"
	        "public:\n"
	        "    int size() { return 1; }\n"
	        "    int front() { return 2; }\n"
	        "    int back() { return 3; }\n"
	        "    int top() { return 4; }\n"
	        "    int keys() { return 5; }\n"
	        "    int values() { return 6; }\n"
	        "    int clone() { return 7; }\n"
	        "    int push(int value) { return value + 1; }\n"
	        "    bool has(int value) { return value == 9; }\n"
	        "}\n"
	        "void main() {\n"
	        "    NamedMethods value();\n"
	        "    print(value.size()); print(value.front()); print(value.back());\n"
	        "    print(value.top()); print(value.keys()); print(value.values());\n"
	        "    print(value.clone()); print(value.push(7)); print(value.has(9));\n"
	        "}\n",
	        "1\n2\n3\n4\n5\n6\n7\n8\ntrue\n",
	        "user methods may share names with container methods"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    uint8 u8 = 250u; uint8 other = 5u;\n"
	        "    print(u8 + other); print(~u8);\n"
	        "    unsigned int maximum = 4294967295u;\n"
	        "    print(maximum + 1u); print(1u << 31);\n"
	        "}\n",
	        "255\n4294967045\n0\n2147483648\n",
	        "integer promotions, unsigned wrapping, and shifts"))
	{
		return EXIT_FAILURE;
	}

	#if defined(_WIN32)
	const std::string DomainNan = "-nan(ind)\n";
	#elif defined(__APPLE__)
	const std::string DomainNan = "nan\n";
	#else
	const std::string DomainNan = "-nan\n";
	#endif
	if (!EmitAndRun(
	        "import \"math.vs\";\n"
	        "void main() {\n"
	        "    double huge = 1e308; double infinity = huge * huge;\n"
	        "    double invalid = std::sqrt(-1.0);\n"
	        "    print(invalid); print(std::floor(invalid));\n"
	        "    print(std::sin(infinity)); print(std::floor(infinity));\n"
	        "    print(min(invalid, 2.0)); print(min(2.0, invalid));\n"
	        "    print(max(invalid, 2.0)); print(max(2.0, invalid));\n"
	        "}\n",
	        DomainNan + DomainNan + DomainNan + "inf\n2\n" + DomainNan + "2\n" + DomainNan,
	        "math builtins propagate non-finite results without Python exceptions"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"math.vs\"; void main() {\n"
	        "    print(std::exp(1000.0)); print(std::log(0.0)); print(std::log2(-0.0));\n"
	        "    print(std::pow(1e308, 2.0)); print(std::pow(-1e308, 3.0));\n"
	        "    print(std::pow(-0.0, -3.0)); print(std::pow(0.0, -2.0));\n"
	        "}\n",
	        "inf\n-inf\n-inf\ninf\n-inf\n-inf\ninf\n",
	        "math overflow and zero poles preserve native infinities and signs"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "void main() { uint8 value = 255u; value += 1u; }\n",
	        "OverflowError",
	        "compound narrow conversion overflow"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "void main() { print(1u << 32); }\n",
	        "shift count out of range",
	        "shift count validation"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "enum class Mode { One = 1 }\n"
	        "enum class OtherMode { One = 1 }\n"
	        "struct Value { int number = 1; public: void set(int value) { number = value; } }\n"
	        "class Base {}\n"
	        "class Derived : public Base {}\n"
	        "class Other : public Base {}\n"
	        "void main() {\n"
	        "    Base base = Derived();\n"
	        "    auto found = try_cast<Derived>(base);\n"
	        "    auto missing = try_cast<Other>(base);\n"
	        "    auto null_value = try_cast<Other>(nullptr);\n"
	        "    optional<int> number = try_cast<int>(2.9);\n"
	        "    Mode mode = cast<Mode>(3);\n"
	        "    auto wrong_enum = try_cast<OtherMode>(mode);\n"
	        "    optional<Value> held = optional<Value>(Value()); held.get().set(9);\n"
	        "    print(found.has_value()); print(missing.has_value());\n"
	        "    print(null_value.has_value()); print(null_value.get() == nullptr);\n"
	        "    print(number.get()); print(wrong_enum.has_value());\n"
	        "    print(held.get().number); print(cast<int>(mode)); print(mode);\n"
	        "}\n",
	        "true\nfalse\ntrue\ntrue\n2\nfalse\n1\n3\nMode(3)\n",
	        "checked casts, optional values, and enum casts"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Value { int number = 1; }\n"
	        "void main() {\n"
	        "    array<Value> values = {Value()};\n"
	        "    for (Value value : values) { value.number = 9; }\n"
	        "    print(values[0].number);\n"
	        "    Value source = Value(); array<Value> pushed = array<Value>();\n"
	        "    pushed.push(source); source.number = 7; print(pushed[0].number);\n"
	        "    int[] copied = int[](); copied.push(7); print(copied[0]);\n"
	        "    map<string,int> scores = {\"answer\": 42};\n"
	        "    for (int value : scores) { print(value); }\n"
	        "    for (pair<string,int> entry : scores) { print(entry.first); print(entry.second); }\n"
	        "    map<string,Value> objects = {\"item\": Value()};\n"
	        "    objects.values()[0].number = 9; print(objects[\"item\"].number);\n"
	        "}\n",
	        "1\n1\n7\n42\nanswer\n42\n1\n",
	        "foreach and typed container value semantics"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    int value = 1; bool stop = true;\n"
	        "    switch (value) {\n"
	        "        case 1: if (stop) { break; } print(9); break;\n"
	        "        default: print(0);\n"
	        "    }\n"
	        "    print(2);\n"
	        "}\n",
	        "2\n",
	        "nested switch break lowering"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "class Base { public: int read() { return 1; } }\n"
	        "class Derived : public Base { public: int read() { return 2; } }\n"
	        "void main() { Derived value = Derived(); Base base = value;\n"
	        "    print(base.read()); print(value.read()); }\n",
	        "1\n2\n",
	        "non-virtual calls preserve static receiver binding"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "int Mark() { print(\"init\"); return 42; }\n"
	        "class Box { public: static int value = Mark(); }\n"
	        "void main() { print(Box.value); }\n",
	        "init\n42\n",
	        "static field module initialization order"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "int seed=7;class Box{public:static int value=seed+1;};"
	        "void main(){print(Box::value);print(seed);}",
	        "1\n7\n", "static initializers read reserved module global slots") ||
	    !EmitAndRun(
	        "class A{public:static int value=read();};class B{public:static int value=7;};"
	        "int read(){return B::value;}void main(){print(A::value);print(B::value);}",
	        "0\n7\n", "static initializers read reserved later static slots") ||
	    !EmitAndRun(
	        "int first=read();int second=7;int read(){return second;}"
	        "void main(){print(first);print(second);}",
	        "0\n7\n", "global initializers read reserved later global slots"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    array<int> values = {0, 1, 2};\n"
	        "    for (int outer = 0; outer < 2; outer += 1) {\n"
	        "        for (int value : values) {\n"
	        "            if (value == 1) { continue; }\n"
	        "            print(value);\n"
	        "        }\n"
	        "        print(9);\n"
	        "    }\n"
	        "}\n",
	        "0\n2\n9\n0\n2\n9\n",
	        "nested foreach continue targets the innermost loop"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    array<int> values = {1, 2};\n"
	        "    for (int value : values) {\n"
	        "        if (value == 1) { values[1] = 9; values.push(3); }\n"
	        "        print(value);\n"
	        "    }\n"
	        "    print(len(values));\n"
	        "}\n",
	        "1\n9\n3\n",
	        "array foreach snapshots length but reads live elements"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequireBothFailure(
	        "void main() {\n"
	        "    array<int> values = {1, 2};\n"
	        "    for (int value : values) { values.remove_at(0); print(value); }\n"
	        "}\n",
	        "array index out of bounds",
	        "array index out of bounds",
	        "array foreach shrink fails against its snapshotted length"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    int value = 10;\n"
	        "    { int value = 20; print(value); }\n"
	        "    for (int value = 0; value < 3; value += 1) { print(value); }\n"
	        "    print(value);\n"
	        "}\n",
	        "20\n0\n1\n2\n10\n",
	        "lexical block and for-init shadowing"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "int counter = 1;\n"
	        "void update() {\n"
	        "    try { print(0); } finally { counter += 1; }\n"
	        "    switch (1) { case 1: counter += 2; break; default: break; }\n"
	        "    { int counter = 8; counter += 1; print(counter); }\n"
	        "}\n"
	        "void main() { update(); print(counter); }\n",
	        "0\n9\n4\n",
	        "deep global writes and local shadowing"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "int add_one(int value) { return value + 1; }\n"
	        "fn(int)->int global_callable = add_one;\n"
	        "void main() { print(global_callable(41)); }\n",
	        "42\n",
	        "global function values use thread-local global storage"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "namespace State {\n"
	        "    int value = 40;\n"
	        "    int read() { return value; }\n"
	        "}\n"
	        "using namespace State;\n"
	        "void main() {\n"
	        "    print(read());\n"
	        "    print(State.value);\n"
	        "    State.value += 2;\n"
	        "    print(read());\n"
	        "}\n",
	        "40\n40\n42\n",
	        "namespaced globals and using-namespace functions"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "class Box { public: static void ping() { print(2); } }\n"
	        "Box make_box() { print(1); return Box(); }\n"
	        "void main() { make_box().ping(); }\n",
	        "1\n2\n",
	        "static call preserves receiver side effects"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Value { int number = 1; void set(int next) { number = next; } }\n"
	        "void main() {\n"
	        "    Value source = Value();\n"
	        "    (array<Value>().push(source))[0].set(9);\n"
	        "    print(source.number);\n"
	        "    print(array<int>());\n"
	        "    pair<int,int> pair = pair<int,int>();\n"
	        "    print(pair);\n"
	        "    print(pair.first); print(pair.second);\n"
	        "    pair.first = 3; pair.second = 4;\n"
	        "    print(pair.first); print(pair.second);\n"
	        "}\n",
	        "1\narray<int>[0]\npair#3:int3:int\n0\n0\n3\n4\n",
	        "temporary generic values retain type and value semantics"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "class pair { public: int value = 9; }\n"
	        "void main(){pair p=pair();pair<int,int> q=pair<int,int>();q.first=4;print(p.value);print(q.first);"
	        "ordered_map<string,pair> values=ordered_map<string,pair>();values[\"a\"]=p;for(pair item:values){print(item.value);}}\n",
	        "9\n4\n9\n", "non-generic user pair coexists with the intrinsic pair and map values"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    array<int> array_a = array<int>(); array<int> array_b = array<int>();\n"
	        "    print(array_a == array_b); array_b = array_a; array_a.push(7);\n"
	        "    print(array_a == array_b); print(array_b[0]);\n"
	        "    map<string,int> map_a = map<string,int>(); map<string,int> map_b = map<string,int>();\n"
	        "    print(map_a == map_b); map_b = map_a; map_a[\"x\"] = 8;\n"
	        "    print(map_a == map_b); print(map_b[\"x\"]);\n"
	        "    set<int> set_a = set<int>(); set<int> set_b = set<int>();\n"
	        "    print(set_a == set_b); set_b = set_a; set_a.add(9);\n"
	        "    print(set_a == set_b); print(set_b.has(9));\n"
	        "    deque<int> deque_a = deque<int>(); deque<int> deque_b = deque<int>();\n"
	        "    print(deque_a == deque_b); deque_b = deque_a; deque_a.push_back(10);\n"
	        "    print(deque_a == deque_b); print(deque_b.front());\n"
	        "}\n",
	        "false\ntrue\n7\nfalse\ntrue\n8\nfalse\ntrue\ntrue\nfalse\ntrue\n10\n",
	        "container assignments share handles and equality uses identity"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Value { int number = 1; }\n"
	        "void main() {\n"
	        "    Value first = Value(); Value second = Value();\n"
	        "    print(first == second);\n"
	        "    second.number = 2; print(first == second);\n"
	        "    Value copied = first; copied.number = 3;\n"
	        "    print(first.number); print(first == copied);\n"
	        "    pair<int,int> pair_a = pair<int,int>();\n"
	        "    pair<int,int> pair_b = pair<int,int>();\n"
	        "    print(pair_a == pair_b); pair_b.second = 1; print(pair_a == pair_b);\n"
	        "}\n",
	        "true\nfalse\n1\nfalse\ntrue\nfalse\n",
	        "struct and pair equality compare fields"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "enum class ByteMode : uint8 { One = 1 }\n"
	        "void main() {\n"
	        "    print(try_cast<ByteMode>(255).has_value());\n"
	        "    print(try_cast<ByteMode>(256).has_value());\n"
	        "    map<ByteMode,int> scores = map<ByteMode,int>();\n"
	        "    scores[ByteMode.One] = 7; print(scores.has(ByteMode.One));\n"
	        "    set<ByteMode> selected = {ByteMode.One}; print(selected.has(ByteMode.One));\n"
	        "    print(ByteMode.One == ByteMode.One);\n"
	        "}\n",
	        "true\nfalse\ntrue\ntrue\ntrue\n",
	        "enum casts, equality, and hashing preserve strong type identity"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    print(try_cast<char>(127).has_value());\n"
	        "    print(try_cast<char>(128).has_value());\n"
	        "    float stored = 0.1f;\n"
	        "    print(stored);\n"
	        "    print(0.1f);\n"
	        "    print(stored + 0.0f);\n"
	        "    double precise = 0.1;\n"
	        "    print(precise);\n"
	        "}\n",
	        "true\nfalse\n0.100000001\n0.100000001\n0.100000001\n0.1\n",
	        "char range and float32 runtime identity"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "import \"math.vs\";\n"
	        "void main() {\n"
	        "    float value = to_float(0.1);\n"
	        "    print(to_float(0.1)); print(abs(-value));\n"
	        "    print(min(1, value)); print(max(-1, -value));\n"
	        "    print(std::floor(3.9)); print(std::ceil(3.1));\n"
	        "}\n",
	        "0.100000001\n0.100000001\n0.100000001\n-0.100000001\n3\n4\n",
	        "builtin calls preserve their VeroScript result types"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "void main() {\n"
	        "    print(to_float(1e308)); print(to_float(-1e308));\n"
	        "    float large = to_float(3.4e38); print(large * large);\n"
	        "}\n",
	        "inf\n-inf\ninf\n",
	        "float32 conversions and arithmetic overflow to infinity"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRequirePythonFailure(
	        "void main() { int8 minimum = cast<int8>(-128); print(abs(minimum)); }\n",
	        "OverflowError",
	        "abs preserves narrow integer overflow checks"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Value { int number = 1; }\n"
	        "void main() {\n"
	        "    stack<int> history = {1, 2}; array<int> snapshot = history.values();\n"
	        "    history.push(3);\n"
	        "    print(len(snapshot)); print(snapshot[0]); print(snapshot[1]);\n"
	        "    print(history.values()[0]); print(history.values()[2]);\n"
	        "    stack<Value> objects = {Value()}; array<Value> copies = objects.values();\n"
	        "    copies[0].number = 9; print(objects.top().number);\n"
	        "}\n",
	        "2\n1\n2\n1\n3\n1\n",
	        "stack values returns an ordered value snapshot"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Point { int x; }\n"
	        "struct Point3 : Point { int z; }\n"
	        "class Box {}\n"
	        "void main() {\n"
	        "    Point3 derived = Point3(); derived.x = 7;\n"
	        "    Point base = derived;\n"
	        "    print(base); print(derived); base.x = 9; print(derived.x);\n"
	        "    print(Box());\n"
	        "}\n",
	        "python-aot-test.vs\x1f"
	        "Point\npython-aot-test.vs\x1f"
	        "Point3\n7\npython-aot-test.vs\x1f"
	        "Box\n",
	        "runtime object type names retain module identity"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitAndRun(
	        "struct Value { int number = 1; }\n"
	        "void main() {\n"
	        "    try { throw true; } catch (int wrong) { print(0); } catch (bool right) { print(right); }\n"
	        "    Value original = Value();\n"
	        "    try { throw original; } catch (Value copy) { copy.number = 9; }\n"
	        "    print(original.number);\n"
	        "}\n",
	        "true\n1\n",
	        "exact catch matching and catch value copies"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "class A {} class B {} class C : public A, public B {} void main() {}\n",
	        "multiple concrete class bases",
	        "multiple concrete bases fail closed"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "void main() { multiset<int> values = multiset<int>(); }\n",
	        "is not supported in standalone Python output",
	        "unsupported container type fails closed"))
	{
		return EXIT_FAILURE;
	}

	if (!EmitMustFail(
	        "class UserBox<T> { public: T value; } void main() { UserBox<int> box(); }\n",
	        "open generic class 'UserBox' is not supported yet",
	        "user open generic type still fails closed"))
	{
		return EXIT_FAILURE;
	}
	for (const std::string& TypeName : {std::string("shared"), std::string("handle")})
	{
		if (!EmitMustFail(
		        "void main() { " + TypeName + "<int> owner = " + TypeName + "<int>(3); }\n",
		        "is not supported in standalone Python output",
		        "shared/handle wrappers fail closed in standalone Python output"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!EmitMustFail(
	        "void main() { auto value = function(int x)->int => x + 1; print(value(1)); }\n",
	        "lambdas are not supported",
	        "source lambda fails closed"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "int value = 21; void main() { int value = value + 21; print(value); "
	        "{ int value = value + 1; print(value); } print(value); }",
	        "42\n43\n42\n", "declaration initializers resolve the enclosing scope"))
	{
		return EXIT_FAILURE;
	}
	if (!EmitAndRun(
	        "int value = 21; void main() { "
	        "for(int first = (value = value + 1), second = first + 2; first < second; ++first) { print(first); } "
	        "print(value); for(int value = value - 20, second = value + 10; value < 4; ++value) { "
	        "if(value == 2) { continue; } print(second + value); } print(value); }",
	        "22\n23\n22\n15\n22\n", "for declaration groups preserve order, side effects and shadowing"))
	{
		return EXIT_FAILURE;
	}
	for (const std::string& Source : {
	         std::string("fn(int)->int Make(int x) { return fn(int x)->int => x + 1; } void main() {}"),
	         std::string("int Apply(fn(int)->int f) { return f(41); } void main() { print(Apply(fn(int x)->int => x + 1)); }"),
	         std::string("void main() { print((fn(int x)->int => x + 1)(41)); }"),
	         std::string("fn()->int Make(int x) { return [=]()->int => x; } void main() {}")})
	{
		if (!EmitMustFail(Source, "lambdas are not supported",
		        "source lambda fails closed in every expression position"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FPythonCodegenBackend Backend;
		VeroScript::FCodegenResult Result;
		if (!Expect(Backend.GetName() == "python", "backend name") || !Expect(Backend.GetFileExtension() == ".py", "backend extension") || !Expect(Backend.EmitFromSource("void main() { print(42); }", "backend.vs", {}, Result), "backend emit") || !Expect(Result.bSuccess, "backend success"))
		{
			std::cerr << Result.Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		namespace fs = std::filesystem;
		const fs::path Root = fs::temp_directory_path() / ("veroscript_python_aot_file_" + std::to_string(std::rand()));
		std::error_code ErrorCode;
		fs::create_directories(Root, ErrorCode);
		const fs::path Library = Root / "library.vs";
		const fs::path Script = Root / "main.vs";
		const fs::path Generated = Root / "main.py";
		{
			std::ofstream Stream(Library, std::ios::binary);
			Stream << "export int imported_value() { return 42; }\n";
		}
		{
			std::ofstream Stream(Script, std::ios::binary);
			Stream << "import \"library.vs\";\nvoid main() { print(imported_value()); }\n";
		}
		std::string Error;
		std::string Python;
		if (!Expect(
		        VeroScript::EmitPythonFromFile(
		            Script.string(), Generated.string(), Error, {Root.string()}),
		        "file API with import") ||
		    !Expect(ReadFile(Generated, Python), "generated Python file exists") || !RunPython(Python, "42\n", "generated file with import"))
		{
			std::cerr << Error << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		{
			std::ofstream Stream(Script, std::ios::binary | std::ios::trunc);
			Stream << "import \"library.vs\";\nvoid main() { print(imported_value() + 1); }\n";
		}
		std::string ReplacementError;
		std::string ReplacementPython;
		if (!Expect(
		        VeroScript::EmitPythonFromFile(
		            Script.string(), Generated.string(), ReplacementError, {Root.string()}),
		        "file API atomically replaces existing output") ||
		    !Expect(ReadFile(Generated, ReplacementPython), "replacement Python file exists") || !RunPython(ReplacementPython, "43\n", "replaced generated file"))
		{
			std::cerr << ReplacementError << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		const fs::path ExistingDirectory = Root / "existing-directory.py";
		const fs::path Sentinel = ExistingDirectory / "keep.txt";
		fs::create_directory(ExistingDirectory, ErrorCode);
		{
			std::ofstream Stream(Sentinel, std::ios::binary);
			Stream << "preserve this directory";
		}
		std::string DirectoryError;
		std::string SentinelContents;
		if (!Expect(!VeroScript::EmitPythonFromFile(
		                Script.string(), ExistingDirectory.string(), DirectoryError, {Root.string()}),
		        "file API rejects a directory as output") ||
		    !Expect(fs::is_directory(ExistingDirectory), "output directory is not moved or replaced") || !Expect(ReadFile(Sentinel, SentinelContents) && SentinelContents == "preserve this directory", "directory contents are preserved after rejected file emission"))
		{
			std::cerr << DirectoryError << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		const fs::path InputDirectory = Root / "unreadable.vs";
		fs::create_directory(InputDirectory, ErrorCode);
		std::string ReadError, PreservedPython;
		VeroScript::FPythonAotSourceBundle UnreadableBundle;
		if (!Expect(!VeroScript::EmitPythonFromFile(
		                InputDirectory.string(), Generated.string(), ReadError),
		        "file API rejects unreadable source before output replacement") ||
		    !Expect(ReadError.find("script: " + InputDirectory.string()) != std::string::npos,
		        "source read failure retains the input path") ||
		    !Expect(ReadFile(Generated, PreservedPython) && PreservedPython == ReplacementPython,
		        "source read failure preserves the previous output") ||
		    !Expect(!VeroScript::EmitPythonSourceBundleFromFile(
		                InputDirectory.string(), "unreadable", UnreadableBundle, ReadError),
		        "bundle API rejects unreadable source") ||
		    !Expect(UnreadableBundle.Files.empty() && UnreadableBundle.ManifestJson.empty() &&
		            ReadError.find("script: " + InputDirectory.string()) != std::string::npos,
		        "bundle source read failure publishes no partial output"))
		{
			std::cerr << ReadError << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		const fs::path EmptyDirectory = Root / "empty-directory.py";
		fs::create_directory(EmptyDirectory, ErrorCode);
		if (!Expect(!VeroScript::EmitPythonFromFile(
		                Script.string(), EmptyDirectory.string(), DirectoryError, {Root.string()}),
		        "file API rejects an empty directory as output") ||
		    !Expect(fs::is_directory(EmptyDirectory) && fs::is_empty(EmptyDirectory),
		        "empty output directory is preserved"))
		{
			std::cerr << DirectoryError << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		const fs::path UnicodeOutput = Root / VeroScript::PathUtil::FromUtf8("output-\xF0\x9F\x90\x8D.py");
		{
			std::ofstream Stream(UnicodeOutput, std::ios::binary);
			Stream << "previous output";
		}
		std::string UnicodeError;
		std::string UnicodePython;
		if (!Expect(VeroScript::EmitPythonFromFile(
		                VeroScript::PathUtil::ToUtf8(Script), VeroScript::PathUtil::ToUtf8(UnicodeOutput),
		                UnicodeError, {VeroScript::PathUtil::ToUtf8(Root)}),
		        "file API replaces output with a Unicode filename") ||
		    !Expect(ReadFile(UnicodeOutput, UnicodePython), "Unicode output exists") || !RunPython(UnicodePython, "43\n", "generated output with Unicode filename"))
		{
			std::cerr << UnicodeError << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		{
			std::ofstream Stream(Script, std::ios::binary | std::ios::trunc);
			Stream << "void main( {\n";
		}
		std::string FailedError;
		std::string Preserved;
		if (!Expect(
		        !VeroScript::EmitPythonFromFile(
		            Script.string(), Generated.string(), FailedError, {Root.string()}),
		        "failed file emission is rejected") ||
		    !Expect(ReadFile(Generated, Preserved), "previous generated file is preserved") || !Expect(Preserved == ReplacementPython, "failed generation does not replace output"))
		{
			std::cerr << FailedError << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, ErrorCode);
	}

	{
		namespace fs = std::filesystem;
		const fs::path Root = fs::temp_directory_path() / ("veroscript_python_aot_bundle_" + std::to_string(std::rand()));
		std::error_code ErrorCode;
		fs::create_directories(Root, ErrorCode);
		const fs::path Library = Root / "library.vs";
		const fs::path Script = Root / "main.vs";
		const fs::path BundleDirectory = Root / "generated_bundle";
		{
			std::ofstream Stream(Library, std::ios::binary);
			Stream << "import \"concurrent.vs\";\n"
			       << "int module_value = 40;\n"
			       << "export class ImportedBox {\n"
			       << "public:\n"
			       << "    static int base = 40;\n"
			       << "    int value = 2;\n"
			       << "};\n"
			       << "export int imported_value() {\n"
			       << "    ImportedBox box();\n"
			       << "    return module_value + box.value;\n"
			       << "}\n"
			       << "int private_answer() { return 41; }\n"
			       << "export int imported_private_answer() { return private_answer(); }\n"
			       << "export void imported_worker(channel<int> output) {\n"
			       << "    output.send(imported_value());\n"
			       << "}\n"
			       << "void private_worker(channel<int> output) { output.send(43); }\n"
			       << "export int run_private_worker() {\n"
			       << "    channel<int> output = channel<int>();\n"
			       << "    int task_id = std::concurrent::spawn(\"private_worker\", output);\n"
			       << "    std::concurrent::join_task(task_id);\n"
			       << "    return output.recv();\n"
			       << "}\n";
		}
		{
			std::ofstream Stream(Script, std::ios::binary);
			Stream << "import \"library.vs\";\n"
			       << "import \"concurrent.vs\";\n"
			       << "int module_value = 1;\n"
			       << "int private_answer() { return 42; }\n"
			       << "void private_worker(channel<int> output) { output.send(44); }\n"
			       << "int run_local_private_worker() {\n"
			       << "    channel<int> output = channel<int>();\n"
			       << "    int task_id = std::concurrent::spawn(\"private_worker\", output);\n"
			       << "    std::concurrent::join_task(task_id);\n"
			       << "    return output.recv();\n"
			       << "}\n"
			       << "void main() {\n"
			       << "    print(module_value);\n"
			       << "    print(imported_value());\n"
			       << "    print(imported_private_answer());\n"
			       << "    print(private_answer());\n"
			       << "    channel<int> output=std::concurrent::make_channel<int>();\n"
			       << "    int id=std::concurrent::spawn(\"imported_worker\",output); std::concurrent::join_task(id);\n"
			       << "    print(output.recv());\n"
			       << "    print(run_private_worker());\n"
			       << "    print(run_local_private_worker());\n"
			       << "}\n";
		}
		VeroScript::FPythonAotSourceBundle Bundle;
		std::string Error;
		if (!Expect(
		        VeroScript::EmitPythonSourceBundleFromFile(
		            Script.string(), "sample", Bundle, Error, {Root.string()}),
		        "source-bundle API with import") ||
		    !Expect(Bundle.Files.size() == 4, "source bundle has entry, package, runtime, and program") || !Expect(Bundle.ManifestJson.find("veroscript.python-aot-source-bundle") != std::string::npos, "source bundle manifest kind") || !Expect(VeroScript::WritePythonSourceBundle(Bundle, BundleDirectory.string(), Error), "source bundle writes atomically") || !Expect(fs::is_regular_file(BundleDirectory / "__main__.py"), "bundle entry exists") || !Expect(fs::is_regular_file(BundleDirectory / "_veroscript_runtime.py"), "bundle runtime exists") || !Expect(fs::is_regular_file(BundleDirectory / "_veroscript_program.py"), "bundle program exists") || !Expect(fs::is_regular_file(BundleDirectory / "python-aot.manifest.json"), "bundle manifest exists") || !RunPythonPath(BundleDirectory, "1\n42\n41\n42\n42\n43\n44\n", "generated Python source bundle"))
		{
			std::cerr << Error << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}

		{
			std::ofstream Stream(Script, std::ios::binary | std::ios::trunc);
			Stream << "import \"library.vs\";\nvoid main() { print(imported_value() + 1); }\n";
		}
		VeroScript::FPythonAotSourceBundle Replacement;
		if (!Expect(
		        VeroScript::EmitPythonSourceBundleFromFile(
		            Script.string(), "sample", Replacement, Error, {Root.string()}),
		        "replacement source bundle emits") ||
		    !Expect(
		        VeroScript::WritePythonSourceBundle(
		            Replacement, BundleDirectory.string(), Error),
		        "owned source bundle can be replaced") ||
		    !RunPythonPath(BundleDirectory, "43\n", "replaced Python source bundle"))
		{
			std::cerr << Error << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}

		const fs::path UnicodeDirectory = Root / "nested" / VeroScript::PathUtil::FromUtf8("bundle-\xF0\x9F\x90\x8D");
		std::string UnicodeProgram;
		try
		{
			if (!Expect(VeroScript::WritePythonSourceBundle(
			                Bundle, VeroScript::PathUtil::ToUtf8(UnicodeDirectory), Error),
			        "source bundle writes to a Unicode directory") ||
			    !Expect(ReadFile(UnicodeDirectory / "_veroscript_program.py", UnicodeProgram) && UnicodeProgram == Bundle.Files.back().Contents,
			        "Unicode bundle program matches generated source") ||
			    !Expect(VeroScript::WritePythonSourceBundle(
			                Replacement, VeroScript::PathUtil::ToUtf8(UnicodeDirectory), Error),
			        "source bundle replaces a Unicode directory") ||
			    !Expect(ReadFile(UnicodeDirectory / "_veroscript_program.py", UnicodeProgram) && UnicodeProgram == Replacement.Files.back().Contents,
			        "Unicode bundle replacement contains the new program"))
			{
				std::cerr << Error << '\n';
				fs::remove_all(Root, ErrorCode);
				return EXIT_FAILURE;
			}
			const fs::path ForeignFile = UnicodeDirectory / VeroScript::PathUtil::FromUtf8("keep-\xF0\x9F\x90\x8D.txt");
			{
				std::ofstream Stream(ForeignFile, std::ios::binary);
				Stream << "preserve foreign content";
			}
			std::string ForeignContents;
			if (!Expect(!VeroScript::WritePythonSourceBundle(
			                Bundle, VeroScript::PathUtil::ToUtf8(UnicodeDirectory), Error),
			        "Unicode foreign content prevents bundle replacement") ||
			    !Expect(ReadFile(ForeignFile, ForeignContents) && ForeignContents == "preserve foreign content",
			        "Unicode foreign content is preserved"))
			{
				std::cerr << Error << '\n';
				fs::remove_all(Root, ErrorCode);
				return EXIT_FAILURE;
			}
		}
		catch (const fs::filesystem_error& Exception)
		{
			std::cerr << "FAIL: Unicode source-bundle paths must not use ANSI conversion: "
			          << Exception.what() << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}

		{
			std::ofstream Stream(Library, std::ios::binary | std::ios::app);
			Stream << "void main() {}\n";
		}
		VeroScript::FPythonAotSourceBundle ImportedMain;
		std::string ImportedMainError;
		if (!Expect(
		        !VeroScript::EmitPythonSourceBundleFromFile(
		            Script.string(), "sample", ImportedMain, ImportedMainError,
		            {Root.string()}),
		        "imported main is rejected") ||
		    !Expect(ImportedMainError.find("imported main() is not supported") != std::string::npos, "imported main diagnostic is explicit"))
		{
			std::cerr << ImportedMainError << '\n';
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}

		VeroScript::FPythonAotSourceBundle Invalid;
		std::string InvalidError;
		if (!Expect(
		        !VeroScript::BuildPythonSourceBundle(
		            "print(1)\n", "../unsafe", Invalid, InvalidError),
		        "unsafe source-bundle name is rejected"))
		{
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		VeroScript::FPythonAotSourceBundle UnsafeFiles = Replacement;
		UnsafeFiles.Files.push_back({"../escape.py", "print('unsafe')\n"});
		std::string UnsafeFilesError;
		if (!Expect(
		        !VeroScript::WritePythonSourceBundle(
		            UnsafeFiles, (Root / "unsafe_bundle").string(), UnsafeFilesError),
		        "source-bundle traversal path is rejected") ||
		    !Expect(!fs::exists(Root / "escape.py"),
		        "source-bundle traversal writes nothing outside output"))
		{
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		VeroScript::FPythonAotSourceBundle NonPortableFiles = Replacement;
		NonPortableFiles.Files.push_back({"nested\\module.py", "print('unsafe')\n"});
		std::string NonPortableFilesError;
		if (!Expect(
		        !VeroScript::WritePythonSourceBundle(
		            NonPortableFiles, (Root / "nonportable_bundle").string(),
		            NonPortableFilesError),
		        "source-bundle backslash path is rejected"))
		{
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		const fs::path ForeignDirectory = Root / "foreign";
		fs::create_directories(ForeignDirectory, ErrorCode);
		{
			std::ofstream Stream(ForeignDirectory / "keep.txt", std::ios::binary);
			Stream << "keep";
		}
		{
			std::ofstream Stream(
			    ForeignDirectory / "python-aot.manifest.json", std::ios::binary);
			Stream << "{\"kind\":\"foreign\"}\n";
		}
		std::string ForeignError;
		if (!Expect(
		        !VeroScript::WritePythonSourceBundle(
		            Replacement, ForeignDirectory.string(), ForeignError),
		        "non-bundle directory is not replaced") ||
		    !Expect(fs::is_regular_file(ForeignDirectory / "keep.txt"),
		        "foreign directory content is preserved"))
		{
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		const fs::path ForgedDirectory = Root / "forged";
		fs::create_directories(ForgedDirectory, ErrorCode);
		{
			std::ofstream Stream(ForgedDirectory / "keep.txt", std::ios::binary);
			Stream << "keep";
		}
		{
			std::ofstream Stream(
			    ForgedDirectory / "python-aot.manifest.json", std::ios::binary);
			Stream << "{\"version\":1,\"kind\":\"veroscript.python-aot-source-bundle\","
			          "\"name\":\"forged\",\"entry\":\"__main__.py\",\"files\":[],"
			          "\"run\":\"python <output-directory>\"}\n";
		}
		std::string ForgedError;
		if (!Expect(
		        !VeroScript::WritePythonSourceBundle(
		            Replacement, ForgedDirectory.string(), ForgedError),
		        "forged source-bundle manifest cannot claim a foreign directory") ||
		    !Expect(fs::is_regular_file(ForgedDirectory / "keep.txt"),
		        "forged directory content is preserved"))
		{
			fs::remove_all(Root, ErrorCode);
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, ErrorCode);
	}

	{
		std::string Python;
		std::string Error;
		const bool Success = VeroScript::EmitPythonFromSource(
		    "int pick(int x) { return x; }\n"
		    "string pick(string x) { return x; }\n"
		    "void main() {}\n",
		    "unsupported.vs", Python, Error);
		if (!Expect(!Success, "unsupported overload rejected") || !Expect(Python.empty(), "failed emission has no output") || !Expect(Error.find("overloaded free function") != std::string::npos, "unsupported diagnostic is explicit"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		struct FGetter final : VeroScript::IAnnotationProcessor
		{
			void Process(const VeroScript::FAnnotationContext& Context,
			    VeroScript::FAnnotationOutput& Output) override
			{
				Output.SubmitMethod(Context.Owner->Id, VeroScript::EMemberAccess::Public,
				    "int Get() const { return value; }");
			}
		};
		auto Registry = std::make_shared<VeroScript::FAnnotationProcessorRegistry>();
		std::string Error;
		if (!Expect(Registry->Register(
		                {"@PythonGetter", "python.aot.getter", "1", "",
		                    VeroScript::AnnotationTargetMask(VeroScript::EAnnotationTarget::Field),
		                    static_cast<std::uint8_t>(VeroScript::EAnnotationOperation::AddMethod), 0,
		                    []
		{
			return std::make_unique<FGetter>();
		}},
		                Error),
		        "register Python AOT processor"))
		{
			return EXIT_FAILURE;
		}
		Registry->Freeze();
		VeroScript::FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.AnnotationProcessing.Registry = Registry;
		VeroScript::FPythonCodegenBackend Backend;
		VeroScript::FCodegenResult Generated;
		if (!Expect(Backend.EmitFromSource(
		                "struct Item { @PythonGetter int value = 42; }; void main() { Item value = Item(); print(value.Get()); }",
		                "python-aot-generated.vs", Options, Generated),
		        "Python AOT prepares generated declarations") ||
		    !RunPython(Generated.Output, "42\n", "Python AOT generated method executes"))
		{
			std::cerr << Generated.Error << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FHostBindingRegistry Bindings;
		if (!Expect(Bindings.RegisterAnnotation("@PythonGetter", "", Error, "", {},
		                VeroScript::AnnotationAllTargets, false),
		        "register Python AOT annotation name policy"))
		{
			return EXIT_FAILURE;
		}
		Options.Bindings = &Bindings;
		const std::string Source =
		    "struct Item { @pYTHONgETTER int value = 42; }; "
		    "void main() { Item value = Item(); print(value.Get()); }";
		const std::string Label = "python-aot-name-policy.vs";
		Options.SourceOverlays = {{Label, Source}};
		for (const bool FromFile : {false, true})
		{
			const bool Emitted = FromFile ? Backend.EmitFromFile(Label, Options, Generated)
			                              : Backend.EmitFromSource(Source, Label, Options, Generated);
			if (!Expect(Emitted, "Python AOT uses registered annotation name policies") || !RunPython(Generated.Output, "42\n", "Python AOT folded annotation generates a method"))
			{
				std::cerr << Generated.Error << '\n';
				return EXIT_FAILURE;
			}
			VeroScript::FPythonAotSourceBundle Bundle;
			const bool Bundled = FromFile
			    ? VeroScript::EmitPythonSourceBundleFromFile(Label, "name-policy", Options, Bundle, Error)
			    : VeroScript::EmitPythonSourceBundleFromSource(Source, Label, "name-policy", Options, Bundle, Error);
			const auto Directory = std::filesystem::temp_directory_path() / ("python-aot-name-policy-" + std::to_string(std::rand()));
			const bool Passed = Expect(Bundled, "Python AOT bundles use registered annotation name policies") && Expect(VeroScript::WritePythonSourceBundle(Bundle, VeroScript::PathUtil::ToUtf8(Directory), Error), "write Python AOT annotation policy bundle") && RunPythonPath(Directory, "42\n", "Python AOT folded annotation bundle executes");
			std::error_code Ignored;
			std::filesystem::remove_all(Directory, Ignored);
			if (!Passed)
			{
				std::cerr << Error << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	{
		namespace fs = std::filesystem;
		const auto Root = fs::temp_directory_path() / ("python-aot-definitions-" + std::to_string(std::rand()));
		fs::create_directories(Root);
		{
			std::ofstream Declaration(Root / "declaration.vs");
			Declaration << "using Number=int; export int Transform(Number value); "
			               "export class Reader { public: int Get(Number value); };";
			std::ofstream Implementation(Root / "implementation.vs");
			Implementation << "import \"declaration.vs\"; using Number=string; int helper(){return 40;} "
			                  "int ::Transform(int input){return input+helper();} "
			                  "int Reader::Get(int input){return input+helper();}";
		}
		std::string Python, Error;
		const bool Emitted = VeroScript::EmitPythonFromSource(
		    "import \"declaration.vs\"; import \"implementation.vs\"; void main(){"
		    "print(Transform(2)); Reader reader = Reader(); print(reader.Get(3));}",
		    (Root / "main.vs").generic_string(), Python, Error);
		std::error_code Ignored;
		fs::remove_all(Root, Ignored);
		if (!Expect(Emitted, "Python AOT resolves cross-file bodies and declaration type aliases") || !RunPython(Python, "42\n43\n", "Python AOT cross-file bodies use implementation scope"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}
	std::cout << "VeroScriptPythonAot tests passed\n";
	return EXIT_SUCCESS;
}
