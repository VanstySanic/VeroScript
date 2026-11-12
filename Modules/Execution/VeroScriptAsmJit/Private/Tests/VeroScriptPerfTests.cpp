// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptJit.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptRun.h"
#include "VeroScriptVm.h"
#include "VeroScriptAsmJit.h"
#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;

	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptPerfTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileScript(
	    const std::string_view Source, std::string* const OutError)
	{
		VeroScript::VeroScriptModuleLoader Loader("perf.vs");
		Loader.SetSourceOverlay("perf.vs", std::string(Source));
		const std::unique_ptr<VeroScript::FProgram> Program =
		    Loader.LoadSourceModule(Source, "perf.vs");
		if (!Program || Loader.HasError())
		{
			if (OutError)
			{
				*OutError = Loader.HasError() ? Loader.GetError().Message : "load failed";
			}
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			if (OutError)
			{
				*OutError = Compiler.GetError().Message;
			}
			return nullptr;
		}
		return Module;
	}

	bool RunScript(
	    const std::string_view Source,
	    std::string& OutOutput,
	    std::string* OutError = nullptr,
	    const VeroScript::EExecutionBackend Backend = VeroScript::EExecutionBackend::Vm,
	    VeroScript::FJitStats* const OutStats = nullptr)
	{
		auto Module = CompileScript(Source, OutError);
		if (!Module) return false;
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		std::string BackendError;
		if (!Vm.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), BackendError))
		{
			std::abort();
		}
		Vm.SetExecutionBackend(Backend);
		if (!Vm.Execute(*Module, VeroScript::ERunMode::Script))
		{
			if (OutError)
			{
				*OutError = Vm.GetError().Message;
			}
			return false;
		}
		OutOutput = Output.str();
		if (OutStats)
		{
			*OutStats = Vm.GetJitStats();
		}
		return true;
	}

	struct FBenchCase
	{
		const char* Name = nullptr;
		const char* Source = nullptr;
		const char* ExpectedOutput = nullptr;
		// Pre-optimization wall time (ms) from plan baseline measurements.
		double PreOptimizationMs = 0.0;
		// Required speedup vs PreOptimizationMs.
		double MinSpeedup = 5.0;
	};

	struct FBaselineMeasurement
	{
		std::string Name;
		std::string Backend;
		std::vector<double> SamplesMs;
	};

	const char* EntryKindName(const VeroScript::AsmJit::EJitEntryKind Kind)
	{
		switch (Kind)
		{
		case VeroScript::AsmJit::EJitEntryKind::Unsupported:
			return "Unsupported";
		case VeroScript::AsmJit::EJitEntryKind::NativeLeaf:
			return "NativeLeaf";
		case VeroScript::AsmJit::EJitEntryKind::NativeCfg:
			return "NativeCfg";
		case VeroScript::AsmJit::EJitEntryKind::BridgeTrampoline:
			return "BridgeTrampoline";
		case VeroScript::AsmJit::EJitEntryKind::LinearTrampoline:
			return "LinearTrampoline";
		case VeroScript::AsmJit::EJitEntryKind::VmTrampoline:
			return "VmTrampoline";
		default:
			{
				break;
			}
		}
		return "?";
	}

	const char* ScalarAbiName(const VeroScript::AsmJit::EJitScalarAbi Abi)
	{
		switch (Abi)
		{
		case VeroScript::AsmJit::EJitScalarAbi::None:
			return "none";
		case VeroScript::AsmJit::EJitScalarAbi::I32:
			return "i32";
		case VeroScript::AsmJit::EJitScalarAbi::U32:
			return "u32";
		case VeroScript::AsmJit::EJitScalarAbi::F64:
			return "f64";
		case VeroScript::AsmJit::EJitScalarAbi::Mixed:
			return "mixed";
		default:
			{
				break;
			}
		}
		return "?";
	}

	bool DumpJitEntries(const std::string_view Source, std::ostream& Out)
	{
		VeroScript::VeroScriptModuleLoader Loader("perf.vs");
		Loader.SetSourceOverlay("perf.vs", std::string(Source));
		const std::unique_ptr<VeroScript::FProgram> Program =
		    Loader.LoadSourceModule(Source, "perf.vs");
		if (!Program || Loader.HasError())
		{
			return false;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			return false;
		}
		VeroScript::AsmJit::FJitCodeCache Cache;
		Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
		const VeroScript::FJitStats Stats = Cache.GetStats();
		Out << "    jit_entries nativeLeaf=" << Stats.NativeLeafEntries
		    << " nativeCfg=" << Stats.NativeCfgEntries
		    << " linear=" << Stats.LinearTrampolineEntries
		    << " vm=" << Stats.VmTrampolineEntries
		    << " unsupported=" << Stats.UnsupportedEntries
		    << " inlineSites=" << Stats.InlinedCallSites << '\n';
		for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
		{
			const auto FnIndex = static_cast<std::uint16_t>(Index);
			const VeroScript::AsmJit::FJitFunctionEntry* Entry = Cache.GetCandidate(FnIndex);
			const char* Kind = Entry ? EntryKindName(Entry->Kind) : "missing";
			const bool bNativeCode = Entry && Entry->NativeCode != nullptr;
			Out << "      [" << Index << "] " << Module->Functions[Index].Name
			    << " -> " << Kind
			    << (bNativeCode ? " (+asmjit)" : "")
			    << (Entry && Entry->DirectScalarKind != VeroScript::AsmJit::EJitDirectScalarKind::None
			               ? " direct-scalar"
			               : "")
			    << (Entry && Entry->bDirectThisFieldAddModuloI32
			               ? " direct-field"
			               : "")
			    << (Entry && Entry->bUseScalarMapStorage ? " flat-map" : "")
			    << (Entry && Entry->bUseScalarSetStorage ? " flat-set" : "")
			    << (Entry && Entry->bUseScalarArrayStorage ? " flat-array" : "")
			    << (Entry && Entry->bPreferVmForExecution ? " prefer-vm" : "")
			    << (Entry ? " abi=" : "")
			    << (Entry ? ScalarAbiName(Entry->ScalarAbi) : "")
			    << '\n';
		}
		return true;
	}

	// Warmup once, then time compile+execute (same model as historical gates).
	double MeasureMs(
	    const FBenchCase& Case,
	    std::string& OutOutput,
	    std::string& OutError,
	    const VeroScript::EExecutionBackend Backend = VeroScript::EExecutionBackend::Vm,
	    VeroScript::FJitStats* const OutStats = nullptr)
	{
		if (!RunScript(Case.Source, OutOutput, &OutError, Backend, OutStats))
		{
			return -1.0;
		}
		const auto Begin = Clock::now();
		if (!RunScript(Case.Source, OutOutput, &OutError, Backend))
		{
			return -1.0;
		}
		const auto End = Clock::now();
		return std::chrono::duration<double, std::milli>(End - Begin).count();
	}

	// Fixed frontend costs must not turn a faster VM into a false JIT regression.
	// Cold compile+execute remains independently measured by MeasureMs.
	double MeasureExecutionMs(const FBenchCase& Case, std::string& OutOutput,
	    std::string& OutError, const VeroScript::EExecutionBackend Backend,
	    VeroScript::FJitStats* const OutStats)
	{
		auto Compiled = CompileScript(Case.Source, &OutError);
		if (!Compiled) return -1.0;
		std::shared_ptr<const VeroScript::FBytecodeModule> Module = std::move(Compiled);
		if (Module->MainFunctionIndex < 0) return -1.0;
		std::ostringstream Output;
		VeroScript::VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Vm.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), OutError) || !Vm.SetJitProgramOwner(Module, OutError))
		{
			return -1.0;
		}
		Vm.SetExecutionBackend(Backend);
		if (!Vm.Execute(*Module, VeroScript::ERunMode::Script))
		{
			OutError = Vm.GetError().Message;
			return -1.0;
		}
		if (Output.str() != Case.ExpectedOutput) return -1.0;
		Output.str({});
		const auto Begin = Clock::now();
		const bool Success = Vm.ExecuteFunction(*Module,
		    static_cast<std::uint16_t>(Module->MainFunctionIndex), {});
		const auto End = Clock::now();
		if (!Success)
		{
			OutError = Vm.GetError().Message;
			return -1.0;
		}
		OutOutput = Output.str();
		if (OutStats)
		{
			*OutStats = Vm.GetJitStats();
		}
		return std::chrono::duration<double, std::milli>(End - Begin).count();
	}

	bool WriteBaseline(
	    const std::string& Path,
	    const std::vector<FBaselineMeasurement>& Results)
	{
		std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
		if (!Out)
		{
			return false;
		}
		const char* Platform =
#if defined(_WIN32)
		    "windows";
#elif defined(__APPLE__)
		    "macos";
#elif defined(__linux__)
		    "linux";
#else
		    "unknown";
#endif
		const char* Architecture =
#if defined(_M_ARM64) || defined(__aarch64__)
		    "aarch64";
#elif defined(_M_X64) || defined(__x86_64__)
		    "x64";
#elif defined(_M_IX86) || defined(__i386__)
		    "x86";
#else
		    "unknown";
#endif
		const char* Compiler =
#if defined(_MSC_VER)
		    "msvc";
#elif defined(__clang__)
		    "clang";
#elif defined(__GNUC__)
		    "gcc";
#else
		    "unknown";
#endif
		const auto ReadEnvironment = [](const char* const Name)
		{
			std::string Value;
			#if defined(_WIN32)
			char* Buffer = nullptr;
			std::size_t Length = 0;
			if (_dupenv_s(&Buffer, &Length, Name) == 0 && Buffer)
			{
				Value.assign(Buffer, Length != 0 ? Length - 1 : 0);
			}
			std::free(Buffer);
			#else
			if (const char* const Buffer = std::getenv(Name))
			{
				Value = Buffer;
			}
			#endif
			return Value;
		};
		const std::string CommitEnv = ReadEnvironment("VEROSCRIPT_PERF_COMMIT");
		std::string MachineEnv = ReadEnvironment("VEROSCRIPT_PERF_MACHINE");
		if (MachineEnv.empty())
		{
			MachineEnv = ReadEnvironment("PROCESSOR_IDENTIFIER");
		}
		if (MachineEnv.empty())
		{
			MachineEnv = ReadEnvironment("RUNNER_NAME");
		}
		if (MachineEnv.empty())
		{
			MachineEnv = ReadEnvironment("HOSTNAME");
		}
		if (MachineEnv.empty())
		{
			MachineEnv = ReadEnvironment("COMPUTERNAME");
		}
		const std::string_view Commit = CommitEnv.empty()
		    ? std::string_view("unrecorded")
		    : std::string_view(CommitEnv);
		const std::string_view Machine = MachineEnv.empty()
		    ? std::string_view("unrecorded")
		    : std::string_view(MachineEnv);
		const auto WriteJsonString = [&Out](const std::string_view Text)
		{
			constexpr char HexDigits[] = "0123456789abcdef";
			Out << '"';
			for (const char Ch : Text)
			{
				const unsigned char Byte = static_cast<unsigned char>(Ch);
				switch (Ch)
				{
				case '"':
					{
						Out << "\\\"";
						break;
					}
				case '\\':
					{
						Out << "\\\\";
						break;
					}
				case '\b':
					{
						Out << "\\b";
						break;
					}
				case '\f':
					{
						Out << "\\f";
						break;
					}
				case '\n':
					{
						Out << "\\n";
						break;
					}
				case '\r':
					{
						Out << "\\r";
						break;
					}
				case '\t':
					{
						Out << "\\t";
						break;
					}
				default:
					{
						if (Byte < 0x20u)
						{
							Out << "\\u00"
							    << HexDigits[(Byte >> 4u) & 0x0Fu]
							    << HexDigits[Byte & 0x0Fu];
						}
						else
						{
							Out << Ch;
						}
						break;
					}
				}
			}
			Out << '"';
		};
		const std::uint64_t CplusplusVersion =
#if defined(_MSVC_LANG)
		    static_cast<std::uint64_t>(_MSVC_LANG);
#else
		    static_cast<std::uint64_t>(__cplusplus);
#endif
		const std::string CompilerVersion =
#if defined(_MSC_VER)
		    std::to_string(_MSC_VER);
#elif defined(__clang__)
		    std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
		        std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#else
		        "unknown";
#endif
		Out << "{\n"
		    << "  \"schema\": 2,\n"
		    << "  \"metadata\": {\n"
		    << "    \"platform\": \"" << Platform << "\",\n"
		    << "    \"architecture\": \"" << Architecture << "\",\n"
		    << "    \"compiler\": \"" << Compiler << "\",\n"
		    << "    \"compiler_version\": \"" << CompilerVersion << "\",\n"
		    << "    \"cplusplus\": " << CplusplusVersion << ",\n"
		    << "    \"build\": \""
#if defined(NDEBUG)
		    << "release"
#else
		    << "debug"
#endif
		    << "\",\n"
		    << "    \"hardware_threads\": " << std::thread::hardware_concurrency() << ",\n"
		    << "    \"machine\": ";
		WriteJsonString(Machine);
		Out << ",\n    \"commit\": ";
		WriteJsonString(Commit);
		Out << "\n  },\n  \"results\": {\n";
		for (std::size_t Index = 0; Index < Results.size(); ++Index)
		{
			const FBaselineMeasurement& Result = Results[Index];
			if (Result.SamplesMs.empty())
			{
				return false;
			}
			std::vector<double> Samples = Result.SamplesMs;
			std::sort(Samples.begin(), Samples.end());
			const double Median = Samples[Samples.size() / 2u];
			Out << "    \"" << Result.Name << "\": {\n"
			    << "      \"backend\": ";
			WriteJsonString(Result.Backend);
			Out << ",\n      \"sample_count\": " << Samples.size()
			    << ",\n      \"min_ms\": " << Samples.front()
			    << ",\n      \"median_ms\": " << Median
			    << ",\n      \"max_ms\": " << Samples.back() << "\n    }";
			if (Index + 1 < Results.size())
			{
				Out << ',';
			}
			Out << '\n';
		}
		Out << "  }\n}\n";
		return true;
	}

	int RunBackendCompare(const std::string_view CaseFilter)
	{
		// In-process only (no process spawn). Each sample = compile + execute after one warmup.
		const FBenchCase Cases[] = {
		    {
		        "main_int_loop",
		        "void main() {\n"
		        "\tint s = 0;\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 5000000) {\n"
		        "\t\ts = s + 1;\n"
		        "\t\ti = i + 1;\n"
		        "\t}\n"
		        "\tprint(s);\n"
		        "}\n",
		        "5000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_int_loop",
		        "int work() {\n"
		        "\tint s = 0;\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 5000000) {\n"
		        "\t\ts = s + 1;\n"
		        "\t\ti = i + 1;\n"
		        "\t}\n"
		        "\treturn s;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "5000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_int_call",
		        "int add(int a, int b) { return a + b; }\n"
		        "int work() {\n"
		        "\tint s = 0;\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 2000000) {\n"
		        "\t\ts = add(s, 1);\n"
		        "\t\ti = i + 1;\n"
		        "\t}\n"
		        "\treturn s;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "2000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "direct_native_free_call",
		        "int transform_loop(int value) { for (int i = 0; i < 4; i++) { "
		        "value = (value * 3 + 1) % 10007; } return value; }\n"
		        "int work() { int total = 0; for (int i = 0; i < 100000; i++) { "
		        "total = (total + transform_loop(i & 255)) % 1000003; } return total; }\n"
		        "void main() { print(work()); }\n",
		        "481283\n",
		        0.0,
		        1.0,
		    },
		    {
		        "direct_native_f64_call",
		        "double transform_loop_f64(double value) { "
		        "for (double i = 0.0; i < 4.0; i += 1.0) { "
		        "value = value * 1.25 + 0.5; } return value; }\n"
		        "double work() { double total = 0.0; "
		        "for (double i = 0.0; i < 10000.0; i += 1.0) { "
		        "total += transform_loop_f64(i); } return total; }\n"
		        "void main() { print(to_int(work())); }\n",
		        "122086933\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_int64_loop",
		        "int64 work() {\n"
		        "\tint64 total = 0i64;\n"
		        "\tint64 i = 0i64;\n"
		        "\twhile (i < 5000000i64) { total = total + 1i64; i = i + 1i64; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(to_int(work())); }\n",
		        "5000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_float_loop",
		        "float work() {\n"
		        "\tfloat total = 0.f;\n"
		        "\tfloat i = 0.f;\n"
		        "\twhile (i < 1000000.f) { total = total + 1.f; i = i + 1.f; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(to_int(work())); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_method",
		        // Hot loop uses locals inside the method (NativeCfg ≈ leaf_int_loop).
		        "class Counter {\n"
		        "public:\n"
		        "\tint n = 0;\n"
		        "\tint run(int limit) {\n"
		        "\t\tint s = n;\n"
		        "\t\tint i = 0;\n"
		        "\t\twhile (i < limit) {\n"
		        "\t\t\ts = s + 1;\n"
		        "\t\t\ti = i + 1;\n"
		        "\t\t}\n"
		        "\t\tn = s;\n"
		        "\t\treturn s;\n"
		        "\t}\n"
		        "}\n"
		        "int work() {\n"
		        "\tCounter c = Counter();\n"
		        "\treturn c.run(5000000);\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "5000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_this_call",
		        // Nested this.bump()/add still NativeCfg; measures method-call overhead on JIT.
		        "class Acc {\n"
		        "public:\n"
		        "\tint n = 0;\n"
		        "\tvoid add(int x) { n = n + x; }\n"
		        "\tvoid bump() { this.add(1); }\n"
		        "\tint get() { return n; }\n"
		        "\tint run(int limit) {\n"
		        "\t\tint i = 0;\n"
		        "\t\twhile (i < limit) {\n"
		        "\t\t\tthis.bump();\n"
		        "\t\t\ti = i + 1;\n"
		        "\t\t}\n"
		        "\t\treturn this.get();\n"
		        "\t}\n"
		        "}\n"
		        "int work() {\n"
		        "\tAcc a = Acc();\n"
		        "\treturn a.run(2000000);\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "2000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "ordinary_instance_method",
		        "class Increment { public: int apply(int value) { return value + 1; } };\n"
		        "int work(int limit) { Increment operation = Increment(); int total = 0; "
		        "for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
		        "return total; }\n"
		        "void main() { print(work(1000000)); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "nested_local_this_method",
		        "class NestedCounter { public: int value = 0; "
		        "void add(int delta) { value += delta; } "
		        "void bump() { this.add(1); } }\n"
		        "int work(int limit) { NestedCounter value = NestedCounter(); "
		        "for (int i = 0; i < limit; i++) { value.bump(); } "
		        "return value.value; }\n"
		        "void main() { print(work(2000000)); }\n",
		        "2000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "ordinary_instance_method_uint",
		        "class UIncrement { public: uint32 apply(uint32 value) { return value + 1u; } };\n"
		        "uint32 work(int limit) { UIncrement operation = UIncrement(); uint32 total = 0u; "
		        "for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
		        "return total; }\n"
		        "void main() { print(to_int(work(1000000))); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "ordinary_instance_method_double",
		        "class DIncrement { public: double apply(double value) { return value + 1.0; } };\n"
		        "double work(int limit) { DIncrement operation = DIncrement(); double total = 0.0; "
		        "for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
		        "return total; }\n"
		        "void main() { print(to_int(work(1000000))); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "ordinary_instance_method_int64",
		        "class I64Increment { public: int64 apply(int64 value) { return value + 1i64; } };\n"
		        "int64 work(int limit) { I64Increment operation = I64Increment(); int64 total = 0i64; "
		        "for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
		        "return total; }\n"
		        "void main() { print(to_int(work(1000000))); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "ordinary_instance_method_uint64",
		        "class U64Increment { public: uint64 apply(uint64 value) { return value + 1u64; } };\n"
		        "uint64 work(int limit) { U64Increment operation = U64Increment(); uint64 total = 0u64; "
		        "for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
		        "return total; }\n"
		        "void main() { print(to_int(work(1000000))); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "ordinary_instance_method_float",
		        "class FIncrement { public: float apply(float value) { return value + 1.f; } };\n"
		        "float work(int limit) { FIncrement operation = FIncrement(); float total = 0.f; "
		        "for (int i = 0; i < limit; i++) { total = operation.apply(total); } "
		        "return total; }\n"
		        "void main() { print(to_int(work(1000000))); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "short_lived_object",
		        "class Pair { public: int first = 1; int second = 2; "
		        "int sum() { return first + second; } };\n"
		        "int work(int limit) { int total = 0; for (int i = 0; i < limit; i++) { "
		        "Pair value = Pair(); total += value.sum(); } return total; }\n"
		        "void main() { print(work(100000)); }\n",
		        "300000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "short_lived_object_double",
		        "class DPair { public: double first = 1.0; double second = 2.0; "
		        "double sum() { return first + second; } };\n"
		        "double work(int limit) { double total = 0.0; for (int i = 0; i < limit; i++) { "
		        "DPair value = DPair(); total += value.sum(); } return total; }\n"
		        "void main() { print(to_int(work(100000))); }\n",
		        "300000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_this_call_uint",
		        "class UAcc {\n"
		        "public:\n"
		        "\tuint32 n = 0u;\n"
		        "\tvoid add(uint32 x) { n = n + x; }\n"
		        "\tvoid bump() { this.add(1u); }\n"
		        "\tuint32 get() { return n; }\n"
		        "\tuint32 run(uint32 limit) {\n"
		        "\t\tuint32 i = 0u;\n"
		        "\t\twhile (i < limit) {\n"
		        "\t\t\tthis.bump();\n"
		        "\t\t\ti = i + 1u;\n"
		        "\t\t}\n"
		        "\t\treturn this.get();\n"
		        "\t}\n"
		        "}\n"
		        "uint32 work() {\n"
		        "\tUAcc a = UAcc();\n"
		        "\treturn a.run(2000000u);\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "2000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_this_call_double",
		        "class DAcc {\n"
		        "public:\n"
		        "\tdouble n = 0.0;\n"
		        "\tvoid add(double x) { n = n + x; }\n"
		        "\tvoid bump() { this.add(1.0); }\n"
		        "\tdouble get() { return n; }\n"
		        "\tdouble run(double limit) {\n"
		        "\t\tdouble i = 0.0;\n"
		        "\t\twhile (i < limit) {\n"
		        "\t\t\tthis.bump();\n"
		        "\t\t\ti = i + 1.0;\n"
		        "\t\t}\n"
		        "\t\treturn this.get();\n"
		        "\t}\n"
		        "}\n"
		        "double work() {\n"
		        "\tDAcc a = DAcc();\n"
		        "\treturn a.run(1000000.0);\n"
		        "}\n"
		        "void main() { print(to_int(work())); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_this_call_int64",
		        "class I64Acc {\n"
		        "public:\n"
		        "\tint64 n = 0i64;\n"
		        "\tvoid add(int64 x) { n = n + x; }\n"
		        "\tvoid bump() { this.add(1i64); }\n"
		        "\tint64 get() { return n; }\n"
		        "\tint64 run(int64 limit) {\n"
		        "\t\tint64 i = 0i64;\n"
		        "\t\twhile (i < limit) { this.bump(); i = i + 1i64; }\n"
		        "\t\treturn this.get();\n"
		        "\t}\n"
		        "}\n"
		        "int64 work() { I64Acc a = I64Acc(); return a.run(1000000i64); }\n"
		        "void main() { print(to_int(work())); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_this_call_uint64",
		        "class U64Acc {\n"
		        "public:\n"
		        "\tuint64 n = 0u64;\n"
		        "\tvoid add(uint64 x) { n = n + x; }\n"
		        "\tvoid bump() { this.add(1u64); }\n"
		        "\tuint64 get() { return n; }\n"
		        "\tuint64 run(uint64 limit) {\n"
		        "\t\tuint64 i = 0u64;\n"
		        "\t\twhile (i < limit) { this.bump(); i = i + 1u64; }\n"
		        "\t\treturn this.get();\n"
		        "\t}\n"
		        "}\n"
		        "uint64 work() { U64Acc a = U64Acc(); return a.run(1000000u64); }\n"
		        "void main() { print(to_int(work())); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "leaf_this_call_float",
		        "class FAcc {\n"
		        "public:\n"
		        "\tfloat n = 0.f;\n"
		        "\tvoid add(float x) { n = n + x; }\n"
		        "\tvoid bump() { this.add(1.f); }\n"
		        "\tfloat get() { return n; }\n"
		        "\tfloat run(float limit) {\n"
		        "\t\tfloat i = 0.f;\n"
		        "\t\twhile (i < limit) { this.bump(); i = i + 1.f; }\n"
		        "\t\treturn this.get();\n"
		        "\t}\n"
		        "}\n"
		        "float work() { FAcc a = FAcc(); return a.run(1000000.f); }\n"
		        "void main() { print(to_int(work())); }\n",
		        "1000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_inline_branch",
		        "int max_leaf(int a, int b) {\n"
		        "\tif (a < b) { a = b; }\n"
		        "\treturn a;\n"
		        "}\n"
		        "int work() {\n"
		        "\tint s = 0;\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 2000000) {\n"
		        "\t\ts = max_leaf(s, i);\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn s;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "1999999\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_dispatch",
		        "interface I { virtual int value() = 0; }\n"
		        "class A : I { int value() { return 1; } }\n"
		        "class B : I { int value() { return 2; } }\n"
		        "int work(I a, I b, int limit) {\n"
		        "\tint total = 0; int i = 0;\n"
		        "\twhile (i < limit) { total = total + a.value(); "
		        "total = total + b.value(); i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { A a = A(); B b = B(); print(work(a, b, 100000)); }\n",
		        "300000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_dispatch_args",
		        "interface I { virtual int add(int x) = 0; }\n"
		        "class A : I { int add(int x) { return x + 1; } }\n"
		        "class B : I { int add(int x) { return x + 2; } }\n"
		        "int work(I a, I b, int limit) {\n"
		        "\tint total = 0; int i = 0; int step = 1;\n"
		        "\twhile (i < limit) { total = total + a.add(step); "
		        "total = total + b.add(step); i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { A a = A(); B b = B(); print(work(a, b, 100000)); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_dispatch_four_types",
		        "interface I { virtual int add(int x) = 0; }\n"
		        "class A : I { int add(int x) { return x + 1; } }\n"
		        "class B : I { int add(int x) { return x + 2; } }\n"
		        "class C : I { int add(int x) { return x + 3; } }\n"
		        "class D : I { int add(int x) { return x + 4; } }\n"
		        "int work(I a, I b, I c, I d, int limit) {\n"
		        "\tI current = a; int total = 0; int i = 0;\n"
		        "\twhile (i < limit) { int choice = i % 4;\n"
		        "\t\tif (choice == 0) current = a;\n"
		        "\t\telse if (choice == 1) current = b;\n"
		        "\t\telse if (choice == 2) current = c; else current = d;\n"
		        "\t\ttotal = total + current.add(1); i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { A a = A(); B b = B(); C c = C(); D d = D(); "
		        "print(work(a, b, c, d, 200000)); }\n",
		        "700000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_dispatch_uint_args",
		        "interface I { virtual unsigned int add(unsigned int x) = 0; }\n"
		        "class A : I { unsigned int add(unsigned int x) { return x + 1u; } }\n"
		        "class B : I { unsigned int add(unsigned int x) { return x + 2u; } }\n"
		        "unsigned int work(I a, I b, int limit) {\n"
		        "\tunsigned int total = 0u; int i = 0; unsigned int step = 1u;\n"
		        "\twhile (i < limit) { total = total + a.add(step); "
		        "total = total + b.add(step); i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { A a = A(); B b = B(); print(work(a, b, 100000)); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_dispatch_int64_raw",
		        "interface I64Op { virtual int64 add(int64 a, int64 b) = 0; }\n"
		        "class I64Add : I64Op { int64 add(int64 a, int64 b) { return a + b; } }\n"
		        "int64 work(I64Op op, int limit) {\n"
		        "\tint64 total = 0i64; int i = 0;\n"
		        "\twhile (i < limit) { total = op.add(total, 1i64); i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { I64Add op = I64Add(); print(work(op, 100000)); }\n",
		        "100000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_dispatch_float_raw",
		        "interface F32Op { virtual float add(float a, float b) = 0; }\n"
		        "class F32Add : F32Op { float add(float a, float b) { return a + b; } }\n"
		        "float work(F32Op op, int limit) {\n"
		        "\tfloat total = 0.f; int i = 0;\n"
		        "\twhile (i < limit) { total = op.add(total, 1.f); i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { F32Add op = F32Add(); print(to_int(work(op, 100000))); }\n",
		        "100000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_fields",
		        "class FieldRich { public:\n"
		        "\tint f0 = 0; int f1 = 0; int f2 = 0; int f3 = 0;\n"
		        "\tint f4 = 0; int f5 = 0; int f6 = 0; int f7 = 0;\n"
		        "\tint f8 = 0; int f9 = 0; int f10 = 0; int f11 = 0;\n"
		        "\tint f12 = 0; int f13 = 0; int f14 = 0; int f15 = 0;\n"
		        "}\n"
		        "int work(FieldRich value, int limit) {\n"
		        "\tint i = 0;\n"
		        "\twhile (i < limit) {\n"
		        "\t\tvalue.f0 = value.f0 + 1; value.f1 = value.f1 + 2;\n"
		        "\t\tvalue.f2 = value.f2 + 3; value.f3 = value.f3 + 4;\n"
		        "\t\tvalue.f4 = value.f4 + 5; value.f5 = value.f5 + 6;\n"
		        "\t\tvalue.f6 = value.f6 + 7; value.f7 = value.f7 + 8;\n"
		        "\t\tvalue.f8 = value.f8 + 9; value.f9 = value.f9 + 10;\n"
		        "\t\tvalue.f10 = value.f10 + 11; value.f11 = value.f11 + 12;\n"
		        "\t\tvalue.f12 = value.f12 + 13; value.f13 = value.f13 + 14;\n"
		        "\t\tvalue.f14 = value.f14 + 15; value.f15 = value.f15 + 16;\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn value.f0 + value.f1 + value.f2 + value.f3\n"
		        "\t\t+ value.f4 + value.f5 + value.f6 + value.f7\n"
		        "\t\t+ value.f8 + value.f9 + value.f10 + value.f11\n"
		        "\t\t+ value.f12 + value.f13 + value.f14 + value.f15;\n"
		        "}\n"
		        "void main() { FieldRich value = FieldRich(); print(work(value, 20000)); }\n",
		        "2720000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_field_accumulate",
		        "class FieldValue { public: int value = 3; }\n"
		        "int work(FieldValue object, int limit) {\n"
		        "\tint total = 0;\n"
		        "\tfor (int i = 0; i < limit; i++) { total += object.value; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { FieldValue object = FieldValue(); "
		        "print(work(object, 1000000)); }\n",
		        "3000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_field_accumulate_while",
		        "class FieldValue { public: int value = 3; }\n"
		        "int work(FieldValue object, int limit) {\n"
		        "\tint total = 0; int i = 0;\n"
		        "\twhile (i < limit) { total += object.value; i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { FieldValue object = FieldValue(); "
		        "print(work(object, 1000000)); }\n",
		        "3000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_field_accumulate_double",
		        "class FieldValue { public: double value = 3.0; }\n"
		        "double work(FieldValue object, int limit) {\n"
		        "\tdouble total = 0.0;\n"
		        "\tfor (int i = 0; i < limit; i++) { total += object.value; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { FieldValue object = FieldValue(); "
		        "print(to_int(work(object, 1000000))); }\n",
		        "3000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_field_accumulate_int64",
		        "class FieldValue { public: int64 value = 3i64; }\n"
		        "int64 work(FieldValue object, int limit) {\n"
		        "\tint64 total = 0i64;\n"
		        "\tfor (int i = 0; i < limit; i++) { total += object.value; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { FieldValue object = FieldValue(); "
		        "print(to_int(work(object, 1000000))); }\n",
		        "3000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_object_fields_compound",
		        "class FieldRich { public:\n"
		        "\tint f0 = 0; int f1 = 0; int f2 = 0; int f3 = 0;\n"
		        "\tint f4 = 0; int f5 = 0; int f6 = 0; int f7 = 0;\n"
		        "\tint f8 = 0; int f9 = 0; int f10 = 0; int f11 = 0;\n"
		        "\tint f12 = 0; int f13 = 0; int f14 = 0; int f15 = 0;\n"
		        "}\n"
		        "int work(FieldRich value, int limit) {\n"
		        "\tint i = 0;\n"
		        "\twhile (i < limit) {\n"
		        "\t\tvalue.f0 += 1; value.f1 += 2; value.f2 += 3; value.f3 += 4;\n"
		        "\t\tvalue.f4 += 5; value.f5 += 6; value.f6 += 7; value.f7 += 8;\n"
		        "\t\tvalue.f8 += 9; value.f9 += 10; value.f10 += 11; value.f11 += 12;\n"
		        "\t\tvalue.f12 += 13; value.f13 += 14; value.f14 += 15; value.f15 += 16;\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn value.f0 + value.f1 + value.f2 + value.f3\n"
		        "\t\t+ value.f4 + value.f5 + value.f6 + value.f7\n"
		        "\t\t+ value.f8 + value.f9 + value.f10 + value.f11\n"
		        "\t\t+ value.f12 + value.f13 + value.f14 + value.f15;\n"
		        "}\n"
		        "void main() { FieldRich value = FieldRich(); print(work(value, 20000)); }\n",
		        "2720000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_array_storage",
		        "int work() {\n"
		        "\tarray<int> values = array<int>();\n"
		        "\tvalues.reserve(50000);\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 50000) { values.push(i); i++; }\n"
		        "\tint total = 0; i = 0;\n"
		        "\twhile (i < 50000) { total = total + values[i]; i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "1249975000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_array_expression_storage",
		        "int work() {\n"
		        "\tarray<int> values = array<int>(); values.reserve(50000);\n"
		        "\tint i = 0; while (i < 50000) { values.push(i % 1024); i++; }\n"
		        "\tint total = 0; i = 0;\n"
		        "\twhile (i < 50000) { total += values[i]; i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "25500376\n",
		        0.0,
		        1.0,
		    },
		    {
		        "typed_array_random_read_write",
		        "int work() {\n"
		        "\tarray<int> values = array<int>(); values.reserve(65536);\n"
		        "\tint i = 0; while (i < 65536) { values.push(0); i++; }\n"
		        "\tint state = 1; int total = 0; i = 0;\n"
		        "\twhile (i < 1000000) {\n"
		        "\t\tstate = (state * 25173 + 13849) % 65536;\n"
		        "\t\tvalues[state] = values[state] + 1;\n"
		        "\t\ttotal = (total + values[state]) % 1000000007;\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn total + values[state];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "8135696\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_array_read_hot",
		        "int work() {\n"
		        "\tarray<int> values = array<int>(); values.push(7);\n"
		        "\tint total = 0; int i = 0;\n"
		        "\twhile (i < 1000000) { total += values[0]; i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "7000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_array_write_hot",
		        "int work() {\n"
		        "\tint seed = 3; array<int> values = {seed};\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 500000) { values[0] = i + seed; i++; }\n"
		        "\treturn values[0];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500002\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_set_add_hot",
		        "int work() {\n"
		        "\tset<int> values = set<int>(); values.reserve(500000);\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 500000) { values.add(i); i++; }\n"
		        "\treturn len(values);\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_set_has_hot",
		        "int work() {\n"
		        "\tset<int> values = {7};\n"
		        "\tint total = 0; int i = 0;\n"
		        "\twhile (i < 500000) { if (values.has(7)) { total++; } i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_set_expression_has_hot",
		        "int work() {\n"
		        "\tset<int> values = set<int>(); values.reserve(4096);\n"
		        "\tint i = 0; while (i < 4096) { values.add(i); i++; }\n"
		        "\tint total = 0; i = 0;\n"
		        "\twhile (i < 500000) { if (values.has(i % 8192)) { total++; } i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "250144\n",
		        0.0,
		        1.0,
		    },
		    {
		        "string_set_has_hot",
		        "int work() {\n"
		        "\tset<string> values = {\"needle\"};\n"
		        "\tstring needle = \"needle\"; int total = 0; int i = 0;\n"
		        "\twhile (i < 500000) { if (values.has(needle)) { total++; } i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "string_equal_hot",
		        "int work() {\n"
		        "\tstring a = \"same\"; string b = \"same\";\n"
		        "\tint total = 0; int i = 0;\n"
		        "\twhile (i < 500000) { if (a == b) { total++; } i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "string_conditional_equal_hot",
		        "int work(int limit) {\n"
		        "\tstring same_a = \"the same benchmark string\";\n"
		        "\tstring same_b = \"the same benchmark string\";\n"
		        "\tstring other = \"a different benchmark string\";\n"
		        "\tint total = 0;\n"
		        "\tfor (int i = 0; i < limit; i++) {\n"
		        "\t\tif ((i % 8 == 0 ? other : same_a) == same_b) total++;\n"
		        "\t}\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work(200000)); }\n",
		        "175000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "exception_catch_hot",
		        "int work() {\n"
		        "\tint total = 0;\n"
		        "\tfor (int i = 0; i < 50000; i++) {\n"
		        "\t\ttry { throw i; } catch (int value) { total += value; }\n"
		        "\t}\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "1249975000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_map_storage",
		        "int work() {\n"
		        "\tmap<int,int> values = map<int,int>();\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 500000) {\n"
		        "\t\tvalues[i] = i + 1;\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn values[499999];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_map_direct_storage",
		        "int work() {\n"
		        "\tmap<int,int> values = map<int,int>();\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 500000) { values[i] = i; i++; }\n"
		        "\treturn values[499999];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "499999\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_map_read_hot",
		        "int work() {\n"
		        "\tmap<int,int> values = map<int,int>();\n"
		        "\tvalues[0] = 7;\n"
		        "\tint total = 0; int i = 0;\n"
		        "\twhile (i < 500000) { total += values[0]; i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "3500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "string_map_read_hot",
		        "int work() {\n"
		        "\tmap<string,int> values = map<string,int>();\n"
		        "\tstring key = \"needle\"; values[key] = 7;\n"
		        "\tint total = 0; int i = 0;\n"
		        "\twhile (i < 500000) { total += values[key]; i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "3500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_map_expression_read_hot",
		        "int work() {\n"
		        "\tmap<int,int> values = map<int,int>(); values.reserve(4096);\n"
		        "\tint i = 0; while (i < 4096) { values[i] = i % 1024; i++; }\n"
		        "\tint total = 0; i = 0;\n"
		        "\twhile (i < 500000) { "
		        "total = (total + values[i % 4096]) % 1000000007; i++; }\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "255644016\n",
		        0.0,
		        1.0,
		    },
		    {
		        "string_set_random_key_has",
		        "int work() {\n"
		        "\tset<string> values = {\"alpha\", \"beta\", \"gamma\", \"delta\"};\n"
		        "\tarray<string> keys = {\"alpha\", \"beta\", \"gamma\", \"delta\", \"missing\"};\n"
		        "\tint state = 1; int total = 0; int i = 0;\n"
		        "\twhile (i < 1000000) {\n"
		        "\t\tstate = (state * 25173 + 13849) % 65536;\n"
		        "\t\tif (values.has(keys[state % 5])) { total++; }\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "800098\n",
		        0.0,
		        1.0,
		    },
		    {
		        "typed_set_random_has",
		        "int work() {\n"
		        "\tset<int> values = set<int>(); values.reserve(32768);\n"
		        "\tint i = 0; while (i < 65536) { if ((i % 2) == 0) values.add(i); i++; }\n"
		        "\tint state = 1; int total = 0; i = 0;\n"
		        "\twhile (i < 1000000) {\n"
		        "\t\tstate = (state * 25173 + 13849) % 65536;\n"
		        "\t\tif (values.has(state)) { total++; }\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn total;\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "typed_map_random_read_write",
		        "int work() {\n"
		        "\tmap<int,int> values = map<int,int>(); values.reserve(65536);\n"
		        "\tint i = 0; while (i < 65536) { values[i] = 0; i++; }\n"
		        "\tint state = 1; int total = 0; i = 0;\n"
		        "\twhile (i < 1000000) {\n"
		        "\t\tstate = (state * 25173 + 13849) % 65536;\n"
		        "\t\tvalues[state] = values[state] + 1;\n"
		        "\t\ttotal = (total + values[state]) % 1000000007;\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn total + values[state];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "8135696\n",
		        0.0,
		        1.0,
		    },
		    {
		        "typed_map_double_random_update",
		        "double work() {\n"
		        "\tmap<int,double> values = map<int,double>(); values.reserve(65536);\n"
		        "\tint i = 0; while (i < 65536) { values[i] = 1.0; i++; }\n"
		        "\tint state = 1; i = 0; while (i < 1000000) {\n"
		        "\t\tstate = (state * 25173 + 13849) % 65536;\n"
		        "\t\tvalues[state] = values[state] + 0.5; i++;\n"
		        "\t}\n"
		        "\treturn values[state];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "9\n",
		        0.0,
		        1.0,
		    },
		    {
		        "typed_map_uint_random_update",
		        "uint32 work() {\n"
		        "\tmap<int,uint32> values = map<int,uint32>(); values.reserve(65536);\n"
		        "\tint i = 0; while (i < 65536) { values[i] = 0u; i++; }\n"
		        "\tint state = 1; i = 0; while (i < 1000000) {\n"
		        "\t\tstate = (state * 25173 + 13849) % 65536;\n"
		        "\t\tvalues[state] = values[state] + 1u; i++;\n"
		        "\t}\n"
		        "\treturn values[state];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "16\n",
		        0.0,
		        1.0,
		    },
		    {
		        "bound_method_field_update",
		        "class Counter { public: int value = 0; "
		        "void add(int delta) { value = (value + delta) % 1000000007; } };\n"
		        "int work() { Counter value = Counter(); int i = 0; "
		        "while (i < 200000) { value.add(i % 97); i++; } "
		        "return value.value; }\n"
		        "void main() { print(work()); }\n",
		        "9599419\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_transform_pair_loop",
		        "interface IOperation { virtual int step(int value) = 0; };\n"
		        "class AddThree : IOperation { public: int step(int value) { "
		        "return (value + 3) % 1000000007; } };\n"
		        "class AddFive : IOperation { public: int step(int value) { "
		        "return (value + 5) % 1000000007; } };\n"
		        "int pair_once(IOperation first, IOperation second) { int total = 0; "
		        "total = first.step(total); total = second.step(total); return total; }\n"
		        "int work(IOperation first, IOperation second, int limit) { int total = 0; "
		        "for (int i = 0; i < limit; i++) { total = first.step(total); "
		        "total = second.step(total); } return total; }\n"
		        "void main() { AddThree first = AddThree(); AddFive second = AddFive(); "
		        "print(pair_once(first, second)); print(work(first, second, 200000)); }\n",
		        "8\n1600000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "dynamic_transform_replaced_receiver",
		        "interface IOperation { virtual int step(int value) = 0; };\n"
		        "class AddThree : IOperation { public: int step(int value) { "
		        "return (value + 3) % 1000000007; } };\n"
		        "class AddFive : IOperation { public: int step(int value) { "
		        "return (value + 5) % 1000000007; } };\n"
		        "int work(int limit) { IOperation operation = AddThree(); int total = 0; "
		        "for (int i = 0; i < limit; i++) { "
		        "if ((i % 2) == 0) { operation = AddThree(); } "
		        "else { operation = AddFive(); } "
		        "total = operation.step(total); } return total; }\n"
		        "void main() { print(work(10000)); }\n",
		        "40000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "scalar_map_storage_reserved",
		        "int work() {\n"
		        "\tmap<int,int> values = map<int,int>();\n"
		        "\tvalues.reserve(500000);\n"
		        "\tint i = 0;\n"
		        "\twhile (i < 500000) {\n"
		        "\t\tvalues[i] = i + 1;\n"
		        "\t\ti++;\n"
		        "\t}\n"
		        "\treturn values[499999];\n"
		        "}\n"
		        "void main() { print(work()); }\n",
		        "500000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "general_i32_mod_loop",
		        "int work() { int total = 0; for (int i = 0; i < 20000000; i++) { "
		        "total = (total + (i % 97)) % 1000000007; } return total; } "
		        "void main() { print(work()); }\n",
		        "959998845\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_double_indexed_recurrence",
		        "int work(int limit) { array<double> table = {0.125, -0.75, 1.5, "
		        "0.0625, -1.25, 0.875, 0.375}; int state = 17; double value = 0.25; "
		        "for (int i = 0; i < limit; i++) { state = "
		        "(state * 25173 + 13849) % 65521; value = value * 0.99991 "
		        "+ table[state % 7] * 0.00009; } return "
		        "to_int((value + 4.0) * 1000000.0) + state; } "
		        "void main() { print(work(200000)); }\n",
		        "4199876\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_indirect_array",
		        "int work(int limit) { array<int> values = array<int>(); "
		        "array<int> indices = array<int>(); for (int i = 0; i < 257; i++) { "
		        "values.push((i * 17 + 11) % 101); indices.push((i * 37 + 19) % 257); } "
		        "int state = 17; int total = 0; for (int i = 0; i < limit; i++) { "
		        "state = (state * 25173 + 13849) % 65521; int position = "
		        "(state + ((i % 257) * 7)) % 257; int index = indices[position]; "
		        "int delta = ((state / 7) % 13) + 1; values[index] = "
		        "(values[index] + delta) % 1000003; total = "
		        "(total + values[index] + (position & 3)) % 1000000007; } "
		        "return (total + state + values[indices[state % 257]]) % 1000000007; } "
		        "void main() { print(work(100000)); }\n",
		        "142060410\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_short_object",
		        // Match the argument-taking case's compile-cost amortization.
		        "class Pair { public: int first = 0; int second = 0; "
		        "int sum() { return first + second; } }; "
		        "int work(int limit) { int state = 17; int total = 0; "
		        "for (int i = 0; i < limit; i++) { state = "
		        "(state * 25173 + 13849) % 65521; Pair value = Pair(); "
		        "value.first = state % 127; value.second = (state / 7) % 127; "
		        "total = (total + value.sum()) % 1000000007; } "
		        "return total + state; } void main() { print(work(200000)); }\n",
		        "25183738\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_short_object_argument",
		        // Samples include compilation. Amortize its fixed cost before
		        // checking the native loop's fivefold end-to-end speedup.
		        "class Pair { public: int first = 0; int second = 0; "
		        "int sum(int salt) { return first + second + (salt & 1); } }; "
		        "int work(int limit) { int state = 17; int total = 0; "
		        "for (int i = 0; i < limit; i++) { state = "
		        "(state * 25173 + 13849) % 65521; Pair value = Pair(); "
		        "value.first = state % 127; value.second = (state / 7) % 127; "
		        "total = (total + value.sum(state)) % 1000000007; } "
		        "return total + state; } void main() { print(work(200000)); }\n",
		        "25284197\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_xorshift_short_object",
		        "uint32 next_state(uint32 value) { value = value ^ (value << 13u); "
		        "value = value ^ (value >> 17u); return value ^ (value << 5u); } "
		        "class Pair { public: int first = 0; int second = 0; "
		        "int sum() { return first + second; } }; "
		        "int work(int limit) { uint32 state = 1357911u; int total = 0; "
		        "for (int i = 0; i < limit; i++) { state = next_state(state); "
		        "Pair value = Pair(); value.first = to_int(state & 255u); "
		        "value.second = to_int((state >> 8u) & 255u); "
		        "total = (total + value.sum()) % 1000000007; } return total; } "
		        "void main() { print(work(500000)); }\n",
		        "127519895\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_array_virtual_receiver",
		        "interface IOperation { virtual int step(int value, int salt) = 0; }; "
		        "class Add : IOperation { public: int step(int value, int salt) { "
		        "return (value + 1 + (salt % 3)) % 1000000007; } }; "
		        "class Xor : IOperation { public: int step(int value, int salt) { "
		        "return ((value ^ (salt & 7)) + 7) % 1000000007; } }; "
		        "int advance(int state) { return (state * 75 + 74) % 65521; } "
		        "int work(int limit) { array<IOperation> operations = {Add(), Xor()}; "
		        "array<int> order = {1,0,1,0}; int state = 17; int total = 0; "
		        "for (int i=0; i<limit; i++) { state=advance(state); "
		        "int index=order[(state+total)&3]; IOperation operation=operations[index]; "
		        "total=operation.step(total,state); } return total+state; } "
		        "void main(){ print(work(200000)); }\n",
		        "881023\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_map_f64_conditional_update",
		        "int work(int limit, int seed) { int size=1021; "
		        "map<int,double> values=map<int,double>(); values.reserve(size); "
		        "for(int i=0;i<size;i++) values[i]=(i%31)*0.25+1.0; "
		        "int state=seed; double total=0.0; for(int i=0;i<limit;i++) { "
		        "state=(state*75+74)%65521; int key=(state+i*13)%size; "
		        "double value=values[key]*1.000001+(state%7)*0.25; "
		        "if(value>10000.0) value*=0.5; values[key]=value; total+=value; } "
		        "return to_int(total)+state; } "
		        "void main(){ print(work(100000,173)); }\n",
		        "4280489\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_dual_string_array_compare",
		        "int work(int limit,int seed){ array<string> left={\"alpha-000\","
		        "\"beta-111\",\"gamma-222\",\"delta-333\",\"epsilon-444\","
		        "\"zeta-555\",\"eta-666\"}; array<string> right={\"alpha-000\","
		        "\"beta-X11\",\"gamma-222\",\"delta-X33\",\"epsilon-444\","
		        "\"zeta-X55\",\"eta-666\"}; int state=seed; int total=0; "
		        "for(int i=0;i<limit;i++){ state=(state*75+74)%65521; "
		        "int a=state%7; int b=(state+(i&3))%7; "
		        "if(left[a]==right[b]) total++; } return total+state; } "
		        "void main(){print(work(300000,173));}\n",
		        "98646\n",
		        0.0,
		        1.0,
		    },
		    {
		        "noncanonical_mixed_typed_containers",
		        "int work(int limit) { array<int> values = {1,2,3,4}; "
		        "array<int> indices = {3,1,0,2}; map<int,int> counts = map<int,int>(); "
		        "set<int> selected = {0,2}; array<string> left = {\"aa\",\"bb\",\"cc\",\"dd\"}; "
		        "array<string> right = {\"aa\",\"bx\",\"cc\",\"dx\"}; "
		        "map<string,int> names = map<string,int>(); set<string> known = {\"aa\",\"cc\"}; "
		        "names[\"aa\"]=5; names[\"bb\"]=7; names[\"cc\"]=11; names[\"dd\"]=13; "
		        "int total=0; for (int i=0; i<limit; i++) { int slot=indices[(i*3+1)&3]; "
		        "values[slot]=values[slot]+(i%5); counts[slot]=values[slot]; "
		        "if(selected.has(slot)) total+=counts[slot]; int textSlot=(i*5+2)&3; "
		        "if(left[textSlot]==right[textSlot]) total+=names[left[textSlot]]; "
		        "if(known.has(left[textSlot])) total++; total%=1000000007; } "
		        "return total+values[indices[limit&3]]; } "
		        "void main(){ print(work(100000)); }\n",
		        "250624997\n",
		        0.0,
		        1.0,
		    },
		    {
		        "general_unary_free_call",
		        "int add_one(int value) { return value + 1; } "
		        "int work() { int total = 0; for (int i = 0; i < 10000000; i++) { "
		        "total = add_one(total); } return total; } "
		        "void main() { print(work()); }\n",
		        "10000000\n",
		        0.0,
		        1.0,
		    },
		    {
		        "general_double_recurrence",
		        "double work() { double total = 0.0; for (int i = 0; i < 10000000; i++) { "
		        "total += 1.0; } return total; } "
		        "void main() { print(to_int(work())); }\n",
		        "10000000\n",
		        0.0,
		        1.0,
		    },
		};

		std::cout << "backend_compare (cold compile+execute; profitability gates use warm execution):\n";
		std::cout << "note: int/uint/bool print/eprint stay on homogeneous I32 CFG (fast main)\n";
		std::cout << "note: f64/string print still use Mixed CFG\n";
		std::cout << "note: leaf_int_* / leaf_method(run with locals) -> NativeCfg (~leaf speed)\n";
		std::cout << "note: bounded current-this scalar chains may expand into caller NativeCfg\n";
		std::cout << "note: scalar-signature work() with NewObject uses ownership-aware LinearTrampoline\n";
		std::cout << "note: host subclass methods with locals stay NativeCfg; CallNative -> LinearTrampoline\n";
		std::cout << "note: bounded pure scalar free calls may expand into caller NativeCfg\n";
		std::cout << "note: dynamic object calls use a VM-local polymorphic dispatch cache\n";
		std::cout << "note: dynamic object fields use a VM-local polymorphic field cache\n";
		std::cout << "note: typed scalar arrays use a single combined Linear get/set probe\n";
		std::cout << "note: non-escaping map<integral, scalar> uses flat raw-value Linear storage\n";
		std::cout << "note: non-escaping string-key maps use native hash storage with typed Linear probes\n";
		std::cout << "note: non-escaping set<integral> uses flat raw-value Linear storage\n";
		std::cout << "note: non-escaping string sets use native hash storage with typed lookup loops\n";
		std::cout << "note: avoid per-iteration field load/store in hot loops (use locals)\n";
		bool bMatchedCase = CaseFilter.empty();
		for (const FBenchCase& Case : Cases)
		{
			if (!CaseFilter.empty() && CaseFilter != Case.Name)
			{
				continue;
			}
			bMatchedCase = true;
			const std::string_view CaseName = Case.Name;
			const bool bProfitabilityCase =
			    CaseName == "noncanonical_double_indexed_recurrence" || CaseName == "noncanonical_indirect_array" || CaseName == "noncanonical_short_object" || CaseName == "noncanonical_short_object_argument" || CaseName == "noncanonical_xorshift_short_object" || CaseName == "noncanonical_array_virtual_receiver" || CaseName == "noncanonical_map_f64_conditional_update" || CaseName == "noncanonical_dual_string_array_compare" || CaseName == "noncanonical_mixed_typed_containers";
			#if defined(NDEBUG)
			constexpr std::size_t ProfitabilitySamples = 3u;
			#else
			constexpr std::size_t ProfitabilitySamples = 1u;
			#endif
			const std::size_t SampleCount = bProfitabilityCase ? ProfitabilitySamples : 1u;
			std::array<double, 3> BackendTimes{};
			std::array<bool, 3> BackendUsesVmFallback{};
			std::size_t BackendIndex = 0;
			for (const VeroScript::EExecutionBackend Backend :
			    {VeroScript::EExecutionBackend::Vm,
			        VeroScript::EExecutionBackend::Mixed,
			        VeroScript::EExecutionBackend::Jit})
			{
				std::string Output;
				std::string Error;
				VeroScript::FJitStats RuntimeStats;
				// Only timed profitability gates need a median: a short VM-equivalent
				// sample must not fail solely because of one scheduling interruption.
				if (bProfitabilityCase)
				{
					const double ColdMs = MeasureMs(Case, Output, Error, Backend);
					if (ColdMs < 0.0 || Output != Case.ExpectedOutput)
					{
						std::cerr << Case.Name << "/cold failed: " << Error << '\n';
						return EXIT_FAILURE;
					}
					std::cout << "  " << Case.Name << " " << VeroScript::ExecutionBackendName(Backend)
					          << " compile_execute: " << ColdMs << " ms\n";
				}
				std::array<double, ProfitabilitySamples> Samples{};
				for (std::size_t Sample = 0; Sample < SampleCount; ++Sample)
				{
					Samples[Sample] = bProfitabilityCase
					    ? MeasureExecutionMs(Case, Output, Error, Backend, &RuntimeStats)
					    : MeasureMs(Case, Output, Error, Backend, &RuntimeStats);
					if (Samples[Sample] < 0.0 || Output != Case.ExpectedOutput)
					{
						std::cerr << Case.Name << "/" << VeroScript::ExecutionBackendName(Backend)
						          << " failed: " << Error << " out=[" << Output << "]\n";
						return EXIT_FAILURE;
					}
				}
				std::sort(Samples.begin(), Samples.begin() + SampleCount);
				const double Ms = Samples[SampleCount / 2u];
				BackendTimes[BackendIndex] = Ms;
				BackendUsesVmFallback[BackendIndex++] = RuntimeStats.CostModelVmFallbacks != 0;
				std::cout << "  " << Case.Name << " "
				          << VeroScript::ExecutionBackendName(Backend) << ": "
				          << static_cast<int>(Ms + 0.5) << " ms\n";
				if (RuntimeStats.ScalarMethodFastPathI32 != 0 || RuntimeStats.ScalarMethodFastPathU32 != 0 || RuntimeStats.ScalarMethodFastPathF64 != 0 || RuntimeStats.ScalarMethodFastPathMixed != 0)
				{
					std::cout << "    scalar_method_fast i32="
					          << RuntimeStats.ScalarMethodFastPathI32
					          << " u32=" << RuntimeStats.ScalarMethodFastPathU32
					          << " f64=" << RuntimeStats.ScalarMethodFastPathF64
					          << " mixed=" << RuntimeStats.ScalarMethodFastPathMixed << '\n';
				}
				if (RuntimeStats.InlinedCallSites != 0)
				{
					std::cout << "    jit_inlined_calls="
					          << RuntimeStats.InlinedCallSites << '\n';
				}
				if (RuntimeStats.DirectNativeCalls != 0)
				{
					std::cout << "    jit_direct_native_calls="
					          << RuntimeStats.DirectNativeCalls << '\n';
				}
				if (RuntimeStats.ScalarMapFastPathGets != 0 || RuntimeStats.ScalarMapFastPathSets != 0)
				{
					std::cout << "    scalar_map_fast get="
					          << RuntimeStats.ScalarMapFastPathGets
					          << " set=" << RuntimeStats.ScalarMapFastPathSets << '\n';
				}
				if (RuntimeStats.ArrayFastPathPushes != 0 || RuntimeStats.ArrayFastPathGets != 0 || RuntimeStats.ArrayFastPathSets != 0)
				{
					std::cout << "    array_fast push="
					          << RuntimeStats.ArrayFastPathPushes
					          << " get=" << RuntimeStats.ArrayFastPathGets
					          << " set=" << RuntimeStats.ArrayFastPathSets << '\n';
				}
				if (Backend != VeroScript::EExecutionBackend::Vm)
				{
					#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
					const std::string_view Name = Case.Name;
					const bool bExpectedFastPathMissing =
					    (Name == "direct_native_free_call" && RuntimeStats.DirectNativeCalls < 99900) || (Name == "direct_native_f64_call" && RuntimeStats.DirectNativeCalls < 9990) || (Name == "leaf_this_call" && RuntimeStats.ScalarMethodFastPathI32 == 0 && RuntimeStats.InlinedCallSites == 0) || (Name == "leaf_this_call_uint" && RuntimeStats.ScalarMethodFastPathU32 == 0 && RuntimeStats.InlinedCallSites == 0) || (Name == "leaf_this_call_double" && RuntimeStats.ScalarMethodFastPathF64 == 0 && RuntimeStats.InlinedCallSites == 0) || ((Name == "leaf_this_call_int64" || Name == "leaf_this_call_uint64" || Name == "leaf_this_call_float") && RuntimeStats.ScalarMethodFastPathMixed == 0 && RuntimeStats.InlinedCallSites == 0) || (Name == "scalar_array_storage" && (RuntimeStats.ArrayFastPathPushes == 0 || RuntimeStats.ArrayFastPathGets == 0)) || (Name == "scalar_array_expression_storage" && (RuntimeStats.ArrayFastPathPushes == 0 || RuntimeStats.ArrayFastPathGets == 0)) || (Name == "scalar_array_read_hot" && RuntimeStats.ArrayFastPathGets == 0) || (Name == "scalar_array_write_hot" && RuntimeStats.ArrayFastPathSets == 0) || ((Name == "scalar_map_read_hot" || Name == "string_map_read_hot") && RuntimeStats.ScalarMapFastPathGets == 0) || (Name == "scalar_map_expression_read_hot" && (RuntimeStats.ScalarMapFastPathGets == 0 || RuntimeStats.ScalarMapFastPathSets == 0)) || ((Name == "typed_map_random_read_write" || Name == "typed_map_double_random_update" || Name == "typed_map_uint_random_update") && (RuntimeStats.ScalarMapFastPathGets == 0 || RuntimeStats.ScalarMapFastPathSets == 0)) || (Name == "bound_method_field_update" && RuntimeStats.ScalarMethodFastPathI32 == 0) || (Name == "ordinary_instance_method" && RuntimeStats.ScalarMethodFastPathI32 < 999000) || (Name == "nested_local_this_method" && RuntimeStats.ScalarMethodFastPathI32 < 1999000) || (Name == "ordinary_instance_method_uint" && RuntimeStats.ScalarMethodFastPathU32 < 999000) || (Name == "ordinary_instance_method_double" && RuntimeStats.ScalarMethodFastPathF64 < 999000) || ((Name == "ordinary_instance_method_int64" || Name == "ordinary_instance_method_uint64" || Name == "ordinary_instance_method_float") && RuntimeStats.ScalarMethodFastPathMixed < 999000) || (Name == "short_lived_object" && RuntimeStats.ScalarMethodFastPathI32 < 99900) || (Name == "short_lived_object_double" && RuntimeStats.ScalarMethodFastPathF64 < 99900) || ((Name == "noncanonical_short_object" || Name == "noncanonical_short_object_argument") && RuntimeStats.ScalarMethodFastPathI32 < 199900) || (Name == "dynamic_transform_pair_loop" && RuntimeStats.ScalarMethodFastPathI32 < 400000) || (Name == "dynamic_transform_replaced_receiver" && RuntimeStats.ScalarMethodFastPathI32 < 9990) || (Name == "noncanonical_array_virtual_receiver" && (RuntimeStats.ScalarMethodFastPathI32 < 199900 || RuntimeStats.ArrayFastPathGets < 399800)) || (Name == "noncanonical_map_f64_conditional_update" && (RuntimeStats.ScalarMapFastPathGets < 99900 || RuntimeStats.ScalarMapFastPathSets < 99900)) || (Name == "noncanonical_dual_string_array_compare" && RuntimeStats.ArrayFastPathGets < 599800)
					#if VEROSCRIPT_ASMJIT_X64
					    || (Name == "noncanonical_xorshift_short_object" && (RuntimeStats.ScalarMethodFastPathI32 < 499900 || RuntimeStats.NativeTypedLoopExecutions == 0))
					#endif
					    ;
					if (bExpectedFastPathMissing)
					{
						std::cerr << Case.Name << "/"
						          << VeroScript::ExecutionBackendName(Backend)
						          << ": expected scalar method fast path was not reached\n";
						(void)DumpJitEntries(Case.Source, std::cerr);
						return EXIT_FAILURE;
					}
					#endif
				}
			}
			#if defined(NDEBUG)
			// Relative, same-process profitability guards are deliberately limited to
			// representative non-canonical shapes.  They are stable across machines and
			// catch the important failure mode where a newly recognized Linear plan is
			// substantially slower than the VM.  The generous ratio is a regression gate,
			// not a claim about absolute throughput.
			const bool bRequiresNativePureMethodKernel =
			    CaseName == "noncanonical_array_virtual_receiver" || CaseName == "noncanonical_xorshift_short_object";
			#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
			const bool bCanEnforceNativePureMethodProfitability = true;
			#else
			const bool bCanEnforceNativePureMethodProfitability = false;
			#endif
			if ((!bRequiresNativePureMethodKernel || bCanEnforceNativePureMethodProfitability) && bProfitabilityCase)
			{
// Native kernels retain the original strong speedup gate, even if an
// unrelated wrapper selected VM fallback during the same run.
				#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
				constexpr double MaxMixedToVmRatio = 0.20;
				constexpr double MaxJitToVmRatio = 0.20;
				#else
				// Portable typed execution lacks the specialized native kernels. Require
				// improvement when accelerated, or comparable throughput for deliberate VM fallback.
				constexpr double MaxAcceleratedToVmRatio = 0.75;
				const double MaxMixedToVmRatio = BackendUsesVmFallback[1]
				    ? 1.5
				    : MaxAcceleratedToVmRatio;
				const double MaxJitToVmRatio = BackendUsesVmFallback[2]
				    ? 1.5
				    : MaxAcceleratedToVmRatio;
				#endif
				if (BackendTimes[1] > BackendTimes[0] * MaxMixedToVmRatio || BackendTimes[2] > BackendTimes[0] * MaxJitToVmRatio)
				{
					std::cerr << Case.Name << ": optimized backend profitability regression: "
					          << "vm=" << BackendTimes[0] << " mixed=" << BackendTimes[1]
					          << " jit=" << BackendTimes[2] << " ms\n";
					return EXIT_FAILURE;
				}
			}
			#endif
			if (!DumpJitEntries(Case.Source, std::cout))
			{
				std::cerr << Case.Name << ": failed to dump JIT entries\n";
				return EXIT_FAILURE;
			}
		}
		if (!bMatchedCase)
		{
			std::cerr << "unknown backend comparison case: " << CaseFilter << '\n';
			return EXIT_FAILURE;
		}
		std::cout << "VeroScriptPerfTests: backend_compare ok\n";
		return EXIT_SUCCESS;
	}
}

