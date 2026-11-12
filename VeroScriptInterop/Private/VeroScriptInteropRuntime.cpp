// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptLocale.h"
#include "VeroScriptJitBackend.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <utility>

namespace VeroScript::Interop
{
	vs_interop_status MakeBuffer(
	    std::vector<std::uint8_t> Bytes,
	    ::vs_interop_buffer** const OutBuffer)
	{
		if (!OutBuffer)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*OutBuffer = nullptr;
		try
		{
			auto Buffer = std::make_unique<::vs_interop_buffer>();
			Buffer->Bytes = std::move(Bytes);
			*OutBuffer = Buffer.release();
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

	vs_interop_status MakeTextBuffer(
	    std::string Text,
	    ::vs_interop_buffer** const OutBuffer)
	{
		try
		{
			return MakeBuffer(
			    std::vector<std::uint8_t>(Text.begin(), Text.end()), OutBuffer);
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

	namespace
	{
		bool ConvertPackageKind(
		    const vs_interop_package_kind Kind,
		    EPackageKind& Out)
		{
			switch (Kind)
			{
			case VS_INTEROP_PACKAGE_APPLICATION:
				{
					Out = EPackageKind::Application;
					return true;
				}
			case VS_INTEROP_PACKAGE_LIBRARY:
				{
					Out = EPackageKind::Library;
					return true;
				}
			default:
				return false;
			}
		}

		bool ConvertPackageOptions(
		    const vs_interop_package_build_options& Source,
		    FPackageBuildOptions& Out,
		    std::string& OutError)
		{
			if (Source.struct_size < sizeof(vs_interop_package_build_options) || !IsValidText(Source.package_name) || Source.package_name.size == 0 || (Source.host_metadata_count != 0 && !Source.host_metadata) || Source.host_metadata_count > 4096)
			{
				OutError = "invalid package build options";
				return false;
			}
			if (!ConvertPackageKind(Source.kind, Out.Kind))
			{
				OutError = "invalid package kind";
				return false;
			}
			Out.PackageName = CopyString(Source.package_name);
			Out.PackageVersion = Source.package_version;
			Out.bStripDebugInfo = Source.strip_debug_info != 0;
			for (std::uint64_t Index = 0; Index < Source.host_metadata_count; ++Index)
			{
				const auto& Entry = Source.host_metadata[Index];
				if (!IsValidText(Entry.key) || Entry.key.size == 0 || !IsValidBytes(Entry.value))
				{
					OutError = "invalid package host metadata";
					return false;
				}
				std::vector<std::uint8_t> Value;
				if (Entry.value.size != 0)
				{
					Value.assign(Entry.value.data,
					    Entry.value.data + static_cast<std::size_t>(Entry.value.size));
				}
				if (!Out.HostMetadata.emplace(CopyString(Entry.key), std::move(Value)).second)
				{
					OutError = "duplicate package host metadata key";
					return false;
				}
			}
			return true;
		}

		FRunResult MakePackageFailure(const std::string& Message)
		{
			VeroScript::Locale::InitializeModuleFromEnvironment("VeroScriptInterop");
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error.MessageId = "diag.failed_to_build_package";
			Result.Error.MessageArgs = {Message};
			Result.Error.Message = VeroScript::Locale::FormatEnglish(
			    "diag.failed_to_build_package",
			    {Message});
			Result.Error.Location.FileLabel = "<interop-package>";
			Result.Diagnostics = {Result.Error};
			return Result;
		}
	}
}

extern "C"
{
	uint64_t VS_INTEROP_CALL vs_interop_get_capabilities(void)
	{
		uint64_t Capabilities = VS_INTEROP_CAP_PACKAGES | VS_INTEROP_CAP_HOT_RELOAD |
		    VS_INTEROP_CAP_HOST_BINDINGS | VS_INTEROP_CAP_BINDING_JSON |
		    VS_INTEROP_CAP_REFLECTION_JSON | VS_INTEROP_CAP_INSTANCE_INTERCEPT |
		    VS_INTEROP_CAP_MUTATION_CALLBACK | VS_INTEROP_CAP_CONTAINER_VALUES |
		    VS_INTEROP_CAP_RETAINED_CALLABLES | VS_INTEROP_CAP_GENERIC_HOST_TYPES |
		    VS_INTEROP_CAP_EXTERNAL_SERIALIZATION | VS_INTEROP_CAP_JIT_DIAGNOSTICS |
		    VS_INTEROP_CAP_LOCALIZATION | VS_INTEROP_CAP_DISTINCT_NUMERIC_PRIMITIVES |
		    VS_INTEROP_CAP_CONTAINER_ENUMERATION | VS_INTEROP_CAP_EXECUTION_GOVERNANCE |
		    VS_INTEROP_CAP_SESSION_JIT_CACHE | VS_INTEROP_CAP_JIT_BACKEND_SELECTION |
		    VS_INTEROP_CAP_EVAL_ONCE | VS_INTEROP_CAP_MEMORY |
		    VS_INTEROP_CAP_MODULE_ENVIRONMENT | VS_INTEROP_CAP_NUMERICS | VS_INTEROP_CAP_VARIANT |
		    VS_INTEROP_CAP_VM_BACKEND_SELECTION | VS_INTEROP_CAP_RELOAD_STATUS;

		return Capabilities;
	}

	void VS_INTEROP_CALL vs_interop_buffer_destroy(vs_interop_buffer* buffer)
	{
		try
		{
			delete buffer;
		}
		catch (...)
		{
		}
	}

	vs_interop_byte_view VS_INTEROP_CALL vs_interop_buffer_view(
	    const vs_interop_buffer* buffer)
	{
		if (!buffer)
		{
			return {nullptr, 0};
		}
		return {buffer->Bytes.data(), static_cast<uint64_t>(buffer->Bytes.size())};
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_buffer_copy(
	    const vs_interop_buffer* buffer,
	    uint8_t* destination,
	    const uint64_t destination_size,
	    uint64_t* required_size)
	{
		if (!buffer || !required_size)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*required_size = static_cast<uint64_t>(buffer->Bytes.size());
		if (destination_size < *required_size || (!destination && *required_size != 0))
		{
			return VS_INTEROP_STATUS_BUFFER_TOO_SMALL;
		}
		if (*required_size != 0)
		{
			std::memcpy(destination, buffer->Bytes.data(), buffer->Bytes.size());
		}
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_input(
	    vs_interop_session* session,
	    const vs_interop_string_view utf8_input)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidString(utf8_input))
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
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "SetInput");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.SetInputFromString(VeroScript::Interop::CopyString(utf8_input));
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_hot_reload_enabled(
	    vs_interop_session* session,
	    const uint8_t enabled)
	{
		if (!session || !session->State)
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
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "SetHotReloadEnabled");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.SetHotReloadEnabled(enabled != 0);
			if (State.Session.IsHotReloadEnabled() != (enabled != 0))
			{
				VeroScript::Interop::SetError(State,
				    State.Session.RequiresReset()
				        ? "Hot reload rollback did not finish. Destroy and recreate the session."
				        : "hot reload configuration was rejected");
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

	uint8_t VS_INTEROP_CALL vs_interop_session_is_hot_reload_enabled(
	    const vs_interop_session* session)
	{
		if (!session || !session->State)
		{
			return 0;
		}
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return 0;
		}
		try
		{
			return State->Session.IsHotReloadEnabled() ? 1u : 0u;
		}
		catch (...)
		{
			return 0;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_reload(
	    vs_interop_session* session,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidRunResultOutput(out_result))
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
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			return VeroScript::Interop::StoreRunResult(
			    State, State.Session.Reload(), out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_reload_status(
	    const vs_interop_session* session,
	    vs_interop_reload_status* out_status)
	{
		if (!session || !session->State || !out_status || out_status->struct_size < sizeof(*out_status))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto Owner = session->State;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(*Owner); Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		using VeroScript::EReloadState;
		static_assert(VS_INTEROP_RELOAD_NONE == static_cast<vs_interop_reload_state>(EReloadState::None));
		static_assert(VS_INTEROP_RELOAD_REJECTED == static_cast<vs_interop_reload_state>(EReloadState::Rejected));
		static_assert(VS_INTEROP_RELOAD_PENDING == static_cast<vs_interop_reload_state>(EReloadState::Pending));
		static_assert(VS_INTEROP_RELOAD_ROLLED_BACK == static_cast<vs_interop_reload_state>(EReloadState::RolledBack));
		static_assert(VS_INTEROP_RELOAD_APPLIED == static_cast<vs_interop_reload_state>(EReloadState::Applied));
		static_assert(VS_INTEROP_RELOAD_APPLIED_WITH_ERRORS == static_cast<vs_interop_reload_state>(EReloadState::AppliedWithErrors));
		static_assert(VS_INTEROP_RELOAD_RESET_REQUIRED == static_cast<vs_interop_reload_state>(EReloadState::ResetRequired));

		const auto Status = Owner->Session.GetReloadStatus();
		*out_status = {sizeof(*out_status), static_cast<vs_interop_reload_state>(Status.State),
		    Status.OperationId, Status.PreviousGeneration, Status.CandidateGeneration,
		    Status.CurrentGeneration, Status.CommittedGeneration};
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_jit_policy(
	    vs_interop_session* session,
	    const vs_interop_jit_policy* policy)
	{
		constexpr std::size_t LegacyPolicySize =
		    offsetof(vs_interop_jit_policy, reuse_session_code);
		if (!session || !session->State || !policy || policy->struct_size < LegacyPolicySize || policy->max_compile_attempts > std::numeric_limits<std::size_t>::max() || policy->max_cached_entries > std::numeric_limits<std::size_t>::max() || policy->eager_compile_budget > std::numeric_limits<std::size_t>::max())
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
		        State, "SetJitPolicy");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FJitPolicy Policy;
			Policy.MixedHotCallThreshold = policy->mixed_hot_call_threshold;
			Policy.MaxCompileAttempts = static_cast<std::size_t>(policy->max_compile_attempts);
			Policy.MaxCachedEntries = static_cast<std::size_t>(policy->max_cached_entries);
			Policy.EagerCompileBudget = static_cast<std::size_t>(policy->eager_compile_budget);
			if (policy->struct_size >= offsetof(vs_interop_jit_policy, reuse_session_code) + sizeof(policy->reuse_session_code))
			{
				Policy.bReuseSessionCode = policy->reuse_session_code != 0;
			}
			if (policy->struct_size >= offsetof(vs_interop_jit_policy, enable_controlled_mixed) + sizeof(policy->enable_controlled_mixed))
			{
				Policy.bEnableControlledMixed = policy->enable_controlled_mixed != 0;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.SetJitPolicy(Policy);
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_jit_cache_limits(
	    vs_interop_session* session,
	    const vs_interop_jit_cache_limits* limits)
	{
		if (!session || !session->State || !limits || limits->struct_size < sizeof(vs_interop_jit_cache_limits) || limits->max_native_bytes > std::numeric_limits<std::size_t>::max() || limits->max_retained_payload_bytes > std::numeric_limits<std::size_t>::max())
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
		        State, "SetJitCacheLimits");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FSessionJitCacheLimits Limits;
			Limits.MaxNativeBytes = static_cast<std::size_t>(limits->max_native_bytes);
			Limits.MaxRetainedPayloadBytes = static_cast<std::size_t>(
			    limits->max_retained_payload_bytes);
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.SetJitCacheLimits(Limits);
			State.LastError.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_cache_limits(
	    const vs_interop_session* session,
	    vs_interop_jit_cache_limits* out_limits)
	{
		if (!session || !session->State || !out_limits || (out_limits->struct_size != 0 && out_limits->struct_size < sizeof(vs_interop_jit_cache_limits)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const std::size_t WritableSize = out_limits->struct_size == 0
		    ? sizeof(vs_interop_jit_cache_limits)
		    : static_cast<std::size_t>(out_limits->struct_size);
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const auto Source = State.Session.GetJitCacheLimits();
			vs_interop_jit_cache_limits Limits{};
			Limits.struct_size = sizeof(Limits);
			Limits.max_native_bytes = static_cast<std::uint64_t>(Source.MaxNativeBytes);
			Limits.max_retained_payload_bytes = static_cast<std::uint64_t>(
			    Source.MaxRetainedPayloadBytes);
			std::memcpy(out_limits, &Limits, std::min(WritableSize, sizeof(Limits)));
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_cache_stats(
	    const vs_interop_session* session,
	    vs_interop_jit_cache_stats* out_stats)
	{
		if (!session || !session->State || !out_stats || (out_stats->struct_size != 0 && out_stats->struct_size < sizeof(vs_interop_jit_cache_stats)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const std::size_t WritableSize = out_stats->struct_size == 0
		    ? sizeof(vs_interop_jit_cache_stats)
		    : static_cast<std::size_t>(out_stats->struct_size);
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const auto Source = State.Session.GetJitCacheStats();
			vs_interop_jit_cache_stats Stats{};
			Stats.struct_size = sizeof(Stats);
			Stats.compile_attempts = static_cast<std::uint64_t>(Source.CompileAttempts);
			Stats.retained_payload_bytes = static_cast<std::uint64_t>(
			    Source.RetainedPayloadBytes);
			Stats.native_bytes = static_cast<std::uint64_t>(Source.NativeBytes);
			Stats.resource_limit_reached = Source.bResourceLimitReached ? 1 : 0;
			std::memcpy(out_stats, &Stats, std::min(WritableSize, sizeof(Stats)));
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_last_jit_stats(
	    const vs_interop_session* session,
	    vs_interop_jit_stats* out_stats)
	{
		constexpr std::size_t LegacyStatsSize =
		    offsetof(vs_interop_jit_stats, scalar_method_fast_path_mixed);
		if (!session || !session->State || !out_stats || (out_stats->struct_size != 0 && out_stats->struct_size < LegacyStatsSize))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const std::size_t WritableSize = out_stats->struct_size == 0
		    ? LegacyStatsSize
		    : static_cast<std::size_t>(out_stats->struct_size);
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const auto& Source = State.Session.GetLastJitStats();
			vs_interop_jit_stats Stats{};
			Stats.struct_size = sizeof(vs_interop_jit_stats);
			#define VS_COPY_JIT_STAT(c_name, cpp_name) \
	Stats.c_name = static_cast<uint64_t>(Source.cpp_name)
			VS_COPY_JIT_STAT(compile_attempts, CompileAttempts);
			VS_COPY_JIT_STAT(cache_hits, CacheHits);
			VS_COPY_JIT_STAT(compiled_entries, CompiledEntries);
			VS_COPY_JIT_STAT(native_leaf_entries, NativeLeafEntries);
			VS_COPY_JIT_STAT(native_cfg_entries, NativeCfgEntries);
			VS_COPY_JIT_STAT(bridge_trampoline_entries, BridgeTrampolineEntries);
			VS_COPY_JIT_STAT(linear_trampoline_entries, LinearTrampolineEntries);
			VS_COPY_JIT_STAT(vm_trampoline_entries, VmTrampolineEntries);
			VS_COPY_JIT_STAT(unsupported_entries, UnsupportedEntries);
			VS_COPY_JIT_STAT(dispatch_attempts, DispatchAttempts);
			VS_COPY_JIT_STAT(dispatches, Dispatches);
			VS_COPY_JIT_STAT(fallbacks, Fallbacks);
			VS_COPY_JIT_STAT(failures, Failures);
			VS_COPY_JIT_STAT(cache_invalidations, CacheInvalidations);
			VS_COPY_JIT_STAT(hotness_deferrals, HotnessDeferrals);
			VS_COPY_JIT_STAT(hot_functions, HotFunctions);
			VS_COPY_JIT_STAT(compile_budget_skips, CompileBudgetSkips);
			VS_COPY_JIT_STAT(cache_capacity_skips, CacheCapacitySkips);
			VS_COPY_JIT_STAT(eager_compile_skips, EagerCompileSkips);
			VS_COPY_JIT_STAT(array_fast_path_pushes, ArrayFastPathPushes);
			VS_COPY_JIT_STAT(array_fast_path_gets, ArrayFastPathGets);
			VS_COPY_JIT_STAT(array_fast_path_sets, ArrayFastPathSets);
			VS_COPY_JIT_STAT(scalar_map_fast_path_gets, ScalarMapFastPathGets);
			VS_COPY_JIT_STAT(scalar_map_fast_path_sets, ScalarMapFastPathSets);
			VS_COPY_JIT_STAT(scalar_method_fast_path_i32, ScalarMethodFastPathI32);
			VS_COPY_JIT_STAT(scalar_method_fast_path_u32, ScalarMethodFastPathU32);
			VS_COPY_JIT_STAT(scalar_method_fast_path_f64, ScalarMethodFastPathF64);
			VS_COPY_JIT_STAT(scalar_function_fast_path_i32, ScalarFunctionFastPathI32);
			VS_COPY_JIT_STAT(scalar_function_fast_path_u32, ScalarFunctionFastPathU32);
			VS_COPY_JIT_STAT(scalar_function_fast_path_f64, ScalarFunctionFastPathF64);
			VS_COPY_JIT_STAT(scalar_native_fast_path_calls, ScalarNativeFastPathCalls);
			VS_COPY_JIT_STAT(inlined_call_sites, InlinedCallSites);
			VS_COPY_JIT_STAT(linear_frame_reuses, LinearFrameReuses);
			VS_COPY_JIT_STAT(linear_scratch_reuses, LinearScratchReuses);
			VS_COPY_JIT_STAT(linear_scratch_allocations, LinearScratchAllocations);
			VS_COPY_JIT_STAT(linear_argument_scratch_leases, LinearArgumentScratchLeases);
			VS_COPY_JIT_STAT(linear_argument_scratch_reuses, LinearArgumentScratchReuses);
			VS_COPY_JIT_STAT(scalar_method_fast_path_mixed, ScalarMethodFastPathMixed);
			VS_COPY_JIT_STAT(scalar_function_fast_path_mixed, ScalarFunctionFastPathMixed);
			VS_COPY_JIT_STAT(direct_native_calls, DirectNativeCalls);
			VS_COPY_JIT_STAT(cost_model_vm_fallbacks, CostModelVmFallbacks);
			VS_COPY_JIT_STAT(native_typed_loop_executions, NativeTypedLoopExecutions);
			VS_COPY_JIT_STAT(controlled_native_executions, ControlledNativeExecutions);
			VS_COPY_JIT_STAT(aot_executions, AotExecutions);
			VS_COPY_JIT_STAT(native_leaf_executions, NativeLeafExecutions);
			VS_COPY_JIT_STAT(native_cfg_executions, NativeCfgExecutions);
			VS_COPY_JIT_STAT(bridge_executions, BridgeExecutions);
			VS_COPY_JIT_STAT(linear_executions, LinearExecutions);
			VS_COPY_JIT_STAT(vm_trampoline_executions, VmTrampolineExecutions);
			#undef VS_COPY_JIT_STAT
			const std::size_t CopySize = WritableSize < sizeof(Stats)
			    ? WritableSize
			    : sizeof(Stats);
			std::memcpy(out_stats, &Stats, CopySize);
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_package_read(
	    const vs_interop_byte_view bytes,
	    vs_interop_package** out_package,
	    vs_interop_buffer** out_error_utf8)
	{
		if (!out_package || !out_error_utf8 || !VeroScript::Interop::IsValidBytes(bytes))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_package = nullptr;
		*out_error_utf8 = nullptr;
		try
		{
			auto Package = std::make_unique<vs_interop_package>();
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			if (bytes.size >= 4 && bytes.data[0] == 'P' && bytes.data[1] == 'K')
			{
				if (bytes.size > (1ull << 30)) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				std::vector<std::uint8_t> Container(bytes.data, bytes.data + static_cast<std::size_t>(bytes.size));
				VeroScript::Application::FDiagnostic E;
				Package->Application = VeroScript::Application::FApplication::Open(std::move(Container),
				    VeroScript::Application::CreateApplicationServices(), E);
				if (Package->Application)
				{
					if (Package->Application->IsSourceEntry())
					{
						Package->Data.Kind = VeroScript::EPackageKind::Application;
						Package->Data.PackageName = Package->Application->GetId();
						*out_package = Package.release();
						return VS_INTEROP_STATUS_OK;
					}
					if (Package->Application->ReadEntryPackage(Package->Data, E))
					{
						*out_package = Package.release();
						return VS_INTEROP_STATUS_OK;
					}
				}
				if (Package->LastError.empty())
				{
					Package->LastError = E.Code + ": " + E.Message;
				}
				const auto S = VeroScript::Interop::MakeTextBuffer(Package->LastError, out_error_utf8);
				return S == VS_INTEROP_STATUS_OK ? VS_INTEROP_STATUS_ENGINE_ERROR : S;
			}
			#endif
			if (!VeroScript::ReadPackage(
			        VeroScript::FByteView(bytes.data, static_cast<std::size_t>(bytes.size)),
			        Package->Data, Package->LastError))
			{
				const auto BufferStatus = VeroScript::Interop::MakeTextBuffer(
				    Package->LastError, out_error_utf8);
				return BufferStatus == VS_INTEROP_STATUS_OK
				    ? VS_INTEROP_STATUS_ENGINE_ERROR
				    : BufferStatus;
			}
			*out_package = Package.release();
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

	void VS_INTEROP_CALL vs_interop_package_destroy(vs_interop_package* package)
	{
		try
		{
			delete package;
		}
		catch (...)
		{
		}
	}

	vs_interop_package_kind VS_INTEROP_CALL vs_interop_package_get_kind(
	    const vs_interop_package* package)
	{
		if (!package)
		{
			return 0;
		}
		return package->Data.Kind == VeroScript::EPackageKind::Application
		    ? VS_INTEROP_PACKAGE_APPLICATION
		    : VS_INTEROP_PACKAGE_LIBRARY;
	}

	vs_interop_string_view VS_INTEROP_CALL vs_interop_package_get_name(
	    const vs_interop_package* package)
	{
		return package ? VeroScript::Interop::ViewOf(package->Data.PackageName)
		               : vs_interop_string_view{nullptr, 0};
	}

	uint64_t VS_INTEROP_CALL vs_interop_package_get_version(
	    const vs_interop_package* package)
	{
		return package ? package->Data.PackageVersion : 0;
	}

	uint64_t VS_INTEROP_CALL vs_interop_package_metadata_count(
	    const vs_interop_package* package)
	{
		return package ? static_cast<uint64_t>(package->Data.HostMetadata.size()) : 0;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_package_get_metadata(
	    const vs_interop_package* package,
	    const uint64_t index,
	    vs_interop_package_metadata* out_metadata)
	{
		if (!package || !out_metadata || index >= package->Data.HostMetadata.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		auto It = package->Data.HostMetadata.begin();
		std::advance(It, static_cast<std::ptrdiff_t>(index));
		out_metadata->key = VeroScript::Interop::ViewOf(It->first);
		out_metadata->value = {
		    It->second.data(), static_cast<uint64_t>(It->second.size())};
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_byte_view VS_INTEROP_CALL vs_interop_package_get_canonical_signed_bytes(
	    const vs_interop_package* package)
	{
		if (!package)
		{
			return {nullptr, 0};
		}
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		if (package->Application)
		{
			const auto View = package->Application->GetContainerBytes();
			return {View.Data, static_cast<std::uint64_t>(View.Size)};
		}
		#endif
		return {package->Data.CanonicalSignedBytes.data(),
		    static_cast<uint64_t>(package->Data.CanonicalSignedBytes.size())};
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_load_package(
	    vs_interop_session* session,
	    vs_interop_package* package,
	    const vs_interop_string_view logical_path,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !package || (!package->Data.Module
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		                                                   && !package->Application
		#endif
		                                                   ) ||
		    !VeroScript::Interop::IsValidText(logical_path) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
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
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			if (package->Application) return VeroScript::Interop::StoreRunResult(State, State.Session.LoadApplication(package->Application), out_result);
			#endif
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.LoadPackage(package->Data,
			        VeroScript::Interop::CopyString(logical_path)),
			    out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_reload_package(
	    vs_interop_session* session,
	    vs_interop_package* package,
	    const vs_interop_string_view logical_path,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !package || (!package->Data.Module
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		                                                   && !package->Application
		#endif
		                                                   ) ||
		    !VeroScript::Interop::IsValidText(logical_path) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
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
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			if (package->Application) return VeroScript::Interop::StoreRunResult(State, State.Session.ReloadApplication(package->Application), out_result);
			#endif
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.ReloadPackage(package->Data,
			        VeroScript::Interop::CopyString(logical_path)),
			    out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_build_package_file(
	    vs_interop_session* session,
	    const vs_interop_string_view source_path,
	    const vs_interop_package_build_options* options,
	    vs_interop_buffer** out_package_bytes,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !options || !out_package_bytes || !VeroScript::Interop::IsValidText(source_path) || source_path.size == 0 || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_package_bytes = nullptr;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::FPackageBuildOptions BuildOptions;
			std::string Error;
			if (!VeroScript::Interop::ConvertPackageOptions(*options, BuildOptions, Error))
			{
				VeroScript::Interop::SetError(State, std::move(Error));
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			std::unique_ptr<VeroScript::FBytecodeModule> Module;
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			const auto CompileResult = State.Session.CompileFile(
			    VeroScript::Interop::CopyString(source_path), Module);
			if (!CompileResult.bSuccess)
			{
				return VeroScript::Interop::StoreRunResult(State, CompileResult, out_result);
			}
			std::vector<std::uint8_t> Bytes;
			bool Built = false;
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			if (BuildOptions.Kind == VeroScript::EPackageKind::Application)
			{
				std::vector<VeroScript::Application::FContentFile> Content;
				VeroScript::Application::FDiagnostic E;
				Built = VeroScript::Application::BuildApplication(*Module, BuildOptions, "0.1.1", {}, Content, E) &&
				    VeroScript::Application::WriteArchive(Content, Bytes, E);
				if (!Built)
				{
					Error = E.Code + ": " + E.Message;
				}
			}
			else
			#endif
				Built = VeroScript::BuildPackage(*Module, BuildOptions, Bytes, Error);
			if (!Built)
			{
				return VeroScript::Interop::StoreRunResult(
				    State, VeroScript::Interop::MakePackageFailure(Error), out_result);
			}
			const auto BufferStatus = VeroScript::Interop::MakeBuffer(
			    std::move(Bytes), out_package_bytes);
			if (BufferStatus != VS_INTEROP_STATUS_OK)
			{
				return BufferStatus;
			}
			return VeroScript::Interop::StoreRunResult(State, CompileResult, out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}
}
