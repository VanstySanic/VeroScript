// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptVariant.h"

using namespace VeroScript;
using namespace VeroScript::Interop;

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_variant_empty(vs_interop_value* Value)
	{
		try
		{
			if (!IsWritableValue(Value)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			AssignValue(*Value, Variant::Empty());
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_variant(vs_interop_session* Session,
	    vs_interop_value* Value, vs_interop_string_view Type, const vs_interop_value* Payload)
	{
		if (!Session || !Session->State || !IsWritableValue(Value) || !Payload || !IsValidText(Type) || !Type.size)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto Pin = Session->State;
		if (const auto Status = RequireOwnerThread(*Pin); Status != VS_INTEROP_STATUS_OK) return Status;
		try
		{
			std::unique_ptr<FScopedSessionActivity> Activity;
			if (Pin->ActiveSessionCalls == 0)
			{
				Activity = std::make_unique<FScopedSessionActivity>(*Pin);
			}
			if (!Pin->Session.ValuesOpen()) return VS_INTEROP_STATUS_ENGINE_ERROR;
			const FValue* Data = ReadValue(Payload, Pin.get());
			if (!Data) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			FValue Prepared;
			std::string Error;
			if (!Pin->Session.MakeVariant(CopyString(Type), *Data, Prepared, Error))
			{
				SetError(*Pin, Error);
				return Error == "variant source does not match its declared concrete type" ? VS_INTEROP_STATUS_TYPE_MISMATCH : VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			if (ValueRequiresOwner(Prepared) && !Pin->Session.ValuesOpen()) return VS_INTEROP_STATUS_ENGINE_ERROR;
			AssignValue(*Value, std::move(Prepared));
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(Pin.get());
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_variant_has_value(const vs_interop_value* Value, uint8_t* Output)
	{
		try
		{
			if (!Value || !Output) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			const auto* Data = ReadValue(Value);
			if (!Data || !Variant::IsVariant(*Data)) return VS_INTEROP_STATUS_TYPE_MISMATCH;
			*Output = Variant::HasValue(*Data) ? 1 : 0;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_variant_get_type_name(const vs_interop_value* Value, vs_interop_string_view* Output)
	{
		try
		{
			if (!Value || !Output) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			const auto* Data = ReadValue(Value);
			if (!Data || !Variant::IsVariant(*Data)) return VS_INTEROP_STATUS_TYPE_MISMATCH;
			*Output = ViewOf(Variant::TypeName(*Data));
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_variant_get(const vs_interop_value* Value, vs_interop_value* Output)
	{
		try
		{
			if (!Value || !IsWritableValue(Output)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			const auto* Data = ReadValue(Value);
			if (!Data || !Variant::IsVariant(*Data)) return VS_INTEROP_STATUS_TYPE_MISMATCH;
			if (!Variant::HasValue(*Data)) return VS_INTEROP_STATUS_TYPE_MISMATCH;
			auto Pin = Value->Owner.lock();
			if (!Pin && ActiveValueSession && ValueRequiresOwner(*Data))
			{
				Pin = ActiveValueSession->shared_from_this();
			}
			std::unique_ptr<FScopedSessionActivity> Activity;
			if (Pin && Pin->ActiveSessionCalls == 0)
			{
				Activity = std::make_unique<FScopedSessionActivity>(*Pin);
			}
			FValue Prepared;
			std::string Error;
			std::optional<FStagedValueResults> Staging;
			if (Pin)
			{
				Staging.emplace(*Pin, Prepared);
			}
			if (!Variant::Snapshot(*Data, Prepared, Error)) return VS_INTEROP_STATUS_ENGINE_ERROR;
			if (Pin && !Pin->Session.ValuesOpen() && ValueRequiresOwner(Prepared)) return VS_INTEROP_STATUS_ENGINE_ERROR;
			// Keep Prepared intact until the reserved cleanup slot has adopted it.
			AssignValue(*Output, Prepared, Value);
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}
}
