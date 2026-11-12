// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptCompile.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptAsmJit.h"
#include "../../../../../VeroScript/Private/Tests/VeroScriptLambdaStaticFixture.h"

#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptJitIr.h"
#include "VeroScriptJitLinear.h"
#include "VeroScriptJitKindStates.h"
#include "VeroScriptJitText.h"
#include "VeroScriptJitTextPatterns.h"
#include "VeroScriptJitLowering.h"
#include "VeroScriptAsmJitCapabilities.h"
#include "VeroScriptJitAbi.h"
#include "VeroScriptJitNative.h"
#include "VeroScriptJitRawScratch.h"
#include "VeroScriptLexer.h"
#include "VeroScriptNative.h"
#include "VeroScriptParser.h"
#include "VeroScriptRun.h"
#include "VeroScriptVm.h"

#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
#include <asmjit/x86.h>
#endif

#include "VeroScriptAst.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "VeroScriptJitBackendTests FAIL: " << Message << '\n';
		}
		return Condition;
	}

	void ConfigureBackend(VeroScript::VeroScriptVm& Vm)
	{
		std::string Error;
		if (!Vm.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), Error))
		{
			std::cerr << Error << '\n';
			std::abort();
		}
	}

	struct FScalarNativeProbe
	{
		std::size_t Calls = 0;
		bool bInvalidContext = false;
		bool bFail = false;
	};

	bool ScalarNativeAdd(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* Probe = static_cast<FScalarNativeProbe*>(Context.UserData);
		if (!Probe
			|| Context.ArgCount != 2
			|| Context.HostThis != nullptr
			|| Context.MutableArgs != nullptr
			|| Context.MutableArg(0) != nullptr
			|| Context.MutableArg(1) != nullptr
			|| !std::holds_alternative<std::int32_t>(Context.Args[0])
			|| !std::holds_alternative<std::int32_t>(Context.Args[1]))
		{
			if (Probe)
			{
				Probe->bInvalidContext = true;
			}
			OutError = "invalid scalar native context";
			return false;
		}
		++Probe->Calls;
		if (Probe->bFail)
		{
			OutError = "scalar native probe failure";
			return false;
		}
		OutReturn = std::get<std::int32_t>(Context.Args[0])
			+ std::get<std::int32_t>(Context.Args[1]);
		return true;
	}

	bool ScalarNativeAddI64(
		const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue& OutReturn,
		std::string& OutError)
	{
		auto* Probe = static_cast<FScalarNativeProbe*>(Context.UserData);
		if (!Probe
			|| Context.ArgCount != 2
			|| Context.HostThis != nullptr
			|| Context.MutableArgs != nullptr
			|| !std::holds_alternative<std::int64_t>(Context.Args[0])
			|| !std::holds_alternative<std::int64_t>(Context.Args[1]))
		{
			if (Probe)
			{
				Probe->bInvalidContext = true;
			}
			OutError = "invalid int64 scalar native context";
			return false;
		}
		++Probe->Calls;
		if (Probe->bFail)
		{
			OutError = "scalar native probe failure";
			return false;
		}
		OutReturn = std::get<std::int64_t>(Context.Args[0])
			+ std::get<std::int64_t>(Context.Args[1]);
		return true;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(const std::string& Source)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "jit.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			std::cerr << "lex: " << Lexer.GetErrorMessage() << '\n';
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "jit.vs");
		// JIT backend coverage deliberately lowers internal builtin opcodes.
		Parser.SetAllowInternalBuiltins(true);
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			std::cerr << "parse: " << Parser.GetError().Location.Line << ':'
				<< Parser.GetError().Location.Column << ": "
				<< Parser.GetError().Message << '\n';
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

	std::uint16_t FindFunctionIndex(
		const VeroScript::FBytecodeModule& Module,
		const char* LeafName,
		const char* OwnerTypeName = nullptr)
	{
		const std::string Prefix = std::string(LeafName) + "#";
		const std::string MethodToken = std::string("::") + LeafName + "#";
		for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
		{
			const VeroScript::FBytecodeFunction& Candidate = Module.Functions[Index];
			if (OwnerTypeName)
			{
				if (!Candidate.bIsMethod || Candidate.OwnerTypeIndex >= Module.Types.size())
				{
					continue;
				}

				const auto& Owner = Module.Types[Candidate.OwnerTypeIndex].Name;
				const auto Separator = Owner.find('\x1F');
				if ((Separator == std::string::npos ? Owner : Owner.substr(Separator + 1)) != OwnerTypeName)
				{
					continue;
				}
			}
			if (Candidate.Code.empty()
				&& Candidate.bIsMethod
				&& Candidate.OwnerTypeIndex == static_cast<std::uint16_t>(0xFFFF))
			{
				// Abstract interface-method stubs never receive a body; skip them so
				// leaf-name lookups resolve the implementing class method instead.
				continue;
			}
			const std::string& Name = Module.Functions[Index].Name;
			// Module path is only the first \x1F segment; later separators appear in
			// qualified type suffixes (e.g. take#fn#8:jit.vs\x1FI).
			const std::size_t Sep = Name.find('\x1F');
			const std::string Leaf = Sep == std::string::npos ? Name : Name.substr(Sep + 1);
			if (Leaf == LeafName || Leaf.rfind(Prefix, 0) == 0 || Leaf.find(MethodToken) != std::string::npos)
			{
				return static_cast<std::uint16_t>(Index);
			}
		}
		return static_cast<std::uint16_t>(-1);
	}

	bool ExpectStatus(
		VeroScript::AsmJit::FJitCodeCache& Cache,
		const VeroScript::FBytecodeModule& Module,
		const char* LeafName,
		const VeroScript::AsmJit::EJitCompileStatus Expected,
		const char* Message)
	{
		const std::uint16_t Index = FindFunctionIndex(Module, LeafName);
		if (!Expect(Index != static_cast<std::uint16_t>(-1), Message))
		{
			return false;
		}
		return Expect(Cache.EnsureCompiled(Module, Index) == Expected, Message);
	}

	bool ExpectRun(
		const VeroScript::FBytecodeModule& Module,
		const VeroScript::EExecutionBackend Backend,
		const std::string_view ExpectedOutput,
		const char* Message)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
		if (!Expect(Vm.Execute(Module, VeroScript::ERunMode::Script), Message))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
				<< " error='" << Vm.GetError().Message << "' output='"
				<< Output.str() << "' stderr='" << Errors.str() << "'\n";
			return false;
		}
		if (!Expect(Output.str() == ExpectedOutput, Message))
		{
			std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
				<< " got: " << Output.str();
			return false;
		}
		return true;
	}

	struct FExecutionCapture
	{
		bool bSuccess = false;
		bool bExitRequested = false;
		int ExitCode = 0;
		std::string Output;
		std::string Errors;
		std::string ErrorMessage;
		std::string ErrorMessageId;
		std::vector<std::string> ErrorMessageArgs;
		std::string ErrorFile;
		std::uint32_t ErrorLine = 0;
		std::uint32_t ErrorColumn = 0;
		VeroScript::FJitStats JitStats;
	};

	FExecutionCapture RunCapture(
		const VeroScript::FBytecodeModule& Module,
		const VeroScript::EExecutionBackend Backend,
		const std::string_view InputText = {},
		const VeroScript::ERunMode RunMode = VeroScript::ERunMode::Script)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream Input{std::string(InputText)};
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		ConfigureBackend(Vm);
		Vm.SetExecutionBackend(Backend);
		FExecutionCapture Capture;
		Capture.bSuccess = Vm.Execute(Module, RunMode);
		Capture.bExitRequested = Vm.WasExitRequested();
		Capture.ExitCode = Vm.GetRequestedExitCode();
		Capture.Output = Output.str();
		Capture.Errors = Errors.str();
		Capture.JitStats = Vm.GetJitStats();
		if (Vm.HasError())
		{
			Capture.ErrorMessage = Vm.GetError().Message;
			Capture.ErrorMessageId = Vm.GetError().MessageId;
			Capture.ErrorMessageArgs = Vm.GetError().MessageArgs;
			Capture.ErrorFile = Vm.GetError().Location.FileLabel;
			Capture.ErrorLine = Vm.GetError().Location.Line;
			Capture.ErrorColumn = Vm.GetError().Location.Column;
		}
		return Capture;
	}

	bool ExpectDifferentialRun(const std::string_view Source, const char* Message)
	{
		const std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(std::string(Source));
		if (!Expect(static_cast<bool>(Module), Message))
		{
			return false;
		}
		const FExecutionCapture Baseline = RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
		// Structured runtime errors are authoritative even when a caller supplies a
		// diagnostic sink that intentionally remains quiet. Requiring rendered text
		// made differential failure tests reject a valid VM baseline before they
		// could compare stable IDs and source locations.
		if (!Expect(Baseline.bSuccess || !Baseline.Errors.empty()
				|| !Baseline.ErrorMessageId.empty()
				|| !Baseline.ErrorMessage.empty(),
				Message))
		{
			std::cerr << Baseline.Errors;
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Candidate = RunCapture(*Module, Backend);
			if (!Expect(Candidate.bSuccess == Baseline.bSuccess
					&& Candidate.bExitRequested == Baseline.bExitRequested
					&& Candidate.ExitCode == Baseline.ExitCode
					&& Candidate.Output == Baseline.Output
					&& Candidate.Errors.empty() == Baseline.Errors.empty()
					// A backend is not semantically equivalent when it reports the
						// same human-readable failure but loses its stable diagnostic
						// identity, substitutions, or source location.
						&& Candidate.ErrorMessage == Baseline.ErrorMessage
					&& Candidate.ErrorMessageId == Baseline.ErrorMessageId
					&& Candidate.ErrorMessageArgs == Baseline.ErrorMessageArgs
					&& Candidate.ErrorFile == Baseline.ErrorFile
					&& Candidate.ErrorLine == Baseline.ErrorLine
					&& Candidate.ErrorColumn == Baseline.ErrorColumn,
					Message))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Candidate.bSuccess
					<< " exit=" << Candidate.bExitRequested << ':' << Candidate.ExitCode
					<< " output=" << Candidate.Output << " errors=" << Candidate.Errors
					<< " baseline-success=" << Baseline.bSuccess
					<< " baseline-exit=" << Baseline.bExitRequested << ':' << Baseline.ExitCode
					<< " baseline-output=" << Baseline.Output
					<< " baseline-errors=" << Baseline.Errors
					<< " candidate-diagnostic=" << Candidate.ErrorMessageId
					<< '@' << Candidate.ErrorFile << ':' << Candidate.ErrorLine
					<< ':' << Candidate.ErrorColumn
					<< " baseline-diagnostic=" << Baseline.ErrorMessageId
					<< '@' << Baseline.ErrorFile << ':' << Baseline.ErrorLine
					<< ':' << Baseline.ErrorColumn;
				return false;
			}
		}
		return true;
	}

	bool TestDeadLocalStoreLowering()
	{
		const auto Module = CompileSource(
			"int dead_stores(int value) { int unused = value + 1; "
			"unused = value + 2; return value; }\n"
			"void overflow_in_dead_store(int value) { int unused = value + 1; }\n"
			"void main() { print(dead_stores(7)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile local liveness fixture"))
		{
			return false;
		}
		const std::uint16_t DeadStoreIndex = FindFunctionIndex(*Module, "dead_stores");
		VeroScript::AsmJit::JitLowering::FPlan Plan;
		std::string Error;
		if (!Expect(DeadStoreIndex != static_cast<std::uint16_t>(-1)
				&& VeroScript::AsmJit::JitLowering::BuildScalarPlan(
					*Module, Module->Functions[DeadStoreIndex], Plan, &Error),
			"build local liveness lowering plan")
			|| !Expect(Plan.ElidedLocalStoreOffsets.size() >= 2,
				"overwritten and terminal dead stores are identified"))
		{
			std::cerr << Error << '\n';
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "7\n", "dead local store semantics"))
			{
				return false;
			}
		}

		const auto OverflowModule = CompileSource(
			"void overflow_in_dead_store(int value) { int unused = value + 1; }\n"
			"void main() { overflow_in_dead_store(2147483647); }\n");
		if (!Expect(static_cast<bool>(OverflowModule),
			"compile dead-store overflow fixture"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			const FExecutionCapture Capture = RunCapture(*OverflowModule, Backend);
			if (!Expect(!Capture.bSuccess
					&& Capture.ErrorMessageId == "diag.integer_overflow",
				"dead local store preserves operand overflow"))
			{
				std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Capture.bSuccess
					<< " error-id=" << Capture.ErrorMessageId
					<< " message=" << Capture.ErrorMessage << '\n';
				return false;
			}
		}
		return true;
	}

	bool TestPartialPlanCapacityFallback()
	{
		#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
		const auto Module = CompileSource(
			"int partial_plan_capacity(int value) { return value + 1; }\n"
			"void main() { print(partial_plan_capacity(4)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile partial-plan capacity fixture"))
		{
			return false;
		}
		const std::uint16_t FunctionIndex =
			FindFunctionIndex(*Module, "partial_plan_capacity");
		VeroScript::AsmJit::JitLowering::FPlan PartialPlan;
		PartialPlan.MaxStackSlots = 1;
		PartialPlan.MaxBoxedStackSlots = 1;
		PartialPlan.bScalarEligible = false;
		VeroScript::AsmJit::JitNative::FArena Arena;
		VeroScript::AsmJit::FJitFunctionEntry Entry;
		if (!Expect(FunctionIndex != static_cast<std::uint16_t>(-1)
				&& VeroScript::AsmJit::JitNative::TryBindLinearI32Cfg(
					Arena, *Module, Module->Functions[FunctionIndex], Entry, &PartialPlan),
			"legacy CFG fallback accepts partial-plan fixture")
			|| !Expect(Entry.StackCapacityHint == 0
					&& Entry.BoxedStackCapacityHint == 0,
				"partial analysis cannot narrow native scratch capacity"))
		{
			return false;
		}
		#endif
		return true;
	}

	bool TestWideObjectFieldCache()
	{
		constexpr int TypeCount = 96;
		std::ostringstream Source;
		Source << "class WideFieldBase { public: int x = 2; int y = 3; };\n";
		for (int Index = 0; Index < TypeCount; ++Index)
		{
			Source << "class WideField" << Index << " : WideFieldBase {};\n";
		}
		Source << "int read_wide_fields(WideFieldBase value) { return value.x + value.y; }\n"
			<< "void main() { int total = 0; for (int round = 0; round < 3; round++) { ";
		for (int Index = 0; Index < TypeCount; ++Index)
		{
			Source << "total += read_wide_fields(WideField" << Index << "()); ";
		}
		Source << "} print(total); }\n";
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source.str());
		if (!Expect(static_cast<bool>(Module), "compile wide object-field suite"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "1440\n",
				"wide object-field cache semantics"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestLargeLocalNativeCapacity()
	{
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		const auto Module = CompileSource(
			"int large_local_capacity(int value) { return value + 1; }\n"
			"void main() { print(large_local_capacity(4)); }\n");
		if (!Expect(static_cast<bool>(Module), "compile large-local native fixture"))
		{
			return false;
		}
		const std::uint16_t FunctionIndex =
			FindFunctionIndex(*Module, "large_local_capacity");
		if (!Expect(FunctionIndex != static_cast<std::uint16_t>(-1),
			"find large-local native fixture"))
		{
			return false;
		}
		Module->Functions[FunctionIndex].LocalCount = 5000u;
		VeroScript::AsmJit::JitNative::FArena Arena;
		VeroScript::AsmJit::FJitFunctionEntry Entry;
		if (!Expect(VeroScript::AsmJit::JitNative::TryBindLinearI32Cfg(
				Arena, *Module, Module->Functions[FunctionIndex], Entry),
			"native CFG accepts locals above former 4096 cliff"))
		{
			return false;
		}
		#endif
		return true;
	}

	bool TestScalarForwardingNativeCfg()
	{
		// Loop-bearing callees keep these calls from being inlined, so the
		// zero-argument and parameter-forwarding bridges are actually executed.
		const auto Module = CompileSource(
			"int integer_value() { int value=37; for(int i=0;i<1;i++){value+=i;} return value; } "
			"int integer_forward() { return integer_value(); } "
			"int integer_add(int a, int b) { for(int i=0;i<1;i++){a+=b;} return a; } "
			"int integer_args(int a, int b) { return integer_add(a, b); } "
			"uint32 unsigned_value(uint32 v) { for(uint32 i=0u;i<1u;i++){v+=1u;} return v; } "
			"uint32 unsigned_args(uint32 v) { return unsigned_value(v); } "
			"double double_value() { double value=5.5; for(double i=0.0;i<1.0;i+=1.0){value+=i;} return value; } "
			"double double_forward() { return double_value(); } "
			"double double_add(double a, double b) { for(double i=0.0;i<1.0;i+=1.0){a+=b;} return a; } "
			"double double_args(double a, double b) { return double_add(a, b); } "
			"void main() { print(integer_forward()); print(integer_args(20, 22)); "
			"print(double_forward()); print(double_args(2.0, 4.5)); print(unsigned_args(2147483648u)); }");
		if (!Expect(static_cast<bool>(Module), "compile scalar forwarding suite"))
		{
			return false;
		}
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "37\n42\n5.5\n6.5\n2147483649\n",
				"scalar forwarding preserves arguments and return values"))
			{
				return false;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		for (const char* Name : {"integer_forward", "integer_args", "double_forward", "double_args", "unsigned_args"})
		{
			const auto Index = FindFunctionIndex(*Module, Name);
			if (!Expect(Index != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*Module, Index) == VeroScript::AsmJit::EJitCompileStatus::Ok
				&& Cache.GetCandidate(Index) != nullptr
				&& Cache.GetCandidate(Index)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
				"scalar forwarding retains native CFG coverage"))
			{
				return false;
			}
		}
		#endif
		return true;
	}

	bool TestClockBuiltinF64NativeCfg()
	{
		// Reading a clock must not widen an otherwise homogeneous double function
		// into the mixed or boxed executor.  This is especially important for
		// scripts that time a hot numeric loop from inside the script itself.
		const auto Module = CompileSource(
			"double timed_sum() { "
			"double wall = now(); double steady = steady_now(); "
			"double total = 0.0; double i = 0.0; "
			"while (i < 4.0) { total += 1.0; i += 1.0; } "
			"print(total + (wall - wall) + (steady - steady)); return total; } "
			"void main() { timed_sum(); }");
		if (!Expect(static_cast<bool>(Module), "compile F64 clock builtin suite"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "4\n",
				"F64 clock builtin semantics"))
			{
				return false;
			}
		}
		#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
		VeroScript::AsmJit::FJitCodeCache Cache;
		const std::uint16_t Index = FindFunctionIndex(*Module, "timed_sum");
		if (!Expect(Index != static_cast<std::uint16_t>(-1)
				&& Cache.EnsureCompiled(*Module, Index)
					== VeroScript::AsmJit::EJitCompileStatus::Ok
				&& Cache.GetCandidate(Index) != nullptr
				&& Cache.GetCandidate(Index)->Kind == VeroScript::AsmJit::EJitEntryKind::NativeCfg,
			"F64 clock builtin retains NativeCfg on every native asmjit ABI"))
		{
			return false;
		}
		#endif
		return true;
	}

	bool TestFloatingAbsSpecialValues()
	{
		// abs follows the standard floating contract: it clears the sign of
		// negative zero and preserves NaN-ness.  This must remain identical in
		// the interpreter, Mixed JIT, and homogeneous F64 NativeCfg paths.
		const auto Module = CompileSource(
			"double abs_double(double value) { return abs(value); } "
			"float abs_float(float value) { return abs(value); } "
			"double abs_nan() { return abs(__sqrt(-1.0)); } "
			"void main() { print(abs_double(-0.0)); print(abs_float(-0.0f)); "
			"print(abs_nan()); }");
		if (!Expect(static_cast<bool>(Module), "compile floating abs special-value suite"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "0\n0\nnan\n",
				"floating abs special-value semantics"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestNativeKernelDiagnosticLocations()
	{
		// Raw native kernels return compact status codes instead of a VM instruction
		// pointer. When a compiled body has a unique possible fault instruction,
		// its source location must still remain identical to the interpreter.
		const auto Verify = [](const std::string_view Source,
			const char* const ExpectedDiagnostic,
			const char* const Message)
		{
			const std::unique_ptr<VeroScript::FBytecodeModule> Module =
				CompileSource(std::string(Source));
			if (!Expect(static_cast<bool>(Module), Message))
			{
				return false;
			}
			const FExecutionCapture Baseline =
				RunCapture(*Module, VeroScript::EExecutionBackend::Vm);
			if (!Expect(!Baseline.bSuccess
					&& Baseline.ErrorMessageId == ExpectedDiagnostic
					&& Baseline.ErrorLine != 0
					&& Baseline.ErrorColumn != 0,
				Message))
			{
				return false;
			}
			for (const VeroScript::EExecutionBackend Backend :
				{VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
			{
				const FExecutionCapture Candidate = RunCapture(*Module, Backend);
				if (!Expect(!Candidate.bSuccess
						&& Candidate.ErrorMessageId == Baseline.ErrorMessageId
						&& Candidate.ErrorLine == Baseline.ErrorLine
						&& Candidate.ErrorColumn == Baseline.ErrorColumn,
					Message))
				{
					std::cerr << "backend=" << VeroScript::ExecutionBackendName(Backend)
						<< " VM=" << Baseline.ErrorLine << ':' << Baseline.ErrorColumn
						<< " JIT=" << Candidate.ErrorLine << ':' << Candidate.ErrorColumn
						<< '\n';
					return false;
				}
			}
			return true;
		};

		return Verify(
			"int native_divide(int left, int right) { return left / right; }\n"
			"void main() { print(native_divide(1, 0)); }\n",
			"diag.division_by_zero", "native division preserves source location")
			&& Verify(
				"int native_to_int(double value) { return to_int(value); }\n"
				"void main() { print(native_to_int(2147483648.0)); }\n",
				"diag.to_int_overflow", "native to_int preserves source location")
			&& Verify(
				"unsigned int native_to_unsigned(int value) { return to_unsigned(value); }\n"
				"void main() { print(native_to_unsigned(-1)); }\n",
				"diag.to_unsigned_overflow", "native to_unsigned preserves source location");
	}

	bool TestLongBuiltinAddChain()
	{
		const auto Module = CompileSource(
			"class SumFields { public: "
			"int f0 = 1; int f1 = 2; int f2 = 3; int f3 = 4; "
			"int f4 = 5; int f5 = 6; int f6 = 7; int f7 = 8; "
			"int f8 = 9; int f9 = 10; int f10 = 11; int f11 = 12; "
			"int f12 = 13; int f13 = 14; int f14 = 15; int f15 = 16; }; "
			"int sum(SumFields value) { return value.f0 + value.f1 + value.f2 + value.f3 "
			"+ value.f4 + value.f5 + value.f6 + value.f7 "
			"+ value.f8 + value.f9 + value.f10 + value.f11 "
			"+ value.f12 + value.f13 + value.f14 + value.f15; } "
			"void main() { print(sum(SumFields())); }");
		if (!Expect(static_cast<bool>(Module), "compile long built-in add chain"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "136\n",
				"long built-in add chain semantics"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestAddChainOverloadFallback()
	{
		// The iterative path must never flatten through user operator resolution.
		const auto Module = CompileSource(
			"struct Vec { int value; Vec(int initial) { value = initial; } "
			"Vec operator+(Vec other) { return Vec(value + other.value); } }; "
			"void main() { print((Vec(1) + Vec(2) + Vec(3)).value); }");
		if (!Expect(static_cast<bool>(Module), "compile overloaded add chain"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "6\n",
				"overloaded add-chain fallback semantics"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestAdditivePolymorphicTransformLoop()
	{
		// Keep the common `total += current.method(constant)` spelling covered
		// independently from the existing transform-accumulator loop.  The JIT
		// may reduce this only after proving every implementation is a direct,
		// side-effect-free transform; VM, Mixed and JIT must remain identical.
		const auto Module = CompileSource(
			"interface IAdd { virtual int add(int value) = 0; }; "
			"class AddOne : IAdd { int add(int value) { return value + 1; } }; "
			"class AddTwo : IAdd { int add(int value) { return value + 2; } }; "
			"class AddThree : IAdd { int add(int value) { return value + 3; } }; "
			"class AddFour : IAdd { int add(int value) { return value + 4; } }; "
			"int work() { IAdd a = AddOne(); IAdd b = AddTwo(); "
			"IAdd c = AddThree(); IAdd d = AddFour(); IAdd current = a; "
			"int total = 0; for (int i = 0; i < 64; i++) { int choice = i % 4; "
			"if (choice == 0) current = a; else if (choice == 1) current = b; "
			"else if (choice == 2) current = c; else current = d; "
			"total = total + current.add(1); } return total; } "
			"void main() { print(work()); }");
		if (!Expect(static_cast<bool>(Module),
			"compile additive polymorphic transform loop"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "224\n",
				"additive polymorphic transform loop semantics"))
			{
				return false;
			}
		}
		const auto ModuloModule = CompileSource(
			"interface IAddMod { virtual int add(int value) = 0; }; "
			"class AddOne : IAddMod { int add(int value) { return (value + 1) % 5; } }; "
			"class AddTwo : IAddMod { int add(int value) { return (value + 2) % 5; } }; "
			"class AddThree : IAddMod { int add(int value) { return (value + 3) % 5; } }; "
			"class AddFour : IAddMod { int add(int value) { return (value + 4) % 5; } }; "
			"int work() { IAddMod a = AddOne(); IAddMod b = AddTwo(); "
			"IAddMod c = AddThree(); IAddMod d = AddFour(); IAddMod current = a; "
			"int total = 0; for (int i = 0; i < 64; i++) { int choice = i % 4; "
			"if (choice == 0) current = a; else if (choice == 1) current = b; "
			"else if (choice == 2) current = c; else current = d; "
			"total = total + current.add(3); } return total; } "
			"void main() { print(work()); }");
		if (!Expect(static_cast<bool>(ModuloModule),
			"compile modulo additive polymorphic transform loop"))
		{
			return false;
		}
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
				VeroScript::EExecutionBackend::Mixed,
				VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*ModuloModule, Backend, "112\n",
				"modulo additive polymorphic transform loop semantics"))
			{
				return false;
			}
		}
		return true;
	}
}

int main()
{
	{
		const auto Module = CompileSource(std::string(VeroScript::Test::LambdaStaticDefinitions)
			+ "void main(){checkLambdaStaticSupport();}");
		if (!Expect(static_cast<bool>(Module), "initialized captures and local statics compile")) return EXIT_FAILURE;
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			if (!ExpectRun(*Module, Backend, "", "capture and static backend parity")) return EXIT_FAILURE;
		}
	}

	if (!TestScalarForwardingNativeCfg()) return EXIT_FAILURE;
	int Result = EXIT_SUCCESS;
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/01_kernel_and_diagnostics.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/02_scalar_cfg.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/03_objects_and_dispatch.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/05_owned_loop_profitability.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/06_scalar_set_probe.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/07_affine_set_kernel.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/08_native_container_query.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/09_owned_string_conversion.inl"
		#include "Suites/VeroScriptJitBackend/11_readonly_text_forwarders.inl"
		#include "Suites/VeroScriptJitBackend/12_text_cost_and_reads.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/10_constant_i64_division.inl"
		return EXIT_SUCCESS;
	}();
	Result |= []() -> int
	{
		#include "Suites/VeroScriptJitBackend/04_host_and_reload_integration.inl"
		#include "Suites/VeroScriptJitBackend/04_object_prefix.inl"
		return EXIT_SUCCESS;
	}();
	return Result;
}