int main(int Argc, char** Argv)
{
	const bool bUpdateBaseline =
	    Argc > 1 && std::string_view(Argv[1]) == "--update-baseline";
	const bool bCompareBackends =
	    Argc > 1 && std::string_view(Argv[1]) == "--compare-backends";
	const bool bDumpSource =
	    Argc > 1 && std::string_view(Argv[1]) == "--dump-source";
	const bool bProfileSource =
	    Argc > 1 && std::string_view(Argv[1]) == "--profile-source";

	if (bDumpSource || bProfileSource)
	{
		if (Argc < 3 || Argc > 4)
		{
			std::cerr << "usage: VeroScriptPerfTests --dump-source <source.vs>\n"
			          << "   or: VeroScriptPerfTests --profile-source <source.vs> "
			             "[mixed|jit|vm]\n";
			return EXIT_FAILURE;
		}
		std::ifstream Input(Argv[2], std::ios::binary);
		if (!Input)
		{
			std::cerr << "failed to open source: " << Argv[2] << '\n';
			return EXIT_FAILURE;
		}
		const std::string Source(
		    (std::istreambuf_iterator<char>(Input)),
		    std::istreambuf_iterator<char>());
		if (!DumpJitEntries(Source, std::cout))
		{
			std::cerr << "failed to compile or prepare source: " << Argv[2] << '\n';
			return EXIT_FAILURE;
		}
		if (!bProfileSource)
		{
			return EXIT_SUCCESS;
		}
		VeroScript::EExecutionBackend Backend = VeroScript::EExecutionBackend::Mixed;
		if (Argc == 4)
		{
			const std::string_view Name(Argv[3]);
			if (Name == "jit")
			{
				Backend = VeroScript::EExecutionBackend::Jit;
			}
			else if (Name == "vm")
			{
				Backend = VeroScript::EExecutionBackend::Vm;
			}
			else if (Name != "mixed")
			{
				std::cerr << "unknown backend: " << Name << '\n';
				return EXIT_FAILURE;
			}
		}
		std::string Output;
		std::string Error;
		VeroScript::FJitStats Stats;
		if (!RunScript(Source, Output, &Error, Backend, &Stats))
		{
			std::cerr << "source execution failed: " << Error << '\n';
			return EXIT_FAILURE;
		}
		std::cout << Output
		          << "    runtime dispatchAttempts=" << Stats.DispatchAttempts
		          << " dispatches=" << Stats.Dispatches
		          << " failures=" << Stats.Failures
		          << " directNativeCalls=" << Stats.DirectNativeCalls
		          << " typedNativeExecutions=" << Stats.NativeTypedLoopExecutions
		          << " vmCostFallbacks=" << Stats.CostModelVmFallbacks << '\n';
		return EXIT_SUCCESS;
	}

	if (bCompareBackends)
	{
		return RunBackendCompare(Argc > 2 ? std::string_view(Argv[2]) : std::string_view{});
	}

	// Gate timings pin Vm so they stay comparable to pre-optimization baselines
	// (default engine backend is Mixed).
	constexpr VeroScript::EExecutionBackend GateBackend = VeroScript::EExecutionBackend::Vm;

	// Pre-optimization timings from plan (Release MSVC, conversation measurements).
	const FBenchCase Cases[] = {
	    {
	        "int_loop",
	        "void main() {\n"
	        "\tint s = 0;\n"
	        "\tint i = 0;\n"
	        "\twhile (i < 5000000) {\n"
	        "\t\ts = s + 1;\n"
	        "\t\ti = i + 1;\n"
	        "\t}\n"
	        "\tprint(s);\n"
	        "}\n",
	        "5000000\n",
	        1400.0,
	        5.0,
	    },
	    {
	        "int_call",
	        "int add(int a, int b) { return a + b; }\n"
	        "void main() {\n"
	        "\tint s = 0;\n"
	        "\tint i = 0;\n"
	        "\twhile (i < 2000000) {\n"
	        "\t\ts = add(s, 1);\n"
	        "\t\ti = i + 1;\n"
	        "\t}\n"
	        "\tprint(s);\n"
	        "}\n",
	        "2000000\n",
	        890.0,
	        5.0,
	    },
	    {
	        "method_call",
	        // Prefer hot loops inside methods (locals in the loop) for Mixed/Jit NativeCfg.
	        "class Counter {\n"
	        "public:\n"
	        "\tint n = 0;\n"
	        "\tint run(int limit) {\n"
	        "\t\tint s = n;\n"
	        "\t\tint i = 0;\n"
	        "\t\twhile (i < limit) {\n"
	        "\t\t\ts = s + 1;\n"
	        "\t\t\ti = i + 1;\n"
	        "\t\t}\n"
	        "\t\tn = s;\n"
	        "\t\treturn s;\n"
	        "\t}\n"
	        "}\n"
	        "void main() {\n"
	        "\tCounter c = Counter();\n"
	        "\tprint(c.run(2000000));\n"
	        "}\n",
	        "2000000\n",
	        812.0,
	        5.0,
	    },
	    {
	        "field_array",
	        // 1M push + 1M get; avoid int32 overflow when reading (do not sum 0..N-1).
	        "void main() {\n"
	        "\tarray<int> a = array<int>();\n"
	        "\tint i = 0;\n"
	        "\twhile (i < 1000000) {\n"
	        "\t\ta.push(i);\n"
	        "\t\ti = i + 1;\n"
	        "\t}\n"
	        "\tint sink = 0;\n"
	        "\ti = 0;\n"
	        "\twhile (i < 1000000) {\n"
	        "\t\tsink = a[i];\n"
	        "\t\ti = i + 1;\n"
	        "\t}\n"
	        "\tprint(sink);\n"
	        "}\n",
	        "999999\n",
	        2500.0,
	        3.0,
	    },
	};

	std::vector<FBaselineMeasurement> Measured;
	Measured.reserve(std::size(Cases) + 1);

	{
		const char* Hello = "void main() { print(1); }\n";
		std::string Output;
		std::string Error;
		const auto Begin = Clock::now();
		if (!Expect(RunScript(Hello, Output, &Error, GateBackend), "cold_start run"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		const double Ms = std::chrono::duration<double, std::milli>(Clock::now() - Begin).count();
		std::cout << "cold_start: " << static_cast<int>(Ms + 0.5) << " ms (backend=vm)\n";
		Measured.push_back({"cold_start", "vm", {Ms}});
		if (!Expect(Output == "1\n", "cold_start output"))
		{
			return EXIT_FAILURE;
		}
	}

	for (const FBenchCase& Case : Cases)
	{
		constexpr double TimingTolerance = 1.2;
		std::string Output;
		std::string Error;
		// Wall-clock gates must not hinge on one scheduler/thermal outlier. Keep
		// compile+execute in every sample and use the median of three, so genuine
		// regressions remain visible while a single interrupted run cannot fail CI.
		std::array<double, 3> Samples{};
		for (double& Sample : Samples)
		{
			Output.clear();
			Error.clear();
			Sample = MeasureMs(Case, Output, Error, GateBackend);
			if (Sample < 0.0)
			{
				std::cerr << Case.Name << " failed: " << Error << '\n';
				return EXIT_FAILURE;
			}
			if (!Expect(Output == Case.ExpectedOutput, Case.Name))
			{
				std::cerr << "expected [" << Case.ExpectedOutput << "] got [" << Output << "]\n";
				return EXIT_FAILURE;
			}
		}
		std::sort(Samples.begin(), Samples.end());
		const double Ms = Samples[1];
		const double Speedup = Case.PreOptimizationMs / Ms;
		const double MaxAllowedMs = Case.PreOptimizationMs / Case.MinSpeedup;
		const double MinimumToleratedSpeedup = Case.MinSpeedup / TimingTolerance;
		std::cout << Case.Name << ": " << static_cast<int>(Ms + 0.5) << " ms"
		          << " (vm pre=" << static_cast<int>(Case.PreOptimizationMs)
		          << " speedup=" << Speedup << "x target=" << Case.MinSpeedup
		          << "x gate>=" << MinimumToleratedSpeedup << "x)\n";
		Measured.push_back({Case.Name, "vm",
		    {Samples[0], Samples[1], Samples[2]}});
// Speed gates use Release baselines; Debug/RelWithDebInfo builds still verify
// output correctness above but skip wall-clock gates (interpreter is far slower).
		#if defined(NDEBUG)
		if (!bUpdateBaseline && Ms > MaxAllowedMs * TimingTolerance)
		{
			std::cerr << Case.Name << " too slow: " << Ms << " ms > "
			          << (MaxAllowedMs * TimingTolerance) << " ms gate\n";
			return EXIT_FAILURE;
		}
		#else
		(void)MaxAllowedMs;
		#endif
	}

	if (bUpdateBaseline)
	{
		const std::string BaselinePath =
		    Argc > 2 ? Argv[2] : "VeroScript/Resources/Benchmarks/baseline.json";
		if (!Expect(WriteBaseline(BaselinePath, Measured), "write baseline"))
		{
			return EXIT_FAILURE;
		}
		std::cout << "wrote " << BaselinePath << '\n';
	}

	std::cout << "VeroScriptPerfTests: ok\n";
	return EXIT_SUCCESS;
}
