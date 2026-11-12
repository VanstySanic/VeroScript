// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropAnnotation.h"
#include "VeroScriptInteropInternal.h"

static_assert(VS_INTEROP_DIAGNOSTIC_SEVERITY_ERROR == static_cast<uint32_t>(VeroScript::ECompileDiagnosticSeverity::Error));
static_assert(VS_INTEROP_DIAGNOSTIC_SEVERITY_WARNING == static_cast<uint32_t>(VeroScript::ECompileDiagnosticSeverity::Warning));
static_assert(VS_INTEROP_DIAGNOSTIC_SEVERITY_INFORMATION == static_cast<uint32_t>(VeroScript::ECompileDiagnosticSeverity::Information));
static_assert(VS_INTEROP_DIAGNOSTIC_SEVERITY_HINT == static_cast<uint32_t>(VeroScript::ECompileDiagnosticSeverity::Hint));

struct vs_interop_annotation_service
{
	VeroScript::FAnnotationProcessingOptions Options;
};
namespace VeroScript::Interop
{
vs_interop_status VS_INTEROP_CALL CreateAnnotationProcessingService(const FAnnotationProcessingOptions &Options,
																	vs_interop_annotation_service **Out)
{
	if (!Out)
	{
		return VS_INTEROP_STATUS_INVALID_ARGUMENT;
	}

	*Out = nullptr;
	if (Options.Registry && !Options.Registry->IsFrozen())
	{
		return VS_INTEROP_STATUS_INVALID_ARGUMENT;
	}

	try
	{
		auto S = std::make_unique<vs_interop_annotation_service>();
		S->Options = Options;
		*Out = S.release();
		return VS_INTEROP_STATUS_OK;
	}
	catch (...)
	{
		return TranslateException(nullptr);
	}
}
} // namespace VeroScript::Interop
extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_annotation_service_retain(const vs_interop_annotation_service *Service,
																		   vs_interop_annotation_service **Out)
	{
		if (Out)
		{
			*Out = nullptr;
		}

		if (!Service)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}

		return VeroScript::Interop::CreateAnnotationProcessingService(Service->Options, Out);
	}
	void VS_INTEROP_CALL vs_interop_annotation_service_release(vs_interop_annotation_service *Service)
	{
		delete Service;
	}
	vs_interop_status VS_INTEROP_CALL
	vs_interop_session_set_annotation_service(vs_interop_session *Session, const vs_interop_annotation_service *Service)
	{
		if (!Session || !Session->State)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}

		const auto Owner = Session->State;
		auto &State = *Owner;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}

		const auto Status = VeroScript::Interop::RequireIdleSession(State, "set annotation service");
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}

		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.SetAnnotationProcessing(Service ? Service->Options
														  : VeroScript::FAnnotationProcessingOptions{});
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_diagnostic_severity(const vs_interop_session *Session,
																				 uint64_t Index, uint32_t *Out)
	{
		if (!Session || !Session->State || !Out)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}

		const auto& State = *Session->State;
		if (std::this_thread::get_id() != State.OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}

		if (Index >= State.Diagnostics.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}

		*Out = static_cast<uint32_t>(State.Diagnostics[static_cast<std::size_t>(Index)].Severity);
		return VS_INTEROP_STATUS_OK;
	}
	uint64_t VS_INTEROP_CALL vs_interop_get_compile_capabilities()
	{
		return VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING | VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS
			| VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING;
	}
}
