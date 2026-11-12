// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <utility>

namespace VeroScript::AsmJit
{
	bool FExecutionContext::JitCanInlineTextProjection() const noexcept
	{
		return !JitHasInstanceCallInterceptor() && !JitHasMutationCallback()
			&& Calls && Calls->GetRemainingCallDepth() != 0u
			&& GetLogicalCallDepth() < CallDepthLimit && !HasPendingControlTransfer() && !HasError();
	}

	void FExecutionContext::JitReportTextFailure(const char* Message,
		const std::uint32_t CallerOffset, const std::uint16_t FunctionIndex,
		const std::uint32_t CalleeOffset) noexcept
	{
		// Use the non-owning service boundary, including while unwinding an
		// allocation failure during native-local commit.
		if (!Message || HasError()) return;
		Services.ReportError(Message);
		if (FunctionIndex != 0xFFFFu)
		{
			Services.RefineErrorLocation(FunctionIndex, CalleeOffset, nullptr, 0u);
		}
		else
		{
			JitRefineRuntimeErrorLocation(CallerOffset);
		}
	}

	void FExecutionContext::JitReportDirectI32TransformFailure(const std::uint16_t Slot,
		const std::uint16_t MethodNameIndex, const std::uint16_t BoundFunctionIndex,
		const std::uint32_t CallSiteOffset, const std::size_t CalleeInstructionOffset, std::string Message)
	{
		std::uint16_t Target = BoundFunctionIndex;
		if (Target == 0xFFFF)
		{
			auto* Local = Scalar->BorrowDirectLocal(Slot);
			const auto* Receiver = Local ? std::get_if<FObjectHandle>(Local) : nullptr;
			if (!Receiver || !*Receiver || !ResolveScriptObjectMethod(*Receiver, MethodNameIndex, 1, CallSiteOffset, Target))
			{
				Services.ReportError(Message);
				return;
			}
		}
		Services.ReportError(Message);
		Services.RefineErrorLocation(Target, CalleeInstructionOffset, nullptr, 0);
	}

	bool FExecutionContext::JitTryCallScalarMethodOnLocalNoArgs(const std::uint16_t Slot,
		const std::uint16_t MethodNameIndex, const std::uint32_t CallSiteOffset,
		FValue& OutValue, bool& OutHandled)
	{
		return JitTryCallScalarMethodOnLocal(Slot, MethodNameIndex, nullptr, 0, CallSiteOffset, OutValue, OutHandled);
	}

	bool FExecutionContext::JitCallMethodOnLocal(const std::uint16_t Slot,
		const std::uint16_t MethodNameIndex, const FValue* Arguments,
		const std::uint8_t ArgumentCount, const std::uint32_t CallSiteOffset)
	{
		auto* Receiver = Frames->BorrowResolvedLocal(Frame, Slot);
		if (!Receiver) return false;
		return JitCallMethod(MethodNameIndex, *Receiver, Arguments, ArgumentCount, CallSiteOffset);
	}

	bool FExecutionContext::ResolveScriptObjectMethod(const FObjectHandle& Receiver,
		const std::uint16_t NameIndex, const std::uint8_t ArgumentCount,
		const std::uint32_t CallSite, std::uint16_t& OutFunctionIndex)
	{
		bool Handled = false;
		return Calls->ResolveMethod(Receiver, NameIndex, ArgumentCount, CallSite, OutFunctionIndex, Handled);
	}

	std::uint16_t FExecutionContext::ResolveObjectTypeIndex(const FScriptObject& Object) const
	{
		std::uint16_t TypeIndex = 0xFFFF;
		(void)Values->ResolveObjectLayout(Object, TypeIndex);
		return TypeIndex;
	}

	std::uint16_t FExecutionContext::FindJitFieldIndex(const std::uint16_t TypeIndex,
		const std::uint16_t NameIndex) const
	{
		if (TypeIndex >= ActiveModule->Types.size() || NameIndex >= ActiveModule->StringPool.size()) return 0xFFFF;
		const auto& Fields = ActiveModule->Types[TypeIndex].Fields;
		const auto& Name = ActiveModule->StringPool[NameIndex];
		for (std::size_t Index = 0; Index < Fields.size() && Index < 65536; ++Index)
		{
			if (Fields[Index].Name == Name) return static_cast<std::uint16_t>(Index);
		}
		return 0xFFFF;
	}

	FValue* FExecutionContext::FindPendingJitThisFieldStorage(FScriptObject& Object,
		const std::uint16_t NameIndex, std::uint16_t& OutFieldIndex) const
	{
		if (PendingJitDirectThisFieldObject == &Object && PendingJitDirectThisFieldStorage
			&& PendingJitDirectThisFieldNameIndex == NameIndex)
		{
			OutFieldIndex = PendingJitDirectThisFieldIndex;
			return PendingJitDirectThisFieldStorage;
		}
		FValue* Storage = nullptr;
		(void)Values->BorrowObjectField(Object, NameIndex, OutFieldIndex, Storage);
		return Storage;
	}

	void FExecutionContext::ReportError(const std::string_view Message)
	{
		Services.ReportError(Message);
	}

	void FExecutionContext::ReportErrorId(const std::string_view Id, FSourceLocation,
		const std::initializer_list<std::string> Arguments)
	{
		Services.ReportErrorId(Id, Arguments.size() == 0 ? std::string_view{} : *Arguments.begin());
	}

	void FExecutionContext::PushCallResult(FValue Value)
	{
		if (!Services.ReleaseValue(CallResult))
		{
			(void)Services.ReleaseValue(Value);
			return;
		}
		CallResult = std::move(Value);
		bHasCallResult = true;
	}

	std::size_t FExecutionContext::GetLogicalCallDepth() const noexcept
	{
		return ActiveJitCallDepth;
	}

	std::size_t FExecutionContext::CurrentLocalCount() const noexcept
	{
		return Frame.Identity == 0 ? 0 : PendingJitLinearFunction
			? PendingJitLinearFunction->LocalCount : ActiveModule->Functions[Context.FunctionIndex].LocalCount;
	}
}
