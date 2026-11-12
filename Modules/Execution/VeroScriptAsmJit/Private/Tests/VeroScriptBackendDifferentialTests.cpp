// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"
#include "VeroScriptAsmJit.h"
#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptNative.h"
#include "VeroScriptJitLinear.h"

#include "VeroScriptAst.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
	std::string ExpectedAffineFunction;
	std::uint32_t ExpectedAffineHeader = 0;
	std::size_t ObservedAffineKernels = 0;
	void ObserveAffine(VeroScript::AsmJit::FExecutionContext& Vm,
		const VeroScript::AsmJit::JitLinear::FTypedLoopPlan& Plan,
		const VeroScript::AsmJit::JitLinear::FAffineSetResult&)
	{
		const auto* Function = Vm.GetPendingJitLinearFunction();
		if (!Function)
		{
			const auto* Module = Vm.DebugGetModule();
			const auto Index = Vm.GetPendingJitFunctionIndex();
			if (Module && Index < Module->Functions.size())
			{
				Function = &Module->Functions[Index];
			}
		}
		if (Function && Function->Name == ExpectedAffineFunction
			&& Plan.HeaderOffset == ExpectedAffineHeader)
		{
			++ObservedAffineKernels;
		}
	}
	#endif
	struct FRunCapture
	{
		bool bSuccess = false;
		bool bHasError = false;
		bool bExitRequested = false;
		int ExitCode = 0;
		std::string Output;
		std::string ErrorText;
		std::string ErrorMessage;
		std::string ErrorId;
		std::vector<std::string> ErrorArgs;
		std::string ErrorFile;
		std::uint32_t ErrorLine = 0;
		std::uint32_t ErrorColumn = 0;
		std::size_t Dispatches = 0;
		std::size_t FailedDispatches = 0;
		std::size_t CostFallbacks = 0;
	};

	bool Expect(const bool Condition, const char* const Message)
	{
		if (!Condition)
		{
			std::cerr << "VeroScriptBackendDifferentialTests: " << Message << '\n';
		}
		return Condition;
	}

	bool ExpectLengthBounds()
	{
		using namespace VeroScript;
		constexpr std::uint32_t MaxLength =
			static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());
		// Length-only host values exercise the VM/JIT bridge without huge allocations.
		for (const std::uint32_t Length : {0u, MaxLength, MaxLength + 1})
		{
			std::ostringstream Output;
			std::istringstream Input;
			std::ostringstream Errors;
			VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			FNativeArray Array;
			Array.Length() = Length;
			const FValue Argument = std::move(Array);
			FValue Result;
			bool bHasResult = false;
			const bool bSuccess = Vm.JitInvokeBuiltin(EBuiltinId::Len, &Argument, 1,
				bHasResult, Result);
			const auto* Count = std::get_if<std::int64_t>(&Result);
			if (!Expect(bSuccess && bHasResult && Count
					&& *Count == static_cast<std::int64_t>(Length),
					"VM/JIT len preserves wide boundaries"))
			{
				return false;
			}
		}
		// Shared buckets model a total of 2^32 occurrences with a small backing store.
		const auto Values = std::make_shared<FDynamicArray>();
		Values->ScalarElements.emplace<std::vector<std::int32_t>>(65536, 0);
		const auto MultiSet = std::make_shared<FMap>();
		MultiSet->TypeName = "multiset<int>";
		MultiSet->KeyTypeName = "int";
		for (std::int32_t Key = 0; Key < 65536; ++Key)
		{
			MultiSet->IntEntries.emplace(Key, Values);
		}
		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		const FValue Argument = MultiSet;
		FValue Result;
		bool bHasResult = false;
		if (!Expect(Vm.JitInvokeBuiltin(EBuiltinId::Len, &Argument, 1, bHasResult, Result)
				&& bHasResult && std::get<std::int64_t>(Result) == 4294967296ll,
				"multiset len accumulates beyond 32-bit size_t"))
		{
			return false;
		}
		const auto OrderedMultiSet = std::make_shared<FOrderedMap>();
		OrderedMultiSet->TypeName = "ordered_multiset<int>";
		OrderedMultiSet->KeyTypeName = "int";
		OrderedMultiSet->IntEntries = MultiSet->IntEntries;
		const FValue OrderedArgument = OrderedMultiSet;
		if (!Expect(Vm.JitInvokeBuiltin(EBuiltinId::Len, &OrderedArgument, 1, bHasResult, Result)
				&& bHasResult && std::get<std::int64_t>(Result) == 4294967296ll,
			"ordered multiset len accumulates beyond 32-bit size_t"))
		{
			return false;
		}
		const auto Channel = std::make_shared<FChannel>();
		std::string CapacityError;
		const FValue ChannelArgument = Channel;
		if (!Expect(Channel->ConfigureCapacity(std::size_t{2147483648ull}, CapacityError)
				&& Vm.JitInvokeBuiltin(EBuiltinId::ChannelCapacity, &ChannelArgument, 1, bHasResult, Result)
				&& bHasResult && std::get<std::int64_t>(Result) == 2147483648ll,
			"channel capacity returns int64 without allocating its capacity"))
		{
			return false;
		}
		std::size_t Size = 0;
		const FValue SliceArguments[] = {std::string("abc"), std::int64_t{1},
			(std::numeric_limits<std::int64_t>::max)()};
		if (!Expect(Vm.JitInvokeBuiltin(EBuiltinId::Substr, SliceArguments, 3, bHasResult, Result)
				&& bHasResult && std::get<std::string>(Result) == "bc",
			"slice count clips before native size conversion"))
		{
			return false;
		}
		const FValue FindArguments[] = {std::string("abc"), std::string("b")};
		if (!Expect(Vm.JitInvokeBuiltin(EBuiltinId::Find, FindArguments, 2, bHasResult, Result)
				&& bHasResult && std::get<std::int64_t>(Result) == 1,
			"find returns int64"))
		{
			return false;
		}
		for (const FValue& Invalid : {FValue{std::int64_t{-1}},
			FValue{(std::numeric_limits<std::uint64_t>::max)()}, FValue{true}, FValue{1.0}})
		{
			if (!Expect(!Runtime::TryGetSizeValue(Invalid, Size), "size boundary rejects invalid values"))
			{
				return false;
			}
		}
		if (!Expect(Runtime::TryGetSizeValue(FValue{std::int64_t{4294967296ll}}, Size)
				== (static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) >= 4294967296ull),
			"size boundary rejects truncation on 32-bit hosts"))
		{
			return false;
		}
		return Expect(Runtime::TryGetSizeValue(FValue{std::int64_t{2147483648ll}}, Size)
				== (static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) >= 2147483648ull),
			"size boundary observes native platform range");
	}

	std::unique_ptr<VeroScript::FBytecodeModule> Compile(
		const std::string_view Source,
		const std::vector<VeroScript::FNativeFunction>* const Natives = nullptr,
		const VeroScript::IHostBindingCatalog* const Bindings = nullptr)
	{
		// Tokens retain source views while parsing, so the owned text must outlive
		// both lexer and parser; never pass a temporary std::string here.
		const std::string OwnedSource(Source);
		VeroScript::VeroScriptLexer Lexer(OwnedSource, "backend-matrix.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			std::cerr << Lexer.GetErrorMessage() << '\n';
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "backend-matrix.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			std::cerr << Parser.GetError().Location.Line << ':'
				<< Parser.GetError().Location.Column << ": "
				<< Parser.GetError().Message << '\n';
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		std::unique_ptr<VeroScript::FBytecodeModule> Module =
			Compiler.Compile(*Program, Natives, Bindings);
		if (!Module || Compiler.HasError())
		{
			std::cerr << Compiler.GetError().Message << '\n';
			return nullptr;
		}
		return Module;
	}

	FRunCapture Run(
		const VeroScript::FBytecodeModule& Module,
		const VeroScript::EExecutionBackend Backend,
		const std::vector<VeroScript::FNativeFunction>* const Natives = nullptr)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		std::string BackendError;
		if (!Vm.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), BackendError))
		{
			std::abort();
		}
		Vm.SetExecutionBackend(Backend);
		FRunCapture Capture;
		Capture.bSuccess = Vm.Execute(Module, VeroScript::ERunMode::Script, {}, Natives);
		Capture.Dispatches = Vm.GetJitStats().Dispatches;
		Capture.FailedDispatches = Vm.GetJitStats().Failures;
		Capture.CostFallbacks = Vm.GetJitStats().CostModelVmFallbacks;
		Capture.bHasError = Vm.HasError();
		Capture.bExitRequested = Vm.WasExitRequested();
		Capture.ExitCode = Vm.GetRequestedExitCode();
		Capture.Output = Output.str();
		Capture.ErrorText = Errors.str();
		if (Capture.bHasError)
		{
			const auto& Error = Vm.GetError();
			Capture.ErrorMessage = Error.Message;
			Capture.ErrorId = Error.MessageId;
			Capture.ErrorArgs = Error.MessageArgs;
			Capture.ErrorFile = Error.Location.FileLabel;
			Capture.ErrorLine = Error.Location.Line;
			Capture.ErrorColumn = Error.Location.Column;
		}
		return Capture;
	}

	bool ExpectEquivalent(const std::string_view Source, const char* const Name,
		const bool bRequireAffineSetPlan = false,
		const std::int32_t AffineModulusOverride = 0,
		const std::optional<bool> ExpectedSuccess = std::nullopt)
	{
		const std::unique_ptr<VeroScript::FBytecodeModule> Module = Compile(Source);
		if (!Expect(static_cast<bool>(Module), Name))
		{
			return false;
		}
		if (AffineModulusOverride != 0)
		{
			// Explicit bytecode fixture: source unary negation is a separate Neg
			// opcode and therefore not this fused shape. Negative pool immediates
			// are valid bytecode inputs too. Replace the fixture's unique 257 pool
			// entry before ANY cache is built; VM and JIT run the identical module.
			std::size_t Replaced = 0;
			for (auto& Constant : Module->Constants)
			{
				if (Constant.Kind == VeroScript::EConstantKind::I32 && Constant.I32 == 257)
				{
					Constant.I32 = AffineModulusOverride;
					++Replaced;
				}
			}
			if (!Expect(Replaced == 1, "negative affine modulus fixture must be unambiguous"))
			{
				return false;
			}
		}
		if (bRequireAffineSetPlan)
		{
			// A differential test that silently lost fusion would not guard the
			// outlined kernel. Require admission, scalar storage and an executable
			// Linear route, in addition to comparing complete runtime diagnostics.
			VeroScript::AsmJit::FJitCodeCache Cache;
			Cache.PrepareModule(*Module, VeroScript::EExecutionBackend::Jit);
			std::size_t FoundPlans = 0;
			for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
			{
				const auto* Entry = Cache.GetCandidate(static_cast<std::uint16_t>(Index));
				if (!Entry || Entry->Kind != VeroScript::AsmJit::EJitEntryKind::LinearTrampoline
					|| Entry->bPreferVmForExecution || !Entry->bUseScalarSetStorage
					|| !Entry->TypedLoopPlans)
				{
					continue;
				}
				for (const auto& Plan : Entry->TypedLoopPlans->Plans)
				{
					if (Plan.FusedKind
							== VeroScript::AsmJit::JitLinear::FTypedLoopPlan::EFusedKind::DirectAffineSetContains
						&& Plan.NativeI32Code == nullptr)
					{
						++FoundPlans;
						#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
						ExpectedAffineFunction = Module->Functions[Index].Name;
						ExpectedAffineHeader = Plan.HeaderOffset;
						#endif
					}
				}
			}
			if (!Expect(FoundPlans == 1, "affine Set input must identify exactly one target kernel"))
			{
				std::cerr << "  input=" << Name << '\n';
				for (std::size_t Index = 0; Index < Module->Functions.size(); ++Index)
				{
					const auto* Entry = Cache.GetCandidate(static_cast<std::uint16_t>(Index));
					if (!Entry)
					{
						continue;
					}
					std::cerr << "  function=" << Module->Functions[Index].Name
						<< " kind=" << static_cast<int>(Entry->Kind)
						<< " prefer-vm=" << Entry->bPreferVmForExecution
						<< " scalar-set=" << Entry->bUseScalarSetStorage << '\n';
					if (!Entry->TypedLoopPlans)
					{
						continue;
					}
					for (const auto& Plan : Entry->TypedLoopPlans->Plans)
					{
						std::cerr << "    header=" << Plan.HeaderOffset
							<< " fused=" << static_cast<int>(Plan.FusedKind)
							<< " native=" << (Plan.NativeI32Code != nullptr) << '\n';
					}
				}
				return false;
			}
		}
		const FRunCapture Vm = Run(*Module, VeroScript::EExecutionBackend::Vm);
		if (ExpectedSuccess && !Expect(Vm.bSuccess == *ExpectedSuccess, Name))
		{
			return false;
		}
		if (!Expect(Vm.bSuccess || !Vm.ErrorMessage.empty(), Name))
		{
			return false;
		}
		const bool bNativeExecution = bRequireAffineSetPlan
			&& VeroScript::CreateAsmJitBackendFactory()->GetDescription().bNativeCode;
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
		{
			#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
			ObservedAffineKernels = 0;
			VeroScript::AsmJit::JitLinear::AffineSetObserver = bRequireAffineSetPlan ? &ObserveAffine : nullptr;
			#endif
			const FRunCapture Candidate = Run(*Module, Backend);
			#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
			VeroScript::AsmJit::JitLinear::AffineSetObserver = nullptr;
			if (bRequireAffineSetPlan && !Expect(ObservedAffineKernels != 0,
				"exact affine function/header must execute the outlined kernel"))
			{
				std::cerr << "  input=" << Name << " function=" << ExpectedAffineFunction << '\n';
				return false;
			}
			if (bRequireAffineSetPlan)
			{
				std::cout << "affine-observed backend=" << VeroScript::ExecutionBackendName(Backend)
						<< " function=" << ExpectedAffineFunction << " header=" << ExpectedAffineHeader
						<< " count=" << ObservedAffineKernels << " input=" << Name << '\n';
			}
			#endif
			// Portable execution may deliberately interpret surrounding functions
			// such as main. Keep the whole-program cost assertion for native builds;
			// admission, execution and the exact kernel observer still apply to both.
			// Negative fixtures finish in Failed rather than successful Dispatches.
			if (bRequireAffineSetPlan && !Expect((Candidate.Dispatches != 0 || Candidate.FailedDispatches != 0)
					&& (!bNativeExecution || Candidate.CostFallbacks == 0),
					"affine Set input must dispatch; native execution must avoid cost fallback"))
			{
				return false;
			}
			const bool bEqual = Candidate.bSuccess == Vm.bSuccess
				&& Candidate.bHasError == Vm.bHasError
				&& Candidate.bExitRequested == Vm.bExitRequested
				&& Candidate.ExitCode == Vm.ExitCode
				&& Candidate.Output == Vm.Output
				&& Candidate.ErrorText == Vm.ErrorText
				&& Candidate.ErrorMessage == Vm.ErrorMessage
				&& Candidate.ErrorId == Vm.ErrorId
				&& Candidate.ErrorArgs == Vm.ErrorArgs
				&& Candidate.ErrorFile == Vm.ErrorFile
				&& Candidate.ErrorLine == Vm.ErrorLine
				&& Candidate.ErrorColumn == Vm.ErrorColumn;
			if (!Expect(bEqual, Name))
			{
				std::cerr << "  backend=" << VeroScript::ExecutionBackendName(Backend)
					<< " success=" << Candidate.bSuccess
					<< " has-error=" << Candidate.bHasError
					<< " output='" << Candidate.Output << "' diagnostic="
					<< Candidate.ErrorId << '@' << Candidate.ErrorFile << ':'
					<< Candidate.ErrorLine << ':' << Candidate.ErrorColumn
					<< " | VM success=" << Vm.bSuccess
					<< " has-error=" << Vm.bHasError
					<< " output='" << Vm.Output << "' diagnostic="
					<< Vm.ErrorId << '@' << Vm.ErrorFile
					<< ':' << Vm.ErrorLine << ':' << Vm.ErrorColumn << '\n';
				return false;
			}
		}
		return true;
	}

	struct FRawFloatCapture
	{
		std::vector<std::uint64_t> F64Bits;
		std::vector<std::uint32_t> F32Bits;
	};

	bool CaptureF64(const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue&, std::string& OutError)
	{
		const auto* const Value = Context.ArgCount == 1
			? std::get_if<double>(&Context.Args[0])
			: nullptr;
		auto* const Capture = static_cast<FRawFloatCapture*>(Context.UserData);
		if (!Value || !Capture)
		{
			OutError = "capture_f64 received an invalid value";
			return false;
		}
		std::uint64_t Bits = 0;
		std::memcpy(&Bits, Value, sizeof(Bits));
		Capture->F64Bits.push_back(Bits);
		return true;
	}

	bool CaptureF32(const VeroScript::FNativeCallContext& Context,
		VeroScript::FValue&, std::string& OutError)
	{
		const auto* const Value = Context.ArgCount == 1
			? std::get_if<float>(&Context.Args[0])
			: nullptr;
		auto* const Capture = static_cast<FRawFloatCapture*>(Context.UserData);
		if (!Value || !Capture)
		{
			OutError = "capture_f32 received an invalid value";
			return false;
		}
		std::uint32_t Bits = 0;
		std::memcpy(&Bits, Value, sizeof(Bits));
		Capture->F32Bits.push_back(Bits);
		return true;
	}

	bool ReturnNaN(const VeroScript::FNativeCallContext&,
		VeroScript::FValue& OutReturn, std::string&)
	{
		OutReturn = std::numeric_limits<double>::quiet_NaN();
		return true;
	}

	bool ReturnInfinity(const VeroScript::FNativeCallContext&,
		VeroScript::FValue& OutReturn, std::string&)
	{
		OutReturn = std::numeric_limits<double>::infinity();
		return true;
	}

	bool ExpectRawFloatEquivalent()
	{
		FRawFloatCapture Capture;
		VeroScript::FHostBindingRegistry Registry;
		std::string ErrorId;
		if (!Registry.RegisterNative("capture_f64", VeroScript::ENativeType::Void,
				{VeroScript::ENativeType::Double}, CaptureF64, &Capture, ErrorId)
			|| !Registry.RegisterNative("capture_f32", VeroScript::ENativeType::Void,
				{VeroScript::ENativeType::Float}, CaptureF32, &Capture, ErrorId)
			|| !Registry.RegisterNative("matrix_nan", VeroScript::ENativeType::Double,
				{}, ReturnNaN, nullptr, ErrorId)
			|| !Registry.RegisterNative("matrix_infinity", VeroScript::ENativeType::Double,
				{}, ReturnInfinity, nullptr, ErrorId))
		{
			std::cerr << ErrorId << '\n';
			return false;
		}
		const std::vector<VeroScript::FNativeFunction>& Natives = Registry.GetNativeFunctions();
		const auto Module = Compile(
			"double exact_induction(double value, int count) { int i = 0; "
			"while (i < count) { value += 1.0; i++; } return value; } "
			"double table_recurrence(int count) { array<double> values = "
			"{0.125, -0.0, 0.75, -1.5}; int state = 1; double total = -0.25; "
			"int i = 0; while (i < count) { state = (state * 5 + 1) % 16; "
			"total = total * 0.99991 + values[state % 4] * 0.00009; i++; } "
			"return total; } "
			"void main() { capture_f64(-0.0); capture_f64(abs(-0.0)); "
			"capture_f64(matrix_nan()); capture_f64(matrix_infinity()); "
			"capture_f64(min(matrix_nan(), 1.0)); "
			"capture_f64(min(1.0, matrix_nan())); "
			"capture_f64(min(-0.0, 0.0)); capture_f64(min(0.0, -0.0)); "
			"capture_f64(max(matrix_nan(), 1.0)); "
			"capture_f64(max(1.0, matrix_nan())); "
			"capture_f64(max(-0.0, 0.0)); capture_f64(max(0.0, -0.0)); "
			"capture_f64(exact_induction(-5.0, 10)); "
			"capture_f64(exact_induction(0.5, 10)); "
			"capture_f64(exact_induction(9007199254740992.0, 2)); "
			"capture_f64(exact_induction(-9007199254740992.0, 2)); "
			"capture_f64(table_recurrence(10000)); "
			"capture_f32(-0.0f); capture_f32(abs(-0.0f)); }",
			&Natives, &Registry);
		if (!Expect(static_cast<bool>(Module), "raw floating bit matrix"))
		{
			return false;
		}
		std::vector<std::uint64_t> ExpectedF64;
		std::vector<std::uint32_t> ExpectedF32;
		for (const VeroScript::EExecutionBackend Backend :
			{VeroScript::EExecutionBackend::Vm,
			 VeroScript::EExecutionBackend::Mixed,
			 VeroScript::EExecutionBackend::Jit})
		{
			Capture.F64Bits.clear();
			Capture.F32Bits.clear();
			const FRunCapture Result = Run(*Module, Backend, &Natives);
			if (!Expect(Result.bSuccess, "raw floating bit matrix execution"))
			{
				return false;
			}
			if (Backend == VeroScript::EExecutionBackend::Vm)
			{
				ExpectedF64 = Capture.F64Bits;
				ExpectedF32 = Capture.F32Bits;
			}
			else if (!Expect(Capture.F64Bits == ExpectedF64
				&& Capture.F32Bits == ExpectedF32, "raw floating bit matrix"))
			{
				return false;
			}
		}
		return true;
	}

	std::string BuildDeepTypedLoopStackSource()
	{
		// Keep the expression data-dependent so the compiler cannot fold it. The
		// right-associated form verifies CFG-derived stack sizing and merge depths
		// around a preceding field mutation.
		std::string Expression = "counter.value";
		for (std::size_t Index = 0; Index < 20; ++Index)
		{
			Expression = "i + (" + Expression + ")";
		}
		return
			"class Counter { public: int value = 1; }; "
			"int work() { Counter counter = Counter(); int total = 0; "
			"for (int i = 0; i < 2; i++) { counter.value++; total += "
			+
			Expression
			+ "; } return total; } void main() { print(work()); }";
	}

	bool ExpectNestedMutationObserver()
	{
		const auto Module = Compile("class Counter {public:int value=0;void add(){value++;}};"
			"int outer(Counter c){for(int i=0;i<64;i++)c.add();return c.value;}"
			"void main(){Counter c=Counter();print(outer(c));}");
		if (!Expect(static_cast<bool>(Module), "nested observer compile")) return false;
		for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Jit})
		{
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			std::string Error;
			if (!Vm.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), Error)) return false;
			Vm.SetExecutionBackend(Backend);
			std::size_t Notifications = 0;
			Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
			{
				if (Event.FieldName == "value")
				{
					++Notifications;
					// Reallocate synchronously: a caller must reacquire field storage.
					Event.Object->Fields.reserve(Event.Object->Fields.capacity() + 1);
				}
			});
			if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script) && Output.str() == "64\n" && Errors.str().empty() && Notifications == 64,
						"nested mutable methods preserve synchronous observer storage invalidation"))
			{
				return false;
			}
		}
		return true;
	}

	bool ExpectNativeMemoryReferenceReentry()
	{
		using namespace VeroScript;
		FNativeFunction Scalar;
		Scalar.Name = "scalar_memory_probe";
		Scalar.ReturnType = ENativeType::Int;
		Scalar.ParamTypes = {ENativeType::Int};
		Scalar.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string&)
		{
			std::uint64_t Address = 0;
			const bool Valid = Call.MemoryServices &&
				Call.MemoryServices->MemoryArgumentAddress(0, Address).Status == EMemoryStatus::Unsupported;
			Out = Valid ? Call.Args[0] : FValue{std::int32_t{-1}};
			return true;
		};
		FNativeFunction Reference;
		Reference.Name = "ref_memory_probe";
		Reference.ReturnType = ENativeType::Bool;
		Reference.ParamTypes = {ENativeType::Int, ENativeType::UInt64};
		Reference.ParamFlags = {1, 0};
		Reference.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string& Error)
		{
			std::uint64_t Before = 0, After = 0;
			const auto* Vm = dynamic_cast<VeroScriptVm*>(Call.ScriptInvoker);
			const auto ScalarCalls = Vm ? Vm->GetJitStats().ScalarNativeFastPathCalls : 0;
			std::vector<FValue> Arguments{std::int32_t{9}};
			FValue Returned;
			Out = Call.MemoryServices && Vm &&
				Call.MemoryServices->MemoryArgumentAddress(0, Before) &&
				Before == std::get<std::uint64_t>(Call.Args[1]) &&
				Call.ScriptInvoker->InvokeFunction("scalar_work", Arguments, Returned, Error) &&
				std::get<std::int32_t>(Returned) == 9 &&
				Vm->GetJitStats().ScalarNativeFastPathCalls > ScalarCalls &&
				Call.MemoryServices->MemoryArgumentAddress(0, After) && After == Before;
			return true;
		};
		const std::vector<FNativeFunction> Natives{Scalar, Reference};
		const auto Module = Compile(
			"export int scalar_work(int x){return scalar_memory_probe(x);}"
			"void main(){int x=4;auto address=std::memory::address_of(&x);"
			"assert(ref_memory_probe(ref x,address));}", &Natives);
		if (!Expect(static_cast<bool>(Module), "native memory reference reentry compile")) return false;
		const auto Result = Run(*Module, EExecutionBackend::Jit, &Natives);
		if (!Result.bSuccess)
		{
			std::cerr << Result.ErrorMessage << '\n';
		}
		return Expect(Result.bSuccess && Result.ErrorText.empty(),
			"JIT scalar native services reject outer references and restore them after reentry");
	}

	bool ExpectTypedFieldMutationCallbackFallback()
	{
		const auto Module = Compile(
			"class Counter { public: int value = 0; }; "
			"int work() { Counter counter = Counter(); int total = 0; "
			"for (int i = 0; i < 16; i++) { counter.value += i + 1; "
			"total += counter.value; } return total; } "
			"void main() { print(work()); }");
		if (!Expect(static_cast<bool>(Module),
				"typed field mutation callback fallback compile"))
		{
			return false;
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		std::string BackendError;
		if (!Vm.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), BackendError))
		{
			std::abort();
		}
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		std::size_t MutationCount = 0;
		Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
		{
			if (Event.FieldName == "value")
			{
				++MutationCount;
			}
		});
		return Expect(
			Vm.Execute(*Module, VeroScript::ERunMode::Script)
				&& Output.str() == "816\n" && Errors.str().empty()
				&& MutationCount == 16u,
			"typed field plan preserves mutation callback semantics");
	}

	bool ExpectTypedBatchFieldMutationCallbackFallback()
	{
		const auto Module = Compile(
			"class Fields { public: int a = 0; int b = 0; int c = 0; }; "
			"int work() { Fields value = Fields(); for (int i = 0; i < 8; i++) { "
			"value.a += 1; value.b += 2; value.c += 3; } "
			"return value.a + value.b + value.c; } "
			"void main() { print(work()); }");
		if (!Expect(static_cast<bool>(Module),
				"typed batch field mutation callback fallback compile"))
		{
			return false;
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream Input;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		std::string BackendError;
		if (!Vm.SetJitBackendFactory(VeroScript::CreateAsmJitBackendFactory(), BackendError))
		{
			std::abort();
		}
		Vm.SetExecutionBackend(VeroScript::EExecutionBackend::Jit);
		std::size_t MutationCount = 0;
		Vm.SetMutationCallback([&](const VeroScript::FMutationEvent& Event)
		{
			if (Event.FieldName == "a" || Event.FieldName == "b"
				|| Event.FieldName == "c")
			{
				++MutationCount;
			}
		});
		return Expect(
			Vm.Execute(*Module, VeroScript::ERunMode::Script)
				&& Output.str() == "48\n" && Errors.str().empty()
				&& MutationCount == 24u,
			"typed batch field plan preserves mutation callback semantics");
	}

}

