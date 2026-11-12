// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptNumericBindings.h"

using namespace VeroScript;
using namespace VeroScript::Interop;
namespace
{
	vs_interop_status Status(const FNumericError& E)
	{
		switch (E.Status)
		{
		case ENumericStatus::Ok:
		{
			return VS_INTEROP_STATUS_OK;
		}
		case ENumericStatus::TypeMismatch:
		{
			return VS_INTEROP_STATUS_TYPE_MISMATCH;
		}
		case ENumericStatus::OutOfMemory:
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		case ENumericStatus::LimitExceeded:
		{
			return VS_INTEROP_STATUS_LIMIT_EXCEEDED;
		}
		case ENumericStatus::Interrupted:
		{
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		case ENumericStatus::InternalError:
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
		default:
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		}
	}
	template <class F>
	vs_interop_status Invoke(vs_interop_session* S, const vs_interop_call_context* C, F&& Function) noexcept
	{
		std::shared_ptr<FSessionState> Pin;
		try
		{
			if ((S != nullptr) == (C != nullptr))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			FNumericError Error;
			if (C)
			{
				const FScopedValueSession ValueSession(C->Owner);
				if (!C->Native || !C->Native->NumericServices)
				{
					return VS_INTEROP_STATUS_UNSUPPORTED;
				}
				auto Context = C->Native->NumericServices->GetNumericOperationContext();
				Function(Context, Error);
				return Status(Error);
			}
			Pin = S->State;
			if (!Pin)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (auto R = RequireOwnerThread(*Pin); R != VS_INTEROP_STATUS_OK)
			{
				return R;
			}
			if (auto R = RequireIdleSession(*Pin, "numeric operation"); R != VS_INTEROP_STATUS_OK)
			{
				return R == VS_INTEROP_STATUS_ENGINE_ERROR ? VS_INTEROP_STATUS_BUSY : R;
			}
			FScopedSessionActivity Activity(*Pin);
			FNumericOperationContext Context{Pin->Session.GetNumericLimits(), {}};
			Function(Context, Error);
			if (Error.Status != ENumericStatus::Ok)
			{
				SetError(*Pin, Error.MessageId ? Error.MessageId : "numeric operation failed");
			}
			return Status(Error);
		}
		catch (...)
		{
			return TranslateException(Pin.get());
		}
	}
	bool Output(vs_interop_value* V)
	{
		return V && IsWritableValue(V);
	}
	std::string_view Target(uint32_t T)
	{
		switch (T)
		{
		case VS_INTEROP_NATIVE_INT8:
		{
			return "int8";
		}
		case VS_INTEROP_NATIVE_UINT8:
		{
			return "uint8";
		}
		case VS_INTEROP_NATIVE_INT16:
		{
			return "int16";
		}
		case VS_INTEROP_NATIVE_UINT16:
		{
			return "uint16";
		}
		case VS_INTEROP_NATIVE_INT32:
		{
			return "int";
		}
		case VS_INTEROP_NATIVE_UINT32:
		{
			return "unsigned int";
		}
		case VS_INTEROP_NATIVE_INT64:
		{
			return "int64";
		}
		case VS_INTEROP_NATIVE_UINT64:
		{
			return "uint64";
		}
		case VS_INTEROP_NATIVE_FLOAT:
		{
			return "float";
		}
		case VS_INTEROP_NATIVE_DOUBLE:
		{
			return "double";
		}
		default:
		{
			return {};
		}
		}
	}
} // namespace
extern "C"
{
	vs_interop_status VS_INTEROP_CALL
	vs_interop_session_set_numeric_limits(vs_interop_session* S, const vs_interop_numeric_limits* L)
	{
		if (!L || L->struct_size < sizeof(*L) || L->reserved)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const FNumericLimits Limits{L->max_value_bits, L->max_input_bytes, L->max_output_bytes,
									L->max_scratch_bytes, L->max_work_units};
		return Invoke(S, nullptr, [&](FNumericOperationContext&, FNumericError& E)
		{
			S->State->Session.SetNumericLimits(Limits, E);
		});
	}
	uint32_t VS_INTEROP_CALL vs_interop_value_numeric_kind(const vs_interop_value* V)
	{
		try
		{
			const auto* Value = ReadValue(V);
			if (!Value) return 0;
			FBigInteger Integer;
			FRational Rational;
			FNumericError Error;
			return Numerics::FromScriptValue(*Value, Integer, Error) ? 1
				: Numerics::FromScriptValue(*Value, Rational, Error) ? 2 : 0;
		}
		catch (...)
		{
			// Like get_kind, an unavailable/closed/foreign-thread value has no kind.
			return 0;
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_numeric_parse(vs_interop_session* S,
															   const vs_interop_call_context* C,
															   uint32_t Kind, vs_interop_string_view Text,
															   vs_interop_value* Out)
	{
		if (!Output(Out) || !IsValidString(Text) || (Kind != 1 && Kind != 2))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		return Invoke(
			S, C,
			[&](FNumericOperationContext& Context, FNumericError& E)
			{
				const std::string_view Input(Text.data ? Text.data : "", static_cast<std::size_t>(Text.size));
				FValue Result;
				if (Kind == 1)
				{
					FBigInteger V;
					if (!Numerics::TryParse(Input, V, Context, E) || !Numerics::ToScriptValue(V, Result, E))
					{
						return;
					}
				}
				else
				{
					FRational V;
					if (!Numerics::TryParse(Input, V, Context, E) || !Numerics::ToScriptValue(V, Result, E))
					{
						return;
					}
				}
				AssignValue(*Out, std::move(Result));
			});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_numeric_to_text(vs_interop_session* S,
																 const vs_interop_call_context* C,
																 const vs_interop_value* V,
																 vs_interop_value* Out)
	{
		if (!V || !Output(Out))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		return Invoke(S, C,
					  [&](FNumericOperationContext& Context, FNumericError& E)
					  {
						  const auto* Value = ReadValue(V);
						  if (!Value)
						  {
							  E.Status = ENumericStatus::InvalidArgument;
							  return;
						  }
						  std::string Text;
						  if (Numerics::FormatValue(*Value, Text, Context, E))
						  {
							  AssignValue(*Out, std::move(Text));
						  }
					  });
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_numeric_convert(vs_interop_session* S,
																 const vs_interop_call_context* C,
																 const vs_interop_value* V, uint32_t Type,
																 uint32_t Exact, vs_interop_value* Out)
	{
		if (!V || !Output(Out) || Target(Type).empty() || Exact > 1)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		return Invoke(S, C,
					  [&](FNumericOperationContext& Context, FNumericError& E)
					  {
						  const auto* Value = ReadValue(V);
						  if (!Value)
						  {
							  E.Status = ENumericStatus::InvalidArgument;
							  return;
						  }
						  FValue Result;
						  if (Exact && (Type == VS_INTEROP_NATIVE_FLOAT || Type == VS_INTEROP_NATIVE_DOUBLE))
						  {
							  const auto Convert = [&](const auto& N)
							  {
								  if (Type == VS_INTEROP_NATIVE_FLOAT)
								  {
									  float X;
									  if (!Numerics::TryToFloat(N, X, true, Context, E))
									  {
										  return false;
									  }
									  Result = X;
								  }
								  else
								  {
									  double X;
									  if (!Numerics::TryToDouble(N, X, true, Context, E))
									  {
										  return false;
									  }
									  Result = X;
								  }
								  return true;
							  };
							  FBigInteger N;
							  FRational R;
							  if (Numerics::FromScriptValue(*Value, N, E))
							  {
								  if (!Convert(N))
								  {
									  return;
								  }
							  }
							  else if (!Numerics::FromScriptValue(*Value, R, E) || !Convert(R))
							  {
								  return;
							  }
						  }
						  else if (!Numerics::ConvertValue(*Value, Target(Type), Result, Context, E))
						  {
							  return;
						  }
						  AssignValue(*Out, std::move(Result));
					  });
	}
}
