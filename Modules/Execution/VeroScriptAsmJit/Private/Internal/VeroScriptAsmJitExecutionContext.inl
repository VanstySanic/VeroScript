// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <utility>

namespace VeroScript::AsmJit
{
	inline FExecutionContext::FExecutionContext(FJitCallContext& InContext, const FJitFunctionEntry& Entry,
		FJitCodeCache& Cache, FJitStats& InStats, std::vector<std::vector<FValue>>& Scratch)
		: ActiveModule(InContext.Program->Module), ProgramGeneration(InContext.Program->Identity.Generation),
		  ExecutionStatus(InContext.Services->BorrowExecutionStatus()),
		  DepthBudget(InContext.Services->GetCallServices()->BorrowCallDepthBudget()),
		  ActiveJitCallDepth(*DepthBudget.ActiveDepth), MaxJitCallDepth(DepthBudget.MaximumBackendDepth),
		  MaxDirectScalarCallDepth(DepthBudget.MaximumDirectScalarDepth
			  ? DepthBudget.MaximumDirectScalarDepth : DepthBudget.MaximumBackendDepth),
		  CallDepthLimit(DepthBudget.MaximumTotalDepth), Context(InContext), Services(*Context.Services), Frames(Services.GetFrameServices()),
		  Values(Services.GetValueServices()), Calls(Services.GetCallServices()),
		  Scalar(Services.GetScalarServices()), JitCache(&Cache), Stats(InStats), ScratchPool(Scratch)
	{
		PendingJitArguments = Context.Arguments;
		PendingJitArgumentCount = Context.ArgumentCount;
		PendingJitThis = Context.Receiver;
		PendingJitFunctionIndex = Context.FunctionIndex;
		PendingJitLinearFunction = Entry.LinearFunction.get();
		PendingJitTypedLoopPlans = Entry.TypedLoopPlans.get();
		PendingJitRuntimeErrorInstructionOffsets =
			Entry.RuntimeErrorInstructionOffsets.data();
		PendingJitRuntimeDebugLines = Entry.RuntimeDebugLines.empty()
			? nullptr
			: Entry.RuntimeDebugLines.data();
		PendingJitRuntimeDebugLineCount = Entry.RuntimeDebugLines.size();
		PendingJitPayloadI32 = Entry.PayloadI32;
		PendingJitPayloadF64 = Entry.PayloadF64;
		PendingJitNativeCode = Entry.NativeCode;
		PendingJitStackCapacityHint = Entry.StackCapacityHint;
		PendingJitNativeLocalCount = Entry.NativeLocalCount;
		PendingJitResolvedThisFieldOwnerTypeIndex =
			Entry.ResolvedThisFieldOwnerTypeIndex;
		PendingJitResolvedThisFields = Entry.ResolvedThisFields.data();
		PendingJitResolvedThisFieldCount = Entry.ResolvedThisFields.size();
		bPendingJitRequiresScalarNativeGuard = Entry.bRequiresScalarNativeGuard;
		bPendingJitUseScalarMapStorage = Entry.bUseScalarMapStorage;
		bPendingJitUseScalarSetStorage = Entry.bUseScalarSetStorage;
		bPendingJitUseScalarArrayStorage = Entry.bUseScalarArrayStorage;
		bPendingJitUseI32ModuloAccumulatorLoop =
			Entry.bUseI32ModuloAccumulatorLoop;
		bPendingJitMayHaveDirectScalarMethodStore =
			Entry.bMayHaveDirectScalarMethodStore;
		bPendingJitMayHaveScalarMethodAccumulator =
			Entry.bMayHaveScalarMethodAccumulator;
		bPendingJitMayHaveBoundScalarMethodCall =
			Entry.bMayHaveBoundScalarMethodCall;
		PendingJitScalarSetCallKinds = Entry.ScalarSetCallKinds.data();
		PendingJitScalarSetCallKindCount = Entry.ScalarSetCallKinds.size();
		PendingJitInvariantLocalCompareSites =
			Entry.InvariantLocalCompareSites.data();
		PendingJitInvariantLocalCompareSiteCount =
			Entry.InvariantLocalCompareSites.size();
		PendingJitScalarNativeGuards = Entry.ScalarNativeCallGuards.data();
		PendingJitScalarNativeGuardCount = Entry.ScalarNativeCallGuards.size();
		PendingJitScalarAbi = Entry.ScalarAbi;
		PendingJitScalarReturnKind = Entry.ScalarReturnKind;
	}

	inline FExecutionContext::~FExecutionContext()
	{
		(void)Services.ReleaseValue(CallResult);
		if (Frame.Identity != 0 && !IsExitRequested())
		{
			(void)Frames->Leave(Frame);
		}
	}

	inline bool FExecutionContext::SetCallResult(const bool Ok) noexcept
	{
		bHasCallResult = Ok;
		return Ok;
	}

	inline void FExecutionContext::AcquireScratch(const std::size_t Capacity, std::vector<FValue>& Out, const bool Argument)
	{
		if (Argument)
		{
			++Stats.LinearArgumentScratchLeases;
		}
		const bool Reused = !ScratchPool.empty();
		if (Reused)
		{
			Out = std::move(ScratchPool.back());
			ScratchPool.pop_back();
			++Stats.LinearScratchReuses;
			if (Argument)
			{
				++Stats.LinearArgumentScratchReuses;
			}
		}
		else
		{
			++Stats.LinearScratchAllocations;
		}
		Out.reserve(Capacity);
	}