int main()
{
	constexpr std::uint32_t FailureOffset = 0x00543210u;
	constexpr std::int32_t EncodedFailure = VeroScript::AsmJit::EncodeJitKernelFailure(
		VeroScript::AsmJit::EJitRuntimeStatus::DivisionByZero, FailureOffset);
	constexpr VeroScript::AsmJit::FJitKernelFailure DecodedFailure =
		VeroScript::AsmJit::DecodeJitKernelFailure(EncodedFailure);
	static_assert(DecodedFailure.Status == VeroScript::AsmJit::EJitRuntimeStatus::DivisionByZero);
	static_assert(DecodedFailure.InstructionOffset == FailureOffset);
	// The matrix intentionally combines outputs and errors.  A JIT backend is
	// only equivalent when stable diagnostic metadata, not just rendered text,
	// agrees with the interpreter.
	return ExpectNativeMemoryReferenceReentry()
		&& ExpectNestedMutationObserver()
		&& ExpectLengthBounds()
		&& ExpectEquivalent(R"VS(
class B
{
public:
    int state=0;
    B(bool verify)
    {
        if(verify)
        {
            assert(constants(this,peer)==9);
            assert(transforms(this,peer)==24);
            assert(conditional(this,peer)==26);
            fields(this);
            assert(state==3);
        }
    }
    virtual int read(){return 1;}
    virtual int apply(int value){return value+1;}
    virtual int bump(int value){state+=value;return state;}
    ~B(){assert(read()==1);}
};
class D:public B
{
public:
    string text="ready";
    D(bool verify):B(verify){}
    int read() override{assert(text=="ready");return 2;}
    int apply(int value) override{return value+2;}
    int bump(int value) override{state+=value*2;return state;}
    ~D(){assert(read()==2);}
};
B peer=nullptr;
int constants(B constructing,B ready)
{
    int total=0;
    for(int i=0;i<6;++i)
    {
        B selected=constructing;
        if((i&1)!=0)selected=ready;
        total+=selected.read();
    }
    return total;
}
int transforms(B constructing,B ready)
{
    array<B> values={constructing,ready};
    int total=0;
    for(int i=0;i<6;++i)total+=values[i&1].apply(i);
    return total;
}
int conditional(B constructing,B ready)
{
    B selected=constructing;
    int total=0;
    for(int i=0;i<6;++i)
    {
        if((i&1)!=0)selected=ready;
        total+=selected.apply(i);
    }
    return total;
}
void fields(B value){for(int i=0;i<3;++i)value.bump(1);}
void work()
{
    D ready=D(false);
    peer=ready;
    assert(constants(ready,ready)==12);
    assert(transforms(ready,ready)==27);
    assert(conditional(ready,ready)==27);
    fields(ready);
    assert(ready.state==6);
    D value=D(true);
    assert(value.read()==2);
    peer=nullptr;
}
void main(){work();print("lifetime virtual dispatch");}
)VS", "constructor and destructor virtual calls keep their lifetime dispatch phase", false, 0, true)
		&& ExpectEquivalent(R"VS(
using Count = int64;
struct ScalarFields
{
    Count wide = 0i64;
    double decimal = 0.0;
    uint8 narrow = 0;
    float real = 0.0f;
    int character = 0;
};
int calls = 0;
int next_value() { calls++; return 7; }
void main()
{
    ScalarFields value = ScalarFields();
    field_info<ScalarFields> wide = field_info<ScalarFields>(type<ScalarFields>().qualified_name, 0);
    wide.set<Count>(value, next_value());
    assert(calls == 1 && wide.get<Count>(value) == 7i64);
    field_info<ScalarFields> decimal = field_info<ScalarFields>(type<ScalarFields>().qualified_name, 1);
    decimal.set<double>(value, 9i64);
    assert(decimal.get<double>(value) == 9.0);
    field_info<ScalarFields> narrow = field_info<ScalarFields>(type<ScalarFields>().qualified_name, 2);
    narrow.set<uint8>(value, 255i64);
    assert(to_int(narrow.get<uint8>(value)) == 255);
    field_info<ScalarFields> real = field_info<ScalarFields>(type<ScalarFields>().qualified_name, 3);
    real.set<float>(value, 1.5);
    assert(real.get<float>(value) == 1.5f);
    field_info<ScalarFields> character = field_info<ScalarFields>(type<ScalarFields>().qualified_name, 4);
    character.set<int>(value, 'A');
    assert(character.get<int>(value) == 65);
}
)VS", "reflected field writes perform checked assignment conversions once", false, 0, true)
		&& ExpectEquivalent(
			"struct Box { uint8 value = 7; }; void main() { Box box = Box(); "
			"field_info<Box> field = field_info<Box>(type<Box>().qualified_name, 0); "
			"field.set<uint8>(box, 256); }",
			"reflected field narrowing rejects overflow", false, 0, false)
		&& ExpectEquivalent(
			"struct Box { int64 value = 7i64; }; void main() { Box box = Box(); "
			"field_info<Box> field = field_info<Box>(type<Box>().qualified_name, 0); "
			"field.set<double>(box, 3); }",
			"reflected field conversion cannot bypass the declared field type", false, 0, false)
		&& ExpectEquivalent(R"VS(int classify(int value) { return 1; }
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
    multiset<int> counts = multiset<int>();
    counts.add(7); counts.add(7);
    assert(classify(counts.count(7)) == 2 && counts.count(7) == 2i64, "wide count failed");
    print("wide lengths passed");
}
)VS",
									   "wide length and index contract", false, 0, true)
		&& ExpectEquivalent("void main(){array<int> a={1};print(a[4294967296i64]);}", "wide index cannot wrap", false, 0, false)
		&& ExpectEquivalent("void main(){int a=2147483648i64;print(a);}", "implicit narrowing rejects overflow", false, 0, false)
		&& ExpectRawFloatEquivalent()
		&& ExpectEquivalent(
			"char id(char value){return value;} void main(){int i=0;int sum=0;"
			"do{i++;if(i==2)continue;sum+=i;}while(i<4);int left=0;int right=0;"
			"for(left=1,right=left+1;left<4;left++,right+=2)sum+=left+right;"
			"print(sum);print(to_int(id('A')));print(len(\"\\xFF\"));"
			"print(len(\"\\u00FF\"));}",
			"do-while character escape and for-expression-list equivalence")
		&& ExpectEquivalent(
			"class Item { public:int value=0;int get(){return value;} }; "
			"int work(){array<Item> values=array<Item>();for(int i=0;i<4;i++){"
			"Item item=Item();item.value=i;values.push(item);}int total=0;"
			"for(int i=0;i<4;i++){values[i].value+=1;total+=values[i].get();}return total;}"
			"void main(){print(work());}",
			"retained object construction/traversal mutation semantics")
		&& ExpectEquivalent(
			"class Item { public:int value=0;int get(){return value;} }; "
			"int work(){array<Item> values=array<Item>();for(int i=0;i<4;i++){"
			"Item item=Item();item.value=i;values.push(item);}int total=0;"
			"for(int i=0;i<5;i++){print(i);total+=values[i].get();}return total;}"
			"void main(){print(work());}",
			"retained object loop preserves partial output and bounds diagnostic")
		&& ExpectEquivalent(
			"class Item { public:int value=2147483647;int get(){return value;} }; "
			"int work(){array<Item> values=array<Item>();for(int i=0;i<2;i++){"
			"Item item=Item();values.push(item);}int total=0;"
			"for(int i=0;i<2;i++){print(i);total+=values[i].get();}return total;}"
			"void main(){print(work());}",
			"retained object loop preserves partial output and checked overflow")
		&& ExpectTypedFieldMutationCallbackFallback()
		&& ExpectTypedBatchFieldMutationCallbackFallback()
		&& ExpectEquivalent(
		"void main() { int total = 0; int i = 0; while (i < 32) { total += i; i += 1; } "
		"print(total); }",
		"integer loop")
		&& ExpectEquivalent(
			"void main() { double value = -0.0; print(abs(value)); "
			"print(1.0 / 2.0); }",
			"floating signed-zero")
		&& ExpectEquivalent(
			"double mixed_numeric(double value, int count, uint32 offset, int64 wide) { "
			"int i = 0; while (i < count) { value = value * i + offset - wide; "
			"if (value > i) value = value / 2; else value = value + i; i++; } "
			"return value; } float mixed_f32(float value, int scale) { "
			"return value * scale + 1; } void main() { "
			"print(mixed_numeric(0.25, 12, 7u, 3i64)); "
			"print(mixed_f32(-0.5f, 3)); "
			"print(1.0 == 1); print(2.0f < 3); "
			"print(0.5 + 4294967295u); print(0.25f + 4294967295u); }",
			"implicit integer floating promotion in native CFG")
		&& ExpectEquivalent(
			"void main() { int zero = 0; print(3.0 / zero); }",
			"promoted floating division by zero diagnostic")
		&& ExpectEquivalent(
		"class Counter { public: int value = 0; }; int work() { "
			"Counter left = Counter(); Counter right = Counter(); "
			"for (int i = 0; i < 32; i++) { if ((i & 1) == 0) "
			"left.value += i; else right.value += i; } "
			"return left.value + right.value; } void main() { print(work()); }",
		"branched same-name fields keep receiver identity")
		&& ExpectEquivalent(
			"int work(int limit, int seed) { array<int> values = array<int>(); "
			"values.reserve(32); for (int i = 0; i < 32; i++) values.push(i); "
			"int state = seed; int total = 0; for (int i = 0; i < limit; i++) { "
			"state = (state * 13 + 7) % 32; int delta = state % 5 + 1; "
			"values[state] += delta; total = (total + values[state]) % 1000003; } "
			"return total + values[state]; } void main() { print(work(257, 3)); }",
			"direct affine array compound update")
		&& ExpectEquivalent(
			"int work() { map<int,int> values = map<int,int>(); "
			"for (int i = 0; i < 32; i++) values[i] = i; int state = 1; "
			"int total = 0; int i = 0; while (i < 257) { "
			"state = (state * 13 + 7) % 32; values[state] = values[state] + 2; "
			"total = (total + values[state]) % 1000003; i++; } "
			"return total + values[state]; } void main() { print(work()); }",
			"constant-bound affine map update")
		&& ExpectEquivalent(
			"int work() { set<int> values = set<int>(); for (int i = 0; i < 32; i++) "
			"if ((i % 3) == 0) values.add(i); int state = 1; int total = 0; "
			"int i = 0; while (i < 257) { state = (state * 13 + 7) % 32; "
			"if (values.has(state)) total++; i++; } return total; } "
			"void main() { print(work()); }",
			"constant-bound affine set lookup")
		&& ExpectEquivalent(
			"class Fields { public: int a = 1; int b = 2; int c = 3; int d = 4; }; "
			"int work(int limit, int seed) { Fields value = Fields(); int state = seed; "
			"int total = 0; for (int i = 0; i < limit; i++) { "
			"state = (state * 13 + 7) % 32; int choice = state % 4; "
			"if (choice == 0) { value.a++; total += value.a; } "
			"else if (choice == 1) { value.b++; total += value.b; } "
			"else if (choice == 2) { value.c++; total += value.c; } "
			"else { value.d++; total += value.d; } total %= 1000003; } "
			"return total; } void main() { print(work(257, 3)); }",
			"stable shape field selector accumulation")
		&& ExpectEquivalent(
			"class Fields { public: int a = 1; int b = 2; int c = 3; }; "
			"int work(int limit) { Fields value = Fields(); int i = 0; "
			"while (i < limit) { value.a += 3; value.b += -2; value.c += 5; i++; } "
			"return value.a + value.b + value.c; } "
			"void main() { print(work(257)); }",
			"stable shape batch field update")
		&& ExpectEquivalent(
			"void main() { int8 i8 = -128; uint8 u8 = 255u; "
			"int16 i16 = -32768; uint16 u16 = 65535u; "
			"int64 i64 = -9223372036854775807i64; "
			"uint64 u64 = 18446744073709551615u64; "
			"print(i8); print(u8); print(i16); print(u16); print(i64); print(u64); "
			"print(i8 + 1); print(u16 >> 1); }",
			"all integer-width boundaries")
		&& ExpectEquivalent(
			"void main() { int value = 2147483647; print(value + 1); }",
			"integer overflow diagnostic")
		&& ExpectEquivalent(
			"int transform(int value, int salt) { "
			"return ((value + (salt % 17) + 1) % 1000003); } "
			"int work(int seed) { int state = seed; int total = 3; "
			"for (int i = 0; i < 1000; i++) { state = transform(state, i); "
			"if ((state & 8) != 0) total = (total + state) % 97; "
			"else total = (total - state) % 89; } return total + state; } "
			"void main() { print(work(-123)); }",
			"signed immediate register chains preserve remainder and branches")
		&& ExpectEquivalent(
			"uint32 transform(uint32 value, uint32 salt) { "
			"value = value * 1664525u + 1013904223u; "
			"return (value ^ (salt & 255u)) % 65521u; } "
			"void main() { uint32 value = 4294967290u; "
			"for (uint32 i = 0u; i < 1000u; i++) value = transform(value, i); "
			"print(value); }",
			"unsigned immediate register chains preserve wrapping semantics")
		&& ExpectEquivalent(
			"void main() { int source = 2147483640; int value = source + 7; "
			"value = value + 1; print(value); }",
			"immediate register chain overflow exact diagnostic")
		&& ExpectEquivalent(
			"int grow(int start) {\n"
			"  int total = start;\n"
			"  int i = 0;\n"
			"  while (i < 2) { total += 1; i += 1; }\n"
			"  return total;\n"
			"}\n"
			"void main() { print(grow(2147483647)); }\n",
			"closed recurrence overflow exact diagnostic location")
		&& ExpectEquivalent(
			"void main() { int zero = 0; print(9 / zero); }",
			"division by zero diagnostic")
		&& ExpectEquivalent(
			"int checked_transform(int value, int divisor) {\n"
			"  int next = value + 1;\n"
			"  if ((next & 1) == 0) next += 2;\n"
			"  return next % divisor;\n"
			"}\n"
			"int invoke_checked(int value, int divisor) {\n"
			"  return checked_transform(value, divisor);\n"
			"}\n"
			"void main() { print(invoke_checked(3, 0)); }\n",
			"inlined scalar callee exact diagnostic location")
		&& ExpectEquivalent(
			"int direct_checked(int value, int divisor) {\n"
			"  value += 1; value += 2; value += 3; value += 4; value += 5;\n"
			"  value += 6; value += 7; value += 8; value += 9; value += 10;\n"
			"  value += 11; value += 12; value += 13; value += 14; value += 15;\n"
			"  value += 16; value += 17; value += 18; value += 19; value += 20;\n"
			"  value += 21; value += 22; value += 23; value += 24; value += 25;\n"
			"  value += 26; value += 27; value += 28; value += 29; value += 30;\n"
			"  return value / divisor;\n"
			"}\n"
			"int invoke_direct_checked(int value, int divisor) {\n"
			"  return direct_checked(value, divisor);\n"
			"}\n"
			"void main() { print(invoke_direct_checked(3, 0)); }\n",
			"generation-scoped direct callee exact diagnostic location")
		&& ExpectEquivalent(
			"int choose(int left, int right) { if ((right & 1) == 0) "
			"return left + right; return left - right; } "
			"void main() { int left = 27; int right = 8; "
			"print(choose(left, right)); print(left); print(right); }",
			"read-only scalar callee forwards caller locals by value")
		&& ExpectEquivalent(
			"int mutate_parameter(int value) { value += 3; return value; } "
			"void main() { int value = 11; print(mutate_parameter(value)); print(value); }",
			"written scalar parameter retains private callee storage")
		&& ExpectEquivalent(
			"int tail_sum(int remaining, int total) { if (remaining <= 0) return total; "
			"return tail_sum(remaining - 1, total + 1); } "
			"void main() { print(tail_sum(1000, 7)); }",
			"checked scalar tail recursion")
		&& ExpectEquivalent(
			"int tail_depth(int remaining) { if (remaining <= 0) return 0; "
			"return tail_depth(remaining - 1); } "
			"void main() { print(tail_depth(20000)); }",
			"tail recursion language depth limit")
		&& ExpectEquivalent(
			"int gcd(int a, int b) { if (b == 0) return a; return gcd(b, a % b); } "
			"int random_gcd(int count) { uint32 state = 975312468u; int total = 0; "
			"for (int i = 0; i < count; i++) { state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); "
			"int a = to_int(state & 65535u) + 1; state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); "
			"int b = to_int(state & 65535u) + 1; total = (total + gcd(a, b)) % 1000000007; } "
			"return total; } void main() { print(random_gcd(100)); }",
			"dynamic multi-parameter tail recursion")
		&& ExpectEquivalent(
			"int gcd_checked(int a, int b) { if (b == 0) return a; "
			"return gcd_checked(b, a % b); } "
			"void main() { int minimum = -2147483647 - 1; "
			"print(gcd_checked(minimum, -1)); }",
			"Euclidean tail recursion preserves modulo overflow")
		&& ExpectEquivalent(
			"int gcd_deep(int a, int b) { if (b == 0) return a; "
			"return gcd_deep(b, a % b); } "
			"int descend_to_gcd(int remaining) { "
			"if (remaining == 0) return gcd_deep(1836311903, 1134903170); "
			"return descend_to_gcd(remaining - 1); } "
			"void main() { print(descend_to_gcd(1010)); }",
			"Euclidean tail recursion preserves combined call-depth limit")
		&& ExpectEquivalent(
			"uint32 next(uint32 value) { value = value ^ (value << 13u); "
			"value = value ^ (value >> 17u); return value ^ (value << 5u); } "
			"int filler_a() { int total = 0; for (int i = 0; i < 3; i++) "
			"total = (total + i + 1023) % 1000000007; return total; } "
			"int filler_b() { uint32 state = 123456789u; for (int i = 0; i < 3; i++) "
			"state = next(state); return to_int(state & 255u); } "
			"int random_array() { array<int> values = array<int>(); values.reserve(64); "
			"for (int i = 0; i < 64; i++) values.push(i & 255); uint32 state = 362436069u; "
			"int total = 0; for (int i = 0; i < 100; i++) { state = next(state); "
			"int index = to_int(state & 63u); int value = values[index]; "
			"value = (value + to_int((state >> 16u) & 7u)) & 1023; values[index] = value; "
			"total = (total + value) % 1000000007; } return total; } "
			"void main() { print(filler_a()); print(filler_b()); print(random_array()); }",
			"speculative scalar probe must not report invalid global")
		&& ExpectEquivalent(
			"int div3(int value) { return value / 3; } "
			"int mod3(int value) { return value % 3; } "
			"int div_neg97(int value) { return value / -97; } "
			"int mod_neg97(int value) { return value % -97; } "
			"int div_large(int value) { return value / 1000000007; } "
			"int mod_large(int value) { return value % 1000000007; } "
			"void main() { "
			"int lo = -2147483647 - 1; int hi = 2147483647; "
			"print(div3(lo)); print(mod3(lo)); print(div3(hi)); print(mod3(hi)); "
			"print(div_neg97(lo)); print(mod_neg97(lo)); "
			"print(div_neg97(hi)); print(mod_neg97(hi)); "
			"print(div_large(lo)); print(mod_large(lo)); "
			"print(div_large(hi)); print(mod_large(hi)); }",
			"signed constant division and modulo boundaries")
		&& ExpectEquivalent(
			"int div_min(int value) { return value / (-2147483647 - 1); } "
			"int mod_min(int value) { return value % (-2147483647 - 1); } "
			"void main() { int lo = -2147483647 - 1; int hi = 2147483647; "
			"print(div_min(lo)); print(mod_min(lo)); "
			"print(div_min(hi)); print(mod_min(hi)); }",
			"signed minimum constant divisor boundaries")
		&& ExpectEquivalent(
			"void main() {\n"
			"  int numerator = 12;\n"
			"  int safe = 3;\n"
			"  print(numerator / safe);\n"
			"  int zero = 0;\n"
			"  print(numerator / zero);\n"
			"}\n",
			"second division exact diagnostic location")
		&& ExpectEquivalent(
			"void main() {\n"
			"  int value = 10;\n"
			"  value += 1;\n"
			"  value = 2147483647;\n"
			"  value += 1;\n"
			"  print(value);\n"
			"}\n",
			"second overflow exact diagnostic location")
		&& ExpectEquivalent(
			"void main() { int count = 32; print(1 << count); }",
			"shift-range diagnostic")
		&& ExpectEquivalent(
			"class Counter { public: int value = 1; }; "
			"void main() { Counter counter = Counter(); counter.value++; "
			"print(counter.value); counter.value = 2147483647; counter.value++; }",
			"discarded local-object field postfix increment")
		&& ExpectEquivalent(
			"class Counter { public: int value = 1; }; "
			"int work(int count) { Counter counter = Counter(); int total = 0; "
			"for (int i = 0; i < count; i++) { counter.value++; "
			"total += counter.value; } return total; } "
			"void main() { print(work(100)); }",
			"fused local-object field increment and accumulation")
		&& ExpectEquivalent(
			"class NamedLikeContainer { public: int value=0; "
			"bool has(int candidate){return candidate>0;} }; "
			"int work(){NamedLikeContainer item=NamedLikeContainer();int total=0;"
			"for(int i=0;i<32;i++){item.value++;"
			"if(item.has(i))total+=item.value;}return total;}"
			"void main(){print(work());}",
			"user method named has is not treated as a typed container call")
		&& ExpectEquivalent(
			"class Pair { public: int x=0; int y=0; "
			"int sum(int salt) { return x + y + (salt & 1); } }; "
			"int work(int count) { int state=7; int total=0; "
			"for (int i=0; i<count; i++) { state=(state*75+74)%101; "
			"Pair value=Pair(); value.x=state%17; value.y=(state/3)%19; "
			"total=(total+value.sum(state))%1000000007; } return total+state; } "
			"void main(){print(work(1000));}",
			"scalar-replaced short object method with argument")
		&& ExpectEquivalent(
			"class Pair { public: int x=2147483647; "
			"int sum(int salt) { return x + salt; } }; "
			"void main(){Pair value=Pair(); value.x=2147483647; "
			"int total=0; total=total+value.sum(1); print(total);}",
			"scalar-replaced short object argument overflow diagnostic")
		&& ExpectEquivalent(
			"class Counter { public: int value = 0; };\n"
			"void main() {\n"
			"  Counter counter = Counter();\n"
			"  int total = 2147483647;\n"
			"  counter.value++;\n"
			"  total += counter.value;\n"
			"  print(total);\n"
			"}\n",
			"fused field accumulation exact failure location")
		&& ExpectEquivalent(
			"void main() { try { throw 17; } catch (int value) { print(value); } }",
			"throw catch")
		&& ExpectEquivalent(
			"int divide(int value, int divisor) { return value / divisor; } "
			"void main() { try { print(divide(7, 0)); } "
			"catch (...) { print(99); } }",
			"inlined catch-only scalar call preserves runtime-error semantics")
		&& ExpectEquivalent(
			"void main() { array<int> values = array<int>(); values.push(4); "
			"values.push(5); print(values[2]); }",
			"array bounds diagnostic")
		&& ExpectEquivalent(
			"void main() { map<string, int> values = map<string, int>(); "
			"values[\"present\"] = 1; "
			"print(values[\"missing\"]); }",
			"map missing-key diagnostic")
		&& ExpectEquivalent(
			"void main() { map<string, double> values = map<string, double>(); "
			"values.reserve(8); values[\"alpha\"] = 1.25; values[\"beta\"] = 2.5; "
			"print(values[\"alpha\"] + values[\"beta\"]); }",
			"typed string-key extended scalar map")
		&& ExpectEquivalent(
			"void main() { map<string,int> values = map<string,int>(); "
			"values[\"alpha\"] = 17; values[\"beta\"] = 29; "
			"print(values[\"alpha\"] + values[\"beta\"]); }",
			"string constant pool indirection in typed map expression")
		&& ExpectEquivalent(
			"int work() { array<int> values = array<int>(); values.reserve(257); "
			"int i = 0; while (i < 257) { values.push(0); i++; } "
			"int state = 1; int total = 0; i = 0; while (i < 10000) { "
			"state = (state * 75 + 74) % 257; values[state] = values[state] + 3; "
			"total = (total + values[state]) % 1000000007; i++; } "
			"return total + values[state]; } void main() { print(work()); }",
			"typed random array loop")
		&& ExpectEquivalent(
			"int work() { array<int> values = array<int>(); values.reserve(257); "
			"int i = 0; while (i < 257) { values.push(i % 11); i++; } "
			"int state = 3; int total = 5; i = 0; while (i < 12345) { "
			"state = (state * 75 + 74) % 257; int delta = (state % 13) + 2; "
			"values[state] += delta; total = (total + values[state]) % 1000003; i++; } "
			"return total + values[state]; } void main() { print(work()); }",
			"typed random array state-derived delta")
		&& ExpectEquivalent(
			"int work() { map<int,int> values = map<int,int>(); values.reserve(257); "
			"int i = 0; while (i < 257) { values[i] = i % 11; i++; } "
			"int state = 3; int total = 5; i = 0; while (i < 12345) { "
			"state = (state * 75 + 74) % 257; int delta = (state % 13) + 2; "
			"values[state] += delta; total = (total + values[state]) % 1000003; i++; } "
			"return total + values[state]; } void main() { print(work()); }",
			"typed random map state-derived delta")
		&& ExpectEquivalent(
			"int work(int limit, int seed) { array<int> order=array<int>(); "
			"array<int> values=array<int>(); order.reserve(64); values.reserve(64); "
			"int state=seed; for(int i=0;i<64;i++){state=(state*13+7)%64; "
			"order.push(state); values.push((i*5+3)%17);} int cursor=seed; int total=0; "
			"for(int i=0;i<limit;i++){cursor=(cursor*9+5)%64; int index=order[cursor]; "
			"int delta=(order[(cursor+11)%64]%5)+1; values[index]+=delta; "
			"total=(total+values[index])%1000003;} return total+values[order[cursor]];} "
			"void main(){print(work(1000,17));}",
			"typed multi-array indirect loop")
		&& ExpectEquivalent(
			"int work(int limit, int seed) { array<int> order=array<int>(); "
			"map<int,int> values=map<int,int>(); order.reserve(64); values.reserve(64); "
			"int state=seed; for(int i=0;i<64;i++){state=(state*13+7)%64; "
			"int key=state*17+3; order.push(key); values[key]=(i*5+3)%17;} "
			"int cursor=seed; int total=0; for(int i=0;i<limit;i++){"
			"cursor=(cursor*9+5)%64; int key=order[cursor]; int value=values[key]; "
			"value+=(order[(cursor+11)%64]%5)+1; values[key]=value; "
			"total=(total+value)%1000003;} return total+values[order[cursor]];} "
			"void main(){print(work(1000,17));}",
			"typed array-map indirect loop")
		&& ExpectEquivalent(
			"int work(int limit,int seed){array<int> order=array<int>();"
			"map<int,int> values=map<int,int>();order.reserve(64);values.reserve(64);"
			"int state=seed;for(int i=0;i<64;i++){state=(state*13+7)%64;"
			"int key=state*17+3;order.push(key);values[key]=(i*5+3)%17;}"
			"values[1000001]=11;values[1000257]=22;values[1000513]=33;"
			"int cursor=seed;int total=0;for(int i=0;i<limit;i++){"
			"cursor=(cursor*9+5)%64;int key=order[cursor];int value=values[key];"
			"value+=(order[(cursor+11)%64]%5)+1;values[key]=value;"
			"total=(total+value)%1000003;}bool before=values.has(order[0]);"
			"bool removed=values.remove(order[0]);bool after=values.has(order[0]);"
			"bool clusteredRemoved=values.remove(1000257);"
			"bool clusteredSurvivor=values.has(1000513);total+=values[1000513];"
			"if(before)total+=100000;if(removed)total+=200000;"
			"if(after)total+=400000;if(clusteredRemoved)total+=800000;"
			"if(clusteredSurvivor)total+=1600000;return total;}"
			"void main(){print(work(1000,17));}",
			"typed scalar map methods retain raw-storage semantics")
		&& ExpectEquivalent(
			"int work(){map<int,int> values=map<int,int>();int total=0;"
			"for(int i=0;i<211;i++){int key=(i*257+3)%4096;values[key]=i+1;}"
			"for(int i=0;i<211;i++){int key=(i*257+3)%4096;"
			"if((i%4)==0){if(values.remove(key))total+=3;}"
			"else if(values.has(key))total=(total+values[key])%1000003;}"
			"for(int i=0;i<211;i++){int key=(i*257+3)%4096;"
			"if(values.has(key))total=(total+values[key])%1000003;}"
			"return total;}void main(){print(work());}",
			"typed map has after adjacent mutation loops")
		&& ExpectEquivalent(
			"int work(int limit, int seed) { array<int> order=array<int>(); "
			"set<int> values=set<int>(); order.reserve(64); values.reserve(64); "
			"int state=seed; for(int i=0;i<64;i++){state=(state*13+7)%64; "
			"int key=state*17+3; order.push(key); if((i%3)==0) values.add(key);} "
			"int cursor=seed; int total=0; for(int i=0;i<limit;i++){"
			"cursor=(cursor*9+5)%64; if(values.has(order[cursor])) total++;} "
			"return total;} void main(){print(work(1000,17));}",
			"typed array-set indirect loop")
		&& ExpectEquivalent(
			"class Fields { public:int a=0;int b=1;int c=2;int d=3;}; "
			"int work(int limit,int seed){array<int> choices=array<int>();"
			"for(int i=0;i<17;i++)choices.push((i*13+5)&3);Fields value=Fields();"
			"int state=seed;for(int i=0;i<limit;i++){state=(state*13+7)%17;"
			"int position=(state+((i%17)*3))%17;int choice=choices[position];"
			"int delta=((state/5)%7)+1;if(choice==0)value.a+=delta;"
			"else if(choice==1)value.b+=delta;else if(choice==2)value.c+=delta;"
			"else value.d+=delta;}return value.a+value.b+value.c+value.d+state;}"
			"void main(){print(work(1000,7));}",
			"typed indirect stable-field slot loop")
		&& ExpectEquivalent(
			"interface Op{virtual int step(int value)=0;};"
			"class Add1:Op{public:int step(int value){return (value+1)%1000003;}};"
			"class Add2:Op{public:int step(int value){return (value+2)%1000003;}};"
			"class Add3:Op{public:int step(int value){return (value+3)%1000003;}};"
			"class Add4:Op{public:int step(int value){return (value+4)%1000003;}};"
			"int work(int limit,int seed){array<int> choices=array<int>();"
			"for(int i=0;i<17;i++)choices.push((i*7+3)&3);Op a=Add1();Op b=Add2();"
			"Op c=Add3();Op d=Add4();Op current=a;int state=seed;int total=0;"
			"for(int i=0;i<limit;i++){state=(state*13+7)%17;"
			"int position=(state+((i%17)*5))%17;int choice=choices[position];"
			"if(choice==0)current=a;else if(choice==1)current=b;"
			"else if(choice==2)current=c;else current=d;total=current.step(total);}"
			"return total+state;}void main(){print(work(1000,7));}",
			"typed indirect polymorphic receiver chain")
		&& ExpectEquivalent(
			"interface BinaryOp{virtual int step(int value)=0;};"
			"class Plus1:BinaryOp{public:int step(int value){return (value+1)%1000003;}};"
			"class Plus3:BinaryOp{public:int step(int value){return (value+3)%1000003;}};"
			"int work(int limit){array<int> choices={0,1,1,0};BinaryOp a=Plus1();"
			"BinaryOp b=Plus3();BinaryOp current=a;int state=1;int total=0;"
			"for(int i=0;i<limit;i++){state=(state*5+1)%17;"
			"int position=(state+((i%4)*1))%4;int choice=choices[position];"
			"if(choice==0)current=a;else current=b;total=current.step(total);}"
			"return total+state;}void main(){print(work(1000));}",
			"typed indirect two-receiver polymorphic chain")
		&& ExpectEquivalent(
			"interface OverflowOp{virtual int step(int value)=0;}\n"
			"class OverflowPlus1:OverflowOp{public:int step(int value){\n"
			"return (value+1)%1000003;}}\n"
			"class OverflowPlus2:OverflowOp{public:int step(int value){\n"
			"return (value+2)%1000003;}}\n"
			"int overflow_work(){array<int> choices={0,1,1,0};"
			"OverflowOp a=OverflowPlus1();OverflowOp b=OverflowPlus2();"
			"OverflowOp current=a;int state=1;int total=2147483647;"
			"for(int i=0;i<1;i++){state=(state*5+1)%17;"
			"int position=(state+((i%4)*1))%4;int choice=choices[position];"
			"if(choice==0)current=a;else current=b;total=current.step(total);}"
			"return total+state;}void main(){print(overflow_work());}",
			"typed polymorphic transform preserves callee overflow location")
		&& ExpectEquivalent(
			"int work(int limit,int seed){array<string> keys={\"amber\",\"birch\","
			"\"cedar\",\"denim\"};array<string> other={\"amber\",\"birch-x\","
			"\"cedar\",\"denim-x\"};array<int> order={3,0,2,1};"
			"map<string,int> values=map<string,int>();for(int i=0;i<4;i++)"
			"values[keys[i]]=i*7+3;int state=seed;int total=0;"
			"for(int i=0;i<limit;i++){state=(state*13+7)%17;"
			"int position=order[(state+(i%4))%4];"
			"total=(total+values[keys[position]])%1000003;"
			"if(keys[position]==other[position])total=(total+1)%1000003;}"
			"return total+state;}void main(){print(work(1000,7));}",
			"typed indirect borrowed-string map and comparison")
		&& ExpectEquivalent(
			"class PairValue{public:int first=0;int second=0;"
			"int sum(){return first+second;}};"
			"int work(int limit,int seed){int state=seed;int total=0;"
			"for(int i=0;i<limit;i++){state=(state*13+7)%127;"
			"PairValue value=PairValue();value.first=state%31;"
			"value.second=(state/7)%31;total=(total+value.sum())%1000003;}"
			"return total+state;}void main(){print(work(1000,7));}",
			"whole-loop scalar replacement for nullary object projection")
		&& ExpectEquivalent(
			"class Fields { public: int a=0; int b=1; int c=2; int d=3; }; "
			"int work(int limit) { Fields value=Fields(); int state=17; int total=0; "
			"for(int i=0;i<limit;i++){state=(state*13+7)%64; int choice=state%4; "
			"if(choice==0){value.a++;total+=value.a;} "
			"else if(choice==1){value.b++;total+=value.b;} "
			"else if(choice==2){value.c++;total+=value.c;} "
			"else {value.d++;total+=value.d;} total%=1000003;} return total;} "
			"void main(){print(work(1000));}",
			"typed direct-field random loop")
		&& ExpectEquivalent(
			BuildDeepTypedLoopStackSource(),
			"typed loop computed deep operand stack")
		&& ExpectEquivalent(
			"void main() { array<int> values = {0, 0}; int state = 0; int total = 0; "
			"int i = 0; while (i < 1) { state = (state * 1 + 1) % 2; "
			"int delta = (state % 2) + 2147483647; values[state] += delta; "
			"total = (total + values[state]) % 7; i++; } print(total); }",
			"typed random array state-derived delta overflow")
		&& ExpectEquivalent(
			"int work(int limit) { int state = 1; int total = 3; int i = 0; "
			"while (i < limit) { state = (state * 109 + 89) % 256; "
			"total = (total + (state % 31)) % 1000003; i++; } "
			"return total + state; } void main() { print(work(10000)); }",
			"power-of-two scalar recurrence reduction")
		&& ExpectEquivalent(
			"int work() { int total = 3; for (int i = 5; i < 10000; i++) { "
			"total = (total + (i % 97)) % 1000003; } return total; } "
			"void main() { print(work()); }",
			"counted remainder accumulation reduction")
		&& ExpectEquivalent(
			"int work() { int total = 3; for (int i = -5; i < 100; i++) { "
			"total = (total + (i % 7)) % 101; } return total; } "
			"void main() { print(work()); }",
			"counted remainder accumulation signed fallback")
		&& ExpectEquivalent(
			"int work() { int total = 2147483647; for (int i = 0; i < 2; i++) { "
			"total = (total + (i % 7)) % 2147483647; } return total; } "
			"void main() { print(work()); }",
			"counted remainder accumulation overflow fallback")
		&& ExpectEquivalent(
			"int step(int value) { return value + 3; } "
			"int work() { int total = -100; for (int i = 5; i < 10000; i++) { "
			"total = step(total); } return total; } void main() { print(work()); }",
			"counted pure unary call recurrence reduction")
		&& ExpectEquivalent(
			"int step(int value) { return value + 3; } "
			"int work() { int total = 2147483646; for (int i = 0; i < 2; i++) { "
			"total = step(total); } return total; } void main() { print(work()); }",
			"counted inlined local recurrence overflow fallback")
		&& ExpectEquivalent(
			"int gcd_tail(int left,int right){if(right==0)return left;"
			"return gcd_tail(right,left%right);}void main(){"
			"print(gcd_tail(65521,46368));print(gcd_tail(1071,462));}",
			"multi-parameter tail recursion elimination")
		&& ExpectEquivalent(
			"double work(int limit) { double value = -0.25; int i = 0; "
			"while (i < limit) { value = value * 1.00001 + 0.00003; i++; } "
			"return value; } void main() { print(work(10000)); }",
			"runtime-bound floating recurrence")
		&& ExpectEquivalent(
			"double work() { double value = -9000000000000000.0; "
			"for (int i = 0; i < 3; i++) { value += 4000000000000001.0; } "
			"return value; } void main() { "
			"print(work() == 3000000000000003.0); "
			"print(work() - 3000000000000003.0); }",
			"exact integer-valued floating recurrence endpoint")
		&& ExpectEquivalent(
			"int mutate(array<string>& values) { int total = 0; "
			"for (int i = 0; i < 4; i++) { int index = i & 1; "
			"values[index] = values[index == 0 ? 1 : 0]; "
			"if (values[index] == values[0]) total++; } return total; } "
			"void main() { array<string> values = {\"a\", \"b\"}; "
			"print(mutate(ref values)); print(values[0]); print(values[1]); }",
			"parameter string array mutation stays ownership-aware")
		&& ExpectEquivalent(
			"int work() { array<int> values = array<int>(); values.reserve(256); "
			"int i = 0; while (i < 256) { values.push(2); i++; } "
			"int state = 7; int total = 11; i = 0; while (i < 10000) { "
			"state = (state * 109 + 89) % 256; values[state] = values[state] + 3; "
			"total = (total + values[state]) % 1000003; i++; } "
			"return total + values[state]; } void main() { print(work()); }",
			"power-of-two typed array cycle reduction")
		&& ExpectEquivalent(
			"int work() { array<int> values = array<int>(); values.reserve(8); "
			"int i = 0; while (i < 8) { values.push(i == 3 ? -2 : 1); i++; } "
			"int state = 1; int total = 0; i = 0; while (i < 1000) { "
			"state = (state * 5 + 1) % 8; values[state] = values[state] + 1; "
			"total = (total + values[state]) % 10007; i++; } "
			"return total; } void main() { print(work()); }",
			"typed array range-proof fallback")
		&& ExpectEquivalent(
			"int work() { map<int,int> values = map<int,int>(); values.reserve(257); "
			"int i = 0; while (i < 257) { values[i] = 0; i++; } "
			"int state = 1; int total = 0; i = 0; while (i < 10000) { "
			"state = (state * 75 + 74) % 257; values[state] = values[state] + 3; "
			"total = (total + values[state]) % 1000000007; i++; } "
			"return total + values[state]; } void main() { print(work()); }",
			"typed random map loop")
		&& ExpectEquivalent(
			"uint32 work() { array<uint32> values = array<uint32>(); values.reserve(64); "
			"for (int i = 0; i < 64; i++) values.push(to_unsigned(i)); "
			"uint32 state = 362436069u; uint32 total = 0u; "
			"for (int i = 0; i < 1000; i++) { state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); int key = to_int(state & 63u); "
			"values[key] = values[key] + 1u; total += values[key]; } "
			"return total; } void main() { print(work()); }",
			"typed uint32 array random loop")
		&& ExpectEquivalent(
			"float work() { array<float> values = array<float>(); values.reserve(64); "
			"for (int i = 0; i < 64; i++) values.push(to_float(i) * 0.25f); "
			"uint32 state = 2463534242u; float total = 0.0f; "
			"for (int i = 0; i < 1000; i++) { state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); int key = to_int(state & 63u); "
			"values[key] = values[key] * 1.0001f + 0.125f; total += values[key]; } "
			"return total; } void main() { print(work()); }",
			"typed float array random loop")
		&& ExpectEquivalent(
			"uint32 work() { map<int,uint32> values = map<int,uint32>(); values.reserve(64); "
			"for (int i = 0; i < 64; i++) values[i] = to_unsigned(i); "
			"uint32 state = 521288629u; uint32 total = 0u; "
			"for (int i = 0; i < 1000; i++) { state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); int key = to_int(state & 63u); "
			"values[key] = values[key] + 1u; total += values[key]; } "
			"return total; } void main() { print(work()); }",
			"typed int to uint32 map random loop")
		&& ExpectEquivalent(
			"float work() { map<int,float> values = map<int,float>(); values.reserve(64); "
			"for (int i = 0; i < 64; i++) values[i] = to_float(i) * 0.25f; "
			"uint32 state = 123456789u; float total = 0.0f; "
			"for (int i = 0; i < 1000; i++) { state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); "
			"int key = to_int(state & 63u); values[key] = values[key] + 0.125f; "
			"total += values[key]; } return total; } void main() { print(work()); }",
			"typed int to float map random loop")
		&& ExpectEquivalent(
			"int work() { map<uint32,int> values = map<uint32,int>(); values.reserve(64); "
			"for (int i = 0; i < 64; i++) values[to_unsigned(i)] = i; "
			"uint32 state = 88675123u; int total = 0; "
			"for (int i = 0; i < 1000; i++) { state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); uint32 key = state & 63u; "
			"values[key] = values[key] + 1; total += values[key]; } "
			"return total; } void main() { print(work()); }",
			"typed uint32 to int map random loop")
		&& ExpectEquivalent(
			"int work() { set<uint32> values = set<uint32>(); values.reserve(64); "
			"for (int i = 0; i < 64; i += 2) values.add(to_unsigned(i)); "
			"uint32 state = 123456789u; int total = 0; "
			"for (int i = 0; i < 1000; i++) { state = state ^ (state << 13u); "
			"state = state ^ (state >> 17u); state = state ^ (state << 5u); "
			"if (values.has(state & 63u)) total++; } "
			"return total; } void main() { print(work()); }",
			"typed uint32 set random loop")
		&& ExpectEquivalent(
			"int work() { map<int,int> values = map<int,int>(); values[1] = 120; "
			"values[1] = values[1] + 3; values[1] = values[1] - 23; "
			"values[1] = values[1] * 3; values[1] = values[1] / 4; "
			"values[1] = values[1] % 16; values[1] = values[1] & 7; "
			"values[1] = values[1] | 8; values[1] = values[1] ^ 2; "
			"values[1] = values[1] << 2; values[1] = values[1] >> 1; "
			"return values[1]; } void main() { print(work()); }",
			"typed int map raw compound operations")
		&& ExpectEquivalent(
			"void main() { map<int,int> values = map<int,int>(); "
			"values[0] = 2147483647; values[0] = values[0] + 1; }",
			"typed int map raw update overflow diagnostic")
		&& ExpectEquivalent(
			"void main() { map<int,int> values = map<int,int>(); "
			"values[0] = 1; values[0] = values[0] / 0; }",
			"typed int map raw update division diagnostic")
		&& ExpectEquivalent(
			"void main() { map<int,int> values = map<int,int>(); "
			"values[0] = 1; values[0] = values[0] << 32; }",
			"typed int map raw update shift diagnostic")
		&& ExpectEquivalent(
			"void main() { map<int,uint32> values = map<int,uint32>(); "
			"values[0] = 4294967295u; values[0] = values[0] + 1u; "
			"print(values[0]); }",
			"typed int uint map raw wrapping update")
		&& ExpectEquivalent(
			"void main() { map<uint32,int> values = map<uint32,int>(); "
			"values[0u] = 2147483647; values[0u] = values[0u] + 1; }",
			"typed uint int map raw overflow diagnostic")
		&& ExpectEquivalent(
			"void main() { map<int,double> values = map<int,double>(); "
			"values[0] = 3.5; values[0] = values[0] * 2.0; "
			"values[0] = values[0] / 4.0; print(values[0]); }",
			"typed int double map raw arithmetic")
		&& ExpectEquivalent(
			"void main() { map<uint32,double> values = map<uint32,double>(); "
			"values[0u] = 1.0; values[0u] = values[0u] / 0.0; }",
			"typed uint double map raw division diagnostic")
		&& ExpectEquivalent(
			"void main() { map<uint32,uint32> values = map<uint32,uint32>(); "
			"values[0u] = 1u; values[0u] = values[0u] / 0u; }",
			"typed uint map raw division diagnostic")
		&& ExpectEquivalent(
			"void main() { map<int,int> values = map<int,int>(); values[0] = 1; "
			"try { values[0] = values[0] / 0; } catch (...) { print(\"caught\"); } }",
			"typed map raw division caught in current function")
		&& ExpectEquivalent(
			"void main() { map<int,double> values = map<int,double>(); "
			"try { values[7] = values[7] + 1.0; } catch (...) { print(\"missing\"); } }",
			"typed map missing key caught in current function")
		&& ExpectEquivalent(
			"void main() { array<int> keys = {0}; map<int,int> values = map<int,int>(); "
			"values[0] = 1; int index = 1; "
			"values[keys[index]] = values[keys[index]] + 1; }",
			"typed map nested-key bounds diagnostic")
		&& ExpectEquivalent(
			"double work() { map<int,double> values = map<int,double>(); int state = 1; "
			"int i = 0; while (i < 1) { state = (state * 3 + 1) % 7; "
			"values[state] = values[state] + 0.5; i++; } return 0.0; } "
			"void main() { print(work()); }",
			"typed affine f64 map update missing-key diagnostic")
		&& ExpectEquivalent(
			"double work() { map<int,double> values = map<int,double>(); "
			"values[0] = 1.0; int state = 2147483647; int i = 0; "
			"while (i < 1) { state = (state * 2 + 0) % 1; "
			"values[state] = values[state] + 0.5; i++; } return values[0]; } "
			"void main() { print(work()); }",
			"typed affine f64 map state-overflow diagnostic")
		&& ExpectEquivalent(
			"int work(int limit, int seed) { int size=127; "
			"map<int,double> values=map<int,double>(); values.reserve(size); "
			"for(int i=0;i<size;i++) values[i]=(i%31)*0.25+1.0; "
			"int state=seed; double total=0.0; for(int i=0;i<limit;i++) { "
			"state=(state*75+74)%65521; int key=(state+i*13)%size; "
			"double value=values[key]*1.000001+(state%7)*0.25; "
			"if(value>10000.0) value*=0.5; values[key]=value; total+=value; } "
			"return to_int(total)+state; } void main(){print(work(1000,173));}",
			"typed conditional f64 map fused loop")
		&& ExpectEquivalent(
			"int work() { int size=1; map<int,double> values=map<int,double>(); "
			"values.reserve(size); for(int i=0;i<size;i++) values[i]=1.0; "
			"int state=2147483647; double total=0.0; for(int i=0;i<1;i++) { "
			"state=(state*75+74)%65521; int key=(state+i*13)%size; "
			"double value=values[key]*1.000001+(state%7)*0.25; "
			"if(value>10000.0) value*=0.5; values[key]=value; total+=value; } "
			"return to_int(total)+state; } void main(){print(work());}",
			"typed conditional f64 map overflow diagnostic")
		&& ExpectEquivalent(
			"int work() { map<int,int> values = map<int,int>(); "
			"values[0] = 2147483647; int state = 0; int i = 0; "
			"while (i < 1) { state = (state * 1 + 0) % 1; "
			"values[state] = values[state] + 1; i++; } return values[0]; } "
			"void main() { print(work()); }",
			"typed affine i32 map value-overflow diagnostic")
		&& ExpectEquivalent(
			"int work() { set<string> values = {\"alpha\", \"beta\", \"gamma\"}; "
			"array<string> keys = {\"alpha\", \"beta\", \"gamma\", \"missing\"}; "
			"int state = 1; int total = 0; int i = 0; while (i < 10000) { "
			"state = (state * 75 + 74) % 257; "
			"if (values.has(keys[state % 4])) { total++; } i++; } return total; } "
			"void main() { print(work()); }",
			"typed string set loop")
		&& ExpectEquivalent(
			"void main() { array<string> parts = {\"alpha\", \"beta\"}; "
			"print(\"{}:{}\", parts[0], parts[1]); }",
			"compact string array formatting and join")
		&& ExpectEquivalent(
			"int work(int limit) { array<string> left = {\"a\", \"b\", \"c\", \"d\"}; "
			"array<string> right = {\"a\", \"x\", \"c\", \"y\"}; "
			"int state = 1; int total = 0; int i = 0; while (i < limit) { "
			"state = (state * 75 + 74) % 257; int index = state % 4; "
			"if (left[index] == right[index]) total++; i++; } return total; } "
			"void main() { print(work(10000)); }",
			"borrowed compact string array comparison")
		&& ExpectEquivalent(
			"int work(int limit) { array<string> left={\"aa\",\"bb\",\"cc\"}; "
			"array<string> right={\"aa\",\"bx\",\"cc\"}; int state=1; int total=0; "
			"for(int i=0;i<limit;i++){state=(state*75+74)%257; int a=state%3; "
			"int b=(state+(i&1))%3; if(left[a]==right[b]) total++;} "
			"return total+state;} void main(){print(work(10000));}",
			"dual borrowed string array hash comparison")
		&& ExpectEquivalent(
			"void main(){array<string> left={\"same\"}; array<string> right={\"same\"}; "
			"int state=0; int total=2147483647; for(int i=0;i<1;i++){"
			"state=(state*1+0)%1; int a=state%1; int b=(state+(i&0))%1; "
			"if(left[a]==right[b]) total++;} print(total);}",
			"dual string array comparison overflow diagnostic")
		&& ExpectEquivalent(
			"void main(){array<string> left={\"a\"}; array<string> right={\"a\"}; "
			"int state=0; int total=0; for(int i=0;i<1;i++){"
			"state=(state*1+1)%2; int a=state%2; int b=(state+(i&0))%1; "
			"if(left[a]==right[b]) total++;} print(total);}",
			"dual string array comparison bounds diagnostic")
		&& ExpectEquivalent(
			"int work() { array<string> values = {\"a\", \"b\"}; int total = 0; "
			"for (int i = 0; i < 8; i++) { int index = i & 1; "
			"values[index] = index == 0 ? \"x\" : \"y\"; "
			"if (values[index] == \"x\") total++; } return total; } "
			"void main() { print(work()); }",
			"mutable string array remains on ownership-aware fallback")
		&& ExpectEquivalent(
			"int work() { array<string> left = {\"a\", \"b\", \"c\", \"d\", \"e\"}; "
			"array<string> right = {\"a\", \"x\", \"c\", \"y\", \"e\"}; "
			"int state = 1; int total = 0; int index = 5; int i = 0; "
			"while (i < index) { state = (state * 5 + 1) % 16; "
			"index = state % 5; if (left[index] == right[index]) total++; i++; } "
			"return total + i + index; } void main() { print(work()); }",
			"string array loop rejects mutable-limit alias")
		&& ExpectEquivalent(
			"int work() { set<int> values = set<int>(); values.reserve(129); "
			"int i = 0; while (i < 257) { if ((i % 3) == 0) values.add(i); i++; } "
			"int state = 1; int total = 0; i = 0; while (i < 10000) { "
			"state = (state * 75 + 74) % 257; if (values.has(state)) { total++; } "
			"i++; } return total; } void main() { print(work()); }",
			"typed integral set loop")
		&& ExpectEquivalent(
			"int work() { set<int> values = set<int>(); values.reserve(128); "
			"int i = -128; while (i < 256) { if ((i % 5) == -2) values.add(i); i++; } "
			"int state = 1; int total = 0; i = 0; while (i < 10000) { "
			"state = (state * 109 + 89) % 256; if (values.has(state)) total++; i++; } "
			"return total; } void main() { print(work()); }",
			"filtered set construction and power-of-two query")
		&& ExpectEquivalent(
			"void main() { array<int> values = {0}; int state = 0; int total = 0; "
			"int i = 0; while (i < 2) { state = (state * 1 + 1) % 2; "
			"values[state] = values[state] + 1; "
			"total = (total + values[state]) % 7; i++; } print(total); }",
			"typed random array bounds diagnostic")
		&& ExpectEquivalent(
			"void main() { map<int,int> values = map<int,int>(); values[0] = 0; "
			"int state = 0; int total = 0; int i = 0; while (i < 2) { "
			"state = (state * 1 + 1) % 2; values[state] = values[state] + 1; "
			"total = (total + values[state]) % 7; i++; } print(total); }",
			"typed random map missing-key diagnostic")
		&& ExpectEquivalent(
			"void main() { array<int> values = {2147483647}; int state = 0; "
			"int total = 0; int i = 0; while (i < 1) { state = (state * 1 + 0) % 1; "
			"values[state] = values[state] + 1; "
			"total = (total + values[state]) % 7; i++; } print(total); }",
			"typed random array element-overflow diagnostic")
		&& ExpectEquivalent(
			"void main() { set<int> values = {0}; int state = 0; int total = 2147483647; "
			"int i = 0; while (i < 1) { state = (state * 1 + 0) % 1; "
			"if (values.has(state)) { total++; } i++; } print(total); }",
			"typed integral set accumulator-overflow diagnostic")
		&& ExpectEquivalent(
			"int work() { set<int> values = {0}; int state = 1073741824; "
			"int total = 0; int i = 0; while (i < 2) { state = (state * 2 + 1) % 7; "
			"if (values.has(state)) total++; i++; } return total; }"
			"void main(){print(73);print(work());}",
			"typed integral set state overflow preserves diagnostic and prior output", true, 0, false)
		&& ExpectEquivalent(
			"void main() { set<int> values = set<int>(); int state = -1; "
			"int total = 0; int i = 0; while (i < 71) { state = (state * 7 - 3) % 257; "
			"if (values.has(state)) total++; i++; } print(total); print(state); }",
			"typed integral set empty storage and negative state")
		&& ExpectEquivalent(
			"int work() { set<int> values = {0}; int state = 1073741823; "
			"int total = 0; int i = 0; while (i < 2) { state = (state * 2 + 2) % 7; "
			"if (values.has(state)) total++; i++; } return total; }"
			"void main(){print(74);print(work());}",
			"affine set state-add overflow preserves prior state and output", true, 0, false)
		&& ExpectEquivalent(
			"int work() { set<int> values = {0}; int state = 1; "
			"int total = 0; int i = 0; while (i < 2) { state = (state * 2 + 1) % 0; "
			"if (values.has(state)) total++; i++; } return total; }"
			"void main(){print(75);print(work());}",
			"affine set zero modulus diagnostic", true, 0, false)
		&& ExpectEquivalent(
			"int work() { set<int> values = {0}; int state = -1073741824; "
			"int total = 0; int i = 0; while (i < 2) { state = (state * 2 + 0) % 257; "
			"if (values.has(state)) total++; i++; } return total; }"
			"void main(){print(76);print(work());}",
			"affine set negative-constant bytecode modulo overflow diagnostic", true, -1, false)
		&& ExpectEquivalent(
			"int work() { set<int> values = {3}; int state = 1; "
			"int total = 2147483647; int i = 0; while (i < 2) { state = (state * 2 + 1) % 5; "
			"if (values.has(state)) total++; i++; } return total; }"
			"void main(){print(77);print(work());}",
			"affine set total overflow after updated state", true, 0, false)
		&& ExpectEquivalent(
			"void show(int total,int state,int i){print(total);print(state);print(i);}"
			"int work() { set<int> values = {-1, -3, -5}; int state = -1; "
			"int total = 0; int i = 0; while (i < 93) { state = (state * 7 + 3) % 257; "
			"if (values.has(state)) total++; i++; } show(total,state,i);return 0; }"
			"void main(){work();}",
			"affine set negative dividend and bytecode divisor preserve final state", true, -257, true)
		&& ExpectEquivalent(
			"void show(int total,int state,int i){print(total);print(state);print(i);}"
			"int work() { set<int> values = set<int>(); int state = -1; "
			"int total = 0; int i = 0; while (i < 71) { state = (state * 7 + 3) % 257; "
			"if (values.has(state)) total++; i++; } show(total,state,i);return 0; }"
			"void main(){work();}",
			"affine set empty storage preserves state recurrence", true, 0, true)
		&& ExpectEquivalent(
			"void main() { set<int> values = {1}; int state = 2147483647; "
			"int total = 2147483647; int i = 0; while (i < 0) { state = (state * 2 + 1) % 0; "
			"if (values.has(state)) total++; i++; } print(total); print(state); print(i); }",
			"affine set zero iterations do not evaluate invalid arithmetic")
		&& ExpectEquivalent(
			"void show(int total,int state,int i){print(total);print(state);print(i);}"
			"int zero(int limit) { set<int> values = {1}; int state = 2147483647; "
			"int total = 2147483647; int i = 0; while (i < limit) { state = (state * 2 + 1) % 0; "
			"if (values.has(state)) total++; i++; } show(total,state,i);return 0; }"
			"void main(){zero(0);}",
			"affine set runtime zero bound retains a plan without arithmetic", true, 0, true)
		&& ExpectEquivalent(
			"void main() { set<string> values = {\"present\"}; "
			"array<string> keys = {\"present\"}; int state = 0; int total = 0; "
			"int i = 0; while (i < 1) { state = (state * 1 + 1) % 2; "
			"if (values.has(keys[state % 2])) { total++; } i++; } print(total); }",
			"typed string set key-bounds diagnostic")
		&& ExpectEquivalent(
			"int work() { set<string> values={\"present\"}; "
			"string prefix=\"pre\"; string suffix=\"sent\"; "
			"return values.has(prefix + suffix) ? 1 : 0; } "
			"void main(){print(work());}",
			"typed string set preserves generic expression fallback")
		&& ExpectEquivalent(
			"int work() { array<int64> values = {1i64, 2i64}; int i = 0; "
			"int64 total = 0i64; while (i < 1) { i++; total += values[i]; } "
			"return to_int(total); } void main() { print(work()); }",
			"unsupported typed-loop storage declines before source mutation")
		&& ExpectEquivalent(
			"class Fields { public: int a=2147483647; int b=0; }; "
			"int work(){Fields value=Fields();int state=0;for(int i=0;i<1;i++){"
			"state=(state*1+0)%2;int delta=(state%1)+1;int choice=state&1;"
			"if(choice==0)value.a+=delta;else value.b+=delta;}return value.a;} "
			"void main(){print(work());}",
			"state-driven field loop exact overflow diagnostic")
		&& ExpectEquivalent(
			"interface Operation { virtual int step(int value)=0; }; "
			"class Add1:Operation { public:int step(int value){return value+1;} }; "
			"class Add2:Operation { public:int step(int value){return value+2;} }; "
			"class Add3:Operation { public:int step(int value){return value+3;} }; "
			"class Add4:Operation { public:int step(int value){return value+4;} }; "
			"int work(){Operation a=Add1();Operation b=Add2();Operation c=Add3();"
			"Operation d=Add4();Operation current=a;"
			"int state=0;int total=2147483647;for(int i=0;i<1;i++){"
			"state=(state*1+0)%4;int choice=state&3;"
			"if(choice==0)current=a;else if(choice==1)current=b;"
			"else if(choice==2)current=c;else current=d;total=current.step(total);}"
			"return total;}void main(){print(work());}",
			"state-driven polymorphic transform exact overflow diagnostic")
		&& ExpectEquivalent(
			"interface F32Operation { virtual float add(float left,float right)=0; }; "
			"class F32Add:F32Operation { public:float add(float left,float right)"
			"{return left+right;} }; float work(F32Operation operation){float total=0.f;"
			"int committed=0;for(int i=0;i<128;i++){committed++;"
			"total=operation.add(total,1.f);}return total+committed;}"
			"void main(){F32Add operation=F32Add();print(to_int(work(operation)));}",
			"multi-argument f32 virtual call declines before source mutation")
		&& ExpectEquivalent(
			"int work(){int x=7;ref int alias=x;"
			"assert(std::memory::address_of(&x)==std::memory::address_of(&alias));"
			"auto b=new std::memory::buffer(8);std::memory::write<uint64>(b,0,42u64);"
			"auto v=b.readonly_slice(0,8);return to_int(std::memory::read<uint64>(v,0));}"
			"void main(){print(1);print(work());}",
			"memory standard calls and actual reference address fallback")
		&& ExpectEquivalent(
			"void fail(){auto b=new std::memory::buffer(1);print(99);b.get(2);}"
			"void main(){auto f=fail;f();}",
			"indirect memory failure preserves effects without replay")
		&& ExpectEquivalent(
			"class Item {}; class Derived : public Item {}; "
			"void check(array<Item> items, Item target) { array<Item> next=array<Item>();"
			"bool found=false;for(int64 i=0;i<len(items);i++){Item value=items[i];"
			"if(value==target){found=true;}else{next.push(value);}}"
			"assert(found);assert(len(next)==3);}"
			"void main(){Derived target=Derived();array<Item> items={Derived(),Derived(),Derived(),target};"
			"check(items,target);print(1);}",
			"loop-local object comparison is evaluated again after each assignment")
		&& ExpectEquivalent(
			"void main(){int limit=4;int value=0;int hits=0;for(int i=0;i<4;i++){"
			"value++;if(value==limit){hits++;}}assert(hits==1);print(hits);}",
			"incremented locals do not enter invariant comparison cache")
		&& ExpectEquivalent(
			"void main(){int total=0;for(int outer=0;outer<4;outer++){int value=outer;"
			"int target=3;for(int inner=0;inner<2;inner++){if(value==target){total++;}}}"
			"assert(total==2);print(total);}",
			"comparison cache considers assignments in an enclosing loop")
		&& ExpectEquivalent(
			"struct Box{public:int value=0;};int work(){Box left=Box();Box right=Box();"
			"int hits=0;for(int i=0;i<3;i++){if(left==right){hits++;}left.value++;}return hits;}"
			"void main(){assert(work()==1);print(work());}",
			"struct field mutation invalidates value equality without replacing locals")
		&& ExpectEquivalent(
			"struct Item{public:int value=0;};struct Box{public:Item item=Item();};"
			"int work(){Box left=Box();Box right=Box();int hits=0;for(int i=0;i<3;i++){"
			"if(left==right){hits++;}left.item.value++;}return hits;}"
			"void main(){assert(work()==1);print(work());}",
			"nested struct comparison keeps observing field changes")
		&& ExpectEquivalent(R"vs(
class QueuedValue<T>
{
public:
	static int value = 7;
	int copy = QueuedValue<T>::value;
	int callCopy = QueuedValue<T>::read(1);
	static int read(int add = 0) { return QueuedValue<T>::value + add; }
	struct Inner
	{
		static int seed = 3;
		int local = Inner::seed;
		int current = QueuedValue<T>::Inner::seed;
		int fixed = QueuedValue<int>::Inner::seed;
	};
};
class QueuedDerived<T> : public QueuedValue<T> {};
class QueuedState
{
public:
	QueuedDerived<int> early = QueuedDerived<int>();
	static int result = QueuedDerived<int>::read(3);
};
int main()
{
	assert(QueuedState::result == 10);
	assert(QueuedValue<int>::value == 7);
	QueuedState state = QueuedState();
	assert(state.early.copy == 7);
	assert(state.early.callCopy == 8);
	QueuedValue<int>::value = 12;
	QueuedValue<int> later = QueuedValue<int>();
	assert(later.copy == 12);
	assert(later.callCopy == 13);
	assert(state.early.copy == 7);
	QueuedValue<int>::Inner::seed = 5;
	QueuedValue<string>::Inner::seed = 9;
	QueuedValue<string>::Inner nested = QueuedValue<string>::Inner();
	assert(nested.local == 9 && nested.current == 9 && nested.fixed == 5);
	print(QueuedState::result);
	return 0;
}
)vs", "queued generic static method initialization preserves arguments across execution backends", false, 0, true)
		&& ExpectEquivalent(R"vs(
class NestedValue<T>
{
public:
	static int seed = 7;
	int copy = NestedValue<T>::seed;
};
class OrdinaryWrapper
{
public:
	NestedValue<int> item = NestedValue<int>();
	int copy = NestedValue<int>::seed;
};
class GenericWrapper<T>
{
public:
	NestedValue<T> item = NestedValue<T>();
};
class StaticWrapperState
{
public:
	GenericWrapper<int> early = GenericWrapper<int>();
	static OrdinaryWrapper ordinary = OrdinaryWrapper();
	static GenericWrapper<int> generic = GenericWrapper<int>();
	static OrdinaryWrapper second = OrdinaryWrapper();
};
int main()
{
	assert(StaticWrapperState::ordinary.item.copy == 7);
	assert(StaticWrapperState::ordinary.copy == 7);
	assert(StaticWrapperState::generic.item.copy == 7);
	assert(StaticWrapperState::second.item.copy == 7);
	NestedValue<int>::seed = 11;
	OrdinaryWrapper later = OrdinaryWrapper();
	GenericWrapper<int> genericLater = GenericWrapper<int>();
	assert(later.item.copy == 11 && later.copy == 11);
	assert(genericLater.item.copy == 11 && NestedValue<int>::seed == 11);
	assert(StaticWrapperState::ordinary.item.copy == 7);
	assert(StaticWrapperState::generic.item.copy == 7);
	print(NestedValue<int>::seed);
	return 0;
}
)vs", "nested static construction initializes queued dependencies once across execution backends", false, 0, true)
		&& ExpectEquivalent(
			"class Order{public:static int trace=0;static int observed=-1;};"
			"int mark(int value){Order::trace=Order::trace*10+value;return value;}"
			"class Needed<T>{public:static int seed=mark(1)+6;};"
			"struct Item{~Item(){Order::observed=Needed<int>::seed;mark(2);}};"
			"class Wrapper{public:Item values[1];};"
			"int work(){Wrapper value=Wrapper();return 0;}"
			"class State{public:static int value=work();static int later=mark(3);};"
			"void main(){assert(Order::observed==7 && Order::trace==123);"
			"print(Order::observed);print(Order::trace);}",
			"local wrapper array destruction prepares queued element dependencies", false, 0, true)
		&& ExpectEquivalent(
			"using Callback=fn()->int;class Needed<T>{public:static int seed=7;};"
			"int zero(){return 0;}int firstNeeded(){return Needed<int>::seed;}"
			"int secondNeeded(){return Needed<bool>::seed;}"
			"class Config{public:static Callback read=zero;};"
			"Callback current(){return Config::read;}"
			"void selectFirst(){Config::read=firstNeeded;}void selectZero(){Config::read=zero;}"
			"void selectSecond(Callback& callback){callback=secondNeeded;}"
			"int returnedWork(){auto before=current();selectFirst();selectZero();selectFirst();"
			"auto read=current();return read();}"
			"int refWork(){Config::read=zero;auto before=Config::read;selectSecond(ref Config::read);"
			"auto read=Config::read;return read();}"
			"class State{public:static int first=returnedWork();static int second=refWork();};"
			"void main(){assert(State::first==7 && State::second==7);"
			"print(State::first);print(State::second);}",
			"callback writes invalidate returned and referenced static initializer provenance", false, 0, true)
		&& ExpectEquivalent(
			"void main(){int left[1]={0};int right[1]={0};int hits=0;for(int i=0;i<3;i++){"
			"if(left==right){hits++;}left[0]++;}assert(hits==1);print(hits);}",
			"native array equality keeps observing element changes")
		? EXIT_SUCCESS
		: EXIT_FAILURE;
}
