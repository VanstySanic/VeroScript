// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptJitFrameServices.h"
#include "VeroScriptJitValueServices.h"
#include "VeroScriptJitCallServices.h"
#include "VeroScriptJitScalarServices.h"

// Keep live status checks inline even inside the large Linear dispatch loop.
#if defined(_MSC_VER)
#define VEROSCRIPT_ASMJIT_HOT_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define VEROSCRIPT_ASMJIT_HOT_INLINE inline __attribute__((always_inline))
#else
#define VEROSCRIPT_ASMJIT_HOT_INLINE inline
#endif

namespace VeroScript::AsmJit
{
	// Backend-owned invocation state. Language behavior is accessed exclusively
	// through negotiated core services; generated code never receives a VM pointer.
	class FExecutionContext final
	{
	public:
		FExecutionContext(FJitCallContext& Context, const FJitFunctionEntry& Entry,
			FJitCodeCache& Cache, FJitStats& Stats, std::vector<std::vector<FValue>>& Scratch);

		~FExecutionContext();

		void JitReportRuntimeError(std::string Message);

		void JitReportRuntimeError(std::string Message, EJitRuntimeStatus Status);

		void JitReportRuntimeError(std::string Message, FJitKernelFailure Failure);

		std::uint8_t GetPendingJitArgumentCount() const;

		const FValue* GetPendingJitArguments() const;

		bool JitPushReturn(FValue Value);

		bool PrepareReturn(FValue& Value);

		void ReportNativeFailure() noexcept;

		double GetPendingJitPayloadF64() const;

		std::int32_t GetPendingJitPayloadI32() const;

		bool JitHandleKernelFailure(std::int32_t EncodedFailure);

		void* GetPendingJitNativeCode() const;
		VEROSCRIPT_ASMJIT_HOT_INLINE void RecordNativeExecution(const EJitExecutionKind Kind) noexcept
		{
			if (Kind == EJitExecutionKind::NativeLeaf)
			{
				++Stats.NativeLeafExecutions;
			}
			else
			{
				++Stats.NativeCfgExecutions;
			}
			if (PendingJitFunctionIndex == Context.FunctionIndex)
			{
				Context.ExecutionKind = Kind;
			}
		}

		const FBytecodeModule* DebugGetModule() const;

		std::uint16_t GetPendingJitFunctionIndex() const;

		EJitScalarAbi GetPendingJitScalarAbi() const;

		bool JitInvokeFunctionU32Fast(std::uint16_t FunctionIndex,
			const std::uint32_t* Arguments,
			std::uint8_t ArgumentCount,
			std::uint32_t* OutResult,
			bool& bOutHandled, bool bRootInvocation = false);

		bool JitInvokeFunctionI32Fast(std::uint16_t FunctionIndex,
			const std::int32_t* Arguments,
			std::uint8_t ArgumentCount,
			std::int32_t* OutResult,
			bool& bOutHandled, bool bRootInvocation = false);

		EJitScalarValueKind GetPendingJitScalarReturnKind() const;

		std::uint16_t GetPendingJitStackCapacityHint() const;

		std::uint16_t GetPendingJitNativeLocalCount() const;

		bool WasExitRequested() const;

		VEROSCRIPT_ASMJIT_HOT_INLINE bool HasError() const noexcept
		{
			return ExecutionStatus.Error ? *ExecutionStatus.Error : Services.HasError();
		}

		bool JitInvokeFunctionF64Fast(std::uint16_t FunctionIndex,
			const double* Arguments,
			std::uint8_t ArgumentCount,
			double* OutResult,
			bool& bOutHandled, bool bRootInvocation = false);

		bool JitInvokeCall(std::uint16_t FunctionIndex,
			const FValue* Arguments,
			std::uint8_t ArgumentCount);

		bool JitTryInvokeReadonlyTextBuiltin(std::uint16_t FunctionIndex,
			const FValue* Arguments, std::uint8_t ArgumentCount, bool& OutHandled);

		bool JitTakeReturn(FValue& OutValue);

		const FObjectHandle& GetJitThisObject() const;

