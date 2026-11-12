// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptJitNative.h"
#include "VeroScriptJitObjectViews.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace VeroScript::AsmJit
{
	class FExecutionContext;
}

namespace VeroScript::AsmJit::JitLinear
{
	enum class ETypedLoopLocalKind : std::uint8_t
	{
		Unsupported = 0,
		I32,
		U32,
		F32,
		F64,
		Bool,
		String,
		WString,
		ArrayI32,
		ArrayU32,
		ArrayF32,
		ArrayF64,
		ArrayString,
		ArrayWString,
		MapI32I32,
		MapI32U32,
		MapI32F32,
		MapI32F64,
		MapU32I32,
		MapU32U32,
		MapU32F32,
		MapU32F64,
		MapStringI32,
		MapWStringI32,
		SetI32,
		SetU32,
		SetString,
		SetWString,
		ArrayObject,
		Object,
		I64,
		U64,
	};

	enum class ETypedLoopExecution : std::uint8_t
	{
		NotHandled = 0,
		Completed,
		Failed,
	};

	struct FTypedLoopNativeBinding
	{
		ETypedLoopLocalKind Kind = ETypedLoopLocalKind::Unsupported;
		void* Storage = nullptr;
		// Optional raw view established once at the loop boundary. The owning
		// container remains rooted by Storage/FValue for the complete invocation.
		void* NativeData = nullptr;
		// Containers store their raw length.
		std::size_t NativeSize = 0;
		// Invocation-local alias chain for bindings that root the same resizable
		// storage. Built once during typed-loop preflight; never retained by the VM.
		FTypedLoopNativeBinding* AliasRoot = nullptr;
		FTypedLoopNativeBinding* NextAlias = nullptr;
		// Authoritative rooted value for frame object bindings. Array elements use
		// separate compact views, so their scratch grows by one type stamp per element.
		FValue* SourceValue = nullptr;
	};
	static_assert(alignof(FTypedLoopNativeBinding) >= 2);

	/**
	 * Immutable invocation-local view used by native typed loops.  The owning
	 * std::string remains rooted by the frame/container binding.  Precomputing the
	 * hash once at loop entry lets repeated string Map/Set probes avoid hashing the
	 * same borrowed key on every iteration without changing script string identity.
	 */
	struct FTypedLoopNativeStringView
	{
		const std::string* Value = nullptr;
		const char* Data = nullptr;
		std::size_t Size = 0;
		std::size_t Hash = 0;
	};

	struct FTypedLoopInstruction;

	// Conversion results live only for this invocation. A local assignment copies
	// into its authoritative frame string before publishing that local's own view.
	struct FTypedLoopNativeStringScratch
	{
		std::string* Temporaries = nullptr;
		FTypedLoopNativeStringView* TemporaryViews = nullptr;
		FTypedLoopNativeStringView* LocalViews = nullptr;
		std::size_t TemporaryCount = 0;
		std::size_t LocalCount = 0;
		const ETypedLoopLocalKind* ProducerKinds = nullptr;
		const FTypedLoopInstruction* Instructions = nullptr;
		std::size_t InstructionCount = 0;
		// New mutable text plans keep hashes lazy. Existing immutable views keep
		// their precomputed hashes and need neither bitmap nor an extra view word.
		std::uint8_t* LocalHashValid = nullptr;
		std::uint8_t* TemporaryHashValid = nullptr;
	};

	struct FTypedLoopNativePolymorphicTarget
	{
		FScriptObject* Object = nullptr;
		void* NativeCode = nullptr;
		std::uint16_t FunctionIndex = 0xFFFFu;
		const std::vector<std::uint16_t>* FieldNameIndices = nullptr;
		const std::string* TypeIdentity = nullptr;
		std::uint16_t TypeIndex = 0xFFFFu;
		std::array<std::uint16_t, 32> FieldIndices{};
	};

	struct FTypedLoopNativePolymorphicMethodBinding
	{
		FTypedLoopNativePolymorphicTarget* Candidates = nullptr;
		std::size_t CandidateCount = 0;
		FTypedLoopNativePolymorphicTarget* Cache = nullptr;
		std::size_t CacheCount = 0;
		std::size_t CacheCapacity = 0;
		std::uint16_t MethodNameIndex = 0xFFFFu;
		std::size_t CallCount = 0;
	};

