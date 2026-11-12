// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptLambdaStaticFixture.h"

#include "VeroScriptCppAot.h"
#include "VeroScriptCppScriptCompileFixture.h"
#include "VeroScriptCompile.h"

#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptParser.h"
#include "VeroScriptPath.h"
#include "VeroScriptVm.h"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
	std::filesystem::path CreateUniqueTempDirectory(const std::string_view Prefix)
	{
		namespace fs = std::filesystem;
		const fs::path Root = fs::temp_directory_path();
		for (std::uint32_t Attempt = 0; Attempt < 1024u; ++Attempt)
		{
			const auto Stamp = std::chrono::high_resolution_clock::now()
			                       .time_since_epoch()
			                       .count();
			const fs::path Candidate = Root /
			    (std::string(Prefix) + '_' + std::to_string(Stamp) + '_' + std::to_string(Attempt));
			std::error_code Error;
			if (fs::create_directory(Candidate, Error))
			{
				return Candidate;
			}
			if (Error && Error != std::errc::file_exists)
			{
				throw fs::filesystem_error(
				    "cannot create unique AOT test directory", Candidate, Error);
			}
		}
		throw std::runtime_error("cannot allocate a unique AOT test directory");
	}

	class FScopedTempDirectory final
	{
	public:
		explicit FScopedTempDirectory(const std::string_view Prefix)
		    : Directory(CreateUniqueTempDirectory(Prefix))
		{
		}

		~FScopedTempDirectory()
		{
			std::error_code Error;
			#if defined(_WIN32)
			auto Native = std::filesystem::absolute(Directory, Error).lexically_normal().native();
			if (!Error && Native.rfind(L"\\\\?\\", 0) != 0)
			{
				Native = Native.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + Native.substr(2) : L"\\\\?\\" + Native;
			}
			std::filesystem::remove_all(Error ? Directory : std::filesystem::path(Native), Error);
			#else
			std::filesystem::remove_all(Directory, Error);
			#endif
		}

		FScopedTempDirectory(const FScopedTempDirectory&) = delete;
		FScopedTempDirectory& operator=(const FScopedTempDirectory&) = delete;

		const std::filesystem::path& GetPath() const noexcept
		{
			return Directory;
		}

	private:
		std::filesystem::path Directory;
	};

	bool Expect(const bool Condition, const char* Label)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Condition;
	}

	bool RunVm(
	    const std::string& Source,
	    std::string& OutOutput,
	    std::string* OutError = nullptr,
	    const std::vector<std::string>& ImportPaths = {})
	{
		VeroScript::VeroScriptModuleLoader Loader("aot.vs");
		for (const std::string& Path : ImportPaths)
		{
			Loader.AddImportPath(Path);
		}
		Loader.SetSourceOverlay("aot.vs", Source);
		std::unique_ptr<VeroScript::FProgram> Program =
		    Loader.LoadSourceModule(Source, "aot.vs");
		if (!Program || Loader.HasError())
		{
			if (OutError)
			{
				*OutError = Loader.HasError() ? Loader.GetError().Message : "module load";
			}
			return false;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			if (OutError)
			{
				*OutError = Compiler.GetError().Message;
			}
			return false;
		}
		std::ostringstream Output;
		std::istringstream In;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, In, Errors);
		VeroScript::Test::Configure(Vm);
		const bool bSucceeded = Vm.Execute(*Module, VeroScript::ERunMode::Script);
		OutOutput = Output.str();
		if (!bSucceeded)
		{
			if (OutError)
			{
				*OutError = Vm.GetError().Message;
			}
			return false;
		}
		return true;
	}

	bool ReadTextFile(const std::filesystem::path& Path, std::string& Out)
	{
		std::ifstream In(Path, std::ios::binary);
		if (!In)
		{
			return false;
		}
		std::ostringstream Buf;
		Buf << In.rdbuf();
		Out = Buf.str();
		return true;
	}

	std::string NormalizeNewlines(std::string Text)
	{
		std::string Out;
		Out.reserve(Text.size());
		for (std::size_t Index = 0; Index < Text.size(); ++Index)
		{
			if (Text[Index] == '\r')
			{
				if (Index + 1 < Text.size() && Text[Index + 1] == '\n')
				{
					continue;
				}
				Out.push_back('\n');
				continue;
			}
			Out.push_back(Text[Index]);
		}
		return Out;
	}

	bool CommandOnPath(const char* Name)
	{
		#if defined(_WIN32)
		char Buffer[MAX_PATH];
		return SearchPathA(nullptr, Name, ".exe", MAX_PATH, Buffer, nullptr) != 0;
		#else
		const std::string Cmd = std::string("command -v ") + Name + " >/dev/null 2>&1";
		return std::system(Cmd.c_str()) == 0;
		#endif
	}

	#if defined(_WIN32)
	std::string RunCapture(const std::string& Command)
	{
		std::string Result;
		FILE* Pipe = _popen(Command.c_str(), "r");
		if (!Pipe)
		{
			return {};
		}
		char Buf[512];
		while (fgets(Buf, sizeof(Buf), Pipe))
		{
			Result += Buf;
		}
		_pclose(Pipe);
		while (!Result.empty() && (Result.back() == '\n' || Result.back() == '\r' || Result.back() == ' '))
		{
			Result.pop_back();
		}
		return Result;
	}

	std::string FindVcvars64()
	{
		if (const char* Env = std::getenv("VEROSCRIPT_AOT_VCVARS"))
		{
			if (Env[0] != '\0' && std::filesystem::exists(Env))
			{
				return Env;
			}
		}
		const char* Pf86 = std::getenv("ProgramFiles(x86)");
		if (!Pf86)
		{
			return {};
		}
		const std::filesystem::path Vswhere =
		    std::filesystem::path(Pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
		if (!std::filesystem::exists(Vswhere))
		{
			return {};
		}
		const std::string Cmd =
		    "\"" + Vswhere.string() + "\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
		                              "-property installationPath";
		const std::string Install = RunCapture(Cmd);
		if (Install.empty())
		{
			return {};
		}
		const std::filesystem::path Vcvars =
		    std::filesystem::path(Install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
		if (!std::filesystem::exists(Vcvars))
		{
			return {};
		}
		return Vcvars.string();
	}
	#endif

	enum class ENativeDualResult
	{
		Matched,
		Skipped,
		Failed,
	};

	int gNativeDualMatched = 0;
	int gNativeDualSkipped = 0;

	bool EnvFlagEnabled(const char* Name)
	{
		const char* const Value = std::getenv(Name);
		if (!Value || Value[0] == '\0')
		{
			return false;
		}
		return Value[0] != '0' && Value[0] != 'f' && Value[0] != 'F' && Value[0] != 'n' && Value[0] != 'N';
	}

	// CI / explicit opt-in: DualRun must compile and run generated C++.
	bool RequireNativeDualRun()
	{
		if (EnvFlagEnabled("VEROSCRIPT_AOT_REQUIRE_NATIVE"))
		{
			return true;
		}
		return EnvFlagEnabled("CI") || EnvFlagEnabled("GITHUB_ACTIONS") || EnvFlagEnabled("TF_BUILD") || EnvFlagEnabled("GITLAB_CI");
	}

	ENativeDualResult RunNativeCpp(
	    const std::string& Cpp,
	    const std::string& ExpectedOutput,
	    std::string& OutError)
	{
		namespace fs = std::filesystem;
		const fs::path Dir = CreateUniqueTempDirectory("veroscript_aot_dual");
		std::error_code Ec;
		const fs::path CppPath = Dir / "aot_gen.cpp";
		const fs::path OutPath = Dir / "stdout.txt";
		{
			std::ofstream File(CppPath, std::ios::binary);
			if (!File)
			{
				OutError = "native: cannot write temp cpp";
				return ENativeDualResult::Failed;
			}
			File << Cpp;
		}

		auto Finish = [&](const ENativeDualResult Result)
		{
			if (Result == ENativeDualResult::Failed && EnvFlagEnabled("VEROSCRIPT_AOT_KEEP_FAILED_NATIVE"))
			{
				OutError += "\nnative artifacts retained at: " + Dir.string();
				return Result;
			}
			fs::remove_all(Dir, Ec);
			return Result;
		};

		#if defined(_WIN32)
		const std::string Vcvars = FindVcvars64();
		if (!Vcvars.empty())
		{
			const fs::path Bat = Dir / "build_run.bat";
			{
				std::ofstream BatFile(Bat);
				BatFile << "@echo off\r\n"
				        << "call \"" << Vcvars << "\" >nul\r\n"
				        << "cd /d \"" << Dir.string() << "\"\r\n"
				        << "cl /nologo /EHsc /std:c++17 /Fe:aot_run.exe aot_gen.cpp >cl_log.txt 2>&1\r\n"
				        << "if errorlevel 1 exit /b 1\r\n"
				        << "aot_run.exe > stdout.txt\r\n";
			}
			const int Code = std::system(("\"" + Bat.string() + "\"").c_str());
			if (Code != 0)
			{
				std::string Log;
				ReadTextFile(Dir / "cl_log.txt", Log);
				OutError = "native: MSVC compile/run failed\n" + Log;
				return Finish(ENativeDualResult::Failed);
			}
			std::string NativeOut;
			if (!ReadTextFile(OutPath, NativeOut))
			{
				OutError = "native: missing stdout capture";
				return Finish(ENativeDualResult::Failed);
			}
			NativeOut = NormalizeNewlines(std::move(NativeOut));
			if (NativeOut != ExpectedOutput)
			{
				OutError = "native=[" + NativeOut + "] expected=[" + ExpectedOutput + "]";
				return Finish(ENativeDualResult::Failed);
			}
			return Finish(ENativeDualResult::Matched);
		}
		#endif

		const char* Compiler = nullptr;
		if (const char* Env = std::getenv("VEROSCRIPT_AOT_CXX"))
		{
			if (Env[0] != '\0')
			{
				Compiler = Env;
			}
		}
		if (!Compiler)
		{
			if (CommandOnPath("c++"))
			{
				Compiler = "c++";
			}
			else if (CommandOnPath("g++"))
			{
				Compiler = "g++";
			}
			else if (CommandOnPath("clang++"))
			{
				Compiler = "clang++";
			}
		}
		if (!Compiler)
		{
			OutError = "native DualRun skipped (no MSVC vcvars / g++ / clang++)";
			return Finish(ENativeDualResult::Skipped);
		}

		const fs::path ExePath = Dir / "aot_run";
		std::ostringstream Cmd;
		Cmd << '"' << Compiler << "\" -std=c++17 -O0 -o \"" << ExePath.string() << "\" \""
		    << CppPath.string() << "\"";
		if (std::system(Cmd.str().c_str()) != 0)
		{
			OutError = "native: g++/clang++ compile failed";
			return Finish(ENativeDualResult::Failed);
		}
		std::ostringstream RunCmd;
		#if defined(_WIN32)
		RunCmd << '"' << ExePath.string() << ".exe\" > \"" << OutPath.string() << '"';
		#else
		RunCmd << '"' << ExePath.string() << "\" > \"" << OutPath.string() << '"';
		#endif
		if (std::system(RunCmd.str().c_str()) != 0)
		{
			OutError = "native: run failed";
			return Finish(ENativeDualResult::Failed);
		}
		std::string NativeOut;
		if (!ReadTextFile(OutPath, NativeOut))
		{
			OutError = "native: missing stdout capture";
			return Finish(ENativeDualResult::Failed);
		}
		NativeOut = NormalizeNewlines(std::move(NativeOut));
		if (NativeOut != ExpectedOutput)
		{
			OutError = "native=[" + NativeOut + "] expected=[" + ExpectedOutput + "]";
			return Finish(ENativeDualResult::Failed);
		}
		return Finish(ENativeDualResult::Matched);
	}

	bool CoreRuntimeRun(const std::string& Source, const std::string& Expected, const char* Label,
	    const std::vector<std::string>& Paths,
	    const char* Reason = "standard-library foundation operations require",
	    const char* ExpectedVmFailure = nullptr)
	{
		std::string Cpp, Error, Output;
		if (std::getenv("VEROSCRIPT_CPP_AOT_TEST_TRACE"))
		{
			std::cerr << "Core runtime: " << Label << std::endl;
		}
		if (!Expect(!VeroScript::EmitCppFromSource(Source, "core-runtime.vs", Cpp, Error, Paths) && Cpp.empty() && Error.find(Reason) != std::string::npos, Label))
		{
			std::cerr << Error << '\n';
			return false;
		}
		const bool bRan = RunVm(Source, Output, &Error, Paths);
		const bool bExpected = ExpectedVmFailure
		    ? !bRan && Error.find(ExpectedVmFailure) != std::string::npos : bRan;
		if (!Expect(bExpected && Output == Expected, Label))
		{
			std::cerr << Error << "\nvm=[" << Output << "]\n";
			return false;
		}
		return true;
	}

	bool DualRun(
	    const std::string& Source,
	    const std::string& ExpectedVmOutput,
	    const char* Label,
	    std::initializer_list<const char*> RequiredCppSnippets,
	    const std::vector<std::string>& ImportPaths = {},
	    std::initializer_list<const char*> ForbiddenCppSnippets = {},
	    const char* ExpectedNativeOutput = nullptr,
	    const char* ExpectedVmFailure = nullptr,
	    const VeroScript::FAnnotationProcessingOptions* Processing = nullptr,
	    const bool bNativeConsoleHost = false,
	    const VeroScript::FCompileProcessingOptions* CompileProcessing = nullptr,
	    const VeroScript::FCodegenOptions* AdditionalOptions = nullptr)
	{
		std::string Cpp;
		std::string Error;
		bool Emitted = false;
		if (std::getenv("VEROSCRIPT_CPP_AOT_TEST_TRACE"))
		{
			std::cerr << "Dual run: " << Label << std::endl;
		}
		if (Processing || CompileProcessing || AdditionalOptions)
		{
			VeroScript::FCodegenOptions Options = AdditionalOptions ? *AdditionalOptions : VeroScript::FCodegenOptions{};
			Options.ImportPaths.assign(ImportPaths.begin(), ImportPaths.end());
			if (Processing)
			{
				Options.AnnotationProcessing = *Processing;
			}

			if (CompileProcessing)
			{
				Options.CompileProcessing = *CompileProcessing;
			}

			VeroScript::FCodegenResult Result;
			Emitted = VeroScript::FCppCodegenBackend{}.EmitFromSource(Source, "aot.vs", Options, Result);
			Cpp = std::move(Result.Output);
			Error = std::move(Result.Error);
		}
		else
		{
			Emitted = VeroScript::EmitCppFromSource(Source, "aot.vs", Cpp, Error, ImportPaths);
		}

		if (!Expect(Emitted, Label))
		{
			std::cerr << Error << '\n'
			          << Cpp;
			return false;
		}
		for (const char* Snippet : RequiredCppSnippets)
		{
			if (!Expect(Cpp.find(Snippet) != std::string::npos, Label))
			{
				std::cerr << "missing snippet: " << Snippet << '\n'
				          << Cpp;
				return false;
			}
		}
		for (const char* Snippet : ForbiddenCppSnippets)
		{
			if (!Expect(Cpp.find(Snippet) == std::string::npos, Label))
			{
				std::cerr << "unexpected snippet: " << Snippet << '\n'
				          << Cpp;
				return false;
			}
		}
		std::string VmOutput;
		bool bVmSucceeded = false;
		if (Processing || CompileProcessing || AdditionalOptions)
		{
			VeroScript::FCompileOptions Options;
			VeroScript::Test::Configure(Options);
			if (AdditionalOptions)
			{
				Options.CompileProcessing = AdditionalOptions->CompileProcessing;
				Options.ScriptCompileExtensions = AdditionalOptions->ScriptCompileExtensions;
				Options.SourceOverlays = AdditionalOptions->SourceOverlays;
				Options.StdlibSearchPaths.assign(AdditionalOptions->StdlibSearchPaths.begin(), AdditionalOptions->StdlibSearchPaths.end());
			}
			Options.ImportPaths.assign(ImportPaths.begin(), ImportPaths.end());
			if (Processing)
			{
				Options.AnnotationProcessing = *Processing;
			}

			if (CompileProcessing)
			{
				Options.CompileProcessing = *CompileProcessing;
			}

			auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "aot.vs", Options);
			if (Compiled.bSuccess)
			{
				std::ostringstream Output;
				VeroScript::VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				bVmSucceeded = Vm.Execute(*Compiled.Module, VeroScript::ERunMode::Script);
				VmOutput = Output.str();
				Error = Vm.GetError().Message;
			}
			else
			{
				Error = Compiled.Error.Message;
			}
		}
		else
		{
			bVmSucceeded = RunVm(Source, VmOutput, &Error, ImportPaths);
		}

		if (!Expect(ExpectedVmFailure ? !bVmSucceeded && Error.find(ExpectedVmFailure) != std::string::npos &&
		                VmOutput == ExpectedVmOutput
		                              : bVmSucceeded && VmOutput == ExpectedVmOutput,
		        Label))
		{
			std::cerr << Error << '\n'
			          << "vm=[" << VmOutput << "]\n";
			return false;
		}

		std::string NativeError;
		// Convert the generated entry's expected runtime failure into a successful
		// test process. Compilation failure, wrapping or a crash still fail the gate.
		std::string NativeCpp = Cpp;
		if (ExpectedVmFailure)
		{
			NativeCpp = "#define main vs_test_entry\n" + Cpp + "\n#undef main\n";
			NativeCpp += "template<class F> int TestInvokeEntry(F Entry) {\n"
			             "    if constexpr (std::is_invocable_v<F>) return Entry();\n"
			             "    else return Entry(0, nullptr);\n}\n";
			NativeCpp += bNativeConsoleHost ? R"CPP(
struct TestConsoleBuffer final : std::stringbuf, VsConsoleRuntime::IHost {
    VsConsoleRuntime::FInfo QueryConsole() override { return {true, true, true, true, 80, 25}; }
    bool ExecuteConsole(VsConsoleRuntime::EOperation Op, std::int32_t First, std::int32_t) override {
        if (Op == VsConsoleRuntime::EOperation::ResetStyle) ++Resets;
        if (Op == VsConsoleRuntime::EOperation::SetCursorVisible && First == 1) ++CursorRestores;
        return true;
    }
    int Resets = 0, CursorRestores = 0;
};
int main() {
    TestConsoleBuffer Buffer;
    auto* Previous = std::cout.rdbuf(&Buffer);
    const int Result = TestInvokeEntry(&vs_test_entry);
    std::cout.rdbuf(Previous);
    std::cout << Buffer.str();
    return Result == 1 && Buffer.Resets == 1 && Buffer.CursorRestores == 1 ? 0 : 2;
}
)CPP"
			                                : "int main() { return TestInvokeEntry(&vs_test_entry) == 1 ? 0 : 2; }\n";
		}
		const ENativeDualResult Native = RunNativeCpp(
		    NativeCpp, ExpectedNativeOutput ? ExpectedNativeOutput : ExpectedVmOutput, NativeError);
		if (Native == ENativeDualResult::Failed)
		{
			std::cerr << "FAIL: " << Label << " (native DualRun)\n"
			          << NativeError << '\n';
			return false;
		}
		if (Native == ENativeDualResult::Matched)
		{
			++gNativeDualMatched;
		}
		if (Native == ENativeDualResult::Skipped)
		{
			++gNativeDualSkipped;
			if (RequireNativeDualRun())
			{
				std::cerr << "FAIL: " << Label
				          << " (native DualRun required: set a C++ toolchain or "
				             "VEROSCRIPT_AOT_CXX / VEROSCRIPT_AOT_VCVARS)\n"
				          << NativeError << '\n';
				return false;
			}
			static bool bWarned = false;
			if (!bWarned)
			{
				std::cerr << "NOTE: " << NativeError
				          << "\n      Native DualRun soft-skipped; set VEROSCRIPT_AOT_REQUIRE_NATIVE=1 "
				             "or CI=1 to fail instead. "
				             "Allow empty-toolchain runs with VEROSCRIPT_AOT_ALLOW_NATIVE_SKIP=1 "
				             "only when zero natives match.\n";
				bWarned = true;
			}
		}
		return true;
	}

	bool TestConcurrentShutdownDescendants()
	{
		return DualRun(R"VS(
import "concurrent.vs";
void leaf(channel<int> ch) { std::this_thread::sleep(20); }
void middle(channel<int> ch) {
    std::this_thread::sleep(100);
    channel<int> child = channel<int>();
    std::concurrent::spawn("leaf", child);
}
void worker(channel<int> ch) {
    std::this_thread::sleep(100);
    channel<int> child = channel<int>();
    std::concurrent::spawn("middle", child);
}
void main() {
    channel<int> ch = channel<int>();
    std::concurrent::spawn("worker", ch);
    print("main");
}
)VS", "main\n", "shutdown joins tasks spawned by workers after main returns", {})
			&& DualRun(R"VS(
import "concurrent.vs";
void leaf(channel<int> ch) { ch.recv_for(10000); }
void worker(channel<int> ch) {
    ch.recv_for(10000);
    channel<int> child = channel<int>();
    if (!child.is_closed()) {
        print("late channel remained open");
        return;
    }
    int task = std::concurrent::spawn("leaf", child);
    std::concurrent::join_task(task);
}
void main() {
    channel<int> ch = channel<int>();
    std::concurrent::spawn("worker", ch);
    print("main");
}
)VS", "main\n", "shutdown closes new channels before workers join their children", {})
			&& DualRun(R"VS(
import "concurrent.vs";
bool launch = false;
void worker(channel<int> ch) { std::this_thread::sleep(20); }
class Finalizer {
public:
    Finalizer() {}
    ~Finalizer() {
        if (launch) {
            channel<int> ch = channel<int>();
            std::concurrent::spawn("worker", ch);
        }
    }
}
Finalizer finalizer = Finalizer();
void main() {
    launch = true;
    print("main");
}
)VS", "main\n", "shutdown joins tasks started by global destructors", {});
	}

	bool TestFileIoBoundaries()
	{
		const FScopedTempDirectory Root("VeroScriptCppAotFileIo");
		const auto DataPath = Root.GetPath() / "data.txt";
		const auto EmptyPath = Root.GetPath() / "empty.txt";
		const auto ExactPath = Root.GetPath() / "exact.txt";
		const auto DirectoryPath = Root.GetPath() / "directory";
		const auto NewDirectoryPath = Root.GetPath() / "new-directory";
		std::string Data(20003, 'x');
		Data[8191] = '\0';
		Data[8192] = '\n';
		Data[16384] = 'y';
		{
			std::ofstream File(DataPath, std::ios::binary);
			File.write(Data.data(), static_cast<std::streamsize>(Data.size()));
			File.close();
			if (!Expect(static_cast<bool>(File), "prepare file I/O data fixture")) return false;
		}
		{
			std::ofstream File(EmptyPath, std::ios::binary);
			File.close();
			if (!Expect(static_cast<bool>(File), "prepare empty file I/O fixture")) return false;
		}
		{
			std::ofstream File(ExactPath, std::ios::binary);
			File << std::string(8192, 'z');
			File.close();
			if (!Expect(static_cast<bool>(File), "prepare exact-chunk file I/O fixture")) return false;
		}
		if (!Expect(std::filesystem::create_directory(DirectoryPath), "prepare directory read fixture")) return false;
		const auto QuotePath = [](const std::filesystem::path& Path)
		{
			std::string Quoted = "\"";
			for (const char Character : VeroScript::PathUtil::ToUtf8(Path))
			{
				if (Character == '\\' || Character == '"')
				{
					Quoted += '\\';
				}
				Quoted += Character;
			}
			return Quoted + '"';
		};
		#if defined(_WIN32)
		std::string LongDirectory = VeroScript::PathUtil::ToUtf8(Root.GetPath());
		while (std::filesystem::u8path(LongDirectory).native().size() < 300)
		{
			LongDirectory += "/long-path-segment";
		}
		LongDirectory += "/unicode-\xe4\xb8\xad\xe6\x96\x87-\xf0\x9f\x99\x82";
		const std::string LongFile = LongDirectory + "/data.txt";
		std::filesystem::create_directories(VeroScript::PathUtil::FromUtf8(LongDirectory));
		const std::string QuotedLongFile = QuotePath(std::filesystem::u8path(LongFile));
		#endif
		const auto CheckVm = [](const std::string& Body, const bool ExpectedSuccess)
		{
			struct FRestoreWorkingDirectory
			{
				std::filesystem::path Original = std::filesystem::current_path();
				~FRestoreWorkingDirectory()
				{
					std::error_code Error;
					std::filesystem::current_path(Original, Error);
				}
			} RestoreWorkingDirectory;
			VeroScript::FCompileOptions Options;
			VeroScript::Test::Configure(Options);
			Options.ImportPaths = {VEROSCRIPT_STDLIB_DIR};
			auto Compiled = VeroScript::VeroScriptCompile::CompileSource(
			    "import \"io.vs\";import \"string.vs\";void main(){" + Body + "}", "file-io-boundaries.vs", Options);
			if (!Expect(Compiled.bSuccess && Compiled.Module != nullptr, "compile file I/O boundary probe"))
			{
				std::cerr << Compiled.Error.Message << '\n';
				return false;
			}
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			const bool Success = Vm.Execute(*Compiled.Module, VeroScript::ERunMode::Script);
			if (!Expect(Success == ExpectedSuccess && (Success || !Vm.GetError().Message.empty()),
			        "VM file I/O boundary result"))
			{
				std::cerr << Body << '\n'
				          << Vm.GetError().Message << '\n';
				return false;
			}
			return true;
		};
		const std::string BadFile = QuotePath(DataPath) + "+\"\\0suffix\"";
		const std::string BadDirectory = QuotePath(DirectoryPath) + "+\"\\0suffix\"";
		const std::string BadNewDirectory = QuotePath(NewDirectoryPath) + "+\"\\0suffix\"";
		if (!CheckVm("string text=std::io::read_file(" + QuotePath(DataPath) + ");assert(len(text)==20003);assert(std::strings::char_code(text,8191)==0);assert(std::strings::char_code(text,8192)==10);"
		                                                                       "assert(std::strings::char_code(text,16384)==121);assert(len(std::io::read_file(" +
		            QuotePath(EmptyPath) + "))==0);assert(len(std::io::read_file(" + QuotePath(ExactPath) + "))==8192);",
		        true) ||
		    !CheckVm("std::io::read_file(" + QuotePath(DirectoryPath) + ");", false) || !CheckVm("std::io::read_file(" + BadFile + ");", false) || !CheckVm("std::io::write_file(" + BadFile + ",\"changed\");", false) || !CheckVm("std::io::append_file(" + BadFile + ",\"changed\");", false))
		{
			return false;
		}
		if (!CheckVm("std::filesystem::delete_file(" + BadFile + ");", false))
		{
			std::cerr << "embedded-NUL delete probe preserved file: " << std::filesystem::exists(DataPath) << '\n';
			return false;
		}
		#if defined(_WIN32)
		if (!CheckVm("std::io::write_file(" + QuotedLongFile + ",\"long\");"
		        "std::io::append_file(" + QuotedLongFile + ",\"-path\");"
		        "assert(std::io::read_file(" + QuotedLongFile + ")==\"long-path\");"
		        "assert(std::filesystem::is_file(" + QuotedLongFile + "));"
		        "assert(std::filesystem::delete_file(" + QuotedLongFile + "));", true))
		{
			return false;
		}
		#endif
		if (!CheckVm("std::filesystem::remove_dir(" + BadDirectory + ");", false) || !CheckVm("std::filesystem::exists(" + BadFile + ");", false) || !CheckVm("std::filesystem::is_file(" + BadFile + ");", false) || !CheckVm("std::filesystem::is_directory(" + BadDirectory + ");", false) || !CheckVm("std::filesystem::list_dir(" + BadDirectory + ");", false) || !CheckVm("std::filesystem::mkdir(" + BadNewDirectory + ");", false) || !CheckVm("std::filesystem::chdir(" + BadDirectory + ");", false))
		{
			return false;
		}
		std::string Current;
		if (!Expect(ReadTextFile(DataPath, Current) && Current == Data, "invalid VM paths preserve the original file")) return false;
		if (!Expect(std::filesystem::is_directory(DirectoryPath) && !std::filesystem::exists(NewDirectoryPath),
		        "invalid VM paths preserve directories"))
		{
			return false;
		}

		std::string Cpp, Error;
		// Emit every filesystem helper once, then probe them in the same native
		// process. These are the helpers called by the generated script functions.
		if (!Expect(VeroScript::EmitCppFromSource(R"VS(import "io.vs";
void file_probes(string path){
    std::io::read_file(path);std::io::write_file(path,"changed");std::io::append_file(path,"changed");
    std::filesystem::exists(path);std::filesystem::is_file(path);std::filesystem::is_directory(path);std::filesystem::remove_dir(path);
    std::filesystem::delete_file(path);std::filesystem::mkdir(path);std::filesystem::chdir(path);std::filesystem::list_dir(path);
}
void main(){}
)VS",
		                "file-io-boundaries.vs", Cpp, Error, {VEROSCRIPT_STDLIB_DIR}),
		        "emit file I/O boundary helpers"))
		{
			std::cerr << Error << '\n';
			return false;
		}
		Cpp = "#define main vs_file_test_entry\n" + Cpp + "\n#undef main\n";
		Cpp += "int main(){\nconst std::string File=" + QuotePath(DataPath) + ";\nconst std::string Directory=" + QuotePath(DirectoryPath) + ";\nconst std::string BadFile=File+std::string(1,'\\0')+\"suffix\";\n"
		                                                                                                                                     "const std::string BadDirectory=Directory+std::string(1,'\\0')+\"suffix\";\n"
		                                                                                                                                     "const auto Rejects=[](auto&& Call){try{Call();return false;}catch(const VsExitException& E){return E.Code==1;}};\n"
		                                                                                                                                     "const auto Text=VsReadFile(File);\n"
		                                                                                                                                     "if(Text.size()!=20003 || Text[8191]!='\\0' || Text[8192]!='\\n' || Text[16384]!='y'\n"
		                                                                                                                                     " || !VsReadFile(" +
		    QuotePath(EmptyPath) + ").empty() || VsReadFile(" + QuotePath(ExactPath) + ")!=std::string(8192,'z'))return 2;\n"
		                                                                               "if(!Rejects([&]{(void)VsReadFile(Directory);})\n"
		                                                                               " || !Rejects([&]{(void)VsReadFile(BadFile);})\n"
		                                                                               " || !Rejects([&]{VsWriteFile(BadFile,\"changed\");})\n"
		                                                                               " || !Rejects([&]{VsAppendFile(BadFile,\"changed\");})\n"
		                                                                               " || !Rejects([&]{(void)VsExists(BadFile);})\n"
		                                                                               " || !Rejects([&]{(void)VsIsFile(BadFile);})\n"
		                                                                               " || !Rejects([&]{(void)VsIsDirectory(BadDirectory);})\n"
		                                                                               " || !Rejects([&]{(void)VsRemoveDir(BadDirectory);})\n"
		                                                                               " || !Rejects([&]{(void)VsDeleteFile(BadFile);})\n"
		                                                                               " || !Rejects([&]{VsMkDir(BadDirectory);})\n"
		                                                                               " || !Rejects([&]{VsChdir(BadDirectory);})\n"
		                                                                               " || !Rejects([&]{(void)VsListDir(BadDirectory);}))return 3;\n"
		                                                                               "if(VsReadFile(File)!=Text || !VsIsDirectory(Directory))return 4;\n";
		#if defined(_WIN32)
		Cpp += "const std::string LongFile=" + QuotedLongFile + ";\n";
		Cpp += R"VSCPP(