		bool JitInvokeBoundMethodI32Fast(std::uint16_t FunctionIndex,
			const FObjectHandle& ThisObject,
			const std::int32_t* Arguments,
			std::uint8_t ArgumentCount,
			std::int32_t* OutResult,
			bool& bOutHandled);

		bool JitInvokeBoundMethod(std::uint16_t FunctionIndex,
			const FValue& ThisObject,
			const FValue* Arguments,
			std::uint8_t ArgumentCount);

		bool JitInvokeBoundMethodU32Fast(std::uint16_t FunctionIndex,
			const FObjectHandle& ThisObject,
			const std::uint32_t* Arguments,
			std::uint8_t ArgumentCount,
			std::uint32_t* OutResult,
			bool& bOutHandled);

		bool JitInvokeBoundMethodF64Fast(std::uint16_t FunctionIndex,
			const FObjectHandle& ThisObject,
			const double* Arguments,
			std::uint8_t ArgumentCount,
			double* OutResult,
			bool& bOutHandled);

		bool JitCallMethod(std::uint16_t MethodNameIndex,
			const FValue& ThisObject,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			std::uint32_t CallSiteOffset = 0xFFFFFFFFu);

		bool JitLoadGlobal(std::uint16_t Slot, FValue& OutValue);

		bool JitStoreGlobal(std::uint16_t Slot, FValue Value);

		bool JitLoadThisFieldI32(std::uint16_t NameIndex, std::int32_t& OutValue);

		bool JitStoreThisFieldI32(std::uint16_t NameIndex, std::int32_t Value);

		bool JitInitializeThisFieldI32(std::uint16_t NameIndex, std::int32_t Value);

		bool JitIncField(FValue Object, std::uint16_t NameIndex);

		bool JitLoadThisFieldU32(std::uint16_t NameIndex, std::uint32_t& OutValue);

		bool JitStoreThisFieldU32(std::uint16_t NameIndex, std::uint32_t Value);

		bool JitInitializeThisFieldU32(std::uint16_t NameIndex, std::uint32_t Value);

		bool JitLoadThisFieldF64(std::uint16_t NameIndex, double& OutValue);

		bool JitStoreThisFieldF64(std::uint16_t NameIndex, double Value);

		bool JitInitializeThisFieldF64(std::uint16_t NameIndex, double Value);

		bool JitInvokeBuiltin(EBuiltinId Builtin,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			bool& bOutHasResult,
			FValue& OutResult);

		bool JitInvokeFunctionMixedFast(std::uint16_t FunctionIndex,
			const std::uint64_t* Arguments,
			std::uint8_t ArgumentCount,
			std::uint64_t* OutResult,
			bool& bOutHandled);

		bool JitInvokeBoundMethodMixedFast(std::uint16_t FunctionIndex,
			const FObjectHandle& ThisObject,
			const std::uint64_t* Arguments,
			std::uint8_t ArgumentCount,
			std::uint64_t* OutResult,
			bool& bOutHandled);

		bool JitLoadThisFieldMixed(std::uint16_t NameIndex,
			EJitScalarValueKind Kind,
			std::uint64_t& OutValue);

		bool JitStoreThisFieldMixed(std::uint16_t NameIndex,
			EJitScalarValueKind Kind,
			std::uint64_t Value);

		bool JitInitializeThisFieldMixed(std::uint16_t NameIndex,
			EJitScalarValueKind Kind,
			std::uint64_t Value);

		bool JitResolveDirectI32MethodTarget(const FObjectHandle& Receiver,
			std::uint16_t MethodNameIndex,
			std::uint8_t ArgumentCount,
			std::uint32_t CallSiteOffset,
			std::uint16_t& OutFunctionIndex,
			bool& OutHandled);

		bool JitRequiresCoreVirtualDispatch(const FScriptObject& Receiver,
			std::uint16_t FunctionIndex) const;
		bool JitBorrowObjectI32Field(const FObjectHandle& Receiver,
			std::uint16_t NameIndex, std::uint16_t& OutFieldIndex,
			std::int32_t*& OutStorage);
		bool JitGetObjectLayout(const FScriptObject& Receiver, std::uint16_t& OutTypeIndex);