	struct FTypedLoopNativeFieldBinding
	{
		enum class EKind : std::uint8_t
		{
			Unsupported = 0,
			I32,
			U32,
			F32,
			F64
		};
		FScriptObject* Object = nullptr;
		std::uint16_t FieldIndex = 0xFFFFu;
		void* Storage = nullptr;
		EKind Kind = EKind::Unsupported;
	};

	struct FTypedLoopNativeMethodBinding
	{
		// A verified straight-line method may touch several scalar fields. Its
		// storage is borrowed only for this loop invocation, after all guards pass.
		struct FFieldState
		{
			std::int32_t** Fields = nullptr;
			std::size_t MutationCount = 0;
		} FieldState;
		void* FieldCode = nullptr;
		std::uint8_t ArgumentCount = 1;
		std::uint16_t FunctionIndex = 0xFFFFu;
		std::uint16_t ReceiverSlot = 0xFFFFu;
		FScriptObject* Object = nullptr;
		std::uint16_t FieldIndex = 0xFFFFu;
		std::int32_t* Storage = nullptr;
		std::int32_t Addend = 0;
		std::int32_t ArgumentModulus = 0;
		std::int32_t ResultModulus = 0;
		std::int32_t ArgumentMagic = 0;
		std::int32_t ResultMagic = 0;
		std::uint8_t ArgumentShift = 0;
		std::uint8_t ResultShift = 0;
		std::size_t CallCount = 0;
	};

	/**
	 * Per-entry invocation data for a generated typed-loop kernel.  The kernel
	 * operates on raw scalar payloads and stable container/field bindings; FValue
	 * conversion is performed once at the loop boundary, never per instruction.
	 */
	struct FTypedLoopInstruction;

	struct FTypedLoopNativeContext
	{
		FExecutionContext* Vm = nullptr;
		FTypedLoopNativeBinding* Bindings = nullptr;
		// Runtime-validated scalar kind for each typed instruction.  Generated
		// kernels are shared by all invocations of a bytecode function, while the
		// concrete I32/U32 identity of compiler temporaries is known only after the
		// loop-entry preflight has inspected the current frame.  Keeping this table
		// invocation-local lets one machine-code CFG preserve signed overflow,
		// unsigned wrapping, logical shifts and checked conversions without
		// reintroducing FValue dispatch inside the loop.
		const ETypedLoopLocalKind* InstructionKinds = nullptr;
		std::uint64_t* Locals = nullptr;
		std::uint64_t* Stack = nullptr;
		// Wide plans exclude field operations. Keep their instruction view in the
		// same slot so ordinary kernels retain the original context footprint.
		union
		{
			FTypedLoopNativeFieldBinding* Fields = nullptr;
			const FTypedLoopInstruction* Instructions;
		};
		FTypedLoopNativeMethodBinding* Methods = nullptr;
		FTypedLoopNativePolymorphicMethodBinding* PolymorphicMethods = nullptr;
		const FTypedLoopNativeObjectViewRange* ObjectViewRanges = nullptr;
		std::size_t ObjectViewRangeCount = 0;
		FTypedLoopNativeStringScratch* Strings = nullptr;
		FScriptObject* DirectFieldMutationObject = nullptr;
		std::uint64_t Scratch = 0;
		std::uint32_t FailureOffset = InvalidJitInstructionOffset;
		const char* FailureMessage = nullptr;
		std::size_t ArrayGets = 0;
		std::size_t ArraySets = 0;
		std::size_t ArrayPushes = 0;
		std::size_t MapGets = 0;
		std::size_t MapSets = 0;
	};

