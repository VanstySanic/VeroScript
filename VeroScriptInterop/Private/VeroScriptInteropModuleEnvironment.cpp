// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropModuleEnvironment.h"
#include "VeroScriptInteropInternal.h"

struct vs_interop_module_environment
{
	std::shared_ptr<const VeroScript::FModuleEnvironment> Environment;
};

namespace VeroScript::Interop
{
	vs_interop_status VS_INTEROP_CALL CreateModuleEnvironment(
		const std::shared_ptr<const FModuleEnvironment>& Environment,
		vs_interop_module_environment** OutEnvironment)
	{
		if (!OutEnvironment) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		*OutEnvironment = nullptr;
		if (!Environment) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		try
		{
			auto Handle = std::make_unique<vs_interop_module_environment>();
			Handle->Environment = Environment;
			*OutEnvironment = Handle.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}

	std::shared_ptr<const FModuleEnvironment> GetModuleEnvironment(const vs_interop_module_environment* Environment)
	{
		return Environment ? Environment->Environment : nullptr;
	}

	vs_interop_status ValidateModuleEnvironment(FSessionState& State,
		const std::shared_ptr<const FModuleEnvironment>& Environment)
	{
		const auto Previous = State.Session.GetModuleEnvironment();
		if (Previous != Environment && State.Session.GetLoadedModule())
		{
			SetError(State, "clear the loaded program before changing its module environment");
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		const auto* Catalog = Environment ? Environment->GetDescription().Bindings.get() : nullptr;
		if (Catalog && !(Previous && Previous->GetDescription().Bindings))
		{
			const auto& Local = State.Session.GetBindingCatalog();
			if (Catalog != &Local && (!Local.GetNativeFunctions().empty() || !Local.GetFunctions().empty()
				|| !Local.GetTypes().empty() || !Local.GetTypeAliases().empty()
				|| !Local.GetGenericTypeTemplates().empty() || !Local.GetAnnotations().empty()))
			{
				SetError(State, "module environment conflicts with existing session bindings; prepare a combined catalog");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
		}
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status RequireMutableBindings(FSessionState& State)
	{
		const auto Environment = State.Session.GetModuleEnvironment();
		if (Environment && Environment->GetDescription().Bindings)
		{
			SetError(State, "cannot mutate an environment-owned binding catalog; prepare a combined snapshot");
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		return VS_INTEROP_STATUS_OK;
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_module_environment_retain(
		const vs_interop_module_environment* Environment, vs_interop_module_environment** OutEnvironment)
	{
		if (OutEnvironment)
		{
			*OutEnvironment = nullptr;
		}

		if (!Environment) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		return VeroScript::Interop::CreateModuleEnvironment(Environment->Environment, OutEnvironment);
	}

	void VS_INTEROP_CALL vs_interop_module_environment_release(vs_interop_module_environment* Environment)
	{
		delete Environment;
	}
}
