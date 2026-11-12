// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitExecutionContext.h"
#include "VeroScriptAsmJitScalarPacking.h"
#include "VeroScriptJitNative.h"
#include "VeroScriptJitNativeLinearHelpers.h"
#include "VeroScriptJitRawScratch.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace VeroScript::AsmJit
{
	namespace
	{
		bool RequiresCoreLifetimeMethodCall(const FBytecodeFunction& Function, const FObjectHandle& Receiver)
		{
			return Receiver && Function.bIsMethod && ((Receiver->bConstructionIncomplete && Function.bReturnsVoid) || ((Receiver->bConstructionIncomplete || Receiver->bDestroying) && Function.bIsVirtual));
		}
	}

	void FExecutionContext::JitReportRuntimeError(std::string Message)
	{
		ReportError(std::move(Message));
	}

	bool FExecutionContext::JitTryInvokeReadonlyTextBuiltin(const std::uint16_t FunctionIndex,
		const FValue* Arguments, const std::uint8_t ArgumentCount, bool& OutHandled)
	{
		OutHandled = false;
		if (ArgumentCount != 2u || !Arguments || !JitCache || !ActiveModule ||
			FunctionIndex >= ActiveModule->Functions.size() || !JitCanUseDirectScalarMethod() ||
			Calls->GetRemainingCallDepth() == 0u || !std::holds_alternative<std::string>(Arguments[0]) ||
			!std::holds_alternative<std::string>(Arguments[1]))
		{
			return true;
		}
		// Only Core-published dependencies may be used. An uncached call follows
		// ordinary dispatch, preserving cumulative budgets and Mixed hotness.
		const auto* Entry = JitCache->TryGet(FunctionIndex);
		if (!Entry || Entry->Status != EJitCompileStatus::Ok || Entry->ReadonlyTextBuiltin == 0xFFu) return true;
		OutHandled = true;
		if (!Calls->EnterOptimizedCall()) return false;
		struct FCallScope
		{
			IJitCallServices& Calls;
			~FCallScope() {Calls.LeaveOptimizedCall();}
		} Scope{*Calls};
		++Stats.DispatchAttempts;
		++Stats.Dispatches;
		bool HasResult = false;
		const bool Ok = JitInvokeBuiltin(static_cast<EBuiltinId>(Entry->ReadonlyTextBuiltin),
			Arguments, ArgumentCount, HasResult, CallResult);
		if (!Ok || !HasResult)
		{
			if (Ok)
			{
				Services.ReportError("JIT readonly text builtin has no result");
			}
			Services.RefineErrorLocation(FunctionIndex, Entry->ReadonlyTextBuiltinOffset, nullptr, 0u);
			return false;
		}
		return true;
	}

	void FExecutionContext::JitReportRuntimeError(
	    std::string Message,
	    const EJitRuntimeStatus Status)
	{
		JitReportRuntimeError(
		    std::move(Message), FJitKernelFailure{Status, InvalidJitInstructionOffset});
	}

	void FExecutionContext::JitReportRuntimeError(
	    std::string Message,
	    const FJitKernelFailure Failure)
	{
		JitReportRuntimeError(std::move(Message));
		if (Failure.InstructionOffset != InvalidJitInstructionOffset)
		{
			JitRefineRuntimeErrorLocation(Failure.InstructionOffset);
			return;
		}
		JitRefinePendingRuntimeErrorLocation(Failure.Status);
	}

	bool FExecutionContext::JitHandleKernelFailure(const std::int32_t EncodedFailure)
	{
		const FJitKernelFailure Failure = DecodeJitKernelFailure(EncodedFailure);
		if (!Failure.HasFailure())
		{
			return true;
		}
		if (HasError())
		{
			// A helper may already have produced a richer diagnostic. Preserve its
			// message while still consuming the structured kernel location. Nested
			// JIT errors pin their own location, so an outer call site cannot replace it.
			if (Failure.InstructionOffset != InvalidJitInstructionOffset)
			{
				JitRefineRuntimeErrorLocation(Failure.InstructionOffset);
			}
			else
			{
				JitRefinePendingRuntimeErrorLocation(Failure.Status);
			}
			return false;
		}
		const char* Message = "JIT native: kernel failed";
		switch (Failure.Status)
		{
		case EJitRuntimeStatus::IntegerOverflow:
			{
				Message = "integer overflow";
				break;
			}
		case EJitRuntimeStatus::DivisionByZero:
			{
				Message = "division by zero";
				break;
			}
		case EJitRuntimeStatus::ShiftOutOfRange:
			{
				Message = "shift count out of range";
				break;
			}
		case EJitRuntimeStatus::ToUnsignedOverflow:
			{
				Message = "to_unsigned overflow";
				break;
			}
		case EJitRuntimeStatus::ToIntOverflow:
			{
				Message = "to_int overflow";
				break;
			}
		default:
			{
				break;
			}
		}
		JitReportRuntimeError(Message, Failure);
		return false;
	}

	bool FExecutionContext::JitInvokeFunctionU32Fast(
	    const std::uint16_t FunctionIndex,
	    const std::uint32_t* const Arguments,
	    const std::uint8_t ArgumentCount,
	    std::uint32_t* const OutResult,
	    bool& bOutHandled, const bool bRootInvocation)
	{
		return JitInvokeScalarFast(
		    FunctionIndex, {}, Arguments, ArgumentCount, OutResult, bOutHandled, false, nullptr, bRootInvocation);
	}

	bool FExecutionContext::JitInvokeFunctionI32Fast(
	    const std::uint16_t FunctionIndex,
	    const std::int32_t* const Arguments,
	    const std::uint8_t ArgumentCount,
	    std::int32_t* const OutResult,
	    bool& bOutHandled, const bool bRootInvocation)
	{
		return JitInvokeScalarFast(
		    FunctionIndex, {}, Arguments, ArgumentCount, OutResult, bOutHandled, false, nullptr, bRootInvocation);
	}

	bool FExecutionContext::JitInvokeFunctionF64Fast(
	    const std::uint16_t FunctionIndex,
	    const double* const Arguments,
	    const std::uint8_t ArgumentCount,
	    double* const OutResult,
	    bool& bOutHandled, const bool bRootInvocation)
	{
		return JitInvokeScalarFast(
		    FunctionIndex, {}, Arguments, ArgumentCount, OutResult, bOutHandled, false, nullptr, bRootInvocation);
	}

	bool FExecutionContext::JitInvokeBoundMethodI32Fast(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const std::int32_t* const Arguments,
	    const std::uint8_t ArgumentCount,
	    std::int32_t* const OutResult,
	    bool& bOutHandled)
	{
		return JitInvokeScalarFast(
		    FunctionIndex,
		    ThisObject,
		    Arguments,
		    ArgumentCount,
		    OutResult,
		    bOutHandled,
		    true);
	}

	bool FExecutionContext::JitInvokeBoundMethodU32Fast(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const std::uint32_t* const Arguments,
	    const std::uint8_t ArgumentCount,
	    std::uint32_t* const OutResult,
	    bool& bOutHandled)
	{
		return JitInvokeScalarFast(
		    FunctionIndex,
		    ThisObject,
		    Arguments,
		    ArgumentCount,
		    OutResult,
		    bOutHandled,
		    true);
	}

	bool FExecutionContext::JitInvokeBoundMethodF64Fast(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const double* const Arguments,
	    const std::uint8_t ArgumentCount,
	    double* const OutResult,
	    bool& bOutHandled)
	{
		return JitInvokeScalarFast(
		    FunctionIndex,
		    ThisObject,
		    Arguments,
		    ArgumentCount,
		    OutResult,
		    bOutHandled,
		    true);
	}

	bool FExecutionContext::JitLoadThisFieldI32(
	    const std::uint16_t NameIndex,
	    std::int32_t& OutValue)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field access outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const Resolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		if (!Resolved)
		{
			ReportError("JIT native this-field: invalid reference storage");
			return false;
		}
		const FValue& Slot = *Resolved;
		if (const auto* IntValue = std::get_if<std::int32_t>(&Slot))
		{
			OutValue = *IntValue;
			return true;
		}
		if (const auto* BoolValue = std::get_if<bool>(&Slot))
		{
			OutValue = *BoolValue ? 1 : 0;
			return true;
		}
		ReportError("JIT native this-field: expected int/bool");
		return false;
	}

	bool FExecutionContext::JitStoreThisFieldI32(
	    const std::uint16_t NameIndex,
	    const std::int32_t Value)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field assignment outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const Resolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		if (!Resolved)
		{
			ReportError("JIT native this-field: invalid reference storage");
			return false;
		}
		FValue& Slot = *Resolved;
		if (auto* BoolValue = std::get_if<bool>(&Slot))
		{
			*BoolValue = Value != 0;
			NotifyFieldMutation(*ThisObject, FieldIndex);
			return true;
		}
		if (auto* IntValue = std::get_if<std::int32_t>(&Slot))
		{
			*IntValue = Value;
			NotifyFieldMutation(*ThisObject, FieldIndex);
			return true;
		}
		ReportError("JIT native this-field: expected int/bool");
		return false;
	}

	bool FExecutionContext::JitInitializeThisFieldI32(
	    const std::uint16_t NameIndex,
	    const std::int32_t Value)
	{
		const auto& Receiver = GetJitThisObject();
		if (Receiver && ActiveModule)
		{
			const auto TypeIndex = ResolveObjectTypeIndex(*Receiver);
			if (TypeIndex < ActiveModule->Types.size())
			{
				const auto FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
				if (FieldIndex < ActiveModule->Types[TypeIndex].Fields.size() && ActiveModule->Types[TypeIndex].Fields[FieldIndex].TypeName == "bool")
				{
					return JitInitializeThisField(NameIndex, FValue{Value != 0});
				}
			}
		}
		return JitInitializeThisField(NameIndex, FValue{Value});
	}

	bool FExecutionContext::JitLoadThisFieldU32(
	    const std::uint16_t NameIndex,
	    std::uint32_t& OutValue)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field access outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const Resolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		const auto* const Value = Resolved
		    ? std::get_if<std::uint32_t>(Resolved)
		    : nullptr;
		if (!Value)
		{
			ReportError("JIT native this-field: expected uint");
			return false;
		}
		OutValue = *Value;
		return true;
	}

	bool FExecutionContext::JitStoreThisFieldU32(
	    const std::uint16_t NameIndex,
	    const std::uint32_t Value)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field assignment outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const Resolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		auto* const Slot = Resolved ? std::get_if<std::uint32_t>(Resolved) : nullptr;
		if (!Slot)
		{
			ReportError("JIT native this-field: expected uint");
			return false;
		}
		*Slot = Value;
		NotifyFieldMutation(*ThisObject, FieldIndex);
		return true;
	}

	bool FExecutionContext::JitInitializeThisFieldU32(
	    const std::uint16_t NameIndex,
	    const std::uint32_t Value)
	{
		return JitInitializeThisField(NameIndex, FValue{Value});
	}

	bool FExecutionContext::JitLoadThisFieldF64(
	    const std::uint16_t NameIndex,
	    double& OutValue)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field access outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const Resolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		const auto* const Value = Resolved ? std::get_if<double>(Resolved) : nullptr;
		if (!Value)
		{
			ReportError("JIT native this-field: expected double");
			return false;
		}
		OutValue = *Value;
		return true;
	}

	bool FExecutionContext::JitStoreThisFieldF64(
	    const std::uint16_t NameIndex,
	    const double Value)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field assignment outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const Resolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		auto* const Slot = Resolved ? std::get_if<double>(Resolved) : nullptr;
		if (!Slot)
		{
			ReportError("JIT native this-field: expected double");
			return false;
		}
		*Slot = Value;
		NotifyFieldMutation(*ThisObject, FieldIndex);
		return true;
	}

	bool FExecutionContext::JitInitializeThisFieldF64(
	    const std::uint16_t NameIndex,
	    const double Value)
	{
		return JitInitializeThisField(NameIndex, FValue{Value});
	}

	bool FExecutionContext::JitInvokeFunctionMixedFast(
	    const std::uint16_t FunctionIndex,
	    const std::uint64_t* const Arguments,
	    const std::uint8_t ArgumentCount,
	    std::uint64_t* const OutResult,
	    bool& bOutHandled)
	{
		return JitInvokeScalarFast(
		    FunctionIndex, {}, Arguments, ArgumentCount, OutResult, bOutHandled, false);
	}

	bool FExecutionContext::JitInvokeBoundMethodMixedFast(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const std::uint64_t* const Arguments,
	    const std::uint8_t ArgumentCount,
	    std::uint64_t* const OutResult,
	    bool& bOutHandled)
	{
		return JitInvokeScalarFast(
		    FunctionIndex,
		    ThisObject,
		    Arguments,
		    ArgumentCount,
		    OutResult,
		    bOutHandled,
		    true);
	}

	bool FExecutionContext::JitLoadThisFieldMixed(
	    const std::uint16_t NameIndex,
	    const EJitScalarValueKind Kind,
	    std::uint64_t& OutValue)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field access outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const MutableResolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		const FValue* const Resolved = MutableResolved;
		if (!Resolved || !JitBridgeDetail::PackMixedScalar(Kind, *Resolved, OutValue))
		{
			ReportError("JIT mixed this-field: scalar type mismatch");
			return false;
		}
		return true;
	}

	bool FExecutionContext::JitStoreThisFieldMixed(
	    const std::uint16_t NameIndex,
	    const EJitScalarValueKind Kind,
	    const std::uint64_t Value)
	{
		const FObjectHandle& ThisObject = GetJitThisObject();
		if (!ThisObject || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT this field assignment outside method");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* const Resolved = FindPendingJitThisFieldStorage(
		    *ThisObject, NameIndex, FieldIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= ThisObject->Fields.size())
		{
			ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			return false;
		}
		if (!Resolved || !JitBridgeDetail::StoreMixedScalar(Kind, Value, *Resolved))
		{
			ReportError("JIT mixed this-field: scalar type mismatch");
			return false;
		}
		NotifyFieldMutation(*ThisObject, FieldIndex);
		return true;
	}

	bool FExecutionContext::JitInitializeThisFieldMixed(
	    const std::uint16_t NameIndex,
	    const EJitScalarValueKind Kind,
	    const std::uint64_t Value)
	{
		FValue Decoded;
		if (!JitBridgeDetail::UnpackMixedScalar(Kind, Value, Decoded))
		{
			ReportError("JIT mixed this-field initialization: scalar type mismatch");
			return false;
		}
		return JitInitializeThisField(NameIndex, std::move(Decoded));
	}

	bool FExecutionContext::JitResolveDirectI32MethodTarget(
	    const FObjectHandle& Receiver,
	    const std::uint16_t MethodNameIndex,
	    const std::uint8_t ArgumentCount,
	    const std::uint32_t CallSiteOffset,
	    std::uint16_t& OutFunctionIndex,
	    bool& OutHandled)
	{
		OutFunctionIndex = 0xFFFFu;
		OutHandled = false;
		if (JitHasInstanceCallInterceptor() || Frame.Identity == 0 || !Receiver || !ActiveModule || !JitCache || ArgumentCount == 0u || ArgumentCount > 16u || ActiveJitCallDepth >= MaxJitCallDepth)
		{
			return true;
		}

		std::uint16_t FunctionIndex = 0xFFFFu;
		if (!ResolveScriptObjectMethod(
		        Receiver, MethodNameIndex, ArgumentCount, CallSiteOffset, FunctionIndex))
		{
			return false;
		}
		// Always expose the authoritative generation-qualified target.  bHandled
		// separately states whether its existing function entry already has the raw
		// scalar ABI; a typed-loop caller may instead own an independently verified
		// pure-expression kernel for this exact function identity.
		if (FunctionIndex >= ActiveModule->Functions.size())
		{
			return true;
		}
		if (JitRequiresCoreVirtualDispatch(*Receiver, FunctionIndex)) return true;
		OutFunctionIndex = FunctionIndex;
		const FJitFunctionEntry* const Entry = JitCache->TryGet(FunctionIndex);
		if (!Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->ScalarAbi != EJitScalarAbi::I32 || Entry->ScalarParameterCount != ArgumentCount || Entry->ScalarReturnKind != EJitScalarValueKind::I32 || Entry->bRequiresThisObject)
		{
			return true;
		}
		for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
		{
			const auto Kind = static_cast<EJitScalarValueKind>(
			    (Entry->ScalarParameterKinds >> (static_cast<unsigned>(Index) * 4u)) & 0xFu);
			if (Kind != EJitScalarValueKind::I32)
			{
				return true;
			}
		}
		OutHandled = true;
		return true;
	}

	bool FExecutionContext::JitRequiresCoreVirtualDispatch(
	    const FScriptObject& Receiver, const std::uint16_t FunctionIndex) const
	{
		return (Receiver.bConstructionIncomplete || Receiver.bDestroying) && ActiveModule && FunctionIndex < ActiveModule->Functions.size() && ActiveModule->Functions[FunctionIndex].bIsVirtual;
	}

	bool FExecutionContext::JitBorrowObjectI32Field(const FObjectHandle& Receiver,
		const std::uint16_t NameIndex, std::uint16_t& OutFieldIndex,
		std::int32_t*& OutStorage)
	{
		OutFieldIndex = 0xFFFFu;
		OutStorage = nullptr;
		if (!Receiver || Receiver->bDestroyed || JitHasInstanceCallInterceptor() || JitHasMutationCallback()) return false;
		FValue* Storage = nullptr;
		if (!Values->BorrowObjectField(*Receiver, NameIndex, OutFieldIndex, Storage) || !Storage) return false;
		OutStorage = std::get_if<std::int32_t>(Storage);
		return OutStorage != nullptr;
	}

	bool FExecutionContext::JitGetObjectLayout(const FScriptObject& Receiver,
		std::uint16_t& OutTypeIndex)
	{
		OutTypeIndex = 0xFFFFu;
		return !Receiver.bDestroyed && Values->ResolveObjectLayout(Receiver, OutTypeIndex) && OutTypeIndex != 0xFFFFu;
	}

	bool FExecutionContext::JitBorrowDirectI32BoundFieldMethod(
	    const std::uint16_t Slot,
	    const std::uint16_t FunctionIndex,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    std::int32_t*& OutStorage,
	    std::int32_t& OutAddend,
	    std::int32_t& OutArgumentModulus,
	    std::int32_t& OutModulus,
	    bool& OutHandled,
	    bool* const bOutReturnsI32,
	    std::uint16_t* const OutFunctionIndex)
	{
		OutObject = nullptr;
		OutFieldIndex = 0xFFFF;
		OutStorage = nullptr;
		OutAddend = 0;
		OutArgumentModulus = 0;
		OutModulus = 0;
		OutHandled = false;
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = 0xFFFF;
		}
		if (bOutReturnsI32)
		{
			*bOutReturnsI32 = false;
		}
		if (JitHasMutationCallback() || JitHasInstanceCallInterceptor() || Frame.Identity == 0 || Slot >= CurrentLocalCount() || !ActiveModule || !JitCache || FunctionIndex >= ActiveModule->Functions.size())
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(JitBorrowDirectLinearLocal(Slot));
		if (Handle && *Handle && JitRequiresCoreVirtualDispatch(**Handle, FunctionIndex)) return true;
		const FJitFunctionEntry* const Entry = JitCache->TryGet(FunctionIndex);
		if (!Handle || !*Handle || !Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->ScalarAbi != EJitScalarAbi::I32 || Entry->ScalarParameterCount != 1 || (Entry->ScalarReturnKind != EJitScalarValueKind::Void && Entry->ScalarReturnKind != EJitScalarValueKind::I32) || !Entry->bDirectThisFieldAddModuloI32)
		{
			return true;
		}
		std::uint16_t FieldIndex = 0xFFFF;
		std::int32_t* Field = nullptr;
		if (!ResolveJitDirectI32FieldStorage(**Handle, *Entry, FieldIndex, Field))
		{
			return true;
		}
		OutObject = Handle->get();
		OutFieldIndex = FieldIndex;
		OutStorage = Field;
		OutAddend = Entry->DirectThisFieldAddendI32;
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = FunctionIndex;
		}
		OutArgumentModulus = Entry->DirectThisArgumentModuloI32;
		OutModulus = Entry->DirectThisFieldModuloI32;
		if (bOutReturnsI32)
		{
			*bOutReturnsI32 = Entry->ScalarReturnKind == EJitScalarValueKind::I32;
		}
		OutHandled = true;
		return true;
	}

	bool FExecutionContext::JitBorrowDirectI32FieldMethod(
	    const std::uint16_t Slot,
	    const std::uint16_t MethodNameIndex,
	    const std::uint32_t CallSiteOffset,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    std::int32_t*& OutStorage,
	    std::int32_t& OutAddend,
	    std::int32_t& OutArgumentModulus,
	    std::int32_t& OutModulus,
	    bool& OutHandled,
	    bool* const bOutReturnsI32,
	    std::uint16_t* const OutFunctionIndex)
	{
		OutObject = nullptr;
		OutFieldIndex = 0xFFFF;
		OutStorage = nullptr;
		OutAddend = 0;
		OutArgumentModulus = 0;
		OutModulus = 0;
		OutHandled = false;
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = 0xFFFF;
		}
		if (bOutReturnsI32)
		{
			*bOutReturnsI32 = false;
		}
		if (JitHasMutationCallback() || JitHasInstanceCallInterceptor() || Frame.Identity == 0 || Slot >= CurrentLocalCount() || !ActiveModule || !JitCache)
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(JitBorrowDirectLinearLocal(Slot));
		if (!Handle || !*Handle)
		{
			return true;
		}

		std::uint16_t FunctionIndex = 0xFFFF;
		if (!ResolveScriptObjectMethod(
		        *Handle, MethodNameIndex, 1, CallSiteOffset, FunctionIndex))
		{
			return false;
		}
		if (FunctionIndex >= ActiveModule->Functions.size())
		{
			return true;
		}
		if (JitRequiresCoreVirtualDispatch(**Handle, FunctionIndex)) return true;
		const FJitFunctionEntry* const Entry = JitCache->TryGet(FunctionIndex);
		if (!Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->ScalarAbi != EJitScalarAbi::I32 || Entry->ScalarParameterCount != 1 || (Entry->ScalarReturnKind != EJitScalarValueKind::Void && Entry->ScalarReturnKind != EJitScalarValueKind::I32) || !Entry->bDirectThisFieldAddModuloI32)
		{
			return true;
		}
		std::uint16_t FieldIndex = 0xFFFF;
		std::int32_t* Field = nullptr;
		if (!ResolveJitDirectI32FieldStorage(**Handle, *Entry, FieldIndex, Field))
		{
			return true;
		}
		OutObject = Handle->get();
		OutFieldIndex = FieldIndex;
		OutStorage = Field;
		OutAddend = Entry->DirectThisFieldAddendI32;
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = FunctionIndex;
		}
		OutArgumentModulus = Entry->DirectThisArgumentModuloI32;
		OutModulus = Entry->DirectThisFieldModuloI32;
		if (bOutReturnsI32)
		{
			*bOutReturnsI32 = Entry->ScalarReturnKind == EJitScalarValueKind::I32;
		}
		OutHandled = true;
		return true;
	}

	bool FExecutionContext::JitCommitDirectI32FieldMethod(FScriptObject& Object, std::uint16_t FieldIndex, const std::size_t Count)
	{
		if (GetLogicalCallDepth() >= CallDepthLimit)
		{
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}
		struct FDepthScope
		{
			explicit FDepthScope(std::size_t& InDepth) : Depth(InDepth)
			{
				++Depth;
			}
			~FDepthScope()
			{
				--Depth;
			}
			std::size_t& Depth;
		};
		const FDepthScope DepthScope(ActiveJitCallDepth);
		Stats.ScalarMethodFastPathI32 += Count;
		if (!JitHasMutationCallback())
		{
			Object.MutationVersion += Count;
		}
		else
		{
			// Batched callers are required to decline while a callback is installed;
			// retain exact behavior defensively if a future caller overlooks that rule.
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				NotifyFieldMutation(Object, FieldIndex);
			}
		}
		return !HasError();
	}

	bool FExecutionContext::JitCommitDirectScalarMethod(const EJitScalarValueKind Kind, const std::size_t Count)
	{
		if (GetLogicalCallDepth() >= CallDepthLimit)
		{
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}
		struct FDepthScope
		{
			explicit FDepthScope(std::size_t& InDepth) : Depth(InDepth)
			{
				++Depth;
			}
			~FDepthScope()
			{
				--Depth;
			}
			std::size_t& Depth;
		};
		const FDepthScope DepthScope(ActiveJitCallDepth);
		Stats.DispatchAttempts += Count;
		Stats.Dispatches += Count;
		switch (Kind)
		{
		case EJitScalarValueKind::I32:
			{
				Stats.ScalarMethodFastPathI32 += Count;
				break;
			}
		case EJitScalarValueKind::U32:
			{
				Stats.ScalarMethodFastPathU32 += Count;
				break;
			}
		case EJitScalarValueKind::F64:
			{
				Stats.ScalarMethodFastPathF64 += Count;
				break;
			}
		default:
			{
				Stats.ScalarMethodFastPathMixed += Count;
				break;
			}
		}
		return !HasError();
	}

	bool FExecutionContext::JitBorrowDirectI32TransformMethod(
	    const std::uint16_t Slot,
	    const std::uint16_t MethodNameIndex,
	    const std::uint32_t CallSiteOffset,
	    FScriptObject*& OutObject,
	    std::int32_t& OutAddend,
	    std::int32_t& OutModulus,
	    bool& OutHandled,
	    std::uint16_t* const OutFunctionIndex)
	{
		OutObject = nullptr;
		OutAddend = 0;
		OutModulus = 0;
		OutHandled = false;
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = 0xFFFF;
		}
		if (JitHasInstanceCallInterceptor() || Frame.Identity == 0 || Slot >= CurrentLocalCount() || !ActiveModule || !JitCache || ActiveJitCallDepth >= MaxJitCallDepth)
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(JitBorrowDirectLinearLocal(Slot));
		if (!Handle || !*Handle)
		{
			return true;
		}

		std::uint16_t FunctionIndex = 0xFFFF;
		if (!ResolveScriptObjectMethod(
		        *Handle, MethodNameIndex, 1, CallSiteOffset, FunctionIndex))
		{
			return false;
		}
		if (FunctionIndex >= ActiveModule->Functions.size())
		{
			return true;
		}
		if (JitRequiresCoreVirtualDispatch(**Handle, FunctionIndex)) return true;
		const FJitFunctionEntry* Entry = JitCache->TryGet(FunctionIndex);
		if (!Entry)
		{
			// This query is the preflight for a whole-loop virtual transform. Mixed
			// may compile the loop body before some receiver implementations have
			// been called, especially when one selector is absent from the current
			// data. Compile the proven dependency now; otherwise the fusion declines
			// and the complete hot loop pays boxed dispatch for this invocation.
			(void)Services.PrepareFunction(FunctionIndex, false);
			if (HasError()) return false;
			Entry = JitCache->TryGet(FunctionIndex);
		}
		if (!Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->ScalarAbi != EJitScalarAbi::I32 || Entry->ScalarParameterCount != 1 || Entry->ScalarReturnKind != EJitScalarValueKind::I32 || !Entry->bDirectI32AddModulo)
		{
			return true;
		}
		OutObject = Handle->get();
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = FunctionIndex;
		}
		OutAddend = Entry->DirectI32Addend;
		OutModulus = Entry->DirectI32Modulo;
		OutHandled = true;
		return true;
	}

	bool FExecutionContext::JitCommitDirectI32TransformMethod(const std::size_t Count)
	{
		return JitCommitDirectScalarMethod(EJitScalarValueKind::I32, Count);
	}

	bool FExecutionContext::JitReadDirectI32FieldAfterMutation(
	    FScriptObject& Object,
	    const std::uint16_t FieldIndex,
	    std::int32_t& OutValue)
	{
		if (FieldIndex == 0xFFFFu || FieldIndex >= Object.Fields.size())
		{
			ReportError("JIT direct field method: invalid field index after mutation");
			return false;
		}
		FValue* const FieldValue =
		    UnwrapClosureReferenceCell(&Object.Fields[FieldIndex]);
		const auto* const Field = FieldValue
		    ? std::get_if<std::int32_t>(FieldValue)
		    : nullptr;
		if (!Field)
		{
			ReportError("JIT direct field method: field type changed during mutation");
			return false;
		}
		OutValue = *Field;
		return true;
	}

	bool FExecutionContext::JitBorrowDirectI32BoundTransformMethod(
	    const std::uint16_t Slot,
	    const std::uint16_t FunctionIndex,
	    FScriptObject*& OutObject,
	    std::int32_t& OutAddend,
	    std::int32_t& OutModulus,
	    bool& OutHandled,
	    std::uint16_t* const OutFunctionIndex)
	{
		OutObject = nullptr;
		OutAddend = 0;
		OutModulus = 0;
		OutHandled = false;
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = 0xFFFF;
		}
		if (JitHasInstanceCallInterceptor() || Frame.Identity == 0 || Slot >= CurrentLocalCount() || !ActiveModule || !JitCache || FunctionIndex >= ActiveModule->Functions.size() || ActiveJitCallDepth >= MaxJitCallDepth)
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(JitBorrowDirectLinearLocal(Slot));
		if (Handle && *Handle && JitRequiresCoreVirtualDispatch(**Handle, FunctionIndex)) return true;
		const FJitFunctionEntry* const Entry = JitCache->TryGet(FunctionIndex);
		if (!Handle || !*Handle || !Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->ScalarAbi != EJitScalarAbi::I32 || Entry->ScalarParameterCount != 1 || Entry->ScalarReturnKind != EJitScalarValueKind::I32 || !Entry->bDirectI32AddModulo)
		{
			return true;
		}
		OutObject = Handle->get();
		if (OutFunctionIndex)
		{
			*OutFunctionIndex = FunctionIndex;
		}
		OutAddend = Entry->DirectI32Addend;
		OutModulus = Entry->DirectI32Modulo;
		OutHandled = true;
		return true;
	}

	bool FExecutionContext::JitBorrowDirectI32NullaryBoundFieldMethod(
	    const std::uint16_t Slot,
	    const std::uint16_t FunctionIndex,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    std::int32_t*& OutStorage,
	    std::int32_t& OutAddend,
	    std::int32_t& OutModulus,
	    bool& OutHandled)
	{
		OutObject = nullptr;
		OutFieldIndex = 0xFFFF;
		OutStorage = nullptr;
		OutAddend = 0;
		OutModulus = 0;
		OutHandled = false;
		if (JitHasMutationCallback() || JitHasInstanceCallInterceptor() || Frame.Identity == 0 || Slot >= CurrentLocalCount() || !ActiveModule || !JitCache || FunctionIndex >= ActiveModule->Functions.size())
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(JitBorrowDirectLinearLocal(Slot));
		if (Handle && *Handle && JitRequiresCoreVirtualDispatch(**Handle, FunctionIndex)) return true;
		const FJitFunctionEntry* const Entry = JitCache->TryGet(FunctionIndex);
		if (!Handle || !*Handle || !Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->ScalarAbi != EJitScalarAbi::I32 || Entry->ScalarParameterCount != 0 || Entry->ScalarReturnKind != EJitScalarValueKind::Void || !Entry->bDirectThisFieldAddModuloI32)
		{
			return true;
		}
		std::uint16_t FieldIndex = 0xFFFF;
		std::int32_t* Field = nullptr;
		if (!ResolveJitDirectI32FieldStorage(**Handle, *Entry, FieldIndex, Field))
		{
			return true;
		}
		OutObject = Handle->get();
		OutFieldIndex = FieldIndex;
		OutStorage = Field;
		OutAddend = Entry->DirectThisFieldAddendI32;
		OutModulus = Entry->DirectThisFieldModuloI32;
		OutHandled = true;
		return true;
	}

	bool FExecutionContext::JitTryInvokeBoundMethodI32OnLocal(
	    const std::uint16_t Slot,
	    const std::uint16_t FunctionIndex,
	    const FValue& Argument,
	    FValue& OutValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frame.Identity == 0 || Slot >= CurrentLocalCount() || !ActiveModule || FunctionIndex >= ActiveModule->Functions.size() || !JitCache || JitHasInstanceCallInterceptor())
		{
			return true;
		}
		const auto* const Handle = std::get_if<FObjectHandle>(
		    JitBorrowDirectLinearLocal(Slot));
		const auto* const RawArgument = std::get_if<std::int32_t>(&Argument);
		const FJitFunctionEntry* const Entry = JitCache->TryGet(FunctionIndex);
		if (!Handle || !*Handle || !RawArgument || !Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->ScalarAbi != EJitScalarAbi::I32 || Entry->ScalarParameterCount != 1 || (Entry->ScalarReturnKind != EJitScalarValueKind::Void && Entry->ScalarReturnKind != EJitScalarValueKind::I32 && Entry->ScalarReturnKind != EJitScalarValueKind::Bool))
		{
			return true;
		}
		std::int32_t RawResult = 0;
		bool bNativeHandled = false;
		const bool bOk = JitInvokeBoundMethodI32Fast(
		    FunctionIndex, *Handle, RawArgument, 1, &RawResult, bNativeHandled);
		if (!bNativeHandled)
		{
			return true;
		}
		OutHandled = true;
		if (!bOk)
		{
			return false;
		}
		if (Entry->ScalarReturnKind == EJitScalarValueKind::Bool)
		{
			OutValue = RawResult != 0;
		}
		else
		{
			OutValue = RawResult;
		}
		return true;
	}

	bool FExecutionContext::JitTryCallScalarMethodOnValue(
	    const FValue& Receiver, const std::uint16_t MethodNameIndex,
	    const FValue* const Arguments, const std::uint8_t ArgumentCount,
	    const std::uint32_t CallSiteOffset, FValue& OutValue, bool& OutHandled)
	{
		OutHandled = false;
		if (JitHasInstanceCallInterceptor() || JitHasMutationCallback() || Frame.Identity == 0 || !ActiveModule || !JitCache || (ArgumentCount != 0 && !Arguments))
		{
			return true;
		}
		const auto* const Object = std::get_if<FObjectHandle>(&Receiver);
		if (!Object || !*Object) return true;
		const FScriptObject& ScriptObject = **Object;
		const std::uint16_t ObjectTypeIndex =
		    ScriptObject.RuntimeTypeModule == ActiveModule && ScriptObject.RuntimeTypeGeneration == ProgramGeneration && ScriptObject.RuntimeTypeIndex < ActiveModule->Types.size()
		    ? ScriptObject.RuntimeTypeIndex
		    : ResolveObjectTypeIndex(ScriptObject);
		constexpr std::size_t WaysPerSet = 4;
		constexpr std::size_t SetCount = 4;
		static_assert(WaysPerSet * SetCount == 16);
		const std::uint32_t SiteHash = CallSiteOffset != 0xFFFFFFFFu
		    ? CallSiteOffset
		    : (static_cast<std::uint32_t>(MethodNameIndex) * 2654435761u ^ static_cast<std::uint32_t>(ArgumentCount) * 2246822519u);
		const std::uint32_t MixedSiteHash = SiteHash ^ (SiteHash >> 4u) ^ (SiteHash >> 11u);
		const std::size_t SetIndex = MixedSiteHash & (SetCount - 1u);
		const std::size_t SetBase = SetIndex * WaysPerSet;
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		const FJitFunctionEntry* Entry = nullptr;
		bool bScalarCacheHit = false;
		if (ObjectTypeIndex != static_cast<std::uint16_t>(-1))
		{
			for (std::size_t Way = 0; Way < WaysPerSet; ++Way)
			{
				const FScalarMethodInlineCacheEntry& Cached =
				    ScalarMethodIc[SetBase + Way];
				if (Cached.JitCacheEpoch == JitCacheEpoch && Cached.CallSiteOffset == CallSiteOffset && Cached.NameIndex == MethodNameIndex && Cached.TypeIndex == ObjectTypeIndex && Cached.ArgumentCount == ArgumentCount && Cached.FunctionIndex < ActiveModule->Functions.size() && Cached.PreparedEntry)
				{
					FunctionIndex = Cached.FunctionIndex;
					Entry = Cached.PreparedEntry;
					bScalarCacheHit = true;
					break;
				}
			}
		}
		if (!Entry && !ResolveScriptObjectMethod(*Object, MethodNameIndex, ArgumentCount, CallSiteOffset, FunctionIndex))
		{
			return false;
		}
		if (JitRequiresCoreVirtualDispatch(ScriptObject, FunctionIndex)) return true;
		if (!bScalarCacheHit)
		{
			if (FunctionIndex >= ActiveModule->Functions.size())
			{
				return true;
			}
			const FBytecodeFunction& Function = ActiveModule->Functions[FunctionIndex];
			Entry = JitCache->TryGet(FunctionIndex);
			if (Function.bDeleted || !Entry || (Entry->bMutatesScriptFields && JitHasMutationCallback()) || Entry->Kind != EJitEntryKind::NativeCfg || !Entry->NativeCode || Entry->ScalarParameterCount != ArgumentCount || Entry->ScalarReturnKind == EJitScalarValueKind::None || ArgumentCount > 16)
			{
				return true;
			}
			const bool bReturnCompatible =
			    (Entry->ScalarAbi == EJitScalarAbi::I32 && (Entry->ScalarReturnKind == EJitScalarValueKind::I32 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void)) || (Entry->ScalarAbi == EJitScalarAbi::U32 && (Entry->ScalarReturnKind == EJitScalarValueKind::U32 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void)) || (Entry->ScalarAbi == EJitScalarAbi::F64 && (Entry->ScalarReturnKind == EJitScalarValueKind::F64 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void)) || (Entry->ScalarAbi == EJitScalarAbi::Mixed && (Entry->ScalarReturnKind == EJitScalarValueKind::I8 || Entry->ScalarReturnKind == EJitScalarValueKind::U8 || Entry->ScalarReturnKind == EJitScalarValueKind::I16 || Entry->ScalarReturnKind == EJitScalarValueKind::U16 || Entry->ScalarReturnKind == EJitScalarValueKind::I32 || Entry->ScalarReturnKind == EJitScalarValueKind::U32 || Entry->ScalarReturnKind == EJitScalarValueKind::I64 || Entry->ScalarReturnKind == EJitScalarValueKind::U64 || Entry->ScalarReturnKind == EJitScalarValueKind::F32 || Entry->ScalarReturnKind == EJitScalarValueKind::F64 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void));
			if (!bReturnCompatible)
			{
				return true;
			}
			if (ObjectTypeIndex != static_cast<std::uint16_t>(-1))
			{
				const std::size_t Way =
				    ScalarMethodIcNextWay[SetIndex]++ & (WaysPerSet - 1u);
				FScalarMethodInlineCacheEntry& Cached =
				    ScalarMethodIc[SetBase + Way];
				Cached.PreparedEntry = Entry;
				Cached.JitCacheEpoch = JitCacheEpoch;
				Cached.CallSiteOffset = CallSiteOffset;
				Cached.NameIndex = MethodNameIndex;
				Cached.TypeIndex = ObjectTypeIndex;
				Cached.FunctionIndex = FunctionIndex;
				Cached.ArgumentCount = ArgumentCount;
			}
		}
		const auto ParameterKind = [Entry](const std::uint8_t Index)
		{
			return static_cast<EJitScalarValueKind>(
			    (Entry->ScalarParameterKinds >> (static_cast<unsigned>(Index) * 4u)) & 0xFu);
		};
		const auto StoreResult = [&](const auto RawResult) -> bool
		{
			using TRaw = std::remove_cv_t<decltype(RawResult)>;
			if constexpr (std::is_same_v<TRaw, std::uint64_t>)
			{
				if (Entry->ScalarReturnKind == EJitScalarValueKind::Void)
				{
					OutValue = std::int32_t{0};
					return true;
				}
				return JitBridgeDetail::UnpackMixedScalar(
				    Entry->ScalarReturnKind, RawResult, OutValue);
			}
			else
			{
				switch (Entry->ScalarReturnKind)
				{
				case EJitScalarValueKind::Void:
					{
						OutValue = std::int32_t{0};
						return true;
					}
				case EJitScalarValueKind::Bool:
					{
						OutValue = RawResult != static_cast<TRaw>(0);
						return true;
					}
				case EJitScalarValueKind::I32:
					{
						if constexpr (std::is_same_v<TRaw, std::int32_t>)
						{
							OutValue = RawResult;
							return true;
						}
						else
						{
							return false;
						}
					}
				case EJitScalarValueKind::U32:
					{
						if constexpr (std::is_same_v<TRaw, std::uint32_t>)
						{
							OutValue = RawResult;
							return true;
						}
						else
						{
							return false;
						}
					}
				case EJitScalarValueKind::F64:
					{
						if constexpr (std::is_same_v<TRaw, double>)
						{
							OutValue = RawResult;
							return true;
						}
						else
						{
							return false;
						}
					}
				default:
					return false;
				}
			}
		};
		if (Entry->bDirectThisFieldAddModuloI32 && Entry->ScalarParameterCount == ArgumentCount && (ArgumentCount == 0 || ArgumentCount == 1))
		{
			const auto* const Delta = ArgumentCount == 1
			    ? std::get_if<std::int32_t>(&Arguments[0])
			    : nullptr;
			std::uint16_t FieldIndex = 0xFFFF;
			std::int32_t* Field = nullptr;
			ResolveJitDirectI32FieldStorage(**Object, *Entry, FieldIndex, Field);
			if ((ArgumentCount == 0 || Delta) && Field)
			{
				if (GetLogicalCallDepth() >= CallDepthLimit)
				{
					OutHandled = true;
					ReportErrorId("diag.call_stack_overflow");
					return false;
				}
				OutHandled = true;
				struct FDirectFieldDepthScope
				{
					explicit FDirectFieldDepthScope(std::size_t& InDepth) : Depth(InDepth)
					{
						++Depth;
					}
					~FDirectFieldDepthScope()
					{
						--Depth;
					}
					std::size_t& Depth;
				};
				const FDirectFieldDepthScope DepthScope(ActiveJitCallDepth);
				std::int32_t EffectiveDelta = ArgumentCount == 0
				    ? Entry->DirectThisFieldAddendI32
				    : *Delta;
				const std::int32_t ArgumentModulus =
				    Entry->DirectThisArgumentModuloI32;
				if (ArgumentModulus != 0)
				{
					if (EffectiveDelta == std::numeric_limits<std::int32_t>::min() && ArgumentModulus == -1)
					{
						ReportErrorId("diag.integer_overflow");
						return false;
					}
					EffectiveDelta %= ArgumentModulus;
				}
				std::int32_t Sum = 0;
				std::int32_t Updated = 0;
				const std::int32_t Modulus = Entry->DirectThisFieldModuloI32;
				const std::int32_t Addend = ArgumentCount == 0
				    ? 0
				    : Entry->DirectThisFieldAddendI32;
				if (!Runtime::CheckedAdd(*Field, EffectiveDelta, Sum) || !Runtime::CheckedAdd(Sum, Addend, Updated) || (Modulus != 0 && Updated == std::numeric_limits<std::int32_t>::min() && Modulus == -1))
				{
					ReportErrorId("diag.integer_overflow");
					return false;
				}
				*Field = Modulus == 0 ? Updated : Updated % Modulus;
				NotifyFieldMutationJitFast(**Object, FieldIndex);
				++Stats.ScalarMethodFastPathI32;
				std::int32_t ReturnedField = 0;
				if (Entry->ScalarReturnKind == EJitScalarValueKind::I32 && !JitHasMutationCallback())
				{
					ReturnedField = *Field;
				}
				else if (Entry->ScalarReturnKind == EJitScalarValueKind::I32 && !JitReadDirectI32FieldAfterMutation(**Object, FieldIndex, ReturnedField))
				{
					return false;
				}
				OutValue = FValue{ReturnedField};
				return !HasError();
			}
		}
		if (Entry->DirectScalarKind != EJitDirectScalarKind::None)
		{
			if (GetLogicalCallDepth() >= CallDepthLimit)
			{
				OutHandled = true;
				ReportErrorId("diag.call_stack_overflow");
				return false;
			}
			if (ActiveJitCallDepth >= MaxJitCallDepth)
			{
				return true;
			}
			struct FDirectDepthScope
			{
				explicit FDirectDepthScope(std::size_t& InDepth) : Depth(InDepth)
				{
					++Depth;
				}
				~FDirectDepthScope()
				{
					--Depth;
				}
				std::size_t& Depth;
			};
			const FDirectDepthScope DepthScope(ActiveJitCallDepth);
			std::int32_t Status = EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
			std::int32_t I32Result = 0;
			std::uint32_t U32Result = 0;
			double F64Result = 0.0;
			std::uint64_t MixedResult = 0;
			bool bPortableArgumentsValid = true;
			using JitNative::LinearHelpers::EMixedScalarTag;
			const auto RunPortableUnary = [&](const EMixedScalarTag Tag,
			                                  const FValue& Argument) -> std::int32_t
			{
				std::uint64_t Bits = 0;
				if (!JitBridgeDetail::PackMixedScalar(ParameterKind(0), Argument, Bits))
				{
					bPortableArgumentsValid = false;
					return EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
				}
				const EOpcode Op = Entry->DirectScalarOpcode;
				if (Op == EOpcode::Nop)
				{
					MixedResult = Bits;
					return 0;
				}
				return JitNative::LinearHelpers::UnaryOpMixed(
				    static_cast<std::uint8_t>(Entry->DirectScalarOpcode),
				    static_cast<std::uint8_t>(Tag),
				    Bits, &MixedResult);
			};
			const auto RunPortableBinary = [&](const EMixedScalarTag Tag) -> std::int32_t
			{
				if (ArgumentCount != 2)
				{
					bPortableArgumentsValid = false;
					return EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
				}
				std::uint64_t Left = 0;
				std::uint64_t Right = 0;
				if (!JitBridgeDetail::PackMixedScalar(ParameterKind(0), Arguments[0], Left) || !JitBridgeDetail::PackMixedScalar(ParameterKind(1), Arguments[1], Right))
				{
					bPortableArgumentsValid = false;
					return EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
				}
				return JitNative::LinearHelpers::BinOpMixed(
				    static_cast<std::uint8_t>(Entry->DirectScalarOpcode),
				    static_cast<std::uint8_t>(Tag),
				    Left, Right, &MixedResult);
			};
			switch (Entry->DirectScalarKind)
			{
			case EJitDirectScalarKind::NullaryI32:
				{
					if (ArgumentCount != 0)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FConstI32Kernel>(
					    Entry->DirectScalarCode)(&I32Result);
					break;
				}
			case EJitDirectScalarKind::UnaryI32:
				{
					if (ArgumentCount != 1)
					{
						return true;
					}
					std::int32_t RawValue = 0;
					if (const auto* const Value =
					        std::get_if<std::int32_t>(&Arguments[0]))
					{
						RawValue = *Value;
					}
					else if (ParameterKind(0) == EJitScalarValueKind::Bool)
					{
						const auto* const BoolValue = std::get_if<bool>(&Arguments[0]);
						if (!BoolValue)
						{
							return true;
						}
						RawValue = *BoolValue ? 1 : 0;
					}
					else
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FUnaryI32Kernel>(
					    Entry->DirectScalarCode)(RawValue, &I32Result);
					break;
				}
			case EJitDirectScalarKind::BinaryI32:
				{
					if (ArgumentCount != 2)
					{
						return true;
					}
					const auto* const Left = std::get_if<std::int32_t>(&Arguments[0]);
					const auto* const Right = std::get_if<std::int32_t>(&Arguments[1]);
					if (!Left || !Right)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FBinaryI32Kernel>(
					    Entry->DirectScalarCode)(*Left, *Right, &I32Result);
					break;
				}
			case EJitDirectScalarKind::NullaryU32:
				{
					if (ArgumentCount != 0)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FConstU32Kernel>(
					    Entry->DirectScalarCode)(&U32Result);
					break;
				}
			case EJitDirectScalarKind::UnaryU32:
				{
					if (ArgumentCount != 1)
					{
						return true;
					}
					const auto* const Value = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Value)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FUnaryU32Kernel>(
					    Entry->DirectScalarCode)(*Value, &U32Result);
					break;
				}
			case EJitDirectScalarKind::UnaryU32RightImm:
				{
					if (ArgumentCount != 1)
					{
						return true;
					}
					const auto* const Value = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Value)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FBinaryU32Kernel>(
					    Entry->DirectScalarCode)(
					    *Value, Entry->DirectScalarPayloadU32, &U32Result);
					break;
				}
			case EJitDirectScalarKind::BinaryU32:
				{
					if (ArgumentCount != 2)
					{
						return true;
					}
					const auto* const Left = std::get_if<std::uint32_t>(&Arguments[0]);
					const auto* const Right = std::get_if<std::uint32_t>(&Arguments[1]);
					if (!Left || !Right)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FBinaryU32Kernel>(
					    Entry->DirectScalarCode)(*Left, *Right, &U32Result);
					break;
				}
			case EJitDirectScalarKind::NullaryF64:
				{
					if (ArgumentCount != 0)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FConstF64Kernel>(
					    Entry->DirectScalarCode)(&F64Result);
					break;
				}
			case EJitDirectScalarKind::UnaryF64:
				{
					if (ArgumentCount != 1)
					{
						return true;
					}
					const auto* const Value = std::get_if<double>(&Arguments[0]);
					if (!Value)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FUnaryF64Kernel>(
					    Entry->DirectScalarCode)(*Value, &F64Result);
					break;
				}
			case EJitDirectScalarKind::BinaryF64:
				{
					if (ArgumentCount != 2)
					{
						return true;
					}
					const auto* const Left = std::get_if<double>(&Arguments[0]);
					const auto* const Right = std::get_if<double>(&Arguments[1]);
					if (!Left || !Right)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FBinaryF64Kernel>(
					    Entry->DirectScalarCode)(*Left, *Right, &F64Result);
					break;
				}
			case EJitDirectScalarKind::BinaryF64Bool:
				{
					if (ArgumentCount != 2)
					{
						return true;
					}
					const auto* const Left = std::get_if<double>(&Arguments[0]);
					const auto* const Right = std::get_if<double>(&Arguments[1]);
					if (!Left || !Right)
					{
						return true;
					}
					Status = reinterpret_cast<JitNative::FCmpF64Kernel>(
					    Entry->DirectScalarCode)(*Left, *Right, &I32Result);
					break;
				}
			case EJitDirectScalarKind::UnaryI64:
				{
					if (ArgumentCount != 1)
					{
						return true;
					}
					Status = RunPortableUnary(EMixedScalarTag::I64, Arguments[0]);
					break;
				}
			case EJitDirectScalarKind::UnaryI64RightImm:
				{
					if (ArgumentCount != 1 || !JitBridgeDetail::PackMixedScalar(ParameterKind(0), Arguments[0], MixedResult))
					{
						return true;
					}
					Status = JitNative::LinearHelpers::BinOpMixed(
					    static_cast<std::uint8_t>(Entry->DirectScalarOpcode),
					    static_cast<std::uint8_t>(EMixedScalarTag::I64), MixedResult,
					    Entry->DirectScalarPayloadU64, &MixedResult);
					break;
				}
			case EJitDirectScalarKind::BinaryI64:
				{
					Status = RunPortableBinary(EMixedScalarTag::I64);
					break;
				}
			case EJitDirectScalarKind::UnaryU64:
				{
					if (ArgumentCount != 1)
					{
						return true;
					}
					Status = RunPortableUnary(EMixedScalarTag::U64, Arguments[0]);
					break;
				}
			case EJitDirectScalarKind::UnaryU64RightImm:
				{
					if (ArgumentCount != 1 || !JitBridgeDetail::PackMixedScalar(ParameterKind(0), Arguments[0], MixedResult))
					{
						return true;
					}
					Status = JitNative::LinearHelpers::BinOpMixed(
					    static_cast<std::uint8_t>(Entry->DirectScalarOpcode),
					    static_cast<std::uint8_t>(EMixedScalarTag::U64), MixedResult,
					    Entry->DirectScalarPayloadU64, &MixedResult);
					break;
				}
			case EJitDirectScalarKind::BinaryU64:
				{
					Status = RunPortableBinary(EMixedScalarTag::U64);
					break;
				}
			case EJitDirectScalarKind::UnaryF32:
				{
					if (ArgumentCount != 1)
					{
						return true;
					}
					Status = RunPortableUnary(EMixedScalarTag::F32, Arguments[0]);
					break;
				}
			case EJitDirectScalarKind::UnaryF32RightImm:
				{
					if (ArgumentCount != 1 || !JitBridgeDetail::PackMixedScalar(ParameterKind(0), Arguments[0], MixedResult))
					{
						return true;
					}
					Status = JitNative::LinearHelpers::BinOpMixed(
					    static_cast<std::uint8_t>(Entry->DirectScalarOpcode),
					    static_cast<std::uint8_t>(EMixedScalarTag::F32), MixedResult,
					    Entry->DirectScalarPayloadU64, &MixedResult);
					break;
				}
			case EJitDirectScalarKind::BinaryF32:
				{
					Status = RunPortableBinary(EMixedScalarTag::F32);
					break;
				}
			case EJitDirectScalarKind::None:
				return true;
			default:
				{
					break;
				}
			}
			if (!bPortableArgumentsValid)
			{
				// Preserve the authoritative NativeCfg fallback for malformed or
				// externally supplied values that do not match the cached signature.
				return true;
			}
			OutHandled = true;
			++Stats.DispatchAttempts;
			++Stats.Dispatches;
			if (Entry->ScalarAbi == EJitScalarAbi::F64)
			{
				++Stats.ScalarMethodFastPathF64;
			}
			else if (Entry->ScalarAbi == EJitScalarAbi::U32)
			{
				++Stats.ScalarMethodFastPathU32;
			}
			else if (Entry->ScalarAbi == EJitScalarAbi::Mixed)
			{
				++Stats.ScalarMethodFastPathMixed;
			}
			else
			{
				++Stats.ScalarMethodFastPathI32;
			}
			if (Status != 0)
			{
				++Stats.Failures;
				(void)JitHandleKernelFailure(Status);
				return false;
			}
			if (Entry->DirectScalarKind == EJitDirectScalarKind::NullaryU32 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryU32 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryU32RightImm || Entry->DirectScalarKind == EJitDirectScalarKind::BinaryU32)
			{
				return StoreResult(U32Result);
			}
			if (Entry->DirectScalarKind == EJitDirectScalarKind::UnaryI64 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryI64RightImm || Entry->DirectScalarKind == EJitDirectScalarKind::BinaryI64 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryU64 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryU64RightImm || Entry->DirectScalarKind == EJitDirectScalarKind::BinaryU64 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryF32 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryF32RightImm || Entry->DirectScalarKind == EJitDirectScalarKind::BinaryF32)
			{
				return StoreResult(MixedResult);
			}
			return Entry->DirectScalarKind == EJitDirectScalarKind::NullaryF64 || Entry->DirectScalarKind == EJitDirectScalarKind::UnaryF64 || Entry->DirectScalarKind == EJitDirectScalarKind::BinaryF64
			    ? StoreResult(F64Result)
			    : StoreResult(I32Result);
		}

		// Pack every live slot before entry; unused capacity is outside the scalar ABI.
		bool bNativeHandled = false;
		switch (Entry->ScalarAbi)
		{
		case EJitScalarAbi::I32:
			{
				std::int32_t RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					if (ParameterKind(Index) == EJitScalarValueKind::I32)
					{
						const auto* Value = std::get_if<std::int32_t>(&Arguments[Index]);
						if (!Value)
						{
							return true;
						}
						RawArguments[Index] = *Value;
					}
					else if (ParameterKind(Index) == EJitScalarValueKind::Bool)
					{
						const auto* Value = std::get_if<bool>(&Arguments[Index]);
						if (!Value)
						{
							return true;
						}
						RawArguments[Index] = *Value ? 1 : 0;
					}
					else
					{
						return true;
					}
				}
				std::int32_t Result = 0;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, *Object, RawArguments, ArgumentCount,
				    &Result, bNativeHandled, true, Entry);
				if (!bNativeHandled)
				{
					return true;
				}
				OutHandled = true;
				return bOk && StoreResult(Result);
			}
		case EJitScalarAbi::U32:
			{
				std::uint32_t RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					if (ParameterKind(Index) == EJitScalarValueKind::U32)
					{
						const auto* Value = std::get_if<std::uint32_t>(&Arguments[Index]);
						if (!Value)
						{
							return true;
						}
						RawArguments[Index] = *Value;
					}
					else if (ParameterKind(Index) == EJitScalarValueKind::Bool)
					{
						const auto* Value = std::get_if<bool>(&Arguments[Index]);
						if (!Value)
						{
							return true;
						}
						RawArguments[Index] = *Value ? 1u : 0u;
					}
					else
					{
						return true;
					}
				}
				std::uint32_t Result = 0;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, *Object, RawArguments, ArgumentCount,
				    &Result, bNativeHandled, true, Entry);
				if (!bNativeHandled)
				{
					return true;
				}
				OutHandled = true;
				return bOk && StoreResult(Result);
			}
		case EJitScalarAbi::F64:
			{
				double RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					if (ParameterKind(Index) != EJitScalarValueKind::F64)
					{
						return true;
					}
					const auto* Value = std::get_if<double>(&Arguments[Index]);
					if (!Value)
					{
						return true;
					}
					RawArguments[Index] = *Value;
				}
				double Result = 0.0;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, *Object, RawArguments, ArgumentCount,
				    &Result, bNativeHandled, true, Entry);
				if (!bNativeHandled)
				{
					return true;
				}
				OutHandled = true;
				return bOk && StoreResult(Result);
			}
		case EJitScalarAbi::Mixed:
			{
				std::uint64_t RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					if (!JitBridgeDetail::PackMixedScalar(
					        ParameterKind(Index), Arguments[Index], RawArguments[Index]))
					{
						return true;
					}
				}
				std::uint64_t Result = 0;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, *Object, RawArguments, ArgumentCount,
				    &Result, bNativeHandled, true, Entry);
				if (!bNativeHandled)
				{
					return true;
				}
				OutHandled = true;
				return bOk && StoreResult(Result);
			}
		default:
			return true;
		}
	}

	bool FExecutionContext::JitTryCallScalarMethodOnLocal(
	    const std::uint16_t Slot, const std::uint16_t MethodNameIndex,
	    const FValue* const Arguments, const std::uint8_t ArgumentCount,
	    const std::uint32_t CallSiteOffset, FValue& OutValue, bool& OutHandled)
	{
		OutHandled = false;
		if (Frame.Identity == 0 || Slot >= CurrentLocalCount()) return true;
		const FValue* const Local = JitBorrowDirectLinearLocal(Slot);
		const FValue* const Receiver = Local && std::holds_alternative<FObjectHandle>(*Local)
		    ? Local
		    : Frames->BorrowResolvedLocal(Frame, Slot);
		return Receiver ? JitTryCallScalarMethodOnValue(*Receiver, MethodNameIndex, Arguments,
		                      ArgumentCount, CallSiteOffset, OutValue, OutHandled)
		                : true;
	}

	bool FExecutionContext::JitTryCallNullaryI32MethodOnLocal(
	    const std::uint16_t Slot,
	    const std::uint16_t MethodNameIndex,
	    const std::uint32_t CallSiteOffset,
	    std::int32_t& OutValue,
	    bool& OutHandled,
	    bool& OutStableConstant)
	{
		OutHandled = false;
		OutStableConstant = false;
		if (JitHasInstanceCallInterceptor() || Frame.Identity == 0 || Slot >= CurrentLocalCount() || !ActiveModule || !JitCache)
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(JitBorrowDirectLinearLocal(Slot));
		if (!Handle || !*Handle)
		{
			return true;
		}

		const FScriptObject& Object = **Handle;
		const std::uint16_t TypeIndex =
		    Object.RuntimeTypeModule == ActiveModule && Object.RuntimeTypeGeneration == ProgramGeneration && Object.RuntimeTypeIndex < ActiveModule->Types.size()
		    ? Object.RuntimeTypeIndex
		    : ResolveObjectTypeIndex(Object);
		constexpr std::size_t WaysPerSet = 4;
		constexpr std::size_t SetCount = 4;
		static_assert(WaysPerSet * SetCount == 16);
		const std::uint32_t SiteHash = CallSiteOffset != 0xFFFFFFFFu
		    ? CallSiteOffset
		    : static_cast<std::uint32_t>(MethodNameIndex) * 2654435761u;
		const std::uint32_t MixedSiteHash = SiteHash ^ (SiteHash >> 4u) ^ (SiteHash >> 11u);
		const std::size_t SetIndex = MixedSiteHash & (SetCount - 1u);
		const std::size_t SetBase = SetIndex * WaysPerSet;
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		const FJitFunctionEntry* Entry = nullptr;
		for (std::size_t Way = 0; Way < WaysPerSet; ++Way)
		{
			const FScalarMethodInlineCacheEntry& Cached =
			    ScalarMethodIc[SetBase + Way];
			if (Cached.JitCacheEpoch == JitCacheEpoch && Cached.CallSiteOffset == CallSiteOffset && Cached.NameIndex == MethodNameIndex && Cached.TypeIndex == TypeIndex && Cached.ArgumentCount == 0 && Cached.FunctionIndex < ActiveModule->Functions.size() && Cached.PreparedEntry)
			{
				FunctionIndex = Cached.FunctionIndex;
				Entry = Cached.PreparedEntry;
				break;
			}
		}
		if (!Entry)
		{
			if (!ResolveScriptObjectMethod(
			        *Handle, MethodNameIndex, 0, CallSiteOffset, FunctionIndex))
			{
				return false;
			}
			if (FunctionIndex >= ActiveModule->Functions.size())
			{
				return true;
			}
			if (JitRequiresCoreVirtualDispatch(Object, FunctionIndex)) return true;
			Entry = JitCache->TryGet(FunctionIndex);
			if (!Entry || Entry->Kind != EJitEntryKind::NativeCfg || Entry->DirectScalarKind != EJitDirectScalarKind::NullaryI32 || !Entry->DirectScalarCode || (Entry->ScalarReturnKind != EJitScalarValueKind::I32 && Entry->ScalarReturnKind != EJitScalarValueKind::Bool))
			{
				return true;
			}
			const std::size_t Way =
			    ScalarMethodIcNextWay[SetIndex]++ & (WaysPerSet - 1u);
			FScalarMethodInlineCacheEntry& Cached = ScalarMethodIc[SetBase + Way];
			Cached.PreparedEntry = Entry;
			Cached.JitCacheEpoch = JitCacheEpoch;
			Cached.CallSiteOffset = CallSiteOffset;
			Cached.NameIndex = MethodNameIndex;
			Cached.TypeIndex = TypeIndex;
			Cached.FunctionIndex = FunctionIndex;
			Cached.ArgumentCount = 0;
		}
		else if (Entry->DirectScalarKind != EJitDirectScalarKind::NullaryI32 || !Entry->DirectScalarCode || (Entry->ScalarReturnKind != EJitScalarValueKind::I32 && Entry->ScalarReturnKind != EJitScalarValueKind::Bool))
		{
			return true;
		}
		if (JitRequiresCoreVirtualDispatch(Object, FunctionIndex)) return true;
		if (GetLogicalCallDepth() >= CallDepthLimit)
		{
			OutHandled = true;
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}
		if (ActiveJitCallDepth >= MaxJitCallDepth)
		{
			return true;
		}
		struct FDepthScope
		{
			explicit FDepthScope(std::size_t& InDepth) : Depth(InDepth)
			{
				++Depth;
			}
			~FDepthScope()
			{
				--Depth;
			}
			std::size_t& Depth;
		};
		const FDepthScope DepthScope(ActiveJitCallDepth);
		std::int32_t RawResult = 0;
		std::int32_t Status = 0;
		if (Entry->DirectScalarOpcode == EOpcode::ConstI32 || Entry->DirectScalarOpcode == EOpcode::ConstBool)
		{
			RawResult = static_cast<std::int32_t>(Entry->DirectScalarPayloadU32);
			OutStableConstant = true;
		}
		else
		{
			Status = reinterpret_cast<JitNative::FConstI32Kernel>(
			    Entry->DirectScalarCode)(&RawResult);
		}
		OutHandled = true;
		++Stats.DispatchAttempts;
		++Stats.Dispatches;
		++Stats.ScalarMethodFastPathI32;
		if (Status != 0)
		{
			++Stats.Failures;
			(void)JitHandleKernelFailure(Status);
			return false;
		}
		OutValue = Entry->ScalarReturnKind == EJitScalarValueKind::Bool
		    ? (RawResult != 0 ? 1 : 0)
		    : RawResult;
		return true;
	}

	std::int32_t FExecutionContext::JitEnterDirectScalarRecursion(
	    const EJitScalarValueKind Kind)
	{
		if (GetLogicalCallDepth() >= CallDepthLimit)
		{
			ReportErrorId("diag.call_stack_overflow");
			return EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
		}
		if (ActiveJitCallDepth >= MaxDirectScalarCallDepth)
		{
			// The emitter normally diverts to the VM before reaching this native
			// depth. If a stale/manual kernel violates that contract, fail as a JIT
			// runtime error; status 1 is IntegerOverflow and must never double as a
			// private recursion-control sentinel.
			JitReportRuntimeError("JIT native recursion limit reached");
			return EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
		}
		++ActiveJitCallDepth;
		++Stats.DispatchAttempts;
		switch (Kind)
		{
		case EJitScalarValueKind::I32:
			{
				++Stats.ScalarFunctionFastPathI32;
				break;
			}
		case EJitScalarValueKind::U32:
			{
				++Stats.ScalarFunctionFastPathU32;
				break;
			}
		case EJitScalarValueKind::F64:
			{
				++Stats.ScalarFunctionFastPathF64;
				break;
			}
		default:
			{
				++Stats.ScalarFunctionFastPathMixed;
				break;
			}
		}
		return 0;
	}

	template <typename T>
	bool FExecutionContext::JitInvokeRecursiveScalarFast(
	    const std::uint16_t FunctionIndex,
	    const T* Arguments,
	    const std::uint8_t ArgumentCount,
	    T* OutResult,
	    bool& bOutHandled, const bool bRootInvocation)
	{
		using TRaw = std::conditional_t<
		    std::is_same_v<T, double>,
		    double,
		    std::conditional_t<std::is_same_v<T, std::uint64_t>, std::uint64_t, std::int32_t>>;
		constexpr std::size_t InlineScratchSlots = 16;

		bOutHandled = false;
		if (!ActiveModule || FunctionIndex >= ActiveModule->Functions.size() || PendingJitFunctionIndex != FunctionIndex || !PendingJitNativeCode || (ArgumentCount != 0 && !Arguments))
		{
			return true;
		}
		const FBytecodeFunction& Function = ActiveModule->Functions[FunctionIndex];
		const auto LocalCount = std::max<std::size_t>(Function.LocalCount, PendingJitNativeLocalCount);
		if (!bRootInvocation && JitCache)
		{
			const auto* Entry = JitCache->TryGet(FunctionIndex);
			const auto Depth = GetLogicalCallDepth();
			if (Entry && Entry->InlinedCallSites != 0 && (Depth >= CallDepthLimit || Entry->InlinedCallSites >= CallDepthLimit - Depth))
			{
				return true;
			}
		}
		if (Function.bIsMethod || Function.ParamCount != ArgumentCount || LocalCount < ArgumentCount || LocalCount > InlineScratchSlots || PendingJitStackCapacityHint == 0 || PendingJitStackCapacityHint > InlineScratchSlots)
		{
			return true;
		}
		if (!bRootInvocation && GetLogicalCallDepth() >= CallDepthLimit)
		{
			bOutHandled = true;
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}
		if (!bRootInvocation && ActiveJitCallDepth >= MaxJitCallDepth)
		{
			// Continue through the ordinary bridge, which safely transfers the rest
			// of a deep recursion to the iterative VM without consuming host stack.
			return true;
		}

		bOutHandled = true;
		struct FJitCallDepthScope
		{
			explicit FJitCallDepthScope(std::size_t& InDepth, const bool Increment)
			    : Depth(InDepth), bIncrement(Increment)
			{
				if (bIncrement)
				{
					++Depth;
				}
			}
			~FJitCallDepthScope()
			{
				if (bIncrement)
				{
					--Depth;
				}
			}
			std::size_t& Depth;
			bool bIncrement;
		};
		// The core already counted the root backend invocation.
		const FJitCallDepthScope CallDepthScope(ActiveJitCallDepth, !bRootInvocation);
		if constexpr (std::is_same_v<T, std::int32_t>)
		{
			++Stats.ScalarFunctionFastPathI32;
		}
		else if constexpr (std::is_same_v<T, std::uint32_t>)
		{
			++Stats.ScalarFunctionFastPathU32;
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			++Stats.ScalarFunctionFastPathF64;
		}
		else
		{
			++Stats.ScalarFunctionFastPathMixed;
		}
		++Stats.DispatchAttempts;

		TRaw Locals[InlineScratchSlots];
		TRaw Stack[InlineScratchSlots];
		std::fill_n(
		    Locals + ArgumentCount,
		    LocalCount - ArgumentCount,
		    TRaw{});
		for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
		{
			if constexpr (std::is_same_v<T, std::uint32_t>)
			{
				std::memcpy(&Locals[Index], &Arguments[Index], sizeof(TRaw));
			}
			else
			{
				Locals[Index] = Arguments[Index];
			}
		}

		TRaw Result{};
		std::size_t* ScalarFastPath = nullptr;
		if constexpr (std::is_same_v<T, std::int32_t>)
		{
			ScalarFastPath = &Stats.ScalarFunctionFastPathI32;
		}
		else if constexpr (std::is_same_v<T, std::uint32_t>)
		{
			ScalarFastPath = &Stats.ScalarFunctionFastPathU32;
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			ScalarFastPath = &Stats.ScalarFunctionFastPathF64;
		}
		else
		{
			ScalarFastPath = &Stats.ScalarFunctionFastPathMixed;
		}
		JitNative::FNativeRecursionState RecursionState{
		    &ActiveJitCallDepth,
		    MaxDirectScalarCallDepth,
		    CallDepthLimit,
		    0,
		    JitCache ? JitCache->GetDirectI32Targets() : nullptr,
		    JitCache ? JitCache->GetDirectI32TargetCount() : 0u,
		    JitCache ? JitCache->GetDirectF64Targets() : nullptr,
		    JitCache ? JitCache->GetDirectF64TargetCount() : 0u,
		    0xFFFFu};
		++Stats.NativeCfgExecutions;
		if (FunctionIndex == Context.FunctionIndex)
		{
			Context.ExecutionKind = EJitExecutionKind::NativeCfg;
		}
		std::int32_t Status = EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
		if constexpr (std::is_same_v<T, double>)
		{
			Status = reinterpret_cast<JitNative::FLinearF64Kernel>(PendingJitNativeCode)(
			    this, Locals, Stack, Function.bReturnsVoid ? nullptr : &Result, &RecursionState);
		}
		else if constexpr (std::is_same_v<T, std::uint64_t>)
		{
			Status = reinterpret_cast<JitNative::FLinearMixedKernel>(PendingJitNativeCode)(
			    this, Locals, Stack, Function.bReturnsVoid ? nullptr : &Result, &RecursionState);
		}
		else
		{
			Status = reinterpret_cast<JitNative::FLinearI32Kernel>(PendingJitNativeCode)(
			    this, Locals, Stack, Function.bReturnsVoid ? nullptr : &Result, &RecursionState);
		}
		Stats.DispatchAttempts += RecursionState.DirectCalls;
		Stats.Dispatches += RecursionState.DirectCalls + 1u;
		*ScalarFastPath += RecursionState.DirectCalls;
		Stats.DirectNativeCalls += RecursionState.DirectCalls;
		if (Status != 0 && !IsExitRequested())
		{
			++Stats.Failures;
			const std::uint16_t SavedFunctionIndex = PendingJitFunctionIndex;
			const std::uint32_t* const SavedRuntimeErrorInstructionOffsets =
			    PendingJitRuntimeErrorInstructionOffsets;
			const FDebugLine* const SavedDebugLines = PendingJitRuntimeDebugLines;
			const std::size_t SavedDebugLineCount = PendingJitRuntimeDebugLineCount;
			if (RecursionState.FailureFunctionIndex != 0xFFFFu && ActiveModule && RecursionState.FailureFunctionIndex < ActiveModule->Functions.size())
			{
				PendingJitFunctionIndex = RecursionState.FailureFunctionIndex;
				const FJitFunctionEntry* const FailureEntry = JitCache
				    ? JitCache->TryGet(RecursionState.FailureFunctionIndex)
				    : nullptr;
				PendingJitRuntimeErrorInstructionOffsets = FailureEntry
				    ? FailureEntry->RuntimeErrorInstructionOffsets.data()
				    : nullptr;
				PendingJitRuntimeDebugLines = FailureEntry && !FailureEntry->RuntimeDebugLines.empty()
				    ? FailureEntry->RuntimeDebugLines.data()
				    : nullptr;
				PendingJitRuntimeDebugLineCount = FailureEntry
				    ? FailureEntry->RuntimeDebugLines.size()
				    : 0u;
			}
			(void)JitHandleKernelFailure(Status);
			PendingJitFunctionIndex = SavedFunctionIndex;
			PendingJitRuntimeErrorInstructionOffsets =
			    SavedRuntimeErrorInstructionOffsets;
			PendingJitRuntimeDebugLines = SavedDebugLines;
			PendingJitRuntimeDebugLineCount = SavedDebugLineCount;
		}
		if (IsExitRequested() || Status != 0)
		{
			return false;
		}
		if (!Function.bReturnsVoid && OutResult)
		{
			if constexpr (std::is_same_v<T, std::uint32_t>)
			{
				std::memcpy(OutResult, &Result, sizeof(Result));
			}
			else
			{
				*OutResult = Result;
			}
		}
		return !HasError();
	}

	template <typename T>
	bool FExecutionContext::JitInvokeScalarFast(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const T* Arguments,
	    const std::uint8_t ArgumentCount,
	    T* OutResult,
	    bool& bOutHandled,
	    const bool bMethodCall,
	    const FJitFunctionEntry* PreparedEntry, const bool bRootInvocation)
	{
		static_assert(
		    std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, double>,
		    "unsupported scalar JIT ABI");
		using TRaw = std::conditional_t<
		    std::is_same_v<T, double>,
		    double,
		    std::conditional_t<std::is_same_v<T, std::uint64_t>, std::uint64_t, std::int32_t>>;
		constexpr EJitScalarAbi ExpectedAbi =
		    std::is_same_v<T, std::int32_t>    ? EJitScalarAbi::I32
		    : std::is_same_v<T, std::uint32_t> ? EJitScalarAbi::U32
		    : std::is_same_v<T, std::uint64_t> ? EJitScalarAbi::Mixed
		                                       : EJitScalarAbi::F64;

		bOutHandled = false;
		if ((bMethodCall && (JitHasInstanceCallInterceptor() || !ThisObject)) || !ActiveModule || FunctionIndex >= ActiveModule->Functions.size() || (ArgumentCount != 0 && !Arguments))
		{
			return true;
		}
		if (!bMethodCall && PendingJitFunctionIndex == FunctionIndex && PendingJitNativeCode)
		{
			bool bRecursiveHandled = false;
			const bool bRecursiveOk = JitInvokeRecursiveScalarFast(
			    FunctionIndex,
			    Arguments,
			    ArgumentCount,
			    OutResult,
			    bRecursiveHandled, bRootInvocation);
			if (bRecursiveHandled || !bRecursiveOk)
			{
				bOutHandled = bRecursiveHandled;
				return bRecursiveOk;
			}
		}

		const EExecutionBackend Backend = EExecutionBackend::Jit;
		if (Backend == EExecutionBackend::Vm || !JitCache)
		{
			return true;
		}
		const FJitFunctionEntry* Entry = PreparedEntry;
		if (!Entry)
		{
			Entry = JitCache->TryGet(FunctionIndex);
		}
		if (!Entry)
		{
			(void)Services.PrepareFunction(FunctionIndex, true);
			Entry = JitCache->TryGet(FunctionIndex);
		}
		const FBytecodeFunction& Function = ActiveModule->Functions[FunctionIndex];
		if (!bRootInvocation && RequiresCoreLifetimeMethodCall(Function, ThisObject)) return true;
		const bool bRawAbiMatches = Entry && [&]()
		{
			if constexpr (std::is_same_v<T, double>)
			{
				return Entry->ScalarAbi == EJitScalarAbi::F64;
			}
			else if constexpr (std::is_same_v<T, std::uint64_t>)
			{
				return Entry->ScalarAbi == EJitScalarAbi::Mixed;
			}
			else
			{
				return Entry->ScalarAbi == ExpectedAbi || Entry->ScalarAbi == EJitScalarAbi::I32 || Entry->ScalarAbi == EJitScalarAbi::U32;
			}
		}();
		if (!Entry || (Entry->bMutatesScriptFields && JitHasMutationCallback()) || Entry->Kind != EJitEntryKind::NativeCfg || !Entry->NativeCode || !bRawAbiMatches || Function.ParamCount != ArgumentCount || Function.LocalCount < ArgumentCount || Function.bIsMethod != bMethodCall)
		{
			return true;
		}
		if (!bRootInvocation && Entry->InlinedCallSites != 0)
		{
			const auto Depth = GetLogicalCallDepth();
			if (Depth >= CallDepthLimit || Entry->InlinedCallSites >= CallDepthLimit - Depth) return true;
		}

		if (!bRootInvocation && GetLogicalCallDepth() >= CallDepthLimit)
		{
			bOutHandled = true;
			ReportErrorId("diag.call_stack_overflow");
			return false;
		}
		if (!bRootInvocation && ActiveJitCallDepth >= MaxJitCallDepth)
		{
			// Let the boxed bridge enter the iterative interpreter. This protects
			// the host stack without lowering the language-level recursion limit.
			return true;
		}
		bOutHandled = true;
		struct FJitCallDepthScope
		{
			explicit FJitCallDepthScope(std::size_t& InDepth, const bool Increment)
			    : Depth(InDepth), bIncrement(Increment)
			{
				if (bIncrement)
				{
					++Depth;
				}
			}
			~FJitCallDepthScope()
			{
				if (bIncrement)
				{
					--Depth;
				}
			}
			std::size_t& Depth;
			bool bIncrement;
		};
		// The core already counted the root backend invocation.
		const FJitCallDepthScope CallDepthScope(ActiveJitCallDepth, !bRootInvocation);
		if constexpr (std::is_same_v<T, std::int32_t>)
		{
			if (bMethodCall)
			{
				++Stats.ScalarMethodFastPathI32;
			}
			else
			{
				++Stats.ScalarFunctionFastPathI32;
			}
		}
		else if constexpr (std::is_same_v<T, std::uint32_t>)
		{
			if (bMethodCall)
			{
				++Stats.ScalarMethodFastPathU32;
			}
			else
			{
				++Stats.ScalarFunctionFastPathU32;
			}
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			if (bMethodCall)
			{
				++Stats.ScalarMethodFastPathF64;
			}
			else
			{
				++Stats.ScalarFunctionFastPathF64;
			}
		}
		else
		{
			if (bMethodCall)
			{
				++Stats.ScalarMethodFastPathMixed;
			}
			else
			{
				++Stats.ScalarFunctionFastPathMixed;
			}
		}
		++Stats.DispatchAttempts;

		const bool bReusePendingContext = !bMethodCall && !Entry->bRequiresThisObject && PendingJitFunctionIndex == FunctionIndex && PendingJitNativeCode == Entry->NativeCode;
		const auto NativeLocals = std::max<std::size_t>(Function.LocalCount, Entry->NativeLocalCount);
		const std::size_t StackSlots = Entry->StackCapacityHint != 0
		    ? Entry->StackCapacityHint
		    : Function.Code.size() + 16u;

		// Keep the native-call bridge frame compact. Scalar CFG functions normally
		// need only a handful of local/operand slots, while reserving two 64-slot
		// arrays here made every recursive edge consume and touch an unnecessarily
		// large host stack frame. Larger functions still use the reusable scratch
		// lease, so this changes neither capacity nor semantics.
		constexpr std::size_t InlineScratchSlots = 16;
		TRaw LocalScratch[InlineScratchSlots];
		JitScratch::TRawScratchLease<TRaw> LocalHeap(
		    NativeLocals > InlineScratchSlots ? NativeLocals : 0);
		TRaw* const Locals = NativeLocals > InlineScratchSlots
		    ? LocalHeap.data()
		    : LocalScratch;
		// Parameter slots are written immediately below. Initialize only the
		// remaining locals so argument-only leaf methods do no redundant work.
		std::fill_n(
		    Locals + ArgumentCount,
		    NativeLocals - ArgumentCount,
		    TRaw{});
		// Script parameters are by-value locals. Arguments is deliberately const:
		// callers may provide immutable storage, and the callee may assign to its
		// parameter slots without writing through to that storage.
		for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
		{
			if constexpr (std::is_same_v<T, std::uint32_t>)
			{
				std::memcpy(&Locals[Index], &Arguments[Index], sizeof(TRaw));
			}
			else
			{
				Locals[Index] = Arguments[Index];
			}
		}

		TRaw StackScratch[InlineScratchSlots];
		JitScratch::TRawScratchLease<TRaw> StackHeap(
		    StackSlots > InlineScratchSlots ? StackSlots : 0);
		TRaw* const Stack = StackSlots > InlineScratchSlots
		    ? StackHeap.data()
		    : StackScratch;

		// Direct self-recursion of a free scalar function already runs under the
		// exact generation-scoped entry context established by its caller. Reusing
		// that immutable context avoids publishing and restoring the same dozen VM
		// bridge fields at every recursive edge. Methods are excluded because their
		// receiver is call-specific, and the NativeCode identity prevents an entry
		// from being reused across cache invalidation or hot reload.
		const FValue* const SavedArgs = PendingJitArguments;
		const std::uint8_t SavedCount = PendingJitArgumentCount;
		FObjectHandle SavedThis;
		const bool bThisChanged = Entry->bRequiresThisObject && PendingJitThis != ThisObject;
		if (bThisChanged)
		{
			// Moving the outer receiver avoids an unnecessary shared_ptr refcount
			// pair. Calls on implicit this normally take the even cheaper no-op path.
			SavedThis = std::move(PendingJitThis);
			PendingJitThis = ThisObject;
		}
		const std::uint16_t SavedFn = PendingJitFunctionIndex;
		const FBytecodeFunction* const SavedLinearFunction =
		    PendingJitLinearFunction;
		const JitLinear::FTypedLoopPlanSet* const SavedTypedLoopPlans =
		    PendingJitTypedLoopPlans;
		const std::uint32_t* const SavedRuntimeErrorInstructionOffsets =
		    PendingJitRuntimeErrorInstructionOffsets;
		const FDebugLine* const SavedRuntimeDebugLines = PendingJitRuntimeDebugLines;
		const std::size_t SavedRuntimeDebugLineCount = PendingJitRuntimeDebugLineCount;
		const std::int32_t SavedPayload = PendingJitPayloadI32;
		const double SavedPayloadF64 = PendingJitPayloadF64;
		void* const SavedNative = PendingJitNativeCode;
		const std::uint16_t SavedStackCapacityHint = PendingJitStackCapacityHint;
		const auto SavedNativeLocalCount = PendingJitNativeLocalCount;
		const std::uint16_t SavedResolvedThisFieldOwnerTypeIndex =
		    PendingJitResolvedThisFieldOwnerTypeIndex;
		const std::uint32_t* const SavedResolvedThisFields =
		    PendingJitResolvedThisFields;
		const std::size_t SavedResolvedThisFieldCount =
		    PendingJitResolvedThisFieldCount;
		FScriptObject* const SavedDirectThisFieldObject =
		    PendingJitDirectThisFieldObject;
		FValue* const SavedDirectThisFieldStorage =
		    PendingJitDirectThisFieldStorage;
		const std::uint16_t SavedDirectThisFieldNameIndex =
		    PendingJitDirectThisFieldNameIndex;
		const std::uint16_t SavedDirectThisFieldIndex =
		    PendingJitDirectThisFieldIndex;

		if (!bReusePendingContext)
		{
			PendingJitArguments = nullptr;
			PendingJitArgumentCount = 0;
			PendingJitFunctionIndex = FunctionIndex;
			PendingJitLinearFunction = Entry->LinearFunction.get();
			PendingJitTypedLoopPlans = Entry->TypedLoopPlans.get();
			PendingJitRuntimeErrorInstructionOffsets =
			    Entry->RuntimeErrorInstructionOffsets.data();
			PendingJitRuntimeDebugLines = Entry->RuntimeDebugLines.empty()
			    ? nullptr
			    : Entry->RuntimeDebugLines.data();
			PendingJitRuntimeDebugLineCount = Entry->RuntimeDebugLines.size();
			PendingJitPayloadI32 = Entry->PayloadI32;
			PendingJitPayloadF64 = Entry->PayloadF64;
			PendingJitNativeCode = Entry->NativeCode;
			PendingJitStackCapacityHint = Entry->StackCapacityHint;
			PendingJitNativeLocalCount = Entry->NativeLocalCount;
			PendingJitResolvedThisFieldOwnerTypeIndex =
			    Entry->ResolvedThisFieldOwnerTypeIndex;
			PendingJitResolvedThisFields = Entry->ResolvedThisFields.data();
			PendingJitResolvedThisFieldCount = Entry->ResolvedThisFields.size();
			PendingJitDirectThisFieldObject = nullptr;
			PendingJitDirectThisFieldStorage = nullptr;
			PendingJitDirectThisFieldNameIndex = 0xFFFF;
			PendingJitDirectThisFieldIndex = 0xFFFF;
		}
		// A scalar method that touches one field is by far the common case. Resolve
		// its stable storage once per invocation so both load and store helpers avoid
		// repeating type lookup, binary search and closure-cell unwrapping. The raw
		// pointer never escapes the synchronous kernel call and is guarded by the
		// exact receiver object.
		if (bMethodCall && ThisObject && Entry->ResolvedThisFields.size() == 1)
		{
			FScriptObject* const Object = ThisObject.get();
			const std::uint16_t RuntimeTypeIndex = ResolveObjectTypeIndex(*Object);
			if (RuntimeTypeIndex == Entry->ResolvedThisFieldOwnerTypeIndex)
			{
				const std::uint32_t Encoded = Entry->ResolvedThisFields.front();
				const std::uint16_t FieldIndex =
				    static_cast<std::uint16_t>(Encoded & 0xFFFFu);
				if (FieldIndex < Object->Fields.size())
				{
					PendingJitDirectThisFieldObject = Object;
					PendingJitDirectThisFieldStorage =
					    UnwrapClosureReferenceCell(&Object->Fields[FieldIndex]);
					PendingJitDirectThisFieldNameIndex =
					    static_cast<std::uint16_t>(Encoded >> 16u);
					PendingJitDirectThisFieldIndex = FieldIndex;
				}
			}
		}

		TRaw Result{};
		std::size_t* ScalarFastPath = nullptr;
		if constexpr (std::is_same_v<T, std::int32_t>)
		{
			ScalarFastPath = bMethodCall
			    ? &Stats.ScalarMethodFastPathI32
			    : &Stats.ScalarFunctionFastPathI32;
		}
		else if constexpr (std::is_same_v<T, std::uint32_t>)
		{
			ScalarFastPath = bMethodCall
			    ? &Stats.ScalarMethodFastPathU32
			    : &Stats.ScalarFunctionFastPathU32;
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			ScalarFastPath = bMethodCall
			    ? &Stats.ScalarMethodFastPathF64
			    : &Stats.ScalarFunctionFastPathF64;
		}
		else
		{
			ScalarFastPath = bMethodCall
			    ? &Stats.ScalarMethodFastPathMixed
			    : &Stats.ScalarFunctionFastPathMixed;
		}
		JitNative::FNativeRecursionState RecursionState{
		    &ActiveJitCallDepth,
		    MaxDirectScalarCallDepth,
		    CallDepthLimit,
		    0,
		    JitCache ? JitCache->GetDirectI32Targets() : nullptr,
		    JitCache ? JitCache->GetDirectI32TargetCount() : 0u,
		    JitCache ? JitCache->GetDirectF64Targets() : nullptr,
		    JitCache ? JitCache->GetDirectF64TargetCount() : 0u,
		    0xFFFFu};
		++Stats.NativeCfgExecutions;
		if (FunctionIndex == Context.FunctionIndex)
		{
			Context.ExecutionKind = EJitExecutionKind::NativeCfg;
		}
		std::int32_t Status = EncodeJitKernelFailure(EJitRuntimeStatus::GenericFailure);
		if constexpr (std::is_same_v<T, double>)
		{
			auto* const Kernel =
			    reinterpret_cast<JitNative::FLinearF64Kernel>(Entry->NativeCode);
			Status = Kernel(
			    this,
			    Locals,
			    Stack,
			    Function.bReturnsVoid ? nullptr : &Result,
			    &RecursionState);
		}
		else if constexpr (std::is_same_v<T, std::uint64_t>)
		{
			auto* const Kernel =
			    reinterpret_cast<JitNative::FLinearMixedKernel>(Entry->NativeCode);
			Status = Kernel(
			    this,
			    Locals,
			    Stack,
			    Function.bReturnsVoid ? nullptr : &Result,
			    &RecursionState);
		}
		else
		{
			auto* const Kernel =
			    reinterpret_cast<JitNative::FLinearI32Kernel>(Entry->NativeCode);
			Status = Kernel(
			    this,
			    Locals,
			    Stack,
			    Function.bReturnsVoid ? nullptr : &Result,
			    &RecursionState);
		}
		Stats.DispatchAttempts += RecursionState.DirectCalls;
		Stats.Dispatches += RecursionState.DirectCalls + 1u;
		*ScalarFastPath += RecursionState.DirectCalls;
		Stats.DirectNativeCalls += RecursionState.DirectCalls;

		if (Status != 0)
		{
			// exit() deliberately makes nested call helpers return a non-zero
			// kernel status so every native caller unwinds. It is successful control
			// flow, not a JIT failure diagnostic.
			if (!IsExitRequested())
			{
				++Stats.Failures;
				const std::uint16_t SavedFailureFunctionIndex = PendingJitFunctionIndex;
				const std::uint32_t* const SavedFailureInstructionOffsets =
				    PendingJitRuntimeErrorInstructionOffsets;
				const FDebugLine* const SavedFailureDebugLines = PendingJitRuntimeDebugLines;
				const std::size_t SavedFailureDebugLineCount = PendingJitRuntimeDebugLineCount;
				if (RecursionState.FailureFunctionIndex != 0xFFFFu && ActiveModule && RecursionState.FailureFunctionIndex < ActiveModule->Functions.size())
				{
					PendingJitFunctionIndex = RecursionState.FailureFunctionIndex;
					const FJitFunctionEntry* const FailureEntry = JitCache
					    ? JitCache->TryGet(RecursionState.FailureFunctionIndex)
					    : nullptr;
					PendingJitRuntimeErrorInstructionOffsets = FailureEntry
					    ? FailureEntry->RuntimeErrorInstructionOffsets.data()
					    : nullptr;
					PendingJitRuntimeDebugLines = FailureEntry && !FailureEntry->RuntimeDebugLines.empty()
					    ? FailureEntry->RuntimeDebugLines.data()
					    : nullptr;
					PendingJitRuntimeDebugLineCount = FailureEntry
					    ? FailureEntry->RuntimeDebugLines.size()
					    : 0u;
				}
				(void)JitHandleKernelFailure(Status);
				PendingJitFunctionIndex = SavedFailureFunctionIndex;
				PendingJitRuntimeErrorInstructionOffsets =
				    SavedFailureInstructionOffsets;
				PendingJitRuntimeDebugLines = SavedFailureDebugLines;
				PendingJitRuntimeDebugLineCount = SavedFailureDebugLineCount;
			}
		}

		if (!bReusePendingContext)
		{
			PendingJitArguments = SavedArgs;
			PendingJitArgumentCount = SavedCount;
			if (bThisChanged)
			{
				PendingJitThis = std::move(SavedThis);
			}
			PendingJitFunctionIndex = SavedFn;
			PendingJitLinearFunction = SavedLinearFunction;
			PendingJitTypedLoopPlans = SavedTypedLoopPlans;
			PendingJitRuntimeErrorInstructionOffsets =
			    SavedRuntimeErrorInstructionOffsets;
			PendingJitRuntimeDebugLines = SavedRuntimeDebugLines;
			PendingJitRuntimeDebugLineCount = SavedRuntimeDebugLineCount;
			PendingJitPayloadI32 = SavedPayload;
			PendingJitPayloadF64 = SavedPayloadF64;
			PendingJitNativeCode = SavedNative;
			PendingJitStackCapacityHint = SavedStackCapacityHint;
			PendingJitNativeLocalCount = SavedNativeLocalCount;
			PendingJitResolvedThisFieldOwnerTypeIndex =
			    SavedResolvedThisFieldOwnerTypeIndex;
			PendingJitResolvedThisFields = SavedResolvedThisFields;
			PendingJitResolvedThisFieldCount = SavedResolvedThisFieldCount;
			PendingJitDirectThisFieldObject = SavedDirectThisFieldObject;
			PendingJitDirectThisFieldStorage = SavedDirectThisFieldStorage;
			PendingJitDirectThisFieldNameIndex = SavedDirectThisFieldNameIndex;
			PendingJitDirectThisFieldIndex = SavedDirectThisFieldIndex;
		}

		// A successful exit() may leave the innermost kernel with status 0. The
		// enclosing helper still needs a false result so it returns a non-zero
		// native status and prevents every caller from executing its next opcode.
		if (IsExitRequested())
		{
			return false;
		}
		if (Status != 0)
		{
			return false;
		}
		if (!Function.bReturnsVoid && OutResult)
		{
			if constexpr (std::is_same_v<T, std::uint32_t>)
			{
				std::memcpy(OutResult, &Result, sizeof(Result));
			}
			else
			{
				*OutResult = Result;
			}
		}
		return !HasError();
	}

	bool FExecutionContext::JitTryInvokeBoxedScalarMethodFast(
	    const std::uint16_t FunctionIndex,
	    const FObjectHandle& ThisObject,
	    const FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		if (JitHasInstanceCallInterceptor() || !ActiveModule || !JitCache || !ThisObject || FunctionIndex >= ActiveModule->Functions.size())
		{
			return true;
		}
		const FBytecodeFunction& Function = ActiveModule->Functions[FunctionIndex];
		// Core dispatch owns constructor completion, failed-subobject cleanup and
		// the virtual target for the current constructor or destructor phase.
		if (RequiresCoreLifetimeMethodCall(Function, ThisObject)) return true;
		const FJitFunctionEntry* const Entry = JitCache->TryGet(FunctionIndex);
		if (Function.bDeleted || !Entry || (Entry->bMutatesScriptFields && JitHasMutationCallback()) || Entry->Kind != EJitEntryKind::NativeCfg || !Entry->NativeCode || Entry->ScalarParameterCount != ArgumentCount || Entry->ScalarReturnKind == EJitScalarValueKind::None || ArgumentCount > 16 || (ArgumentCount != 0 && !Arguments))
		{
			return true;
		}

		const auto ParameterKind = [Entry](const std::uint8_t Index)
		{
			return static_cast<EJitScalarValueKind>(
			    (Entry->ScalarParameterKinds >> (static_cast<unsigned>(Index) * 4u)) & 0xFu);
		};
		const bool bReturnCompatible = [&]()
		{
			switch (Entry->ScalarAbi)
			{
			case EJitScalarAbi::I32:
				return Entry->ScalarReturnKind == EJitScalarValueKind::I32 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void;
			case EJitScalarAbi::U32:
				return Entry->ScalarReturnKind == EJitScalarValueKind::U32 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void;
			case EJitScalarAbi::F64:
				return Entry->ScalarReturnKind == EJitScalarValueKind::F64 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void;
			case EJitScalarAbi::Mixed:
				return Entry->ScalarReturnKind == EJitScalarValueKind::I8 || Entry->ScalarReturnKind == EJitScalarValueKind::U8 || Entry->ScalarReturnKind == EJitScalarValueKind::I16 || Entry->ScalarReturnKind == EJitScalarValueKind::U16 || Entry->ScalarReturnKind == EJitScalarValueKind::I32 || Entry->ScalarReturnKind == EJitScalarValueKind::U32 || Entry->ScalarReturnKind == EJitScalarValueKind::I64 || Entry->ScalarReturnKind == EJitScalarValueKind::U64 || Entry->ScalarReturnKind == EJitScalarValueKind::F32 || Entry->ScalarReturnKind == EJitScalarValueKind::F64 || Entry->ScalarReturnKind == EJitScalarValueKind::Bool || Entry->ScalarReturnKind == EJitScalarValueKind::Void;
			default:
				return false;
			}
		}();
		if (!bReturnCompatible)
		{
			return true;
		}
		const auto PushResult = [&](const auto RawResult) -> bool
		{
			using TRaw = std::remove_cv_t<decltype(RawResult)>;
			if constexpr (std::is_same_v<TRaw, std::uint64_t>)
			{
				if (Entry->ScalarReturnKind == EJitScalarValueKind::Void)
				{
					PushCallResult(std::int32_t{0});
					return !HasError();
				}
				FValue Decoded;
				if (!JitBridgeDetail::UnpackMixedScalar(
				        Entry->ScalarReturnKind, RawResult, Decoded))
				{
					return false;
				}
				PushCallResult(std::move(Decoded));
				return !HasError();
			}
			else
			{
				switch (Entry->ScalarReturnKind)
				{
				case EJitScalarValueKind::Void:
					{
						PushCallResult(std::int32_t{0});
						break;
					}
				case EJitScalarValueKind::I32:
					{
						if constexpr (std::is_same_v<TRaw, std::int32_t>)
						{
							PushCallResult(RawResult);
						}
						else
						{
							return false;
						}
						break;
					}
				case EJitScalarValueKind::U32:
					{
						if constexpr (std::is_same_v<TRaw, std::uint32_t>)
						{
							PushCallResult(RawResult);
						}
						else
						{
							return false;
						}
						break;
					}
				case EJitScalarValueKind::F64:
					{
						if constexpr (std::is_same_v<TRaw, double>)
						{
							PushCallResult(RawResult);
						}
						else
						{
							return false;
						}
						break;
					}
				case EJitScalarValueKind::Bool:
					{
						PushCallResult(RawResult != static_cast<TRaw>(0));
						break;
					}
				default:
					return false;
				}
				return !HasError();
			}
		};

		// A resolved scalar method whose complete body is a checked update of one
		// stable receiver field does not need a nested VM/JIT frame.  This path is
		// generation-scoped through Entry, retains the ordinary call-depth and
		// mutation semantics, and is valid for both discarded void results and a
		// returned updated i32 field.
		if (Entry->bDirectThisFieldAddModuloI32 && (ArgumentCount == 0 || ArgumentCount == 1))
		{
			const auto* const Delta = ArgumentCount == 1
			    ? std::get_if<std::int32_t>(&Arguments[0])
			    : nullptr;
			std::uint16_t FieldIndex = 0xFFFF;
			std::int32_t* Field = nullptr;
			ResolveJitDirectI32FieldStorage(*ThisObject, *Entry, FieldIndex, Field);
			if ((ArgumentCount == 0 || Delta) && Field)
			{
				if (GetLogicalCallDepth() >= CallDepthLimit)
				{
					bOutHandled = true;
					ReportErrorId("diag.call_stack_overflow");
					return false;
				}
				bOutHandled = true;
				struct FDirectFieldDepthScope
				{
					explicit FDirectFieldDepthScope(std::size_t& InDepth)
					    : Depth(InDepth)
					{
						++Depth;
					}
					~FDirectFieldDepthScope()
					{
						--Depth;
					}
					std::size_t& Depth;
				};
				const FDirectFieldDepthScope DepthScope(ActiveJitCallDepth);
				std::int32_t EffectiveDelta = ArgumentCount == 0
				    ? Entry->DirectThisFieldAddendI32
				    : *Delta;
				if (Entry->DirectThisArgumentModuloI32 != 0)
				{
					const std::int32_t ArgumentModulus =
					    Entry->DirectThisArgumentModuloI32;
					if (EffectiveDelta == std::numeric_limits<std::int32_t>::min() && ArgumentModulus == -1)
					{
						ReportErrorId("diag.integer_overflow");
						return false;
					}
					EffectiveDelta %= ArgumentModulus;
				}
				std::int32_t Sum = 0;
				std::int32_t Updated = 0;
				const std::int32_t Addend = ArgumentCount == 0
				    ? 0
				    : Entry->DirectThisFieldAddendI32;
				const std::int32_t Modulus = Entry->DirectThisFieldModuloI32;
				if (!Runtime::CheckedAdd(*Field, EffectiveDelta, Sum) || !Runtime::CheckedAdd(Sum, Addend, Updated) || (Modulus != 0 && Updated == std::numeric_limits<std::int32_t>::min() && Modulus == -1))
				{
					ReportErrorId("diag.integer_overflow");
					return false;
				}
				*Field = Modulus == 0 ? Updated : Updated % Modulus;
				NotifyFieldMutationJitFast(*ThisObject, FieldIndex);
				++Stats.ScalarMethodFastPathI32;
				std::int32_t ReturnedField = 0;
				if (Entry->ScalarReturnKind == EJitScalarValueKind::I32 && !JitHasMutationCallback())
				{
					ReturnedField = *Field;
				}
				else if (Entry->ScalarReturnKind == EJitScalarValueKind::I32 && !JitReadDirectI32FieldAfterMutation(*ThisObject, FieldIndex, ReturnedField))
				{
					return false;
				}
				return PushResult(ReturnedField);
			}
		}

		// Pack every live slot before entry; unused capacity is outside the scalar ABI.
		switch (Entry->ScalarAbi)
		{
		case EJitScalarAbi::I32:
			{
				std::int32_t RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					switch (ParameterKind(Index))
					{
					case EJitScalarValueKind::I32:
						{
							if (const auto* Value = std::get_if<std::int32_t>(&Arguments[Index]))
							{
								RawArguments[Index] = *Value;
								break;
							}
							return true;
						}
					case EJitScalarValueKind::Bool:
						{
							if (const auto* Value = std::get_if<bool>(&Arguments[Index]))
							{
								RawArguments[Index] = *Value ? 1 : 0;
								break;
							}
							return true;
						}
					default:
						return true;
					}
				}
				std::int32_t Result = 0;
				bool bHandled = false;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, ThisObject, RawArguments, ArgumentCount,
				    &Result, bHandled, true);
				if (!bHandled)
				{
					return true;
				}
				bOutHandled = true;
				return bOk && PushResult(Result);
			}
		case EJitScalarAbi::U32:
			{
				std::uint32_t RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					switch (ParameterKind(Index))
					{
					case EJitScalarValueKind::U32:
						{
							if (const auto* Value = std::get_if<std::uint32_t>(&Arguments[Index]))
							{
								RawArguments[Index] = *Value;
								break;
							}
							return true;
						}
					case EJitScalarValueKind::Bool:
						{
							if (const auto* Value = std::get_if<bool>(&Arguments[Index]))
							{
								RawArguments[Index] = *Value ? 1u : 0u;
								break;
							}
							return true;
						}
					default:
						return true;
					}
				}
				std::uint32_t Result = 0;
				bool bHandled = false;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, ThisObject, RawArguments, ArgumentCount,
				    &Result, bHandled, true);
				if (!bHandled)
				{
					return true;
				}
				bOutHandled = true;
				return bOk && PushResult(Result);
			}
		case EJitScalarAbi::F64:
			{
				double RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					if (ParameterKind(Index) != EJitScalarValueKind::F64)
					{
						return true;
					}
					const auto* const Value = std::get_if<double>(&Arguments[Index]);
					if (!Value)
					{
						return true;
					}
					RawArguments[Index] = *Value;
				}
				double Result = 0.0;
				bool bHandled = false;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, ThisObject, RawArguments, ArgumentCount,
				    &Result, bHandled, true);
				if (!bHandled)
				{
					return true;
				}
				bOutHandled = true;
				return bOk && PushResult(Result);
			}
		case EJitScalarAbi::Mixed:
			{
				std::uint64_t RawArguments[16];
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					if (!JitBridgeDetail::PackMixedScalar(
					        ParameterKind(Index), Arguments[Index], RawArguments[Index]))
					{
						return true;
					}
				}
				std::uint64_t Result = 0;
				bool bHandled = false;
				const bool bOk = JitInvokeScalarFast(
				    FunctionIndex, ThisObject, RawArguments, ArgumentCount,
				    &Result, bHandled, true);
				if (!bHandled)
				{
					return true;
				}
				bOutHandled = true;
				return bOk && PushResult(Result);
			}
		default:
			return true;
		}
	}

	bool FExecutionContext::ResolveJitDirectI32FieldStorage(
	    FScriptObject& Object,
	    const FJitFunctionEntry& Entry,
	    std::uint16_t& OutFieldIndex,
	    std::int32_t*& OutStorage)
	{
		OutFieldIndex = 0xFFFF;
		OutStorage = nullptr;
		std::uint16_t FieldIndex = Entry.DirectThisFieldIndex;
		const std::uint16_t RuntimeTypeIndex =
		    Object.RuntimeTypeModule == ActiveModule && Object.RuntimeTypeGeneration == ProgramGeneration && ActiveModule && Object.RuntimeTypeIndex < ActiveModule->Types.size()
		    ? Object.RuntimeTypeIndex
		    : ResolveObjectTypeIndex(Object);
		if (RuntimeTypeIndex == 0xFFFF)
		{
			return false;
		}
		if (RuntimeTypeIndex != Entry.ResolvedThisFieldOwnerTypeIndex)
		{
			std::uint16_t NameIndex = 0xFFFF;
			for (const std::uint32_t Encoded : Entry.ResolvedThisFields)
			{
				if (static_cast<std::uint16_t>(Encoded & 0xFFFFu) == Entry.DirectThisFieldIndex)
				{
					NameIndex = static_cast<std::uint16_t>(Encoded >> 16u);
					break;
				}
			}
			if (NameIndex == 0xFFFF)
			{
				return false;
			}
			FieldIndex = FindJitFieldIndex(RuntimeTypeIndex, NameIndex);
		}
		if (FieldIndex == 0xFFFF || FieldIndex >= Object.Fields.size())
		{
			return false;
		}
		FValue* const FieldValue = UnwrapClosureReferenceCell(&Object.Fields[FieldIndex]);
		auto* const Field = FieldValue
		    ? std::get_if<std::int32_t>(FieldValue)
		    : nullptr;
		if (!Field)
		{
			return false;
		}
		OutFieldIndex = FieldIndex;
		OutStorage = Field;
		return true;
	}

	void FExecutionContext::JitRefinePendingRuntimeErrorLocation(
	    const EJitRuntimeStatus Status)
	{
		const std::size_t StatusIndex = static_cast<std::size_t>(Status);
		if (!PendingJitRuntimeErrorInstructionOffsets || StatusIndex >= static_cast<std::size_t>(EJitRuntimeStatus::Count))
		{
			return;
		}
		const std::uint32_t InstructionOffset =
		    PendingJitRuntimeErrorInstructionOffsets[StatusIndex];
		if (InstructionOffset != InvalidJitInstructionOffset)
		{
			JitRefineRuntimeErrorLocation(InstructionOffset);
		}
	}
}
