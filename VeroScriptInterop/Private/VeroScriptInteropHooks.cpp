// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"

#include <memory>
#include <new>
#include <utility>

namespace VeroScript::Interop
{
	namespace
	{
		bool ReadObject(const ::vs_interop_value* Value, FObjectHandle& Out, FSessionState& State)
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

		EInstanceCallInterceptResult InvokeForeignInterceptor(
		    FSessionState& State,
		    const FInstanceCallInterceptContext& Context,
		    FValue& OutReturn,
		    FScriptError& OutError) noexcept
		{
			if (!State.InstanceInterceptor)
			{
				return EInstanceCallInterceptResult::Unhandled;
			}
			try
			{
				::vs_interop_intercept_context InteropContext;
				InteropContext.Native = &Context;
				InteropContext.Owner = &State;
				InteropContext.ScriptError = &OutError;
				InteropContext.Receiver.BorrowedRead = &Context.Receiver;
				InteropContext.Receiver.Owner = State.weak_from_this();
				InteropContext.Receiver.BorrowedOwner = &State;
				InteropContext.Arguments.reserve(Context.Arguments.size());
				for (const FValue& Argument : Context.Arguments)
				{
					::vs_interop_value Value;
					Value.BorrowedRead = &Argument;
					Value.Owner = State.weak_from_this();
					Value.BorrowedOwner = &State;
					InteropContext.Arguments.push_back(std::move(Value));
				}
				auto& Return = InteropContext.ReturnSlot;
				Return.Owner = State.weak_from_this();
				Return.BorrowedOwner = &State;
				const vs_interop_intercept_result Result = State.InstanceInterceptor(
				    &InteropContext, &Return, State.InstanceInterceptorUserData);
				switch (Result)
				{
				case VS_INTEROP_INTERCEPT_UNHANDLED:
					return EInstanceCallInterceptResult::Unhandled;
				case VS_INTEROP_INTERCEPT_HANDLED:
					{
						if (const FValue* Data = ReadValue(&Return, &State))
						{
							if (Return.Storage && Data == &*Return.Storage)
							{
								OutReturn = std::move(*Return.Storage);
							}
							else
							{
								OutReturn = *Data;
							}
							return EInstanceCallInterceptResult::Handled;
						}
						if (Context.Target.bReturnsVoid)
						{
							return EInstanceCallInterceptResult::Handled;
						}
						OutError.Message = "VeroScriptInterop interceptor did not set its return value";
						return EInstanceCallInterceptResult::Failed;
					}
				case VS_INTEROP_INTERCEPT_FAILED:
					{
						if (OutError.Message.empty())
						{
							OutError.Message = "VeroScriptInterop instance interceptor failed";
						}
						return EInstanceCallInterceptResult::Failed;
					}
				default:
					{
						OutError.Message = "VeroScriptInterop interceptor returned an invalid result";
						return EInstanceCallInterceptResult::Failed;
					}
				}
			}
			catch (const std::exception& Error)
			{
				try
				{
					OutError.Message = Error.what();
				}
				catch (...)
				{
				}
				return EInstanceCallInterceptResult::Failed;
			}
			catch (...)
			{
				try
				{
					OutError.Message = "foreign instance interceptor raised an exception";
				}
				catch (...)
				{
				}
				return EInstanceCallInterceptResult::Failed;
			}
		}