	inline std::uint8_t FExecutionContext::GetPendingJitArgumentCount() const
	{
		return PendingJitArgumentCount;
	}

	inline const FValue* FExecutionContext::GetPendingJitArguments() const
	{
		return PendingJitArguments;
	}

	inline double FExecutionContext::GetPendingJitPayloadF64() const
	{
		return PendingJitPayloadF64;
	}

	inline std::int32_t FExecutionContext::GetPendingJitPayloadI32() const
	{
		return PendingJitPayloadI32;
	}

	inline void* FExecutionContext::GetPendingJitNativeCode() const
	{
		return PendingJitNativeCode;
	}

	inline std::uint16_t FExecutionContext::GetPendingJitFunctionIndex() const
	{
		return PendingJitFunctionIndex;
	}

	inline EJitScalarAbi FExecutionContext::GetPendingJitScalarAbi() const
	{
		return PendingJitScalarAbi;
	}

	inline EJitScalarValueKind FExecutionContext::GetPendingJitScalarReturnKind() const
	{
		return PendingJitScalarReturnKind;
	}

	inline std::uint16_t FExecutionContext::GetPendingJitStackCapacityHint() const
	{
		return PendingJitStackCapacityHint;
	}

	inline std::uint16_t FExecutionContext::GetPendingJitNativeLocalCount() const
	{
		return PendingJitNativeLocalCount;
	}

	inline FValue* FExecutionContext::JitBorrowDirectLinearLocal(const std::uint16_t Slot) noexcept
	{
		if (HasError() || HasPendingControlTransfer()) return nullptr;
		return DirectLocals.Data && Slot < DirectLocals.Count
			? DirectLocals.Data + Slot : Scalar->BorrowDirectLocal(Slot);
	}

	inline bool FExecutionContext::JitBorrowLocalI32Field(std::uint16_t Slot, std::uint16_t NameIndex, FScriptObject*& OutObject, std::uint16_t& OutFieldIndex, std::int32_t*& OutStorage)
	{
		return Scalar->BorrowLocalI32Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
	}

	inline bool FExecutionContext::JitBorrowLocalU32Field(std::uint16_t Slot, std::uint16_t NameIndex, FScriptObject*& OutObject, std::uint16_t& OutFieldIndex, std::uint32_t*& OutStorage)
	{
		return Scalar->BorrowLocalU32Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
	}

	inline bool FExecutionContext::JitBorrowLocalF32Field(std::uint16_t Slot, std::uint16_t NameIndex, FScriptObject*& OutObject, std::uint16_t& OutFieldIndex, float*& OutStorage)
	{
		return Scalar->BorrowLocalF32Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
	}

	inline bool FExecutionContext::JitBorrowLocalF64Field(std::uint16_t Slot, std::uint16_t NameIndex, FScriptObject*& OutObject, std::uint16_t& OutFieldIndex, double*& OutStorage)
	{
		return Scalar->BorrowLocalF64Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
	}

	inline void FExecutionContext::JitCommitBorrowedFieldMutation(FScriptObject& Object, const std::uint16_t FieldIndex)
	{
		Scalar->CommitBorrowedFieldMutation(Object, FieldIndex);
	}

	VEROSCRIPT_ASMJIT_HOT_INLINE void FExecutionContext::NotifyFieldMutation(FScriptObject& Object, const std::uint16_t FieldIndex)
	{
		Scalar->CommitBorrowedFieldMutation(Object, FieldIndex);
		PendingJitDirectThisFieldStorage = nullptr;
		PendingJitDirectThisFieldObject = nullptr;
	}

	VEROSCRIPT_ASMJIT_HOT_INLINE void FExecutionContext::NotifyFieldMutationJitFast(FScriptObject& Object, const std::uint16_t FieldIndex)
	{
		NotifyFieldMutation(Object, FieldIndex);
	}

	inline const FBytecodeFunction* FExecutionContext::GetPendingJitLinearFunction() const
	{
		return PendingJitLinearFunction;
	}

	inline bool FExecutionContext::GetPendingJitUseScalarMapStorage() const
	{
		return bPendingJitUseScalarMapStorage;
	}

	inline bool FExecutionContext::GetPendingJitUseScalarSetStorage() const
	{
		return bPendingJitUseScalarSetStorage;
	}

	inline bool FExecutionContext::GetPendingJitUseScalarArrayStorage() const
	{
		return bPendingJitUseScalarArrayStorage;
	}

	inline bool FExecutionContext::GetPendingJitUseI32ModuloAccumulatorLoop() const
	{
		return bPendingJitUseI32ModuloAccumulatorLoop;
	}

	inline const JitLinear::FTypedLoopPlanSet* FExecutionContext::GetPendingJitTypedLoopPlans() const
	{
		return PendingJitTypedLoopPlans;
	}

	inline bool FExecutionContext::GetPendingJitRequiresScalarNativeGuard() const
	{
		return bPendingJitRequiresScalarNativeGuard;
	}

