// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"

#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace VeroScript::Interop
{
	namespace
	{
		vs_interop_status StoreCallContextError(
		    ::vs_interop_call_context& Context,
		    const std::string_view Message,
		    const vs_interop_status FailureStatus) noexcept
		{
			try
			{
				if (Message.empty())
				{
					Context.Error.clear();
				}
				else
				{
					Context.Error.assign(Message.data(), Message.size());
				}
				return FailureStatus;
			}
			catch (const std::bad_alloc&)
			{
				return VS_INTEROP_STATUS_OUT_OF_MEMORY;
			}
			catch (...)
			{
				return VS_INTEROP_STATUS_INTERNAL_ERROR;
			}
		}

		vs_interop_status FinishCall(
		    FSessionState& State,
		    const FRunResult& Result,
		    ::vs_interop_value* const* Arguments,
		    const std::uint64_t ArgumentCount,
		    const std::vector<FValue>& Values,
		    const FValue& Return,
		    ::vs_interop_value* OutReturn,
		    vs_interop_run_result* OutResult)
		{
			const vs_interop_status ResultStatus = StoreRunResult(State, Result, OutResult);
			if (ResultStatus != VS_INTEROP_STATUS_OK)
			{
				return ResultStatus;
			}
			// An exit request completes control flow but does not produce a function
			// return value.
			// Keep control-flow exit requests from writing back ref/out arguments.
			if (Result.bExitRequested)
			{
				return VS_INTEROP_STATUS_OK;
			}
			if (const auto Status = CommitArguments(Arguments, ArgumentCount, Values);
			    Status != VS_INTEROP_STATUS_OK)
			{
				SetError(State, "unable to write back an Interop ref/out argument");
				return Status;
			}
			if (OutReturn)
			{
				AssignValue(*OutReturn, Return);
			}
			return VS_INTEROP_STATUS_OK;
		}

		bool GetObject(const ::vs_interop_value* Value, FObjectHandle& Out, FSessionState& State)
		{
			const FValue* Data = ReadValue(Value, &State);
			const auto* Object = Data ? std::get_if<FObjectHandle>(Data) : nullptr;
			if (!Object || !*Object)
			{
				return false;
			}
			Out = *Object;
			return true;
		}
	}

	bool NativeThunkCallback(
	    const FNativeCallContext& Context,
	    FValue& OutReturn,
	    std::string& OutError) noexcept
	{
		auto* Thunk = static_cast<FNativeThunk*>(Context.UserData);
		if (!Thunk || !Thunk->Callback)
		{
			try
			{
				OutError = "VeroScriptInterop native callback is unavailable";
			}
			catch (...)
			{
			}
			return false;
		}
		try
		{
			::vs_interop_call_context InteropContext;
			InteropContext.Native = &Context;
			InteropContext.Owner = ActiveValueSession;
			InteropContext.Arguments.reserve(Context.ArgCount);
			for (std::size_t Index = 0; Index < Context.ArgCount; ++Index)
			{
				::vs_interop_value Value;
				Value.BorrowedRead = Context.Args + Index;
				Value.BorrowedWrite = Context.MutableArg(Index);
				Value.NativeContext = &Context;
				Value.BorrowedOwner = InteropContext.Owner;
				if (InteropContext.Owner)
				{
					Value.Owner = InteropContext.Owner->weak_from_this();
				}
				InteropContext.Arguments.push_back(std::move(Value));
			}
			::vs_interop_value Return;
			Return.NativeContext = &Context;
			Return.BorrowedOwner = InteropContext.Owner;
			if (InteropContext.Owner)
			{
				Return.Owner = InteropContext.Owner->weak_from_this();
			}
			struct FTransferReturn
			{
				::vs_interop_value& Value;
				FValue& Destination;
				~FTransferReturn() noexcept
				{
					// The VM owns cleanup on callback failure and exception as well.
					if (Value.Storage)
					{
						Destination = std::move(*Value.Storage);
					}
				}
			} Transfer{Return, OutReturn};
			const vs_interop_callback_result CallbackResult = Thunk->Callback(
			    &InteropContext, &Return, Thunk->UserData);
			if (CallbackResult != VS_INTEROP_CALLBACK_OK)
			{
				OutError = InteropContext.Error.empty()
				    ? "VeroScriptInterop host callback failed"
				    : InteropContext.Error;
				return false;
			}
			if (!ReadValue(&Return, InteropContext.Owner) && Thunk->ReturnType != ENativeType::Void)
			{
				OutError = "VeroScriptInterop native callback did not set its return value";
				return false;
			}
			return true;
		}
		catch (const std::exception& Error)
		{
			try
			{
				OutError = Error.what();
			}
			catch (...)
			{
			}
			return false;
		}
		catch (...)
		{
			try
			{
				OutError = "foreign callback raised an unknown exception";
			}
			catch (...)
			{
			}
			return false;
		}
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_session_call_function(
	    vs_interop_session* session,
	    const vs_interop_string_view function_name,
	    vs_interop_value* const* arguments,
	    const uint64_t argument_count,
	    vs_interop_value* out_return,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(function_name) || function_name.size == 0 || (out_return && !VeroScript::Interop::IsWritableValue(out_return)) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			std::vector<VeroScript::FValue> Values;
			if (const auto Status = VeroScript::Interop::BuildArguments(
			        arguments, argument_count, Values, true, &State);
			    Status != VS_INTEROP_STATUS_OK)
			{
				VeroScript::Interop::SetError(State, "invalid Interop function arguments");
				return Status;
			}
			VeroScript::FValue Return = std::int32_t{0};
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			const VeroScript::Interop::FStagedValueResults Staging(State, Return, &Values);
			const VeroScript::FRunResult Result = State.Session.CallFunction(
			    VeroScript::Interop::CopyString(function_name), Values,
			    out_return ? &Return : nullptr);
			return VeroScript::Interop::FinishCall(State, Result, arguments,
			    argument_count, Values, Return, out_return, out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_call_method(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    const vs_interop_string_view method_name,
	    vs_interop_value* const* arguments,
	    const uint64_t argument_count,
	    vs_interop_value* out_return,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(method_name) || method_name.size == 0 || (out_return && !VeroScript::Interop::IsWritableValue(out_return)) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			if (!VeroScript::Interop::GetObject(object, Object, State))
			{
				VeroScript::Interop::SetError(State, "method receiver is not a script object");
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			std::vector<VeroScript::FValue> Values;
			if (const auto Status = VeroScript::Interop::BuildArguments(arguments,
			        argument_count, Values, true, &State);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			VeroScript::FValue Return = std::int32_t{0};
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			const VeroScript::Interop::FStagedValueResults Staging(State, Return, &Values);
			const VeroScript::FRunResult Result = State.Session.CallMethod(
			    Object, VeroScript::Interop::CopyString(method_name), Values,
			    out_return ? &Return : nullptr);
			return VeroScript::Interop::FinishCall(State, Result, arguments,
			    argument_count, Values, Return, out_return, out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_call_method_bypassing_interceptor(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    const vs_interop_string_view method_name,
	    vs_interop_value* const* arguments,
	    const uint64_t argument_count,
	    vs_interop_value* out_return,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(method_name) || method_name.size == 0 || (out_return && !VeroScript::Interop::IsWritableValue(out_return)) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			if (!VeroScript::Interop::GetObject(object, Object, State))
			{
				VeroScript::Interop::SetError(State, "method receiver is not a script object");
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			std::vector<VeroScript::FValue> Values;
			if (const auto Status = VeroScript::Interop::BuildArguments(arguments,
			        argument_count, Values, true, &State);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			VeroScript::FValue Return = std::int32_t{0};
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			const VeroScript::Interop::FStagedValueResults Staging(State, Return, &Values);
			const VeroScript::FRunResult Result =
			    State.Session.CallMethodBypassingInterceptor(Object,
			        VeroScript::Interop::CopyString(method_name), Values,
			        out_return ? &Return : nullptr);
			return VeroScript::Interop::FinishCall(State, Result, arguments,
			    argument_count, Values, Return, out_return, out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_has_method(
	    const vs_interop_session* session,
	    const vs_interop_value* object,
	    const vs_interop_string_view method_name,
	    const uint32_t argument_count,
	    uint8_t* out_has_method)
	{
		if (!session || !session->State || !out_has_method || argument_count > UINT8_MAX || !VeroScript::Interop::IsValidText(method_name) || method_name.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_has_method = 0;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			if (!VeroScript::Interop::GetObject(object, Object, State))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_has_method = State.Session.HasInstanceMethod(Object,
			                      VeroScript::Interop::CopyString(method_name),
			                      static_cast<std::uint8_t>(argument_count))
			    ? 1u
			    : 0u;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_has_function(
	    const vs_interop_session* session,
	    const vs_interop_string_view function_name,
	    const uint32_t argument_count,
	    uint8_t* out_has_function)
	{
		if (!session || !session->State || !out_has_function || argument_count > UINT8_MAX || !VeroScript::Interop::IsValidText(function_name) || function_name.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_has_function = 0;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			*out_has_function = State.Session.HasFreeFunction(
			                        VeroScript::Interop::CopyString(function_name),
			                        static_cast<std::uint8_t>(argument_count))
			    ? 1u
			    : 0u;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_construct_object(
	    vs_interop_session* session,
	    const vs_interop_string_view type_name,
	    vs_interop_value* const* arguments,
	    const uint64_t argument_count,
	    vs_interop_value* out_object,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !out_object || !VeroScript::Interop::IsWritableValue(out_object) || !VeroScript::Interop::IsValidText(type_name) || type_name.size == 0 || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			std::vector<VeroScript::FValue> Values;
			if (const auto Status = VeroScript::Interop::BuildArguments(arguments,
			        argument_count, Values, false, &State);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			VeroScript::FObjectHandle Object;
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			VeroScript::FValue OwnedObject = std::int32_t{0};
			const VeroScript::Interop::FStagedValueResults Staging(State, OwnedObject, &Values);
			const VeroScript::FRunResult Result = State.Session.ConstructObject(
			    VeroScript::Interop::CopyString(type_name), Values, Object);
			OwnedObject = std::move(Object);
			const auto Status = VeroScript::Interop::StoreRunResult(State, Result, out_result);
			if (Status != VS_INTEROP_STATUS_OK || Result.bExitRequested)
			{
				return Status;
			}
			VeroScript::Interop::AssignValue(*out_object, OwnedObject);
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_field(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    const vs_interop_string_view field_name,
	    vs_interop_value* out_value,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !out_value || !VeroScript::Interop::IsWritableValue(out_value) || !VeroScript::Interop::IsValidText(field_name) || field_name.size == 0 || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			if (!VeroScript::Interop::GetObject(object, Object, State))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			VeroScript::FValue Value = std::int32_t{0};
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			const VeroScript::Interop::FStagedValueResults Staging(State, Value);
			const auto Result = State.Session.GetField(Object,
			    VeroScript::Interop::CopyString(field_name), Value);
			const auto Status = VeroScript::Interop::StoreRunResult(State, Result, out_result);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				VeroScript::Interop::AssignValue(*out_value, Value);
			}
			return Status;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_field(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    const vs_interop_string_view field_name,
	    const vs_interop_value* value,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(field_name) || field_name.size == 0 || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			const auto* Data = VeroScript::Interop::ReadValue(value, &State);
			if (!VeroScript::Interop::GetObject(object, Object, State) || !Data)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.SetField(Object, VeroScript::Interop::CopyString(field_name), *Data),
			    out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_global(
	    vs_interop_session* session,
	    const vs_interop_string_view global_name,
	    vs_interop_value* out_value,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !out_value || !VeroScript::Interop::IsWritableValue(out_value) || !VeroScript::Interop::IsValidText(global_name) || global_name.size == 0 || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FValue Value = std::int32_t{0};
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			const VeroScript::Interop::FStagedValueResults Staging(State, Value);
			const auto Result = State.Session.GetGlobal(
			    VeroScript::Interop::CopyString(global_name), Value);
			const auto Status = VeroScript::Interop::StoreRunResult(State, Result, out_result);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				VeroScript::Interop::AssignValue(*out_value, Value);
			}
			return Status;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_global(
	    vs_interop_session* session,
	    const vs_interop_string_view global_name,
	    const vs_interop_value* value,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(global_name) || global_name.size == 0 || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value, &State);
			if (!Data)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.SetGlobal(VeroScript::Interop::CopyString(global_name), *Data),
			    out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_native(
	    vs_interop_session* session,
	    const vs_interop_string_view name,
	    const vs_interop_native_type_desc* return_type,
	    const vs_interop_native_param_desc* parameters,
	    const uint64_t parameter_count,
	    const vs_interop_native_callback callback,
	    void* user_data)
	{
		if (!session || !session->State || !return_type || !callback || !VeroScript::Interop::IsValidText(name) || name.size == 0 || (parameter_count != 0 && !parameters) || parameter_count > UINT8_MAX)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(
		        State, "RegisterNative");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FNativeTypeDesc ReturnType;
			std::string Error;
			std::size_t TypeNodeCount = 0;
			if (!VeroScript::Interop::ConvertNativeType(
			        *return_type, ReturnType, Error, 0, &TypeNodeCount))
			{
				VeroScript::Interop::SetError(State, std::move(Error));
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			std::vector<VeroScript::FNativeParamDesc> Params;
			Params.reserve(static_cast<std::size_t>(parameter_count));
			for (uint64_t Index = 0; Index < parameter_count; ++Index)
			{
				VeroScript::FNativeParamDesc Param;
				if (!VeroScript::Interop::ConvertNativeParam(
				        parameters[Index], Param, Error, &TypeNodeCount))
				{
					VeroScript::Interop::SetError(State, std::move(Error));
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
				Params.push_back(std::move(Param));
			}
			if (State.NativeThunks.size() == State.NativeThunks.max_size())
			{
				VeroScript::Interop::SetError(State, "native thunk table is full");
				return VS_INTEROP_STATUS_OUT_OF_MEMORY;
			}
			// Reserve before publishing Thunk.get() to the native registry. The
			// ownership commit below is then noexcept and cannot leave a dangling
			// callback user-data pointer after successful registration.
			VeroScript::Interop::ReserveAdditional(State.NativeThunks, 1);
			auto Thunk = std::make_shared<VeroScript::Interop::FNativeThunk>();
			Thunk->Callback = callback;
			Thunk->UserData = user_data;
			Thunk->ReturnType = ReturnType.Type;
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (!State.Session.RegisterNative(VeroScript::Interop::CopyString(name),
			        std::move(ReturnType), std::move(Params),
			        &VeroScript::Interop::NativeThunkCallback, Thunk.get()))
			{
				VeroScript::Interop::SetError(State, "native registration was rejected");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			State.NativeThunks.push_back(std::move(Thunk));
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_retain_callable(
	    vs_interop_session* session,
	    const vs_interop_value* function_value,
	    const uint8_t owner_thread_only,
	    const uint8_t weak_this,
	    vs_interop_callable** out_callable)
	{
		if (!session || !session->State || !out_callable)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_callable = nullptr;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(function_value, &State);
			const auto* Function = Data ? std::get_if<VeroScript::FFunctionHandle>(Data) : nullptr;
			if (!Function || !*Function)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			// Allocate the public wrapper before retaining in the core so an
			// allocation failure cannot orphan an otherwise unreachable entry.
			auto Callable = std::make_unique<vs_interop_callable>();
			const auto Handle = State.Session.RetainCallable(*Function,
			    owner_thread_only != 0, weak_this != 0 ? VeroScript::ERetainedThisCapture::Weak : VeroScript::ERetainedThisCapture::Strong);
			if (!Handle)
			{
				VeroScript::Interop::SetError(State, "callable could not be retained");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			Callable->State = session->State;
			Callable->Handle = Handle;
			Callable->OwnerThread = State.OwnerThread;
			Callable->bOwnerThreadOnly = owner_thread_only != 0;
			*out_callable = Callable.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	void VS_INTEROP_CALL vs_interop_callable_destroy(vs_interop_callable* callable)
	{
		if (!callable)
		{
			return;
		}
		try
		{
			// A foreign finalizer may discard an owner-thread-only wrapper. Do not
			// promote the weak state on that thread: concurrent creator-thread
			// session destruction must not migrate final FSessionState ownership.
			if (callable->bOwnerThreadOnly && std::this_thread::get_id() != callable->OwnerThread)
			{
				delete callable;
				return;
			}
			if (const auto State = callable->State.lock())
			{
				VeroScript::Interop::ReleaseCallableOrDefer(
				    *State, callable->Handle, callable->bOwnerThreadOnly);
			}
			delete callable;
		}
		catch (...)
		{
			try
			{
				delete callable;
			}
			catch (...)
			{
			}
		}
	}

	uint8_t VS_INTEROP_CALL vs_interop_callable_is_valid(const vs_interop_callable* callable)
	{
		if (!callable)
		{
			return 0;
		}
		try
		{
			if (callable->bOwnerThreadOnly && std::this_thread::get_id() != callable->OwnerThread)
			{
				return 0;
			}
			const auto State = callable->State.lock();
			if (!State)
			{
				return 0;
			}
			return State->Session.IsCallableValid(callable->Handle) ? 1u : 0u;
		}
		catch (...)
		{
			return 0;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_callable_invoke(
	    vs_interop_callable* callable,
	    vs_interop_value* const* arguments,
	    const uint64_t argument_count,
	    vs_interop_value* out_return,
	    vs_interop_run_result* out_result)
	{
		if (!callable || (out_return && !VeroScript::Interop::IsWritableValue(out_return)) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		if (callable->bOwnerThreadOnly && std::this_thread::get_id() != callable->OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}
		const auto StateHandle = callable->State.lock();
		if (!StateHandle)
		{
			VeroScript::Interop::InitializeFailedRunResult(out_result);
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		auto& State = *StateHandle;
		if (callable->bOwnerThreadOnly && State.OwnerThread != callable->OwnerThread)
		{
			VeroScript::Interop::InitializeFailedRunResult(out_result);
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		try
		{
			std::vector<VeroScript::FValue> Values;
			if (const auto Status = VeroScript::Interop::BuildArguments(arguments,
			        argument_count, Values, true, &State);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			VeroScript::FValue Return = std::int32_t{0};
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			const VeroScript::Interop::FStagedValueResults Staging(State, Return, &Values);
			const auto Result = State.Session.InvokeCallable(callable->Handle, Values,
			    out_return ? &Return : nullptr);
			return VeroScript::Interop::FinishCall(State, Result, arguments,
			    argument_count, Values, Return, out_return, out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	uint64_t VS_INTEROP_CALL vs_interop_call_context_argument_count(
	    const vs_interop_call_context* context)
	{
		return context ? static_cast<uint64_t>(context->Arguments.size()) : 0;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_call_context_get_argument(
	    const vs_interop_call_context* context,
	    const uint64_t index,
	    const vs_interop_value** out_value)
	{
		if (!context || !out_value || index >= context->Arguments.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_value = &context->Arguments[static_cast<std::size_t>(index)];
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_call_context_get_mutable_argument(
	    vs_interop_call_context* context,
	    const uint64_t index,
	    vs_interop_value** out_value)
	{
		if (!context || !out_value || index >= context->Arguments.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		auto& Value = context->Arguments[static_cast<std::size_t>(index)];
		if (!Value.BorrowedWrite)
		{
			return VS_INTEROP_STATUS_TYPE_MISMATCH;
		}
		*out_value = &Value;
		return VS_INTEROP_STATUS_OK;
	}

	void* VS_INTEROP_CALL vs_interop_call_context_host_this(
	    const vs_interop_call_context* context)
	{
		return context && context->Native ? context->Native->HostThis : nullptr;
	}

	uint64_t VS_INTEROP_CALL vs_interop_call_context_program_generation(
	    const vs_interop_call_context* context)
	{
		try
		{
			return context && context->Native && context->Native->ScriptInvoker
			    ? context->Native->ScriptInvoker->GetProgramGeneration()
			    : 0;
		}
		catch (...)
		{
			return 0;
		}
	}

	void VS_INTEROP_CALL vs_interop_call_context_set_error(
	    vs_interop_call_context* context,
	    const vs_interop_string_view error)
	{
		if (!context || !VeroScript::Interop::IsValidText(error))
		{
			return;
		}
		const std::string_view ErrorView = error.size == 0
		    ? std::string_view{}
		    : std::string_view(error.data, static_cast<std::size_t>(error.size));
		(void)VeroScript::Interop::StoreCallContextError(*context, ErrorView,
		    VS_INTEROP_STATUS_OK);
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_call_context_invoke_function(
	    vs_interop_call_context* context,
	    const vs_interop_string_view function_name,
	    vs_interop_value* const* arguments,
	    const uint64_t argument_count,
	    vs_interop_value* out_return)
	{
		if (!context || !context->Native || !context->Native->ScriptInvoker || !VeroScript::Interop::IsValidText(function_name) || function_name.size == 0 || (out_return && !VeroScript::Interop::IsWritableValue(out_return)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const VeroScript::Interop::FScopedValueSession ValueSession(context->Owner);
			std::vector<VeroScript::FValue> Values;
			if (const auto Status = VeroScript::Interop::BuildArguments(arguments,
			        argument_count, Values, true, context->Owner);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			VeroScript::FValue Return = std::int32_t{0};
			if (!context->Owner) return VS_INTEROP_STATUS_ENGINE_ERROR;
			const VeroScript::Interop::FStagedValueResults Staging(*context->Owner, Return, &Values);
			std::string Error;
			if (!context->Native->ScriptInvoker->InvokeFunction(
			        VeroScript::Interop::CopyString(function_name), Values, Return, Error))
			{
				return VeroScript::Interop::StoreCallContextError(*context, Error,
				    VS_INTEROP_STATUS_ENGINE_ERROR);
			}
			if (const auto Status = VeroScript::Interop::CommitArguments(arguments,
			        argument_count, Values);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			if (out_return)
			{
				VeroScript::Interop::AssignValue(*out_return, Return);
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const VeroScript::Interop::FValueAccessFailure& Failure)
		{
			return VeroScript::Interop::StoreCallContextError(*context,
			    "VeroScriptInterop nested-call value is unavailable on this thread", Failure.Status);
		}
		catch (const std::bad_alloc&)
		{
			return VeroScript::Interop::StoreCallContextError(*context,
			    "VeroScriptInterop nested-call allocation failed",
			    VS_INTEROP_STATUS_OUT_OF_MEMORY);
		}
		catch (const std::exception& Error)
		{
			return VeroScript::Interop::StoreCallContextError(*context, Error.what(),
			    VS_INTEROP_STATUS_INTERNAL_ERROR);
		}
		catch (...)
		{
			return VeroScript::Interop::StoreCallContextError(*context,
			    "unknown nested-call exception", VS_INTEROP_STATUS_INTERNAL_ERROR);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_call_context_invoke_callable(
	    vs_interop_call_context* context,
	    const vs_interop_value* function_value,
	    vs_interop_value* const* arguments,
	    const uint64_t argument_count,
	    vs_interop_value* out_return)
	{
		if (!context || !context->Native || !context->Native->ScriptInvoker || (out_return && !VeroScript::Interop::IsWritableValue(out_return)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const VeroScript::Interop::FScopedValueSession ValueSession(context->Owner);
			const auto* Data = VeroScript::Interop::ReadValue(function_value, context->Owner);
			const auto* Function = Data ? std::get_if<VeroScript::FFunctionHandle>(Data) : nullptr;
			if (!Function || !*Function)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			std::vector<VeroScript::FValue> Values;
			if (const auto Status = VeroScript::Interop::BuildArguments(arguments,
			        argument_count, Values, true, context->Owner);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			VeroScript::FValue Return = std::int32_t{0};
			if (!context->Owner) return VS_INTEROP_STATUS_ENGINE_ERROR;
			const VeroScript::Interop::FStagedValueResults Staging(*context->Owner, Return, &Values);
			std::string Error;
			if (!context->Native->ScriptInvoker->InvokeCallable(*Function, Values, Return, Error))
			{
				return VeroScript::Interop::StoreCallContextError(*context, Error,
				    VS_INTEROP_STATUS_ENGINE_ERROR);
			}
			if (const auto Status = VeroScript::Interop::CommitArguments(arguments,
			        argument_count, Values);
			    Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			if (out_return)
			{
				VeroScript::Interop::AssignValue(*out_return, Return);
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const VeroScript::Interop::FValueAccessFailure& Failure)
		{
			return VeroScript::Interop::StoreCallContextError(*context,
			    "VeroScriptInterop nested-call value is unavailable on this thread", Failure.Status);
		}
		catch (const std::bad_alloc&)
		{
			return VeroScript::Interop::StoreCallContextError(*context,
			    "VeroScriptInterop nested-call allocation failed",
			    VS_INTEROP_STATUS_OUT_OF_MEMORY);
		}
		catch (const std::exception& Error)
		{
			return VeroScript::Interop::StoreCallContextError(*context, Error.what(),
			    VS_INTEROP_STATUS_INTERNAL_ERROR);
		}
		catch (...)
		{
			return VeroScript::Interop::StoreCallContextError(*context,
			    "unknown nested-call exception", VS_INTEROP_STATUS_INTERNAL_ERROR);
		}
	}
}