VsWriteFile(LongFile,"native"); VsAppendFile(LongFile,"-path");
if(VsReadFile(LongFile)!="native-path" || !VsIsFile(LongFile) || !VsDeleteFile(LongFile))return 5;
if(VsPathFromUtf8("short.txt").native()!=L"short.txt")return 6;
const std::string Relative=std::string("folder/")+std::string(280,'x');
auto RelativeExpected=std::filesystem::u8path(Relative); RelativeExpected.make_preferred();
if(VsPathFromUtf8(Relative)!=RelativeExpected)return 7;
const std::string Unc=std::string("\\\\server\\share\\")+std::string(280,'x');
if(VsPathFromUtf8(Unc).native()!=L"\\\\?\\UNC\\server\\share\\"+std::wstring(280,L'x'))return 8;
const std::string Extended=std::string("\\\\?\\C:\\")+std::string(280,'x');
if(VsPathFromUtf8(Extended).native()!=std::filesystem::u8path(Extended).native())return 9;
)VSCPP";
		// Compare generated public spelling with Core for both ordinary aliases
		// and verbatim-only names; blindly stripping every prefix changes objects.
		for (const std::string Suffix : {
		    "C:/", "C:/ordinary/\xe4\xb8\xad\xe6\x96\x87/file.txt", "UNC/server/share/folder/file.txt",
		    "unc/server/share/file.txt", "C:/folder./file", "C:/folder /file", "C:/NUL.txt", "C:/conin$",
		    "C:/COM0", "C:/lpt\xc2\xb9.txt", "C:/normal:stream", "UNC/server./share/file",
		    "UNC/server/share./file", "GLOBALROOT/Device/HarddiskVolume1/file"})
		{
			Cpp += "if(VsPathToUtf8(std::filesystem::u8path(std::string(\"//?/\")+" +
			    QuotePath(std::filesystem::u8path(Suffix)) + "))!=" +
			    QuotePath(std::filesystem::u8path("//?/" + Suffix)) + ")return 10;\n";
		}
		Cpp += "const auto SavedDirectory=std::filesystem::current_path();\n"
		    "VsChdir(Directory);const auto PublicCwd=VsCwd();\n"
		    "std::filesystem::current_path(SavedDirectory);"
		    "if(!std::filesystem::equivalent(VsPathFromUtf8(PublicCwd),VsPathFromUtf8(Directory)))return 11;\n";
		#endif
		Cpp += "std::cout<<\"file I/O boundaries passed\\n\";return 0;\n}\n";
		const auto Native = RunNativeCpp(Cpp, "file I/O boundaries passed\n", Error);
		if (!Expect(Native != ENativeDualResult::Failed && (Native != ENativeDualResult::Skipped || !RequireNativeDualRun()), "native file I/O boundary results"))
		{
			std::cerr << Error << '\n';
			return false;
		}
		if (Native == ENativeDualResult::Matched)
		{
			++gNativeDualMatched;
		}
		else
		{
			++gNativeDualSkipped;
		}
		return true;
	}

	bool TestConsoleEntryBoundaries()
	{
		int Matched = 0, Skipped = 0;
		for (const bool bArgs : {false, true})
		{
			for (const int Failure : {0, 1, 2})
			{
				// 0: backing stream callback fails during guard construction.
				// 1/2: diagnostics fail while reporting standard/nonstandard exceptions.
				const std::string Source = std::string("void main(") + (bArgs ? "string[] args" : "") +
				    "){std::console::set_color(\"green\",\"default\");"
				    "std::console::set_cursor_visible(false);print(\"marker\");" +
				    (Failure == 2 ? "std::console::supports_color();}"
				                  : "std::console::set_color(\"invalid\",\"default\");}");
				std::string Cpp, Error;
				if (!Expect(VeroScript::EmitCppFromSource(Source, "console-boundary.vs", Cpp, Error),
				        "emit console exception boundary"))
				{
					std::cerr << Error << '\n';
					return false;
				}
				Cpp = "#define main vs_test_entry\n" + Cpp + "\n#undef main\n";
				Cpp += "static constexpr int Failure = " + std::to_string(Failure) + ";\n";
				Cpp += R"CPP(
struct TestConsoleBuffer final : std::stringbuf, VsConsoleRuntime::IHost {
    VsConsoleRuntime::FInfo QueryConsole() override {
        if (Failure == 2 && Hidden) throw 7;
        return {true, true, true, true, 80, 25};
    }
    std::ostream* ConsoleOutputStream() override {
        if (Failure == 0) throw std::runtime_error("backing unavailable");
        return nullptr;
    }
    bool ExecuteConsole(VsConsoleRuntime::EOperation Op, std::int32_t First, std::int32_t) override {
        if (Op == VsConsoleRuntime::EOperation::ResetStyle) ++Resets;
        if (Op == VsConsoleRuntime::EOperation::SetCursorVisible) {
            Hidden = First == 0;
            if (First == 1) ++CursorRestores;
        }
        return true;
    }
    int Resets = 0, CursorRestores = 0;
    bool Hidden = false;
};
struct TestErrorBuffer final : std::streambuf {
    std::streamsize xsputn(const char*, std::streamsize) override {
        throw std::runtime_error("error sink unavailable");
    }
    int_type overflow(int_type) override { throw std::runtime_error("error sink unavailable"); }
};
template<class F> int TestInvokeEntry(F Entry) {
    if constexpr (std::is_invocable_v<F>) return Entry();
    else return Entry(0, nullptr);
}
int main() {
    TestConsoleBuffer Output;
    TestErrorBuffer Errors;
    auto* PreviousOutput = std::cout.rdbuf(&Output);
    auto* PreviousErrors = std::cerr.rdbuf(&Errors);
    const auto PreviousMask = std::cerr.exceptions();
    if (Failure != 0) std::cerr.exceptions(std::ios::badbit | std::ios::failbit);
    int Result = -1;
    try { Result = TestInvokeEntry(&vs_test_entry); } catch (...) { Result = -2; }
    std::cerr.exceptions(std::ios::goodbit);
    std::cerr.clear();
    std::cerr.rdbuf(PreviousErrors);
    std::cerr.exceptions(PreviousMask);
    std::cout.rdbuf(PreviousOutput);
    std::cout << Output.str();
    const bool Restored = Failure == 0 ? Output.Resets == 0 && Output.CursorRestores == 0
        : Output.Resets == 1 && Output.CursorRestores == 1 && !Output.Hidden;
    return Result == 1 && Restored ? 0 : 2;
}
)CPP";
				const ENativeDualResult Native = RunNativeCpp(Cpp, Failure == 0 ? "" : "marker\n", Error);
				if (!Expect(Native != ENativeDualResult::Failed &&
				            (Native != ENativeDualResult::Skipped || !RequireNativeDualRun()),
				        "console entry contains adapter and diagnostic exceptions"))
				{
					std::cerr << "args=" << bArgs << " failure=" << Failure << '\n'
					          << Error << '\n';
					return false;
				}
				if (Native == ENativeDualResult::Matched)
				{
					++Matched;
				}

				else
				{
					++Skipped;
				}
			}
		}

		std::cout << "Console entry boundaries matched=" << Matched << " skipped=" << Skipped << '\n';
		return true;
	}

	bool TestConsoleWorkerBoundaries()
	{
		int Matched = 0, Skipped = 0;
		for (const int Failure : {0, 1, 2})
		{
			const std::string Source = "import \"concurrent.vs\";"
			                           "void worker(channel<int> unused){std::console::set_color(\"green\",\"default\");"
			                           "std::console::set_cursor_visible(false);}"
			                           "void main(){channel<int> ch=channel<int>();int id=std::concurrent::spawn(\"worker\",ch);"
			                           "std::concurrent::join_task(id);print(\"joined\");}";
			std::string Cpp, Error;
			if (!Expect(VeroScript::EmitCppFromSource(Source, "console-worker.vs", Cpp, Error),
			        "emit console worker exception boundary"))
			{
				std::cerr << Error << '\n';
				return false;
			}
			Cpp = "#define main vs_test_entry\n" + Cpp + "\n#undef main\n";
			Cpp += "static constexpr int Failure = " + std::to_string(Failure) + ";\n";
			Cpp += R"CPP(
#include <atomic>
struct TestConsoleBuffer final : std::stringbuf, VsConsoleRuntime::IHost {
    const std::thread::id Owner = std::this_thread::get_id();
    std::atomic<int> Attempts{0}, Resets{0}, CursorRestores{0};
    std::atomic<bool> CleanupBeforeDone{true};
    VsConsoleRuntime::FInfo QueryConsole() override { return {true, true, true, true, 80, 25}; }
    std::ostream* ConsoleOutputStream() override {
        if (std::this_thread::get_id() != Owner && Failure != 2) {
            ++Attempts;
            if (Failure == 0) throw std::runtime_error("worker backing unavailable");
            throw 7;
        }
        return nullptr;
    }
    bool ExecuteConsole(VsConsoleRuntime::EOperation Op, std::int32_t First, std::int32_t) override {
        const bool Restore = Op == VsConsoleRuntime::EOperation::ResetStyle
            || (Op == VsConsoleRuntime::EOperation::SetCursorVisible && First == 1);
        if (Restore) {
            auto* State = VsCurrentTaskState;
            if (!State) CleanupBeforeDone = false;
            else {
                std::lock_guard<std::mutex> Lock(State->Mu);
                if (State->Done) CleanupBeforeDone = false;
            }
            if (Op == VsConsoleRuntime::EOperation::ResetStyle) ++Resets;
            else ++CursorRestores;
        }
        return true;
    }
};
int main() {
    // A regression must fail the test process without invoking crash dialogs.
    std::set_terminate([] { std::_Exit(77); });
    TestConsoleBuffer Output;
    auto* Previous = std::cout.rdbuf(&Output);
    const int Result = vs_test_entry();
    std::cout.rdbuf(Previous);
    std::cout << Output.str();
    if (Failure != 2) return Result == 1 && Output.Attempts == 1 ? 0 : 2;
    return Result == 0 && Output.Resets == 1 && Output.CursorRestores == 1
        && Output.CleanupBeforeDone ? 0 : 2;
}
)CPP";
			const ENativeDualResult Native = RunNativeCpp(Cpp, Failure == 2 ? "joined\n" : "", Error);
			if (!Expect(Native != ENativeDualResult::Failed && (Native != ENativeDualResult::Skipped || !RequireNativeDualRun()),
			        "worker initialization is contained and cleanup precedes completion"))
			{
				std::cerr << "failure=" << Failure << '\n'
				          << Error << '\n';
				return false;
			}
			if (Native == ENativeDualResult::Matched)
			{
				++Matched;
			}

			else
			{
				++Skipped;
			}
		}
		std::cout << "Console worker boundaries matched=" << Matched << " skipped=" << Skipped << '\n';
		return true;
	}

	bool TestEntryArgumentStorage()
	{
		for (const bool bNegative : {false, true})
		{
			const std::string Source = bNegative
			    ? "string right(){print(\"right\");return \"changed\";}void main(string[] args){args[-1]=right();}"
			    : "string right(){return \"changed\";}"
			      "void main(string[] args){args[0]=right();print(args[0]);print(args[1]);}";
			std::string Cpp, Error;
			if (!VeroScript::EmitCppFromSource(Source, "args-reference.vs", Cpp, Error))
			{
				std::cerr << "entry argument storage: " << Error << '\n';
				return false;
			}
			// Supply actual application arguments while preserving the generated entry.
			const std::string Wrapped = "#define main vs_test_entry\n" + Cpp + "\n#undef main\n"
			    "int main(){char app[]=\"test\";char value[]=\"hello\";char other[]=\"world\";char* args[]={app,value,other,nullptr};return vs_test_entry(3,args)=="
			    + (bNegative ? "1" : "0") + "?0:2;}\n";
			const auto Result = RunNativeCpp(Wrapped, bNegative ? "right\n" : "changed\nworld\n", Error);
			if (Result == ENativeDualResult::Failed || (Result == ENativeDualResult::Skipped && RequireNativeDualRun()))
			{
				std::cerr << "entry argument storage: " << Error << '\n';
				return false;
			}
			if (Result == ENativeDualResult::Matched)
			{
				++gNativeDualMatched;
			}
			else
			{
				++gNativeDualSkipped;
			}
		}
		return true;
	}

	bool TestAssignmentSequencing()
	{
		if (!TestEntryArgumentStorage()) return false;
		if (!DualRun("int right(){print(\"right\");return 2;}void main(){int values[1];values[-1]=right();}",
		        "right\n", "negative fixed array write validates after the right operand", {}, {}, {},
		        nullptr, "invalid ref binding") ||
		    !DualRun("int right(){print(\"right\");return 2;}void main(){int values[1];values[-1]+=right();}",
		        "", "negative fixed array compound write validates its read first", {}, {}, {},
		        nullptr, "invalid ref binding"))
		{
			return false;
		}
		if (!DualRun(R"VS(
int values[2]={7,8};int change(){values[1]+=1;return 2;}
void main(){print(values[0]=change());print(values[1]);print(values[0]+=change());print(values[1]);}
)VS", "2\n9\n4\n10\n", "fixed array writes retain right operand mutations", {}))
		{
			return false;
		}
		if (!DualRun("void main(){int values[3]={1,2,3};values[0]=7;values[0]+=2;print(values[0]);}", "9\n",
		        "scalar indexed updates do not copy the native array", {}, {}, {"= (values);"}))
		{
			return false;
		}

		if (!DualRun(R"VS(
array<int> values={7};array<int> old=values;array<int> fresh=array<int>();
int replace(){values=array<int>();values.push(20);fresh=values;return 2;}
void main(){print(values[0]=replace());print(values[0]);print(old[0]);print(fresh[0]);
 values=array<int>();values.push(7);old=values;
 print(values[0]+=replace());print(values[0]);print(old[0]);print(fresh[0]);}
)VS",
		        "2\n2\n2\n20\n9\n9\n9\n20\n",
		        "indexed assignment writes its saved container back after RHS rebinding", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class C{public:array<int> values={7};}
C item=C();C old_item=item;array<int> old=item.values;array<int> fresh=array<int>();
int replace(){item.values=array<int>();item.values.push(20);fresh=item.values;item=C();return 2;}
void main(){print(item.values[0]=replace());print(item.values[0]);
 print(old_item.values[0]);print(old[0]);print(fresh[0]);}
)VS",
		        "2\n7\n2\n2\n20\n", "indexed field writeback retains the original object", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
array<array<int>> values={{7}};array<array<int>> old=values;
int replace(){values=array<array<int>>();array<int> row={20};values.push(row);return 2;}
class C{public:static array<int> values={7};}
namespace N{array<int> values={7};}
int replace_static(){C.values=array<int>();C.values.push(20);return 2;}
int replace_namespace(){N.values=array<int>();N.values.push(20);return 2;}
void main(){print(values[0][0]=replace());print(values[0][0]);print(old[0][0]);
 print(C.values[0]=replace_static());print(C.values[0]);
 print(N.values[0]=replace_namespace());print(N.values[0]);}
)VS",
		        "2\n2\n7\n2\n2\n2\n2\n",
		        "nested and qualified index assignments resolve the current storage path", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class C{public:string text="before";int number=7;}
C item=C();string replace_text(){item=C();return "after";}
int replace_number(){item=C();return 9;}
void main(){print(item.text=replace_text());print(item.text);
 print(item.number=replace_number());print(item.number);}
)VS",
		        "after\nbefore\n9\n7\n", "assignment result outlives its saved receiver", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
array<int> values={7};int right(){print("right");return 2;}
void main(){values[-1]=right();}
)VS",
		        "right\n", "plain indexed assignment validates bounds after RHS side effects", {}, {}, {},
		        nullptr, "index"))
		{
			return false;
		}

		return true;
	}

	bool TestReferenceValueSequencing()
	{
		if (!DualRun(R"VS(
class C{public:string text="before";string& borrow(int ignored){return text;}}
C item=C();int replace(){item=C();return 1;}
void show(string text,int ignored){print(text);}
T echo<T>(T value,int ignored){return value;}
void main(){print(item.borrow(replace()));string copy=item.borrow(replace());print(copy);
 print(len(item.borrow(replace())));print(item.borrow(replace())+"!");show(item.borrow(replace()),replace());
 print(echo(item.borrow(replace()),0));}
)VS",
		        "before\nbefore\n6\nbefore!\nbefore\nbefore\n",
		        "reference results read as values are copied before their receiver is destroyed", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
string text="before";string& borrow(int a,int b){return text;}
struct S{public:int n=3;int& borrow(int ignored){return n;}}
void main(){string copy=borrow(1,2);borrow(1,2)="after";print(copy);print(text);
 S item=S();int& alias=item.borrow(1);alias=9;print(item.n);}
)VS",
		        "before\nafter\n9\n", "sequencing preserves references to global and direct struct storage",
		        {}))
		{
			return false;
		}

		return true;
	}

	bool TestReferenceOwnersAndLayouts()
	{
		if (!DualRun(R"VS(
class C{public:int id;string text="before";C(int n){id=n;}string& borrow(){return text;}}
C item=C(1);void main(){{string& alias=item.borrow();item=C(2);print(alias);}print("scope");
 {string& temporary=C(3).borrow();print(temporary);}}
)VS",
		        "before\nscope\nbefore\n",
		        "field reference aliases retain receiver storage until their scope ends", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class C{public:string text="before";string& borrow(int ignored=0){return text;}}
C item=C();int replace(){item=C();return 1;}
void main(){string& alias=item.borrow();item=C();print(alias);string& local=item.borrow(replace());item=C();print(local);}
)VS",
		        "before\nbefore\n", "global receiver and parameterized field aliases retain storage", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class C{public:string text="before";string read(){item=C();return text;}}
C item=C();void main(){print(item.read());}
)VS",
		        "before\n", "zero argument call keeps its receiver alive during its body", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
int calls=0;class C{public:string text="before";string& borrow(){calls++;return text;}}
C item=C();string change(){item=C();return "suffix";}
void main(){print(item.borrow()+=change());print(item.text);print(calls);}
)VS",
		        "beforesuffix\nbefore\n1\n",
		        "reference assignment invokes its target once and retains the original receiver", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
interface I{virtual int read(){return state.n;}}
struct S{public:int n=7;}S state=S();
class C:I{public:static int value=7;int own(){return state.n;}}
class Box<T>{public:int read(){return state.n;}T value;Box(T input){value=input;state.n=8;}}
void main(){C c=C();print(c.read());print(c.own());print(C.value);
 Box<int> b=Box<int>(3);print(b.read());}
)VS",
		        "7\n7\n7\n8\n", "member and open template bodies see complete global layouts", {}))
		{
			return false;
		}

		for (const auto& Case : std::vector<std::pair<std::string, const char*>>{
		         {"int n=2147483647;int& get(){return n;}void main(){print(get()+1);}",
		             "reference arithmetic overflow"},
		         {"int n=2147483647;int& get(){return n;}void main(){get()+=1;}",
		             "reference compound overflow"},
		         {"int n=32768;int& get(){return n;}void main(){short value=get();print(value);}",
		             "reference narrowing overflow"},
		         {"int n=-2147483647-1;int& get(){return n;}void main(){print(-get());}",
		             "reference unary overflow"}})
		{
			if (!DualRun(Case.first, "", Case.second, {}, {}, {}, nullptr, "overflow")) return false;
		}

		if (!DualRun("double n=1.5;double& get(){return n;}void main(){print(get()+2);print(-get());}",
		        "3.5\n-1.5\n", "reference floating arithmetic preserves its width", {}))
		{
			return false;
		}

		if (!DualRun(
		        "class C{public:int n=7;int& get(){return n;}}void main(){C c=C();print(c.get()++);print(++c.get());}",
		        "7\n9\n", "reference method increment keeps storage alive and invokes its target once", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
interface I{virtual int read()=0;}class C:I{public:int read(){return 7;}}
class Box<T> where T:I{public:static int n=3;T value;int read(){return value.read();}}
void main(){Box<C> b=Box<C>();b.value=C();print(b.read());print(Box<C>.n);}
)VS",
		        "7\n3\n", "deferred constrained template member and static field definitions", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class Cell{public:string text="before";string& borrow(){return text;}}
class D{public:~D(){string& alias=Cell().borrow();print(alias);}}
string& echo(string& alias){return alias;}
void main(){D d=D();string word="word";print(echo(ref word));}
)VS",
		        "word\nbefore\n", "destructor aliases do not contaminate other function scopes", {}))
		{
			return false;
		}

		return true;
	}

	bool TestDirectFieldReferences()
	{
		if (!DualRun(R"VS(
class C{public:string text="before";}C item=C();
void main(){string& alias=item.text;item=C();print(alias);string& temporary=C().text;print(temporary);}
)VS",
		        "before\nbefore\n", "direct field aliases retain rebound and temporary receivers", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class C{public:string text="before";}
void main(){C item=C();string& alias=item.text;string& second=alias;
 item=C();alias+="!";print(second);print(item.text);}
)VS",
		        "before!\nbefore\n", "direct local field aliases share the original storage", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
struct S{public:string text="before";string& borrow(){return text;}}class C{public:S state;}C item=C();
void main(){string& alias=item.state.text;item=C();print(alias);
 string& method=item.state.borrow();item=C();print(method);}
)VS",
		        "before\nbefore\n", "nested struct fields and methods retain their class owner", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class C{public:string text="before";string& borrow(){return this.text;}
 string read(){string& alias=this.text;item=C();return alias;}
 string read_method(){string& alias=this.borrow();item=C();return alias;}
 fn()->string callback(){return fn()->string{string& alias=this.text;item=C();return alias;};}}
C item=C();void main(){print(item.read());print(item.read_method());fn()->string f=item.callback();print(f());}
)VS",
		        "before\nbefore\nbefore\n", "member and captured receivers retain field storage", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class Box<T>{public:T value;}
void main(){Box<string> item=Box<string>();item.value="before";
 string& alias=item.value;item=Box<string>();print(alias);}
)VS",
		        "before\n", "generic class field aliases retain receiver storage", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
struct S{public:int n=3;S(){n=7;}}
class C{public:S state;S slots[2];}class Box<T>{public:T value;}
void main(){C c=C();Box<S> b=Box<S>();S explicit_value=S();
 print(c.state.n);print(b.value.n);print(c.slots[1].n);print(explicit_value.n);}
)VS",
		        "7\n7\n7\n7\n",
		        "default struct fields and array elements execute their default constructor", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
int defaults=0;
struct S{int n;S(){defaults+=1;n=7;}S(int value){n=value;}}
struct Only{int n;Only(int value){n=value;}}
class C{public:S state;S slots[2];S initialized;Only nondefault;
 C():initialized(11),nondefault(13){}}
class Box<T>{public:T value;}
void main(){C c=C();Box<S> b=Box<S>();print(defaults);print(c.initialized.n);
 print(c.nondefault.n);print(c.state.n);print(c.slots[1].n);print(b.value.n);}
)VS",
		        "4\n11\n13\n7\n7\n7\n",
		        "explicit field initialization skips default constructor effects", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
int seed=40;
namespace N{int seed=9;using Number=int;
struct S<T>{T value=seed;Number alias=seed;T samples[2]={seed,seed+1};
 map<int,T> entries={seed:seed+2};T selected;
 S(T seed):selected(seed){selected+=seed;}};}
void main(){N::S<int> value=N::S<int>(3);
 print("{} {} {} {} {}",value.value,value.alias,value.samples[1],value.entries[9],value.selected);}
)VS",
		        "9 9 10 11 6\n",
		        "declaration field initializers cannot capture constructor parameters", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
class C{public:string text="before";string& borrow(){return text;}}C item=C();
void main(){for(string& alias=item.text;true;){item=C();print(alias);break;}
 for(string& first=item.borrow(),second=item.text;true;){item=C();print(first);print(second);break;}}
)VS",
		        "before\nbefore\nbefore\n",
		        "for initializer reference aliases retain their receivers for the loop scope", {}))
		{
			return false;
		}

		return true;
	}

	bool TestConstructorIncompleteCopy()
	{
		return DualRun(R"VS(
int destroyed=0;
struct S{public:int value;S(int n){value=n;if(n==7)saved.push(this);}~S(){destroyed++;}}
array<S> saved=array<S>();
void exercise(){S value=S(7);}
void main(){exercise();print(destroyed);saved.clear();print(destroyed);}
)VS",
		    "1\n2\n", "copying a constructing value completes the independent copy", {});
	}

	bool TestConstructorFieldCopy()
	{
		return DualRun(R"VS(
int destroyed=0;
struct S{public:int value=7;~S(){destroyed++;}}
S make(){return S();}
S fail(){throw 7;}
class Holder{public:S member=make();}
class Broken{public:S member=fail();~Broken(){print("wrong");}}
void exercise(){Holder value=Holder();print(destroyed);}
void main(){exercise();print(destroyed);try{Broken broken=Broken();}catch(int error){print(error);}print(destroyed);}
)VS",
		    "0\n1\n7\n1\n", "field initializers construct returned values in place and recover raw storage on failure", {});
	}

	bool TestConstructorLvalueReturn()
	{
		return DualRun(R"VS(
struct S{public:~S(){print("S");}}
S make(){S value=S();return value;}
void main(){S result=make();}
)VS",
		    "S\nS\n", "returning a struct lvalue preserves value copy destruction", {});
	}

	bool TestTemplateScopeIsolation()
	{
		if (!DualRun(R"VS(
export struct T{int value=28;};template<typename U=T> struct Box{U child;};
template<typename T> int read(T unused){return Box<>().child.value;}
void main(){print(read(0));}
)VS",
		        "28\n", "type defaults retain their declaration environment", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
export struct T{int value=29;};template<typename U=T> U make(){U value=T();return value;}
template<typename T> int read(T unused){return make().value;}
void main(){print(read(0));}
)VS",
		        "29\n", "completed function defaults are not rebound by the caller", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
export struct T{int value=30;};template<typename U> int fixed(){return T().value;}
template<typename T> int read(T unused){return fixed<int>();}
void main(){print(read(0));}
)VS",
		        "30\n", "free template bodies do not capture caller bindings", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
