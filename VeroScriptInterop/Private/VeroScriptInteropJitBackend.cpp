// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptJitBackend.h"

// The C ABI forwards the public Core capability mask without translation.
static_assert(VS_INTEROP_JIT_CAP_NATIVE_CODE == VeroScript::JitNativeCodeCapability);
static_assert(VS_INTEROP_JIT_CAP_PORTABLE_EXECUTION == VeroScript::JitPortableExecutionCapability);
static_assert(VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS == VeroScript::JitNativeTypedLoopsCapability);
static_assert(VS_INTEROP_JIT_CAP_DIRECT_SCALAR_CALLS == VeroScript::JitDirectScalarCallsCapability);
static_assert(VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION == VeroScript::JitControlledExecutionCapability);
static_assert(VS_INTEROP_VM_CAP_BYTECODE == VeroScript::VmBytecodeCapability);
static_assert(VS_INTEROP_VM_CAP_GOVERNANCE == VeroScript::VmGovernanceCapability);
static_assert(VS_INTEROP_VM_CAP_DEBUG == VeroScript::VmDebugCapability);
static_assert(VS_INTEROP_VM_CAP_HOT_RELOAD == VeroScript::VmHotReloadCapability);
static_assert(VS_INTEROP_VM_CAP_CLEANUP == VeroScript::VmCleanupCapability);

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_vm_backend_info(
	    vs_interop_session* session, vs_interop_vm_backend_info* out_info)
	{
		if (!session || !session->State || !out_info || out_info->struct_size < sizeof(*out_info))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto Owner = session->State;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(*Owner); Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		const auto& Domain = Owner->Session.GetVmExecutionDomain();
		const auto& Factory = Domain->GetFactory();
		vs_interop_vm_backend_info Info{};
		Info.struct_size = sizeof(Info);
		Info.flags = (Factory ? VS_INTEROP_VM_SELECTED : 0) | (Domain->IsFrozen() ? VS_INTEROP_VM_FROZEN : 0);
		Info.library_path = VeroScript::Interop::ViewOf(Domain->GetLibraryPath());
		if (Factory)
		{
			const auto& Description = Factory->GetDescription();
			Info.id = VeroScript::Interop::ViewOf(Description.Id);
			Info.version = VeroScript::Interop::ViewOf(Description.Version);
			Info.target = VeroScript::Interop::ViewOf(Description.Target);
			Info.capabilities = Description.Capabilities;
		}
		else
		{
			Info.id = VeroScript::Interop::ViewOf("none");
		}
		*out_info = Info;
		return VS_INTEROP_STATUS_OK;
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_backend_info(
	    vs_interop_session* session, vs_interop_jit_backend_info* out_info)
	{
		if (!session || !session->State || !out_info || out_info->struct_size < sizeof(*out_info))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto Owner = session->State;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(*Owner);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		const auto& Factory = Owner->Session.GetJitBackendFactory();
		vs_interop_jit_backend_info Info{};
		Info.struct_size = sizeof(Info);
		Info.backend = Factory ? VS_INTEROP_JIT_BACKEND_EXTERNAL : VS_INTEROP_JIT_BACKEND_NONE;
		if (Factory)
		{
			const auto& Description = Factory->GetDescription();
			Info.id = VeroScript::Interop::ViewOf(Description.Id);
			Info.target = VeroScript::Interop::ViewOf(Description.Target);
			Info.native_code = Description.bNativeCode ? 1 : 0;
			Info.capabilities = Description.Capabilities;
		}
		else
		{
			Info.id = VeroScript::Interop::ViewOf("none");
			Info.target = VeroScript::Interop::ViewOf("interpreter");
		}
		*out_info = Info;
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_jit_backend(
	    vs_interop_session* session, const vs_interop_string_view path)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(path))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto Owner = session->State;
		auto& State = *Owner;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "SetJitBackend");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			std::string Error;
			const std::string_view Path(path.data ? path.data : "", static_cast<std::size_t>(path.size));
			if (!State.Session.SetJitBackendPath(Path, Error))
			{
				VeroScript::Interop::SetError(State, Error);
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_backend(
	    vs_interop_session* session, vs_interop_jit_backend* out_backend)
	{
		if (!session || !session->State || !out_backend) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		const auto Owner = session->State;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(*Owner);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		*out_backend = Owner->Session.GetJitBackendFactory()
		    ? VS_INTEROP_JIT_BACKEND_EXTERNAL
		    : VS_INTEROP_JIT_BACKEND_NONE;
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_vm_backend(
	    vs_interop_session* session, const vs_interop_string_view path)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(path))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto Owner = session->State;
		auto& State = *Owner;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "SetVmBackend");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			std::string Error;
			const std::string_view Path(path.data ? path.data : "", static_cast<std::size_t>(path.size));
			if (!State.Session.SetVmBackendPath(Path, Error))
			{
				VeroScript::Interop::SetError(State, Error);
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}
}
