// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonBindingInternal.h"
#include "VeroScriptMemory.h"

namespace VeroScript::Json::Binding
{
	enum class EBuilderTry
	{
		Push,
		Set,
		Reserve,
		Finish,
		ResetArray,
		ResetObject,
		Size,
		Capacity
	};
	template <EBuilderTry Action>
	bool BuilderTry(const FNativeCallContext& Original, VeroScript::FValue& Out, std::string& Error)
	{
		auto C = Original;
		++C.Args;
		if (C.MutableArgs)
		{
			++C.MutableArgs;
		}
		--C.ArgCount;
		auto& S = *static_cast<FState*>(C.UserData);
		constexpr std::size_t ErrorIndex = Action == EBuilderTry::Set ? 2
										   : Action == EBuilderTry::ResetArray || Action == EBuilderTry::ResetObject
											   ? 0
											   : 1;
		if (!C.MutableArg(ErrorIndex))
		{
			Error = "JSON builder try API requires ref error";
			return false;
		}
		constexpr bool HasOutput =
			Action == EBuilderTry::Finish || Action == EBuilderTry::Size || Action == EBuilderTry::Capacity;
		if constexpr (HasOutput)
		{
			if (!C.MutableArg(0))
			{
				Error = "JSON builder try API requires ref output";
				return false;
			}
		}
		const auto Work = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
		if (Work.Check && !Work.Check(Work.UserData, 1))
		{
			Error = "JSON builder operation interrupted";
			return false;
		}
		VeroScript::FValue Diagnostic, Result;
		if (!Wrap(S, FError{}, Diagnostic, Error))
		{
			return false;
		}
		if constexpr (Action == EBuilderTry::Finish)
		{
			if (!Wrap(S, FJsonValue{}, Result, Error))
			{
				return false;
			}
		}
		FError Failure;
		FJsonValue Value;
		std::int64_t Count = 0;
		bool Success;
		auto& Builder = *static_cast<FBuilder*>(C.HostThis);
		if constexpr (Action == EBuilderTry::Push)
		{
			Success = Builder.TryPush(Object<FJsonValue>(C, 0), Failure);
		}
		else if constexpr (Action == EBuilderTry::Set)
		{
			Success = Builder.TrySet(std::get<std::string>(C.Args[0]), Object<FJsonValue>(C, 1), Failure);
		}
		else if constexpr (Action == EBuilderTry::Reserve)
		{
			Success = Builder.TryReserve(std::get<std::int64_t>(C.Args[0]), Failure);
		}
		else if constexpr (Action == EBuilderTry::Finish)
		{
			Success = Builder.TryFinish(Value, Failure);
		}
		else if constexpr (Action == EBuilderTry::Size)
		{
			Success = Builder.TrySize(Count, Failure);
		}
		else if constexpr (Action == EBuilderTry::Capacity)
		{
			Success = Builder.TryCapacity(Count, Failure);
		}
		else
		{
			Success = Builder.TryReset(Action == EBuilderTry::ResetArray ? EKind::Array : EKind::Object, Failure);
		}
		if constexpr (Action == EBuilderTry::Finish)
		{
			if (Success)
			{
				*static_cast<FJsonValue*>(std::get<FHostObjectHandle>(Result)->Payload) = std::move(Value);
			}
		}
		*static_cast<FError*>(std::get<FHostObjectHandle>(Diagnostic)->Payload) = std::move(Failure);
		if constexpr (HasOutput)
		{
			if (Success)
			{
				if constexpr (Action == EBuilderTry::Finish)
				{
					C.AssignValue(*C.MutableArg(0), std::move(Result));
				}
				else
				{
					C.AssignValue(*C.MutableArg(0), Count);
				}
			}
		}
		C.AssignValue(*C.MutableArg(ErrorIndex), std::move(Diagnostic));
		Out = Success;
		return true;
	}
	bool RegisterBuilderTry(FState& S, FHostTypeId Id, std::string& Error)
	{
		using T = ENativeType;
		constexpr auto R = NativeParamFlagRef;
		return Method<BuilderTry<EBuilderTry::Push>>(S,
				   Id,
				   "try_push",
				   T::Bool,
				   {T::HostObject, T::HostObject},
				   Error,
				   true,
				   "",
				   {"json::value", "json::error"},
				   {0, R}) &&
			   Method<BuilderTry<EBuilderTry::Set>>(S,
				   Id,
				   "try_set",
				   T::Bool,
				   {T::String, T::HostObject, T::HostObject},
				   Error,
				   true,
				   "",
				   {"", "json::value", "json::error"},
				   {0, 0, R}) &&
			   Method<BuilderTry<EBuilderTry::Reserve>>(S,
				   Id,
				   "try_reserve",
				   T::Bool,
				   {T::Int64, T::HostObject},
				   Error,
				   true,
				   "",
				   {"", "json::error"},
				   {0, R}) &&
			   Method<BuilderTry<EBuilderTry::Finish>>(S,
				   Id,
				   "try_finish",
				   T::Bool,
				   {T::HostObject, T::HostObject},
				   Error,
				   true,
				   "",
				   {"json::value", "json::error"},
				   {R, R}) &&
			   Method<BuilderTry<EBuilderTry::ResetArray>>(
				   S, Id, "try_reset_array", T::Bool, {T::HostObject}, Error, true, "", {"json::error"}, {R}) &&
			   Method<BuilderTry<EBuilderTry::ResetObject>>(
				   S, Id, "try_reset_object", T::Bool, {T::HostObject}, Error, true, "", {"json::error"}, {R}) &&
			   Method<BuilderTry<EBuilderTry::Size>>(S,
				   Id,
				   "try_size",
				   T::Bool,
				   {T::Int64, T::HostObject},
				   Error,
				   false,
				   "",
				   {"", "json::error"},
				   {R, R}) &&
			   Method<BuilderTry<EBuilderTry::Capacity>>(S,
				   Id,
				   "try_capacity",
				   T::Bool,
				   {T::Int64, T::HostObject},
				   Error,
				   false,
				   "",
				   {"", "json::error"},
				   {R, R});
	}
} // namespace VeroScript::Json::Binding