		void JitReportDirectI32TransformFailure(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint16_t BoundFunctionIndex,
			std::uint32_t CallSiteOffset,
			std::size_t CalleeInstructionOffset,
			std::string Message);

		FValue* JitBorrowDirectLinearLocal(const std::uint16_t Slot) noexcept;

		bool JitStoreLocal(std::uint16_t Slot, FValue Value);

		VEROSCRIPT_ASMJIT_HOT_INLINE bool JitCanUseDirectScalarMethod() const noexcept;

		VEROSCRIPT_ASMJIT_HOT_INLINE bool JitHasInstanceCallInterceptor() const noexcept;

		VEROSCRIPT_ASMJIT_HOT_INLINE bool JitHasMutationCallback() const noexcept;

		bool JitCanInlineTextProjection() const noexcept;

		void JitReportTextFailure(const char* Message, std::uint32_t CallerOffset,
			std::uint16_t FunctionIndex, std::uint32_t CalleeOffset) noexcept;

		void JitRefineRuntimeErrorLocation(std::size_t InstructionOffset);

		bool JitNewObject(std::uint16_t TypeIndex, FValue& OutObject, bool bUninitialized = false);

		void RecordJitArrayFastPathGet(const std::size_t Count = 1);

		bool JitBorrowDirectI32BoundFieldMethod(std::uint16_t Slot,
			std::uint16_t FunctionIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::int32_t*& OutStorage,
			std::int32_t& OutAddend,
			std::int32_t& OutArgumentModulus,
			std::int32_t& OutModulus,
			bool& OutHandled,
			bool* bOutReturnsI32 = nullptr,
			std::uint16_t* OutFunctionIndex = nullptr);

		bool JitBorrowDirectI32FieldMethod(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint32_t CallSiteOffset,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::int32_t*& OutStorage,
			std::int32_t& OutAddend,
			std::int32_t& OutArgumentModulus,
			std::int32_t& OutModulus,
			bool& OutHandled,
			bool* bOutReturnsI32 = nullptr,
			std::uint16_t* OutFunctionIndex = nullptr);

		bool JitBorrowLocalI32Field(std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::int32_t*& OutStorage);

		bool JitBorrowLocalU32Field(std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::uint32_t*& OutStorage);

		bool JitBorrowLocalF32Field(std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			float*& OutStorage);

		bool JitBorrowLocalF64Field(std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			double*& OutStorage);

		void RecordJitNativeTypedLoopExecution();

		void RecordJitArrayFastPathSet(const std::size_t Count = 1);

		void RecordJitArrayFastPathPush(const std::size_t Count = 1);

		void RecordJitScalarMapFastPathGet(const std::size_t Count = 1);

		void RecordJitScalarMapFastPathSet(const std::size_t Count = 1);

		bool JitCommitDirectI32FieldMethod(FScriptObject& Object,
			std::uint16_t FieldIndex,
			const std::size_t Count = 1);

		bool JitCommitDirectScalarMethod(const EJitScalarValueKind Kind,
			const std::size_t Count = 1);

		bool JitBorrowDirectI32TransformMethod(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint32_t CallSiteOffset,
			FScriptObject*& OutObject,
			std::int32_t& OutAddend,
			std::int32_t& OutModulus,
			bool& OutHandled,
			std::uint16_t* OutFunctionIndex = nullptr);

		bool JitCommitDirectI32TransformMethod(const std::size_t Count = 1);

		void JitCommitBorrowedFieldMutation(FScriptObject& Object,
			const std::uint16_t FieldIndex);

		const FBytecodeFunction* GetPendingJitLinearFunction() const;

		bool GetPendingJitUseScalarMapStorage() const;

		bool GetPendingJitUseScalarSetStorage() const;

		bool GetPendingJitUseScalarArrayStorage() const;

		bool GetPendingJitUseI32ModuloAccumulatorLoop() const;

		const JitLinear::FTypedLoopPlanSet* GetPendingJitTypedLoopPlans() const;

