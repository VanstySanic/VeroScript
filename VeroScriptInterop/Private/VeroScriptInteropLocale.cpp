// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"

#include "VeroScriptLocale.h"

#include <new>
#include <utility>

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_locale_translate(
		const vs_interop_string_view english_message,
		vs_interop_buffer** out_translation)
	{
		if (!out_translation)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_translation = nullptr;
		if (!VeroScript::Interop::IsValidString(english_message))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			return VeroScript::Interop::MakeTextBuffer(
				VeroScript::Locale::Translate(VeroScript::Interop::CopyString(english_message)),
				out_translation);
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

	vs_interop_status VS_INTEROP_CALL vs_interop_locale_set_language(
		const vs_interop_string_view language_tag)
	{
		if (!VeroScript::Interop::IsValidText(language_tag) || language_tag.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			return VeroScript::Locale::TrySetLanguageFromTag(
				VeroScript::Interop::CopyString(language_tag))
				? VS_INTEROP_STATUS_OK
				: VS_INTEROP_STATUS_INVALID_ARGUMENT;
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

	vs_interop_string_view VS_INTEROP_CALL vs_interop_locale_get_language(void)
	{
		try
		{
			thread_local std::string LanguageTag;
			LanguageTag = VeroScript::Locale::GetLanguageTag();
			return {LanguageTag.data(), LanguageTag.size()};
		}
		catch (...)
		{
			return {nullptr, 0};
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_locale_get_available_languages(
		vs_interop_buffer** out_languages_json)
	{
		if (!out_languages_json)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_languages_json = nullptr;
		try
		{
			const auto Languages = VeroScript::Locale::GetAvailableLanguageTags();
			std::string Json = "[";
			for (std::size_t Index = 0; Index < Languages.size(); ++Index)
			{
				if (Index != 0)
				{
					Json.push_back(',');
				}
				Json.push_back('"');
				Json.append(Languages[Index]);
				Json.push_back('"');
			}
			Json.push_back(']');
			return VeroScript::Interop::MakeTextBuffer(
				std::move(Json), out_languages_json);
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

	vs_interop_status VS_INTEROP_CALL vs_interop_locale_load_catalog_directory(
		const vs_interop_string_view directory,
		vs_interop_buffer** out_error)
	{
		if (!out_error || !VeroScript::Interop::IsValidText(directory)
			|| directory.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_error = nullptr;
		try
		{
			std::string Error;
			if (VeroScript::Locale::LoadCatalogDirectory(
					VeroScript::Interop::CopyString(directory), &Error))
			{
				return VS_INTEROP_STATUS_OK;
			}
			if (Error.empty())
			{
				Error = "failed to load localization catalog";
			}
			const auto BufferStatus = VeroScript::Interop::MakeTextBuffer(
				std::move(Error), out_error);
			return BufferStatus == VS_INTEROP_STATUS_OK
				? VS_INTEROP_STATUS_ENGINE_ERROR
				: BufferStatus;
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

	vs_interop_status VS_INTEROP_CALL vs_interop_locale_merge_catalog_directory(
		const vs_interop_string_view directory,
		const uint8_t override_existing,
		vs_interop_buffer** out_error)
	{
		if (!out_error || !VeroScript::Interop::IsValidText(directory)
			|| directory.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_error = nullptr;
		try
		{
			std::string Error;
			if (VeroScript::Locale::MergeCatalogDirectory(
					VeroScript::Interop::CopyString(directory),
					override_existing != 0, &Error))
			{
				return VS_INTEROP_STATUS_OK;
			}
			if (Error.empty())
			{
				Error = "failed to merge localization catalog";
			}
			const auto BufferStatus = VeroScript::Interop::MakeTextBuffer(
				std::move(Error), out_error);
			return BufferStatus == VS_INTEROP_STATUS_OK
				? VS_INTEROP_STATUS_ENGINE_ERROR
				: BufferStatus;
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

	vs_interop_status VS_INTEROP_CALL vs_interop_session_format_diagnostic(
		const vs_interop_session* session,
		const uint64_t diagnostic_index,
		vs_interop_buffer** out_text)
	{
		if (!session || !session->State || !out_text)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_text = nullptr;
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}
		if (diagnostic_index >= State->Diagnostics.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto& Item = State->Diagnostics[
				static_cast<std::size_t>(diagnostic_index)];
			return VeroScript::Interop::MakeTextBuffer(
				VeroScript::Locale::FormatDiagnosticMessage(
					Item.MessageId, Item.MessageArgs, Item.Message),
				out_text);
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
}