	inline bool FExecutionContext::JitCanUseScalarNativeCalls(const std::uint32_t* EncodedGuards, std::size_t GuardCount) const
	{
		return Scalar->CanUseScalarNativeCalls(EncodedGuards, GuardCount);
	}

	inline const std::uint32_t* FExecutionContext::GetPendingJitScalarNativeGuards() const
	{
		return PendingJitScalarNativeGuards;
	}

	inline std::size_t FExecutionContext::GetPendingJitScalarNativeGuardCount() const
	{
		return PendingJitScalarNativeGuardCount;
	}

	inline const FObjectHandle& FExecutionContext::GetPendingJitThis() const
	{
		return PendingJitThis;
	}

	inline bool FExecutionContext::GetPendingJitMayHaveDirectScalarMethodStore() const
	{
		return bPendingJitMayHaveDirectScalarMethodStore;
	}

	inline bool FExecutionContext::GetPendingJitMayHaveScalarMethodAccumulator() const
	{
		return bPendingJitMayHaveScalarMethodAccumulator;
	}

	inline bool FExecutionContext::GetPendingJitMayHaveBoundScalarMethodCall() const
	{
		return bPendingJitMayHaveBoundScalarMethodCall;
	}

	inline const std::uint8_t* FExecutionContext::GetPendingJitScalarSetCallKinds() const
	{
		return PendingJitScalarSetCallKinds;
	}

	inline std::size_t FExecutionContext::GetPendingJitScalarSetCallKindCount() const
	{
		return PendingJitScalarSetCallKindCount;
	}

	inline const std::uint8_t* FExecutionContext::GetPendingJitInvariantLocalCompareSites() const
	{
		return PendingJitInvariantLocalCompareSites;
	}

	inline std::size_t FExecutionContext::GetPendingJitInvariantLocalCompareSiteCount() const
	{
		return PendingJitInvariantLocalCompareSiteCount;
	}

	inline bool FExecutionContext::JitAddLocalImm(std::uint16_t Slot, std::int32_t Imm)
	{
		return Scalar->AddLocalImm(Slot, Imm);
	}

	inline bool FExecutionContext::JitAddLocalLocal(std::uint16_t DestSlot, std::uint16_t SrcSlot)
	{
		return Scalar->AddLocalLocal(DestSlot, SrcSlot);
	}

	inline bool FExecutionContext::JitTryPushDynamicArrayScalar(const FValue& Container, const FValue& Value, bool& bOutHandled)
	{
		return Scalar->TryPushDynamicArrayScalar(Container, Value, bOutHandled);
	}

	inline bool FExecutionContext::JitEnableScalarArrayStorage(FValue& Container)
	{
		return Scalar->EnableScalarArrayStorage(Container);
	}

	inline bool FExecutionContext::JitEnableScalarMapStorage(FValue& Container)
	{
		return Scalar->EnableScalarMapStorage(Container);
	}

	inline bool FExecutionContext::JitEnableScalarSetStorage(FValue& Container)
	{
		return Scalar->EnableScalarSetStorage(Container);
	}

	inline bool FExecutionContext::JitTryGetScalarMap(const FValue& Container, const FValue& Index, FValue& OutValue, bool& bOutHandled)
	{
		return Scalar->TryGetScalarMap(Container, Index, OutValue, bOutHandled);
	}

	inline bool FExecutionContext::JitTryGetDynamicArrayScalar(const FValue& Container, std::int64_t Index, FValue& OutValue, bool& bOutHandled)
	{
		return Scalar->TryGetDynamicArrayScalar(Container, Index, OutValue, bOutHandled);
	}

	inline bool FExecutionContext::JitTrySetScalarMap(FValue& Container, const FValue& Index, const FValue& Value, bool& bOutHandled)
	{
		return Scalar->TrySetScalarMap(Container, Index, Value, bOutHandled);
	}

	inline bool FExecutionContext::JitTrySetDynamicArrayScalar(FValue& Container, std::int64_t Index, const FValue& Value, bool& bOutHandled)
	{
		return Scalar->TrySetDynamicArrayScalar(Container, Index, Value, bOutHandled);
	}

	inline bool FExecutionContext::JitEvaluateCondition(const FValue& Value, bool& bOutCondition)
	{
		return Scalar->EvaluateCondition(Value, bOutCondition);
	}

	inline bool FExecutionContext::JitLocalGeImm(std::uint16_t Slot, std::int32_t Immediate, bool& bOutResult)
	{
		return Scalar->LocalGeImm(Slot, Immediate, bOutResult);
	}

	inline bool FExecutionContext::JitBindLocal(std::uint16_t Slot, FValue Value)
	{
		return Scalar->BindLocal(Slot, Value);
	}

	inline bool FExecutionContext::JitHandleLinearThrow(FValue Value, std::size_t InstructionOffset, std::size_t& OutHandlerOffset)
	{
		return Scalar->HandleLinearThrow(Value, InstructionOffset, OutHandlerOffset);
	}

	inline bool FExecutionContext::JitTryPushDynamicArrayScalarOnLocal(std::uint16_t Slot, const FValue& Value, bool& bOutHandled)
	{
		return Scalar->TryPushDynamicArrayScalarOnLocal(Slot, Value, bOutHandled);
	}