		bool GetPendingJitRequiresScalarNativeGuard() const;

		bool JitCanUseScalarNativeCalls(const std::uint32_t* EncodedGuards,
			std::size_t GuardCount) const;

		const std::uint32_t* GetPendingJitScalarNativeGuards() const;

		std::size_t GetPendingJitScalarNativeGuardCount() const;

		bool JitInterpretCurrentFunction();

		bool JitEnterLinearFrame(std::size_t LocalCount,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			const FObjectHandle& ThisObject);

		const FObjectHandle& GetPendingJitThis() const;

		void JitAcquireLinearScratch(std::size_t CapacityHint,
			std::vector<FValue>& OutScratch);

		bool JitReleaseOwnedValues(std::vector<FValue>& Values);

		bool JitLeaveLinearFrame();

		void JitRecycleLinearScratch(std::vector<FValue>& Scratch);

		bool GetPendingJitMayHaveDirectScalarMethodStore() const;

		bool GetPendingJitMayHaveScalarMethodAccumulator() const;

		bool GetPendingJitMayHaveBoundScalarMethodCall() const;

		const std::uint8_t* GetPendingJitScalarSetCallKinds() const;

		std::size_t GetPendingJitScalarSetCallKindCount() const;

		const std::uint8_t* GetPendingJitInvariantLocalCompareSites() const;

		std::size_t GetPendingJitInvariantLocalCompareSiteCount() const;

		bool JitReadDirectI32FieldAfterMutation(FScriptObject& Object,
			std::uint16_t FieldIndex,
			std::int32_t& OutValue);

		bool JitBorrowDirectI32BoundTransformMethod(std::uint16_t Slot,
			std::uint16_t FunctionIndex,
			FScriptObject*& OutObject,
			std::int32_t& OutAddend,
			std::int32_t& OutModulus,
			bool& OutHandled,
			std::uint16_t* OutFunctionIndex = nullptr);

		bool JitReleaseOwnedValue(FValue& Value);

		void JitAcquireLinearArgumentScratch(std::size_t CapacityHint,
			std::vector<FValue>& OutScratch);

		bool JitAddLocalImm(std::uint16_t Slot, std::int32_t Imm);

		bool JitAddLocalLocal(std::uint16_t DestSlot, std::uint16_t SrcSlot);

		void JitSetActiveInstructionOffset(const std::size_t InstructionOffset) noexcept;

		bool JitEvalArithmetic(EOpcode Op,
			const FValue& Left,
			const FValue& Right,
			FValue& OutResult);

		bool JitEvalBitwise(EOpcode Op,
			const FValue& Left,
			const FValue& Right,
			FValue& OutResult);

		bool JitCompare(EOpcode Op,
			const FValue& Left,
			const FValue& Right,
			bool& OutResult);

		bool JitEvalUnary(EOpcode Op, const FValue& Operand, FValue& OutResult);

		bool JitTryPushDynamicArrayScalar(const FValue& Container,
			const FValue& Value,
			bool& bOutHandled);

		bool JitLoadThisField(std::uint16_t NameIndex, FValue& OutValue);

		bool JitStoreThisField(std::uint16_t NameIndex, FValue Value);

		bool JitInitializeThisField(std::uint16_t NameIndex, FValue Value);

		bool JitLoadField(const FValue& Object, std::uint16_t NameIndex, FValue& OutValue);

		bool JitStoreField(const FValue& Object, std::uint16_t NameIndex, const FValue& Value);

		bool JitCloneStruct(FValue& InOutValue);

		bool JitNewContainer(EOpcode Op, std::uint16_t TypeNameIndex, FValue& Out);

		bool JitEnableScalarArrayStorage(FValue& Container);

		bool JitEnableScalarMapStorage(FValue& Container);

		bool JitEnableScalarSetStorage(FValue& Container);

		bool JitTryGetScalarMap(const FValue& Container,
			const FValue& Index,
			FValue& OutValue,
			bool& bOutHandled);

		bool JitTryGetDynamicArrayScalar(const FValue& Container,
			std::int64_t Index,
			FValue& OutValue,
			bool& bOutHandled);