		void InvokeForeignMutationCallback(
		    FSessionState& State,
		    const FMutationEvent& Source) noexcept
		{
			if (!State.MutationCallback)
			{
				return;
			}
			try
			{
				FValue ObjectValue = FObjectHandle{};
				::vs_interop_value Object;
				const ::vs_interop_value* ObjectPointer = nullptr;
				if (Source.Object)
				{
					ObjectValue = Source.Object->shared_from_this();
					Object.BorrowedRead = &ObjectValue;
					Object.Owner = State.weak_from_this();
					Object.BorrowedOwner = &State;
					ObjectPointer = &Object;
				}
				::vs_interop_value Value;
				const ::vs_interop_value* ValuePointer = nullptr;
				if (Source.Value)
				{
					Value.BorrowedRead = Source.Value;
					Value.Owner = State.weak_from_this();
					Value.BorrowedOwner = &State;
					ValuePointer = &Value;
				}
				const vs_interop_mutation_event Event{
				    sizeof(vs_interop_mutation_event),
				    ObjectPointer,
				    ViewOf(Source.ObjectTypeName),
				    ViewOf(Source.FieldName),
				    Source.FieldIndex,
				    static_cast<vs_interop_mutation_kind>(Source.Kind),
				    ValuePointer,
				    Source.ObjectMutationVersion};
				State.MutationCallback(&Event, State.MutationCallbackUserData);
			}
			catch (...)
			{
				// Mutation notifications are observational and must never unwind into VM/JIT.
			}
		}
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_instance_call_interceptor(
	    vs_interop_session* session,
	    const vs_interop_instance_call_interceptor interceptor,
	    void* user_data)
	{
		if (!session || !session->State)
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
		        State, "SetInstanceCallInterceptor");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (interceptor)
			{
				State.Session.SetInstanceCallInterceptor(
				    [&State](const VeroScript::FInstanceCallInterceptContext& Context,
				        VeroScript::FValue& OutReturn, VeroScript::FScriptError& OutError)
				{
					return VeroScript::Interop::InvokeForeignInterceptor(
					    State, Context, OutReturn, OutError);
				});
			}
			else
			{
				State.Session.SetInstanceCallInterceptor({});
			}
			// Publish foreign callback state only after the underlying session accepts
			// the hook, so an allocation failure cannot leave the two layers divergent.
			State.InstanceInterceptor = interceptor;
			State.InstanceInterceptorUserData = interceptor ? user_data : nullptr;
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_mutation_callback(
	    vs_interop_session* session,
	    const vs_interop_mutation_callback callback,
	    void* user_data)
	{
		if (!session || !session->State)
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
		        State, "SetMutationCallback");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (callback)
			{
				State.Session.SetMutationCallback(
				    [&State](const VeroScript::FMutationEvent& Event)
				{
					VeroScript::Interop::InvokeForeignMutationCallback(State, Event);
				});
			}
			else
			{
				State.Session.SetMutationCallback({});
			}
			State.MutationCallback = callback;
			State.MutationCallbackUserData = callback ? user_data : nullptr;
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_apply_class_defaults(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidRunResultOutput(out_result))
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
			if (!VeroScript::Interop::ReadObject(object, Object, State))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.ApplyClassDefaults(Object), out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	const vs_interop_value* VS_INTEROP_CALL vs_interop_intercept_context_receiver(
	    const vs_interop_intercept_context* context)
	{
		return context && context->Native ? &context->Receiver : nullptr;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_intercept_context_get_target(
	    const vs_interop_intercept_context* context,
	    vs_interop_resolved_method* out_target)
	{
		if (!context || !context->Native || !out_target || (out_target->struct_size != 0 && out_target->struct_size < sizeof(vs_interop_resolved_method)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto& Source = context->Native->Target;
		*out_target = {};
		out_target->struct_size = sizeof(vs_interop_resolved_method);
		out_target->program_generation = Source.ProgramGeneration;
		out_target->module_identity = Source.ModuleIdentity;
		out_target->declaring_type_identity = Source.DeclaringTypeIdentity;
		out_target->method_identity = Source.MethodIdentity;
		out_target->function_index = Source.FunctionIndex;
		out_target->module_name = VeroScript::Interop::ViewOf(Source.ModuleName);
		out_target->declaring_type_name = VeroScript::Interop::ViewOf(Source.DeclaringTypeName);
		out_target->method_name = VeroScript::Interop::ViewOf(Source.MethodName);
		out_target->qualified_name = VeroScript::Interop::ViewOf(Source.QualifiedName);
		out_target->is_virtual = Source.bVirtual ? 1u : 0u;
		out_target->is_const = Source.bConst ? 1u : 0u;
		out_target->returns_void = Source.bReturnsVoid ? 1u : 0u;
		return VS_INTEROP_STATUS_OK;
	}

	uint64_t VS_INTEROP_CALL vs_interop_intercept_context_argument_count(
	    const vs_interop_intercept_context* context)
	{
		return context && context->Native
		    ? static_cast<uint64_t>(context->Arguments.size())
		    : 0;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_intercept_context_get_argument(
	    const vs_interop_intercept_context* context,
	    const uint64_t index,
	    const vs_interop_value** out_value)
	{
		if (!context || !context->Native || !out_value || index >= context->Arguments.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_value = &context->Arguments[static_cast<std::size_t>(index)];
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_intercept_context_proceed(
	    vs_interop_intercept_context* context,
	    vs_interop_value* out_return)
	{
		if (!context || !context->Native || !context->ScriptError || !VeroScript::Interop::IsWritableValue(out_return))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const VeroScript::Interop::FScopedValueSession ValueSession(context->Owner);
			if (out_return->Root.IsRegistered())
			{
				(void)VeroScript::Interop::ReadValue(out_return);
			}
			VeroScript::FValue Return = std::int32_t{0};
			if (!context->Native->Proceed(Return, *context->ScriptError))
			{
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			// Core retains the original script-owned result until interception settles.
			// Move a prepared array through this callback's private slot so forwarding
			// preserves its copy boundary. Other slots still use normal publication.
			if (out_return == &context->ReturnSlot && std::holds_alternative<VeroScript::FNativeArray>(Return))
			{
				if (const auto Owner = out_return->Owner.lock();
				    Owner && VeroScript::Interop::RequireOwnerThread(*Owner) == VS_INTEROP_STATUS_OK)
				{
					VeroScript::Interop::BindValueOwnership(Return, *Owner);
					VeroScript::Interop::ResetValue(*out_return);
					out_return->Storage.emplace(std::move(Return));
					return VS_INTEROP_STATUS_OK;
				}
			}
			VeroScript::Interop::AssignValue(*out_return, std::move(Return));
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	void VS_INTEROP_CALL vs_interop_intercept_context_set_error(
	    vs_interop_intercept_context* context,
	    const vs_interop_string_view error)
	{
		if (!context || !context->ScriptError || !VeroScript::Interop::IsValidText(error))
		{
			return;
		}
		try
		{
			context->ScriptError->Message = VeroScript::Interop::CopyString(error);
		}
		catch (...)
		{
		}
	}
}
