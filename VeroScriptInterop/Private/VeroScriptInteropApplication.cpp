// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"

struct vs_interop_application_services
{
	#if defined(VEROSCRIPT_WITH_APPLICATION)
	VeroScript::Application::FApplicationServices Services;
	#endif
};

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_application_services_create(
	    vs_interop_string_view options, const vs_interop_module_environment* environment,
	    vs_interop_application_services** out_services, vs_interop_buffer** out_error)
	{
		using namespace VeroScript::Interop;
		if (!out_services || !out_error || !IsValidText(options)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		*out_services = nullptr;
		*out_error = nullptr;
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		try
		{
			auto S = std::make_unique<vs_interop_application_services>();
			S->Services = VeroScript::Application::CreateApplicationServices();
			S->Services.Environment = GetModuleEnvironment(environment);
			VeroScript::Application::FDiagnostic E;
			if (!VeroScript::Application::ConfigureApplicationServices(options.size ? CopyString(options) : "{}", S->Services, E))
			{
				const auto Status = MakeTextBuffer(E.Code + ": " + E.Message, out_error);
				return Status == VS_INTEROP_STATUS_OK ? VS_INTEROP_STATUS_ENGINE_ERROR : Status;
			}
			*out_services = S.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
		#else
		(void)environment;
		return VS_INTEROP_STATUS_UNSUPPORTED;
		#endif
	}
	void VS_INTEROP_CALL vs_interop_application_services_destroy(vs_interop_application_services* services)
	{
		try
		{
			delete services;
		}
		catch (...)
		{
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_session_load_application(vs_interop_session* session,
	    vs_interop_string_view path, const vs_interop_application_services* services, vs_interop_run_result* result)
	{
		using namespace VeroScript::Interop;
		if (!session || !session->State || !services || !IsValidText(path) || !path.size || !IsValidRunResultOutput(result)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto S = RequireOwnerThread(State); S != VS_INTEROP_STATUS_OK) return S;
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		try
		{
			const FScopedSessionActivity Activity(State, result);
			auto Settings = services->Services;
			if (!Settings.Environment)
			{
				Settings.Environment = State.Session.GetModuleEnvironment();
			}
			return StoreRunResult(State, State.Session.LoadApplication(CopyString(path), Settings), result);
		}
		catch (...)
		{
			return TranslateException(&State, result);
		}
		#else
		return VS_INTEROP_STATUS_UNSUPPORTED;
		#endif
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_application_read(vs_interop_byte_view bytes,
	    vs_interop_string_view signatures, const vs_interop_application_services* services,
	    vs_interop_package** out_package, vs_interop_buffer** out_error)
	{
		using namespace VeroScript::Interop;
		if (!out_package || !out_error || !services || !IsValidBytes(bytes) || !IsValidText(signatures)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		*out_package = nullptr;
		*out_error = nullptr;
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		try
		{
			if (bytes.size > (1ull << 30) || signatures.size > (4ull << 20)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			VeroScript::FArtifactData Data;
			if (bytes.size)
			{
				Data.Bytes.assign(bytes.data, bytes.data + static_cast<std::size_t>(bytes.size));
			}
			if (signatures.size)
			{
				Data.SignatureRecords = CopyString(signatures);
			}
			VeroScript::Application::FDiagnostic E;
			auto Package = std::make_unique<vs_interop_package>();
			Package->Application = VeroScript::Application::FApplication::Open(std::move(Data), services->Services, E);
			if (Package->Application && (Package->Application->IsSourceEntry() || Package->Application->ReadEntryPackage(Package->Data, E)))
			{
				Package->Data.Kind = VeroScript::EPackageKind::Application;
				if (Package->Application->IsSourceEntry())
				{
					Package->Data.PackageName = Package->Application->GetId();
				}
				*out_package = Package.release();
				return VS_INTEROP_STATUS_OK;
			}
			const auto Status = MakeTextBuffer(E.Code + ": " + E.Message, out_error);
			return Status == VS_INTEROP_STATUS_OK ? VS_INTEROP_STATUS_ENGINE_ERROR : Status;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
		#else
		return VS_INTEROP_STATUS_UNSUPPORTED;
		#endif
	}
}
