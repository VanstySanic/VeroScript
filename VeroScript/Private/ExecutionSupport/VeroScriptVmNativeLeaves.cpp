// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmNativeLeaves.h"
#include "VeroScriptVm.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptVmArguments.h"
#include <exception>

namespace VeroScript
{
	bool VeroScriptVm::TryCallStandardValueLeaf(const std::uint16_t StandardIndex,
	    const std::uint8_t ArgumentCount, const FSourceLocation& Location, bool& Handled)
	{
		Handled = false;
		const auto& Catalog = StandardLibrary::GetBindingCatalog().GetNativeFunctions();
		if (ActiveNatives != &Catalog || ExternalExecutionCheck || NativeValueRoots || MutationCallback || StandardIndex >= Catalog.size() || ArgumentCount > 2 || OperandStack.size() < ArgumentCount)
		{
			return true;
		}
		const auto Start = OperandStack.size() - ArgumentCount;
		const auto* Arguments = ArgumentCount ? OperandStack.data() + Start : nullptr;
		for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
		{
			const auto* Host = std::get_if<FHostObjectHandle>(&Arguments[Index]);
			if (Host && *Host && (*Host)->ProviderIdentity) return true;
		}
		const auto& Native = Catalog[StandardIndex];
		FValue Output;
		FScriptError Diagnostic;
		std::string NativeError;
		bool Success = false;
		try
		{
			if (StandardIndex < Memory::GetBindingCatalog().GetNativeFunctions().size())
			{
				FMemoryResult Result;
				Handled = Memory::TryInvokeValueLeaf(StandardIndex, *this, Arguments,
				    ArgumentCount, Output, Result);
				Success = static_cast<bool>(Result);
				if (Handled && !Success)
				{
					NativeError = Result.MessageId;
				}
			}
			else
			{
				FNativeCallContext Context;
				Context.Args = Arguments;
				Context.ArgCount = ArgumentCount;
				Context.UserData = Native.UserData;
				Context.NumericServices = this;
				Context.DiagnosticOutput = &Diagnostic;
				Handled = Numerics::TryInvokeValueLeaf(Native, Context, Output, NativeError, Success);
			}
		}
		catch (const std::exception& Exception)
		{
			Handled = true;
			Diagnostic = {};
			NativeError = std::string("native callback threw: ") + Exception.what();
		}
		catch (...)
		{
			Handled = true;
			Diagnostic = {};
			NativeError = "native callback threw an unknown exception";
		}
		if (!Handled) return true;
		// These private callbacks cannot reenter and have no reference operands.
		// Keep final owners through result/error publication, then destroy arguments
		// in reverse order, matching ordinary CallNative's external release order.
		VmDetail::FArgumentBuffer Owners(ArgumentCount);
		for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
		{
			Owners.data()[Index] = std::move(OperandStack[Start + Index]);
		}
		OperandStack.resize(Start);
		if (!Success) return NativeFailure(std::move(Diagnostic), std::move(NativeError), Native.Name, Location);
		Push(std::move(Output));
		for (std::size_t Index = ArgumentCount; Index > 0; --Index)
		{
			Owners.data()[Index - 1] = std::int32_t{0};
		}
		return !bHasError;
	}
}
