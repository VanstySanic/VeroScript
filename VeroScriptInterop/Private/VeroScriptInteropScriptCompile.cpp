// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"

extern "C" vs_interop_status VS_INTEROP_CALL vs_interop_session_set_script_compile_options(
	vs_interop_session* Session, const vs_interop_script_compile_options* Options)
{
	using namespace VeroScript::Interop;
	if (!Session || !Session->State)
	{
		return VS_INTEROP_STATUS_INVALID_ARGUMENT;
	}

	const auto Owner = Session->State;
	auto& State = *Owner;
	if (const auto Status = RequireOwnerThread(State); Status != VS_INTEROP_STATUS_OK)
	{
		return Status;
	}

	if (const auto Status = RequireIdleSession(State, "set script compile options");
		Status != VS_INTEROP_STATUS_OK)
	{
		return Status;
	}

	try
	{
		const FScopedSessionActivity Activity(State);
		VeroScript::FScriptCompileExtensionsOptions Copy;
		if (Options)
		{
			if (Options->struct_size != sizeof(*Options) || Options->allow_source_imports > 1 ||
				Options->reserved[0] || Options->reserved[1] || Options->reserved[2] ||
				Options->root_count > 64 || Options->configuration_count > 4096 ||
				(Options->root_count && !Options->roots) ||
				(Options->configuration_count && !Options->configuration) ||
				Options->allowed_root_count > 4096 ||
				(Options->allowed_root_count && !Options->allowed_roots))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}

			Copy.bAllowSourceImports = Options->allow_source_imports != 0;
			Copy.MaxInstructions = Options->max_instructions;
			Copy.TimeoutMilliseconds = Options->timeout_milliseconds;
			for (uint64_t I = 0; I < Options->root_count; ++I)
			{
				const auto& R = Options->roots[I];
				if (!IsValidText(R.path) || !R.path.size || !IsValidText(R.registration_entry))
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}

				Copy.Roots.push_back({CopyString(R.path), CopyString(R.registration_entry)});
			}
			for (uint64_t I = 0; I < Options->configuration_count; ++I)
			{
				const auto& C = Options->configuration[I];
				if (!IsValidString(C.key) || !C.key.size || !IsValidString(C.value) ||
					!Copy.Configuration.emplace(CopyString(C.key), CopyString(C.value)).second)
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
			}
			for (uint64_t I = 0; I < Options->allowed_root_count; ++I)
			{
				const auto& Root = Options->allowed_roots[I];
				if (!IsValidText(Root) || !Root.size)
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}

				Copy.AllowedRoots.push_back(CopyString(Root));
			}
		}
		State.Session.SetScriptCompileExtensions(std::move(Copy));
		State.LastError.clear();
		return VS_INTEROP_STATUS_OK;
	}
	catch (...)
	{
		return TranslateException(&State);
	}
}