export struct T{int value=31;};template<typename A,typename B=A> B copy(A value){return value;}
template<typename T> T forward(T value){return copy(value);}
void main(){print(forward(7));T value=T();print(forward(value).value);}
)VS",
		        "7\n31\n", "dependent defaults retain caller-provided type bindings", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
namespace N{struct T{int value=32;};}template<typename U=N::T> U make(){U value=N::T();return value;}
template<typename T> int read(T unused){return make().value;}
void main(){print(read(0));}
)VS",
		        "32\n", "qualified default types retain their declaration namespace", {}))
		{
			return false;
		}
		return DualRun(R"VS(
export enum class T{Value=33};template<typename U=T> U make(){U value=T::Value;return value;}
template<typename T> int read(T unused){return cast<int>(make());}
void main(){print(read(0));}
)VS",
		    "33\n", "enum defaults retain their declaration identity", {});
	}

	bool TestGenericMemberChains()
	{
		const std::string Declarations = R"VS(
struct Leaf { int value = 7; int read() { return value; } };
template<typename T = Leaf> struct Box { T child; };
template<typename T = Leaf> class Owner { public: T child; };
template<typename T> struct Base { T child; };
template<typename T = Leaf> struct Derived : public Base<T> {};
template<typename T = Leaf> struct Items { T children[2]; };
)VS";
		for (const char* Body : {
		         "print(Box<>().child.value);",
		         "print(Owner<>().child.read());",
		         "print(Derived<>().child.value);",
		         "print(Items<>().children[0].value);",
		         "Box<Leaf> box=Box<>();print(box.child.value);"})
		{
			if (!DualRun(Declarations + "void main(){" + Body + "}", "7\n", Body, {})) return false;
		}
		return DualRun(Declarations + R"VS(
void main()
{
    Owner<string> item=Owner<string>();item.child="before";
    string& alias=item.child;item=Owner<string>();print(alias);
}
)VS",
		    "before\n", "generic constructor identity preserves retained field references", {});
	}

	bool TestConstructorRetentionBoundary()
	{
		int Matched = 0, Skipped = 0;
		for (const std::string Retain : {
		         "saved=this;", "publish();", "retained=fn()->int{return read();};"})
		{
			const std::string Source = R"VS(
int finalized=0;
struct Child{public:~Child(){finalized++;}}
class Ready{public:Ready keep(){return this;}}Ready prior=Ready();
fn()->int retained=fn()->int{return 0;};
class Broken{public:Child child;Broken(){Ready copy=prior.keep();print("borrowed");
)VS" + Retain + R"VS(
throw 7;}void publish(){saved=this;}int read(){return 7;}}Broken saved=nullptr;
void main(){try{Broken value=Broken();}catch(...){}print(finalized);}
)VS";
			auto Compiled = VeroScript::VeroScriptCompile::CompileSource(Source, "constructor-retention.vs");
			if (!Expect(Compiled.bSuccess && Compiled.Module != nullptr, "compile retained construction VM probe")) return false;
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			if (!Expect(Vm.Execute(*Compiled.Module, VeroScript::ERunMode::Script) && Output.str() == "borrowed\n1\n", "VM releases failed construction despite a retained alias"))
			{
				return false;
			}
			std::string Cpp, Error;
			if (!Expect(VeroScript::EmitCppFromSource(Source, "constructor-retention.vs", Cpp, Error),
			        "emit failed construction retention boundary"))
			{
				return false;
			}
			Cpp = "#include <sstream>\n#define main vs_constructor_retention_entry\n" + Cpp + "\n#undef main\n";
			Cpp += R"CPP(
int main() {
    std::ostringstream errors;
    auto* previous = std::cerr.rdbuf(errors.rdbuf());
    const int code = vs_constructor_retention_entry();
    std::cerr.rdbuf(previous);
    if (code != 1 || errors.str().find("retained receiver") == std::string::npos) return 2;
    std::cout << "construction retention rejected\n";
    return 0;
}
)CPP";
			const auto Native = RunNativeCpp(Cpp, "borrowed\nconstruction retention rejected\n", Error);
			if (!Expect(Native != ENativeDualResult::Failed && (Native != ENativeDualResult::Skipped || !RequireNativeDualRun()),
			        "failed construction with a retained receiver cannot resume through a script catch"))
			{
				std::cerr << Error << '\n';
				return false;
			}
			Matched += Native == ENativeDualResult::Matched ? 1 : 0;
			Skipped += Native == ENativeDualResult::Skipped ? 1 : 0;
		}
		std::cout << "Constructor retention boundary matched=" << Matched << " skipped=" << Skipped << '\n';
		if (!DualRun(R"VS(
int finalized=0;
struct Child{public:~Child(){finalized++;}}
class Broken{public:Child child;Broken(){
 fn()->int local=fn()->int{return read();};print(local());throw 7;
}int read(){return 9;}}
void main(){try{Broken value=Broken();}catch(int error){}print(finalized);}
)VS",
		        "9\n1\n", "temporary constructor retention unwinds before failed receiver cleanup", {}))
		{
			return false;
		}
		return DualRun(R"VS(
class Leaving{public:Leaving(){saved=this;print("leaving");exit(0);}}Leaving saved=nullptr;
void main(){Leaving value=Leaving();print("wrong");}
)VS",
		    "leaving\n", "retained constructor receiver preserves an explicit successful exit", {});
	}

	bool TestConstructorLocalNames()
	{
		if (!DualRun(R"VS(
int finalized=0;
struct Guard{public:~Guard(){finalized++;}}
class C{public:C(int ___vs_construction_types){
 Guard guard=Guard();
 int ___vs_complete_receiver=1;int ___vs_construction_scope=2;int ___vs_completion=3;
 print(___vs_complete_receiver+___vs_construction_scope+___vs_completion+___vs_construction_types);
}~C(){print("C");}}
class Broken{public:Broken(){
 Guard guard=Guard();
 int ___vs_complete_receiver=5;int ___vs_construction_types=6;
 int ___vs_construction_scope=7;int ___vs_completion=8;
 throw ___vs_complete_receiver;
}~Broken(){print("wrong");}}
void main(){C value=C(4);print(finalized);try{Broken value=Broken();}catch(int error){print(error);}print(finalized);}
)VS",
		        "10\n1\n5\n2\nC\n", "constructor bookkeeping cannot capture script local names", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
class C{public:int n;C(int self,int self_,int self__):n(self+self_+self__){} }
class D{public:int n;D(){int self=4;int self_=5;int self__=6;int self___=7;n=self+self_+self__+self___;
 fn()->int read=fn()->int{return n;};print(read());}}
struct S{public:int n;S(int self):n(self){}int add(int self){return n+self;}
 int local(){int self=7;return this.n+self;}}
class G<self>{public:self n;G(self value):n(value){}self read(){return n;}}
void main(){C first=C(1,2,3);print(first.n);D second=D();print(second.n);
 S value=S(8);print(value.add(9));print(value.local());G<int> generic=G<int>(10);print(generic.read());}
)VS",
		        "6\n22\n22\n17\n15\n10\n", "generated receivers cannot capture script parameters, locals or template names", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
class C{public:int n=2;
 int parameter(){fn(int)->int read=fn(int vs_this)->int{return n+vs_this;};return read(3);}
 int local(){fn()->int read=fn()->int{int vs_this=4;int vs_this_=5;return n+vs_this+vs_this_;};return read();}
 int nested(){fn()->int outer=fn()->int{fn(int)->int inner=fn(int vs_this__)->int{return n+vs_this__;};return inner(6);};return outer();}}
interface I{virtual int value()=0;}
struct S:public I{public:int n=3;int value(){return n;}
 int read(){fn(int)->int get=fn(int vs_this)->int{return n+vs_this;};return get(4);}}
void main(){C value=C();print(value.parameter());print(value.local());print(value.nested());S item=S();print(item.read());}
)VS",
		        "5\n11\n8\n7\n", "captured receivers cannot capture lambda parameters or nested local names", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
void main(){ordered_map<int,int> entries={1:2};
 for((int ___vs_kv,int value):entries){print(___vs_kv+value);}
 for(pair<int,int> ___vs_kv_:entries){print(___vs_kv_.second);}
 for(int ___vs_kv__:entries){print(___vs_kv__);}}
)VS",
		        "3\n2\n2\n", "map foreach entry temporary cannot capture script binding names", {}))
		{
			return false;
		}
		return DualRun(R"VS(
void main(){bool ___vs_finally_active_0=false;int ___vs_finally_guard_0=7;
 try{try{throw 1;}catch(int& ___vs_exception_1){___vs_exception_1=2;throw;}}
 catch(int ___vs_caught_0_0){print(___vs_caught_0_0);}
 finally{print(___vs_finally_guard_0);}
 print(___vs_finally_active_0);}
)VS",
		    "2\n7\nfalse\n", "catch and finally temporaries cannot capture script bindings", {});
	}

	bool TestConstructorVirtualDispatch()
	{
		if (!DualRun(R"VS(
class B{public:B(){print(read());}virtual int read(){return 1;}}
class D:public B{public:string text="ok";int read()override{print(text);return 2;}}
void main(){D value=D();print(value.read());}
)VS",
		        "1\nok\n2\n", "constructor virtual calls use the current constructor owner", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
class Left{public:Left(){print(left());}virtual int left(){return 11;}}
class Right{public:Right(){print(right());}virtual int right(){return 22;}}
class D:public Left,public Right{public:string text="ready";
 int left()override{print(text);return 31;}int right()override{print(text);return 32;}}
void main(){D value=D();print(value.left());print(value.right());}
)VS",
		        "11\n22\nready\n31\nready\n32\n", "constructor phase identifies multiple inheritance subobjects", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
class B<T>{public:T stored;B(T seed):stored(seed){print(read());}virtual T read()const{return stored;}}
class D:public B<int>{public:D():B(7){}int read()const override{return 9;}}
void main(){D value=D();print(value.read());}
)VS",
		        "7\n9\n", "generic const virtual methods preserve construction dispatch", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
class Other{public:Other(){print(read());}virtual int read(){return 3;}}
class Nested:public Other{public:int read()override{return 4;}}
class B{public:B(){Nested inner=Nested();print(inner.read());print(read());}virtual int read(){return 1;}}
class D:public B{public:int read()override{return 2;}}
void main(){D value=D();print(value.read());}
)VS",
		        "3\n4\n1\n2\n", "nested object construction restores the previous receiver phase", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
bool fail=true;
class Left{public:Left(){print(left());}virtual int left(){return 11;}~Left(){print("left");}}
class Right{public:Right(){print(right());if(fail)throw 7;}virtual int right(){return 22;}~Right(){print("right");}}
class D:public Left,public Right{public:int left()override{return 31;}int right()override{return 32;}~D(){print("derived");}}
void main(){try{D broken=D();}catch(int error){print(error);}fail=false;D value=D();print(value.right());}
)VS",
		        "11\n22\nleft\n7\n11\n22\n32\nderived\nright\nleft\n", "failed multiple inheritance construction cleans only completed subobjects and restores dispatch", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
struct Part{public:int value=4;~Part(){print(value);}}
class B{public:B(){print(read());}virtual int read(){return 1;}virtual ~B()=0;}
B::~B(){print("base");}
class D:public B{public:Part part;D(){throw 9;}int read()override{return 2;}~D(){print("derived");}}
void main(){try{D broken=D();}catch(int error){print(error);}}
)VS",
		        "1\n4\nbase\n9\n", "failed derived constructor releases completed fields and pure destructor bases", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
int destroyed=0;
struct S{public:S(){return;}~S(){destroyed++;}}
class C{public:C(){return;}~C(){print("class");}}
void exercise(){S original=S();S copy=original;C value=C();}
void main(){exercise();print(destroyed);}
)VS",
		        "class\n2\n", "normal constructor returns and default struct copies retain destructor completion", {}))
		{
			return false;
		}
		if (!DualRun(R"VS(
int made=0;int destroyed=0;
struct S{public:int value;S(){made++;value=7;}S(int input){made++;value=input;}~S(){destroyed++;}}
struct Holder{public:S value;}
struct ConstantHolder{public:const S value=S();}
class Fields{public:S first=S();S second;S third=S(9);Fields():second(8){}}
struct G<T>{public:T value;G(T input):value(input){}~G(){destroyed++;}}
void exercise(){Holder holder=Holder();ConstantHolder constant=ConstantHolder();Fields fields=Fields();G<int> generic=G<int>(10);
 print(made);print(holder.value.value);print(constant.value.value);print(fields.first.value);print(fields.second.value);print(fields.third.value);print(generic.value);}
void main(){exercise();print(destroyed);}
)VS",
		        "5\n7\n7\n7\n8\n9\n10\n6\n", "struct factories and explicit fields construct once and release once", {}))
		{
			return false;
		}
		if (!TestConstructorIncompleteCopy()) return false;
		if (!TestConstructorFieldCopy()) return false;
		if (!TestConstructorLvalueReturn()) return false;
		if (!DualRun(R"VS(
import "callable.vs";
fn()->int escaped=fn()->int{return 0;};
class B{public:B(){print(indirect(this));fn()->int lambda=fn()->int{return read();};print(lambda());
 auto bound=std::bind_method<fn()->int>(this,"read");print(bound());escaped=bound;}
 virtual int read(){return 1;}}
int indirect(B value){return value.read();}
class D:public B{public:string text="ok";int read()override{print(text);return 2;}}
void main(){D value=D();print(escaped());print(indirect(value));}
)VS",
		        "1\n1\n1\nok\n2\nok\n2\n", "constructor helpers closures and method bindings share the receiver phase", {},
		        {VEROSCRIPT_STDLIB_DIR}))
		{
			return false;
		}
		return true;
	}

	bool TestEvaluationOrder()
	{
		std::string Expected;
		for (const char* Result : {"5", "3", "8", "5", "5", "5", "5", "5", "false", "2", "64", "8 3"})
		{
			Expected += std::string("first\nsecond\n") + Result + "\n";
		}

		Expected += "77\n";
		if (!DualRun(R"VS(
	int first(){print("first");return 8;} int second(){print("second");return 3;}
	int combine(int a,int b){return a-b;}
	T generic<T>(T a,T b){return a-b;}
	class C { public: int combine(int a,int b){return a-b;} static int mix(int a,int b){return a-b;} }
	namespace N { int combine(int a,int b){return a-b;} }
	void main(){
	 int __vs_eval_0_1=77;
	 print(first()-second()); print(min(first(),second())); print(max(first(),second()));
	 print(combine(first(),second())); print(generic<int>(first(),second()));
	 C c=C();print(c.combine(first(),second()));print(C.mix(first(),second()));
	 print(N::combine(first(),second()));
	 print(first()<second()); print(first()/second()); print(first()<<second());
	 print(format("{} {}",first(),second())); print(__vs_eval_0_1);
	}
	)VS",
		        Expected, "calls and primitive operations evaluate in source order", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
	string text="before";
	string change(){text="after";return "suffix";}
	void show(string a,string b){print(a);print(b);}
	struct Value { public:int n=3; };
	Value value=Value(); int change_value(){value.n=9;return 1;}
	int read(Value v,int unused){return v.n;}
	void bump(ref int x,int amount){x+=amount;}
	int increment(ref int x){x++;return 2;}
	bool reject(){print("reject");return false;} bool accept(){print("accept");return true;}
	bool unused(){print("wrong");return true;}
	void main(){show(text,change()); print(read(value,change_value()));
	 int x=4; bump(ref x,increment(ref x));print(x);
	 print(reject()&&unused());print(accept()||unused());}
	)VS",
		        "before\nsuffix\n3\n7\nreject\nfalse\naccept\ntrue\n",
		        "value snapshots, ref arguments and short circuit survive sequencing", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
	class C {public:int n; C(int value){n=value;} int read(int unused){return n;}}
	C item=C(3); int replace(){item=C(9);return 1;}
	int old(int ignored){return 3;} int fresh(int ignored){return 9;}
	fn(int)->int callback=old;
	int replace_callable(){callback=fresh;return 1;}
	fn(int)->int get_callback(){print("callee");return callback;}
	namespace N {fn(int)->int callback=old;int replace_namespace_callable(){callback=fresh;return 1;}
	 int exercise(){return callback(replace_namespace_callable());}}
	struct S {public:int n=3;int bump(int amount){n+=amount;return n;}}
	void main(){print(item.read(replace()));print(callback(replace_callable()));
	 print(N.exercise());print(get_callback()(replace_callable()));S s=S();print(s.bump(2));print(s.n);}
	)VS",
		        "3\n9\n9\ncallee\n9\n5\n5\n", "method and callable evaluation matches VM binding order", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
	int first(){print("first");return 8;} int second(){print("second");return 3;}
	class C {public:static int value=first()-second();}
	struct V {public:int n;V(int input){print("convert");n=input;}}
	int combine(V a,int b){return a.n-b;}
	int sum(int a,int... rest){int total=a;for(int x:rest){total+=x;}return total;}
	void main(){print(C.value);print(combine(first(),second()));print(sum(first(),second(),1));}
	)VS",
		        "first\nsecond\n5\nfirst\nconvert\nsecond\n5\nfirst\nsecond\n12\n",
		        "conversions, namespace initializers and variadic calls preserve order", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
	array<int> values={0};int x=5;
	array<int> get(){print("array");return values;}
	int index(){print("index");return 0;}int right(){print("right");return 42;}
	int alter(){x=20;return 2;}
	string text="before";string change(){text="after";return "suffix";}
	void main(){values[index()]=right();print(values[0]);x+=alter();print(x);
	 text+=change();print(text);int _vs_target=7;_vs_target+=2;print(_vs_target);}
	)VS",
		        "index\nright\n42\n7\nbeforesuffix\n9\n",
		        "assignment target order and compound value snapshots", {}))
		{
			return false;
		}

		if (!DualRun(R"VS(
	int first(){print("first");return 8;} int second(){print("second");return 3;}
	class C {public:int n;C(int a,int b){n=a-b;}}
	int read(C c){return c.n;}
	void main(){C stack=C(first(),second());print(stack.n);C heap=C(first(),second());print(read(heap));}
	)VS",
		        "first\nsecond\n5\nfirst\nsecond\n5\n",
		        "stack and heap constructors evaluate arguments in order", {}))
		{
			return false;
		}

		if (!CoreRuntimeRun(R"VS(
	array<int> values={7};array<int> original=values;
	int grow(){values.reserve(1000);return 2;}
	int replace(){values=array<int>();values.push(20);return 2;}
	void bump(ref int value,int amount){value+=amount;}
	void main(){bump(ref values[0],grow());print(values[0]);
	 bump(ref values[0],replace());print(original[0]);print(values[0]);}
	)VS",
		        "9\n11\n20\n", "indexed references borrow original storage before later arguments", {},
		        "requires managed storage paths"))
		{
			return false;
		}
		if (!CoreRuntimeRun(R"VS(
	array<int> empty=array<int>();int fill(){print("wrong");empty.push(7);return 2;}
	void bump(ref int value,int amount){value+=amount;}
	void main(){bump(ref empty[0],fill());}
	)VS", "", "missing reference target fails before later arguments", {},
		        "requires managed storage paths", "target does not exist"))
		{
			return false;
		}

		if (!DualRun(R"VS(
	struct S {public:int n=3;}
	S value=S();int change(){value.n=9;return 1;}
	int copied(S self,int ignored){return self.n;}
	int alias(ref S self,int ignored){return self.n;}
	void main(){print(value.copied(change()));value.n=3;print(value.alias(change()));}
	)VS",
		        "3\n9\n", "extension receiver respects value and reference parameter modes", {}))
		{
			return false;
		}

		if (!CoreRuntimeRun(R"VS(
	array<int> values=array<int>();int bad(){return -1;}int later(){print("wrong");return 2;}
	void bump(ref int value,int amount){value+=amount;}
	void main(){bump(ref values[bad()],later());}
	)VS",
		        "", "invalid reference index fails before later argument side effects", {},
		        "requires managed storage paths", "index"))
		{
			return false;
		}

		return true;
	}
}

