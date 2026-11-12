// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	vs_interop_string_view View(const char* Text)
	{
		return {Text, static_cast<std::uint64_t>(std::strlen(Text))};
	}

	vs_interop_string_view View(const std::string& Text)
	{
		return {Text.data(), static_cast<std::uint64_t>(Text.size())};
	}

	bool Expect(const bool Condition, const char* Message)
	{
		if (Condition)
		{
			return true;
		}
		std::cerr << "FAILED: " << Message << '\n';
		return false;
	}

	struct FOutput
	{
		std::vector<std::string> Lines;
		std::vector<std::string> Diagnostics;
	};

	void VS_INTEROP_CALL OnPrint(const vs_interop_string_view Line, void* UserData)
	{
		auto& Output = *static_cast<FOutput*>(UserData);
		Output.Lines.emplace_back(Line.data, static_cast<std::size_t>(Line.size));
	}

	void VS_INTEROP_CALL OnDiagnostic(const vs_interop_string_view Line, void* UserData)
	{
		auto& Output = *static_cast<FOutput*>(UserData);
		Output.Diagnostics.emplace_back(Line.data, static_cast<std::size_t>(Line.size));
	}

	void VS_INTEROP_CALL DestroyCounter(void* Payload)
	{
		++*static_cast<int*>(Payload);
	}

	vs_interop_callback_result VS_INTEROP_CALL HostBump(
	    vs_interop_call_context* Context,
	    vs_interop_value*,
	    void*)
	{
		vs_interop_value* Value = nullptr;
		int32_t Number = 0;
		if (vs_interop_call_context_get_mutable_argument(Context, 0, &Value) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		/* Borrowed callback wrappers are never owned by the adapter. Destruction
		 * must be harmless even if a foreign-runtime finalizer calls it. */
		vs_interop_value_destroy(Value);
		if (vs_interop_value_reset(Value) != VS_INTEROP_STATUS_INVALID_ARGUMENT || vs_interop_value_get_int32(Value, &Number) != VS_INTEROP_STATUS_OK || vs_interop_value_set_int32(Value, Number + 10) != VS_INTEROP_STATUS_OK)
		{
			vs_interop_call_context_set_error(Context, View("host_bump failed"));
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostNested(
	    vs_interop_call_context* Context,
	    vs_interop_value* OutReturn,
	    void*)
	{
		// Callback return wrappers are borrowed too; an accidental foreign
		// finalizer must not invalidate the slot the callback still has to fill.
		vs_interop_value_destroy(OutReturn);
		const vs_interop_value* Input = nullptr;
		if (vs_interop_call_context_get_argument(Context, 0, &Input) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		/* A by-value callback argument is a read-only borrowed view. Calls that may
		 * write ref/out values must reject it before entering the script. */
		auto* ReadOnlyInput = const_cast<vs_interop_value*>(Input);
		vs_interop_value* ReadOnlyArgs[] = {ReadOnlyInput};
		if (vs_interop_value_reset(ReadOnlyInput) != VS_INTEROP_STATUS_INVALID_ARGUMENT || vs_interop_call_context_invoke_function(Context, View("Double"), ReadOnlyArgs, 1, OutReturn) != VS_INTEROP_STATUS_INVALID_ARGUMENT)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		vs_interop_value* Copy = nullptr;
		if (vs_interop_value_clone(Input, &Copy) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		vs_interop_value* Args[] = {Copy};
		const vs_interop_status Status = vs_interop_call_context_invoke_function(
		    Context, View("Double"), Args, 1, OutReturn);
		vs_interop_value_destroy(Copy);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_call_context_set_error(Context, View("nested invocation failed"));
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return VS_INTEROP_CALLBACK_OK;
	}

	struct FNestedFailureProbe
	{
		vs_interop_status EngineStatus = VS_INTEROP_STATUS_OK;
		vs_interop_status ExceptionStatus = VS_INTEROP_STATUS_OK;
	};

	vs_interop_callback_result VS_INTEROP_CALL HostNestedFailureBoundary(
	    vs_interop_call_context* Context,
	    vs_interop_value*,
	    void* UserData)
	{
		auto& Probe = *static_cast<FNestedFailureProbe*>(UserData);
		vs_interop_call_context_set_error(Context, {nullptr, 0});
		Probe.EngineStatus = vs_interop_call_context_invoke_function(
		    Context, View("MissingNestedFunction"), nullptr, 0, nullptr);

		/* On 64-bit hosts this exceeds vector<FValue>::max_size() and exercises the
		 * nested exception translator. A 32-bit adapter rejects the uint64_t range
		 * before attempting allocation. Neither path may unwind through the C ABI. */
		vs_interop_value* Placeholder = nullptr;
		vs_interop_value* Arguments[] = {Placeholder};
		Probe.ExceptionStatus = vs_interop_call_context_invoke_function(Context,
		    View("Double"), Arguments, std::numeric_limits<std::uint64_t>::max(), nullptr);
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostMissingReturn(
	    vs_interop_call_context*,
	    vs_interop_value*,
	    void*)
	{
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostNumericPrimitives(
	    vs_interop_call_context* Context,
	    vs_interop_value* OutReturn,
	    void*)
	{
		const vs_interop_value* Args[5]{};
		int8_t I8 = 0;
		uint8_t U8 = 0;
		int16_t I16 = 0;
		uint16_t U16 = 0;
		float F32 = 0.0f;
		for (uint64_t Index = 0; Index < 5; ++Index)
		{
			if (vs_interop_call_context_get_argument(Context, Index, &Args[Index]) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
		}
		if (vs_interop_value_get_int8(Args[0], &I8) != VS_INTEROP_STATUS_OK || vs_interop_value_get_uint8(Args[1], &U8) != VS_INTEROP_STATUS_OK || vs_interop_value_get_int16(Args[2], &I16) != VS_INTEROP_STATUS_OK || vs_interop_value_get_uint16(Args[3], &U16) != VS_INTEROP_STATUS_OK || vs_interop_value_get_float(Args[4], &F32) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return vs_interop_value_set_float(OutReturn,
		           static_cast<float>(I8) + static_cast<float>(U8) + static_cast<float>(I16) + static_cast<float>(U16) + F32) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostCharContainers(
	    vs_interop_call_context* Context,
	    vs_interop_value*,
	    void* UserData)
	{
		const vs_interop_value* Arguments[3]{};
		for (std::uint64_t Index = 0; Index < 3; ++Index)
		{
			if (vs_interop_call_context_get_argument(Context, Index, &Arguments[Index]) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
		}
		if (vs_interop_value_get_kind(Arguments[0]) != VS_INTEROP_VALUE_MAP || vs_interop_value_get_kind(Arguments[1]) != VS_INTEROP_VALUE_ORDERED_MAP || vs_interop_value_get_kind(Arguments[2]) != VS_INTEROP_VALUE_SET)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		*static_cast<bool*>(UserData) = true;
		return VS_INTEROP_CALLBACK_OK;
	}

	struct FReentrantState
	{
		vs_interop_session* Session = nullptr;
		vs_interop_status ClearStatus = VS_INTEROP_STATUS_OK;
		vs_interop_status ConfigStatus = VS_INTEROP_STATUS_OK;
	};

	vs_interop_callback_result VS_INTEROP_CALL HostRejectStateMutation(
	    vs_interop_call_context*,
	    vs_interop_value*,
	    void* UserData)
	{
		auto& State = *static_cast<FReentrantState*>(UserData);
		State.ClearStatus = vs_interop_session_clear(State.Session);
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		State.ConfigStatus = vs_interop_session_apply_config(State.Session, &Config);
		return VS_INTEROP_CALLBACK_OK;
	}

	struct FWorkerCallbackProbe
	{
		std::thread::id OwnerThread;
		std::atomic<int> Calls{0};
		std::atomic<bool> RanOffOwner{false};
		std::atomic<int> CaptureCalls{0};
		std::atomic<vs_interop_status> VariantClone{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<vs_interop_status> HostClone{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<vs_interop_status> EnumClone{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<int> BoxCalls{0};
		std::atomic<vs_interop_status> VariantExtract{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<vs_interop_status> HostExtract{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<vs_interop_status> EnumExtract{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<bool> ExtractionPreserved{false};
		std::atomic<int> MapCalls{0};
		std::atomic<vs_interop_status> MapExtract{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<vs_interop_status> HostMapExtract{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<vs_interop_status> EnumMapExtract{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		std::atomic<bool> MapExtractionPreserved{false};
		std::atomic<int> Destructors{0};
		std::atomic<vs_interop_status> Injection{VS_INTEROP_STATUS_INVALID_ARGUMENT};
		const vs_interop_value* ExternalStorage = nullptr;
	};

	vs_interop_callback_result VS_INTEROP_CALL HostWorkerCapture(
	    vs_interop_call_context* Context, vs_interop_value*, void* UserData)
	{
		auto& Probe = *static_cast<FWorkerCallbackProbe*>(UserData);
		std::atomic<vs_interop_status>* Statuses[] = {&Probe.VariantClone, &Probe.HostClone, &Probe.EnumClone};
		for (std::uint64_t Index = 0; Index != 3; ++Index)
		{
			const vs_interop_value* Input = nullptr;
			if (vs_interop_call_context_get_argument(Context, Index, &Input) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			vs_interop_value* Copy = nullptr;
			Statuses[Index]->store(vs_interop_value_clone(Input, &Copy), std::memory_order_relaxed);
			vs_interop_value_destroy(Copy);
		}
		Probe.CaptureCalls.fetch_add(1, std::memory_order_relaxed);
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostWorkerEmpty(
	    vs_interop_call_context*, vs_interop_value* OutReturn, void* UserData)
	{
		auto& Probe = *static_cast<FWorkerCallbackProbe*>(UserData);
		if (vs_interop_value_create_array(OutReturn, View("variant"), nullptr, 0) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		// A rejected external alias must leave the private return usable by script.
		Probe.Injection.store(vs_interop_value_assign(OutReturn, Probe.ExternalStorage), std::memory_order_relaxed);
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostWorkerCaptureBox(
	    vs_interop_call_context* Context, vs_interop_value*, void* UserData)
	{
		auto& Probe = *static_cast<FWorkerCallbackProbe*>(UserData);
		std::atomic<vs_interop_status>* Statuses[] = {&Probe.VariantExtract, &Probe.HostExtract, &Probe.EnumExtract};
		for (std::uint64_t Index = 0; Index != 3; ++Index)
		{
			const vs_interop_value* Input = nullptr;
			if (vs_interop_call_context_get_argument(Context, Index, &Input) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			vs_interop_value* Copy = nullptr;
			if (vs_interop_value_create(&Copy) != VS_INTEROP_STATUS_OK) return VS_INTEROP_CALLBACK_ERROR;
			(void)vs_interop_value_set_int32(Copy, 17);
			Statuses[Index]->store(vs_interop_variant_get(Input, Copy), std::memory_order_relaxed);
			if (Index == 0)
			{
				std::int32_t Preserved = 0;
				Probe.ExtractionPreserved.store(vs_interop_value_get_int32(Copy, &Preserved) == VS_INTEROP_STATUS_OK && Preserved == 17, std::memory_order_relaxed);
			}
			vs_interop_value_destroy(Copy);
		}
		Probe.BoxCalls.fetch_add(1, std::memory_order_relaxed);
		return VS_INTEROP_CALLBACK_OK;
	}

	struct FWorkerMapVisit
	{
		FWorkerCallbackProbe& Probe;
		std::size_t Index;
	};

	vs_interop_callback_result VS_INTEROP_CALL VisitWorkerMap(
	    const vs_interop_value*, const vs_interop_value* Mapped, void* UserData)
	{
		auto& Visit = *static_cast<FWorkerMapVisit*>(UserData);
		vs_interop_value* Copy = nullptr;
		if (vs_interop_value_create(&Copy) != VS_INTEROP_STATUS_OK) return VS_INTEROP_CALLBACK_ERROR;
		(void)vs_interop_value_set_int32(Copy, 17);
		std::atomic<vs_interop_status>* Statuses[] = {
		    &Visit.Probe.MapExtract, &Visit.Probe.HostMapExtract, &Visit.Probe.EnumMapExtract};
		Statuses[Visit.Index]->store(vs_interop_value_assign(Copy, Mapped), std::memory_order_relaxed);
		if (Visit.Index == 0)
		{
			std::int32_t Preserved = 0;
			Visit.Probe.MapExtractionPreserved.store(vs_interop_value_get_int32(Copy, &Preserved) == VS_INTEROP_STATUS_OK && Preserved == 17, std::memory_order_relaxed);
		}
		vs_interop_value_destroy(Copy);
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostWorkerCaptureMap(
	    vs_interop_call_context* Context, vs_interop_value* OutReturn, void* UserData)
	{
		auto& Probe = *static_cast<FWorkerCallbackProbe*>(UserData);
		for (std::uint64_t Index = 0; Index != 3; ++Index)
		{
			const vs_interop_value* Input = nullptr;
			if (vs_interop_call_context_get_argument(Context, Index, &Input) != VS_INTEROP_STATUS_OK || vs_interop_variant_get(Input, OutReturn) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			FWorkerMapVisit Visit{Probe, static_cast<std::size_t>(Index)};
			if (vs_interop_value_map_enumerate(OutReturn, &VisitWorkerMap, &Visit) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
		}
		Probe.MapCalls.fetch_add(1, std::memory_order_relaxed);
		return vs_interop_value_set_variant_empty(OutReturn) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostWorkerDestroyed(
	    vs_interop_call_context*, vs_interop_value*, void* UserData)
	{
		static_cast<FWorkerCallbackProbe*>(UserData)->Destructors.fetch_add(1, std::memory_order_relaxed);
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL HostWorkerProbe(
	    vs_interop_call_context* Context,
	    vs_interop_value* OutReturn,
	    void* UserData)
	{
		auto& Probe = *static_cast<FWorkerCallbackProbe*>(UserData);
		Probe.RanOffOwner.store(
		    std::this_thread::get_id() != Probe.OwnerThread,
		    std::memory_order_relaxed);
		Probe.Calls.fetch_add(1, std::memory_order_relaxed);
		const vs_interop_value* Input = nullptr;
		int32_t Number = 0;
		if (vs_interop_call_context_get_argument(Context, 0, &Input) != VS_INTEROP_STATUS_OK || vs_interop_value_get_int32(Input, &Number) != VS_INTEROP_STATUS_OK || vs_interop_value_set_int32(OutReturn, Number + 1) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return VS_INTEROP_CALLBACK_OK;
	}

	struct FValueOwner
	{
		FValueOwner()
		{
			(void)vs_interop_value_create(&Value);
		}

		~FValueOwner()
		{
			vs_interop_value_destroy(Value);
		}

		FValueOwner(const FValueOwner&) = delete;

		FValueOwner& operator=(const FValueOwner&) = delete;

		vs_interop_value* Value = nullptr;
	};

	struct FDestroyOnPrint
	{
		vs_interop_session* Session = nullptr;
		bool Called = false;
	};

	void VS_INTEROP_CALL DestroySessionOnPrint(vs_interop_string_view, void* UserData)
	{
		auto& State = *static_cast<FDestroyOnPrint*>(UserData);
		State.Called = true;
		vs_interop_session* Session = State.Session;
		State.Session = nullptr;
		(void)vs_interop_session_destroy(Session);
	}
}

static bool TestJitBackendSelection()
{
	vs_interop_session* Session = nullptr;
	vs_interop_session_options Options{sizeof(Options)};
	if (!Expect(vs_interop_session_create(&Options, &Session) == VS_INTEROP_STATUS_OK, "selection session")) return false;
	const auto Destroy = [](vs_interop_session* Value)
	{
		vs_interop_session_destroy(Value);
	};
	std::unique_ptr<vs_interop_session, decltype(Destroy)> Owner(Session, Destroy);
	vs_interop_jit_backend Selected = 999;
	vs_interop_jit_backend_info Info{sizeof(Info)};
	if (!Expect(vs_interop_session_get_jit_backend(Session, &Selected) == VS_INTEROP_STATUS_OK && Selected == VS_INTEROP_JIT_BACKEND_NONE && vs_interop_session_get_jit_backend_info(Session, &Info) == VS_INTEROP_STATUS_OK && std::string(Info.id.data, static_cast<std::size_t>(Info.id.size)) == "none" && Info.native_code == 0, "no implicit backend"))
	{
		return false;
	}
	Info.struct_size = 1;
	if (!Expect(vs_interop_session_get_jit_backend_info(Session, &Info) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "short metadata rejected")) return false;
	Info.struct_size = sizeof(Info);
	vs_interop_status SetStatus = VS_INTEROP_STATUS_OK;
	vs_interop_status GetStatus = VS_INTEROP_STATUS_OK;
	vs_interop_status InfoStatus = VS_INTEROP_STATUS_OK;
	std::thread Foreign([&]()
	{
		SetStatus = vs_interop_session_set_jit_backend(Session, {});
		GetStatus = vs_interop_session_get_jit_backend(Session, &Selected);
		InfoStatus = vs_interop_session_get_jit_backend_info(Session, &Info);
	});
	Foreign.join();
	if (!Expect(SetStatus == VS_INTEROP_STATUS_WRONG_THREAD && GetStatus == VS_INTEROP_STATUS_WRONG_THREAD && InfoStatus == VS_INTEROP_STATUS_WRONG_THREAD,
	        "backend selection and metadata remain owner-thread confined"))
	{
		return false;
	}
	const char EmbeddedNull[] = {'a', 0, 'b'};
	if (!Expect(vs_interop_session_set_jit_backend(Session, {nullptr, 1}) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_set_jit_backend(Session, {EmbeddedNull, sizeof(EmbeddedNull)}) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
	        "invalid backend text rejected"))
	{
		return false;
	}
	vs_interop_config Config{sizeof(Config)};
	Config.execution_backend = VS_INTEROP_BACKEND_JIT;
	vs_interop_run_result Result{sizeof(Result)};
	if (!Expect(vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, View("int value(){return 49;}void main(){print(value());}"), View("choice.vs"), &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success, "forced JIT requires explicit backend"))
	{
		return false;
	}
	const char* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	if (Path && *Path)
	{
		if (!Expect(vs_interop_session_set_jit_backend(Session, View(Path)) == VS_INTEROP_STATUS_OK && vs_interop_session_get_jit_backend(Session, &Selected) == VS_INTEROP_STATUS_OK && Selected == VS_INTEROP_JIT_BACKEND_EXTERNAL && vs_interop_session_get_jit_backend_info(Session, &Info) == VS_INTEROP_STATUS_OK && std::string(Info.id.data, static_cast<std::size_t>(Info.id.size)) == "asmjit" && vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success, "explicit backend execution"))
		{
			return false;
		}
		vs_interop_jit_stats Stats{sizeof(Stats)};
		if (!Expect(vs_interop_session_get_last_jit_stats(Session, &Stats) == VS_INTEROP_STATUS_OK && Stats.dispatches > 0, "dynamic backend dispatch")) return false;
	}
	if (!Expect(vs_interop_session_set_jit_backend(Session, View("missing-backend")) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_get_jit_backend(Session, &Selected) == VS_INTEROP_STATUS_OK && Selected == (Path && *Path ? VS_INTEROP_JIT_BACKEND_EXTERNAL : VS_INTEROP_JIT_BACKEND_NONE), "failure preserves selection"))
	{
		return false;
	}
	if (!Expect(vs_interop_session_set_jit_backend(Session, {}) == VS_INTEROP_STATUS_OK, "empty path disables backend")) return false;
	return true;
}

static bool TestStandardLibraryConfigurationLimits()
{
	for (const bool WithEnvironment : {false, true})
	{
		vs_interop_session* Raw = nullptr;
		if (!Expect(vs_interop_session_create(nullptr, &Raw) == VS_INTEROP_STATUS_OK,
		        "stdlib configuration session"))
		{
			return false;
		}
		const auto Destroy = [](vs_interop_session* Value)
		{
			(void)vs_interop_session_destroy(Value);
		};
		const std::unique_ptr<vs_interop_session, decltype(Destroy)> Owner(Raw, Destroy);
		vs_interop_stdlib_limits Limits{sizeof(Limits), 0, 5, 4, 2};
		vs_interop_config Config{sizeof(Config)};
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		vs_interop_run_result Result{sizeof(Result)};
		const auto Apply = [&]
		{
			return WithEnvironment ? vs_interop_session_apply_config_with_environment(Raw, &Config, nullptr)
			                       : vs_interop_session_apply_config(Raw, &Config);
		};
		if (!Expect(vs_interop_session_set_stdlib_limits(Raw, &Limits) == VS_INTEROP_STATUS_OK && Apply() == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Raw, View("void main(){format(\"{0}{0}\",\"abc\");}"), View("stdlib-configuration.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Raw, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success, "configuration preserves separately configured stdlib limits"))
		{
			return false;
		}
		Limits.max_text_bytes = Limits.max_read_bytes = Limits.max_split_parts = 0;
		if (!Expect(vs_interop_session_set_stdlib_limits(Raw, &Limits) == VS_INTEROP_STATUS_OK && Apply() == VS_INTEROP_STATUS_OK && vs_interop_session_run(Raw, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success, "explicit stdlib reset removes the retained limit"))
		{
			return false;
		}
	}
	return true;
}

struct FSessionBoundaryProbe
{
	vs_interop_session* First = nullptr;
	vs_interop_session* Second = nullptr;
	vs_interop_value* Foreign = nullptr;
	vs_interop_call_context* FirstCall = nullptr;
	const vs_interop_value* FirstReturn = nullptr;
	int Serializations = 0, Deserializations = 0;
	bool CallbackAssignmentRejected = false, ContextRestored = false, ScalarAggregationChecked = false;
};

static vs_interop_callback_result VS_INTEROP_CALL BoundarySerialize(
    vs_interop_external_serialize_context* Context, void* User)
{
	++static_cast<FSessionBoundaryProbe*>(User)->Serializations;
	const uint8_t Bytes[] = {1};
	return vs_interop_external_serialize_context_set_bytes(Context, {Bytes, sizeof(Bytes)}) == VS_INTEROP_STATUS_OK ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
}

static vs_interop_callback_result VS_INTEROP_CALL BoundaryDeserialize(
    vs_interop_external_deserialize_context* Context, void* User)
{
	++static_cast<FSessionBoundaryProbe*>(User)->Deserializations;
	FValueOwner Number;
	return Number.Value && vs_interop_value_set_int32(Number.Value, 55) == VS_INTEROP_STATUS_OK && vs_interop_external_deserialize_context_add_field(Context, View("value"), Number.Value) == VS_INTEROP_STATUS_OK ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
}

static vs_interop_callback_result VS_INTEROP_CALL BoundaryReferences(
    vs_interop_external_reference_kind, const vs_interop_value*, void* User)
{
	++*static_cast<int*>(User);
	return VS_INTEROP_CALLBACK_OK;
}

static vs_interop_callback_result VS_INTEROP_CALL BoundaryInSecond(
    vs_interop_call_context* Call, vs_interop_value* Out, void* User)
{
	auto& Probe = *static_cast<FSessionBoundaryProbe*>(User);
	FValueOwner FirstValue, SecondValue;
	vs_interop_run_result Result{sizeof(Result)};
	if (!FirstValue.Value || !SecondValue.Value || !Probe.FirstCall || !Probe.FirstReturn || vs_interop_value_set_int32(Out, 77) != VS_INTEROP_STATUS_OK)
	{
		return VS_INTEROP_CALLBACK_ERROR;
	}
	// Independent scalar values retain callback identity for later writes, but
	// their different originating sessions must not prevent aggregation.
	const vs_interop_value* Scalars[] = {Probe.FirstReturn, Out};
	int32_t Scalar = 0;
	Probe.ScalarAggregationChecked = vs_interop_value_create_array(FirstValue.Value, View("int"), Scalars, 2) == VS_INTEROP_STATUS_OK && vs_interop_value_sequence_get(FirstValue.Value, 0, SecondValue.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(SecondValue.Value, &Scalar) == VS_INTEROP_STATUS_OK && Scalar == 99 && vs_interop_value_sequence_get(FirstValue.Value, 1, SecondValue.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(SecondValue.Value, &Scalar) == VS_INTEROP_STATUS_OK && Scalar == 77;
	if (!Expect(Probe.ScalarAggregationChecked,
	        "independent scalar callback values from different sessions form an array"))
	{
		return VS_INTEROP_CALLBACK_ERROR;
	}
	// A result from the still-active first context must not be published into
	// this second context's native return slot, even though both are borrowed.
	const auto Rejected = vs_interop_call_context_invoke_function(Probe.FirstCall, View("make"),
	    nullptr, 0, Out);
	int32_t Unchanged = 0;
	const bool Kept = vs_interop_value_get_int32(Out, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 77;
	Probe.ContextRestored = Rejected == VS_INTEROP_STATUS_ENGINE_ERROR && Kept && vs_interop_call_context_invoke_function(Probe.FirstCall, View("make"), nullptr, 0, FirstValue.Value) == VS_INTEROP_STATUS_OK && vs_interop_session_get_field(Probe.Second, FirstValue.Value, View("value"), SecondValue.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_call_context_invoke_function(Call, View("make"), nullptr, 0, SecondValue.Value) == VS_INTEROP_STATUS_OK && vs_interop_session_get_field(Probe.First, SecondValue.Value, View("value"), FirstValue.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR;
	return Probe.ContextRestored && vs_interop_value_set_int32(Out, 42) == VS_INTEROP_STATUS_OK
	    ? VS_INTEROP_CALLBACK_OK
	    : VS_INTEROP_CALLBACK_ERROR;
}

static vs_interop_callback_result VS_INTEROP_CALL BoundaryInFirst(
    vs_interop_call_context* Call, vs_interop_value* Out, void* User)
{
	auto& Probe = *static_cast<FSessionBoundaryProbe*>(User);
	if (vs_interop_value_set_int32(Out, 99) != VS_INTEROP_STATUS_OK) return VS_INTEROP_CALLBACK_ERROR;
	const auto Assignment = vs_interop_value_assign(Out, Probe.Foreign);
	int32_t Unchanged = 0;
	Probe.CallbackAssignmentRejected = Assignment == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_value_get_int32(Out, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 99;
	Probe.FirstCall = Call;
	Probe.FirstReturn = Out;
	FValueOwner Other;
	vs_interop_run_result Result{sizeof(Result)};
	const auto Status = Other.Value ? vs_interop_session_call_function(Probe.Second, View("entry"),
	                                      nullptr, 0, Other.Value, &Result)
	                                : VS_INTEROP_STATUS_OUT_OF_MEMORY;
	Probe.FirstCall = nullptr;
	Probe.FirstReturn = nullptr;
	return Probe.CallbackAssignmentRejected && Probe.ContextRestored && Probe.ScalarAggregationChecked && Status == VS_INTEROP_STATUS_OK
	    ? VS_INTEROP_CALLBACK_OK
	    : VS_INTEROP_CALLBACK_ERROR;
}

static bool TestCrossSessionObjectBoundaries()
{
	FSessionBoundaryProbe Probe;
	if (!Expect(vs_interop_session_create(nullptr, &Probe.First) == VS_INTEROP_STATUS_OK && vs_interop_session_create(nullptr, &Probe.Second) == VS_INTEROP_STATUS_OK,
	        "cross-session boundary sessions"))
	{
		(void)vs_interop_session_destroy(Probe.First);
		return false;
	}
	const auto Destroy = [](vs_interop_session* Value)
	{
		(void)vs_interop_session_destroy(Value);
	};
	const std::unique_ptr<vs_interop_session, decltype(Destroy)> First(Probe.First, Destroy), Second(Probe.Second, Destroy);
	FValueOwner Object, Foreign, Output;
	Probe.Foreign = Foreign.Value;
	vs_interop_native_type_desc Return{sizeof(Return)};
	Return.type = VS_INTEROP_NATIVE_INT32;
	vs_interop_run_result Result{sizeof(Result)};
	if (!Expect(Object.Value && Foreign.Value && Output.Value && vs_interop_session_register_native(Probe.First, View("boundary"), &Return, nullptr, 0, BoundaryInFirst, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Probe.Second, View("other_boundary"), &Return, nullptr, 0, BoundaryInSecond, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_register_external_serializer(Probe.Second, View("Item"), 1, BoundarySerialize, BoundaryDeserialize, &Probe) == VS_INTEROP_STATUS_OK,
	        "cross-session boundary callbacks"))
	{
		return false;
	}
	const char* Common = "class Item{public:int value=11; int read(){return value;}}; "
	                     "Item make(){return Item();} int consume(Item item){return item.read();} Item current=Item(); ";
	if (!Expect(vs_interop_session_load_source(Probe.First, View(std::string(Common) + "int main(){return boundary();}"), View("boundary-first.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Probe.Second, View(std::string(Common) + "int entry(){return other_boundary();}"), View("boundary-second.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Probe.First, View("make"), nullptr, 0, Object.Value, &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Probe.Second, View("make"), nullptr, 0, Foreign.Value, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(Output.Value, 99) == VS_INTEROP_STATUS_OK,
	        "cross-session objects have matching public layouts"))
	{
		return false;
	}
	const auto RootedOutputStatus = vs_interop_session_call_function(Probe.First, View("make"),
	    nullptr, 0, Foreign.Value, &Result);
	vs_interop_diagnostic RootedOutputDiagnostic{sizeof(RootedOutputDiagnostic)};
	if (!Expect(RootedOutputStatus == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && Result.diagnostic_count != 0 && vs_interop_session_get_diagnostic(Probe.First, 0, &RootedOutputDiagnostic) == VS_INTEROP_STATUS_OK && RootedOutputDiagnostic.message.data && std::string_view(RootedOutputDiagnostic.message.data, static_cast<std::size_t>(RootedOutputDiagnostic.message.size)).find("foreign session") != std::string_view::npos,
	        "foreign rooted return output is preserved and produces a structured diagnostic"))
	{
		return false;
	}
	vs_interop_value* Arguments[] = {Object.Value};
	uint8_t Has = 1;
	const bool Rejected = vs_interop_session_get_field(Probe.Second, Object.Value, View("value"),
	                          Output.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
	    vs_interop_session_set_field(Probe.Second, Object.Value, View("value"), Output.Value,
	        &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
	    vs_interop_session_call_method(Probe.Second, Object.Value, View("read"), nullptr, 0,
	        Output.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
	    vs_interop_session_call_method_bypassing_interceptor(Probe.Second, Object.Value, View("read"),
	        nullptr, 0, Output.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
	    vs_interop_session_has_method(Probe.Second, Object.Value, View("read"), 0, &Has) == VS_INTEROP_STATUS_ENGINE_ERROR && Has == 0 && vs_interop_session_call_function(Probe.Second, View("consume"), Arguments, 1, Output.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_session_set_global(Probe.Second, View("current"), Object.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_session_apply_class_defaults(Probe.Second, Object.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR;
	vs_interop_buffer* Bytes = nullptr;
	uint32_t Version = 0;
	int References = 0;
	const uint8_t Input[] = {1};
	const bool SerializationRejected = vs_interop_session_serialize_object(Probe.Second, Object.Value,
	                                       &Bytes, &Version) == VS_INTEROP_STATUS_ENGINE_ERROR &&
	    !Bytes && Version == 0 && vs_interop_session_deserialize_object(Probe.Second, Object.Value, {Input, sizeof(Input)}, 1) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_session_enumerate_object_references(Probe.Second, Object.Value, BoundaryReferences, &References) == VS_INTEROP_STATUS_ENGINE_ERROR && References == 0 && Probe.Serializations == 0 && Probe.Deserializations == 0;
	vs_interop_buffer_destroy(Bytes);
	int32_t Value = 0;
	if (!Expect(Rejected && SerializationRejected && vs_interop_value_get_int32(Output.Value, &Value) == VS_INTEROP_STATUS_OK && Value == 99 && vs_interop_session_get_field(Probe.First, Object.Value, View("value"), Output.Value, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Output.Value, &Value) == VS_INTEROP_STATUS_OK && Value == 11,
	        "foreign objects are rejected before metadata access, callbacks or mutation"))
	{
		return false;
	}
	return Expect(vs_interop_session_run(Probe.First, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 99 && Probe.CallbackAssignmentRejected && Probe.ContextRestored && Probe.ScalarAggregationChecked,
	    "borrowed values and nested contexts retain their originating session");
}

struct FExpiredAggregateProbe
{
	~FExpiredAggregateProbe()
	{
		(void)vs_interop_session_destroy(Closing);
		(void)vs_interop_session_destroy(Target);
	}
	vs_interop_session* Closing = nullptr;
	vs_interop_session* Target = nullptr;
	const vs_interop_value* BorrowedForeign = nullptr;
	int Intercepts = 0;
	bool Rejected = false, NestedCallCompleted = false, EmptyContainer = false;
};

static vs_interop_callback_result VS_INTEROP_CALL CheckExpiredAggregate(
    vs_interop_call_context*, vs_interop_value* Out, void* User)
{
	auto& Probe = *static_cast<FExpiredAggregateProbe*>(User);
	if (!Probe.BorrowedForeign || vs_interop_value_set_int32(Out, 17) != VS_INTEROP_STATUS_OK)
	{
		return VS_INTEROP_CALLBACK_ERROR;
	}
	const vs_interop_value* Elements[] = {Probe.BorrowedForeign};
	const auto Status = vs_interop_value_create_array(Out,
	    View(Probe.EmptyContainer ? "array<Item>" : "Item"), Elements, 1);
	int32_t Unchanged = 0;
	Probe.Rejected = Status == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_value_get_int32(Out, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 17;
	if (!Probe.Rejected)
	{
		std::cerr << "Expired aggregate empty=" << Probe.EmptyContainer << " status=" << Status
		          << " output=" << Unchanged << '\n';
	}
	// Keep this fixture's native return well formed even when the regression is
	// exercised against an implementation that accepted the foreign aggregate.
	return vs_interop_value_set_int32(Out, 17) == VS_INTEROP_STATUS_OK
	    ? VS_INTEROP_CALLBACK_OK
	    : VS_INTEROP_CALLBACK_ERROR;
}

static vs_interop_intercept_result VS_INTEROP_CALL InterceptClosingAggregate(
    vs_interop_intercept_context* Context, vs_interop_value*, void* User)
{
	vs_interop_resolved_method Target{sizeof(Target)};
	if (vs_interop_intercept_context_get_target(Context, &Target) != VS_INTEROP_STATUS_OK)
	{
		return VS_INTEROP_INTERCEPT_FAILED;
	}
	if (!Target.method_name.data || Target.method_name.size != 7 || std::memcmp(Target.method_name.data, "inspect", 7) != 0)
	{
		return VS_INTEROP_INTERCEPT_UNHANDLED;
	}
	auto& Probe = *static_cast<FExpiredAggregateProbe*>(User);
	++Probe.Intercepts;
	if (Probe.EmptyContainer)
	{
		if (vs_interop_intercept_context_get_argument(Context, 0, &Probe.BorrowedForeign) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_INTERCEPT_FAILED;
		}
	}
	else
	{
		Probe.BorrowedForeign = vs_interop_intercept_context_receiver(Context);
	}
	if (!Probe.BorrowedForeign)
	{
		return VS_INTEROP_INTERCEPT_FAILED;
	}
	FValueOwner Return;
	vs_interop_run_result Result{sizeof(Result)};
	int32_t Value = 0;
	Probe.NestedCallCompleted = Return.Value && vs_interop_session_call_function(Probe.Target, View("entry"), nullptr, 0, Return.Value, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_value_get_int32(Return.Value, &Value) == VS_INTEROP_STATUS_OK && Value == 17;
	Probe.BorrowedForeign = nullptr;
	return Probe.NestedCallCompleted ? VS_INTEROP_INTERCEPT_UNHANDLED : VS_INTEROP_INTERCEPT_FAILED;
}

static bool TestExpiredAggregateOriginCase(const bool EmptyContainer)
{
	FExpiredAggregateProbe Probe;
	Probe.EmptyContainer = EmptyContainer;
	vs_interop_native_type_desc Integer{sizeof(Integer)};
	Integer.type = VS_INTEROP_NATIVE_INT32;
	vs_interop_run_result Result{sizeof(Result)};
	if (!Expect(vs_interop_session_create(nullptr, &Probe.Closing) == VS_INTEROP_STATUS_OK && vs_interop_session_create(nullptr, &Probe.Target) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Probe.Target, View("check_expired_aggregate"), &Integer, nullptr, 0, CheckExpiredAggregate, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Probe.Target, View("export class Item{};int entry(){return check_expired_aggregate();}"), View("expired-aggregate-target.vs"), &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_session_set_instance_call_interceptor(Probe.Closing, InterceptClosingAggregate, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_set_hot_reload_enabled(Probe.Closing, 1) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Probe.Closing, View(EmptyContainer ? "export class Item{public:void inspect(array<Item> items){}};"
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      "class Keeper{public:~Keeper(){Item local=Item();local.inspect(array<Item>());}};"
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      "Keeper live=Keeper();void main(){}"
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    : "export class Item{public:void inspect(){}};"
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      "class Keeper{public:~Keeper(){Item local=Item();local.inspect();}};"
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      "Keeper live=Keeper();void main(){}"),
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      View("expired-aggregate-source.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success && vs_interop_session_run(Probe.Closing, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Probe.Intercepts == 0,
	        "prepare aggregate created from a final-teardown borrow"))
	{
		return false;
	}
	// Synchronous finalization has no remaining strong FSessionState owner, but
	// intercepted views still carry their expired weak session identity. Empty
	// arrays also need the callback's live module context to classify their type.
	vs_interop_session* Closing = Probe.Closing;
	Probe.Closing = nullptr;
	return Expect(vs_interop_session_destroy(Closing) == VS_INTEROP_STATUS_OK && Probe.Intercepts == 1 && Probe.NestedCallCompleted && Probe.Rejected,
	    "foreign aggregate keeps its expired owner identity and preserves borrowed output");
}

static bool TestExpiredAggregateOrigin()
{
	return TestExpiredAggregateOriginCase(false) && TestExpiredAggregateOriginCase(true);
}

struct FNestedTeardownProbe
{
	~FNestedTeardownProbe()
	{
		// Value wrappers declared after this probe have already been destroyed.
		Foreign = nullptr;
		(void)vs_interop_session_destroy(First);
		(void)vs_interop_session_destroy(Second);
	}
	vs_interop_session* First = nullptr;
	vs_interop_session* Second = nullptr;
	vs_interop_value* Foreign = nullptr;
	int Destructions = 0;
	bool OriginChecked = false, Restored = false;
};

static vs_interop_callback_result VS_INTEROP_CALL CheckTeardownOrigin(
    vs_interop_call_context*, vs_interop_value* Out, void* User)
{
	auto& Probe = *static_cast<FNestedTeardownProbe*>(User);
	++Probe.Destructions;
	if (vs_interop_value_set_int32(Out, 21) != VS_INTEROP_STATUS_OK) return VS_INTEROP_CALLBACK_ERROR;
	const auto Status = vs_interop_value_assign(Out, Probe.Foreign);
	int32_t Unchanged = 0;
	Probe.OriginChecked = Status == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_value_get_int32(Out, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 21;
	return Probe.OriginChecked ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
}

static vs_interop_callback_result VS_INTEROP_CALL CloseOtherSession(
    vs_interop_call_context* Call, vs_interop_value* Out, void* User)
{
	auto& Probe = *static_cast<FNestedTeardownProbe*>(User);
	const auto Status = vs_interop_session_destroy(Probe.First);
	Probe.First = nullptr;
	FValueOwner Created;
	Probe.Restored = Status == VS_INTEROP_STATUS_OK && Created.Value && vs_interop_call_context_invoke_function(Call, View("make"), nullptr, 0, Created.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_get_kind(Created.Value) == VS_INTEROP_VALUE_OBJECT;
	return Probe.Restored && vs_interop_value_set_int32(Out, 42) == VS_INTEROP_STATUS_OK
	    ? VS_INTEROP_CALLBACK_OK
	    : VS_INTEROP_CALLBACK_ERROR;
}

static bool TestNestedSessionTeardownOrigin()
{
	FNestedTeardownProbe Probe;
	FValueOwner Foreign;
	Probe.Foreign = Foreign.Value;
	vs_interop_native_type_desc Void{sizeof(Void)}, Integer{sizeof(Integer)};
	Void.type = VS_INTEROP_NATIVE_VOID;
	Integer.type = VS_INTEROP_NATIVE_INT32;
	vs_interop_run_result Result{sizeof(Result)};
	if (!Expect(Foreign.Value && vs_interop_session_create(nullptr, &Probe.First) == VS_INTEROP_STATUS_OK && vs_interop_session_create(nullptr, &Probe.Second) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Probe.First, View("on_destroy"), &Void, nullptr, 0, CheckTeardownOrigin, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Probe.Second, View("close_other"), &Integer, nullptr, 0, CloseOtherSession, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_set_hot_reload_enabled(Probe.First, 1) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Probe.First, View("class Item{public:~Item(){on_destroy();}}; "
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             "Item live=Item(); void main(){}"),
	                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                View("teardown-origin-first.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_session_run(Probe.First, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Probe.Second, View("class Item{}; Item make(){return Item();} "
	                                                                                                                                                             "int main(){return close_other();}"),
	                                                                                                               View("teardown-origin-second.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_session_call_function(Probe.Second, View("make"), nullptr, 0, Foreign.Value, &Result) == VS_INTEROP_STATUS_OK,
	        "nested session teardown fixture"))
	{
		return false;
	}
	return Expect(vs_interop_session_run(Probe.Second, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42 && Probe.Destructions == 1 && Probe.OriginChecked && Probe.Restored, "final teardown uses its own session then restores the caller context");
}

static bool TestGovernedJitCache()
{
	const char* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	if (!Path || !*Path) return true;
	for (const auto Backend : {VS_INTEROP_BACKEND_JIT, VS_INTEROP_BACKEND_MIXED})
	{
		vs_interop_session* Raw = nullptr;
		if (!Expect(vs_interop_session_create(nullptr, &Raw) == VS_INTEROP_STATUS_OK,
		        "governed JIT session"))
		{
			return false;
		}
		const auto Destroy = [](vs_interop_session* Session)
		{
			(void)vs_interop_session_destroy(Session);
		};
		const std::unique_ptr<vs_interop_session, decltype(Destroy)> Owner(Raw, Destroy);
		FValueOwner Return;
		vs_interop_jit_backend_info Info{sizeof(Info)};
		if (!Expect(Return.Value && vs_interop_session_set_jit_backend(Raw, View(Path)) == VS_INTEROP_STATUS_OK && vs_interop_session_get_jit_backend_info(Raw, &Info) == VS_INTEROP_STATUS_OK,
		        "governed JIT backend metadata"))
		{
			return false;
		}
		const bool ControlledNative = Info.native_code != 0 && (Info.capabilities & VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION) != 0;
		vs_interop_config Config{sizeof(Config)};
		Config.execution_backend = Backend;
		Config.max_instructions = 1000;
		Config.enable_execution_control = 1;
		vs_interop_jit_policy Policy{sizeof(Policy)};
		Policy.mixed_hot_call_threshold = 1;
		Policy.max_compile_attempts = Policy.max_cached_entries = Policy.eager_compile_budget = 1;
		Policy.reuse_session_code = 1;
		vs_interop_run_result Result{sizeof(Result)};
		if (!Expect(vs_interop_session_apply_config(Raw, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_set_jit_policy(Raw, &Policy) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Raw, View("int main(){return 42;}"), View("governed-c-cache.vs"), &Result) == VS_INTEROP_STATUS_OK,
		        "governed JIT configuration"))
		{
			return false;
		}
		if (Backend == VS_INTEROP_BACKEND_MIXED)
		{
			vs_interop_jit_stats Stats{sizeof(Stats)};
			if (!Expect(vs_interop_session_run(Raw, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42 && vs_interop_session_get_last_jit_stats(Raw, &Stats) == VS_INTEROP_STATUS_OK && Stats.dispatches == 0 && Stats.controlled_native_executions == 0,
			        "governed Mixed stays on VM until explicitly enabled"))
			{
				return false;
			}
		}
		// Explicit JIT must retain its controlled path with the Mixed-only opt-in off.
		Policy.enable_controlled_mixed = Backend == VS_INTEROP_BACKEND_MIXED ? 1 : 0;
		if (!Expect(vs_interop_session_set_jit_policy(Raw, &Policy) == VS_INTEROP_STATUS_OK,
		        "controlled policy follows the selected backend"))
		{
			return false;
		}
		// One compilation attempt must cover both ordinary Run and CallFunction.
		// A governed cache restored without its governance identity recompiles or
		// falls back after exhausting this quota, even though results still match.
		for (int Call = 0; Call < 4; ++Call)
		{
			const bool Run = Call % 2 == 0;
			const auto Status = Run ? vs_interop_session_run(Raw, VS_INTEROP_RUN_SCRIPT, &Result)
			                        : vs_interop_session_call_function(Raw, View("main"), nullptr, 0, Return.Value, &Result);
			int32_t Value = 0;
			if (!Expect(Status == VS_INTEROP_STATUS_OK && Result.success && (Run ? Result.exit_code == 42 : vs_interop_value_get_int32(Return.Value, &Value) == VS_INTEROP_STATUS_OK && Value == 42),
			        "governed repeated execution"))
			{
				return false;
			}
			vs_interop_jit_stats Stats{sizeof(Stats)};
			vs_interop_jit_cache_stats Cache{sizeof(Cache)};
			if (!Expect(vs_interop_session_get_last_jit_stats(Raw, &Stats) == VS_INTEROP_STATUS_OK && vs_interop_session_get_jit_cache_stats(Raw, &Cache) == VS_INTEROP_STATUS_OK,
			        "governed JIT statistics"))
			{
				return false;
			}
			if (ControlledNative && !Expect(Stats.controlled_native_executions == 1 && Stats.compile_attempts == (Call == 0 ? 1u : 0u) && Cache.compile_attempts == 1, "governed cache keeps per-call native entries and its cumulative compilation quota"))
			{
				return false;
			}
			if (!ControlledNative && !Expect(Stats.controlled_native_executions == 0, "backend without controlled native execution retains the VM path"))
			{
				return false;
			}
		}
		Config.max_instructions = 1;
		if (!Expect(vs_interop_session_apply_config(Raw, &Config) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(Return.Value, 99) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Raw, View("main"), nullptr, 0, Return.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success,
		        "replacement instruction budget is enforced after cache reuse"))
		{
			return false;
		}
		int32_t Unchanged = 0;
		if (!Expect(vs_interop_value_get_int32(Return.Value, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 99,
		        "governed failure preserves the return slot"))
		{
			return false;
		}
		Config.max_instructions = 1000;
		if (!Expect(vs_interop_session_apply_config(Raw, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_request_cancel(Raw) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Raw, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && vs_interop_session_apply_config(Raw, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Raw, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42,
		        "controlled cancellation and complete-configuration reset"))
		{
			return false;
		}
		if (Backend == VS_INTEROP_BACKEND_MIXED)
		{
			Policy.enable_controlled_mixed = 0;
			vs_interop_jit_stats Stats{sizeof(Stats)};
			vs_interop_jit_cache_stats Cache{sizeof(Cache)};
			if (!Expect(vs_interop_session_set_jit_policy(Raw, &Policy) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Raw, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.exit_code == 42 && vs_interop_session_get_last_jit_stats(Raw, &Stats) == VS_INTEROP_STATUS_OK && Stats.dispatches == 0 && vs_interop_session_get_jit_cache_stats(Raw, &Cache) == VS_INTEROP_STATUS_OK && Cache.retained_payload_bytes == 0 && Cache.native_bytes == 0,
			        "controlled Mixed opt-out retires native cache resources"))
			{
				return false;
			}
		}
	}
	return true;
}

static bool TestRunModes()
{
	if (!Expect((vs_interop_get_capabilities() & VS_INTEROP_CAP_EVAL_ONCE) != 0 && (vs_interop_get_capabilities() & (VS_INTEROP_CAP_NATIVE_JIT | VS_INTEROP_CAP_NATIVE_TYPED_LOOPS)) == 0,
	        "run-mode discovery is independent of session native capabilities"))
	{
		return false;
	}
	const char* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	for (const bool bSelectBackend : {false, true})
	{
		if (bSelectBackend && (!Path || !*Path))
		{
			continue;
		}
		for (const vs_interop_run_mode Mode :
		    {VS_INTEROP_RUN_EVAL, VS_INTEROP_RUN_SCRIPT, VS_INTEROP_RUN_EVAL_ONCE})
		{
			FOutput Output;
			vs_interop_session_options Options{sizeof(Options)};
			Options.print_callback = OnPrint;
			Options.user_data = &Output;
			vs_interop_session* Session = nullptr;
			if (!Expect(vs_interop_session_create(&Options, &Session) == VS_INTEROP_STATUS_OK, "run-mode session creation"))
			{
				return false;
			}
			const auto Destroy = [](vs_interop_session* Value)
			{
				(void)vs_interop_session_destroy(Value);
			};
			std::unique_ptr<vs_interop_session, decltype(Destroy)> Owner(Session, Destroy);
			vs_interop_config Config{sizeof(Config)};
			Config.execution_backend = bSelectBackend ? VS_INTEROP_BACKEND_JIT : VS_INTEROP_BACKEND_MIXED;
			if (bSelectBackend && !Expect(vs_interop_session_set_jit_backend(Session, View(Path)) == VS_INTEROP_STATUS_OK, "run-mode backend selection"))
			{
				return false;
			}
			vs_interop_run_result Result{sizeof(Result)};
			if (!Expect(vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, View("int value=42; print(value); void main(){print(7);}"), View("interop-run-modes.vs"), &Result) == VS_INTEROP_STATUS_OK && Result.success, "run-mode source loading"))
			{
				return false;
			}
			if (!Expect(vs_interop_session_run(Session, UINT32_C(3), &Result) == VS_INTEROP_STATUS_INVALID_ARGUMENT && Output.Lines.empty(),
			        "unknown run mode rejected before side effects"))
			{
				return false;
			}
			if (!Expect(vs_interop_session_run(Session, Mode, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 0 && Output.Lines == (Mode == VS_INTEROP_RUN_SCRIPT ? std::vector<std::string>{"42", "7"} : std::vector<std::string>{"42"}),
			        "run modes initialize once and only Script invokes main"))
			{
				return false;
			}
			if (bSelectBackend && Mode == VS_INTEROP_RUN_EVAL_ONCE)
			{
				vs_interop_jit_stats Stats{sizeof(Stats)};
				if (!Expect(vs_interop_session_get_last_jit_stats(Session, &Stats) == VS_INTEROP_STATUS_OK && Stats.dispatches > 0,
				        "one-shot initializer itself enters the selected backend"))
				{
					return false;
				}
			}
			if (Mode == VS_INTEROP_RUN_EVAL_ONCE)
			{
				if (!Expect(vs_interop_session_run(Session, Mode, &Result) == VS_INTEROP_STATUS_OK && Result.success && Output.Lines == std::vector<std::string>{"42", "42"},
				        "ordinary one-shot evaluation initializes on every run"))
				{
					return false;
				}
				Output.Lines.clear();
				if (!bSelectBackend)
				{
					Config.execution_backend = VS_INTEROP_BACKEND_JIT;
					if (!Expect(vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Session, Mode, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && Output.Lines.empty(),
					        "one-shot forced JIT without a backend fails before initialization"))
					{
						return false;
					}
					Config.execution_backend = VS_INTEROP_BACKEND_MIXED;
					if (!Expect(vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK,
					        "restore mixed execution"))
					{
						return false;
					}
				}
				if (!Expect(vs_interop_session_set_hot_reload_enabled(Session, 1) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Session, Mode, &Result) == VS_INTEROP_STATUS_OK && Result.success && Output.Lines == std::vector<std::string>{"42"},
				        "persistent one-shot evaluation initializes state"))
				{
					return false;
				}
				vs_interop_jit_stats Stats{sizeof(Stats)};
				if (!Expect(vs_interop_session_get_last_jit_stats(Session, &Stats) == VS_INTEROP_STATUS_OK && Stats.dispatches == 0, "hot reload keeps one-shot evaluation on the VM"))
				{
					return false;
				}
				if (!Expect(vs_interop_session_run(Session, Mode, &Result) == VS_INTEROP_STATUS_OK && Result.success && Output.Lines == std::vector<std::string>{"42"},
				        "persistent one-shot evaluation retains initialized globals"))
				{
					return false;
				}
			}
		}
	}
	return true;
}

static bool TestFloatingPointWriteBack()
{
	const char* BackendPath = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	for (const bool UseJit : {false, true})
	{
		if (UseJit && (!BackendPath || !*BackendPath))
		{
			continue;
		}

		vs_interop_session* Raw = nullptr;
		if (!Expect(vs_interop_session_create(nullptr, &Raw) == VS_INTEROP_STATUS_OK,
		        "floating point write-back session"))
		{
			return false;
		}

		const std::unique_ptr<vs_interop_session, decltype(&vs_interop_session_destroy)> Session(
		    Raw, &vs_interop_session_destroy);
		vs_interop_config Config{sizeof(Config)};
		Config.execution_backend = UseJit ? VS_INTEROP_BACKEND_JIT : VS_INTEROP_BACKEND_VM;
		if ((UseJit && vs_interop_session_set_jit_backend(Raw, View(BackendPath)) != VS_INTEROP_STATUS_OK) ||
		    vs_interop_session_apply_config(Raw, &Config) != VS_INTEROP_STATUS_OK ||
		    vs_interop_session_load_source(Raw,
		        View("class ZeroStorage{public:double doubles[1]; float floats[1];}; "
		             "void FlipDouble(ref double x){x=-x;} "
		             "void FlipFloat(ref float x){x=-x;} "
		             "void FlipDoubles(ref double x[1]){x[0]=-x[0];} "
		             "void FlipFloats(ref float x[1]){x[0]=-x[0];} "
		             "int BackendProbe(){return 42;}"),
		        View("floating-write-back.vs"), nullptr) != VS_INTEROP_STATUS_OK ||
		    vs_interop_session_run(Raw, VS_INTEROP_RUN_EVAL_ONCE, nullptr) != VS_INTEROP_STATUS_OK)
		{
			return Expect(false, "floating point write-back setup");
		}

		for (const bool IsFloat : {false, true})
		{
			for (const bool IsArray : {false, true})
			{
				FValueOwner Argument, Element, Object;
				if (IsArray &&
				    !Expect(vs_interop_session_construct_object(Raw, View("ZeroStorage"), nullptr, 0,
				                Object.Value, nullptr) == VS_INTEROP_STATUS_OK,
				        "floating point native array owner"))
				{
					return false;
				}

				const auto Setup = IsArray
				    ? vs_interop_session_get_field(
				          Raw, Object.Value, View(IsFloat ? "floats" : "doubles"), Argument.Value, nullptr)
				    : IsFloat ? vs_interop_value_set_float(Argument.Value, 0.0f)
				              : vs_interop_value_set_double(Argument.Value, 0.0);
				if (!Expect(Setup == VS_INTEROP_STATUS_OK, "floating point argument setup")) return false;
				vs_interop_value* Arguments[] = {Argument.Value};
				for (const bool Negative : {true, false})
				{
					const char* Function = IsArray ? (IsFloat ? "FlipFloats" : "FlipDoubles")
					    : IsFloat                  ? "FlipFloat"
					                               : "FlipDouble";
					if (!Expect(vs_interop_session_call_function(Raw, View(Function), Arguments, 1, nullptr,
					                nullptr) == VS_INTEROP_STATUS_OK,
					        "floating point ref call"))
					{
						return false;
					}

					if (IsArray &&
					    !Expect(vs_interop_value_sequence_get(Argument.Value, 0, Element.Value) ==
					            VS_INTEROP_STATUS_OK,
					        "floating point native array element"))
					{
						return false;
					}

					const auto* Value = IsArray ? Element.Value : Argument.Value;
					float Single = 1.0f;
					double Double = 1.0;
					const auto Status = IsFloat ? vs_interop_value_get_float(Value, &Single)
					                            : vs_interop_value_get_double(Value, &Double);
					const bool Matches = IsFloat ? Single == 0.0f && std::signbit(Single) == Negative
					                             : Double == 0.0 && std::signbit(Double) == Negative;
					if (!Expect(Status == VS_INTEROP_STATUS_OK && Matches,
					        IsArray ? "native array ref write-back preserves signed zero"
					                : "scalar ref write-back preserves signed zero"))
					{
						return false;
					}
				}
			}
		}
		if (UseJit)
		{
			// Ref calls use Core's VM path even in a JIT-configured session. Check
			// the selected backend separately with an ordinary value-returning call.
			FValueOwner Return;
			if (!Expect(vs_interop_session_call_function(Raw, View("BackendProbe"), nullptr, 0, Return.Value,
			                nullptr) == VS_INTEROP_STATUS_OK,
			        "floating point session backend probe"))
			{
				return false;
			}

			vs_interop_jit_stats Stats{sizeof(Stats)};
			if (!Expect(vs_interop_session_get_last_jit_stats(Raw, &Stats) == VS_INTEROP_STATUS_OK &&
			            Stats.dispatches != 0,
			        "floating point session has an active external JIT"))
			{
				return false;
			}
		}
	}
	return true;
}

int main()
{
	if (!TestFloatingPointWriteBack() || !TestJitBackendSelection() || !TestGovernedJitCache() || !TestRunModes() || !TestStandardLibraryConfigurationLimits() || !TestCrossSessionObjectBoundaries() || !TestNestedSessionTeardownOrigin() || !TestExpiredAggregateOrigin())
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_get_abi_version() == VS_INTEROP_ABI_VERSION, "ABI version"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_get_runtime_version().size != 0, "runtime version"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect((vs_interop_get_capabilities() & VS_INTEROP_CAP_LOCALIZATION) != 0,
	        "localization capability"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect((vs_interop_get_capabilities() & VS_INTEROP_CAP_DISTINCT_NUMERIC_PRIMITIVES) != 0,
	        "distinct numeric primitive capability"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect((vs_interop_get_capabilities() & VS_INTEROP_CAP_EXECUTION_GOVERNANCE) != 0,
	        "execution governance capability"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_locale_set_language(View("zh-CN")) == VS_INTEROP_STATUS_OK && std::string(vs_interop_locale_get_language().data, static_cast<std::size_t>(vs_interop_locale_get_language().size)) == "zh-CN" && vs_interop_locale_set_language(View("en-US")) == VS_INTEROP_STATUS_OK,
	        "locale language selection"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_buffer* AvailableLanguages = nullptr;
	if (!Expect(vs_interop_locale_get_available_languages(&AvailableLanguages) == VS_INTEROP_STATUS_OK && AvailableLanguages != nullptr,
	        "locale language discovery"))
	{
		return EXIT_FAILURE;
	}
	const auto AvailableLanguageBytes = vs_interop_buffer_view(AvailableLanguages);
	const std::string AvailableLanguageJson(
	    reinterpret_cast<const char*>(AvailableLanguageBytes.data),
	    static_cast<std::size_t>(AvailableLanguageBytes.size));
	if (!Expect(AvailableLanguageJson.find("\"en-US\"") != std::string::npos && AvailableLanguageJson.find("\"zh-CN\"") != std::string::npos,
	        "locale language metadata"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_buffer_destroy(AvailableLanguages);
	for (const char* Language : {"en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"})
	{
		vs_interop_buffer* Translated = nullptr;
		if (!Expect(vs_interop_locale_set_language(View(Language)) == VS_INTEROP_STATUS_OK && vs_interop_locale_translate(View("JIT backend loading failed"), &Translated) == VS_INTEROP_STATUS_OK,
		        "translate diagnostic through C ABI"))
		{
			return EXIT_FAILURE;
		}
		const auto Bytes = vs_interop_buffer_view(Translated);
		const std::string Message(reinterpret_cast<const char*>(Bytes.data), static_cast<std::size_t>(Bytes.size));
		vs_interop_buffer_destroy(Translated);
		if (!Expect(!Message.empty() && (std::string(Language) == "en-US" || Message != "JIT backend loading failed"), "C ABI translation is localized"))
		{
			return EXIT_FAILURE;
		}
	}
	(void)vs_interop_locale_set_language(View("en-US"));
	const std::string Unknown("opaque\0message", 14);
	vs_interop_buffer* Translation = nullptr;
	if (!Expect(vs_interop_locale_translate(View(Unknown), &Translation) == VS_INTEROP_STATUS_OK,
	        "unknown diagnostic copy"))
	{
		return EXIT_FAILURE;
	}
	const auto UnknownBytes = vs_interop_buffer_view(Translation);
	const bool Preserved = UnknownBytes.size == Unknown.size() && std::memcmp(UnknownBytes.data, Unknown.data(), Unknown.size()) == 0;
	vs_interop_buffer_destroy(Translation);
	Translation = reinterpret_cast<vs_interop_buffer*>(1);
	if (!Expect(Preserved && vs_interop_locale_translate({nullptr, 1}, &Translation) == VS_INTEROP_STATUS_INVALID_ARGUMENT && Translation == nullptr && vs_interop_locale_translate(View("test"), nullptr) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
	        "translation preserves NUL and clears output for invalid input"))
	{
		return EXIT_FAILURE;
	}

	FOutput Output;
	vs_interop_session_options Options{};
	Options.struct_size = sizeof(Options);
	Options.print_callback = &OnPrint;
	Options.diagnostic_callback = &OnDiagnostic;
	Options.user_data = &Output;
	vs_interop_session* Session = nullptr;
	if (!Expect(vs_interop_session_create(&Options, &Session) == VS_INTEROP_STATUS_OK,
	        "session creation"))
	{
		return EXIT_FAILURE;
	}

	const char* BackendPath = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	if (BackendPath && *BackendPath && !Expect(vs_interop_session_set_jit_backend(Session, View(BackendPath)) == VS_INTEROP_STATUS_OK, "integration backend selection"))
	{
		return EXIT_FAILURE;
	}

	{
		FValueOwner OversizedValue;
		const char Byte = 'x';
		const std::uint16_t WideUnit = u'x';
		const std::uint64_t Oversized =
		    static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) + 1u;
		const vs_interop_value* Elements[] = {OversizedValue.Value};
		if (!Expect(vs_interop_value_set_string(OversizedValue.Value, {&Byte, Oversized}) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_set_wstring(OversizedValue.Value, &WideUnit, Oversized) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_create_array(OversizedValue.Value, View("int"), Elements, Oversized) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
		        "host counts must fit the script int64 length range"))
		{
			return EXIT_FAILURE;
		}
	}

	if (std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max())
	{
		/* The ABI keeps lengths 64-bit on every platform. A 32-bit adapter must
		 * reject unrepresentable ranges before pointer arithmetic or iteration. */
		FValueOwner OversizedValue;
		const char Byte = 'x';
		const std::uint8_t RawByte = 0x78;
		const std::uint16_t WideUnit = u'x';
		const auto Oversized = std::numeric_limits<std::uint64_t>::max();
		const vs_interop_value* ConstElements[] = {OversizedValue.Value};
		vs_interop_value* MutableElements[] = {OversizedValue.Value};
		vs_interop_package* Package = nullptr;
		vs_interop_buffer* PackageError = nullptr;
		if (!Expect(vs_interop_value_set_string(OversizedValue.Value, {&Byte, Oversized}) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_set_wstring(OversizedValue.Value, &WideUnit, Oversized) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_create_array(OversizedValue.Value, View("int"), ConstElements, Oversized) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_call_function(Session, View("unused"), MutableElements, Oversized, nullptr, nullptr) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_package_read({&RawByte, Oversized}, &Package, &PackageError) == VS_INTEROP_STATUS_INVALID_ARGUMENT && Package == nullptr && PackageError == nullptr,
		        "32-bit uint64 length overflow rejection"))
		{
			return EXIT_FAILURE;
		}
	}

	vs_interop_config Config{};
	Config.struct_size = sizeof(Config);
	Config.execution_backend = VS_INTEROP_BACKEND_VM;
	Config.enable_execution_control = 1;
	Config.allow_circular_imports = 1;
	const vs_interop_string_view MacroParameters[] = {View("value")};
	vs_interop_predefined_macro PredefinedMacro{};
	PredefinedMacro.struct_size = sizeof(PredefinedMacro);
	PredefinedMacro.name = View("TWICE");
	PredefinedMacro.parameters = {MacroParameters, 1};
	PredefinedMacro.body = View("((value) + (value))");
	PredefinedMacro.source_name = View("<interop-test>");
	Config.predefined_macros = &PredefinedMacro;
	Config.predefined_macro_count = 1;
	if (!Expect(vs_interop_session_apply_config(Session, &Config) == VS_INTEROP_STATUS_OK,
	        "session configuration"))
	{
		return EXIT_FAILURE;
	}

	vs_interop_native_type_desc VoidType{};
	{
		FOutput MacroOutput;
		auto MacroOptions = Options;
		MacroOptions.user_data = &MacroOutput;
		vs_interop_session* MacroSession = nullptr;
		const vs_interop_source_overlay Overlays[] = {
		    {View("interop-macros/library.vs"),
		        View("#define_export PUBLIC_VALUE 21\n#define PRIVATE_VALUE 99\nexport int "
		             "get(){return PUBLIC_VALUE;}\n")},
		    {View("interop-macros/facade.vs"), View("export import \"library.vs\";\n")}};
		auto MacroConfig = Config;
		MacroConfig.source_overlays = Overlays;
		MacroConfig.source_overlay_count = 2;
		vs_interop_run_result MacroResult{sizeof(MacroResult)};
		const bool OK =
		    vs_interop_session_create(&MacroOptions, &MacroSession) == VS_INTEROP_STATUS_OK &&
		    vs_interop_session_apply_config(MacroSession, &MacroConfig) == VS_INTEROP_STATUS_OK &&
		    vs_interop_session_load_source(
		        MacroSession,
		        View("import \"facade.vs\";\n#ifdef PRIVATE_VALUE\n#error private macro "
		             "leaked\n#endif\nint main(){print(TWICE(PUBLIC_VALUE));return 0;}"),
		        View("interop-macros/main.vs"), &MacroResult) == VS_INTEROP_STATUS_OK &&
		    MacroResult.success &&
		    vs_interop_session_run(MacroSession, VS_INTEROP_RUN_SCRIPT, &MacroResult) ==
		        VS_INTEROP_STATUS_OK &&
		    MacroResult.success && MacroOutput.Lines == std::vector<std::string>{"42"};
		bool LocationsOK = OK;
		for (const auto* Source : {
		         "#define PREFIX ;;;;;;;;;;;;;;;;;\nPREFIX import \"\";\n",
		         "#define PREFIX ;;;;;;;;;;;;;;;;;\nPREFIX import \"bad\\0path.vs\";\n"})
		{
			vs_interop_diagnostic Diagnostic{sizeof(Diagnostic)};
			LocationsOK = LocationsOK &&
			    vs_interop_session_load_source(MacroSession, View(Source),
			        View("interop-macros/invalid.vs"), &MacroResult) == VS_INTEROP_STATUS_ENGINE_ERROR &&
			    !MacroResult.success &&
			    vs_interop_session_get_diagnostic(MacroSession, 0, &Diagnostic) == VS_INTEROP_STATUS_OK &&
			    Diagnostic.line == 2 && Diagnostic.column == 8;
		}
		vs_interop_session_destroy(MacroSession);
		if (!Expect(OK, "C ABI overlay imports expose public macros and preserve private macros"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(LocationsOK, "C ABI preserves physical invalid import path positions"))
		{
			return EXIT_FAILURE;
		}
	}
	VoidType.struct_size = sizeof(VoidType);
	VoidType.type = VS_INTEROP_NATIVE_VOID;
	vs_interop_native_type_desc IntType{};
	IntType.struct_size = sizeof(IntType);
	IntType.type = VS_INTEROP_NATIVE_INT32;
	vs_interop_native_param_desc RefInt{};
	RefInt.struct_size = sizeof(RefInt);
	RefInt.type = IntType;
	RefInt.flags = VS_INTEROP_PARAM_REF;
	vs_interop_native_param_desc ValueInt{};
	ValueInt.struct_size = sizeof(ValueInt);
	ValueInt.type = IntType;
	vs_interop_native_type_desc CharType{};
	CharType.struct_size = sizeof(CharType);
	CharType.type = VS_INTEROP_NATIVE_INT8;
	vs_interop_native_type_desc CharMapArguments[] = {CharType, IntType};
	vs_interop_native_type_desc CharMapType{};
	CharMapType.struct_size = sizeof(CharMapType);
	CharMapType.type = VS_INTEROP_NATIVE_MAP;
	CharMapType.type_arguments = CharMapArguments;
	CharMapType.type_argument_count = 2;
	vs_interop_native_type_desc OrderedCharMapType = CharMapType;
	OrderedCharMapType.type = VS_INTEROP_NATIVE_ORDERED_MAP;
	vs_interop_native_type_desc CharSetType{};
	CharSetType.struct_size = sizeof(CharSetType);
	CharSetType.type = VS_INTEROP_NATIVE_SET;
	CharSetType.type_arguments = &CharType;
	CharSetType.type_argument_count = 1;
	vs_interop_native_param_desc CharContainerParams[3]{};
	for (auto& Param : CharContainerParams)
	{
		Param.struct_size = sizeof(Param);
	}
	CharContainerParams[0].type = CharMapType;
	CharContainerParams[1].type = OrderedCharMapType;
	CharContainerParams[2].type = CharSetType;
	vs_interop_native_type_desc NumericTypes[5]{};
	const vs_interop_native_type NumericKinds[5] = {VS_INTEROP_NATIVE_INT8, VS_INTEROP_NATIVE_UINT8,
	    VS_INTEROP_NATIVE_INT16, VS_INTEROP_NATIVE_UINT16, VS_INTEROP_NATIVE_FLOAT};
	vs_interop_native_param_desc NumericParams[5]{};
	for (std::size_t Index = 0; Index < 5; ++Index)
	{
		NumericTypes[Index].struct_size = sizeof(vs_interop_native_type_desc);
		NumericTypes[Index].type = NumericKinds[Index];
		NumericParams[Index].struct_size = sizeof(vs_interop_native_param_desc);
		NumericParams[Index].type = NumericTypes[Index];
	}

	if (!Expect(vs_interop_session_register_native(Session, View("host_bump"),
	                &VoidType, &RefInt, 1, &HostBump, nullptr) == VS_INTEROP_STATUS_OK,
	        "ref native registration"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_register_native(Session, View("host_nested"),
	                &IntType, &ValueInt, 1, &HostNested, nullptr) == VS_INTEROP_STATUS_OK,
	        "nested native registration"))
	{
		return EXIT_FAILURE;
	}
	FNestedFailureProbe NestedFailureProbe;
	if (!Expect(vs_interop_session_register_native(Session, View("host_nested_failure_boundary"),
	                &VoidType, nullptr, 0, &HostNestedFailureBoundary, &NestedFailureProbe) == VS_INTEROP_STATUS_OK,
	        "nested failure-boundary native registration"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_register_native(Session, View("host_missing_return"),
	                &IntType, nullptr, 0, &HostMissingReturn, nullptr) == VS_INTEROP_STATUS_OK,
	        "missing-return native registration"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_register_native(Session, View("host_numeric"),
	                &NumericTypes[4], NumericParams, 5, &HostNumericPrimitives, nullptr) == VS_INTEROP_STATUS_OK,
	        "numeric primitive native registration"))
	{
		return EXIT_FAILURE;
	}
	bool bSawCharContainers = false;
	if (!Expect(vs_interop_session_register_native(Session, View("host_char_containers"),
	                &VoidType, CharContainerParams, 3, &HostCharContainers, &bSawCharContainers) == VS_INTEROP_STATUS_OK,
	        "char container native registration"))
	{
		return EXIT_FAILURE;
	}
	FReentrantState ReentrantState{Session};
	if (!Expect(vs_interop_session_register_native(Session, View("host_reentrant"),
	                &VoidType, nullptr, 0, &HostRejectStateMutation, &ReentrantState) == VS_INTEROP_STATUS_OK,
	        "reentrant native registration"))
	{
		return EXIT_FAILURE;
	}

	const std::string Source =
	    "int Double(int value) { return TWICE(value); }\n"
	    "int ScriptRef(ref int value) { value += 3; return value; }\n"
	    "int ExitWithoutWriteBack(ref int value) { value = 99; exit(0); return 1; }\n"
	    "int UseHostRef(int value) { host_bump(ref value); return value; }\n"
	    "int UseNested(int value) { return host_nested(value); }\n"
	    "void ExerciseNestedFailureBoundary() { host_nested_failure_boundary(); }\n"
	    "int UseMissingReturn() { return host_missing_return(); }\n"
	    "void ExerciseReentrant() { host_reentrant(); }\n"
	    "float UseNumericPrimitives() { int8 a = -1; uint8 b = 2u; int16 c = -3; uint16 d = 4u; return host_numeric(a, b, c, d, 1.5f); }\n"
	    "char EchoChar(char value) { return value; }\n"
	    "char LiteralChar() { return '\\xFF'; }\n"
	    "string LiteralBytes() { return \"\\x00\\xFF\"; }\n"
	    "int Sum(array<int> values) { int total = 0; for (int value : values) total += value; return total; }\n"
	    "function(int)->int GetCallable() { return Double; }\n"
	    "class Counter {\n"
	    "public:\n"
	    "    int value;\n"
	    "    Counter(int initial) { value = initial; }\n"
	    "    int Add(ref int delta) { value += delta; delta += 1; return value; }\n"
	    "};\n"
	    "struct ConstPayload {\n"
	    "    int value;\n"
	    "    ConstPayload(int initial) { value = initial; }\n"
	    "};\n"
	    "class ConstHolder {\n"
	    "public:\n"
	    "    const int id;\n"
	    "    const ConstPayload payload;\n"
	    "    ConstHolder(int initial) : payload(initial + 1), id(initial) {}\n"
	    "    int Total() const { return id + payload.value; }\n"
	    "};\n"
	    "class ExitCounter { public: ExitCounter() { exit(0); } };\n"
	    "void main() {\n"
	    "    map<char,int> mapped = {'A': 1};\n"
	    "    ordered_map<char,int> ordered = {'B': 2};\n"
	    "    set<char> unique = {'\\xFF'};\n"
	    "    host_char_containers(mapped, ordered, unique);\n"
	    "    print(UseNested(6));\n"
	    "}\n";
	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	if (!Expect(vs_interop_session_load_source(Session, View(Source), View("interop-test.vs"),
	                &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "source load"))
	{
		std::uint64_t Size = 0;
		(void)vs_interop_session_copy_last_error(Session, nullptr, 0, &Size);
		std::string Error(static_cast<std::size_t>(Size), '\0');
		(void)vs_interop_session_copy_last_error(Session, Error.data(), Size, &Size);
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	const std::string EmbeddedNulName("Double\0Other", 12);
	const std::string EmbeddedNulPath("safe.vs\0other.vs", 16);
	if (!Expect(vs_interop_session_call_function(Session, View(EmbeddedNulName), nullptr, 0,
	                nullptr, &Result) == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
	            vs_interop_session_load_file(Session, View(EmbeddedNulPath), &Result) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
	        "embedded NUL rejection for names and paths"))
	{
		return EXIT_FAILURE;
	}

	FValueOwner Argument;
	FValueOwner Return;
	vs_interop_value* Args[] = {Argument.Value};
	FValueOwner I8Value;
	FValueOwner U8Value;
	FValueOwner I16Value;
	FValueOwner U16Value;
	FValueOwner F32Value;
	FValueOwner NumericArray;
	int8_t I8RoundTrip = 0;
	uint8_t U8RoundTrip = 0;
	int16_t I16RoundTrip = 0;
	uint16_t U16RoundTrip = 0;
	float F32RoundTrip = 0.0f;
	if (!Expect(vs_interop_value_set_int8(I8Value.Value, INT8_C(-12)) == VS_INTEROP_STATUS_OK && vs_interop_value_set_uint8(U8Value.Value, UINT8_C(250)) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int16(I16Value.Value, INT16_C(-1234)) == VS_INTEROP_STATUS_OK && vs_interop_value_set_uint16(U16Value.Value, UINT16_C(54321)) == VS_INTEROP_STATUS_OK && vs_interop_value_set_float(F32Value.Value, 2.25f) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int8(I8Value.Value, &I8RoundTrip) == VS_INTEROP_STATUS_OK && vs_interop_value_get_uint8(U8Value.Value, &U8RoundTrip) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int16(I16Value.Value, &I16RoundTrip) == VS_INTEROP_STATUS_OK && vs_interop_value_get_uint16(U16Value.Value, &U16RoundTrip) == VS_INTEROP_STATUS_OK && vs_interop_value_get_float(F32Value.Value, &F32RoundTrip) == VS_INTEROP_STATUS_OK && I8RoundTrip == -12 && U8RoundTrip == 250 && I16RoundTrip == -1234 && U16RoundTrip == 54321 && F32RoundTrip == 2.25f && vs_interop_value_get_kind(I8Value.Value) == VS_INTEROP_VALUE_INT8 && vs_interop_value_get_kind(U8Value.Value) == VS_INTEROP_VALUE_UINT8 && vs_interop_value_get_kind(I16Value.Value) == VS_INTEROP_VALUE_INT16 && vs_interop_value_get_kind(U16Value.Value) == VS_INTEROP_VALUE_UINT16 && vs_interop_value_get_kind(F32Value.Value) == VS_INTEROP_VALUE_FLOAT,
	        "distinct numeric primitive values"))
	{
		return EXIT_FAILURE;
	}
	const vs_interop_value* FloatElements[] = {F32Value.Value};
	if (!Expect(vs_interop_value_create_array(NumericArray.Value, View("float"),
	                FloatElements, 1) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_set_double(Return.Value, 2.25) == VS_INTEROP_STATUS_OK,
	        "float container creation"))
	{
		return EXIT_FAILURE;
	}
	const vs_interop_value* DoubleAsFloat[] = {Return.Value};
	if (!Expect(vs_interop_value_create_array(NumericArray.Value, View("float"),
	                DoubleAsFloat, 1) == VS_INTEROP_STATUS_TYPE_MISMATCH,
	        "float and double container values stay distinct"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_call_function(Session, View("UseNumericPrimitives"),
	                nullptr, 0, Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_float(Return.Value, &F32RoundTrip) == VS_INTEROP_STATUS_OK && F32RoundTrip == 3.5f,
	        "numeric primitive native call round trip"))
	{
		return EXIT_FAILURE;
	}
	int8_t CharacterValue = 0;
	(void)vs_interop_value_set_int8(Argument.Value, INT8_C(-42));
	Result = {sizeof(Result)};
	if (!Expect(
	        vs_interop_session_call_function(
	            Session, View("EchoChar"), Args, 1, Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_kind(Return.Value) == VS_INTEROP_VALUE_INT8 && vs_interop_value_get_int8(Return.Value, &CharacterValue) == VS_INTEROP_STATUS_OK && CharacterValue == -42,
	        "C INT8 interoperates with a script char parameter"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(
	        vs_interop_session_call_function(
	            Session, View("LiteralChar"), nullptr, 0, Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_kind(Return.Value) == VS_INTEROP_VALUE_INT8 && vs_interop_value_get_int8(Return.Value, &CharacterValue) == VS_INTEROP_STATUS_OK && CharacterValue == -1,
	        "script hexadecimal char literal returns stable INT8 bits"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	vs_interop_string_view ScriptBytes{};
	if (!Expect(
	        vs_interop_session_call_function(
	            Session, View("LiteralBytes"), nullptr, 0, Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_string(Return.Value, &ScriptBytes) == VS_INTEROP_STATUS_OK && ScriptBytes.size == 2 && static_cast<unsigned char>(ScriptBytes.data[0]) == 0x00u && static_cast<unsigned char>(ScriptBytes.data[1]) == 0xFFu,
	        "script string escapes preserve embedded NUL and raw bytes through Interop"))
	{
		return EXIT_FAILURE;
	}
	const std::string BinaryString("a\0b", 3);
	vs_interop_string_view BinaryView{};
	if (!Expect(vs_interop_value_set_string(Return.Value, View(BinaryString)) == VS_INTEROP_STATUS_OK && vs_interop_value_get_string(Return.Value, &BinaryView) == VS_INTEROP_STATUS_OK && BinaryView.size == 3 && BinaryView.data[1] == '\0',
	        "length-delimited string preserves embedded NUL"))
	{
		return EXIT_FAILURE;
	}
	(void)vs_interop_value_set_int32(Argument.Value, 4);
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_call_function(Session, View("ScriptRef"), Args, 1,
	                Return.Value, &Result) == VS_INTEROP_STATUS_OK,
	        "host to script ref call"))
	{
		return EXIT_FAILURE;
	}
	int32_t Number = 0;
	int32_t Returned = 0;
	if (!Expect(vs_interop_value_get_int32(Argument.Value, &Number) == VS_INTEROP_STATUS_OK && Number == 7 && vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 7, "host to script ref write-back"))
	{
		return EXIT_FAILURE;
	}
	(void)vs_interop_value_set_int32(Argument.Value, 8);
	(void)vs_interop_value_set_int32(Return.Value, 77);
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_call_function(Session, View("ExitWithoutWriteBack"),
	                Args, 1, Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success && Result.exit_requested && vs_interop_value_get_int32(Argument.Value, &Number) == VS_INTEROP_STATUS_OK && Number == 8 && vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 77,
	        "exit preserves return and ref/out handles"))
	{
		return EXIT_FAILURE;
	}

	(void)vs_interop_value_set_int32(Argument.Value, 5);
	const vs_interop_status HostRefStatus = vs_interop_session_call_function(
	    Session, View("UseHostRef"), Args, 1, Return.Value, &Result);
	const vs_interop_status HostRefValueStatus =
	    vs_interop_value_get_int32(Return.Value, &Returned);
	if (!Expect(HostRefStatus == VS_INTEROP_STATUS_OK && HostRefValueStatus == VS_INTEROP_STATUS_OK && Returned == 15,
	        "script to host ref write-back"))
	{
		std::uint64_t Size = 0;
		(void)vs_interop_session_copy_last_error(Session, nullptr, 0, &Size);
		std::string Error(static_cast<std::size_t>(Size), '\0');
		(void)vs_interop_session_copy_last_error(Session, Error.data(), Size, &Size);
		std::cerr << "status=" << HostRefStatus << " value-status=" << HostRefValueStatus
		          << " return=" << Returned << " error=" << Error << '\n';
		return EXIT_FAILURE;
	}

	(void)vs_interop_value_set_int32(Argument.Value, 9);
	if (!Expect(vs_interop_session_call_function(Session, View("UseNested"), Args, 1,
	                Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 18,
	        "active VM nested invocation"))
	{
		return EXIT_FAILURE;
	}
	const vs_interop_status ExpectedNestedExceptionStatus =
	    std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max()
	    ? VS_INTEROP_STATUS_INVALID_ARGUMENT
	    : VS_INTEROP_STATUS_INTERNAL_ERROR;
	if (!Expect(vs_interop_session_call_function(Session,
	                View("ExerciseNestedFailureBoundary"), nullptr, 0, nullptr, &Result) == VS_INTEROP_STATUS_OK &&
	            NestedFailureProbe.EngineStatus == VS_INTEROP_STATUS_ENGINE_ERROR && NestedFailureProbe.ExceptionStatus == ExpectedNestedExceptionStatus,
	        "nested call failures remain inside the C ABI"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_call_function(Session, View("UseMissingReturn"),
	                nullptr, 0, Return.Value, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR,
	        "missing native return rejection"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_call_function(Session, View("ExerciseReentrant"),
	                nullptr, 0, nullptr, &Result) == VS_INTEROP_STATUS_OK &&
	            ReentrantState.ClearStatus == VS_INTEROP_STATUS_ENGINE_ERROR && ReentrantState.ConfigStatus == VS_INTEROP_STATUS_ENGINE_ERROR,
	        "running session state-mutation rejection"))
	{
		return EXIT_FAILURE;
	}

	FValueOwner Array;
	FValueOwner ArrayElement;
	FValueOwner SecondElement;
	(void)vs_interop_value_set_int32(ArrayElement.Value, 20);
	(void)vs_interop_value_set_int32(SecondElement.Value, 22);
	const vs_interop_value* ArrayElements[] = {ArrayElement.Value, SecondElement.Value};
	if (!Expect(vs_interop_value_create_array(Array.Value, View("int32"), ArrayElements, 2) == VS_INTEROP_STATUS_OK, "array construction"))
	{
		return EXIT_FAILURE;
	}
	std::uint64_t ArrayCount = 0;
	vs_interop_string_view ArrayType{};
	if (!Expect(vs_interop_value_sequence_count(Array.Value, &ArrayCount) == VS_INTEROP_STATUS_OK && ArrayCount == 2 && vs_interop_value_get_type_name(Array.Value, &ArrayType) == VS_INTEROP_STATUS_OK && std::string(ArrayType.data, static_cast<std::size_t>(ArrayType.size)) == "array<int>" && vs_interop_value_sequence_get(Array.Value, 1, ArrayElement.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(ArrayElement.Value, &Number) == VS_INTEROP_STATUS_OK && Number == 22, "array inspection"))
	{
		return EXIT_FAILURE;
	}
	(void)vs_interop_value_set_string(SecondElement.Value, View("wrong"));
	if (!Expect(vs_interop_value_create_array(Array.Value, View("int"), ArrayElements, 2) == VS_INTEROP_STATUS_TYPE_MISMATCH && vs_interop_value_sequence_count(Array.Value, &ArrayCount) == VS_INTEROP_STATUS_OK && ArrayCount == 2,
	        "array element type validation is transactional"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_value_create_array(Array.Value, View("void"), nullptr, 0) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_create_array(Array.Value, View("array<int"), nullptr, 0) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_sequence_count(Array.Value, &ArrayCount) == VS_INTEROP_STATUS_OK && ArrayCount == 2,
	        "array type-name validation is transactional"))
	{
		return EXIT_FAILURE;
	}
	(void)vs_interop_value_set_int32(SecondElement.Value, 22);
	if (!Expect(vs_interop_value_sequence_get(Array.Value, 2, ArrayElement.Value) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_value_sequence_get(SecondElement.Value, 0, ArrayElement.Value) == VS_INTEROP_STATUS_TYPE_MISMATCH,
	        "sequence error classification"))
	{
		return EXIT_FAILURE;
	}
	FValueOwner WideString;
	const std::uint16_t WideUnits[] = {0x4f60, 0x597d, 0x0000, 0xd83d, 0xde00};
	const std::uint16_t* WideView = nullptr;
	std::uint64_t WideSize = 0;
	if (!Expect(vs_interop_value_set_wstring(WideString.Value, WideUnits, 5) == VS_INTEROP_STATUS_OK && vs_interop_value_get_wstring(WideString.Value, &WideView, &WideSize) == VS_INTEROP_STATUS_OK && WideView && WideSize == 5 && std::memcmp(WideView, WideUnits, sizeof(WideUnits)) == 0,
	        "UTF-16 code-unit marshalling without aliasing"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_value_set_wstring(WideString.Value, WideView + 2, 3) == VS_INTEROP_STATUS_OK && vs_interop_value_get_wstring(WideString.Value, &WideView, &WideSize) == VS_INTEROP_STATUS_OK && WideView && WideSize == 3 && std::memcmp(WideView, WideUnits + 2, 3 * sizeof(*WideUnits)) == 0,
	        "UTF-16 assignment from the same value's borrowed subrange"))
	{
		return EXIT_FAILURE;
	}
	FValueOwner HostObject;
	int HostDestroyCount = 0;
	const std::string InvalidHostType("Host\0Type", 9);
	if (!Expect(vs_interop_value_set_host_object(HostObject.Value, 7,
	                View(InvalidHostType), &HostDestroyCount, &DestroyCounter) == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
	            HostDestroyCount == 0,
	        "failed host object assignment preserves caller ownership"))
	{
		return EXIT_FAILURE;
	}
	uint32_t HostTypeId = 0;
	vs_interop_string_view HostTypeName{};
	void* HostPayload = nullptr;
	if (!Expect(vs_interop_value_set_host_object(HostObject.Value, 7,
	                View("HostType"), &HostDestroyCount, &DestroyCounter) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_host_object(HostObject.Value, &HostTypeId,
	                &HostTypeName, &HostPayload) == VS_INTEROP_STATUS_OK &&
	            HostTypeId == 7 && HostPayload == &HostDestroyCount && vs_interop_value_reset(HostObject.Value) == VS_INTEROP_STATUS_OK && HostDestroyCount == 1,
	        "host object transactional ownership"))
	{
		return EXIT_FAILURE;
	}
	FValueOwner Null;
	uint8_t IsNull = 0;
	if (!Expect(vs_interop_value_set_null(Null.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_is_null(Null.Value, &IsNull) == VS_INTEROP_STATUS_OK && IsNull && vs_interop_value_get_kind(Null.Value) == VS_INTEROP_VALUE_NULL,
	        "explicit null value ABI"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_value* ArrayArgs[] = {Array.Value};
	if (!Expect(vs_interop_session_call_function(Session, View("Sum"), ArrayArgs, 1,
	                Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 42,
	        "array argument marshalling"))
	{
		return EXIT_FAILURE;
	}

	FValueOwner Object;
	(void)vs_interop_value_set_int32(Object.Value, 123);
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_construct_object(Session, View("ExitCounter"), nullptr, 0,
	                Object.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success && Result.exit_requested && vs_interop_value_get_int32(Object.Value, &Number) == VS_INTEROP_STATUS_OK && Number == 123,
	        "constructor exit preserves output handle"))
	{
		return EXIT_FAILURE;
	}
	(void)vs_interop_value_set_int32(Argument.Value, 7);
	if (!Expect(vs_interop_session_construct_object(Session, View("Counter"), Args, 1,
	                Object.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_kind(Object.Value) == VS_INTEROP_VALUE_OBJECT,
	        "object construction"))
	{
		return EXIT_FAILURE;
	}
	(void)vs_interop_value_set_int32(Argument.Value, 3);
	if (!Expect(vs_interop_session_call_method(Session, Object.Value, View("Add"), Args, 1,
	                Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_int32(Argument.Value, &Number) == VS_INTEROP_STATUS_OK && Number == 4 && vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 10,
	        "method call and ref write-back"))
	{
		return EXIT_FAILURE;
	}
	FValueOwner Field;
	if (!Expect(vs_interop_session_get_field(Session, Object.Value, View("value"), Field.Value,
	                &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_int32(Field.Value, &Number) == VS_INTEROP_STATUS_OK && Number == 10,
	        "field read"))
	{
		return EXIT_FAILURE;
	}

	FValueOwner ConstObject;
	(void)vs_interop_value_set_int32(Argument.Value, 7);
	if (!Expect(vs_interop_session_construct_object(Session, View("ConstHolder"), Args, 1,
	                ConstObject.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_kind(ConstObject.Value) == VS_INTEROP_VALUE_OBJECT,
	        "const-field constructor initialization through Interop"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_get_field(Session, ConstObject.Value, View("id"), Field.Value,
	                &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_int32(Field.Value, &Number) == VS_INTEROP_STATUS_OK && Number == 7,
	        "Interop reads constructor-initialized const scalar field"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_call_method(Session, ConstObject.Value, View("Total"),
	                nullptr, 0, Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 15,
	        "Interop observes constructor-initialized const object field"))
	{
		return EXIT_FAILURE;
	}

	FValueOwner Function;
	if (!Expect(vs_interop_session_call_function(Session, View("GetCallable"), nullptr, 0,
	                Function.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_kind(Function.Value) == VS_INTEROP_VALUE_FUNCTION,
	        "function value return"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_callable* Callable = nullptr;
	if (!Expect(vs_interop_session_retain_callable(Session, Function.Value, 1, 0, &Callable) == VS_INTEROP_STATUS_OK && vs_interop_callable_is_valid(Callable),
	        "retain callable"))
	{
		return EXIT_FAILURE;
	}
	uint8_t WrongThreadCallableValid = 1;
	std::thread WrongCallableThread([&]()
	{
		WrongThreadCallableValid = vs_interop_callable_is_valid(Callable);
	});
	WrongCallableThread.join();
	if (!Expect(WrongThreadCallableValid == 0 && vs_interop_callable_is_valid(Callable),
	        "owner-thread callable validity check"))
	{
		return EXIT_FAILURE;
	}
	(void)vs_interop_value_set_int32(Argument.Value, 11);
	if (!Expect(vs_interop_callable_invoke(Callable, Args, 1, Return.Value, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 22, "retained callable invocation"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_callable_destroy(Callable);

	vs_interop_callable* CrossThreadCallable = nullptr;
	if (!Expect(vs_interop_session_retain_callable(
	                Session, Function.Value, 0, 0, &CrossThreadCallable) == VS_INTEROP_STATUS_OK,
	        "cross-thread callable retention"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_status CrossThreadStatus = VS_INTEROP_STATUS_INTERNAL_ERROR;
	int32_t CrossThreadReturn = 0;
	std::thread CallableThread([&]()
	{
		FValueOwner ThreadArgument;
		FValueOwner ThreadReturn;
		(void)vs_interop_value_set_int32(ThreadArgument.Value, 13);
		vs_interop_value* ThreadArgs[] = {ThreadArgument.Value};
		vs_interop_run_result ThreadResult{};
		ThreadResult.struct_size = sizeof(ThreadResult);
		CrossThreadStatus = vs_interop_callable_invoke(CrossThreadCallable,
		    ThreadArgs, 1, ThreadReturn.Value, &ThreadResult);
		(void)vs_interop_value_get_int32(ThreadReturn.Value, &CrossThreadReturn);
	});
	CallableThread.join();
	if (!Expect(CrossThreadStatus == VS_INTEROP_STATUS_OK && CrossThreadReturn == 26,
	        "serialized cross-thread callable invocation"))
	{
		char Error[1024]{};
		uint64_t Required = 0;
		(void)vs_interop_session_copy_last_error(Session, Error, sizeof(Error), &Required);
		std::cerr << "callable status=" << CrossThreadStatus << " return=" << CrossThreadReturn
		          << " error=" << Error << '\n';
		return EXIT_FAILURE;
	}
	// Keep this weak session wrapper alive to verify the expired-session result
	// after destruction below. The core may invalidate its capture on reload.

	if (!Expect(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && bSawCharContainers && !Output.Lines.empty() && Output.Lines.back() == "12",
	        "run and output callback"))
	{
		return EXIT_FAILURE;
	}

	vs_interop_status WrongThreadStatus = VS_INTEROP_STATUS_OK;
	std::thread WrongThread([&]()
	{
		vs_interop_run_result ThreadResult{};
		ThreadResult.struct_size = sizeof(ThreadResult);
		WrongThreadStatus = vs_interop_session_run(
		    Session, VS_INTEROP_RUN_SCRIPT, &ThreadResult);
	});
	WrongThread.join();
	if (!Expect(WrongThreadStatus == VS_INTEROP_STATUS_WRONG_THREAD,
	        "creator-thread enforcement"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_status WrongThreadDestroyStatus = VS_INTEROP_STATUS_OK;
	std::thread WrongDestroyThread([&]()
	{
		WrongThreadDestroyStatus = vs_interop_session_destroy(Session);
	});
	WrongDestroyThread.join();
	if (!Expect(WrongThreadDestroyStatus == VS_INTEROP_STATUS_WRONG_THREAD && vs_interop_session_is_loaded(Session),
	        "wrong-thread destruction preserves session"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_callable* FinalizerCallable = nullptr;
	if (!Expect(vs_interop_session_retain_callable(
	                Session, Function.Value, 1, 0, &FinalizerCallable) == VS_INTEROP_STATUS_OK,
	        "finalizer callable retention"))
	{
		return EXIT_FAILURE;
	}
	std::thread ForeignFinalizer([&]()
	{
		vs_interop_callable_destroy(FinalizerCallable);
	});
	ForeignFinalizer.join();
	(void)vs_interop_value_set_int32(Argument.Value, 2);
	if (!Expect(vs_interop_session_call_function(Session, View("Double"), Args, 1,
	                Return.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 4,
	        "foreign finalizer does not disturb owner session"))
	{
		return EXIT_FAILURE;
	}

	const std::string IntMainSource = "int main() { return 7; }";
	// Module-dependent owned snapshots must be retired before replacement.
	if (!Expect(vs_interop_value_reset(Object.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(ConstObject.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(Function.Value) == VS_INTEROP_STATUS_OK, "retire module snapshots before replacement"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(Session, View(IntMainSource),
	                View("int-main.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "int main source load"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 7 && !Result.exit_requested,
	        "int main process exit code"))
	{
		return EXIT_FAILURE;
	}

	const std::string ExceptionSource = R"(
int main()
{
	int result = 0;
	assert(.5f < 1.f, "interop leading-dot float assertion");
	try
	{
		throw 7;
	}
	catch (const int& value)
	{
		result = value;
	}
	finally
	{
		result += 2;
	}
	return result;
}
)";
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(Session, View(ExceptionSource),
	                View("exception.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "exception source load"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 9,
	        "caught exception should cross the public C ABI safely"))
	{
		return EXIT_FAILURE;
	}

	const std::string UncaughtExceptionSource =
	    "void main() { throw \"uncaught\"; }";
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(Session, View(UncaughtExceptionSource),
	                View("uncaught-exception.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "uncaught exception source load"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success,
	        "uncaught script exception must become an ABI error"))
	{
		return EXIT_FAILURE;
	}

	const std::string AssertFailureSource =
	    "void main() { assert(false, \"interop assertion detail\"); }";
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(Session, View(AssertFailureSource),
	                View("assert-message.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "assert message source load"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success,
	        "failed assert must become an ABI error"))
	{
		return EXIT_FAILURE;
	}
	std::uint64_t AssertErrorSize = 0;
	(void)vs_interop_session_copy_last_error(Session, nullptr, 0, &AssertErrorSize);
	std::string AssertError(static_cast<std::size_t>(AssertErrorSize), '\0');
	(void)vs_interop_session_copy_last_error(
	    Session, AssertError.data(), AssertErrorSize, &AssertErrorSize);
	if (!Expect(AssertError.find("interop assertion detail") != std::string::npos,
	        "assert message must survive the public C ABI error boundary"))
	{
		return EXIT_FAILURE;
	}

	const std::string BadSource = "void main() { MissingCall(); }";
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(Session, View(BadSource), View("bad.vs"), &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && vs_interop_session_diagnostic_count(Session) != 0, "diagnostic preservation"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_diagnostic Diagnostic{};
	Diagnostic.struct_size = sizeof(Diagnostic);
	if (!Expect(vs_interop_session_get_diagnostic(Session, 0, &Diagnostic) == VS_INTEROP_STATUS_OK && Diagnostic.message.size != 0,
	        "diagnostic query"))
	{
		return EXIT_FAILURE;
	}
	uint64_t DiagnosticArgumentCount = 0;
	vs_interop_string_view DiagnosticArgument{};
	if (!Expect(vs_interop_session_diagnostic_argument_count(
	                Session, 0, &DiagnosticArgumentCount) == VS_INTEROP_STATUS_OK &&
	            DiagnosticArgumentCount == 1 && vs_interop_session_get_diagnostic_argument(Session, 0, 0, &DiagnosticArgument) == VS_INTEROP_STATUS_OK && std::string(DiagnosticArgument.data, static_cast<std::size_t>(DiagnosticArgument.size)) == "MissingCall",
	        "structured diagnostic arguments"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_buffer* FormattedDiagnostic = nullptr;
	if (!Expect(vs_interop_locale_set_language(View("zh-CN")) == VS_INTEROP_STATUS_OK && vs_interop_session_format_diagnostic(Session, 0, &FormattedDiagnostic) == VS_INTEROP_STATUS_OK && vs_interop_buffer_view(FormattedDiagnostic).size != 0 && vs_interop_locale_set_language(View("en-US")) == VS_INTEROP_STATUS_OK,
	        "localized diagnostic formatting"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_buffer_destroy(FormattedDiagnostic);

	const std::string ErrorDirectiveSource =
	    "#define DETAIL must_not_expand\n#error interop publication stop: DETAIL\n";
	Result = {sizeof(Result)};
	if (!Expect(
	        vs_interop_session_load_source(
	            Session, View(ErrorDirectiveSource), View("interop-error.vs"), &Result) == VS_INTEROP_STATUS_ENGINE_ERROR &&
	            !Result.success,
	        "active #error must cross the public C ABI as a compile failure"))
	{
		return EXIT_FAILURE;
	}
	std::uint64_t ErrorDirectiveSize = 0;
	(void)vs_interop_session_copy_last_error(
	    Session, nullptr, 0, &ErrorDirectiveSize);
	std::string ErrorDirectiveText(
	    static_cast<std::size_t>(ErrorDirectiveSize), '\0');
	(void)vs_interop_session_copy_last_error(
	    Session,
	    ErrorDirectiveText.data(),
	    ErrorDirectiveSize,
	    &ErrorDirectiveSize);
	if (!Expect(
	        ErrorDirectiveText.find("interop publication stop: DETAIL") != std::string::npos && ErrorDirectiveText.find("must_not_expand") == std::string::npos,
	        "#error payload and no-expansion contract must survive the C ABI"))
	{
		return EXIT_FAILURE;
	}

	/* Rejection must be transactional: a bad trailing configuration section may
	 * not disconnect request_cancel from the execution-control token installed in
	 * the Session by the previous successful configuration. */
	vs_interop_config InvalidConfig = Config;
	InvalidConfig.enable_execution_control = 0;
	InvalidConfig.source_overlay_count = 1;
	InvalidConfig.source_overlays = nullptr;
	if (!Expect(vs_interop_session_apply_config(Session, &InvalidConfig) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
	        "invalid configuration should be rejected transactionally"))
	{
		return EXIT_FAILURE;
	}

	const std::string CancellationSource = "void main() { print(1); }";
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(
	                Session, View(CancellationSource), View("cancel.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "cancellation test module load"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_request_cancel(Session) == VS_INTEROP_STATUS_OK,
	        "interop execution cancellation request"))
	{
		return EXIT_FAILURE;
	}
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success,
	        "interop execution cancellation stops a configured session"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK,
	        "session destruction"))
	{
		vs_interop_callable_destroy(CrossThreadCallable);
		return EXIT_FAILURE;
	}
	Result.success = Result.exit_requested = 1;
	Result.reserved_u8[0] = Result.reserved_u8[1] = 255;
	Result.exit_code = 99;
	Result.diagnostic_count = 123;
	(void)vs_interop_value_set_int32(Return.Value, 99);
	const auto ExpiredStatus = vs_interop_callable_invoke(
	    CrossThreadCallable, Args, 1, Return.Value, &Result);
	vs_interop_callable_destroy(CrossThreadCallable);
	if (!Expect(ExpiredStatus == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && !Result.exit_requested && Result.exit_code != 0 && !Result.reserved_u8[0] && !Result.reserved_u8[1] && Result.diagnostic_count == 0 && vs_interop_value_get_int32(Return.Value, &Returned) == VS_INTEROP_STATUS_OK && Returned == 99,
	        "expired callable reports failure and preserves the return value"))
	{
		return EXIT_FAILURE;
	}

	/* A native registered through the language-neutral ABI must remain callable
	 * from an isolated VM worker. Foreign adapters use this path to enter their
	 * own runtime thread context (for example a GIL) before touching host state. */
	FOutput WorkerOutput;
	vs_interop_session_options WorkerOptions{};
	WorkerOptions.struct_size = sizeof(WorkerOptions);
	WorkerOptions.print_callback = &OnPrint;
	WorkerOptions.diagnostic_callback = &OnDiagnostic;
	WorkerOptions.user_data = &WorkerOutput;
	vs_interop_session* WorkerSession = nullptr;
	if (!Expect(vs_interop_session_create(&WorkerOptions, &WorkerSession) == VS_INTEROP_STATUS_OK, "worker callback session creation"))
	{
		return EXIT_FAILURE;
	}
	const std::string StdlibPath = VEROSCRIPT_STDLIB_DIR;
	const vs_interop_string_view StdlibPaths[] = {View(StdlibPath)};
	vs_interop_config WorkerConfig{};
	WorkerConfig.struct_size = sizeof(WorkerConfig);
	WorkerConfig.execution_backend = VS_INTEROP_BACKEND_VM;
	WorkerConfig.stdlib_search_paths = {StdlibPaths, 1};
	if (!Expect(vs_interop_session_apply_config(WorkerSession, &WorkerConfig) == VS_INTEROP_STATUS_OK, "worker callback session configuration"))
	{
		return EXIT_FAILURE;
	}
	FWorkerCallbackProbe WorkerProbe;
	WorkerProbe.OwnerThread = std::this_thread::get_id();
	FValueOwner WorkerExternal;
	WorkerProbe.ExternalStorage = WorkerExternal.Value;
	vs_interop_host_type_desc WorkerHost{sizeof(WorkerHost)};
	WorkerHost.name = View("WorkerHostValue");
	WorkerHost.kind = VS_INTEROP_HOST_CLASS;
	vs_interop_host_type_desc WorkerEnum{sizeof(WorkerEnum)};
	WorkerEnum.name = View("WorkerHostEnum");
	WorkerEnum.kind = VS_INTEROP_HOST_ENUM;
	std::uint32_t WorkerTypeId = 0;
	vs_interop_native_param_desc WorkerArrayParams[3]{};
	const char* WorkerArrayNames[] = {"variant", "WorkerHostValue", "WorkerHostEnum"};
	for (std::size_t Index = 0; Index != 3; ++Index)
	{
		WorkerArrayParams[Index].struct_size = sizeof(WorkerArrayParams[Index]);
		WorkerArrayParams[Index].type.struct_size = sizeof(WorkerArrayParams[Index].type);
		WorkerArrayParams[Index].type.type = VS_INTEROP_NATIVE_ARRAY;
		WorkerArrayParams[Index].type.name = View(WorkerArrayNames[Index]);
	}
	vs_interop_native_param_desc WorkerBoxParams[3]{};
	for (auto& Parameter : WorkerBoxParams)
	{
		Parameter.struct_size = sizeof(Parameter);
		Parameter.type.struct_size = sizeof(Parameter.type);
		Parameter.type.type = VS_INTEROP_NATIVE_VARIANT;
	}
	if (!Expect(vs_interop_session_register_native(
	                WorkerSession, View("host_worker_probe"), &IntType, &ValueInt, 1,
	                &HostWorkerProbe, &WorkerProbe) == VS_INTEROP_STATUS_OK,
	        "worker callback native registration") ||
	    !Expect(vs_interop_value_create_array(WorkerExternal.Value, View("variant"), nullptr, 0) == VS_INTEROP_STATUS_OK, "unbound C variant storage creation") || !Expect(vs_interop_session_register_host_type(WorkerSession, &WorkerHost, &WorkerTypeId) == VS_INTEROP_STATUS_OK && vs_interop_session_register_host_type(WorkerSession, &WorkerEnum, &WorkerTypeId) == VS_INTEROP_STATUS_OK, "worker independent host and enum registrations") || !Expect(vs_interop_session_register_native(WorkerSession, View("host_worker_capture"), &VoidType, WorkerArrayParams, 3, &HostWorkerCapture, &WorkerProbe) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(WorkerSession, View("host_worker_empty"), &WorkerArrayParams[0].type, nullptr, 0, &HostWorkerEmpty, &WorkerProbe) == VS_INTEROP_STATUS_OK, "worker container native registrations") || !Expect(vs_interop_session_register_native(WorkerSession, View("host_worker_capture_box"), &VoidType, WorkerBoxParams, 3, &HostWorkerCaptureBox, &WorkerProbe) == VS_INTEROP_STATUS_OK, "worker boxed container native registration") || !Expect(vs_interop_session_register_native(WorkerSession, View("host_worker_capture_map"), &WorkerBoxParams[0].type, WorkerBoxParams, 3, &HostWorkerCaptureMap, &WorkerProbe) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(WorkerSession, View("host_worker_destroyed"), &VoidType, nullptr, 0, &HostWorkerDestroyed, &WorkerProbe) == VS_INTEROP_STATUS_OK, "worker map view and destructor native registrations"))
	{
		return EXIT_FAILURE;
	}
	const std::string WorkerSource =
	    "import \"concurrent.vs\";\n"
	    "class WorkerItem { public: int value = 7; ~WorkerItem() { host_worker_destroyed(); } };\n"
	    "enum class WorkerScriptEnum { Ready };\n"
	    "export void interop_worker(channel<int> output) {\n"
	    "    array<variant> items = array<variant>();\n"
	    "    host_worker_capture(items, array<WorkerHostValue>(), array<WorkerHostEnum>());\n"
	    "    items.push(WorkerItem());\n"
	    "    array<WorkerItem> boxed_items = array<WorkerItem>();\n"
	    "    host_worker_capture_box(boxed_items, array<WorkerHostValue>(), array<WorkerScriptEnum>());\n"
	    "    boxed_items.push(WorkerItem());\n"
	    "    array<WorkerItem> mapped_items = array<WorkerItem>();\n"
	    "    map<int,array<WorkerItem>> mapped = map<int,array<WorkerItem>>(); mapped[1] = mapped_items;\n"
	    "    map<int,array<WorkerHostValue>> hosts = map<int,array<WorkerHostValue>>(); hosts[1] = array<WorkerHostValue>();\n"
	    "    map<int,array<WorkerScriptEnum>> enums = map<int,array<WorkerScriptEnum>>(); enums[1] = array<WorkerScriptEnum>();\n"
	    "    host_worker_capture_map(mapped, hosts, enums);\n"
	    "    mapped_items.push(WorkerItem());\n"
	    "    array<variant> injected = host_worker_empty();\n"
	    "    injected.push(WorkerItem());\n"
	    "    output.send(host_worker_probe(41));\n"
	    "}\n"
	    "void main() {\n"
	    "    task<int> work = std::concurrent::spawn_task<int>(\"interop_worker\");\n"
	    "    print(work.await());\n"
	    "}\n";
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(
	                WorkerSession, View(WorkerSource), View("interop-worker.vs"), &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "worker callback source load") ||
	    !Expect(vs_interop_session_run(
	                WorkerSession, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "worker callback run") ||
	    !Expect(WorkerOutput.Lines == std::vector<std::string>{"42"} && WorkerProbe.Calls.load(std::memory_order_relaxed) == 1 && WorkerProbe.RanOffOwner.load(std::memory_order_relaxed),
	        "worker callback executes through C ABI off the owner thread") ||
	    !Expect(WorkerProbe.CaptureCalls.load(std::memory_order_relaxed) == 1 && WorkerProbe.VariantClone.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_ENGINE_ERROR && WorkerProbe.HostClone.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_OK && WorkerProbe.EnumClone.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_OK && WorkerProbe.Injection.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_ENGINE_ERROR,
	        "worker rejects external module-capable aliases and preserves host/enum portability") ||
	    !Expect(WorkerProbe.BoxCalls.load(std::memory_order_relaxed) == 1 && WorkerProbe.VariantExtract.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_ENGINE_ERROR && WorkerProbe.HostExtract.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_OK && WorkerProbe.EnumExtract.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_OK && WorkerProbe.ExtractionPreserved.load(std::memory_order_relaxed),
	        "worker extraction rejects empty script-class aliases and preserves independent boxed host/enum values") ||
	    !Expect(WorkerProbe.MapCalls.load(std::memory_order_relaxed) == 1 && WorkerProbe.MapExtract.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_ENGINE_ERROR && WorkerProbe.HostMapExtract.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_OK && WorkerProbe.EnumMapExtract.load(std::memory_order_relaxed) == VS_INTEROP_STATUS_OK && WorkerProbe.MapExtractionPreserved.load(std::memory_order_relaxed) && WorkerProbe.Destructors.load(std::memory_order_relaxed) == 4,
	        "worker map views preserve module boundaries and exact script destruction") ||
	    !Expect(vs_interop_session_destroy(WorkerSession) == VS_INTEROP_STATUS_OK,
	        "worker callback session destruction"))
	{
		return EXIT_FAILURE;
	}

	/* Destroying the public handle from a callback must not tear down the active
	 * session until the ABI entry returns. Foreign runtimes commonly release
	 * wrappers from callbacks/finalizers, so the implementation pins shared state. */
	FDestroyOnPrint DestroyState;
	vs_interop_session_options DestroyOptions{};
	DestroyOptions.struct_size = sizeof(DestroyOptions);
	DestroyOptions.print_callback = &DestroySessionOnPrint;
	DestroyOptions.user_data = &DestroyState;
	if (!Expect(vs_interop_session_create(&DestroyOptions, &DestroyState.Session) == VS_INTEROP_STATUS_OK, "callback-destroy session creation"))
	{
		return EXIT_FAILURE;
	}
	const std::string DestroySource = "void main() { print(1); }";
	Result = {sizeof(Result)};
	if (!Expect(vs_interop_session_load_source(DestroyState.Session, View(DestroySource),
	                View("destroy.vs"), &Result) == VS_INTEROP_STATUS_OK,
	        "callback-destroy source load"))
	{
		return EXIT_FAILURE;
	}
	vs_interop_session* RunningSession = DestroyState.Session;
	if (!Expect(vs_interop_session_run(RunningSession, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && DestroyState.Called && DestroyState.Session == nullptr,
	        "session lifetime pin during callback"))
	{
		return EXIT_FAILURE;
	}

	std::cout << "VeroScriptInterop tests passed\n";
	return EXIT_SUCCESS;
}