	// Native typed-loop string values are borrowed FTypedLoopNativeStringView
	// pointers tagged in the top bit.
	// Generated kernels are currently emitted only for x64, where user-space object
	// addresses are canonical low-half addresses. The tag never escapes this private
	// invocation ABI and is removed before a C++ string is observed.
	inline constexpr std::uint64_t TypedLoopNativeStringTag = UINT64_C(1) << 63u;
	inline constexpr std::uint64_t TypedLoopNativePayloadMask =
		~TypedLoopNativeStringTag;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeToString(
		FTypedLoopNativeContext* Context, std::uint32_t InstructionIndex,
		std::uint64_t Argument, std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeStoreStringLocal(
		FTypedLoopNativeContext* Context, std::uint32_t Slot,
		std::uint64_t SourceToken, std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeGetIndexI32(
		FTypedLoopNativeContext* Context,
		std::uint64_t ContainerToken,
		std::uint64_t IndexToken,
		std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeSetIndexI32(
		FTypedLoopNativeContext* Context,
		std::uint64_t ContainerToken,
		std::uint64_t IndexToken,
		std::int32_t Value,
		std::uint32_t InstructionOffset) noexcept;

	// Read-only query entry: native plans already know the operation is `has`.
	#if defined(_MSC_VER) && defined(_M_X64)
	__declspec(code_seg(".vsquery"))
	#endif
	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeContainsI32(
		FTypedLoopNativeContext* Context,
		std::uint64_t ContainerToken,
		std::uint64_t ArgumentToken,
		std::uint32_t InstructionOffset) noexcept;

	#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
	std::uint64_t TestNativeContainsCallCount() noexcept;
	#endif

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeContainerMethodI32(
		FTypedLoopNativeContext* Context,
		std::uint64_t ContainerToken,
		std::uint64_t ArgumentToken,
		std::uint32_t Operation,
		std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeCompareI32OrString(
		FTypedLoopNativeContext* Context,
		std::uint64_t LeftToken,
		std::uint64_t RightToken,
		std::uint32_t Operation,
		std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeInvokeI32FieldMethod(
		FTypedLoopNativeContext* Context,
		std::uint32_t InstructionIndex,
		std::int32_t Argument,
		std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeInvokeI32FieldMethodArguments(
		FTypedLoopNativeContext* Context, std::uint32_t InstructionIndex,
		const std::uint64_t* Arguments, std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeReportI32FieldMethodFailure(
		FTypedLoopNativeContext* Context, std::uint32_t InstructionIndex,
		std::int32_t Status, std::uint32_t InstructionOffset) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeInvokePureI32Method(
		FTypedLoopNativeContext* Context,
		std::uint32_t InstructionIndex,
		std::uint64_t ReceiverToken,
		std::int32_t Argument,
		std::uint32_t InstructionOffset) noexcept;

	struct FTypedLoopPlan;

	// Raw integer services shared by the portable wide executor and native CFG.
	// They never box operands or enter the VM between loop instructions.
	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopWideOperation(
		FTypedLoopNativeContext* Context, std::uint32_t InstructionIndex,
		std::uint64_t Left, std::uint64_t Right) noexcept;
	std::int32_t ExecuteWideTypedLoop(
		FTypedLoopNativeContext& Context, const FTypedLoopPlan& Plan) noexcept;

	struct FTypedLoopInstruction
	{
		EOpcode Opcode = EOpcode::Nop;
		std::uint32_t BytecodeOffset = 0;
		std::uint32_t NextInstruction = 0;
		std::uint32_t BranchInstruction = 0;
		std::uint32_t ExternalBranchOffset = 0;
		std::uint16_t Operand0 = 0;
		std::uint16_t Operand1 = 0;
		std::uint16_t EntryStackDepth = 0;
		std::uint16_t FieldMethodIndex = 0xFFFFu;
		std::uint16_t ReceiverArraySlot = 0xFFFFu;
		std::uint16_t TextValueSlot = 0xFFFFu;
		std::uint16_t TextFunctionIndex = 0xFFFFu;
		std::uint32_t TextCalleeOffset = InvalidJitInstructionOffset;
		std::int32_t Immediate = 0;
		// Constants are either floating or wide integer payloads. Reuse the same
		// storage so wide support does not enlarge every existing typed instruction.
		union
		{
			double ImmediateF64 = 0.0;
			std::uint64_t Immediate64;
		};
		std::uint8_t ArgumentCount = 0;
		bool bImmediateU32 = false;
		bool bImmediateF32 = false;
		bool bTextAdd = false;
		bool bTextIdentity = false;
		// A concatenation result may be represented by its two live operand slots
		// until the immediately following store, without an unbounded JIT value.
		bool bTextConcatStore = false;
		std::uint16_t TextFormatInstruction = 0xFFFFu;
		ETypedLoopLocalKind WideImmediateKind = ETypedLoopLocalKind::Unsupported;
	};

	struct FTypedLoopTextBufferPool;

	struct FTypedLoopPlan
	{
		enum class EFusedKind : std::uint8_t
		{
			None = 0,
			DirectIndexedUpdate,
			DirectConstantScalarIndexedUpdate,
			DirectIndexedSetContains,
			DirectAffineIndexedUpdate,
			DirectAffineSetContains,
			DirectFieldAccumulate,
			DirectFieldBatchUpdate,
			DirectStateFieldAccumulate,
			StateIndirectIndexedUpdate,
			StateIndirectSetContains,
			StateIndirectFieldUpdate,
			StateIndirectStringLookupCompare,
			StateStringMapLookup,
			StateDualStringArrayCompare,
			StateIndirectVirtualTransform,
			StateArrayVirtualTransform,
			StateMapF64Update,
			IndirectArrayUpdate,
			IndirectMapUpdate,
			IndirectSetContains,
		};

		struct FDirectAffineIndexedI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t DeltaSlot = 0xFFFFu;
			std::uint16_t ContainerSlot = 0xFFFFu;
			std::uint16_t StoredValueSlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::int32_t LimitImmediate = 0;
			bool bLimitImmediate = false;
			bool bDeltaConstant = false;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t DeltaModulus = 0;
			std::int32_t DeltaAddend = 0;
			std::int32_t TotalModulus = 0;
			// Exact source-visible failure sites retained by the normalized loop IR.
			std::uint16_t StateMulInstruction = 0xFFFFu;
			std::uint16_t StateAddInstruction = 0xFFFFu;
			std::uint16_t StateModInstruction = 0xFFFFu;
			std::uint16_t DeltaModInstruction = 0xFFFFu;
			std::uint16_t DeltaAddInstruction = 0xFFFFu;
			std::uint16_t LookupInstruction = 0xFFFFu;
			std::uint16_t ElementAddInstruction = 0xFFFFu;
			std::uint16_t TotalAddInstruction = 0xFFFFu;
			std::uint16_t TotalModInstruction = 0xFFFFu;
			std::uint16_t CounterInstruction = 0xFFFFu;
		};

		struct FDirectAffineSetI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t SetSlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::int32_t LimitImmediate = 0;
			bool bLimitImmediate = false;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::uint16_t StateMulInstruction = 0xFFFFu;
			std::uint16_t StateAddInstruction = 0xFFFFu;
			std::uint16_t StateModInstruction = 0xFFFFu;
			std::uint16_t TotalIncrementInstruction = 0xFFFFu;
			std::uint16_t CounterInstruction = 0xFFFFu;
			bool bModuloKey = false;
			std::int32_t KeyModulus = 0;
			std::uint16_t KeyModInstruction = 0xFFFFu;
		};

		struct FDirectStateFieldsI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t ChoiceSlot = 0xFFFFu;
			std::uint16_t ObjectSlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::uint8_t FieldCount = 0;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t ChoiceModulus = 0;
			std::int32_t TotalModulus = 0;
			std::uint16_t StateMulInstruction = 0xFFFFu;
			std::uint16_t StateAddInstruction = 0xFFFFu;
			std::uint16_t StateModInstruction = 0xFFFFu;
			std::uint16_t ChoiceModInstruction = 0xFFFFu;
			std::uint16_t TotalModInstruction = 0xFFFFu;
			std::uint16_t CounterInstruction = 0xFFFFu;
			std::array<std::int32_t, 32> SelectorValues{};
			std::array<std::uint16_t, 32> FieldNameIndices{};
			std::array<std::uint16_t, 32> FieldIncrementInstructions{};
			std::array<std::uint16_t, 32> TotalAddInstructions{};
		};

		struct FDirectFieldAccumulator
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t ObjectSlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::uint16_t FieldNameIndex = 0xFFFFu;
			std::uint16_t FieldInstruction = 0xFFFFu;
			std::uint16_t AddInstruction = 0xFFFFu;
			std::uint16_t CounterInstruction = 0xFFFFu;
		};

		struct FDirectFieldBatchI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t ObjectSlot = 0xFFFFu;
			std::uint8_t FieldCount = 0;
			std::uint16_t CounterInstruction = 0xFFFFu;
			std::array<std::uint16_t, 32> FieldNameIndices{};
			std::array<std::int32_t, 32> Deltas{};
			std::array<std::uint16_t, 32> AdditionInstructions{};
		};

		struct FIndirectI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t CursorSlot = 0xFFFFu;
			std::uint16_t OrderSlot = 0xFFFFu;
			std::uint16_t ContainerSlot = 0xFFFFu;
			std::uint16_t IndexOrKeySlot = 0xFFFFu;
			std::uint16_t DeltaOrValueSlot = 0xFFFFu;
			std::uint16_t StoredValueSlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::int32_t CursorMultiplier = 0;
			std::int32_t CursorIncrement = 0;
			std::int32_t CursorModulus = 0;
			std::int32_t SecondaryOffset = 0;
			std::int32_t SecondaryIndexModulus = 0;
			std::int32_t PositionMultiplier = 0;
			std::int32_t PositionModulus = 0;
			std::int32_t PositionMask = 0;
			std::int32_t TrueIncrement = 0;
			std::int32_t FalseIncrement = 0;
			std::int32_t DeltaDivisor = 0;
			std::int32_t DeltaModulus = 0;
			std::int32_t DeltaAddend = 0;
			std::int32_t ElementModulus = 0;
			std::int32_t TotalModulus = 0;
			// Some inlined state recurrences retain a semantic Nop between Mod and
			// StoreLocal while directly written recurrences do not.  Fused descriptors
			// use canonical instruction indices and translate through this flag so both
			// bytecode forms receive identical diagnostics and execution semantics.
			bool bStateUpdateHasNop = true;
		};

		struct FStateIndirectFieldsI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t PositionSlot = 0xFFFFu;
			std::uint16_t ChoicesSlot = 0xFFFFu;
			std::uint16_t ChoiceSlot = 0xFFFFu;
			std::uint16_t DeltaSlot = 0xFFFFu;
			std::uint16_t ObjectSlot = 0xFFFFu;
			std::uint8_t FieldCount = 0;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t CounterModulus = 0;
			std::int32_t PositionMultiplier = 0;
			std::int32_t PositionModulus = 0;
			std::int32_t DeltaDivisor = 0;
			std::int32_t DeltaModulus = 0;
			std::int32_t DeltaAddend = 0;
			std::array<std::int32_t, 16> SelectorValues{};
			std::array<std::uint16_t, 16> FieldNameIndices{};
			std::array<std::uint16_t, 16> TemporarySlots{};
			std::array<std::uint16_t, 16> AdditionInstructionIndices{};
		};

		struct FStateIndirectStringsI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t PositionSlot = 0xFFFFu;
			std::uint16_t OrderSlot = 0xFFFFu;
			std::uint16_t KeysSlot = 0xFFFFu;
			std::uint16_t OtherSlot = 0xFFFFu;
			std::uint16_t MapSlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t CounterModulus = 0;
			std::int32_t PositionModulus = 0;
			std::int32_t EqualityIncrement = 0;
			std::int32_t TotalModulus = 0;
		};

