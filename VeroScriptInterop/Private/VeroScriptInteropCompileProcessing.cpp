// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropCompileProcessing.h"
#include "VeroScriptInteropInternal.h"

struct vs_interop_compile_processing_service
{
	VeroScript::FCompileProcessingOptions Options;
};

namespace VeroScript::Interop
{
	vs_interop_status VS_INTEROP_CALL CreateCompileProcessingService(
		const FCompileProcessingOptions& Options, vs_interop_compile_processing_service** OutService)
	{
		if (!OutService) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		*OutService = nullptr;
		if (Options.Registry && !Options.Registry->IsFrozen()) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		try
		{
			auto Service = std::make_unique<vs_interop_compile_processing_service>();
			Service->Options = Options;
			*OutService = Service.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_compile_processing_service_retain(
		const vs_interop_compile_processing_service* Service, vs_interop_compile_processing_service** OutService)
	{
		if (OutService)
		{
			*OutService = nullptr;
		}

		if (!Service) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		return VeroScript::Interop::CreateCompileProcessingService(Service->Options, OutService);
	}

	void VS_INTEROP_CALL vs_interop_compile_processing_service_release(vs_interop_compile_processing_service* Service)
	{
		delete Service;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_compile_processing_service(
		vs_interop_session* Session, const vs_interop_compile_processing_service* Service)
	{
		using namespace VeroScript::Interop;
		if (!Session || !Session->State) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		const auto Owner = Session->State;
		auto& State = *Owner;
		if (const auto Status = RequireOwnerThread(State); Status != VS_INTEROP_STATUS_OK) return Status;
		if (const auto Status = RequireIdleSession(State, "set compile processing service");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}

		try
		{
			const FScopedSessionActivity Activity(State);
			State.Session.SetCompileProcessing(Service ? Service->Options : VeroScript::FCompileProcessingOptions{});
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(&State);
		}
	}
}