int main(int Argc, char** Argv)
{
	if ((Argc == 1 || std::string_view(Argv[1]) == "--function-values-only") && !DualRun(R"vs(
int make(){return 99;}
template<typename T> T identity(T value){return value;}
namespace N
{
    int calls=0;
    int make(){return 42 + calls++;}
    struct Payload{int value=12;}
    Payload echo(Payload value){return value;}
    namespace Inner
    {
        int make(){return 7;}
        template<typename T> T identity(T value){return value;}
    }
    void bump(int& value){value+=2;}
    int read(const int& value){return value;}
}
class Receiver{public:fn()->int make=[]()->int{return 21;};}
class Owner
{
public:
    Receiver N=Receiver();
    fn()->int get(){return [callback=N.make]()->int{return callback();};}
}
void main()
{
    auto f=[callback=N::make]()->int{return callback();};
    print(N::calls);print(f());print(f());print(N::calls);
    auto direct=N::Inner::make;
    assert(direct==N::Inner::make);
    auto echo=N::echo;N::Payload payload=N::Payload();assert(echo(payload).value==12);
    fn()->int typed=N.Inner.make;
    auto global=[callback=::N::Inner::make]()->int{return callback();};
    print(direct());print(typed());print(global());
    auto generic=[value=N::Inner::identity<int>(8)]()->int{return value;};
    auto ordinary=[value=identity<int>(9)]()->int{return value;};
    print(generic());print(ordinary());
    auto reference=[callback=N::bump](int& value)->void
    {
        callback(&value);
    };
    int value=40;reference(&value);print(value);
    auto read=N::read;assert(read(&value)==42);
    fn(ref int)->void typed_reference=[callback=N::bump](int& input)->void{callback(&input);};
    typed_reference(&value);assert(value==44);
    fn(const ref int)->int contextual=fn(input)=>input;assert(contextual(&value)==44);
    {Receiver N=Receiver();auto field=[callback=N.make]()->int{return callback();};print(field());}
    Owner owner=Owner();auto field=owner.get();print(field());
    auto bare=[callback=make]()->int{return callback();};print(bare());
}
)vs",
	                                                                                "0\n42\n43\n2\n7\n7\n7\n8\n9\n42\n21\n21\n99\n", "qualified function values and template calls in initialized captures", {}))
	{
		return EXIT_FAILURE;
	}
	if ((Argc == 1 || std::string_view(Argv[1]) == "--function-values-only") && !DualRun(R"vs(
namespace N
{
    int make(){return 99;}
    fn()->int make=[]()->int{return 42;};
}
void main(){auto f=[callback=N::make]()->int{return callback();};print(f());}
)vs",
	                                                                                "42\n", "qualified function globals keep priority over free functions", {}))
	{
		return EXIT_FAILURE;
	}
	if ((Argc == 1 || std::string_view(Argv[1]) == "--function-values-only") && !DualRun(R"vs(
int make(){return 99;}
fn()->string make=[]()->string{return "global";};
namespace N
{
    int make(){return 7;}
    fn()->string make=[]()->string{return "namespace global";};
    fn()->int get(){return make;}
}
class Owner
{
public:
    fn()->int make=[]()->int{return 21;};
    fn()->int get(){return make;}
}
void main()
{
    auto bare=make;print(bare());
    auto captured=[callback=make]()->int{return callback();};print(captured());
    auto qualified=N::make;print(qualified());
    auto scoped=N::get();print(scoped());
    Owner owner=Owner();auto field=owner.get();print(field());
    {auto make=[]()->int{return 13;};auto local=make;print(local());}
}
)vs",
	                                                                                "99\n99\nnamespace global\n7\n21\n13\n", "bare function values precede globals while locals and fields retain priority", {}))
	{
		return EXIT_FAILURE;
	}
	if ((Argc == 1 || std::string_view(Argv[1]) == "--function-values-only") && !DualRun(R"vs(
enum E{value=7};
class Owner
{
public:
    int value=42;
    int get(){return value;}
    fn()->int get_callback(){return [=]()->int{return value;};}
}
struct Plain{int value=21;int get(){return value;}}
template<typename T> class Generic
{
public:
    T value;
    Generic(T seed){value=seed;}
    T get(){return value;}
}
void main()
{
    Owner owner=Owner();Plain plain=Plain();
    print(owner.get());auto callback=owner.get_callback();print(callback());
    print(plain.get());print(value);print(E.value);
    {int value=13;print(value);}
    Generic<int> generic=Generic<int>(31);print(generic.get());
}
)vs",
	                                                                                "42\n42\n21\n7\n7\n13\n31\n", "implicit fields precede global enum constants in methods and captures", {}))
	{
		return EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--function-values-only")
	{
		return gNativeDualMatched != 0 && gNativeDualSkipped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if ((Argc == 1 || std::string_view(Argv[1]) == "--lambda-followup-only") && !DualRun(R"vs(
struct ReceiverCounter
{
    int value = 1;
    fn()->int make() { return fn()->int { value += 1; return value; }; }
};
void main()
{
    ReceiverCounter receiver = ReceiverCounter();
    auto callback = receiver.make();
    auto alias = callback;
    print(callback()); print(alias()); print(receiver.value);
}
)vs",
	                                                                                "2\n3\n1\n", "struct receiver capture preserves writable snapshot", {}))
	{
		return EXIT_FAILURE;
	}
	if ((Argc == 1 || std::string_view(Argv[1]) == "--lambda-followup-only") && !DualRun(R"vs(
struct ReadonlyChoice
{
    int read() const { return 200; }
};
struct ReceiverWithCapture
{
    int value = 1;
    fn()->int make(ReadonlyChoice choice)
    {
        return [choice]()->int { value += 1; return choice.read() + value; };
    }
    fn()->int initialized(ReadonlyChoice choice)
    {
        return [copy = choice]()->int { value += 1; return copy.read() + value; };
    }
};
void main()
{
    ReceiverWithCapture receiver = ReceiverWithCapture();
    auto first = receiver.make(ReadonlyChoice());
    auto second = receiver.initialized(ReadonlyChoice());
    print(first()); print(first()); print(second()); print(second()); print(receiver.value);
}
)vs",
	                                                                                "202\n203\n202\n203\n1\n", "receiver mutability does not weaken readonly captures", {}))
	{
		return EXIT_FAILURE;
	}
	if ((Argc == 1 || std::string_view(Argv[1]) == "--lambda-followup-only") && !DualRun(R"vs(
struct ConstReceiver
{
    int read() const { return 200; }
    fn()->int make() const
    {
        return [count = 0]() mutable -> int { return read() + ++count; };
    }
};
void main()
{
    ConstReceiver receiver = ConstReceiver();
    auto callback = receiver.make();
    print(callback()); print(callback());
}
)vs",
	                                                                                "201\n202\n", "mutable captures preserve const receiver methods", {}))
	{
		return EXIT_FAILURE;
	}
	if ((Argc == 1 || std::string_view(Argv[1]) == "--lambda-followup-only") && !DualRun(R"vs(
int attempts = 0;
int seed() { ++attempts; if (attempts == 1) throw attempts; return 8; }
int next() { static int value = seed(); return ++value; }
void main()
{
    try { next(); } catch (int error) { print(error); }
    print(next()); print(next()); print(attempts);
}
)vs",
	                                                                                "1\n9\n10\n2\n", "local static retries after failed initialization", {}))
	{
		return EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--lambda-followup-only")
	{
		return gNativeDualMatched != 0 && gNativeDualSkipped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if ((Argc == 1 || std::string_view(Argv[1]) == "--lambda-static-only") && !DualRun(std::string(VeroScript::Test::LambdaStaticDefinitions) + "void main(){checkLambdaStaticSupport();}", "", "initialized captures, mutable and local statics", {}))
	{
		return EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--lambda-static-only")
	{
		return gNativeDualMatched == 1 && gNativeDualSkipped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (Argc > 1 && std::string_view(Argv[1]) == "--template-scope-only")
	{
		if (!TestTemplateScopeIsolation()) return EXIT_FAILURE;
		std::cout << "Template scope isolation: native matched=" << gNativeDualMatched
		          << " skipped=" << gNativeDualSkipped << '\n';
		return gNativeDualMatched == 6 && gNativeDualSkipped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--generic-member-chains-only")
	{
		if (!TestGenericMemberChains()) return EXIT_FAILURE;
		std::cout << "Generic member chains: native matched=" << gNativeDualMatched
		          << " skipped=" << gNativeDualSkipped << '\n';
		return gNativeDualMatched == 6 && gNativeDualSkipped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--concurrent-shutdown-only")
	{
		return TestConcurrentShutdownDescendants() && gNativeDualMatched != 0 && gNativeDualSkipped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--assignment-sequencing-only")
	{
		return TestAssignmentSequencing() && gNativeDualMatched != 0 && gNativeDualSkipped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--constructor-copy-this-only")
	{
		return TestConstructorIncompleteCopy() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--constructor-field-copy-only")
	{
		return TestConstructorFieldCopy() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--constructor-lvalue-return-only")
	{
		return TestConstructorLvalueReturn() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--constructor-virtual-only")
	{
		return TestConstructorVirtualDispatch() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--constructor-local-names-only")
	{
		return TestConstructorLocalNames() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--constructor-retention-only")
	{
		return TestConstructorRetentionBoundary() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--file-io-only")
	{
		return TestFileIoBoundaries() && gNativeDualMatched == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--constexpr-only")
	{
		return DualRun(R"VS(constexpr int plus(int n){if constexpr(true)return n+1;else return nonexistent();}constexpr int count=plus(3);constexpr uint64 maximum=18446744073709551615u64;enum class Mode{First=plus(1),Second};constexpr Mode selected=Mode::Second;struct Config{static constexpr int count=plus(4);int values[plus(2)];};int length(int values[plus(2)]){return len(values);}class Card{public:int value=9;int get(){auto reader=[=]()->int{if constexpr(true)return value;else return missing();};return reader();}};void main(){Card card=Card();assert(card.get()==9);if constexpr(false){struct Discarded{int read(){return missing();}};int discarded=nonexistent();}constexpr int n=plus(5);int data[plus(2)];Config config=Config();print(n);print(count);print(Config::count);print(length(data));print(len(config.values));int runtime=7;print(plus(runtime));print(maximum);print(cast<int>(selected));constexpr auto narrowed=cast<int16>(7);print(narrowed);})VS",
		           "6\n4\n5\n3\n3\n8\n18446744073709551615\n3\n7\n", "constexpr closed scalar and array lowering", {}) &&
		        gNativeDualMatched == 1
		    ? EXIT_SUCCESS
		    : EXIT_FAILURE;
	}

	if (!DualRun(R"VS(import "string.vs"; import "algorithm.vs"; import "math.vs"; import "random.vs";
void main(){
    assert(std::strings::starts_with("abc","ab") && std::strings::ends_with("abc","bc"));
    assert(std::strings::last_find("aaa","aa")==1);
    assert(std::strings::trim("\v\f a \r\n")=="a");
    assert(std::strings::repeat("ab",3)=="ababab");
    assert(std::strings::replace("aaaaa","aa","x")=="xxa");
    assert(std::clamp_int(9,0,8)==8);
    array<int> a={1,2,3}; std::algorithm::reverse(a); assert(a[0]==3);
    std::algorithm::rotate(a,1); assert(a[0]==2 && a[2]==3);
    auto e=std::random::make_engine(123u); assert(std::random::next_uint(e)==31682556u);
    std::algorithm::shuffle(a,e); std::algorithm::sort(a); assert(a[0]==1 && a[2]==3);
    assert(format("{:x}",18446744073709551615u64)=="ffffffffffffffff");
    assert(format("{:x}",-9223372036854775807i64-1i64)=="8000000000000000");
    print("foundation portable passed");
})VS",
	        "foundation portable passed\n", "portable foundation operations", {}, {VEROSCRIPT_STDLIB_DIR}))
	{
		return EXIT_FAILURE;
	}
	if (!CoreRuntimeRun(R"VS(import "concurrent.vs";
export void answer(channel<int> output){output.send(42);}
void main(){
    auto cell=std::concurrent::make_synchronized<int>(4);
    assert(std::concurrent::synchronized_fetch_add(cell,1)==4);
    task_group<int> group=task_group<int>();
    group.add(std::concurrent::spawn_task<int>("answer"));
    assert(group.await_all()[0]==42); print("Core runtime passed");
})VS",
	        "Core runtime passed\n", "task and synchronization capability AOT boundary", {VEROSCRIPT_STDLIB_DIR}))
	{
		return EXIT_FAILURE;
	}
	if (Argc > 1 && std::string(Argv[1]) == "--foundation-only") return gNativeDualMatched == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
	if (!TestFileIoBoundaries()) return EXIT_FAILURE;
	if (!CoreRuntimeRun(R"VS(import "algorithm.vs";
array<bool> flags={true,false,true};
bool clear_first(){flags[0]=false;return true;}
bool equal(bool left,bool right){return left==right;}
void flip(bool& value){value=!value;}
void main(){
    array<bool> shared=flags;
    print(flags[0]);print(equal(flags[0],clear_first()));
    flip(ref flags[1]);print(shared[1]);
    flags.reserve(16);flags.push(false);print(flags[len(flags)-1]);flags.remove_at(len(flags)-1);
    std::algorithm::reverse(flags);print(flags[0]);print(flags[2]);
    print(flags.remove(true));print(len(flags));
})VS",
	        "true\ntrue\ntrue\nfalse\ntrue\nfalse\ntrue\n2\n",
	        "boolean array storage preserves scalar values, references and swaps", {VEROSCRIPT_STDLIB_DIR},
	        "requires managed storage paths"))
	{
		return EXIT_FAILURE;
	}
	if (!DualRun(
	        "T identity<T>(T value){using Value=T;Value result=value;return result;}"
	        "void main(){using Value=int;Value result=identity<int>(7);print(result);}",
	        "7\n", "generic block aliases stay in their lexical scope", {"using Value ="}))
	{
		return EXIT_FAILURE;
	}
	if (!TestEvaluationOrder()) return EXIT_FAILURE;
	if (!TestAssignmentSequencing()) return EXIT_FAILURE;
	if (!TestReferenceValueSequencing()) return EXIT_FAILURE;
	if (!TestReferenceOwnersAndLayouts()) return EXIT_FAILURE;
	if (!TestDirectFieldReferences()) return EXIT_FAILURE;
	if (!TestTemplateScopeIsolation()) return EXIT_FAILURE;
	if (!TestGenericMemberChains()) return EXIT_FAILURE;
	if (!TestConstructorVirtualDispatch()) return EXIT_FAILURE;
	if (!TestConstructorLocalNames()) return EXIT_FAILURE;
	if (!TestConstructorRetentionBoundary()) return EXIT_FAILURE;
	{
		VeroScript::FCodegenOptions O;
		O.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		const auto Builder = (std::filesystem::temp_directory_path() / "vero_cpp_native_script_builder.vs").generic_string();
		O.ScriptCompileExtensions.Roots = {{Builder, {}}};
		O.ScriptCompileExtensions.Configuration = {{"amount", "5"}};
		O.SourceOverlays = {{Builder, VeroScript::CppScriptFixture::Extension}};
		O.StdlibSearchPaths = {VEROSCRIPT_STDLIB_DIR};
		O.CompileProcessing.ReadDependency = [](std::string_view, std::string& Text)
		{
			Text = "7";
			return true;
		};
		if (!DualRun(VeroScript::CppScriptFixture::Source, "15\n",
		        "script extension generated business code runs natively", {}, {}, {}, nullptr, nullptr,
		        nullptr, false, nullptr, &O))
		{
			return EXIT_FAILURE;
		}
	}
	{
		using namespace VeroScript;
		struct FPhased final : ICompileProcessor
		{
			void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
			{
				if (C.Phase == ECompileProcessingPhase::RawSource)
				{
					++RawCalls;
					const auto P = C.Source->Text.find("CUSTOM");
					if (P != std::string::npos)
					{
						FSourceRange R;
						R.SourceId = C.Source->Id;
						R.Begin = P;
						R.End = P + 6;
						O.EditSource(R, "class");
					}
				}
				else
				{
					if (RawCalls != 1)
					{
						throw std::runtime_error("lost source processor state");
					}

					for (const auto& D : C.Declarations)
					{
						if (D.Name == "Pending" && !D.bHasBody)
						{
							O.CompleteBody(D.Id, C.Snapshot, "{return 19;}");
						}
					}
				}
			}

			unsigned RawCalls = 0;
		};
		auto R = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc D;
		D.ProcessorId = "cpp.native.phased";
		D.Version = "1";
		D.Phases = 1 | 16;
		D.Operations = 1 | 8;
		D.Subscriptions = {{ECompileProcessingEvent::Module}};
		D.Factory = []
		{
			return std::make_unique<FPhased>();
		};
		std::string Error;
		if (!R->Register(std::move(D), Error)) return EXIT_FAILURE;
		R->Freeze();
		FCompileProcessingOptions O;
		O.Registry = R;
		if (!DualRun("CUSTOM Item{};int Pending();void main(){print(Pending());}", "19\n",
		        "source processor state survives native generation", {}, {}, {}, nullptr, nullptr, nullptr,
		        false, &O))
		{
			return EXIT_FAILURE;
		}
	}
	std::srand(static_cast<unsigned>(std::time(nullptr)));
	{
		using namespace VeroScript;
		const FScopedTempDirectory Directory("veroscript_aot_annotations");
		const auto Write = [&](const char* Name, const char* Source)
		{
			std::ofstream File(Directory.GetPath() / Name);
			File << Source;
			File.close();
			return bool(File);
		};
		if (!Write("decl.vs",
		        "using Number=int; namespace API{export Number Call(Number declared);} "
		        "export struct Box<T>{T value;Box(T input){value=input;} T Get() const;};") ||
		    !Write("impl.vs",
		        "import \"decl.vs\"; using OtherNumber=string; int helper(){return 3;} "
		        "int API::Call(int input){return input+helper();} T Box<T>::Get() const{return value;}"))
		{
			return EXIT_FAILURE;
		}

		struct FForward final : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
			{
				Output.CompleteMethodBody(Context.Declaration.Id, "{return Call_Impl(value);}");
			}
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		std::string Error;
		if (!Registry->Register({"@Forward", "cpp.aot.forward", "1", "",
		                            AnnotationTargetMask(EAnnotationTarget::Method), 2, 0,
		                            []
		{
			return std::make_unique<FForward>();
		}},
		        Error))
		{
			return EXIT_FAILURE;
		}

		Registry->Freeze();
		FAnnotationProcessingOptions Options;
		Options.Registry = Registry;
		if (!DualRun(
		        "import \"decl.vs\";import \"impl.vs\";class Base{public: @Forward virtual int Call(int value);"
		        "virtual int Call_Impl(int value){return value+1;}};class Child:public Base{public:"
		        "int Call_Impl(int value) override{return value+2;}};"
		        "void main(){Child c=Child();Base b=c;print(b.Call(40));Box<int> x(7);print(x.Get());print(API::Call(39));}",
		        "42\n7\n42\n",
		        "annotation bodies and separated definitions retain dispatch and lexical scope", {},
		        {PathUtil::ToUtf8(Directory.GetPath())}, {}, nullptr, nullptr, &Options))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const FScopedTempDirectory Directory("veroscript_aot_public_macros");
		const auto WriteModule = [&](const char* Name, const char* Source)
		{
			std::ofstream File(Directory.GetPath() / Name, std::ios::binary);
			File << Source;
			File.close();
			return static_cast<bool>(File);
		};
		if (!WriteModule("base.vs",
		        "#define PRIVATE 999\n#define_export VALUE 21\n"
		        "#define_export ADD(x, ...) ((x)+LOCAL_BIAS __VA_OPT__(+ __VA_ARGS__))\n") ||
		    !WriteModule("facade.vs", "export import \"base.vs\";\n") ||
		    !WriteModule("ordinary.vs", "import \"base.vs\";\n") ||
		    !DualRun(
		        "import \"ordinary.vs\";\n#ifdef VALUE\n#error ordinary import re-exported a macro\n#endif\n"
		        "#define LOCAL_BIAS 1\n#define LOAD import \"facade.vs\";\nLOAD\n"
		        "#ifdef PRIVATE\n#error private macro leaked\n#endif\n"
		        "int shared = ADD(VALUE, 20);\n#undef VALUE\nimport \"facade.vs\";\n"
		        "void main(){print(shared);print(VALUE);}\n",
		        "42\n21\n", "public macros use caller environment and module visibility", {},
		        {VeroScript::PathUtil::ToUtf8(Directory.GetPath())}))
		{
			return EXIT_FAILURE;
		}
	}
	for (const char* Source : {
	         "void main() { uint8 value = 255; ++value; }",
	         "void main() { uint8 value = 255; value++; }",
	         "void main() { uint16 value = 0; --value; }",
	         "void main() { uint16 value = 0; value--; }",
	         "T increase<T>(T value) { return ++value; } void main() { uint8 value = 255; increase(value); }"})
	{
		if (!DualRun(Source, "", "narrow unsigned increment/decrement must reject overflow",
		        {}, {}, {}, nullptr, "conversion overflow"))
		{
			return EXIT_FAILURE;
		}
	}
	if (!DualRun(R"VS(
int calls = 0;
int next() { calls += 1; return 0; }
class Counter { public: int value = 20; uint8 narrow = 255; double real = 2.0; }
Counter item = Counter();
Counter get() { calls += 1; return item; }
Counter fresh() { calls += 1; return Counter(); }
T increase<T>(T value) { return ++value; }
void main() {
    int VsCheckedIncDec = 7;
    array<int> values = {10};
    print(values[next()]++); print(calls); print(values[0]);
    calls = 0;
    print(++values[next()]); print(calls); print(values[0]);
    calls = 0;
    print(get().value--); print(calls); print(item.value);
    calls = 0;
    print(--get().value); print(calls); print(item.value);
    calls = 0;
    print(++get().real); print(calls);
    uint8 narrow = 254;
    print(++narrow);
    long large = 4294967296i64;
    print(large++); print(large);
    unsigned long wide = 18446744073709551615u64;
    print(++wide);
    print(increase(large));
    calls = 0;
    print(fresh().value++); print(calls);
    print(VsCheckedIncDec);
}
)VS",
	        "10\n1\n11\n12\n1\n12\n20\n1\n19\n18\n1\n18\n3\n1\n255\n4294967296\n4294967297\n0\n4294967298\n20\n1\n7\n",
	        "increment/decrement evaluates target once", {}))
	{
		return EXIT_FAILURE;
	}
	if (!DualRun(R"VS(import "string.vs";
int classify(int value) { return 1; }
int classify(int64 value) { return 2; }
void main()
{
    array<int> values = {4, 7};
    int64 index = 1i64;
    uint64 unsignedIndex = 0u64;
    int16 smallIndex = 0;
    assert(classify(len(values)) == 2, "len must return int64");
    assert(classify(len("abc")) == 2, "string length must return int64");
    assert(classify(values.capacity()) == 2, "capacity must return int64");
    values.reserve(16i64);
    values[index] += 3;
    values[unsignedIndex] = values[smallIndex] + 1;
    assert(values[index] == 10 && values[unsignedIndex] == 5, "wide indexing failed");
    int total = 0;
    for (ref int value : values) { value += 1; total += value; }
    assert(total == 17, "wide foreach failed");
    values.remove_at(0i64);
    int narrowed = len(values);
    assert(narrowed == 1, "checked implicit narrowing failed");
    assert(std::strings::substring("abc", 1i64, 9223372036854775807i64) == "bc", "wide substring failed");
    assert(classify(std::strings::find("abc", "b")) == 2 && std::strings::find("abc", "z") == -1i64, "wide find failed");
    assert(std::strings::char_code("abc", 1i64) == 98, "wide character index failed");
    multiset<int> counts = multiset<int>();
    counts.add(7); counts.add(7);
    assert(classify(counts.count(7)) == 2 && counts.count(7) == 2i64, "wide count failed");
    print("wide lengths passed");
}
)VS",
	        "wide lengths passed\n", "wide length and index contract", {}))
	{
		return EXIT_FAILURE;
	}

	std::string Cpp;
	std::string Error;
	{
		if (!Expect(VeroScript::EmitCppFromSource(
		                "void main() { print(len(\"abc\")); }", "length-bounds.vs", Cpp, Error),
		        "emit checked length boundary probes"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		const std::size_t MainPosition = Cpp.find("int main()");
		if (!Expect(MainPosition != std::string::npos && Cpp.find("VsCheckedLength((") != std::string::npos,
		        "len routes through the checked length conversion"))
		{
			return EXIT_FAILURE;
		}
		Cpp.replace(MainPosition, 10, "int VsLengthScriptMain()");
		// Probe counts directly so overflow coverage does not require huge allocations.
		Cpp += R"CPP(
int main()
{
    const std::uint64_t Limit = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
    if (VsCheckedLength(0) != 0 || VsCheckedLength(2147483648ull) != 2147483648ll
        || VsCheckedLength(Limit) != (std::numeric_limits<std::int64_t>::max)()) return 1;
    for (const std::uint64_t Length : {Limit + 1, (std::numeric_limits<std::uint64_t>::max)()})
    {
        try
        {
            VsCheckedLength(Length);
            return 1;
        } catch (const VsExitException& Error)
        {
            if (Error.Code != 1) return 1;
        }
    }
    if (VsLengthScriptMain() != 0) return 1;
    try { (void)VsCheckedNativeSize(true); return 1; }
    catch (const VsExitException&) { }
    std::cout << "length bounds passed\n";
    return 0;
}
)CPP";
		std::string NativeError;
		const ENativeDualResult Native = RunNativeCpp(Cpp, "3\nlength bounds passed\n", NativeError);
		if (!Expect(Native != ENativeDualResult::Failed && (Native != ENativeDualResult::Skipped || !RequireNativeDualRun()),
		        "native checked length boundaries"))
		{
			std::cerr << NativeError << '\n';
			return EXIT_FAILURE;
		}
	}
	{
		const auto ExpectUnsupported = [](const std::string_view Source,
		                                   const std::string_view Diagnostic, const char* Label)
		{
			std::string Generated;
			std::string Failure;
			if (VeroScript::EmitCppFromSource(Source, "aot.vs", Generated, Failure) || Failure.find(Diagnostic) == std::string::npos)
			{
				std::cerr << "FAIL: " << Label << '\n'
				          << Failure << '\n';
				return false;
			}
			return true;
		};
		if (!ExpectUnsupported(
		        "import \"concurrent.vs\";\n"
		        "int twice(int value) { return value * 2; }\n"
		        "void main() { task<int> work = std::concurrent::spawn_task<int>(twice, 21); }\n",
		        "standard-library foundation operations require",
		        "typed callable task requires Core runtime") ||
		    !ExpectUnsupported(
		        "class Counter { public: int value = 1; }\n"
		        "void main() {\n"
		        "    field_info<Counter> field = field_info<Counter>(type<Counter>().qualified_name, 0);\n"
		        "    Counter value = Counter();\n"
		        "    print(field.get<int>(value));\n"
		        "}\n",
		        "executable reflection is not supported",
		        "AOT rejects executable reflection"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		VeroScript::FHostBindingRegistry Bindings;
		std::string BindingError;
		if (!Bindings.RegisterNative(
		        "wide_native",
		        VeroScript::ENativeType::Int64,
		        {VeroScript::ENativeType::UInt64},
		        [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		},
		        nullptr,
		        BindingError))
		{
			std::cerr << BindingError << '\n';
			return EXIT_FAILURE;
		}
		if (!Bindings.RegisterNative(
		        "Math::wide_native",
		        VeroScript::ENativeType::Int64,
		        {VeroScript::ENativeType::UInt64},
		        [](const VeroScript::FNativeCallContext&, VeroScript::FValue&, std::string&)
		{
			return true;
		},
		        nullptr,
		        BindingError) ||
		    !Bindings.RegisterTypeAlias(
		        "AliasInt", {"T"}, "int", BindingError))
		{
			std::cerr << BindingError << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FHostTypeId StubTypeId;
		if (!Bindings.RegisterType(
		        "HostStub", VeroScript::EHostTypeKind::Struct, StubTypeId, BindingError))
		{
			std::cerr << BindingError << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FHostMemberDesc ValueCtor;
		ValueCtor.Kind = VeroScript::EHostMemberKind::Constructor;
		ValueCtor.ParamTypes = {VeroScript::ENativeType::Array};
		ValueCtor.ParamElementTypeNames = {"int"};
		VeroScript::FHostMemberDesc ConstRefCtor = ValueCtor;
		ConstRefCtor.ParamFlags = {
		    static_cast<std::uint8_t>(
		        VeroScript::NativeParamFlagRef | VeroScript::NativeParamFlagConstRef)};
		VeroScript::FHostMemberDesc StaticAnswer;
		StaticAnswer.Kind = VeroScript::EHostMemberKind::StaticConstant;
		StaticAnswer.Name = "Answer";
		StaticAnswer.ReturnType = VeroScript::ENativeType::Int;
		if (!Bindings.RegisterMember(StubTypeId, ValueCtor, BindingError) || !Bindings.RegisterMember(StubTypeId, ConstRefCtor, BindingError) || !Bindings.RegisterMember(StubTypeId, StaticAnswer, BindingError))
		{
			std::cerr << BindingError << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::FCppCodegenBackend Backend;
		VeroScript::FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.Bindings = &Bindings;
		VeroScript::FCodegenResult Result;
		if (!Expect(
		        Backend.EmitFromSource(
		            "void main() { AliasInt<string> value = 1; "
		            "wide_native(1u64); }\n",
		            "<aot_wide_native>",
		            Options,
		            Result),
		        "AOT wide native host stub emit") ||
		    !Expect(
		        Result.Output.find(
		            "std::int64_t wide_native(std::uint64_t);") != std::string::npos,
		        "AOT host stubs preserve int64/uint64 ABI") ||
		    !Expect(
		        Result.Output.find(
		            "std::int64_t Math_wide_native(std::uint64_t);") != std::string::npos &&
		            Result.Output.find("Math__wide_native") == std::string::npos,
		        "AOT namespaced host stubs match call-site mangling") ||
		    !Expect(
		        Result.Output.find("void* const&") != std::string::npos,
		        "AOT host stubs preserve const-ref ABI") ||
		    !Expect(
		        Result.Output.find("std::int32_t HostStub_Answer();") != std::string::npos,
		        "AOT static constants have no implicit receiver"))
		{
			std::cerr << Result.Error << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!DualRun(
	        "void main() {\n"
	        "    print(\"hello\");\n"
	        "}\n",
	        "hello\n",
	        "hello",
	        {"VsPrint(", "vs_script_main", "int main()", "std::string(\"hello\")"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
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
	        "struct ConstValueHolder {\n"
	        "    const ConstPayload payload;\n"
	        "    ConstValueHolder(ConstPayload input) : payload(input.value) {}\n"
	        "}\n"
	        "struct ConstHandleHolder {\n"
	        "    const handle<ConstPayload> payload;\n"
	        "    ConstHandleHolder(int input) : payload(ConstPayload(input)) {}\n"
	        "}\n"
	        "void main() {\n"
	        "    ConstHolder value = ConstHolder(4);\n"
	        "    print(value.total());\n"
	        "    ConstValueHolder copy = ConstValueHolder(ConstPayload(6));\n"
	        "    print(copy.payload.value);\n"
	        "    ConstHandleHolder handle = ConstHandleHolder(8);\n"
	        "    print(handle.payload.value.value);\n"
	        "}\n",
	        "9\n6\n8\n",
	        "constructor field initializers match native C++ AOT",
	        {"self.id =", "self.payload ="}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int total = 0, next = 1;\n"
	        "    char marker = '\\x41';\n"
	        "    do { total += next; next++; } while (total < 3);\n"
	        "    for (int i = 0, j = 1; i < 2; i++, j++) { total += j; }\n"
	        "    for (total = 0, next = 1; total < 2; total++, next++) {}\n"
	        "    int seed = 5;\n"
	        "    for (int& left = seed, right = seed; left < 6; left++, right++) {}\n"
	        "    print(cast<int>(marker));\n"
	        "    print(total);\n"
	        "    print(next);\n"
	        "    print(seed);\n"
	        "}\n",
	        "65\n2\n3\n7\n",
	        "new core syntax",
	        {"static_cast<std::int8_t>(65)",
	            "do\n",
	            "while ((total < 3));",
	            "for (int i = 0, j = 1;",
	            "(total = 0), (next = 1)",
	            "for (int &left = seed, &right = seed;"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class GroupCell {\n"
	        "public:\n"
	        "    int value;\n"
	        "    GroupCell(int initial) { value = initial; }\n"
	        "    int read() { return value; }\n"
	        "}\n"
	        "GroupCell make_group_cell(int initial) { return GroupCell(initial); }\n"
	        "int read_block_shadow() {\n"
	        "    GroupCell item = GroupCell(1);\n"
	        "    {\n"
	        "        GroupCell item = make_group_cell(42), spare = make_group_cell(0);\n"
	        "        return item.read();\n"
	        "    }\n"
	        "}\n"
	        "int read_for_shadow() {\n"
	        "    GroupCell item = GroupCell(1);\n"
	        "    for (GroupCell item = make_group_cell(7), spare = make_group_cell(0);\n"
	        "         item.read() == 7;) {\n"
	        "        return item.read();\n"
	        "    }\n"
	        "    return item.read();\n"
	        "}\n"
	        "int read_foreach_shadow() {\n"
	        "    GroupCell item = GroupCell(1);\n"
	        "    array<GroupCell> items = {make_group_cell(9)};\n"
	        "    for (GroupCell item : items) { return item.read(); }\n"
	        "    return item.read();\n"
	        "}\n"
	        "int read_catch_shadow() {\n"
	        "    GroupCell item = GroupCell(1);\n"
	        "    try { throw make_group_cell(11); }\n"
	        "    catch (GroupCell item) { return item.read(); }\n"
	        "    return item.read();\n"
	        "}\n"
	        "void main() {\n"
	        "    print(read_block_shadow());\n"
	        "    print(read_for_shadow());\n"
	        "    print(read_foreach_shadow());\n"
	        "    print(read_catch_shadow());\n"
	        "}\n",
	        "42\n7\n9\n11\n",
	        "loop, catch, and declaration-group bindings shadow stack class locals",
	        {"vs_GroupCell item{};",
	            "std::shared_ptr<vs_GroupCell> item = vs_make_group_cell_",
	            "for (std::shared_ptr<vs_GroupCell> item = vs_make_group_cell_",
	            "for (std::shared_ptr<vs_GroupCell> item : items)",
	            " = (item); return VsRequireObject(__vs_eval_"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class SourceCell {\n"
	        "public:\n"
	        "    int value;\n"
	        "    SourceCell(int initial) { value = initial; }\n"
	        "    int source() { return value; }\n"
	        "}\n"
	        "class TargetCell {\n"
	        "public:\n"
	        "    int value;\n"
	        "    TargetCell(int initial) { value = initial; }\n"
	        "    int read() { return value; }\n"
	        "}\n"
	        "int read_shadow_initializer() {\n"
	        "    SourceCell item = SourceCell(13);\n"
	        "    {\n"
	        "        TargetCell item = TargetCell(item.source());\n"
	        "        return item.read();\n"
	        "    }\n"
	        "}\n"
	        "int read_for_shadow_initializer() {\n"
	        "    SourceCell item = SourceCell(17);\n"
	        "    for (TargetCell item = TargetCell(item.source()),\n"
	        "         spare = TargetCell(item.read()); item.read() == 17;) {\n"
	        "        return spare.read();\n"
	        "    }\n"
	        "    return 0;\n"
	        "}\n"
	        "int read_reference_shadow_initializer() {\n"
	        "    int value = 23;\n"
	        "    {\n"
	        "        const int& value = value;\n"
	        "        return value;\n"
	        "    }\n"
	        "}\n"
	        "int read_for_reference_shadow_initializer() {\n"
	        "    int value = 29;\n"
	        "    for (const int& value = value; value == 29;) {\n"
	        "        return value;\n"
	        "    }\n"
	        "    return 0;\n"
	        "}\n"
	        "void main() {\n"
	        "    print(read_shadow_initializer());\n"
	        "    print(read_for_shadow_initializer());\n"
	        "    print(read_reference_shadow_initializer());\n"
	        "    print(read_for_reference_shadow_initializer());\n"
	        "}\n",
	        "13\n17\n23\n29\n",
	        "shadowing declarations evaluate initializers in the outer environment",
	        {"auto&& ___vs_initializer_",
	            "std::forward<decltype(___vs_initializer_",
	            "vs_TargetCell_new(",
	            " = (item); return VsRequireObject(__vs_eval_"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct DefaultToken {\n"
	        "    int value = 3;\n"
	        "    DefaultToken() { value = 4; }\n"
	        "}\n"
	        "struct GenericFieldBox<T> {\n"
	        "    const T value;\n"
	        "    GenericFieldBox(T input) : value(input) {}\n"
	        "}\n"
	        "struct FieldInitDefaults {\n"
	        "    const int explicit_zero;\n"
	        "    const int implicit_zero;\n"
	        "    const string explicit_text;\n"
	        "    int mutable_value;\n"
	        "    const DefaultToken token;\n"
	        "    FieldInitDefaults(int input) : token(), mutable_value(input), explicit_text(), explicit_zero() {}\n"
	        "}\n"
	        "void main() {\n"
	        "    GenericFieldBox<int> box = GenericFieldBox<int>(7);\n"
	        "    FieldInitDefaults defaults = FieldInitDefaults(9);\n"
	        "    print(box.value);\n"
	        "    print(defaults.explicit_zero);\n"
	        "    print(defaults.implicit_zero);\n"
	        "    print(len(defaults.explicit_text));\n"
	        "    print(defaults.mutable_value);\n"
	        "    print(defaults.token.value);\n"
	        "}\n",
	        "7\n0\n0\n0\n9\n4\n",
	        "constructor field initializer defaults match native C++ AOT",
	        {"self.explicit_zero =", "self.mutable_value =", "vs_DefaultToken_init(self.token,"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "fn()->int global_getter = fn() -> int {\n"
	        "    int local = 42;\n"
	        "    return local;\n"
	        "};\n"
	        "void main() { print(global_getter()); }\n",
	        "42\n",
	        "top-level lambda keeps automatic local storage",
	        {"thread_local vs_Function<int()> Value", "int local = 42;"},
	        {},
	        {"thread_local int local"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int initialize_global() { print(\"global\"); return 5; }\n"
	        "int state = initialize_global();\n"
	        "void main() { print(\"main\"); }\n",
	        "global\nmain\n",
	        "top-level globals initialize before main in declaration order",
	        {"static void vs_ensure_globals()", "(void)vs_global_state"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct GlobalRecord { int value = 7; };\n"
	        "namespace GlobalState {\n"
	        "    GlobalRecord record = GlobalRecord();\n"
	        "    optional<GlobalRecord> maybe = optional<GlobalRecord>(GlobalRecord());\n"
	        "    fn()->int getter = fn() -> int { return record.value; };\n"
	        "    int read() { return getter(); }\n"
	        "}\n"
	        "using namespace GlobalState;\n"
	        "void main() {\n"
	        "    print(read());\n"
	        "    print(getter());\n"
	        "    print(GlobalState.record.value);\n"
	        "    print(GlobalState.maybe.value.value);\n"
	        "    GlobalState.record.value = 9;\n"
	        "    print(getter());\n"
	        "}\n",
	        "7\n7\n7\n7\n9\n",
	        "namespaced globals and callable values",
	        {"static vs_GlobalRecord& vs_global_GlobalState_2Erecord",
	            "vs_global_GlobalState_2Egetter"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    std::environment::setenv(\"VERO_AOT_环境\", \"缓存/测试\");\n"
	        "    print(std::environment::getenv(\"VERO_AOT_环境\"));\n"
	        "    std::environment::setenv(\"VERO_AOT_环境\", \"\");\n"
	        "    print(std::environment::getenv(\"VERO_AOT_环境\"));\n"
	        "}\n",
	        "缓存/测试\n\n",
	        "UTF-8 environment parity",
	        {"VsEnvironmentMutex", "GetEnvironmentVariableW"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Box<T, U = int> { public: T first; U second; }\n"
	        "template<T, U = array<T>> struct Values { U items; }\n"
	        "template<T = int, U = T> U make_value() { return 6; }\n"
	        "template<T, U = int> U select_value(T ignored, U value) { return value; }\n"
	        "void main() {\n"
	        "    Box<string> box = Box<string>();\n"
	        "    box.first = \"aot\"; box.second = 4;\n"
	        "    Values<int> values = Values<int>(); values.items.push(5);\n"
	        "    int generated = make_value();\n"
	        "    print(box.first); print(box.second + values.items[0]);\n"
	        "    print(generated); print(select_value<int>(7, \"deduced\"));\n"
	        "}\n",
	        "aot\n9\n6\ndeduced\n",
	        "generic default type arguments",
	        {"template <typename T, typename U = int>", "vs_Box<T, U>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Point {\n"
	        "    int value = 1;\n"
	        "    Point() {}\n"
	        "    Point(int in_value) { value = in_value; }\n"
	        "}\n"
	        "Point make_point(int value) { return Point(value); }\n"
	        "void main() {\n"
	        "    Point default_point();\n"
	        "    Point value_point(7);\n"
	        "    int scalar_value(8);\n"
	        "    int scalar_zero();\n"
	        "    print(default_point.value);\n"
	        "    print(value_point.value);\n"
	        "    print(scalar_value);\n"
	        "    print(scalar_zero);\n"
	        "    print(make_point(9).value);\n"
	        "}\n",
	        "1\n7\n8\n0\n9\n",
	        "direct constructor initialization",
	        {"vs_Point", "vs_make_point"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class AbstractBase<T> {\n"
	        "public:\n"
	        "    virtual T Read() const = 0;\n"
	        "}\n"
	        "class StillAbstract<T> : public AbstractBase<T> {}\n"
	        "void main() { print(1); }\n",
	        "1\n",
	        "inherited abstract generic has no factory",
	        {"struct vs_StillAbstract : virtual public VsObjectBase, public vs_AbstractBase<T>"},
	        {},
	        {"vs_StillAbstract_new"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Box<T> {\n"
	        "    T value;\n"
	        "    Box() = default;\n"
	        "    Box(string text) = delete;\n"
	        "    ~Box() = default;\n"
	        "    T get() const { return value; }\n"
	        "    T get(int unused) const = delete;\n"
	        "    Box operator+(string text) = delete;\n"
	        "}\n"
	        "int removed(int value) = delete;\n"
	        "void main() { Box<int> box(); print(box.get()); }\n",
	        "0\n",
	        "defaulted and deleted declarations",
	        {"vs_Box", "= delete", "vs_script_main"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "enum class Color { Green = 2 }\n"
	        "void main() {\n"
	        "    int64 i = 9223372036854775807i64;\n"
	        "    uint64 u = 18446744073709551615u64;\n"
	        "    Color c = Color.Green;\n"
	        "    print(i); print(u); print(c);\n"
	        "    print(40i64 + 2i64); print(to_int64(42)); print(to_uint64(42U));\n"
	        "    switch (i) { case 9223372036854775807i64: print(7); }\n"
	        "}\n",
	        "9223372036854775807\n18446744073709551615\nColor(2)\n42\n42\n42\n7\n",
	        "int64 uint64 and strong enum",
	        {"std::int64_t", "std::uint64_t", "VsCheckedAdd", "VsToInt64",
	            "VsToUInt64", "enum class vs_Color", "Color("}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int select_signed(int value) { return 1; }\n"
	        "int select_signed(int64 value) { return 2; }\n"
	        "int select_unsigned(unsigned int value) { return 3; }\n"
	        "int select_unsigned(uint64 value) { return 4; }\n"
	        "void main() {\n"
	        "    int64 wide = 40i64;\n"
	        "    int divisor = 2;\n"
	        "    uint64 unsigned_wide = 12u64;\n"
	        "    print(select_signed(wide + divisor));\n"
	        "    print(select_signed(min(wide, 50)));\n"
	        "    print(select_unsigned(~unsigned_wide));\n"
	        "    print(select_unsigned(max(unsigned_wide, 4u64)));\n"
	        "    print(wide / divisor);\n"
	        "    wide /= divisor;\n"
	        "    print(wide);\n"
	        "    print(unsigned_wide / 3u64);\n"
	        "    print(5.0 / 2.0);\n"
	        "    print(\"wide=\" + 9223372036854775807i64);\n"
	        "}\n",
	        "2\n2\n4\n4\n20\n20\n4\n2.5\nwide=9223372036854775807\n",
	        "wide numeric inference and checked arithmetic parity",
	        {"VsCheckedDiv", "static_cast<std::int64_t>",
	            "static_cast<std::uint64_t>", "static_cast<double>",
	            "VsMin(", "VsMax("}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int select_float(float value) { return 32; }\n"
	        "int select_float(double value) { return 64; }\n"
	        "int select_wide(long value) { return 64; }\n"
	        "int select_wide(double value) { return 128; }\n"
	        "int select_unsigned_wide(unsigned long value) { return 64; }\n"
	        "int select_unsigned_wide(double value) { return 128; }\n"
	        "void main() {\n"
	        "    float value = 1.25f;\n"
	        "    int64 signed_wide = 7i64;\n"
	        "    uint64 unsigned_wide = 8u64;\n"
	        "    print(select_float(-value));\n"
	        "    print(select_float(-1.0f));\n"
	        "    print(select_float(value / 2.0f));\n"
	        "    print(select_float(-1.0));\n"
	        "    print(select_float(.5f));\n"
	        "    print(select_float(.5));\n"
	        "    print(.5e2);\n"
	        "    print(select_wide(signed_wide));\n"
	        "    print(select_unsigned_wide(unsigned_wide));\n"
	        "}\n",
	        "32\n32\n32\n64\n32\n64\n50\n64\n64\n",
	        "floating and fixed-width overload parity",
	        {"float", "double", "vs_select_float", "VsCheckedDiv"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() { assert(true, \"AOT assertion detail\"); print(1); }\n",
	        "1\n",
	        "assert optional message AOT parity",
	        {"VsAssert(", "VsScriptFail", "AOT assertion detail"},
	        {},
	        {"std::abort()"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "enum class Color { Red = 1, Green = 2 }\n"
	        "void main() {\n"
	        "    map<Color,int> scores = map<Color,int>();\n"
	        "    scores[Color.Red] = 10;\n"
	        "    scores[Color.Green] = 20;\n"
	        "    print(scores[Color.Red]); print(scores.has(Color.Green));\n"
	        "    set<Color> selected = set<Color>();\n"
	        "    selected.add(Color.Red); print(selected.has(Color.Red));\n"
	        "    ordered_map<Color,int> ordered = ordered_map<Color,int>();\n"
	        "    ordered[Color.Green] = 21; ordered[Color.Red] = 11;\n"
	        "    print(ordered.keys()[0]); print(ordered.values()[1]);\n"
	        "}\n",
	        "10\ntrue\ntrue\nColor(2)\n11\n",
	        "strong enum container keys",
	        {"vs_Map<vs_Color", "vs_Set<vs_Color",
	            "vs_OrderedMap<vs_Color"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int add(int a, int b) { return a + b; }\n"
	        "void main() { print(add(1, 2)); }\n",
	        "3\n",
	        "add + forward decl",
	        {"vs_add_m", "VsCheckedAdd", "vs_script_main"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int n = 0;\n"
	        "    if (1 < 2) { n = 7; } else { n = 1; }\n"
	        "    print(n);\n"
	        "    int i = 0;\n"
	        "    while (i < 3) { i = i + 1; }\n"
	        "    print(i);\n"
	        "    int s = 0;\n"
	        "    for (int j = 0; j < 4; j = j + 1) { s = s + j; }\n"
	        "    print(s);\n"
	        "}\n",
	        "7\n3\n6\n",
	        "control flow",
	        {"if (", "while (", "for ("}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    array<int> xs = {1, 2, 3};\n"
	        "    print(len(xs));\n"
	        "    print(xs[1]);\n"
	        "    string a = \"hi\";\n"
	        "    string b = a + \"!\";\n"
	        "    print(b);\n"
	        "    print(true);\n"
	        "    print(\"n=\" + 3);\n"
	        "}\n",
	        "3\n2\nhi!\ntrue\nn=3\n",
	        "array + string + bool print",
	        {"#include <vector>", "vs_Array<int>", "VsStr(", "true"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Point {\n"
	        "    int value;\n"
	        "    Point(int input) { value = input; }\n"
	        "}\n"
	        "using Ints = array<int>;\n"
	        "using Scores = map<string,int>;\n"
	        "struct AliasHolder {\n"
	        "    Ints values = {37, 41};\n"
	        "    Scores scores = {\"value\": 43};\n"
	        "}\n"
	        "Ints global_alias_values = {47, 53};\n"
	        "Scores global_alias_scores = {\"value\": 59};\n"
	        "Point make_point() { return Point{7}; }\n"
	        "array<int> make_values() { return array<int>{2, 3, 5}; }\n"
	        "int sum(array<int> values) {\n"
	        "    int result = 0;\n"
	        "    for (int i = 0; i < len(values); i = i + 1) { result += values[i]; }\n"
	        "    return result;\n"
	        "}\n"
	        "void main() {\n"
	        "    Point point = make_point();\n"
	        "    array<int> values = make_values();\n"
	        "    map<string,int> scores = map<string,int>{\"ok\": 11};\n"
	        "    Ints alias_values = Ints{13, 17};\n"
	        "    Scores alias_scores = {\"answer\": 23};\n"
	        "    using LocalInts = array<int>;\n"
	        "    LocalInts local_values = LocalInts{29, 31};\n"
	        "    AliasHolder alias_holder = AliasHolder{};\n"
	        "    print(point.value);\n"
	        "    print(sum(array<int>{4, 6}));\n"
	        "    print(values[2]);\n"
	        "    print(scores[\"ok\"]);\n"
	        "    print(Point{17}.value);\n"
	        "    print(array<int>{8, 19}[1]);\n"
	        "    print(alias_values[1]);\n"
	        "    print(alias_scores[\"answer\"]);\n"
	        "    print(local_values[1]);\n"
	        "    print(alias_holder.values[1]);\n"
	        "    print(alias_holder.scores[\"value\"]);\n"
	        "    print(global_alias_values[1]);\n"
	        "    print(global_alias_scores[\"value\"]);\n"
	        "}\n",
	        "7\n10\n5\n11\n17\n19\n17\n23\n31\n41\n43\n53\n59\n",
	        "typed brace construction expressions",
	        {"vs_Point_new", "vs_Array<int>{", "vs_Map<std::string, int>{"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "enum class Big : int64 { Small = 1, Huge = 9223372036854775807i64 }\n"
	        "enum Wide : int64 { WideHuge = 9223372036854775807i64 }\n"
	        "void main() {\n"
	        "    Big value = Big.Huge;\n"
	        "    print(value);\n"
	        "    print(WideHuge);\n"
	        "    switch (value) {\n"
	        "        case Big.Small: print(1); break;\n"
	        "        case Big.Huge: print(2); break;\n"
	        "    }\n"
	        "}\n",
	        "Big(9223372036854775807)\n9223372036854775807\n2\n",
	        "int64 enum ABI",
	        {"enum class vs_Big : std::int64_t",
	            "static_cast<std::int64_t>(Value)",
	            "vs_Wide_WideHuge = 9223372036854775807"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    array<int> a = array<int>();\n"
	        "    a.reserve(24);\n"
	        "    print(a.capacity() >= 24);\n"
	        "    array<int> b = a;\n"
	        "    b.push(7);\n"
	        "    print(a[0]); print(a == b);\n"
	        "    array<int> returned = a.push(8);\n"
	        "    returned.clear();\n"
	        "    print(len(a));\n"
	        "    map<string,int> m = map<string,int>();\n"
	        "    m.reserve(24);\n"
	        "    map<string,int> n = m;\n"
	        "    n[\"answer\"] = 42;\n"
	        "    print(m[\"answer\"]); print(m == n);\n"
	        "    m[\"answer\"] += 1;\n"
	        "    print(n[\"answer\"]);\n"
	        "    set<int> s = set<int>();\n"
	        "    s.reserve(24);\n"
	        "    set<int> t = s;\n"
	        "    t.add(9);\n"
	        "    print(s.has(9)); print(s == t);\n"
	        "    set<int> u = t.add(10);\n"
	        "    print(u.has(10));\n"
	        "    queue<int> q = queue<int>();\n"
	        "    queue<int> qr = q.push(3);\n"
	        "    print(qr.front());\n"
	        "    queue<int> qc = qr.clear();\n"
	        "    qc.push(4);\n"
	        "    print(q.front());\n"
	        "    stack<int> st = stack<int>();\n"
	        "    stack<int> sr = st.push(5);\n"
	        "    print(sr.top());\n"
	        "    deque<int> d = deque<int>();\n"
	        "    deque<int> dr = d.push_front(6);\n"
	        "    print(dr.front());\n"
	        "    int compound = 2;\n"
	        "    compound += 3;\n"
	        "    print(compound);\n"
	        "}\n",
	        "true\n7\ntrue\n0\n42\ntrue\n43\ntrue\ntrue\ntrue\n3\n4\n5\n6\n5\n",
	        "container handles preserve VM alias and identity semantics",
	        {"class vs_Array", "void reserve(std::int64_t capacity)",
	            "class vs_Map", "class vs_Set", "class vs_Queue",
	            "class vs_Stack", "class vs_Deque", "get_or_insert"}))
	{
		return EXIT_FAILURE;
	}

	{
		namespace fs = std::filesystem;
		const FScopedTempDirectory TempDirectory("veroscript_aot_import");
		const fs::path& Dir = TempDirectory.GetPath();
		const fs::path LibPath = Dir / "lib.vs";
		{
			std::ofstream Lib(LibPath);
			Lib << "export int triple(int x) { return x + x + x; }\n";
		}
		const std::string MainSource =
		    "import \"lib.vs\";\n"
		    "void main() { print(triple(3)); }\n";
		if (!Expect(
		        VeroScript::EmitCppFromSource(
		            MainSource,
		            "main_import.vs",
		            Cpp,
		            Error,
		            {Dir.string()}),
		        "import emit") ||
		    !Expect(Cpp.find("vs_triple(") != std::string::npos, "import has triple") || !Expect(Cpp.find("vs_script_main") != std::string::npos, "import has main"))
		{
			std::cerr << Error << '\n'
			          << Cpp;
			return EXIT_FAILURE;
		}

		VeroScript::VeroScriptModuleLoader Loader("main_import.vs");
		Loader.AddImportPath(Dir.string());
		Loader.SetSourceOverlay("main_import.vs", MainSource);
		std::unique_ptr<VeroScript::FProgram> Program =
		    Loader.LoadSourceModule(MainSource, "main_import.vs");
		if (!Expect(Program && !Loader.HasError(), "import load"))
		{
			std::cerr << Loader.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (!Expect(Module && !Compiler.HasError(), "import compile"))
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return EXIT_FAILURE;
		}
		std::ostringstream Output;
		std::istringstream In;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, In, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(
		        Vm.Execute(*Module, VeroScript::ERunMode::Script),
		        "import vm") ||
		    !Expect(Output.str() == "9\n", "import vm output"))
		{
			std::cerr << Errors.str() << Output.str();
			return EXIT_FAILURE;
		}

		std::string NativeError;
		const ENativeDualResult Native = RunNativeCpp(Cpp, "9\n", NativeError);
		if (Native == ENativeDualResult::Failed)
		{
			std::cerr << "FAIL: import native DualRun\n"
			          << NativeError << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!Expect(
	        VeroScript::EmitCppFromSource(
	            "class C {}\n"
	            "void main() {}\n",
	            "class_ok.vs",
	            Cpp,
	            Error),
	        "emit empty class") ||
	    !Expect(Cpp.find("struct vs_C") != std::string::npos || Cpp.find("vs_C") != std::string::npos, "class struct present"))
	{
		std::cerr << Error << '\n'
		          << Cpp;
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int i = 0;\n"
	        "    i++;\n"
	        "    print(i);\n"
	        "    ++i;\n"
	        "    print(i);\n"
	        "    print(i++);\n"
	        "    print(i);\n"
	        "    print(++i);\n"
	        "    int j = 0;\n"
	        "    j--;\n"
	        "    print(j);\n"
	        "}\n",
	        "1\n2\n2\n3\n4\n-1\n",
	        "prefix/postfix ++/--",
	        {"VsCheckedAdd", "VsCheckedSub"}))
	{
		return EXIT_FAILURE;
	}

	if (!Expect(
	        VeroScript::EmitCppFromSource(
	            "void main() {\n"
	            "    int x = 2147483647;\n"
	            "    ++x;\n"
	            "}\n",
	            "inc_overflow.vs",
	            Cpp,
	            Error),
	        "emit int prefix inc") ||
	    !Expect(Cpp.find("VsCheckedAdd") != std::string::npos, "int ++ uses VsCheckedAdd"))
	{
		std::cerr << Error << '\n'
		          << Cpp;
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void f(int x) { print(x); }\n"
	        "void f(string s) { print(s); }\n"
	        "void main() {\n"
	        "    f(1);\n"
	        "    f(\"a\");\n"
	        "}\n",
	        "1\na\n",
	        "free-function overloads",
	        {"void vs_f_m", "VsPrint(", "(int x)", "(std::string s)"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "T id<T>(T x) { return x; }\n"
	        "void main() { print(id(7)); }\n",
	        "7\n",
	        "generic specialization",
	        {"template <typename", "vs_id"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Point {\n"
	        "    public:\n"
	        "    int x;\n"
	        "    int get() { return this.x; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Point p = Point();\n"
	        "    p.x = 9;\n"
	        "    print(p.get());\n"
	        "}\n",
	        "9\n",
	        "class AOT subset",
	        {"vs_Point", "(*this)", "std::shared_ptr<vs_Point>", "std::make_shared<vs_Point>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void mark<T>(T x) { print(x); }\n"
	        "void main() {\n"
	        "    mark(1);\n"
	        "    mark(\"a\");\n"
	        "}\n",
	        "1\na\n",
	        "generic specializations type-arg suffix",
	        {"template <typename", "vs_mark"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "T wrap<T>(T x) { return x; }\n"
	        "void main() {\n"
	        "    print(wrap<string>(\"z\"));\n"
	        "}\n",
	        "z\n",
	        "explicit type args specialize",
	        {"template <typename", "vs_wrap_m"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "using I = int;\n"
	        "void main() {\n"
	        "    I n = 4;\n"
	        "    print(n);\n"
	        "}\n",
	        "4\n",
	        "using / type alias",
	        {"using I = int"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    array<int> xs = {1, 2, 3};\n"
	        "    int s = 0;\n"
	        "    for (int x : xs) { s = s + x; }\n"
	        "    print(s);\n"
	        "}\n",
	        "6\n",
	        "array foreach",
	        {"for (", " : "}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "enum Color { Red, Green = 2, Blue }\n"
	        "void main() {\n"
	        "    Color c = Color.Green;\n"
	        "    switch (c) {\n"
	        "        case Color.Red:\n"
	        "            print(1);\n"
	        "        case Green:\n"
	        "            print(2);\n"
	        "        default:\n"
	        "            print(0);\n"
	        "    }\n"
	        "}\n",
	        "2\n",
	        "enum + switch",
	        {"vs_Color_Green", "switch (", "case ", "break;"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "enum class Color { Red, Green = 2, Blue }\n"
	        "void main() {\n"
	        "    Color c = Color.Green;\n"
	        "    switch (c) {\n"
	        "        case Color.Red:\n"
	        "            print(1);\n"
	        "        case Color.Green:\n"
	        "            print(2);\n"
	        "        default:\n"
	        "            print(0);\n"
	        "    }\n"
	        "}\n",
	        "2\n",
	        "enum class + switch",
	        {"enum class vs_Color", "vs_Color::Green", "switch ("}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int x = 1;\n"
	        "    switch (x) {\n"
	        "        case 1:\n"
	        "            print(1);\n"
	        "            fallthrough;\n"
	        "        case 2:\n"
	        "            print(2);\n"
	        "        default:\n"
	        "            print(0);\n"
	        "    }\n"
	        "}\n",
	        "1\n2\n",
	        "switch fallthrough",
	        {"switch (", "case 1", "case 2"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int n = 0;\n"
	        "again:\n"
	        "    n = n + 1;\n"
	        "    print(n);\n"
	        "    if (n >= 3) { return; }\n"
	        "    goto again;\n"
	        "}\n",
	        "1\n2\n3\n",
	        "goto + label",
	        {"goto again;", "again:"}))
	{
		return EXIT_FAILURE;
	}

	{
		namespace fs = std::filesystem;
		const FScopedTempDirectory TempDirectory("veroscript_aot_private_shadow");
		const fs::path& Dir = TempDirectory.GetPath();
		const fs::path LibPath = Dir / "shadow_lib.vs";
		{
			std::ofstream Lib(LibPath);
			Lib << "void tag() { print(\"lib\"); }\n";
		}
		const std::string MainSource =
		    "import \"shadow_lib.vs\";\n"
		    "void tag() { print(\"main\"); }\n"
		    "void main() { tag(); }\n";
		if (!DualRun(MainSource, "main\n", "private same-name prefers local module",
		        {"VsPrint("}, {Dir.string()}))
		{
			return EXIT_FAILURE;
		}
	}

	{
		namespace fs = std::filesystem;
		const FScopedTempDirectory TempDirectory("veroscript_aot_implicit_import");
		const fs::path& Dir = TempDirectory.GetPath();
		const fs::path LibPath = Dir / "shared.vs";
		{
			std::ofstream Lib(LibPath);
			Lib << "int project_value() { return 42; }\n";
		}
		VeroScript::FCppCodegenBackend Backend;
		VeroScript::FCodegenOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ImplicitImportPaths.push_back(LibPath.generic_string());
		VeroScript::FCodegenResult HiddenResult;
		const std::string MainPath = (Dir / "main.vs").generic_string();
		if (!Expect(
		        !Backend.EmitFromSource(
		            "void main() { print(project_value()); }\n",
		            MainPath,
		            Options,
		            HiddenResult),
		        "AOT implicit import preserves export visibility"))
		{
			return EXIT_FAILURE;
		}
		Options.bAllowProjectWideAccess = true;
		VeroScript::FCodegenResult ProjectResult;
		if (!Expect(
		        Backend.EmitFromSource(
		            "void main() { print(project_value()); }\n",
		            MainPath,
		            Options,
		            ProjectResult),
		        "AOT project-wide visibility matches runtime") ||
		    !Expect(
		        ProjectResult.Output.find("project_value") != std::string::npos,
		        "AOT emits implicitly imported project declaration"))
		{
			std::cerr << ProjectResult.Error << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		namespace fs = std::filesystem;
		const FScopedTempDirectory TempDirectory("veroscript_aot_file");
		const fs::path& Dir = TempDirectory.GetPath();
		const fs::path Script = Dir / "hello.vs";
		const fs::path OutCpp = Dir / "hello_gen.cpp";
		{
			std::ofstream File(Script);
			File << "void main() { print(\"aot-file\"); }\n";
		}
		if (!Expect(
		        VeroScript::EmitCppFromFile(
		            Script.string(),
		            OutCpp.string(),
		            Error),
		        "EmitCppFromFile") ||
		    !Expect(fs::exists(OutCpp), "output exists"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!DualRun(
	        "void bump(ref int x) { x = x + 1; }\n"
	        "void main() {\n"
	        "    int n = 5;\n"
	        "    bump(ref n);\n"
	        "    print(n);\n"
	        "}\n",
	        "6\n",
	        "ref param bump",
	        {"int& x", "vs_bump_maot"}))
	{
		return EXIT_FAILURE;
	}

	if (!CoreRuntimeRun(
	        "T identity<T>(T value) { return value; }\n"
	        "void bump_index(ref int value) { value++; }\n"
	        "void main() {\n"
	        "    array<int> values = {4};\n"
	        "    bump_index(ref values[identity<int>(0)]);\n"
	        "    print(values[0]);\n"
	        "}\n",
	        "5\n",
	        "generic specializations inside ref operands are collected",
	        {}, "requires managed storage paths"))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    array<int> xs = array<int>();\n"
	        "    xs.push(1);\n"
	        "    xs.push(2);\n"
	        "    int s = 0;\n"
	        "    for (int x : xs) { s = s + x; }\n"
	        "    print(to_int(3.7));\n"
	        "    print(s);\n"
	        "}\n",
	        "3\n3\n",
	        "dynamic array push foreach to_int",
	        {"vs_Array<int>", "push_back", "VsToInt", "for (int x : xs)"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    print(\"x={}\", 42);\n"
	        "    array<string> args = {\"array\"};\n"
	        "    print(\"x={}\", args);\n"
	        "    print(format(\"returned={}\", 7));\n"
	        "    print(format(\"returned={}\", args));\n"
	        "    print(\"align=[{:>4}] center=[{:^5}]\", 7, \"x\");\n"
	        "    print(\"precision={:.2} hex={:X} escape={{}}\", 1.5, 255);\n"
	        "    print(format(\"indexed={1}-{0}-{1}\", \"a\", \"b\"));\n"
	        "}\n",
	        "x=42\nx=array\nreturned=7\nreturned=array\n"
	        "align=[   7] center=[  x  ]\n"
	        "precision=1.50 hex=FF escape={}\n"
	        "indexed=b-a-b\n",
	        "print formatting",
	        {"VsPrintFormat", "VsFormatBrace", "VsFormatArray"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    print(formatf(\"d=%+06d x=%#08X f=%.2f\", -42, 255U, 3.14159));\n"
	        "    printf(\"s=%-5.3s c=%c %%\", \"abcdef\", 65);\n"
	        "    printf(\"chars=%c/%c\", \"7\", \"A\");\n"
	        "    printf(\"floats=[%#.0g]/[%.0g]/[%010.3e]/[% G]/[%-#12.4g]/[% f]\",\n"
	        "        12.0, 12.0, 1.5, 1.5, 12.0, -0.0);\n"
	        "    array<string> args = {\"7\", \"255\"};\n"
	        "    printf(\"array=%d/%x\", args);\n"
	        "}\n",
	        "d=-00042 x=0X0000FF f=3.14\ns=abc   c=A %\n"
	        "chars=\a/A\n"
	        "floats=[1.e+01]/[1e+01]/[01.500e+00]/[ 1.5]/[12.00       ]/"
	        "[-0.000000]\n"
	        "array=7/ff\n",
	        "formatf and printf",
	        {"VsFormatPrintf", "VsPrintfFormat", "VsFormatPrintfArray"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int nums[3] = {1, 2, 3};\n"
	        "    print(len(nums));\n"
	        "    print(nums[1]);\n"
	        "}\n",
	        "3\n2\n",
	        "native int[3] array",
	        {"#include <array>", "std::array<int, 3>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    map<string,int> m = map<string,int>();\n"
	        "    m[\"a\"] = 10;\n"
	        "    m[\"b\"] = 20;\n"
	        "    print(m.has(\"a\"));\n"
	        "    print(m[\"a\"]);\n"
	        "    int s = 0;\n"
	        "    for ((string k, int v) : m) { s = s + v; }\n"
	        "    print(s);\n"
	        "}\n",
	        "true\n10\n30\n",
	        "map has get foreach destructure",
	        {"std::unordered_map", ".count(", "___vs_kv"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Box {\n"
	        "    public:\n"
	        "    int v;\n"
	        "}\n"
	        "void main() {\n"
	        "    Box a = Box();\n"
	        "    a.v = 5;\n"
	        "    Box b = a;\n"
	        "    b.v = 9;\n"
	        "    print(a.v);\n"
	        "    Box c = nullptr;\n"
	        "    print(c == nullptr);\n"
	        "    c = a;\n"
	        "    print(c == nullptr);\n"
	        "    print(c.v);\n"
	        "}\n",
	        "9\ntrue\nfalse\n9\n",
	        "class RC (shared_ptr) + nullptr",
	        {"std::shared_ptr<vs_Box>", "nullptr"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Point {\n"
	        "    int x = 0;\n"
	        "    int y = 0;\n"
	        "}\n"
	        "void bump_x(handle<Point> h) {\n"
	        "    h.value.x = h.value.x + 1;\n"
	        "}\n"
	        "shared<T> keep_shared<T>(handle<T> owner) { return owner; }\n"
	        "shared<U> forward_shared<U>(handle<U> owner) { return keep_shared<U>(owner); }\n"
	        "shared<T> forward_same_shared<T>(handle<T> owner) { return keep_shared<T>(owner); }\n"
	        "bool same_shared<T>(shared<T> first, handle<T> second) { return first == second; }\n"
	        "int count_shared<T>(shared<T>... owners) { return len(owners); }\n"
	        "void clear_shared<T>(ref handle<T> owner) { owner = nullptr; }\n"
	        "int read_shared<T>(shared<T> owner, int extra = 0) { return to_int(owner.value) + extra; }\n"
	        "int sum_shared<T>(shared<T>... owners) { int total = 0; "
	        "for (shared<T> owner : owners) { total += to_int(owner.value); } return total; }\n"
	        "int sum_from<T>(shared<T> first, shared<T>... rest) { "
	        "return to_int(first.value) + sum_shared(rest[0], rest[1]); }\n"
	        "void main() {\n"
	        "    shared<Point> a = shared<Point>(Point());\n"
	        "    a.value.x = 10;\n"
	        "    handle<Point> empty = handle<Point>();\n"
	        "    empty = nullptr;\n"
	        "    print(empty == nullptr);\n"
	        "    handle<Point> b = a;\n"
	        "    assert(keep_shared(a) == b && keep_shared<Point>(a) == a);\n"
	        "    assert(forward_shared(a) == a && forward_same_shared(a) == a);\n"
	        "    assert(a.keep_shared() == a && a.keep_shared<Point>() == a);\n"
	        "    bump_x(b);\n"
	        "    print(a.value.x);\n"
	        "    print(b.value.x);\n"
	        "    std::shared<Point> inferred = std::shared(Point());\n"
	        "    auto inferred_scalar = shared(3); assert(inferred_scalar.value == 3);\n"
	        "    assert(inferred_scalar.keep_shared() == inferred_scalar && keep_shared(inferred_scalar) == inferred_scalar);\n"
	        "    assert(sum_shared(inferred_scalar, inferred_scalar, inferred_scalar) == 9);\n"
	        "    assert(inferred_scalar.sum_shared(inferred_scalar, inferred_scalar) == 9);\n"
	        "    assert(sum_shared<int>() == 0 && read_shared(inferred_scalar) == 3);\n"
	        "    assert(inferred_scalar.read_shared() == 3);\n"
	        "    assert(sum_from(inferred_scalar, inferred_scalar, inferred_scalar) == 9);\n"
	        "    assert(keep_shared<int>(nullptr) == nullptr);\n"
	        "    assert(!same_shared(nullptr, inferred_scalar) && !same_shared(inferred_scalar, nullptr));\n"
	        "    assert(!inferred_scalar.same_shared(nullptr));\n"
	        "    assert(count_shared<int>(nullptr, inferred_scalar) == 2);\n"
	        "    auto cleared = shared(5); cleared.clear_shared(); assert(cleared == nullptr);\n"
	        "    shared<double> widened = shared(7); assert(widened.value == 7.0);\n"
	        "    auto inferred_nested = shared(shared(2)); assert(inferred_nested.value.value == 2);\n"
	        "    shared<shared<int>> contextual_nested = shared(shared(3)); assert(contextual_nested.value.value == 3);\n"
	        "    shared<Point> copied = inferred.value; inferred.value.x = 9; assert(copied.value.x == 0);\n"
	        "    inferred.value.x = 4; assert(inferred.value.x == 4);\n"
	        "    std::handle<Point> same = b; assert(same == a); same = nullptr;\n"
	        "    weak<shared<Point>> observer = weak<handle<Point>>(a);\n"
	        "    handle<Point> locked = observer.lock();\n"
	        "    a = nullptr; b = nullptr; assert(!observer.expired());\n"
	        "    assert(locked.value.x == 11); locked = nullptr;\n"
	        "    assert(observer.expired()); assert(observer.lock() == nullptr);\n"
	        "}\n",
	        "true\n11\n11\n",
	        "handle<T> share + .value mutate",
	        {"vs_SoftHandle", "std::shared_ptr<vs_SoftHandle<vs_Point>>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class handle { public: int value; handle(int n) { value = n + 1; } }\n"
	        "void main() { auto local = handle(2); assert(local.value == 3);\n"
	        "    shared<int> owner = shared<int>(7); handle<int> alias = owner;\n"
	        "    assert(alias == owner && owner.value == 7); print(\"coexist-ok\"); }\n",
	        "coexist-ok\n", "non-generic handle and builtin handle<T> coexist",
	        {"vs_handle_new(2)", "std::shared_ptr<vs_SoftHandle<int>>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Vec {\n"
	        "    int x;\n"
	        "}\n"
	        "Vec operator+(Vec a, Vec b) {\n"
	        "    Vec r = Vec();\n"
	        "    r.x = a.x + b.x;\n"
	        "    return r;\n"
	        "}\n"
	        "void main() {\n"
	        "    Vec a = Vec();\n"
	        "    a.x = 3;\n"
	        "    Vec b = Vec();\n"
	        "    b.x = 4;\n"
	        "    Vec c = a + b;\n"
	        "    print(c.x);\n"
	        "}\n",
	        "7\n",
	        "free function operator+ overload",
	        {"vs_operator__2B"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(R"vs(
struct Value { public: int n = 3; };
Value value = Value();
int mutate() { value.n = 9; return 1; }
int operator+(Value v, int n) { return v.n; }
class Member { public: bool operator<(Value v) { return v.n == 3; } };
class Free {};
bool operator<(Free f, Value v) { return v.n == 3; }
Member right_member() { value.n = 9; return Member(); }
Free right_free() { value.n = 9; return Free(); }
class Receiver { public: int n; Receiver(int v) { n = v; } int operator+(int v) { return n; } };
Receiver receiver = Receiver(3);
int replace_receiver() { receiver = Receiver(9); return 1; }
void main() {
    print(value + mutate()); value.n = 3;
    print(value > right_member()); value.n = 3;
    print(value > right_free());
    print(receiver + replace_receiver());
    Value __vs_op_left = Value(); int __vs_op_right = 2;
    print(__vs_op_left + __vs_op_right);
}
)vs",
	        "3\ntrue\ntrue\n3\n3\n", "operator value snapshots and receiver evaluation order",
	        {"__vs_op_left", "__vs_op_right"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(R"vs(
interface Readable { virtual int get() = 0; };
struct Value : Readable { public: int n = 3; int get() { return n; } };
class Base<T> where T : Readable {
public:
    int total = 0;
    int operator+(T v) { return v.get(); }
    int operator[](T v) { return v.get(); }
    void operator+=(T v) { total += v.get(); }
    bool operator<(T v) { return v.get() == 3; }
};
class Receiver : Base<Readable> {};
Value value = Value();
Receiver receiver = Receiver();
Receiver mutate() { value.n = 9; return receiver; }
class Numeric {
public:
    int total = 0;
    int operator+(char n) { return n; }
    int operator[](char n) { return n; }
    void operator+=(char n) { total = n; }
};
void main() {
    print(receiver + value);
    print(receiver[value]);
    receiver += value; print(receiver.total);
    print(value > mutate());
    Numeric numeric = Numeric(); int n = 12;
    print(numeric + n); print(numeric[n]);
    numeric += n; print(numeric.total);
}
)vs",
	        "3\n3\n3\ntrue\n12\n12\n12\n", "member operator conversions use the declaring generic owner",
	        {"VsCheckedNumericCast<std::int8_t>", "_box_"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int calls = 0; export class Printed {}; "
	        "Printed make() { calls++; return Printed(); } "
	        "void main() { print(make()); print(calls); }",
	        "Printed\n1\n", "printing a user value evaluates its expression once",
	        {"static_cast<void>("}, {}, {}, "Printed{...}\n1\n"))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Counter {\n"
	        "    int n;\n"
	        "    void operator+=(int d) { this.n = this.n + d; }\n"
	        "    bool operator==(Counter o) { return this.n == o.n; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Counter a = Counter();\n"
	        "    a.n = 1;\n"
	        "    a += 4;\n"
	        "    Counter b = Counter();\n"
	        "    b.n = 5;\n"
	        "    print(a.n);\n"
	        "    print(a == b);\n"
	        "    print(a != b);\n"
	        "}\n",
	        "5\ntrue\nfalse\n",
	        "method operator+= and != from ==",
	        {"operator__2B_3D", "operator__3D_3D"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Vec {\n"
	        "    public:\n"
	        "    int x;\n"
	        "    Vec(int v) { x = v; }\n"
	        "    bool operator<(Vec other) { return x < other.x; }\n"
	        "    Vec operator+(int n) { return Vec(x + n); }\n"
	        "    Vec operator-() { return Vec(0 - x); }\n"
	        "}\n"
	        "void main() {\n"
	        "    print(Vec(3) > Vec(1));\n"
	        "    print(Vec(1) > Vec(3));\n"
	        "    print((1 + Vec(2)).x);\n"
	        "    print((-Vec(5)).x);\n"
	        "}\n",
	        "true\nfalse\n3\n-5\n",
	        "AOT class operator mirror/right-member/unary",
	        {"operator__3C", "operator__2B", "operator__2D"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    fn(int)->int inc = fn(int x) -> int => x + 1;\n"
	        "    print(inc(41));\n"
	        "    fn()->void greet = fn() -> void { print(\"hi\"); };\n"
	        "    greet();\n"
	        "}\n",
	        "42\nhi\n",
	        "lambda literal + fn variable call",
	        {"vs_Function<int(int)>", "[="}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int result = 0;\n"
	        "    {\n"
	        "        fn(int)->int print = fn(int x) -> int => x + 1;\n"
	        "        result = print(41);\n"
	        "    }\n"
	        "    print(result);\n"
	        "}\n",
	        "42\n",
	        "single-argument print local function shadowing",
	        {"vs_Function<int(int)> print", "41"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int total = 0;\n"
	        "    for (int print = 0; print < 2; ++print) { total = total + print; }\n"
	        "    print(total);\n"
	        "    array<int> values = {3};\n"
	        "    for (int print : values) { total = total + print; }\n"
	        "    print(total);\n"
	        "}\n",
	        "1\n4\n",
	        "loop binding scope does not shadow later builtins",
	        {"for (int print = 0", "for (int print : values)", "VsPrint(total)"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int apply(fn(int)->int g, int x) {\n"
	        "    return g(x);\n"
	        "}\n"
	        "void main() {\n"
	        "    print(apply(fn(x) => x + 1, 41));\n"
	        "}\n",
	        "42\n",
	        "contextual lambda from free-function argument",
	        {"vs_Function<int(int)>", "[="}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Holder {\n"
	        "    public:\n"
	        "    fn(int)->int bind(fn(int)->int f) { return f; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Holder h = Holder();\n"
	        "    fn(int)->int f = h.bind(fn(x) => x + 1);\n"
	        "    print(f(41));\n"
	        "}\n",
	        "42\n",
	        "contextual lambda from method argument",
	        {"vs_Function<int(int)>", "[="}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    fn(int)->int f = fn(x) => x * 2;\n"
	        "    print(f(21));\n"
	        "}\n",
	        "42\n",
	        "contextual lambda from var type",
	        {"vs_Function<int(int)>", "[="}))
	{
		return EXIT_FAILURE;
	}

	{
		const std::vector<std::string> StdlibImportPaths;
		const std::string EventSource =
		    "import \"delegate.vs\";\n"
		    "struct CaptureState { public: int n=1; };\n"
		    "int constant() { return 7; }\n"
		    "void quiet() {}\n"
		    "fn()->void make_handler(int value) {\n"
		    "    return fn() -> void => print(value);\n"
		    "}\n"
		    "void main() {\n"
		    "    multicast_delegate onClick = multicast_delegate();\n"
		    "    fn()->void h1 = fn() -> void => print(1);\n"
		    "    fn()->void h2 = fn() -> void { print(2); };\n"
		    "    onClick += h1;\n"
		    "    onClick += h2;\n"
		    "    onClick.broadcast();\n"
		    "    onClick -= h1;\n"
		    "    onClick.broadcast();\n"
		    "    assert(onClick.count() == 1);\n"
		    "\n"
		    "    delegate<int, int> mitigate = delegate<int, int>();\n"
		    "    mitigate.bind(fn(int dmg) -> int => dmg / 2);\n"
		    "    print(mitigate.is_bound());\n"
		    "    print(mitigate.execute(10));\n"
		    "    print(mitigate.execute_if_bound(8));\n"
		    "    mitigate.unbind();\n"
		    "    print(mitigate.execute_if_bound(8));\n"
		    "\n"
		    "    delegate<int, int, int, int> sum3 = delegate<int, int, int, int>();\n"
		    "    sum3.bind(fn(int a, int b, int c) -> int => a + b + c);\n"
		    "    print(sum3.execute(1, 2, 3));\n"
		    "\n"
		    "    multicast_delegate<int, int, int> onTriple = multicast_delegate<int, int, int>();\n"
		    "    fn(int, int, int)->void h3 = fn(int a, int b, int c) -> void => print(a + b + c);\n"
		    "    onTriple.add(h3);\n"
		    "    onTriple.broadcast(1, 2, 3);\n"
		    "\n"
		    "    multicast_delegate selected = multicast_delegate();\n"
		    "    fn()->void first = make_handler(4);\n"
		    "    fn()->void second = make_handler(5);\n"
		    "    selected.add(first);\n"
		    "    selected.add(second);\n"
		    "    selected.remove(second);\n"
		    "    selected.broadcast();\n"
		    "    array<int> captured = array<int>();\n"
		    "    delegate<void> mutate = delegate<void>();\n"
		    "    mutate.bind(fn() mutable ->void { captured.push(7); });\n"
		    "    mutate.execute();\n"
		    "    assert(len(captured) == 1 && captured[0] == 7);\n"
		    "    delegate<int> named = delegate<int>();\n"
		    "    named.bind(constant);\n"
		    "    assert(named.execute() == 7);\n"
		    "    fn()->void same_a = quiet;\n"
		    "    fn()->void same_b = quiet;\n"
		    "    assert(same_a == same_b);\n"
		    "    multicast_delegate named_listeners = multicast_delegate();\n"
		    "    named_listeners += quiet;\n"
		    "    named_listeners -= quiet;\n"
		    "    assert(named_listeners.count() == 0);\n"
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
		    "    delegate<int> self_unbind = delegate<int>();\n"
		    "    self_unbind.bind(fn() mutable ->int { self_unbind.unbind(); return 7; });\n"
		    "    assert(self_unbind.execute() == 7 && !self_unbind.is_bound());\n"
		    "}\n";
		const std::string ExpectedOutput = "1\n2\n2\ntrue\n5\ntrue\nfalse\n6\n6\n4\n";
		const char* Label = "events: multicast_delegate + delegate (import delegate.vs)";
		if (!DualRun(
		        EventSource,
		        ExpectedOutput,
		        Label,
		        {"vs_MulticastDelegate0", "vs_Delegate1", "vs_Delegate3",
		            "vs_MulticastDelegate3", "execute_if_bound"},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
	}

	// ---- W5 OOP: virtual / interface / struct boxing
	if (!DualRun(
	        "class Base {\n"
	        "    public:\n"
	        "    virtual int get() { return 1; }\n"
	        "}\n"
	        "class Der : public Base{\n"
	        "    public:\n"
	        "    int get() override { return 2; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Base b = Der();\n"
	        "    print(b.get());\n"
	        "}\n",
	        "2\n",
	        "W5 virtual override",
	        {"struct vs_Base", "struct vs_Der", "virtual", "override"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Base {\n"
	        "    public:\n"
	        "    virtual int get() final { return 9; }\n"
	        "}\n"
	        "class Leaf final : public Base {\n"
	        "    public:\n"
	        "    int extra() { return 1; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Leaf x = Leaf();\n"
	        "    print(x.get());\n"
	        "    print(x.extra());\n"
	        "}\n",
	        "9\n1\n",
	        "W5 final class and method",
	        {"struct vs_Leaf final", " final"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface ISpeak { virtual void speak() = 0; }\n"
	        "class Dog : public ISpeak{\n"
	        "    public:\n"
	        "    void speak() { print(\"woof\"); }\n"
	        "}\n"
	        "void main() {\n"
	        "    Dog d = Dog();\n"
	        "    ISpeak i = d;\n"
	        "    i.speak();\n"
	        "}\n",
	        "woof\n",
	        "W5 interface call",
	        {"struct vs_ISpeak", "struct vs_Dog", "virtual"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class AbstractValue {\n"
	        "public:\n"
	        "    int value;\n"
	        "    AbstractValue(int initial);\n"
	        "    int Add(int amount) const { return this.value + amount; }\n"
	        "    virtual int Get() const = 0;\n"
	        "    virtual ~AbstractValue() = 0;\n"
	        "}\n"
	        "AbstractValue::AbstractValue(int initial) { this.value = initial; }\n"
	        "int AbstractValue::Get() const { return this.value; }\n"
	        "AbstractValue::~AbstractValue() { print(\"base\"); }\n"
	        "class ConcreteValue : public AbstractValue {\n"
	        "public:\n"
	        "    ConcreteValue(int initial) : AbstractValue(initial) {}\n"
	        "    int Get() const override { return Add(1); }\n"
	        "    ~ConcreteValue() { print(\"derived\"); }\n"
	        "}\n"
	        "class Box<T> {\n"
	        "public:\n"
	        "    T value;\n"
	        "    Box(T initial);\n"
	        "    T Read() const;\n"
	        "}\n"
	        "Box<T>::Box(T initial) { this.value = initial; }\n"
	        "T Box<T>::Read() const { return this.value; }\n"
	        "void main() {\n"
	        "    ConcreteValue value = ConcreteValue(6);\n"
	        "    print(value.Get());\n"
	        "    Box<string> text = Box<string>(\"ok\");\n"
	        "    print(text.Read());\n"
	        "}\n",
	        "7\nok\nderived\nbase\n",
	        "pure virtual and generic same-file definitions",
	        {"virtual ~vs_AbstractValue() = 0", "vs_AbstractValue::~vs_AbstractValue",
	            "vs_AbstractValue_init", "template <typename T>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct AbstractStruct {\n"
	        "    int value;\n"
	        "    AbstractStruct(int initial);\n"
	        "    virtual int Get() const = 0;\n"
	        "}\n"
	        "AbstractStruct::AbstractStruct(int initial) { this.value = initial; }\n"
	        "int AbstractStruct::Get() const { return this.value; }\n"
	        "struct ConcreteStruct : AbstractStruct {\n"
	        "    ConcreteStruct(int initial) : AbstractStruct(initial) {}\n"
	        "    int Get() const override { return this.value + 1; }\n"
	        "}\n"
	        "void main() { ConcreteStruct value(8); print(value.Get()); }\n",
	        "9\n",
	        "abstract value struct constructor initialization",
	        {"virtual int Get() const = 0", "vs_AbstractStruct_init",
	            "vs_ConcreteStruct_init"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class AbstractBox<T> {\n"
	        "public:\n"
	        "    T stored;\n"
	        "    AbstractBox(T initial) { this.stored = initial; }\n"
	        "    virtual T Read() const = 0;\n"
	        "}\n"
	        "class ConcreteBox<T> : public AbstractBox<T> {\n"
	        "public:\n"
	        "    ConcreteBox(T initial) : AbstractBox(initial) {}\n"
	        "    T Read() const override { return this.stored; }\n"
	        "}\n"
	        "void main() {\n"
	        "    ConcreteBox<int> value = ConcreteBox<int>(11);\n"
	        "    print(value.Read());\n"
	        "}\n",
	        "11\n",
	        "generic abstract base specialization",
	        {"template <typename T>", "virtual T Read() const = 0",
	            "vs_ConcreteBox<int>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IA { virtual void a() = 0; }\n"
	        "interface IB { virtual void b() = 0; }\n"
	        "interface IC : IA, IB {}\n"
	        "class C : public IC{\n"
	        "    public:\n"
	        "    void a() { print(\"a\"); }\n"
	        "    void b() { print(\"b\"); }\n"
	        "}\n"
	        "void main() {\n"
	        "    IC c = C();\n"
	        "    c.a();\n"
	        "    IA a = c;\n"
	        "    a.a();\n"
	        "    IB b = c;\n"
	        "    b.b();\n"
	        "}\n",
	        "a\na\nb\n",
	        "multi-parent interface",
	        {"struct vs_IA", "struct vs_IB", "struct vs_IC"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface ISpeak { virtual void speak() = 0; }\n"
	        "struct Cat : ISpeak {\n"
	        "    void speak() { print(\"meow\"); }\n"
	        "}\n"
	        "void main() {\n"
	        "    Cat c = Cat();\n"
	        "    ISpeak i = c;\n"
	        "    i.speak();\n"
	        "}\n",
	        "meow\n",
	        "W5 struct box to interface",
	        {"_box_", "vs_ISpeak", "make_shared"}))
	{
		return EXIT_FAILURE;
	}

	// ---- W6 type monomorph + pair map entries
	if (!DualRun(
	        "class Box<T> {\n"
	        "    public:\n"
	        "    T value;\n"
	        "    T get() { return this.value; }\n"
	        "}\n"
	        "void main() {\n"
	        "    Box<int> b = Box<int>();\n"
	        "    b.value = 7;\n"
	        "    print(b.get());\n"
	        "}\n",
	        "7\n",
	        "W6 Box<int> as C++ template",
	        {"template <typename", "vs_Box<int>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IBoxable<T> {\n"
	        "    virtual T get() = 0;\n"
	        "    virtual void set(T v) = 0;\n"
	        "}\n"
	        "class IntBox : public IBoxable<int> {\n"
	        "    public:\n"
	        "    int value;\n"
	        "    int get() { return this.value; }\n"
	        "    void set(int v) { this.value = v; }\n"
	        "}\n"
	        "void main() {\n"
	        "    IntBox b = IntBox();\n"
	        "    b.set(9);\n"
	        "    IBoxable<int> i = b;\n"
	        "    print(i.get());\n"
	        "}\n",
	        "9\n",
	        "W6 open generic interface as C++ template",
	        {"template <typename", "vs_IBoxable", "struct vs_IBoxable"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IParent<T> {\n"
	        "    virtual T get() = 0;\n"
	        "}\n"
	        "interface IChild<T> : IParent<T> {\n"
	        "    virtual void set(T v) = 0;\n"
	        "}\n"
	        "class C : public IChild<int> {\n"
	        "    public:\n"
	        "    int v;\n"
	        "    int get() { return this.v; }\n"
	        "    void set(int x) { this.v = x; }\n"
	        "}\n"
	        "void main() {\n"
	        "    C c = C();\n"
	        "    c.set(4);\n"
	        "    IChild<int> x = c;\n"
	        "    IParent<int> y = x;\n"
	        "    print(y.get());\n"
	        "}\n",
	        "4\n",
	        "W6 open iface inheritance IChild<T>:IParent<T>",
	        {"struct vs_IParent", "struct vs_IChild", "public vs_IParent"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IHasId {\n"
	        "    virtual int id() = 0;\n"
	        "}\n"
	        "class Dog : public IHasId {\n"
	        "    public:\n"
	        "    int id() { return 7; }\n"
	        "}\n"
	        "int getId<T>(T x) where T : IHasId {\n"
	        "    return x.id();\n"
	        "}\n"
	        "void main() {\n"
	        "    Dog d = Dog();\n"
	        "    print(getId(d));\n"
	        "}\n",
	        "7\n",
	        "W6 where T:I → enable_if is_convertible",
	        {"std::enable_if_t", "std::is_convertible_v", "#include <type_traits>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IBoxable<T> {\n"
	        "    virtual T get() = 0;\n"
	        "}\n"
	        "class IntBox : public IBoxable<int> {\n"
	        "    public:\n"
	        "    int v;\n"
	        "    int get() { return this.v; }\n"
	        "}\n"
	        "int read<T>(T x) where T : IBoxable<int> {\n"
	        "    return x.get();\n"
	        "}\n"
	        "void main() {\n"
	        "    IntBox b = IntBox();\n"
	        "    b.v = 11;\n"
	        "    print(read(b));\n"
	        "}\n",
	        "11\n",
	        "W6 where T:IBoxable<int> method return subst",
	        {"std::enable_if_t", "std::is_convertible_v", "vs_IBoxable"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IParent<T> {\n"
	        "    virtual T get() = 0;\n"
	        "}\n"
	        "interface IChild<T> : IParent<T> {\n"
	        "    virtual void set(T v) = 0;\n"
	        "}\n"
	        "class C : public IChild<int> {\n"
	        "    public:\n"
	        "    int v;\n"
	        "    int get() { return this.v; }\n"
	        "    void set(int x) { this.v = x; }\n"
	        "}\n"
	        "int readChild<T>(T x) where T : IChild<int> {\n"
	        "    return x.get();\n"
	        "}\n"
	        "void main() {\n"
	        "    C c = C();\n"
	        "    c.set(8);\n"
	        "    print(readChild(c));\n"
	        "}\n",
	        "8\n",
	        "W6 where T:IChild resolves parent method",
	        {"std::enable_if_t", "std::is_convertible_v"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IA<T> { virtual T a() = 0; }\n"
	        "interface IB<T> { virtual T b() = 0; }\n"
	        "interface IC<T> : IA<T>, IB<T> {}\n"
	        "class C : public IC<int> {\n"
	        "    public:\n"
	        "    int a() { return 1; }\n"
	        "    int b() { return 2; }\n"
	        "}\n"
	        "void main() {\n"
	        "    IC<int> c = C();\n"
	        "    print(c.a());\n"
	        "    print(c.b());\n"
	        "    IA<int> a = c;\n"
	        "    print(a.a());\n"
	        "}\n",
	        "1\n2\n1\n",
	        "W6 multi-parent open iface IC<T>:IA,IB",
	        {"struct vs_IA", "struct vs_IB", "struct vs_IC"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "interface IHasId {\n"
	        "    virtual int id() = 0;\n"
	        "}\n"
	        "class Dog : public IHasId {\n"
	        "    public:\n"
	        "    int id() { return 3; }\n"
	        "}\n"
	        "class Box<T> where T : IHasId {\n"
	        "    public:\n"
	        "    T value;\n"
	        "    int peek() { return this.value.id(); }\n"
	        "}\n"
	        "void main() {\n"
	        "    Box<Dog> b = Box<Dog>();\n"
	        "    b.value = Dog();\n"
	        "    print(b.peek());\n"
	        "}\n",
	        "3\n",
	        "W6 class where T:I as enable_if on template",
	        {"std::enable_if_t", "std::is_convertible_v", "template <typename"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    ordered_map<string,int> m = ordered_map<string,int>();\n"
	        "    m[\"a\"] = 1;\n"
	        "    m[\"b\"] = 2;\n"
	        "    int s = 0;\n"
	        "    for (pair<string,int> p : m.entries()) {\n"
	        "        s = s + p.second;\n"
	        "    }\n"
	        "    print(s);\n"
	        "}\n",
	        "3\n",
	        "W6 pair map entries",
	        {"vs_InternalPair", "___vs_out"}))
	{
		return EXIT_FAILURE;
	}

	// A user pair template retains its ordinary fields, methods and value iteration.
	if (!DualRun(
	        "void main(){ordered_map<int64,int> values={1:3};"
	        "for(pair<long,int> entry:values){print(entry.second);}}",
	        "3\n", "map entry iteration preserves scalar type aliases", {"vs_InternalPair<std::int64_t, int>"}))
	{
		return EXIT_FAILURE;
	}

	// A user pair template retains its ordinary fields, methods and value iteration.
	if (!DualRun(
	        "struct pair<T>{T value=9;pair(){value+=7;}}"
	        "void main(){pair<int> local=pair<int>();print(local.value);"
	        "pair<int,int> entry=pair<int,int>();print(entry.first);}",
	        "16\n0\n", "user pair<T> constructor beside builtin pair<T,U>",
	        {"vs_pair_g_int_new()", "vs_InternalPair<int, int>"}))
	{
		return EXIT_FAILURE;
	}

	// A user pair template retains its ordinary fields, methods and value iteration.
	if (!DualRun(
	        "struct pair<T,U> { T custom; U extra; T read() { return custom; } };\n"
	        "void main(){pair<int,string> value=pair<int,string>();value.custom=7;value.extra=\"user\";"
	        "print(value.read());print(value.extra);ordered_map<string,pair<int,string>> values=ordered_map<string,pair<int,string>>();"
	        "values[\"a\"]=value;for(pair<int,string> item:values){print(item.read());}"
	        "array<pair<int,string>> items={value};for(pair<int,string> item:items){print(item.read());}"
	        "for((string key,pair<int,string> item):values){print(item.read());}"
	        "for(auto& [key,item]:values){print(item.read());}print(values[\"a\"].read());}",
	        "7\nuser\n7\n7\n7\n7\n7\n", "user pair template and container value iteration",
	        {"vs_pair<int, std::string>"}))
	{
		return EXIT_FAILURE;
	}

	// Runtime support names must never collide with a legal non-generic user
	// declaration.  pair<K,V> is a soft intrinsic and intentionally coexists
	// with a bare user class pair.
	if (!DualRun(
	        "class pair { public: int value = 7; int get() { return this.value; } };\n"
	        "void main() {\n"
	        "    pair userPair();\n"
	        "    ordered_map<string,int> values = ordered_map<string,int>();\n"
	        "    values[\"a\"] = 5;\n"
	        "    array<pair<string,int>> entries = values.entries();\n"
	        "    print(entries[0].second);\n"
	        "    print(userPair.get());\n"
	        "}\n",
	        "5\n7\n",
	        "W6 user pair does not collide with runtime pair",
	        {"struct vs_pair", "struct vs_InternalPair", "vs_InternalPair<std::string, int>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Counter { int value = 1; };\n"
	        "int route(ref Counter self) { return 11; }\n"
	        "int route(Counter self) { return 22; }\n"
	        "Counter make_counter() { return Counter(); }\n"
	        "T choose<T>(T self, T other) { return other; }\n"
	        "void main() {\n"
	        "    Counter counter();\n"
	        "    print(counter.route());\n"
	        "    print(make_counter().route());\n"
	        "    int value = 3;\n"
	        "    print(value.choose<int>(9));\n"
	        "}\n",
	        "11\n22\n9\n",
	        "W6 uniform extension calls",
	        {"_ref0", "vs_choose", "<int>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() { print(std::chrono::now() > 0.0); }\n",
	        "true\n",
	        "W6 chrono-only runtime",
	        {"#include <chrono>", "VsNowSeconds()"}))
	{
		return EXIT_FAILURE;
	}
	if (!DualRun(
	        "void main() { std::console::write(\"x\"); std::console::flush(); "
	        "print(std::console::is_interactive()); print(std::console::clear()); }\n",
	        "xfalse\nfalse\n",
	        "W6 console runtime",
	        {"VsConsoleWrite(", "VsConsoleFlush()", "VsConsoleIsInteractive()",
	            "VsConsoleClear()"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "string foreground(){print(\"foreground\");return \"red\";}\n"
	        "string background(){print(\"background\");return \"default\";}\n"
	        "int column(){print(\"column\");return 1;}\n"
	        "int row(){print(\"row\");return 2;}\n"
	        "string sharedForeground=\"red\";\n"
	        "string mutateBackground(){sharedForeground=\"invalid\";return \"default\";}\n"
	        "void main(){print(std::console::set_color(foreground(),background()));"
	        "print(std::console::move_cursor(column(),row()));"
	        "assert(!std::console::set_color(sharedForeground,mutateBackground()));"
	        "assert(sharedForeground==\"invalid\");"
	        "string __vs_console_arg0=\"red\";string __vs_console_arg1=\"default\";"
	        "int __vs_console_first=1;int __vs_console_second=2;"
	        "assert(!std::console::set_color(__vs_console_arg0,__vs_console_arg1));"
	        "assert(!std::console::move_cursor(__vs_console_first,__vs_console_second));}\n",
	        "foreground\nbackground\nfalse\ncolumn\nrow\nfalse\n",
	        "console controls preserve script argument evaluation order",
	        {"VsConsoleRuntime::Execute(", "VsConsoleRuntime::ColorIndex("}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "string foreground(){print(\"foreground\");return \"invalid\";}\n"
	        "string background(){print(\"background\");return \"default\";}\n"
	        "void main(){std::console::set_color(\"green\",\"default\");"
	        "std::console::set_cursor_visible(false);std::console::set_color(foreground(),background());}\n",
	        "foreground\nbackground\n", "console evaluates both arguments before color validation",
	        {}, {}, {}, nullptr, "unknown color name", nullptr, true))
	{
		return EXIT_FAILURE;
	}
	for (const char* Coordinate : {"2147483648i64", "-1i64"})
	{
		if (!DualRun(
		        std::string("int64 column(){print(\"column\");return ") + Coordinate + ";}\n"
		                                                                               "int row(){print(\"row\");return 2;}\n"
		                                                                               "void main(string[] args){std::console::move_cursor(column(),row());}\n",
		        "column\nrow\n", "console evaluates both arguments before coordinate validation",
		        {}, {}, {}, nullptr, "coordinates in 0..32766"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!TestConsoleEntryBoundaries()) return EXIT_FAILURE;
	if (!TestConsoleWorkerBoundaries()) return EXIT_FAILURE;
	for (const char* Parameter : {"ConsoleRestore", "ConcurrentShutdown", "argc", "argv", "i",
	         "___veroscript_main_exit_code", "VsMainArgs"})
	{
		if (!DualRun(std::string("int main(string[] ") + Parameter + "){print(len(" + Parameter + "));print(std::console::supports_color());return 0;}", "0\nfalse\n",
		        "entry wrapper names do not collide with script parameters", {}))
		{
			return EXIT_FAILURE;
		}
	}

	// ---- W7 concurrent spawn + channel (OS threads with isolated script state)
	if (!TestConcurrentShutdownDescendants()) return EXIT_FAILURE;
	{
		const std::vector<std::string> StdlibImportPaths;
		if (!DualRun(
		        "import \"concurrent.vs\";\n"
		        "void worker(channel<int> ch) {\n"
		        "    int x = ch.recv();\n"
		        "    ch.send(x + 1);\n"
		        "}\n"
		        "void main() {\n"
		        "    channel<int> ch = channel<int>();\n"
		        "    int t = std::concurrent::spawn(\"worker\", ch);\n"
		        "    print(t);\n"
		        "    ch.send(41);\n"
		        "    std::concurrent::join_task(t);\n"
		        "    print(ch.recv());\n"
		        "}\n",
		        "0\n42\n",
		        "W7 spawn+channel smoke",
		        {"vs_Channel",
		            "VsSpawnWithChannelCloser",
		            "VsJoinTaskOrFail",
		            "static int Next = 0",
		            "VsTasks().emplace(Id, std::thread{})",
		            "VsConcurrentShutdownGuard ConcurrentShutdown"},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!DualRun(
		        "import \"concurrent.vs\";\n"
		        "export void producer(channel<int> ch) {\n"
		        "    for (int i = 0; i < 1000; ++i) ch.send(i);\n"
		        "}\n"
		        "export void consumer(channel<int> ch) {\n"
		        "    for (int i = 0; i < 1000; ++i) { int value = ch.recv(); assert(value >= 0 && value < 1000); }\n"
		        "}\n"
		        "void main() {\n"
		        "    channel<int> ch = std::concurrent::make_channel<int>(1);\n"
		        "    array<int> ids = array<int>();\n"
		        "    for (int i = 0; i < 4; ++i) ids.push(std::concurrent::spawn(\"producer\", ch));\n"
		        "    for (int i = 0; i < 4; ++i) ids.push(std::concurrent::spawn(\"consumer\", ch));\n"
		        "    for (int id : ids) std::concurrent::join_task(id);\n"
		        "    print(ch.pending());\n"
		        "}\n",
		        "0\n", "bounded channel contention",
		        {"can_send.wait", "can_receive.wait", "can_send.notify_one()", "can_receive.notify_one()"},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!CoreRuntimeRun(
		        "import \"concurrent.vs\";\n"
		        "export void answer(channel<int> output) {\n"
		        "    std::concurrent::channel_send(output, 42);\n"
		        "}\n"
		        "void main() {\n"
		        "    task<int> work = std::concurrent::spawn_task<int>(\"answer\");\n"
		        "    print(work.valid());\n"
		        "    print(work.await());\n"
		        "    print(work.valid());\n"
		        "}\n",
		        "true\n42\nfalse\n",
		        "W7 typed task lifecycle",
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!CoreRuntimeRun(
		        "import \"concurrent.vs\";\n"
		        "export void wait_for_cancel(channel<int> output) {\n"
		        "    int ignored = std::concurrent::channel_recv(output);\n"
		        "}\n"
		        "void main() {\n"
		        "    task<int> work = std::concurrent::spawn_task<int>(\"wait_for_cancel\");\n"
		        "    work.cancel();\n"
		        "    print(work.valid());\n"
		        "}\n",
		        "false\n",
		        "W7 typed task cancel",
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!DualRun(
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
		        "W7 cooperative cancellation flag",
		        {"VsCancelRequested()", "State->CancelRequested = true"},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!CoreRuntimeRun(
		        "import \"concurrent.vs\";\n"
		        "int worker_global = 7;\n"
		        "class WorkerState { public: static int value = 11; };\n"
		        "export void mutate_isolated_state(channel<int> output) {\n"
		        "    worker_global = 99;\n"
		        "    WorkerState.value = 101;\n"
		        "    output.send(worker_global + WorkerState.value);\n"
		        "}\n"
		        "void main() {\n"
		        "    task<int> work = std::concurrent::spawn_task<int>(\"mutate_isolated_state\");\n"
		        "    print(work.await());\n"
		        "    print(worker_global);\n"
		        "    print(WorkerState.value);\n"
		        "}\n",
		        "200\n7\n11\n",
		        "W7 worker global and static isolation",
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!DualRun(
		        "import \"concurrent.vs\";\n"
		        "export void repeatable_worker(channel<int> output) { output.send(1); }\n"
		        "void main() {\n"
		        "    channel<int> output = channel<int>();\n"
		        "    int id = std::concurrent::spawn(\"repeatable_worker\", output);\n"
		        "    std::concurrent::join_task(id);\n"
		        "    std::concurrent::join_task(id);\n"
		        "    print(output.recv());\n"
		        "}\n",
		        "1\n",
		        "W7 repeated join completion identity",
		        {"VsCompletedTaskResults()", "VsScriptFail(\"invalid task id\")"},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!CoreRuntimeRun(
		        "import \"concurrent.vs\";\n"
		        "export void delayed(channel<int> output) {\n"
		        "    std::this_thread::sleep(20);\n"
		        "    output.send(42);\n"
		        "}\n"
		        "export void first(channel<int> output) { output.send(1); }\n"
		        "export void second(channel<int> output) { output.send(2); }\n"
		        "void main() {\n"
		        "    channel<int> ch = std::concurrent::make_channel<int>(1);\n"
		        "    print(ch.capacity());\n"
		        "    print(ch.try_send(7));\n"
		        "    print(ch.try_send(8));\n"
		        "    optional<int> received = ch.try_recv();\n"
		        "    print(received.get());\n"
		        "    print(ch.recv_for(0).has_value());\n"
		        "    task<int> work = std::concurrent::spawn_task<int>(\"delayed\");\n"
		        "    print(work.await_for(0).has_value());\n"
		        "    print(work.await());\n"
		        "    task_group<int> group = task_group<int>();\n"
		        "    group.add(std::concurrent::spawn_task<int>(\"first\"));\n"
		        "    group.add(std::concurrent::spawn_task<int>(\"second\"));\n"
		        "    array<int> all = group.await_all();\n"
		        "    print(all[0] + all[1]);\n"
		        "}\n",
		        "1\ntrue\nfalse\n7\nfalse\nfalse\n42\n3\n",
		        "W7 bounded and timed concurrency",
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!CoreRuntimeRun(
		        "import \"concurrent.vs\";\n"
		        "export void increment(channel<channel<int>> setup) {\n"
		        "    channel<int> value = setup.recv();\n"
		        "    std::concurrent::synchronized_fetch_add(value, 1);\n"
		        "}\n"
		        "void main() {\n"
		        "    channel<channel<int>> setup = channel<channel<int>>();\n"
		        "    channel<int> value = std::concurrent::make_synchronized<int>(4);\n"
		        "    int work = std::concurrent::spawn(\"increment\", setup);\n"
		        "    setup.send(value);\n"
		        "    std::concurrent::join_task(work);\n"
		        "    print(std::concurrent::synchronized_load(value));\n"
		        "    print(std::concurrent::synchronized_exchange(value, 9));\n"
		        "    std::concurrent::synchronized_store(value, 11);\n"
		        "    print(std::concurrent::synchronized_load(value));\n"
		        "    std::concurrent::mutex gate = std::concurrent::make_mutex();\n"
		        "    print(std::concurrent::mutex_try_lock(gate));\n"
		        "    std::concurrent::mutex_unlock(gate);\n"
		        "    std::concurrent::counting_semaphore permits =\n"
		        "        std::concurrent::make_semaphore(1, 2);\n"
		        "    print(std::concurrent::semaphore_try_acquire(permits));\n"
		        "}\n",
		        "5\n5\n11\ntrue\ntrue\n",
		        "W7 nested channel synchronization capabilities",
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
	}

	// W7b: AOT spawn resolves entry-module worker over imported private same-name worker.
	{
		namespace fs = std::filesystem;
		const FScopedTempDirectory TempDirectory("veroscript_aot_spawn_vis");
		const fs::path& Dir = TempDirectory.GetPath();
		{
			std::ofstream Lib(Dir / "spawn_lib.vs");
			Lib << "import \"concurrent.vs\";\n"
			    << "void worker(channel<int> ch) {\n"
			    << "    ch.send(99);\n"
			    << "}\n";
		}
		const std::string MainSource =
		    "import \"spawn_lib.vs\";\n"
		    "import \"concurrent.vs\";\n"
		    "void worker(channel<int> ch) {\n"
		    "    int x = ch.recv();\n"
		    "    ch.send(x + 1);\n"
		    "}\n"
		    "void main() {\n"
		    "    channel<int> ch = channel<int>();\n"
		    "    int t = std::concurrent::spawn(\"worker\", ch);\n"
		    "    ch.send(41);\n"
		    "    std::concurrent::join_task(t);\n"
		    "    print(ch.recv());\n"
		    "}\n";
		const std::vector<std::string> Paths = {
		    Dir.string()};
		if (!DualRun(
		        MainSource,
		        "42\n",
		        "W7b AOT spawn module visibility",
		        {"VsSpawnWithChannelCloser", "VsJoinTaskOrFail"},
		        Paths))
		{
			return EXIT_FAILURE;
		}
	}

	// W7c: AOT glue tracks per-task channel closers (join timeout isolation).
	{
		const std::vector<std::string> StdlibImportPaths;
		std::string ConcurrentCpp;
		std::string EmitError;
		const std::string Source =
		    "import \"concurrent.vs\";\n"
		    "void worker(channel<int> ch) {\n"
		    "    int x = ch.recv();\n"
		    "    ch.send(x + 1);\n"
		    "}\n"
		    "void main() {\n"
		    "    channel<int> ch = channel<int>();\n"
		    "    int t = std::concurrent::spawn(\"worker\", ch);\n"
		    "    ch.send(41);\n"
		    "    std::concurrent::join_task(t);\n"
		    "    print(ch.recv());\n"
		    "}\n";
		if (!Expect(
		        VeroScript::EmitCppFromSource(
		            Source,
		            "aot.vs",
		            ConcurrentCpp,
		            EmitError,
		            StdlibImportPaths),
		        "W7c AOT emit concurrent glue"))
		{
			std::cerr << EmitError << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        ConcurrentCpp.find("VsSpawnChannelClosers") != std::string::npos,
		        "W7c per-task closers") ||
		    !Expect(
		        ConcurrentCpp.find("VsChannelSend") != std::string::npos,
		        "W7c channel send helper") ||
		    !Expect(
		        ConcurrentCpp.find("VsJoinTaskOrFail") != std::string::npos,
		        "W7c join or fail") ||
		    !Expect(
		        ConcurrentCpp.find(
		            "Parsed.ptr != Text.data() + Text.size()") != std::string::npos,
		        "W7c join timeout requires a complete integer") ||
		    !Expect(
		        ConcurrentCpp.find("VsTimedOutTasks") != std::string::npos && ConcurrentCpp.find(".detach()") == std::string::npos,
		        "W7c timed-out tasks remain joinable until safe shutdown"))
		{
			std::cerr << ConcurrentCpp;
			return EXIT_FAILURE;
		}
	}

	// ---- W8 caps: defaults, argv main, queue, builtins
	if (!DualRun(
	        "void greet(string name = \"world\") {\n"
	        "    print(name);\n"
	        "}\n"
	        "void main() {\n"
	        "    greet();\n"
	        "    greet(\"hi\");\n"
	        "}\n",
	        "world\nhi\n",
	        "W8 default params",
	        {"world"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main(string[] args) {\n"
	        "    print(len(args));\n"
	        "}\n",
	        "0\n",
	        "W8 main(string[] args)",
	        {"int main(int argc, char** argv)", "std::vector<std::string>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    queue<int> q = queue<int>();\n"
	        "    q.push(1);\n"
	        "    q.push(2);\n"
	        "    print(q.pop());\n"
	        "    print(q.front());\n"
	        "}\n",
	        "1\n2\n",
	        "W8 queue push/pop/front",
	        {"std::queue"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    print(abs(-3));\n"
	        "    print(min(2, 5));\n"
	        "    print(max(2, 5));\n"
	        "    print(abs(-7i64));\n"
	        "    print(min(-9i64, 3i64));\n"
	        "    print(max(4u64, 5000000000u64));\n"
	        "}\n",
	        "3\n2\n5\n7\n-9\n5000000000\n",
	        "W8 abs/min/max builtins",
	        {"VsAbs", "VsMin", "VsMax"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    wstring converted = to_wstring(\"你好🙂\");\n"
	        "    print(converted);\n"
	        "    print(to_string(L\"再见🙂\"));\n"
	        "    print(len(converted)); print(len(L\"再见🙂\"));\n"
	        "    print(len(L\"a\\0b\")); print(len(\"a\\0b\"));\n"
	        "    print(to_string(L\"a\\0b\") == \"a\\0b\");\n"
	        "}\n",
	        "你好🙂\n再见🙂\n4\n4\n3\n3\ntrue\n",
	        "W8 Unicode wstring VM/AOT parity",
	        {"VsAppendUtf8", "invalid UTF-8 sequence"}))
	{
		return EXIT_FAILURE;
	}

	// ---- 2.46.1: stdlib __* glue + lambda bare this field
	{
		const std::vector<std::string> StdlibImportPaths;
		if (!DualRun(
		        "import \"math.vs\";\n"
		        "void main() {\n"
		        "    print(to_int(std::sqrt(4.0)));\n"
		        "    print(to_int(std::floor(3.7)));\n"
		        "}\n",
		        "2\n3\n",
		        "2.46.1 math.vs sqrt/floor",
		        {"std::sqrt", "#include <cmath>"},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!DualRun(
		        "import \"string.vs\";\n"
		        "void main() {\n"
		        "    print(std::strings::substring(\"hello\", 1, 3));\n"
		        "    print(std::strings::find(\"hello\", \"ll\"));\n"
		        "    print(std::strings::find(\"hello\", \"x\"));\n"
		        "    print(std::strings::to_lower(\"Ab\xC3\x89\"));\n"
		        "    print(std::strings::to_upper(\"Ab\xC3\xA9\"));\n"
		        "}\n",
		        "ell\n2\n-1\nab\xC3\x89\nAB\xC3\xA9\n",
		        "2.46.1 string.vs substr/find",
		        {"VsSubstr", "VsFind"},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!DualRun(
		        "import \"algorithm.vs\";\n"
		        "import \"numeric.vs\";\n"
		        "import \"random.vs\";\n"
		        "import \"path.vs\";\n"
		        "import \"math.vs\";\n"
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
		        "    array<int> copied = std::algorithm::copy(values);\n"
		        "    print(std::algorithm::equal(values, copied));\n"
		        "    print(std::algorithm::contains(copied, 7));\n"
		        "    print(std::algorithm::is_sorted(copied));\n"
		        "    print(std::algorithm::min_index(copied));\n"
		        "    print(std::algorithm::max_index(copied));\n"
		        "    std::algorithm::rotate(copied, 2);\n"
		        "    print(copied[0]);\n"
		        "    std::algorithm::fill(copied, 3);\n"
		        "    print(copied[4]);\n"
		        "    array<unsigned int> first = std::random::make_engine(123u);\n"
		        "    array<unsigned int> second = std::random::make_engine(123u);\n"
		        "    print(std::random::next_uint(first) == std::random::next_uint(second));\n"
		        "    unsigned int bounded = std::random::next_bounded_uint(first, 10u);\n"
		        "    print(bounded < 10u);\n"
		        "    int ranged = std::random::next_int(first, -3, 4);\n"
		        "    print(ranged >= -3 && ranged < 4);\n"
		        "    double ranged_double = std::random::next_double_range(first, 2.0, 3.0);\n"
		        "    print(ranged_double >= 2.0 && ranged_double < 3.0);\n"
		        "    array<int> shuffled = {1, 2, 3, 4};\n"
		        "    std::algorithm::shuffle(shuffled, first);\n"
		        "    std::algorithm::sort(shuffled);\n"
		        "    print(std::algorithm::equal(shuffled, array<int>{1, 2, 3, 4}));\n"
		        "    array<unsigned int> full = std::random::make_engine(4071982377u);\n"
		        "    assert(std::random::next_bounded_uint(full, 4294967295u) == 0u);\n"
		        "    array<unsigned int> narrow = std::random::make_engine(12345u);\n"
		        "    double hi = 1.0000000000000002;\n"
		        "    assert(std::random::next_double_range(narrow, 1.0, hi) < hi);\n"
		        "    array<double> large = {1e308, 1e308};\n"
		        "    assert(std::numeric::average(large) == 1e308);\n"
		        "    assert(std::filesystem::dirname(\"C:/foo.txt\") == \"C:/\");\n"
		        "}\n",
		        "2\ntrue\n25\n35\n5\nveroscript\ntrue\ntrue\ntrue\n0\n4\n5\n3\ntrue\ntrue\ntrue\ntrue\ntrue\n",
		        "algorithm, numeric, and random standard-library modules",
		        {},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		const FScopedTempDirectory FsRoot("VeroScriptCppAotFs");
		const std::string EmptyDir =
		    (FsRoot.GetPath() / "empty").generic_string();
		if (!DualRun(
		        "import \"io.vs\"; import \"path.vs\"; import \"string.vs\";\n"
		        "void main() {\n"
		        "    string path = \"" +
		            EmptyDir + "\";\n"
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
		        "filesystem type and empty directory AOT parity",
		        {},
		        StdlibImportPaths))
		{
			return EXIT_FAILURE;
		}
		if (!DualRun(
		        "class CounterBase {\n"
		        "    public:\n"
		        "    int value;\n"
		        "    CounterBase(int v) { value = v; }\n"
		        "    fn()->int make_getter() {\n"
		        "        return fn() -> int => value;\n"
		        "    }\n"
		        "}\n"
		        "class Counter : public CounterBase {\n"
		        "    public:\n"
		        "    Counter(int v) : CounterBase(v) {}\n"
		        "}\n"
		        "fn()->int make_counter_getter() {\n"
		        "    Counter c = Counter(42);\n"
		        "    return c.make_getter();\n"
		        "}\n"
		        "void main() {\n"
		        "    fn()->int get = make_counter_getter();\n"
		        "    print(get());\n"
		        "}\n",
		        "42\n",
		        "2.46.1 inherited lambda this field capture",
		        {"std::shared_ptr<vs_Counter> c = vs_Counter_new(42);",
		            "(*vs_this).value",
		            "[=, vs_this = VsRetainObject(this)]("}))
		{
			return EXIT_FAILURE;
		}
	}

	// ---- C++-style static members: per-type, per-worker AOT storage
	if (!DualRun(
	        "class Counter {\n"
	        "    public:\n"
	        "    static int Count = 0;\n"
	        "    static const int Max = 100;\n"
	        "    static int Next() {\n"
	        "        Counter.Count = Counter.Count + 1;\n"
	        "        return Counter.Count;\n"
	        "    }\n"
	        "}\n"
	        "void main() {\n"
	        "    print(Counter.Max);\n"
	        "    print(Counter.Next());\n"
	        "    print(Counter.Count);\n"
	        "    Counter c = Counter();\n"
	        "    print(c.Next());\n"
	        "    print(Counter.Count);\n"
	        "}\n",
	        "100\n1\n1\n2\n2\n",
	        "static class field and method",
	        {"static thread_local int Count;", "static int Next()", "vs_Counter::Count",
	            "thread_local int vs_Counter::Count = 0"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct MathUtil {\n"
	        "    static int Square(int x) { return x * x; }\n"
	        "}\n"
	        "void main() {\n"
	        "    print(MathUtil.Square(9));\n"
	        "}\n",
	        "81\n",
	        "static struct method",
	        {"static int Square(", "vs_MathUtil::Square("}))
	{
		return EXIT_FAILURE;
	}

	// Generic classes monomorphize to distinct C++ template instantiations, so each
	// `Box<T>` gets its own `static` storage (Box<int>.Count != Box<string>.Count).
	if (!DualRun(
	        "class Box<T> {\n"
	        "    public:\n"
	        "    static int Count = 0;\n"
	        "    T value;\n"
	        "}\n"
	        "void main() {\n"
	        "    Box<int> a = Box<int>();\n"
	        "    a.Count = 1;\n"
	        "    print(a.Count);\n"
	        "    Box<string> s = Box<string>();\n"
	        "    print(s.Count);\n"
	        "    a.Count = a.Count + 1;\n"
	        "    print(a.Count);\n"
	        "    print(s.Count);\n"
	        "    Box<int>.Count = 9;\n"
	        "    print(a.Count);\n"
	        "    print(Box<string>.Count);\n"
	        "}\n",
	        "1\n0\n2\n0\n9\n0\n",
	        "generic per-instantiation static storage",
	        {"static thread_local int Count;", "vs_Box<int>::Count", "vs_Box<T>::Count"}))
	{
		return EXIT_FAILURE;
	}

	// Namespaced type as a static-member receiver: `Ns.Box.Field` /
	// `Ns.Box.Method()` fold the dotted MemberAccess chain into the type name
	// `Ns.Box` and emit the C++ `vs_Ns__dot__Box::` qualifier.
	if (!DualRun(
	        "namespace Ns {\n"
	        "    class Box {\n"
	        "        public:\n"
	        "        static int Value = 9;\n"
	        "        static int Get() { return Ns.Box.Value; }\n"
	        "    }\n"
	        "}\n"
	        "void main() { print(Ns.Box.Get()); }\n",
	        "9\n",
	        "namespaced static member receiver",
	        {"vs_Ns__dot__Box::Value", "vs_Ns__dot__Box::Get("}))
	{
		return EXIT_FAILURE;
	}

	// Namespaced generic: per-instantiation static storage via C++ template.
	if (!DualRun(
	        "namespace Ns {\n"
	        "    class Box<T> {\n"
	        "        public:\n"
	        "        static int Count = 0;\n"
	        "        T pad;\n"
	        "    }\n"
	        "}\n"
	        "void main() {\n"
	        "    Ns.Box<int> a = Ns.Box<int>();\n"
	        "    a.Count = 1;\n"
	        "    print(a.Count);\n"
	        "    Ns.Box<string> s = Ns.Box<string>();\n"
	        "    print(s.Count);\n"
	        "    Ns.Box<int>.Count = 9;\n"
	        "    print(a.Count);\n"
	        "    print(Ns.Box<string>.Count);\n"
	        "}\n",
	        "1\n0\n9\n0\n",
	        "namespaced generic per-instantiation static",
	        {"vs_Ns__dot__Box", "static thread_local int Count;"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class RefNode {\n"
	        "public:\n"
	        "    int value;\n"
	        "    RefNode(int v) { value = v; }\n"
	        "}\n"
	        "using MyInt = int;\n"
	        "void main() {\n"
	        "    optional<int> empty = optional<int>();\n"
	        "    optional<int> some = optional<int>(42);\n"
	        "    print(empty.value_or(9));\n"
	        "    print(some.get());\n"
	        "    type<int> token = type<int>();\n"
	        "    print(token.name);\n"
	        "    print(token.qualified_name);\n"
	        "    type<MyInt> aliasToken = type<MyInt>();\n"
	        "    print(aliasToken.name);\n"
	        "    RefNode owner = RefNode(7);\n"
	        "    weak<RefNode> observer = weak<RefNode>(owner);\n"
	        "    print(observer.lock().value);\n"
	        "    weak<RefNode> copied = observer;\n"
	        "    observer.reset();\n"
	        "    print(observer.expired());\n"
	        "    print(copied.expired());\n"
	        "    optional<RefNode> holder = optional<RefNode>(RefNode(8));\n"
	        "    holder.reset();\n"
	        "    print(!holder.has_value());\n"
	        "}\n",
	        "9\n42\nint\nint\nint\n7\ntrue\nfalse\ntrue\n",
	        "optional type token and weak reference",
	        {"vs_Optional<int>", "vs_TypeToken<int>", "vs_Weak<"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct ReadonlyCounter {\n"
	        "    int value = 1;\n"
	        "    void set_value(int next) { value = next; }\n"
	        "};\n"
	        "struct ReadonlyRequired {\n"
	        "    int value;\n"
	        "    ReadonlyRequired(int next) { value = next; }\n"
	        "};\n"
	        "class ReadonlyBox {\n"
	        "public:\n"
	        "    int value = 1;\n"
	        "    int get_value() const { return value; }\n"
	        "    void set_value(int next) { value = next; }\n"
	        "    void force_value(int next) const { const_cast<ReadonlyBox>(this).set_value(next); }\n"
	        "};\n"
	        "const array<int> SharedLocked = {5};\n"
	        "const array<int>& shared_locked() { return SharedLocked; }\n"
	        "void main() {\n"
	        "    ReadonlyBox box = ReadonlyBox();\n"
	        "    readonly<ReadonlyBox> view = readonly<ReadonlyBox>(box);\n"
	        "    print(view.get().get_value());\n"
	        "    readonly<int> implicit_value = 41;\n"
	        "    print(implicit_value.get());\n"
	        "    readonly<ReadonlyRequired> required = ReadonlyRequired(13);\n"
	        "    print(required.get().value);\n"
	        "    const array<readonly<int>> locked_values = {implicit_value};\n"
	        "    const_cast<array<readonly<int>>>(locked_values).push(readonly<int>(42));\n"
	        "    print(len(locked_values));\n"
	        "    const_cast<ReadonlyBox>(view.get()).set_value(7);\n"
	        "    print(view.get().get_value());\n"
	        "    box.force_value(9);\n"
	        "    print(view.get().get_value());\n"
	        "    const array<int> locked = {1};\n"
	        "    const_cast<array<int>>(locked).push(2);\n"
	        "    print(len(locked));\n"
	        "    readonly<ReadonlyCounter> counter = readonly<ReadonlyCounter>(ReadonlyCounter());\n"
	        "    const_cast<ReadonlyCounter>(counter.get()).set_value(12);\n"
	        "    print(counter.get().value);\n"
	        "    const_cast<array<int>>(shared_locked()).push(6);\n"
	        "    print(len(SharedLocked));\n"
	        "}\n",
	        "1\n41\n13\n2\n7\n9\n2\n12\n2\n",
	        "readonly and const_cast AOT parity",
	        {"vs_Readonly<std::shared_ptr<vs_ReadonlyBox>>",
	            "const_cast<vs_ReadonlyBox&>",
	            "const_cast<vs_Array<int>&>"}))
	{
		return EXIT_FAILURE;
	}
	{
		std::string InvalidConstCastCpp;
		std::string InvalidConstCastError;
		if (!Expect(
		        !VeroScript::EmitCppFromSource(
		            "void main() { int value = 1; const_cast<int>(value); }\n",
		            "aot.vs",
		            InvalidConstCastCpp,
		            InvalidConstCastError),
		        "AOT rejects const_cast on a non-const source") ||
		    !Expect(
		        InvalidConstCastError.find("not const") != std::string::npos,
		        "AOT non-const const_cast diagnostic"))
		{
			std::cerr << InvalidConstCastError << '\n'
			          << InvalidConstCastCpp;
			return EXIT_FAILURE;
		}

		InvalidConstCastCpp.clear();
		InvalidConstCastError.clear();
		if (!Expect(
		        !VeroScript::EmitCppFromSource(
		            "void main() { const int value = 1; const_cast<double>(value); }\n",
		            "aot.vs",
		            InvalidConstCastCpp,
		            InvalidConstCastError),
		        "AOT rejects a type-changing const_cast") ||
		    !Expect(
		        InvalidConstCastError.find("cannot change type") != std::string::npos,
		        "AOT type-changing const_cast diagnostic"))
		{
			std::cerr << InvalidConstCastError << '\n'
			          << InvalidConstCastCpp;
			return EXIT_FAILURE;
		}
	}

	if (!DualRun(
	        "struct Meter {\n"
	        "    int value = 0;\n"
	        "    Meter(int input) { value = input; }\n"
	        "};\n"
	        "struct Token {\n"
	        "    int value = 0;\n"
	        "    explicit Token(int input) { value = input; }\n"
	        "};\n"
	        "int read(Meter value) { return value.value; }\n"
	        "void main() {\n"
	        "    Meter assigned = 7;\n"
	        "    print(assigned.value);\n"
	        "    print(read(8));\n"
	        "    Token direct(9);\n"
	        "    Token converted = cast<Token>(10);\n"
	        "    print(direct.value + converted.value);\n"
	        "}\n",
	        "7\n8\n19\n",
	        "implicit and explicit converting constructors AOT parity",
	        {"vs_Meter_new(7)", "vs_Meter_new(8)", "vs_Token_new(10)"}))
	{
		return EXIT_FAILURE;
	}
	{
		std::string ExplicitCpp;
		std::string ExplicitError;
		if (!Expect(
		        !VeroScript::EmitCppFromSource(
		            "struct Token { explicit Token(int value) {} };\n"
		            "void main() { Token value = 1; }\n",
		            "aot.vs",
		            ExplicitCpp,
		            ExplicitError),
		        "AOT rejects explicit constructor in implicit conversion") ||
		    !Expect(
		        ExplicitError.find("cannot convert") != std::string::npos,
		        "AOT explicit constructor rejection diagnostic"))
		{
			std::cerr << ExplicitError << '\n'
			          << ExplicitCpp;
			return EXIT_FAILURE;
		}
	}

	if (!DualRun(
	        "enum class Tone : int8 { Low = 1, High = 2 };\n"
	        "enum class OtherTone : int8 { Low = 1, High = 2 };\n"
	        "class CastBase {\n"
	        "public:\n"
	        "    virtual int value() { return 1; }\n"
	        "};\n"
	        "class CastDerived : public CastBase {\n"
	        "public:\n"
	        "    int value() override { return 9; }\n"
	        "};\n"
	        "void main() {\n"
	        "    print(cast<int>(7.9));\n"
	        "    Tone tone = cast<Tone>(2);\n"
	        "    print(cast<int>(tone));\n"
	        "    optional<OtherTone> other_tone = try_cast<OtherTone>(tone);\n"
	        "    print(!other_tone.has_value());\n"
	        "    optional<int8> overflow = try_cast<int8>(300);\n"
	        "    print(!overflow.has_value());\n"
	        "    CastBase base = CastDerived();\n"
	        "    CastDerived derived = cast<CastDerived>(base);\n"
	        "    print(derived.value());\n"
	        "    optional<CastDerived> found = try_cast<CastDerived>(base);\n"
	        "    print(found.has_value());\n"
	        "    CastBase plain = CastBase();\n"
	        "    optional<CastDerived> missing = try_cast<CastDerived>(plain);\n"
	        "    print(!missing.has_value());\n"
	        "    CastDerived empty = nullptr;\n"
	        "    optional<CastDerived> null_value = try_cast<CastDerived>(empty);\n"
	        "    print(null_value.has_value());\n"
	        "    print(null_value.get() == nullptr);\n"
	        "}\n",
	        "7\n2\ntrue\ntrue\n9\ntrue\ntrue\ntrue\ntrue\n",
	        "checked cast and try_cast AOT parity",
	        {"VsTryNumericCast<std::int8_t>",
	            "std::dynamic_pointer_cast<vs_CastDerived>",
	            "vs_Optional<std::shared_ptr<vs_CastDerived>>"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void main() {\n"
	        "    int source = 1;\n"
	        "    int& alias = source;\n"
	        "    const int& readonly = source;\n"
	        "    alias = 4;\n"
	        "    for (int& loop_alias = source; loop_alias < 5; loop_alias++) {}\n"
	        "    print(source);\n"
	        "    print(readonly);\n"
	        "}\n",
	        "5\n5\n",
	        "ordinary reference declarations preserve aliasing in AOT",
	        {"int& alias = source", "const int& readonly = source",
	            "for (int& loop_alias = source"}))
	{
		return EXIT_FAILURE;
	}

	{
		std::string ReferenceCaptureCpp;
		std::string ReferenceCaptureError;
		if (!Expect(
		        !VeroScript::EmitCppFromSource(
		            "function()->int make() { int x = 1; return [&]() -> int { return ++x; }; }\n"
		            "void main() {}\n",
		            "aot.vs",
		            ReferenceCaptureCpp,
		            ReferenceCaptureError),
		        "AOT must reject unsafe raw reference-capture lowering") ||
		    !Expect(
		        ReferenceCaptureError.find("managed capture-cell lowering") != std::string::npos,
		        "AOT reference-capture diagnostic"))
		{
			std::cerr << ReferenceCaptureError << '\n';
			return EXIT_FAILURE;
		}
	}

	if (!DualRun(
	        "enum class Flags : uint32 { None = 0u, All = 4294967295u }\n"
	        "void main() {\n"
	        "    int32 value = 9;\n"
	        "    uint32 key = 4294967295u;\n"
	        "    map<uint32,int32> m = map<uint32,int32>();\n"
	        "    m[key] = value;\n"
	        "    set<uint32> s = set<uint32>(); s.add(key);\n"
	        "    ordered_map<uint32,int32> o = ordered_map<uint32,int32>();\n"
	        "    o[key] = 10;\n"
	        "    print(m[key]); print(s.has(key)); print(o[key]); print(Flags.All);\n"
	        "}\n",
	        "9\ntrue\n10\nFlags(4294967295)\n",
	        "int32 uint32 aliases and enum ABI",
	        {"enum class vs_Flags : std::uint32_t",
	            "vs_Map<std::uint32_t, int>",
	            "vs_Set<std::uint32_t>"}))
	{
		return EXIT_FAILURE;
	}

	{
		std::string IntMainCpp;
		std::string IntMainError;
		if (!Expect(
		        VeroScript::EmitCppFromSource(
		            "using ExitCode = int;\n"
		            "using Args = string[];\n"
		            "ExitCode main(Args args) { return len(args) + 19; }\n",
		            "aot-int-main.vs",
		            IntMainCpp,
		            IntMainError),
		        "W8 int main emission") ||
		    !Expect(
		        IntMainCpp.find("int vs_script_main") != std::string::npos && IntMainCpp.find("const int ___veroscript_main_exit_code = vs_script_main") != std::string::npos && IntMainCpp.find("return ___veroscript_main_exit_code;") != std::string::npos,
		        "W8 int main wrapper propagates exit code"))
		{
			std::cerr << IntMainError << '\n'
			          << IntMainCpp;
			return EXIT_FAILURE;
		}

		std::string ImplicitMainCpp;
		std::string ImplicitMainError;
		if (!Expect(
		        VeroScript::EmitCppFromSource(
		            "int main() {}\n",
		            "aot-implicit-int-main.vs",
		            ImplicitMainCpp,
		            ImplicitMainError),
		        "W8 implicit int main emission") ||
		    !Expect(
		        ImplicitMainCpp.find("int vs_script_main()\n{\n\treturn 0;\n}") != std::string::npos,
		        "W8 falling off int main emits return zero"))
		{
			std::cerr << ImplicitMainError << '\n'
			          << ImplicitMainCpp;
			return EXIT_FAILURE;
		}
	}
	if (!DualRun(
	        "using Args = string[];\n"
	        "int main(Args args) { print(len(args)); return 0; }\n",
	        "0\n",
	        "W8 native int main wrapper with aliased args",
	        {"const int ___veroscript_main_exit_code = vs_script_main(",
	            "return ___veroscript_main_exit_code;"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int Value = 3;\n"
	        "using Score = int;\n"
	        "int& value_ref() { return Value; }\n"
	        "const int& read_ref() { return Value; }\n"
	        "Score& alias_ref() { return Value; }\n"
	        "class RefBox<T> { public: T value; RefBox(T input) { value = input; } T& get() { return value; } };\n"
	        "void main() {\n"
	        "    value_ref() += 2;\n"
	        "    int& alias = value_ref();\n"
	        "    alias++;\n"
	        "    print(read_ref() + 1);\n"
	        "    fn()->int& getter = []() -> int& { return Value; };\n"
	        "    getter() = 9;\n"
	        "    print(Value);\n"
	        "    alias_ref() = 10;\n"
	        "    RefBox<int> box = RefBox<int>(40);\n"
	        "    box.get() += 2;\n"
	        "    print(alias_ref());\n"
	        "    print(box.get());\n"
	        "}\n",
	        "7\n9\n10\n42\n",
	        "reference returns",
	        {"vs_Function<int&()>", "int& vs_value_ref_", "T& get()"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "enum class SignedByte : int8 { Min = -128, Max = 127 }\n"
	        "enum class Byte : uint8 { Zero = 0u, Max = 255u }\n"
	        "enum class SignedWord : int16 { Min = -32768, Max = 32767 }\n"
	        "enum class Word : uint16 { Zero = 0u, Max = 65535u }\n"
	        "enum class RawByte : unsigned char { Max = 255u }\n"
	        "enum class RawWord : unsigned short { Max = 65535u }\n"
	        "int8 echo8(int8 value) { return value; }\n"
	        "uint16 echo16(uint16 value) { return value; }\n"
	        "int runtime_int(int value) { return value; }\n"
	        "int8 runtime_byte() { return runtime_int(125); }\n"
	        "void main() {\n"
	        "    array<int8> signed_values = array<int8>{-128, 127};\n"
	        "    array<uint16> words = array<uint16>{65535u, 1u};\n"
	        "    int8 mutable_byte = runtime_int(120);\n"
	        "    mutable_byte = runtime_int(121);\n"
	        "    mutable_byte += 2;\n"
	        "    mutable_byte++;\n"
	        "    uint8 shifted_byte = 1u;\n"
	        "    shifted_byte <<= 7;\n"
	        "    print(echo8(signed_values[0]));\n"
	        "    print(echo16(words[0]));\n"
	        "    print(mutable_byte); print(shifted_byte); print(runtime_byte());\n"
	        "    for (int8 loop = runtime_int(1); loop < 2; loop++) { print(loop); }\n"
	        "    print(1 << 5); print(-8 >> 2);\n"
	        "    print(SignedByte::Min); print(Byte::Max);\n"
	        "    print(SignedWord::Max); print(Word::Max);\n"
	        "    print(RawByte::Max); print(RawWord::Max);\n"
	        "}\n",
	        "-128\n65535\n124\n128\n125\n1\n32\n-2\nSignedByte(-128)\nByte(255)\n"
	        "SignedWord(32767)\nWord(65535)\n"
	        "RawByte(255)\nRawWord(65535)\n",
	        "int8 uint8 int16 uint16 aliases and enum ABI",
	        {"enum class vs_SignedByte : std::int8_t",
	            "enum class vs_Byte : std::uint8_t",
	            "enum class vs_SignedWord : std::int16_t",
	            "enum class vs_Word : std::uint16_t",
	            "enum class vs_RawByte : std::uint8_t",
	            "enum class vs_RawWord : std::uint16_t",
	            "vs_Array<std::uint16_t>",
	            "VsCheckedNumericCast<std::int8_t>",
	            "VsCheckedNumericCast<std::uint16_t>",
	            "VsCheckedShiftLeft", "VsCheckedShiftRight"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Box<T> {\n"
	        "public:\n"
	        "    T value;\n"
	        "    int normalized;\n"
	        "    Box(T input, int number) {\n"
	        "        value = input;\n"
	        "        normalized = normalize(number);\n"
	        "    }\n"
	        "}\n"
	        "int normalize(int value) { return value + 1; }\n"
	        "Box<array<T>> wrap<T>(array<T> values) {\n"
	        "    return Box<array<T>>(values, 8);\n"
	        "}\n"
	        "void main() {\n"
	        "    array<int> values = {3, 4};\n"
	        "    Box<array<int>> boxed = wrap<int>(values);\n"
	        "    print(boxed.value[1]);\n"
	        "    print(boxed.normalized);\n"
	        "}\n",
	        "4\n9\n",
	        "nested open generic constructor and early free prototype",
	        {"vs_Box_new<vs_Array<T>>", "vs_normalize"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Secret {\n"
	        "    friend int inspect(Secret value);\n"
	        "    int code = 44;\n"
	        "    int read() const { return code; }\n"
	        "public:\n"
	        "    Secret() {}\n"
	        "}\n"
	        "int inspect(Secret value) { return value.read(); }\n"
	        "void main() { print(inspect(Secret())); }\n",
	        "44\n",
	        "friend free function private method",
	        {"vs_inspect", "public:"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class Base {\n"
	        "protected:\n"
	        "    int code = 54;\n"
	        "}\n"
	        "class Derived : public Base {\n"
	        "    friend int inspect_derived(Derived value);\n"
	        "}\n"
	        "int inspect_derived(Derived value) { return value.code; }\n"
	        "void main() { print(inspect_derived(Derived())); }\n",
	        "54\n",
	        "friend function inherited protected member",
	        {"vs_inspect_derived", "vs_Derived"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "void choose(string value, int suffix = 1) { print(\"string\"); }\n"
	        "void choose(int value, int suffix = 2) { print(value + suffix); }\n"
	        "void widen(int64 value, int suffix = 1) { print(value + suffix); }\n"
	        "void widen(double value, int suffix = 2) { print(value + suffix); }\n"
	        "void select(ref int value, int suffix = 1) { value += suffix; print(value); }\n"
	        "void select(double value, int suffix = 2) { print(value + suffix); }\n"
	        "void main() {\n"
	        "    choose(5); choose(\"x\");\n"
	        "    float value = 5.5f; widen(value);\n"
	        "    select(5);\n"
	        "}\n",
	        "7\nstring\n7.5\n7\n",
	        "default-parameter overload resolution",
	        {"vs_choose", "vs_widen", "vs_select", "5", "\"x\""}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct Box {\n"
	        "    double value;\n"
	        "    Box(double input) { value = input; }\n"
	        "    double add(double amount, int suffix = 2) { return value + amount + suffix; }\n"
	        "};\n"
	        "void main() {\n"
	        "    float input = 1.5f;\n"
	        "    Box box(input);\n"
	        "    print(box.add(input));\n"
	        "}\n",
	        "5\n",
	        "constructor and method numeric/default resolution",
	        {"vs_Box_new", "vs_Box_add"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "enum class Token { Ready = 1 };\n"
	        "struct Counter { int value = 7; };\n"
	        "namespace Visible {\n"
	        "    int inspect(Counter self) { return self.value; }\n"
	        "    double adjust(Counter self, double amount = 2.0) { return self.value + amount; }\n"
	        "    int add_to(Counter self, ref int amount) { amount += self.value; return amount; }\n"
	        "    int route(ref Counter self) { return 1; }\n"
	        "    int route(Counter self) { return 2; }\n"
	        "    int enum_route(ref Token self) { return 33; }\n"
	        "    int enum_route(Token self) { return 44; }\n"
	        "    T echo<T>(T value) { return value; }\n"
	        "    T get<T>(Counter self, T value) { return value + self.value; }\n"
	        "}\n"
	        "namespace Separate { T get<T>(T value) { return value; } }\n"
	        "namespace More { int choose(int value) { return value + 1; } }\n"
	        "using namespace Visible;\n"
	        "using namespace More;\n"
	        "void main() {\n"
	        "    Counter counter();\n"
	        "    print(counter.inspect());\n"
	        "    float amount = 1.5f;\n"
	        "    print(counter.adjust(amount));\n"
	        "    int delta = 1;\n"
	        "    print(counter.add_to(&delta));\n"
	        "    print(Token::Ready.enum_route());\n"
	        "    const Counter fixed();\n"
	        "    print(fixed.route());\n"
	        "    print(echo<int>(8));\n"
	        "    print(counter.get<int>(4));\n"
	        "    print(Separate.get<int>(12));\n"
	        "    print(choose(9));\n"
	        "}\n",
	        "7\n8.5\n8\n44\n2\n8\n11\n12\n10\n",
	        "using namespace free, generic, and extension lookup",
	        {"vs_Visible", "vs_More"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "int DiscardCalls = 0;\n"
	        "int discard_touch(int value) { DiscardCalls += 1; return value; }\n"
	        "struct CastBox { int value; explicit CastBox(int input) { value = input; } };\n"
	        "void main() {\n"
	        "  (void) discard_touch(41);\n"
	        "  double source = 2.75;\n"
	        "  int converted = (int)source + 3;\n"
	        "  CastBox box = (CastBox)40;\n"
	        "  print(DiscardCalls); print(converted); print(box.value);\n"
	        "}\n",
	        "1\n5\n40\n",
	        "C-style cast sugar and explicit void discard AOT parity",
	        {}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "struct RootType {\n"
	        "    int value = 0;\n"
	        "    static int Marker = 50;\n"
	        "    RootType(int input) { value = input; }\n"
	        "};\n"
	        "using RootAlias = RootType;\n"
	        "struct RootBox<T> { T value; RootBox(T input) { value = input; } };\n"
	        "int RootValue = 10;\n"
	        "int choose() { return 20; }\n"
	        "enum class RootFlag { Ready = 30 };\n"
	        "namespace RootNs {\n"
	        "    int choose() { return 40; }\n"
	        "    int imported() { return 41; }\n"
	        "}\n"
	        "namespace Inner {\n"
	        "    struct RootType { int value = -1; };\n"
	        "    struct RootBox<T> { T value; };\n"
	        "    int choose() { return -20; }\n"
	        "    enum class RootFlag { Ready = -30 };\n"
	        "    namespace RootNs { int choose() { return -40; } }\n"
	        "    using namespace ::RootNs;\n"
	        "    void run() {\n"
	        "        using RootAlias = RootType;\n"
	        "        int RootValue = -10;\n"
	        "        ::RootType item(1);\n"
	        "        ::RootAlias aliased(2);\n"
	        "        ::RootBox<int> box(60);\n"
	        "        print(item.value);\n"
	        "        print(aliased.value);\n"
	        "        print(::RootValue);\n"
	        "        print(::choose());\n"
	        "        print(cast<int>(::RootFlag::Ready));\n"
	        "        print(::RootType::Marker);\n"
	        "        print(::RootNs::choose());\n"
	        "        print(imported());\n"
	        "        print(box.value);\n"
	        "    }\n"
	        "}\n"
	        "void main() { Inner::run(); }\n",
	        "1\n2\n10\n20\n30\n50\n40\n41\n60\n",
	        "leading global scope qualifier AOT parity",
	        {"vs_RootType", "vs_RootBox", "vs_choose"}))
	{
		return EXIT_FAILURE;
	}

	if (!DualRun(
	        "class ErrorBase { public: int code = 7; };\n"
	        "class ErrorChild : public ErrorBase {};\n"
	        "int guarded_return() {\n"
	        "    try { return 5; } finally { print(4); }\n"
	        "}\n"
	        "void main() {\n"
	        "    try { throw 6; } catch (int) { print(6); }\n"
	        "    try { throw 3; } catch (int value) { print(value); }\n"
	        "    try { throw \"x\"; } catch (int ignored) { print(0); }\n"
	        "    catch (string value) { print(value); } finally { print(2); }\n"
	        "    try { ErrorChild child(); throw child; }\n"
	        "    catch (ErrorBase error) { print(error.code); }\n"
	        "    try { try { throw 8; } catch (int value) { value = 9; throw; } }\n"
	        "    catch (int value) { print(value); }\n"
	        "    try { try { throw 10; } catch (int& value) { value = 11; throw; } }\n"
	        "    catch (int value) { print(value); }\n"
	        "    print(guarded_return());\n"
	        "}\n",
	        "6\n3\nx\n2\n7\n8\n11\n4\n5\n",
	        "typed/subtype exceptions, anonymous catch, finally, and return AOT parity",
	        {}))
	{
		return EXIT_FAILURE;
	}

	// Keep exception runtime headers self-contained: this source deliberately has
	// no class/container feature that could incidentally request <memory>.
	if (!DualRun(
	        "void main() { try { throw 13; } catch (int value) { print(value); } }\n",
	        "13\n",
	        "scalar-only exception AOT support",
	        {}))
	{
		return EXIT_FAILURE;
	}

	// A base-reference catch aliases the original value exception. Mutating it and
	// rethrowing must therefore be visible when a later handler catches the derived
	// struct, matching the VM's single exception object.
	if (!DualRun(
	        "struct ValueErrorBase { public: int code = 7; };\n"
	        "struct ValueErrorChild : public ValueErrorBase {};\n"
	        "void main() {\n"
	        "    try {\n"
	        "        try { ValueErrorChild error(); throw error; }\n"
	        "        catch (ValueErrorBase& error) { error.code = 17; throw; }\n"
	        "    } catch (ValueErrorChild error) { print(error.code); }\n"
	        "}\n",
	        "17\n",
	        "struct exception base-reference identity",
	        {}))
	{
		return EXIT_FAILURE;
	}

	// Class values are shared handles. Rebinding a base-reference catch must update
	// the exception's dynamic handle set before a bare rethrow: a replacement base
	// object must no longer match a later derived handler.
	if (!DualRun(
	        "class HandleErrorBase { public: int code = 1; };\n"
	        "class HandleErrorChild : public HandleErrorBase {};\n"
	        "void main() {\n"
	        "    try {\n"
	        "        try { HandleErrorChild error(); throw error; }\n"
	        "        catch (HandleErrorBase& error) {\n"
	        "            HandleErrorBase replacement();\n"
	        "            replacement.code = 19;\n"
	        "            error = replacement;\n"
	        "            throw;\n"
	        "        }\n"
	        "    } catch (HandleErrorChild error) { print(0); }\n"
	        "      catch (HandleErrorBase error) { print(error.code); }\n"
	        "}\n",
	        "19\n",
	        "class exception base-reference handle rebinding",
	        {}))
	{
		return EXIT_FAILURE;
	}

	// Gate BEFORE announcing success: a run where zero DualRuns were natively
	// verified (only soft-skips) must fail loudly rather than print "passed".
	if (gNativeDualMatched == 0 && gNativeDualSkipped > 0 && !EnvFlagEnabled("VEROSCRIPT_AOT_ALLOW_NATIVE_SKIP"))
	{
		std::cerr << "FAIL: no DualRun was natively verified (no C++ toolchain). "
		             "Install MSVC / g++ / clang++, or set VEROSCRIPT_AOT_CXX. "
		             "To allow skip explicitly: VEROSCRIPT_AOT_ALLOW_NATIVE_SKIP=1\n";
		return EXIT_FAILURE;
	}
	std::cout << "VeroScriptCppAotHelloTests passed"
	          << " (native DualRun matched=" << gNativeDualMatched
	          << " skipped=" << gNativeDualSkipped << ")\n";
	return EXIT_SUCCESS;
}
