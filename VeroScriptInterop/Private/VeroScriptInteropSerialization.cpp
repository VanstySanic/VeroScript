// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"

#include <new>
#include <utility>

namespace VeroScript::Interop
{
	namespace
	{
		bool ReadScriptObject(const ::vs_interop_value* Value, FObjectHandle& Out, FSessionState& State)
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

		bool InvokeSerialize(
		    FExternalSerializerThunk& Thunk,
		    const FExternalObjectView& Object,
		    std::vector<std::uint8_t>& OutBytes,
		    std::string& OutError) noexcept
		{
			try
			{
				::vs_interop_external_serialize_context Context;
				Context.Object = &Object;
				Context.Output = &OutBytes;
				Context.Error = &OutError;
				Context.Fields.reserve(Object.Fields.size());
				for (const FExternalFieldView& Field : Object.Fields)
				{
					::vs_interop_value Value;
					Value.BorrowedRead = Field.Value;
					Value.BorrowedOwner = ActiveValueSession;
					if (ActiveValueSession)
					{
						Value.Owner = ActiveValueSession->weak_from_this();
					}
					Context.Fields.push_back(std::move(Value));
				}
				const auto Result = Thunk.Serialize(&Context, Thunk.UserData);
				if (Result != VS_INTEROP_CALLBACK_OK || !Context.bOutputSet)
				{
					if (OutError.empty())
					{
						OutError = Result == VS_INTEROP_CALLBACK_OK
						    ? "external serializer did not set its output bytes"
						    : "external serializer callback failed";
					}
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
					OutError = "foreign external serializer raised an exception";
				}
				catch (...)
				{
				}
				return false;
			}
		}