	inline bool FExecutionContext::JitTrySetI32MapAffineRangeOnLocal(std::uint16_t MapSlot, std::int32_t Begin, std::int32_t End, std::int32_t Immediate, bool& OutHandled)
	{
		return Scalar->TrySetI32MapAffineRangeOnLocal(MapSlot, Begin, End, Immediate, OutHandled);
	}

	inline bool FExecutionContext::JitTrySetScalarMapI32AddOnLocals(std::uint16_t MapSlot, std::uint16_t KeySlot, std::uint16_t ValueSlot, std::int32_t Immediate, bool& bOutHandled)
	{
		return Scalar->TrySetScalarMapI32AddOnLocals(MapSlot, KeySlot, ValueSlot, Immediate, bOutHandled);
	}

	inline bool FExecutionContext::JitTrySetScalarMapDirectFromLocals(std::uint16_t MapSlot, std::uint16_t KeySlot, std::uint16_t SourceSlot, bool& bOutHandled)
	{
		return Scalar->TrySetScalarMapDirectFromLocals(MapSlot, KeySlot, SourceSlot, bOutHandled);
	}

	inline bool FExecutionContext::JitTryAddLocalFromScalarArray(std::uint16_t DestSlot, std::uint16_t ArraySlot, const FValue& Index, bool& OutHandled)
	{
		return Scalar->TryAddLocalFromScalarArray(DestSlot, ArraySlot, Index, OutHandled);
	}

	inline bool FExecutionContext::JitTryAddLocalFromScalarMapModuloI32(std::uint16_t DestSlot, std::uint16_t MapSlot, const FValue& Key, std::int32_t Modulus, bool& OutHandled)
	{
		return Scalar->TryAddLocalFromScalarMapModuloI32(DestSlot, MapSlot, Key, Modulus, OutHandled);
	}

	inline bool FExecutionContext::JitTryGetScalarIndexOnLocal(std::uint16_t Slot, const FValue& Index, FValue& OutValue, bool& bOutMapHandled, bool& bOutArrayHandled)
	{
		return Scalar->TryGetScalarIndexOnLocal(Slot, Index, OutValue, bOutMapHandled, bOutArrayHandled);
	}

	inline bool FExecutionContext::JitTryAddLocalFromScalarMap(std::uint16_t DestSlot, std::uint16_t MapSlot, const FValue& Key, bool& OutHandled)
	{
		return Scalar->TryAddLocalFromScalarMap(DestSlot, MapSlot, Key, OutHandled);
	}

	inline bool FExecutionContext::JitTryCallScalarSetMethodOnLocal(std::uint16_t Slot, const FValue& Argument, EJitScalarSetOperation Operation, bool bDiscardReturn, FValue& OutValue, bool& OutHandled)
	{
		return Scalar->TryCallScalarSetMethodOnLocal(Slot, Argument, Operation, bDiscardReturn, OutValue, OutHandled);
	}

	inline bool FExecutionContext::JitTryInsertI32SetRangeOnLocal(std::uint16_t Slot, std::int32_t Begin, std::int32_t End, bool& OutHandled)
	{
		return Scalar->TryInsertI32SetRangeOnLocal(Slot, Begin, End, OutHandled);
	}

	inline bool FExecutionContext::JitTryCallScalarSetMethodOnLocals(std::uint16_t Slot, std::uint16_t ArgumentSlot, EJitScalarSetOperation Operation, bool bDiscardReturn, FValue& OutValue, bool& OutHandled)
	{
		return Scalar->TryCallScalarSetMethodOnLocals(Slot, ArgumentSlot, Operation, bDiscardReturn, OutValue, OutHandled);
	}

	inline bool FExecutionContext::JitAddValueToLocal(std::uint16_t DestSlot, const FValue& Source)
	{
		return Scalar->AddValueToLocal(DestSlot, Source);
	}

	inline bool FExecutionContext::JitLocalGeLocal(std::uint16_t LeftSlot, std::uint16_t RightSlot, bool& bOutResult)
	{
		return Scalar->LocalGeLocal(LeftSlot, RightSlot, bOutResult);
	}

	inline bool FExecutionContext::JitCompareLocals(EOpcode Op, std::uint16_t LeftSlot, std::uint16_t RightSlot, bool& bOutResult)
	{
		return Scalar->CompareLocals(Op, LeftSlot, RightSlot, bOutResult);
	}