		bool JitGetIndex(const FValue& Container, const FValue& Index, FValue& OutValue);

		bool JitTrySetScalarMap(FValue& Container,
			const FValue& Index,
			const FValue& Value,
			bool& bOutHandled);

		bool JitTrySetDynamicArrayScalar(FValue& Container,
			std::int64_t Index,
			const FValue& Value,
			bool& bOutHandled);

		bool JitSetIndex(FValue Container, const FValue& Index, const FValue& Value, FValue& OutContainer);

		bool JitCallNative(std::uint16_t NativeIndex,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			FValue& OutReturn);

		bool JitMakeClosure(std::uint16_t FunctionIndex,
			const FValue* Captures,
			std::uint8_t CaptureCount,
			FValue& OutClosure);

		bool JitCallIndirect(const FValue& Callee,
			const FValue* Arguments,
			std::uint8_t ArgumentCount);

		bool JitEvaluateCondition(const FValue& Value, bool& bOutCondition);

		bool JitLocalGeImm(std::uint16_t Slot, std::int32_t Immediate, bool& bOutResult);

		bool JitMakeLocalRef(std::uint16_t Slot, FValue& OutRef);

		bool JitMakeGlobalRef(std::uint16_t Slot, FValue& OutRef);

		bool JitMakeThisFieldRef(std::uint16_t NameIndex, FValue& OutRef);

		bool JitMakeFieldRef(FValue Object, std::uint16_t NameIndex, FValue& OutRef);

		bool JitGetIndexRef(std::uint16_t ArrayLocalSlot, FValue Index, FValue& OutRef, bool bBorrow = true);

		bool JitGetMapEntryRef(std::uint16_t MapLocalSlot, FValue Key, FValue& OutRef);

		bool JitBindLocal(std::uint16_t Slot, FValue Value);

		bool JitHandleLinearThrow(FValue Value,
			std::size_t InstructionOffset,
			std::size_t& OutHandlerOffset);

		bool JitBorrowDirectI32NullaryBoundFieldMethod(std::uint16_t Slot,
			std::uint16_t FunctionIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::int32_t*& OutStorage,
			std::int32_t& OutAddend,
			std::int32_t& OutModulus,
			bool& OutHandled);