		bool InvokeDeserialize(
		    FExternalSerializerThunk& Thunk,
		    const std::uint8_t* Bytes,
		    const std::size_t ByteCount,
		    const std::uint32_t EncodedVersion,
		    std::vector<FExternalFieldAssignment>& OutFields,
		    std::string& OutError) noexcept
		{
			try
			{
				::vs_interop_external_deserialize_context Context;
				Context.Bytes = Bytes;
				Context.ByteCount = ByteCount;
				Context.EncodedVersion = EncodedVersion;
				Context.Fields = &OutFields;
				Context.Error = &OutError;
				Context.Owner = ActiveValueSession;
				if (Thunk.Deserialize(&Context, Thunk.UserData) != VS_INTEROP_CALLBACK_OK)
				{
					if (OutError.empty())
					{
						OutError = "external deserializer callback failed";
					}
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
					OutError = "foreign external deserializer raised an exception";
				}
				catch (...)
				{
				}
				return false;
			}
		}
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_session_register_external_serializer(
	    vs_interop_session* session,
	    const vs_interop_string_view type_name,
	    const uint32_t version,
	    const vs_interop_external_serialize_callback serialize_callback,
	    const vs_interop_external_deserialize_callback deserialize_callback,
	    void* user_data)
	{
		if (!session || !session->State || version == 0 || !serialize_callback || !deserialize_callback || !VeroScript::Interop::IsValidText(type_name) || type_name.size == 0)
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
		        State, "RegisterExternalSerializer");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			auto Thunk = std::make_unique<VeroScript::Interop::FExternalSerializerThunk>();
			Thunk->Serialize = serialize_callback;
			Thunk->Deserialize = deserialize_callback;
			Thunk->UserData = user_data;
			VeroScript::FExternalSerializerDesc Desc;
			Desc.Version = version;
			auto* Raw = Thunk.get();
			Desc.Serialize = [Raw](const VeroScript::FExternalObjectView& Object,
			                     std::vector<std::uint8_t>& OutBytes, std::string& OutError)
			{
				return VeroScript::Interop::InvokeSerialize(
				    *Raw, Object, OutBytes, OutError);
			};
			Desc.Deserialize = [Raw](const std::uint8_t* Bytes, const std::size_t ByteCount,
			                       const std::uint32_t EncodedVersion,
			                       std::vector<VeroScript::FExternalFieldAssignment>& OutFields,
			                       std::string& OutError)
			{
				return VeroScript::Interop::InvokeDeserialize(
				    *Raw, Bytes, ByteCount, EncodedVersion, OutFields, OutError);
			};
			VeroScript::Interop::ReserveAdditional(State.ExternalSerializerThunks, 1);
			std::string Error;
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (!State.Session.RegisterExternalSerializer(
			        VeroScript::Interop::CopyString(type_name), std::move(Desc), &Error))
			{
				VeroScript::Interop::SetError(State, Error.empty() ? "external serializer registration was rejected" : std::move(Error));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			State.ExternalSerializerThunks.push_back(std::move(Thunk));
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_serialize_object(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    vs_interop_buffer** out_bytes,
	    uint32_t* out_version)
	{
		if (!session || !session->State || !out_bytes || !out_version)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_bytes = nullptr;
		*out_version = 0;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(
		        State, "SerializeObject");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			if (!VeroScript::Interop::ReadScriptObject(object, Object, State))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (!State.Session.SerializeObject(Object, Bytes, *out_version, Error))
			{
				VeroScript::Interop::SetError(State, std::move(Error));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			const auto Status = VeroScript::Interop::MakeBuffer(std::move(Bytes), out_bytes);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				State.LastError.clear();
			}
			return Status;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_deserialize_object(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    const vs_interop_byte_view bytes,
	    const uint32_t encoded_version)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidBytes(bytes))
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
		        State, "DeserializeObject");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			if (!VeroScript::Interop::ReadScriptObject(object, Object, State))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			std::string Error;
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (!State.Session.DeserializeObject(Object, bytes.data,
			        static_cast<std::size_t>(bytes.size), encoded_version, Error))
			{
				VeroScript::Interop::SetError(State, std::move(Error));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_enumerate_object_references(
	    vs_interop_session* session,
	    const vs_interop_value* object,
	    const vs_interop_external_reference_visitor visitor,
	    void* user_data)
	{
		if (!session || !session->State || !visitor)
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
		        State, "EnumerateObjectReferences");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FObjectHandle Object;
			if (!VeroScript::Interop::ReadScriptObject(object, Object, State))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			bool bCallbackFailed = false;
			std::string Error;
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			const bool bEnumerated = State.Session.EnumerateObjectReferences(Object,
			    [&](const VeroScript::FExternalObjectReference& Reference)
			{
				if (bCallbackFailed)
				{
					return;
				}
				try
				{
					VeroScript::FValue Data = Reference.Kind == VeroScript::EExternalReferenceKind::ScriptObject
					    ? VeroScript::FValue{Reference.ScriptObject}
					    : VeroScript::FValue{Reference.HostObject};
					::vs_interop_value Value;
					Value.BorrowedRead = &Data;
					Value.Owner = StateHandle;
					Value.BorrowedOwner = &State;
					const auto Kind = Reference.Kind == VeroScript::EExternalReferenceKind::ScriptObject
					    ? VS_INTEROP_EXTERNAL_SCRIPT_OBJECT
					    : VS_INTEROP_EXTERNAL_HOST_OBJECT;
					bCallbackFailed = visitor(Kind, &Value, user_data) != VS_INTEROP_CALLBACK_OK;
				}
				catch (...)
				{
					bCallbackFailed = true;
				}
			}, Error);
			if (!bEnumerated)
			{
				VeroScript::Interop::SetError(State, std::move(Error));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			if (bCallbackFailed)
			{
				VeroScript::Interop::SetError(State, "external reference visitor failed");
				return VS_INTEROP_STATUS_CALLBACK_ERROR;
			}
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_string_view VS_INTEROP_CALL vs_interop_external_serialize_context_type_name(
	    const vs_interop_external_serialize_context* context)
	{
		return context && context->Object
		    ? VeroScript::Interop::ViewOf(context->Object->TypeName)
		    : vs_interop_string_view{nullptr, 0};
	}

	uint64_t VS_INTEROP_CALL vs_interop_external_serialize_context_mutation_version(
	    const vs_interop_external_serialize_context* context)
	{
		return context && context->Object ? context->Object->MutationVersion : 0;
	}

	uint64_t VS_INTEROP_CALL vs_interop_external_serialize_context_field_count(
	    const vs_interop_external_serialize_context* context)
	{
		return context && context->Object
		    ? static_cast<uint64_t>(context->Object->Fields.size())
		    : 0;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_external_serialize_context_get_field(
	    const vs_interop_external_serialize_context* context,
	    const uint64_t index,
	    vs_interop_string_view* out_name,
	    vs_interop_string_view* out_declared_type,
	    vs_interop_string_view* out_runtime_type,
	    uint32_t* out_field_index,
	    const vs_interop_value** out_value)
	{
		if (!context || !context->Object || index >= context->Object->Fields.size() || (!out_name && !out_declared_type && !out_runtime_type && !out_field_index && !out_value))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto& Field = context->Object->Fields[static_cast<std::size_t>(index)];
		if (out_name)
		{
			*out_name = VeroScript::Interop::ViewOf(Field.Name);
		}
		if (out_declared_type)
		{
			*out_declared_type = VeroScript::Interop::ViewOf(Field.TypeName);
		}
		if (out_runtime_type)
		{
			*out_runtime_type = VeroScript::Interop::ViewOf(Field.RuntimeTypeName);
		}
		if (out_field_index)
		{
			*out_field_index = Field.FieldIndex;
		}
		if (out_value)
		{
			*out_value = &context->Fields[static_cast<std::size_t>(index)];
		}
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_external_serialize_context_set_bytes(
	    vs_interop_external_serialize_context* context,
	    const vs_interop_byte_view bytes)
	{
		if (!context || !context->Output || !VeroScript::Interop::IsValidBytes(bytes))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			std::vector<std::uint8_t> Replacement;
			if (bytes.size != 0)
			{
				Replacement.assign(bytes.data,
				    bytes.data + static_cast<std::size_t>(bytes.size));
			}
			// Commit only after the copy succeeds, preserving any accepted payload
			// if the callback handles a failed replacement and continues.
			context->Output->swap(Replacement);
			context->bOutputSet = true;
			return VS_INTEROP_STATUS_OK;
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

	void VS_INTEROP_CALL vs_interop_external_serialize_context_set_error(
	    vs_interop_external_serialize_context* context,
	    const vs_interop_string_view error)
	{
		if (!context || !context->Error || !VeroScript::Interop::IsValidText(error))
		{
			return;
		}
		try
		{
			*context->Error = VeroScript::Interop::CopyString(error);
		}
		catch (...)
		{
		}
	}

	vs_interop_byte_view VS_INTEROP_CALL vs_interop_external_deserialize_context_bytes(
	    const vs_interop_external_deserialize_context* context)
	{
		return context ? vs_interop_byte_view{
		                     context->Bytes, static_cast<uint64_t>(context->ByteCount)}
		               : vs_interop_byte_view{nullptr, 0};
	}

	uint32_t VS_INTEROP_CALL vs_interop_external_deserialize_context_encoded_version(
	    const vs_interop_external_deserialize_context* context)
	{
		return context ? context->EncodedVersion : 0;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_external_deserialize_context_add_field(
	    vs_interop_external_deserialize_context* context,
	    const vs_interop_string_view field_name,
	    const vs_interop_value* value)
	{
		if (!context || !context->Fields || !VeroScript::Interop::IsValidText(field_name) || field_name.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value, context->Owner);
			if (!Data) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			VeroScript::FExternalFieldAssignment Assignment;
			Assignment.Name = VeroScript::Interop::CopyString(field_name);
			Assignment.Value = *Data;
			context->Fields->push_back(std::move(Assignment));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	void VS_INTEROP_CALL vs_interop_external_deserialize_context_set_error(
	    vs_interop_external_deserialize_context* context,
	    const vs_interop_string_view error)
	{
		if (!context || !context->Error || !VeroScript::Interop::IsValidText(error))
		{
			return;
		}
		try
		{
			*context->Error = VeroScript::Interop::CopyString(error);
		}
		catch (...)
		{
		}
	}
}