	inline bool FExecutionContext::JitBorrowLocalScalarField(std::uint16_t Slot, std::uint16_t NameIndex, FScriptObject*& OutObject, std::uint16_t& OutFieldIndex, const FValue*& OutStorage)
	{
		return Scalar->BorrowLocalScalarField(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
	}

	inline bool FExecutionContext::JitTryAddLocalFromLocalField(std::uint16_t DestSlot, std::uint16_t ObjectSlot, std::uint16_t NameIndex, std::uint32_t CallSiteOffset, bool& OutHandled)
	{
		return Scalar->TryAddLocalFromLocalField(DestSlot, ObjectSlot, NameIndex, CallSiteOffset, OutHandled);
	}

	inline bool FExecutionContext::JitTrySetDynamicArrayScalarOnLocal(std::uint16_t ArraySlot, const FValue& Index, const FValue& Value, bool& bOutHandled)
	{
		return Scalar->TrySetDynamicArrayScalarOnLocal(ArraySlot, Index, Value, bOutHandled);
	}

	inline bool FExecutionContext::JitTrySetScalarMapDirectOnLocal(std::uint16_t MapSlot, const FValue& Key, const FValue& Value, bool& bOutHandled)
	{
		return Scalar->TrySetScalarMapDirectOnLocal(MapSlot, Key, Value, bOutHandled);
	}

	inline bool FExecutionContext::JitTryUpdateLocalFieldI32Modulo(std::uint16_t Slot, std::uint16_t NameIndex, const FValue& Delta, std::int32_t Modulus, std::uint32_t CallSiteOffset, bool& OutHandled)
	{
		return Scalar->TryUpdateLocalFieldI32Modulo(Slot, NameIndex, Delta, Modulus, CallSiteOffset, OutHandled);
	}

	inline bool FExecutionContext::JitLoadLocalField(std::uint16_t Slot, std::uint16_t NameIndex, FValue& OutValue)
	{
		return Scalar->LoadLocalField(Slot, NameIndex, OutValue);
	}

	inline bool FExecutionContext::JitTryCallScalarContainerMethodOnLocals(std::uint16_t Slot, std::uint16_t MethodNameIndex, std::uint16_t ArgumentSlot, bool bDiscardReturn, FValue& OutValue, bool& OutHandled)
	{
		return Scalar->TryCallScalarContainerMethodOnLocals(Slot, MethodNameIndex, ArgumentSlot, bDiscardReturn, OutValue, OutHandled);
	}

	inline bool FExecutionContext::JitTryCallScalarContainerMethodOnLocal(std::uint16_t Slot, std::uint16_t MethodNameIndex, const FValue* Arguments, std::uint8_t ArgumentCount, bool bDiscardReturn, FValue& OutValue, bool& OutHandled)
	{
		return Scalar->TryCallScalarContainerMethodOnLocal(Slot, MethodNameIndex, Arguments, ArgumentCount, bDiscardReturn, OutValue, OutHandled);
	}

	inline bool FExecutionContext::JitTryCompareStringArrayElements(std::uint16_t LeftArraySlot, std::uint16_t LeftIndexSlot, std::uint16_t RightArraySlot, std::uint16_t RightIndexSlot, bool bEqual, bool& OutResult, bool& OutHandled)
	{
		return Scalar->TryCompareStringArrayElements(LeftArraySlot, LeftIndexSlot, RightArraySlot, RightIndexSlot, bEqual, OutResult, OutHandled);
	}

	inline bool FExecutionContext::JitTryIncAndAccumulateLocalI32Field(std::uint16_t ObjectSlot, std::uint16_t NameIndex, std::uint16_t AccumulatorSlot, bool& OutHandled, EJitFieldAccumulateFailure& OutFailure)
	{
		return Scalar->TryIncAndAccumulateLocalI32Field(ObjectSlot, NameIndex, AccumulatorSlot, OutHandled, OutFailure);
	}

	inline bool FExecutionContext::JitTryIncLocalField(std::uint16_t Slot, std::uint16_t NameIndex, bool& OutHandled)
	{
		return Scalar->TryIncLocalField(Slot, NameIndex, OutHandled);
	}

	inline bool FExecutionContext::JitTryLoadScalarLocal(std::uint16_t Slot, FValue& OutValue, bool& OutHandled)
	{
		return Scalar->TryLoadScalarLocal(Slot, OutValue, OutHandled);
	}

	inline bool FExecutionContext::JitTryLoadScalarGlobal(std::uint16_t Slot, FValue& OutValue, bool& OutHandled)
	{
		return Scalar->TryLoadScalarGlobal(Slot, OutValue, OutHandled);
	}

	inline bool FExecutionContext::JitTryCheckedScalarCast(const FValue& Value, std::uint16_t TargetTypeNameIndex, FValue& OutResult, bool& OutHandled)
	{
		return Scalar->TryCheckedScalarCast(Value, TargetTypeNameIndex, OutResult, OutHandled);
	}

	inline bool FExecutionContext::JitTryScalarBuiltinConversion(EBuiltinId Builtin, const FValue& Value, FValue& OutResult, bool& OutHandled)
	{
		return Scalar->TryScalarBuiltinConversion(Builtin, Value, OutResult, OutHandled);
	}

	inline bool FExecutionContext::JitTryUpdateScalarMapEntryOnLocal(std::uint16_t MapSlot, const FValue& Key, EOpcode Operation, const FValue& Operand, bool& OutHandled, EJitScalarMapUpdateFailure& OutFailure)
	{
		return Scalar->TryUpdateScalarMapEntryOnLocal(MapSlot, Key, Operation, Operand, OutHandled, OutFailure);
	}

	inline const FBytecodeModule* FExecutionContext::DebugGetModule() const
	{
		return Context.Program->Module;
	}

	inline const FObjectHandle& FExecutionContext::GetJitThisObject() const
	{
		return PendingJitThis;
	}

	inline bool FExecutionContext::WasExitRequested() const
	{
		return IsExitRequested();
	}

	VEROSCRIPT_ASMJIT_HOT_INLINE bool FExecutionContext::JitHasInstanceCallInterceptor() const noexcept
	{
		return ExecutionStatus.InstanceInterceptor ? *ExecutionStatus.InstanceInterceptor : Calls->HasInstanceInterceptor();
	}

	VEROSCRIPT_ASMJIT_HOT_INLINE bool FExecutionContext::JitHasMutationCallback() const noexcept
	{
		return ExecutionStatus.MutationObserver ? *ExecutionStatus.MutationObserver : Calls->HasMutationObserver();
	}

	VEROSCRIPT_ASMJIT_HOT_INLINE bool FExecutionContext::JitCanUseDirectScalarMethod() const noexcept
	{
		return !JitHasInstanceCallInterceptor() && !JitHasMutationCallback()
			&& ActiveJitCallDepth < MaxJitCallDepth
			&& ActiveJitCallDepth < CallDepthLimit && !HasPendingControlTransfer() && !HasError();
	}

	inline bool FExecutionContext::JitLoadGlobal(std::uint16_t Slot, FValue& OutValue)
	{
		return Frames->LoadGlobal(Slot, OutValue);
	}

	inline bool FExecutionContext::JitStoreGlobal(std::uint16_t Slot, FValue Value)
	{
		return Frames->StoreGlobal(Slot, Value);
	}

	inline bool FExecutionContext::JitLoadLocal(std::uint16_t Slot, FValue& OutValue)
	{
		return Frames->LoadLocal(Frame, Slot, OutValue);
	}

	inline bool FExecutionContext::JitStoreLocal(std::uint16_t Slot, FValue Value)
	{
		return Frames->StoreLocal(Frame, Slot, Value);
	}

	inline bool FExecutionContext::JitTakeLocal(std::uint16_t Slot, FValue& OutValue)
	{
		auto* Local = Frames->BorrowLocal(Frame, Slot);
		if (!Local) return false;
		OutValue = std::move(*Local);
		*Local = std::int32_t{0};
		return true;
	}

	inline bool FExecutionContext::JitMakeLocalRef(std::uint16_t Slot, FValue& OutRef)
	{
		return Frames->MakeLocalReference(Frame, Slot, OutRef);
	}

	inline bool FExecutionContext::JitMakeGlobalRef(std::uint16_t Slot, FValue& OutRef)
	{
		return Frames->MakeGlobalReference(Slot, OutRef);
	}

	inline bool FExecutionContext::JitEvalArithmetic(EOpcode Op, const FValue& Left, const FValue& Right, FValue& OutResult)
	{
		return Frames->EvaluateArithmetic(Op, Left, Right, OutResult);
	}

	inline bool FExecutionContext::JitEvalBitwise(EOpcode Op, const FValue& Left, const FValue& Right, FValue& OutResult)
	{
		return Frames->EvaluateBitwise(Op, Left, Right, OutResult);
	}

	inline bool FExecutionContext::JitEvalUnary(EOpcode Op, const FValue& Operand, FValue& OutResult)
	{
		return Frames->EvaluateUnary(Op, Operand, OutResult);
	}

	inline bool FExecutionContext::JitCompare(EOpcode Op, const FValue& Left, const FValue& Right, bool& OutResult)
	{
		return Frames->Compare(Op, Left, Right, OutResult);
	}

	inline bool FExecutionContext::JitNewObject(std::uint16_t TypeIndex, FValue& OutObject, const bool bUninitialized)
	{
		return Values->CreateObject(TypeIndex, OutObject, bUninitialized);
	}

	inline bool FExecutionContext::JitNewContainer(EOpcode Op, std::uint16_t TypeNameIndex, FValue& Out)
	{
		return Values->CreateContainer(Op, TypeNameIndex, Out);
	}

	inline bool FExecutionContext::JitCloneStruct(FValue& InOutValue)
	{
		return Values->CloneStruct(InOutValue);
	}

	inline bool FExecutionContext::JitLoadField(const FValue& Object, std::uint16_t NameIndex, FValue& OutValue)
	{
		return Values->LoadField(Object, NameIndex, OutValue);
	}

	inline bool FExecutionContext::JitStoreField(const FValue& Object, std::uint16_t NameIndex, const FValue& Value)
	{
		return Values->StoreField(Object, NameIndex, Value);
	}

	inline bool FExecutionContext::JitLoadThisField(std::uint16_t NameIndex, FValue& OutValue)
	{
		return Values->LoadField(FValue{PendingJitThis}, NameIndex, OutValue);
	}

	inline bool FExecutionContext::JitStoreThisField(std::uint16_t NameIndex, FValue Value)
	{
		return Values->StoreField(FValue{PendingJitThis}, NameIndex, Value);
	}

	inline bool FExecutionContext::JitInitializeThisField(std::uint16_t NameIndex, FValue Value)
	{
		return Values->InitializeField(FValue{PendingJitThis}, NameIndex, Value);
	}

	inline bool FExecutionContext::JitMakeThisFieldRef(std::uint16_t NameIndex, FValue& OutRef)
	{
		return Values->MakeFieldReference(FValue{PendingJitThis}, NameIndex, OutRef);
	}

	inline bool FExecutionContext::JitMakeFieldRef(FValue Object, std::uint16_t NameIndex, FValue& OutRef)
	{
		return Values->MakeFieldReference(Object, NameIndex, OutRef);
	}

	inline bool FExecutionContext::JitIncField(FValue Object, std::uint16_t NameIndex)
	{
		return Values->IncrementField(Object, NameIndex);
	}

	inline bool FExecutionContext::JitGetIndex(const FValue& Container, const FValue& Index, FValue& OutValue)
	{
		return Values->GetIndex(Container, Index, OutValue);
	}

	inline bool FExecutionContext::JitSetIndex(FValue Container, const FValue& Index, const FValue& Value, FValue& OutContainer)
	{
		return Values->SetIndex(Container, Index, Value, OutContainer);
	}

	inline bool FExecutionContext::JitGetIndexRef(std::uint16_t ArrayLocalSlot, FValue Index, FValue& OutRef, bool bBorrow)
	{
		return Values->MakeIndexReference(Frame, ArrayLocalSlot, Index, OutRef, bBorrow);
	}

	inline bool FExecutionContext::JitGetMapEntryRef(std::uint16_t MapLocalSlot, FValue Key, FValue& OutRef)
	{
		return Values->MakeMapEntryReference(Frame, MapLocalSlot, Key, OutRef);
	}

	inline bool FExecutionContext::JitInvokeBuiltin(EBuiltinId Builtin, const FValue* Arguments, std::uint8_t ArgumentCount, bool& bOutHasResult, FValue& OutResult)
	{
		return Calls->InvokeBuiltin(Builtin, Arguments, ArgumentCount, bOutHasResult, OutResult);
	}

	inline bool FExecutionContext::JitCallNative(std::uint16_t NativeIndex, const FValue* Arguments, std::uint8_t ArgumentCount, FValue& OutReturn)
	{
		return Calls->InvokeNative(NativeIndex, Arguments, ArgumentCount, OutReturn);
	}

	inline bool FExecutionContext::JitMakeClosure(std::uint16_t FunctionIndex, const FValue* Captures, std::uint8_t CaptureCount, FValue& OutClosure)
	{
		return Calls->MakeClosure(FunctionIndex, Captures, CaptureCount, OutClosure);
	}

	inline bool FExecutionContext::JitInvokeCall(std::uint16_t FunctionIndex, const FValue* Arguments, std::uint8_t ArgumentCount)
	{
		// Native scalar callers have no runtime frame to update before this
		// boundary. Reject an exhausted logical depth here so their structured
		// failure site can locate the call, before Core pins a stale frame offset.
		if (ActiveModule && FunctionIndex < ActiveModule->Functions.size() && Calls->GetRemainingCallDepth() == 0u)
		{
			ReportErrorId("diag.call_stack_overflow");
			return SetCallResult(false);
		}
		bool Handled = false;
		const bool Ok = JitTryInvokeReadonlyTextBuiltin(FunctionIndex, Arguments, ArgumentCount, Handled);
		if (!Ok || Handled) return SetCallResult(Ok);
		return SetCallResult(Services.InvokeFunction(FunctionIndex, {}, Arguments, ArgumentCount, CallResult));
	}

	inline bool FExecutionContext::JitInvokeBoundMethod(std::uint16_t FunctionIndex, const FValue& ThisObject, const FValue* Arguments, std::uint8_t ArgumentCount)
	{
		const auto *Receiver = std::get_if<FObjectHandle>(&ThisObject);
		if (!Receiver || !*Receiver)
		{
			Services.ReportError("JIT bound method call requires object");
			return false;
		}
		bool Handled = false;
		if (!JitTryInvokeBoxedScalarMethodFast(FunctionIndex, *Receiver, Arguments, ArgumentCount, Handled)) return false;
		if (Handled) return true;
		return SetCallResult(Services.InvokeFunction(FunctionIndex, *Receiver, Arguments, ArgumentCount, CallResult));
	}

	inline bool FExecutionContext::JitCallIndirect(const FValue& Callee, const FValue* Arguments, std::uint8_t ArgumentCount)
	{
		return SetCallResult(Calls->InvokeIndirect(Callee, Arguments, ArgumentCount, CallResult));
	}

	inline bool FExecutionContext::JitCallMethod(std::uint16_t MethodNameIndex, const FValue& ThisObject, const FValue* Arguments, std::uint8_t ArgumentCount, std::uint32_t CallSiteOffset)
	{
		bool Handled = false;
		const bool Ok = JitTryCallScalarMethodOnValue(ThisObject, MethodNameIndex, Arguments,
			ArgumentCount, CallSiteOffset, CallResult, Handled);
		if (!Ok || Handled) return SetCallResult(Ok);
		return SetCallResult(Calls->InvokeMethod(MethodNameIndex, ThisObject, Arguments, ArgumentCount, CallSiteOffset, CallResult));
	}

	inline bool FExecutionContext::JitReleaseOwnedValue(FValue& Value)
	{
		return Services.ReleaseValue(Value);
	}

	inline bool FExecutionContext::JitReleaseOwnedValues(std::vector<FValue>& OwnedValues)
	{
		bool Ok = true;
		for (auto It = OwnedValues.rbegin(); It != OwnedValues.rend(); ++It)
		{
			Ok = Services.ReleaseValue(*It) && Ok;
		}
		OwnedValues.clear();
		return Ok;
	}

	inline bool FExecutionContext::JitEnterLinearFrame(std::size_t LocalCount, const FValue* Arguments, std::uint8_t ArgumentCount, const FObjectHandle& ThisObject)
	{
		const auto *Body = PendingJitLinearFunction ? PendingJitLinearFunction : &Context.Program->Module->Functions[Context.FunctionIndex];
		if (Body->LocalCount != LocalCount)
		{
			Services.ReportError("JIT frame layout mismatch");
			return false;
		}
		if (!Frames->Enter({Context.FunctionIndex, Body}, Arguments, ArgumentCount, ThisObject, Frame)) return false;
		DirectLocals = Frames->BorrowDirectLocalStorage(Frame);
		return !HasError();
	}

	inline bool FExecutionContext::JitLeaveLinearFrame()
	{
		const auto Token = Frame;
		DirectLocals = {};
		Frame = {};
		return Frames->Leave(Token);
	}

	inline void FExecutionContext::JitSetActiveInstructionOffset(const std::size_t InstructionOffset) noexcept
	{
		ActiveInstructionOffset = InstructionOffset;
		if (Frame.Identity != 0)
		{
			(void)Frames->SetInstructionOffset(Frame, InstructionOffset);
		}
	}

	inline void FExecutionContext::JitRefineRuntimeErrorLocation(std::size_t InstructionOffset)
	{
		Services.RefineErrorLocation(PendingJitFunctionIndex, InstructionOffset, PendingJitRuntimeDebugLines, PendingJitRuntimeDebugLineCount);
	}

	inline bool FExecutionContext::JitTakeReturn(FValue& OutValue)
	{
		if (!bHasCallResult || HasPendingControlTransfer()) return false;
		if (!Services.ReleaseValue(OutValue)) return false;
		OutValue = std::move(CallResult);
		CallResult = std::int32_t{0};
		bHasCallResult = false;
		return true;
	}

	inline void FExecutionContext::ReportNativeFailure() noexcept
	{
		Services.ReportError("native JIT helper threw an exception");
	}

	inline bool FExecutionContext::PrepareReturn(FValue& Value)
	{
		return Services.PrepareReturn(Context.Program->Module->Functions[Context.FunctionIndex], Value);
	}

	inline bool FExecutionContext::JitPushReturn(FValue Value)
	{
		if (Context.bHasReturn || !Services.PrepareReturn(Context.Program->Module->Functions[Context.FunctionIndex], Value))
		{
			(void)Services.ReleaseValue(Value);
			return false;
		}
		Context.ReturnValue = std::move(Value);
		Context.bHasReturn = true;
		return !HasError();
	}

	inline bool FExecutionContext::JitInterpretCurrentFunction()
	{
		++Stats.VmTrampolineExecutions;
		Context.ExecutionKind = EJitExecutionKind::VmTrampoline;
		if (Context.FallbackReason == EJitFallbackReason::None)
		{
			Context.FallbackReason = EJitFallbackReason::RuntimeGuard;
		}
		FValue Value;
		if (!Calls->InterpretInvocation(Context.Arguments, Context.ArgumentCount, Context.Receiver, Value)) return false;
		if (Context.Program->Module->Functions[Context.FunctionIndex].bReturnsVoid) return Services.ReleaseValue(Value);
		return JitPushReturn(std::move(Value));
	}

	inline void FExecutionContext::JitAcquireLinearScratch(std::size_t CapacityHint, std::vector<FValue>& OutScratch)
	{
		AcquireScratch(CapacityHint, OutScratch, false);
	}

	inline void FExecutionContext::JitAcquireLinearArgumentScratch(std::size_t CapacityHint, std::vector<FValue>& OutScratch)
	{
		AcquireScratch(CapacityHint, OutScratch, true);
	}

	inline void FExecutionContext::JitRecycleLinearScratch(std::vector<FValue>& Scratch)
	{
		if (Scratch.empty() && Scratch.capacity() <= 4096 && ScratchPool.size() < 16)
		{
			ScratchPool.push_back(std::move(Scratch));
		}
	}

	inline void FExecutionContext::RecordJitArrayFastPathGet(const std::size_t Count)
	{
		Stats.ArrayFastPathGets += Count;
	}

	inline void FExecutionContext::RecordJitArrayFastPathSet(const std::size_t Count)
	{
		Stats.ArrayFastPathSets += Count;
	}

	inline void FExecutionContext::RecordJitArrayFastPathPush(const std::size_t Count)
	{
		Stats.ArrayFastPathPushes += Count;
	}

	inline void FExecutionContext::RecordJitScalarMapFastPathGet(const std::size_t Count)
	{
		Stats.ScalarMapFastPathGets += Count;
	}

	inline void FExecutionContext::RecordJitScalarMapFastPathSet(const std::size_t Count)
	{
		Stats.ScalarMapFastPathSets += Count;
	}

	inline void FExecutionContext::RecordJitNativeTypedLoopExecution()
	{
		Stats.NativeTypedLoopExecutions += 1;
	}

}