		bool JitTryInvokeBoundMethodI32OnLocal(std::uint16_t Slot,
			std::uint16_t FunctionIndex,
			const FValue& Argument,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryPushDynamicArrayScalarOnLocal(std::uint16_t Slot,
			const FValue& Value,
			bool& bOutHandled);

		bool JitTrySetI32MapAffineRangeOnLocal(std::uint16_t MapSlot,
			std::int32_t Begin,
			std::int32_t End,
			std::int32_t Immediate,
			bool& OutHandled);

		bool JitTrySetScalarMapI32AddOnLocals(std::uint16_t MapSlot,
			std::uint16_t KeySlot,
			std::uint16_t ValueSlot,
			std::int32_t Immediate,
			bool& bOutHandled);

		bool JitTrySetScalarMapDirectFromLocals(std::uint16_t MapSlot,
			std::uint16_t KeySlot,
			std::uint16_t SourceSlot,
			bool& bOutHandled);

		bool JitTryAddLocalFromScalarArray(std::uint16_t DestSlot,
			std::uint16_t ArraySlot,
			const FValue& Index,
			bool& OutHandled);

		bool JitTryAddLocalFromScalarMapModuloI32(std::uint16_t DestSlot,
			std::uint16_t MapSlot,
			const FValue& Key,
			std::int32_t Modulus,
			bool& OutHandled);

		bool JitTryGetScalarIndexOnLocal(std::uint16_t Slot,
			const FValue& Index,
			FValue& OutValue,
			bool& bOutMapHandled,
			bool& bOutArrayHandled);

		bool JitTryAddLocalFromScalarMap(std::uint16_t DestSlot,
			std::uint16_t MapSlot,
			const FValue& Key,
			bool& OutHandled);

		bool JitTryCallScalarSetMethodOnLocal(std::uint16_t Slot,
			const FValue& Argument,
			EJitScalarSetOperation Operation,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryInsertI32SetRangeOnLocal(std::uint16_t Slot,
			std::int32_t Begin,
			std::int32_t End,
			bool& OutHandled);

		bool JitTryCallScalarSetMethodOnLocals(std::uint16_t Slot,
			std::uint16_t ArgumentSlot,
			EJitScalarSetOperation Operation,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryCallScalarMethodOnValue(const FValue& Receiver, std::uint16_t MethodNameIndex,
			const FValue* Arguments, std::uint8_t ArgumentCount, std::uint32_t CallSiteOffset,
			FValue& OutValue, bool& OutHandled);

		bool JitTryCallScalarMethodOnLocal(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			std::uint32_t CallSiteOffset,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryCallNullaryI32MethodOnLocal(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint32_t CallSiteOffset,
			std::int32_t& OutValue,
			bool& OutHandled,
			bool& OutStableConstant);

		bool JitAddValueToLocal(std::uint16_t DestSlot, const FValue& Source);

		bool JitLocalGeLocal(std::uint16_t LeftSlot,
			std::uint16_t RightSlot,
			bool& bOutResult);

		bool JitCompareLocals(EOpcode Op,
			std::uint16_t LeftSlot,
			std::uint16_t RightSlot,
			bool& bOutResult);

		bool JitBorrowLocalScalarField(std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			const FValue*& OutStorage);

		bool JitTryAddLocalFromLocalField(std::uint16_t DestSlot,
			std::uint16_t ObjectSlot,
			std::uint16_t NameIndex,
			std::uint32_t CallSiteOffset,
			bool& OutHandled);

		bool JitTrySetDynamicArrayScalarOnLocal(std::uint16_t ArraySlot,
			const FValue& Index,
			const FValue& Value,
			bool& bOutHandled);

		bool JitTrySetScalarMapDirectOnLocal(std::uint16_t MapSlot,
			const FValue& Key,
			const FValue& Value,
			bool& bOutHandled);

		bool JitTryUpdateLocalFieldI32Modulo(std::uint16_t Slot,
			std::uint16_t NameIndex,
			const FValue& Delta,
			std::int32_t Modulus,
			std::uint32_t CallSiteOffset,
			bool& OutHandled);

		bool JitLoadLocalField(std::uint16_t Slot,
			std::uint16_t NameIndex,
			FValue& OutValue);

		bool JitTryCallScalarMethodOnLocalNoArgs(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint32_t CallSiteOffset,
			FValue& OutValue,
			bool& OutHandled);

		bool JitCallMethodOnLocal(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			std::uint32_t CallSiteOffset = 0xFFFFFFFFu);

		bool JitTryCallScalarContainerMethodOnLocals(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint16_t ArgumentSlot,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryCallScalarContainerMethodOnLocal(std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryCompareStringArrayElements(std::uint16_t LeftArraySlot,
			std::uint16_t LeftIndexSlot,
			std::uint16_t RightArraySlot,
			std::uint16_t RightIndexSlot,
			bool bEqual,
			bool& OutResult,
			bool& OutHandled);

		bool JitTryIncAndAccumulateLocalI32Field(std::uint16_t ObjectSlot,
			std::uint16_t NameIndex,
			std::uint16_t AccumulatorSlot,
			bool& OutHandled,
			EJitFieldAccumulateFailure& OutFailure);

		bool JitTryIncLocalField(std::uint16_t Slot,
			std::uint16_t NameIndex,
			bool& OutHandled);

		bool JitLoadLocal(std::uint16_t Slot, FValue& OutValue);
		bool JitTakeLocal(std::uint16_t Slot, FValue& OutValue);

		bool JitTryLoadScalarLocal(std::uint16_t Slot,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryLoadScalarGlobal(std::uint16_t Slot,
			FValue& OutValue,
			bool& OutHandled);

		bool JitTryCheckedScalarCast(const FValue& Value,
			std::uint16_t TargetTypeNameIndex,
			FValue& OutResult,
			bool& OutHandled);

		bool JitTryScalarBuiltinConversion(EBuiltinId Builtin,
			const FValue& Value,
			FValue& OutResult,
			bool& OutHandled);

		bool JitTryUpdateScalarMapEntryOnLocal(std::uint16_t MapSlot,
			const FValue& Key,
			EOpcode Operation,
			const FValue& Operand,
			bool& OutHandled,
			EJitScalarMapUpdateFailure& OutFailure);

		std::int32_t JitEnterDirectScalarRecursion(EJitScalarValueKind Kind);

	private:
		struct FScalarMethodInlineCacheEntry
		{
			const FJitFunctionEntry* PreparedEntry = nullptr;
			std::uint64_t JitCacheEpoch = 0;
			std::uint32_t CallSiteOffset = 0xFFFFFFFFu;
			std::uint16_t NameIndex = 0xFFFF;
			std::uint16_t TypeIndex = 0xFFFF;
			std::uint16_t FunctionIndex = 0xFFFF;
			std::uint8_t ArgumentCount = 0xFF;
		};

		template <typename T>
		bool JitInvokeRecursiveScalarFast(std::uint16_t FunctionIndex, const T* Arguments,
			std::uint8_t ArgumentCount, T* OutResult, bool& OutHandled, bool RootInvocation);

		template <typename T>
		bool JitInvokeScalarFast(std::uint16_t FunctionIndex, const FObjectHandle& ThisObject,
			const T* Arguments, std::uint8_t ArgumentCount, T* OutResult, bool& OutHandled,
			bool MethodCall, const FJitFunctionEntry* PreparedEntry = nullptr, bool RootInvocation = false);

		bool JitTryInvokeBoxedScalarMethodFast(std::uint16_t FunctionIndex, const FObjectHandle& Receiver,
			const FValue* Arguments, std::uint8_t ArgumentCount, bool& OutHandled);

		bool ResolveJitDirectI32FieldStorage(FScriptObject& Object, const FJitFunctionEntry& Entry,
			std::uint16_t& OutFieldIndex, std::int32_t*& OutStorage);

		void JitRefinePendingRuntimeErrorLocation(EJitRuntimeStatus Status);

		bool ResolveScriptObjectMethod(const FObjectHandle& Receiver, std::uint16_t NameIndex,
			std::uint8_t ArgumentCount, std::uint32_t CallSite, std::uint16_t& OutFunctionIndex);

		std::uint16_t ResolveObjectTypeIndex(const FScriptObject& Object) const;

		std::uint16_t FindJitFieldIndex(std::uint16_t TypeIndex, std::uint16_t NameIndex) const;

		FValue* FindPendingJitThisFieldStorage(FScriptObject& Object, std::uint16_t NameIndex,
			std::uint16_t& OutFieldIndex) const;

		VEROSCRIPT_ASMJIT_HOT_INLINE void NotifyFieldMutation(FScriptObject& Object, std::uint16_t FieldIndex);

		VEROSCRIPT_ASMJIT_HOT_INLINE void NotifyFieldMutationJitFast(FScriptObject& Object, std::uint16_t FieldIndex);

		void ReportError(std::string_view Message);

		void ReportErrorId(std::string_view Id, FSourceLocation = {},
			std::initializer_list<std::string> Arguments = {});

		void PushCallResult(FValue Value);

		VEROSCRIPT_ASMJIT_HOT_INLINE bool HasPendingControlTransfer() const noexcept
		{
			return ExecutionStatus.PendingException && ExecutionStatus.ExitRequested
				? *ExecutionStatus.PendingException || *ExecutionStatus.ExitRequested
				: Services.HasPendingControlTransfer();
		}

		bool IsExitRequested() const noexcept
		{
			return ExecutionStatus.ExitRequested ? *ExecutionStatus.ExitRequested : Services.IsExitRequested();
		}

		std::size_t GetLogicalCallDepth() const noexcept;

		std::size_t CurrentLocalCount() const noexcept;

		bool SetCallResult(bool Ok) noexcept;

		void AcquireScratch(std::size_t Capacity, std::vector<FValue>& Out, bool Argument);

		std::array<FScalarMethodInlineCacheEntry, 16> ScalarMethodIc{};
		std::array<std::uint8_t, 4> ScalarMethodIcNextWay{};
		const std::uint64_t JitCacheEpoch = 1;
		const FBytecodeModule* ActiveModule;
		std::uint64_t ProgramGeneration;
		FJitExecutionStatusView ExecutionStatus;
		FJitCallDepthBudget DepthBudget;
		std::size_t& ActiveJitCallDepth;
		std::size_t MaxJitCallDepth;
		std::size_t MaxDirectScalarCallDepth;
		std::size_t CallDepthLimit;

		FJitCallContext& Context;
		IJitRuntimeServices& Services;
		IJitFrameServices* Frames;
		IJitValueServices* Values;
		IJitCallServices* Calls;
		IJitScalarServices* Scalar;
		FJitCodeCache* JitCache;
		FJitStats& Stats;
		std::vector<std::vector<FValue>>& ScratchPool;
		FJitFrameToken Frame;
		FJitLocalStorageView DirectLocals;
		FValue CallResult = std::int32_t{0};
		bool bHasCallResult = false;
		std::size_t ActiveInstructionOffset = 0;
		const FValue* PendingJitArguments = nullptr;
		std::uint8_t PendingJitArgumentCount = 0;
		FObjectHandle PendingJitThis;
		std::uint16_t PendingJitFunctionIndex = 0xFFFF;
		const FBytecodeFunction* PendingJitLinearFunction = nullptr;
		const JitLinear::FTypedLoopPlanSet* PendingJitTypedLoopPlans = nullptr;
		const std::uint32_t* PendingJitRuntimeErrorInstructionOffsets = nullptr;
		const FDebugLine* PendingJitRuntimeDebugLines = nullptr;
		std::size_t PendingJitRuntimeDebugLineCount = 0;
		std::int32_t PendingJitPayloadI32 = 0;
		double PendingJitPayloadF64 = 0.0;
		void* PendingJitNativeCode = nullptr;
		std::uint16_t PendingJitStackCapacityHint = 0;
		std::uint16_t PendingJitNativeLocalCount = 0;
		std::uint16_t PendingJitResolvedThisFieldOwnerTypeIndex = 0xFFFF;
		const std::uint32_t* PendingJitResolvedThisFields = nullptr;
		std::size_t PendingJitResolvedThisFieldCount = 0;
		FScriptObject* PendingJitDirectThisFieldObject = nullptr;
		FValue* PendingJitDirectThisFieldStorage = nullptr;
		std::uint16_t PendingJitDirectThisFieldNameIndex = 0xFFFF;
		std::uint16_t PendingJitDirectThisFieldIndex = 0xFFFF;
		bool bPendingJitRequiresScalarNativeGuard = false;
		bool bPendingJitUseScalarMapStorage = false;
		bool bPendingJitUseScalarSetStorage = false;
		bool bPendingJitUseScalarArrayStorage = false;
		bool bPendingJitUseI32ModuloAccumulatorLoop = false;
		bool bPendingJitMayHaveDirectScalarMethodStore = false;
		bool bPendingJitMayHaveScalarMethodAccumulator = false;
		bool bPendingJitMayHaveBoundScalarMethodCall = false;
		const std::uint8_t* PendingJitScalarSetCallKinds = nullptr;
		std::size_t PendingJitScalarSetCallKindCount = 0;
		const std::uint8_t* PendingJitInvariantLocalCompareSites = nullptr;
		std::size_t PendingJitInvariantLocalCompareSiteCount = 0;
		const std::uint32_t* PendingJitScalarNativeGuards = nullptr;
		std::size_t PendingJitScalarNativeGuardCount = 0;
		EJitScalarAbi PendingJitScalarAbi = EJitScalarAbi::None;
		EJitScalarValueKind PendingJitScalarReturnKind = EJitScalarValueKind::None;
	};
}

#include "Internal/VeroScriptAsmJitExecutionContext.inl"

#undef VEROSCRIPT_ASMJIT_HOT_INLINE
