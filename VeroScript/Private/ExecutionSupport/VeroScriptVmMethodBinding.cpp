// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptMethodBinding.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"

namespace VeroScript
{
	bool VeroScriptVm::CallMethodBindingBuiltin(
		EBuiltinId Builtin, FValue* Arguments, std::uint8_t ArgumentCount)
	{
		FMethodBindingContext Context;
		Context.Module = ActiveModule;
		Context.Bindings = MemoryBindings;
		Context.Natives = ActiveNatives;
		Context.ProgramOwner = ProgramOwner;
		Context.ProgramGeneration = ProgramGeneration;
		Context.CheckWork = [this]
		{
			return CheckExecutionGovernance();
		};
		std::string BindingError;
		if (Builtin == EBuiltinId::MethodBindingResolve)
		{
			if (ArgumentCount != 4) return false;
			const auto* ReceiverType = std::get_if<std::string>(&Arguments[0]);
			const auto* FunctionType = std::get_if<std::string>(&Arguments[1]);
			const auto* ReadOnly = std::get_if<bool>(&Arguments[2]);
			const auto* Name = std::get_if<std::string>(&Arguments[3]);
			std::shared_ptr<const FMethodBindingDescriptor> Descriptor;
			if (!ReceiverType || !FunctionType || !ReadOnly || !Name)
			{
				ReportError("invalid method binding request operands");
				return false;
			}
			FMethodBindingRequest Request{*ReceiverType, *Name, *FunctionType, *ReadOnly};
			if (!ResolveMethodBinding(Context, Request, Descriptor, BindingError))
			{
				if (!bHasError)
				{
					ReportError(BindingError);
				}

				return false;
			}
			Push(MakeMethodBindingToken(std::move(Descriptor)));
			return !bHasError;
		}
		if (ArgumentCount < 1) return false;
		const auto Descriptor = ReadMethodBindingToken(Arguments[0]);
		if (!Descriptor)
		{
			ReportError("invalid method binding descriptor");
			return false;
		}
		if (Builtin == EBuiltinId::MethodBindingValidate)
		{
			if (ArgumentCount != 1 || !ValidateMethodBinding(Context, *Descriptor, BindingError))
			{
				if (!bHasError)
				{
					ReportError(BindingError);
				}

				return false;
			}
			return true;
		}
		if (ArgumentCount < 2) return false;
		std::vector<FValue> Values(Arguments + 2, Arguments + ArgumentCount);
		std::vector<std::string> ParameterTypes;
		std::string ReturnType;
		if (!Runtime::ParseFunctionType(Descriptor->Request.FunctionType, ParameterTypes, ReturnType) ||
			ParameterTypes.size() != Values.size())
		{
			ReportError("bound method argument signature mismatch");
			return false;
		}
		for (std::size_t Index = 0; Index < Values.size(); ++Index)
		{
			FValue* Storage = &Values[Index];
			if (Runtime::IsReferenceTypeName(ParameterTypes[Index]))
			{
				Storage = ResolveRefStorage(Values[Index]);
				if (!Storage || Storage == &Values[Index])
				{
					ReportError("bound method reference argument has no valid storage");
					return false;
				}
			}
			if (!ValueMatchesElementType(
					*Storage, std::string(Runtime::StripReferenceTypeName(ParameterTypes[Index])), {}))
			{
				ReportError("bound method argument type mismatch");
				return false;
			}
		}
		FValue* ReceiverStorage = ResolveRefStorage(Arguments[1]);
		if (!ReceiverStorage)
		{
			ReportError("bound method receiver storage is invalid");
			return false;
		}
		const FValue ReceiverValue = *ReceiverStorage;
		const auto Invoke = [this, &Arguments, &ReceiverValue](const FMethodBindingDescriptor& Binding,
								std::uint16_t FunctionIndex, const FValue& Receiver,
								const std::vector<FValue>& Params, FValue& Result, std::string& Error)
		{
			const std::size_t StackBefore = OperandStack.size();
			if (Binding.Selection.bHost)
			{
				if (Params.size() >= 255)
				{
					Error = "bound native method has too many arguments";
					return false;
				}
				// Preserve the actual wrapper field for mutable host-value write-back.
				const bool MutableHostValue = !Binding.Request.bConstReceiver &&
					!Binding.Native.ParamFlags.empty() && (Binding.Native.ParamFlags[0] & 1) &&
					std::holds_alternative<FHostObjectHandle>(ReceiverValue);
				Push(MutableHostValue ? Arguments[1] : Receiver);
				for (const auto& Param : Params)
				{
					Push(Param);
				}

				if (!CallNative(Binding.NativeIndex, static_cast<std::uint8_t>(Params.size() + 1), {}))
				{
					return false;
				}
				if (Binding.Native.ReturnType == ENativeType::Void)
				{
					Push(std::int32_t{0});
				}

			}
			else
			{
				const auto* Script = std::get_if<FObjectHandle>(&Receiver);
				if (Script && *Script && Binding.Selection.bVirtual
					&& ((*Script)->bConstructionIncomplete || (*Script)->bDestroying))
				{
					const auto Type = ResolveObjectDispatchTypeIndex(**Script, ResolveObjectTypeIndex(**Script));
					if (Type >= ActiveModule->Types.size()
						|| ModuleMeta::TryResolveInstanceMethodOnType(*ActiveModule, ActiveModule->Types[Type].Name,
							Binding.Selection.DispatchKey, static_cast<std::uint8_t>(Params.size()), FunctionIndex)
							!= EMethodResolveStatus::Found
						|| ActiveModule->Functions[FunctionIndex].bIsAbstract)
					{
						Error = "bound virtual method has no executable implementation";
						return false;
					}
				}
				if (!Script || !*Script ||
					!InvokeResolvedInstanceMethod(FunctionIndex, *Script,
						Params.empty() ? nullptr : Params.data(), static_cast<std::uint8_t>(Params.size()),
						{}, true))
				{
					return false;
				}
			}
			if (bExitRequested)
			{
				Result = std::int32_t{0};
				return true;
			}
			if (OperandStack.size() != StackBefore + 1)
			{
				Error = "bound method did not produce its ordinary call result";
				return false;
			}
			Result = Pop();
			return !bHasError;
		};
		FValue Result;
		if (!InvokeBoundMethod(Context, *Descriptor, ReceiverValue, Values, Invoke, Result, BindingError))
		{
			if (!bHasError && !bHasPendingScriptException && !bExitRequested)
			{
				ReportError(BindingError.empty() ? "bound method invocation failed" : BindingError);
			}

			return false;
		}
		if (!bExitRequested)
		{
			Push(std::move(Result));
		}

		return !bHasError;
	}
}