		struct FStateDualStringArrayCompareI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t LeftIndexSlot = 0xFFFFu;
			std::uint16_t RightIndexSlot = 0xFFFFu;
			std::uint16_t LeftArraySlot = 0xFFFFu;
			std::uint16_t RightArraySlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t LeftIndexModulus = 0;
			std::int32_t CounterMask = 0;
			std::int32_t RightIndexModulus = 0;
			bool bHasInlinedCallNop = false;
		};

		struct FStateIndirectVirtualI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::int32_t LimitImmediate = 0;
			bool bLimitImmediate = false;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t PositionSlot = 0xFFFFu;
			bool bDirectStateIndex = false;
			std::uint16_t ChoicesSlot = 0xFFFFu;
			std::uint16_t ChoiceSlot = 0xFFFFu;
			std::uint16_t SelectedReceiverSlot = 0xFFFFu;
			std::uint16_t AccumulatorSlot = 0xFFFFu;
			std::uint16_t MethodNameIndex = 0xFFFFu;
			std::uint8_t ReceiverCount = 0;
			std::uint16_t CallInstructionIndex = 0xFFFFu;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t CounterModulus = 0;
			std::int32_t PositionMultiplier = 0;
			std::int32_t PositionModulus = 0;
			std::uint16_t StateMulInstruction = 0xFFFFu;
			std::uint16_t StateAddInstruction = 0xFFFFu;
			std::uint16_t StateModInstruction = 0xFFFFu;
			std::uint16_t CounterModInstruction = 0xFFFFu;
			std::uint16_t PositionMulInstruction = 0xFFFFu;
			std::uint16_t PositionAddInstruction = 0xFFFFu;
			std::uint16_t PositionModInstruction = 0xFFFFu;
			std::uint16_t LookupInstruction = 0xFFFFu;
			std::uint16_t CounterInstruction = 0xFFFFu;
			std::array<std::int32_t, 8> SelectorValues{};
			std::array<std::uint16_t, 8> ReceiverSlots{};
		};

		/**
		 * Straight-line, side-effect-free i32 method body selected after ordinary
		 * generation-qualified virtual resolution.  The compact RPN stream is built
		 * from verified bytecode once with the loop plan; runtime only maps the
		 * resolved function index to its immutable expression.
		 */
		struct FPureI32MethodExpression
		{
			std::uint16_t FunctionIndex = 0xFFFFu;
			std::uint8_t MaxStackDepth = 0;
			void* NativeCode = nullptr;
			std::vector<FTypedLoopInstruction> Instructions;
			std::vector<std::uint16_t> FieldNameIndices;
		};

		struct FFieldI32MethodExpression
		{
			std::uint16_t FunctionIndex = 0xFFFFu;
			std::uint8_t ArgumentCount = 1;
			void* NativeCode = nullptr;
			std::vector<std::uint16_t> FieldNameIndices;
			std::vector<FTypedLoopInstruction> Instructions;
		};

		struct FStateArrayVirtualI32
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t OrderSlot = 0xFFFFu;
			std::uint16_t ReceiverArraySlot = 0xFFFFu;
			std::uint16_t IndexSlot = 0xFFFFu;
			std::uint16_t SelectedReceiverSlot = 0xFFFFu;
			std::uint16_t AccumulatorSlot = 0xFFFFu;
			std::uint16_t MethodNameIndex = 0xFFFFu;
			std::uint16_t CallInstructionIndex = 0xFFFFu;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t PositionOperand = 0;
			bool bPositionUsesMask = false;
			bool bDirectReceiverIndex = false;
			std::uint16_t PositionInstruction = 18u;
			std::uint16_t ReceiverLookupInstruction = 23u;
			std::uint16_t CounterInstruction = 30u;
			std::vector<FPureI32MethodExpression> Methods;
		};

		struct FStateMapF64Update
		{
			std::uint16_t CounterSlot = 0xFFFFu;
			std::uint16_t LimitSlot = 0xFFFFu;
			std::uint16_t StateSlot = 0xFFFFu;
			std::uint16_t SizeSlot = 0xFFFFu;
			std::uint16_t MapSlot = 0xFFFFu;
			std::uint16_t KeySlot = 0xFFFFu;
			std::uint16_t ValueSlot = 0xFFFFu;
			std::uint16_t StoredValueSlot = 0xFFFFu;
			std::uint16_t TotalSlot = 0xFFFFu;
			std::int32_t StateMultiplier = 0;
			std::int32_t StateIncrement = 0;
			std::int32_t StateModulus = 0;
			std::int32_t KeyMultiplier = 0;
			std::int32_t StateValueModulus = 0;
			double ValueMultiplier = 0.0;
			double StateValueScale = 0.0;
			double Threshold = 0.0;
			double ConditionalScale = 0.0;
			bool bHasInlinedCallNop = false;
		};

		// Profitability belongs to this natural loop, not to the function as a
		// whole. Mixed functions may contain both an efficient typed kernel and a
		// complex loop that is cheaper in the bytecode VM.
		bool bProfitable = false;
		std::uint32_t HeaderOffset = 0;
		std::uint32_t ExitOffset = 0;
		std::uint16_t LocalCount = 0;
		std::uint16_t MaxStackDepth = 0;
		bool bUsesDirectScalarFields = false;
		bool bMutatesDirectScalarFields = false;
		bool bOwnsStringTemporaries = false;
		bool bUsesTextOperations = false;
		std::uint16_t TextTemporaryCount = 0;
		std::vector<FTypedLoopNativeStringView> TextConstants;
		std::shared_ptr<FTypedLoopTextBufferPool> TextBuffers;
		// Rebind compact array storage at the loop boundary, after any outer call.
		bool bRuntimeBoundArrayPush = false;

		// A proven zero-start i32 induction variable bounds every indexed read.
		// Only receiver preparation is shortened; NativeSize remains the actual
		// array length, so bounds errors still occur at the original instruction.
		std::uint16_t ObjectArrayPrefixCounterSlot = 0xFFFFu;
		std::uint16_t ObjectArrayPrefixLimitSlot = 0xFFFFu;
		std::int32_t ObjectArrayPrefixLimitImmediate = 0;

		// A fresh object allocated inside the loop may be materialized once at the
		// loop boundary only after the planner proves that its identity cannot be
		// observed, every field read is dominated by an assignment, and the sole
		// method call is a side-effect-free scalar projection.  The transformed
		// typed CFG still operates on an ordinary rooted script object, so field ABI,
		// checked arithmetic and ownership remain shared with the normal runtime.
		bool bHoistsNonEscapingObject = false;
		std::uint16_t HoistedObjectTypeIndex = 0xFFFFu;
		std::uint16_t HoistedObjectSlot = 0xFFFFu;
		std::uint16_t HoistedObjectCounterSlot = 0xFFFFu;
		std::uint32_t HoistedObjectInstructionOffset = 0xFFFFFFFFu;

		// Optional generation-owned machine-code lowering for the architecture-
		// neutral plan.  It is deliberately separate from FusedKind: structural
		// fusions remain useful fallbacks, while this covers arbitrary validated CFG.
		void* NativeI32Code = nullptr;
		EFusedKind FusedKind = EFusedKind::None;
		FDirectAffineIndexedI32 DirectIndexed;
		FDirectAffineSetI32 DirectSet;
		FDirectFieldAccumulator DirectFieldAccumulator;
		FDirectFieldBatchI32 DirectFieldBatch;
		FDirectStateFieldsI32 DirectFields;
		FIndirectI32 Indirect;
		FStateIndirectFieldsI32 StateFields;
		FStateIndirectStringsI32 StateStrings;
		FStateDualStringArrayCompareI32 StateStringArrays;
		FStateIndirectVirtualI32 StateVirtual;
		FStateArrayVirtualI32 StateArrayVirtual;

		// Pure one/two-argument method targets used by the general typed CFG. The
		// list is keyed by stable function identity after normal overload/virtual
		// resolution; source names are never used as the runtime dispatch key.
		std::vector<FPureI32MethodExpression> GenericPureMethods;
		std::vector<FFieldI32MethodExpression> FieldMethods;
		FStateMapF64Update StateMapF64;
		std::vector<FTypedLoopInstruction> Instructions;
		void* NativeWideCode = nullptr;
		bool bWideCandidate = false;

	};

	struct FRetainedObjectArrayProjectionShape
	{
		std::uint16_t ParameterSlot = 0xFFFFu;
		std::uint16_t ArraySlot = 0xFFFFu;
		std::uint16_t ObjectSlot = 0xFFFFu;
		std::uint16_t CounterSlot = 0xFFFFu;
		std::uint16_t TotalSlot = 0xFFFFu;
		std::uint16_t ReverseSlot = 0xFFFFu;
		std::int32_t FirstAddend = 0;
		std::int32_t FirstMask = 0;
		std::int32_t SecondMultiplier = 0;
		std::int32_t SecondMask = 0;
		std::int32_t SecondConstant = 0;
		std::int32_t FirstModulus = 0;
		std::int32_t SecondModulus = 0;
		std::int32_t SecondPostMultiplier = 0;
		std::uint16_t ProjectionFunctionIndex = 0xFFFFu;
		std::uint32_t FirstAddOffset = 0;
		std::uint32_t SecondMultiplyOffset = 0;
		std::uint32_t ProjectionAddOffset = 0;
		std::uint32_t ProjectionCallOffset = 0;
		std::uint32_t TotalAddOffset = 0;
		std::uint32_t ReverseInitializeOffset = 0;
		bool bForwardDirectConstant = false;
		bool bForwardModuloMultiply = false;
	};

	struct FTypedLoopPlanSet
	{
		std::vector<FTypedLoopPlan> Plans;
		// Plan index + 1 at a bytecode header; zero is not a typed loop.
		std::vector<std::uint16_t> PlanAtOffset;
		// A fused plan can require more than one primitive container kind. Keep
		// this proof with the immutable plan instead of broadening the legacy
		// whole-function storage candidates (which would affect unrelated code).
		bool bRequiresScalarArrayStorage = false;
		bool bRequiresScalarMapStorage = false;
		bool bRequiresScalarSetStorage = false;
		bool bHasProfitablePlan = false;
		bool bHasUnprofitableComplexPlan = false;
		// Cost selection must be based on coverage, not merely on the presence of
		// one profitable loop.  A common setup loop can be typed while the real
		// container/object hot loop remains unsupported; treating that function as
		// fully profitable makes Mixed/JIT slower than the VM.
		std::uint32_t BackwardLoopCount = 0;
		std::uint32_t PlannedBackwardLoopCount = 0;
		bool bHasUnplannedComplexLoop = false;
		// A semantically valid fusion was found, but its only profitable lowering
		// (for example an asmjit pure-method kernel) is unavailable on this build.
		// The function-level selector must choose VM instead of running the same
		// complex loop through the slower boxed Linear fallback.
		bool bNativeOnlyPlanUnavailable = false;
		// Whole-function escape analysis is immutable for a module generation. Cache
		// both the positive descriptor and the negative result so hot calls never
		// rescan bytecode merely to decide whether scalar replacement applies.
		bool bRetainedObjectArrayProjectionAnalyzed = false;
		bool bHasRetainedObjectArrayProjection = false;
		FRetainedObjectArrayProjectionShape RetainedObjectArrayProjection;
	};

	struct FTypedLoopExecutionResult
	{
		ETypedLoopExecution Status = ETypedLoopExecution::NotHandled;
		std::size_t NextBytecodeOffset = 0;
		std::size_t ErrorBytecodeOffset = 0;
	};

	struct FAffineSetResult
	{
		std::int32_t Counter;
		std::int32_t State;
		std::int32_t Total;
		std::uint16_t FailureInstruction;
		const char* Error;
	};

	bool TryInlineLocalScalarClosure(const FBytecodeModule& Module,
		const FBytecodeFunction& Function, FBytecodeFunction& Out, std::uint16_t& OutSites);
	#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
	// A test-only adapter calls the same file-local production kernel. Do not
	// change the production kernel's linkage just to expose it to a test target.
	FAffineSetResult TestExecuteAffineSetKernel(
		const FTypedLoopPlan::FDirectAffineSetI32& Plan,
		FSet::FIntScalarElements::FReadOnlyProbe Values,
		std::int32_t Counter, std::int32_t Limit, std::int32_t State, std::int32_t Total) noexcept;

	using FAffineSetObserver = void (*)(FExecutionContext&, const FTypedLoopPlan&, const FAffineSetResult&);
	extern thread_local FAffineSetObserver AffineSetObserver;
	#endif

	// Shared by plan admission and runtime preflight. Keeping this proof in the
	// typed-loop internal interface prevents the planner and executor from
	// drifting after their implementation was split into independent units.
	bool TypedContainerMethodsHaveProvenStorage(
		const FTypedLoopPlan& Plan,
		bool bFusedScalarStorageSafe,
		bool bAllowScalarArrayStorage,
		bool bAllowScalarMapStorage,
		bool bAllowScalarSetStorage);

	std::shared_ptr<FTypedLoopPlanSet> BuildTypedLoopPlans(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		JitNative::FArena& Arena,
		bool bAllowScalarArrayStorage,
		bool bAllowScalarMapStorage,
		bool bAllowScalarSetStorage,
		const std::vector<std::size_t>* TextAddOffsets = nullptr,
		const std::vector<std::size_t>* UnsupportedTextFormatOffsets = nullptr);
	bool HasDirectI32FieldSummary(const FBytecodeModule& Module,
		const FBytecodeFunction& Function);

	#if defined(_MSC_VER) && defined(_M_X64)
	__declspec(code_seg(".vsjit"))
	#endif
	FTypedLoopExecutionResult ExecuteTypedLoopPlan(
		FExecutionContext& Vm,
		const FTypedLoopPlanSet& Plans,
		std::size_t HeaderOffset);

	/**
	 * Prefer asmjit native CFG for scalar I32/U32/F64/bool bodies (including simple
	 * methods that only touch typed this-fields). Otherwise the FValue linear trampoline.
	 * Ref-parameter functions skip native and use the linear frame path. Bodies that
	 * fail CFG validation still compile to the interpret trampoline (not Unsupported).
	 */
	bool TryCompile(
		const FBytecodeModule& Module,
		const FBytecodeFunction& Function,
		JitNative::FArena& Arena,
		FJitFunctionEntry& OutEntry);
}
