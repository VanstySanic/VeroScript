// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace
{
	vs_interop_string_view Text(const char* Value)
	{
		return {Value, std::strlen(Value)};
	}
	std::string Copy(vs_interop_string_view Value)
	{
		return {Value.data ? Value.data : "", static_cast<std::size_t>(Value.size)};
	}
	bool SameInfo(const vs_interop_vm_backend_info& Left, const vs_interop_vm_backend_info& Right)
	{
		const auto SameView = [](vs_interop_string_view A, vs_interop_string_view B)
		{
			return A.data == B.data && A.size == B.size;
		};
		return Left.struct_size == Right.struct_size && Left.flags == Right.flags && Left.capabilities == Right.capabilities && SameView(Left.id, Right.id) && SameView(Left.version, Right.version) && SameView(Left.target, Right.target) && SameView(Left.library_path, Right.library_path);
	}
	bool Check(bool Okay, const char* Stage)
	{
		if (!Okay)
		{
			std::cerr << "FAILED: " << Stage << '\n';
		}
		return Okay;
	}
	struct FSessionDeleter
	{
		void operator()(vs_interop_session* Session) const
		{
			(void)vs_interop_session_destroy(Session);
		}
	};
	using FSession = std::unique_ptr<vs_interop_session, FSessionDeleter>;
	FSession CreateSession()
	{
		vs_interop_session* Session = nullptr;
		(void)vs_interop_session_create(nullptr, &Session);
		return FSession(Session);
	}
	constexpr uint64_t Capabilities = VS_INTEROP_VM_CAP_BYTECODE | VS_INTEROP_VM_CAP_GOVERNANCE | VS_INTEROP_VM_CAP_DEBUG | VS_INTEROP_VM_CAP_HOT_RELOAD | VS_INTEROP_VM_CAP_CLEANUP;

	bool TestMetadataAndFailedSelection()
	{
		auto Session = CreateSession();
		vs_interop_vm_backend_info Info{sizeof(Info)};
		if (!Check(Session && vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK,
		        "VM metadata"))
		{
			return false;
		}
		#if VEROSCRIPT_INTEROP_TEST_DEFAULT_VM
		if (!Check(Info.flags == VS_INTEROP_VM_SELECTED && Copy(Info.id) == "vs-bytecode" && !Copy(Info.version).empty() && !Copy(Info.target).empty() && Info.library_path.size == 0 && Info.capabilities == Capabilities, "assembled static VM metadata"))
		{
			return false;
		}
		#else
		if (!Check(Info.flags == 0 && Copy(Info.id) == "none" && Info.capabilities == 0,
		        "custom product without a default VM"))
		{
			return false;
		}
		#endif
		if (!Check(vs_interop_session_set_jit_backend(Session.get(), Text("none")) == VS_INTEROP_STATUS_OK && vs_interop_session_set_jit_backend(Session.get(), Text("auto")) == VS_INTEROP_STATUS_OK,
		        "optional automatic JIT and explicit disable tokens"))
		{
			return false;
		}
		#if VEROSCRIPT_INTEROP_TEST_DEFAULT_VM
		if (!Check(vs_interop_session_set_vm_backend(Session.get(), Text("none")) == VS_INTEROP_STATUS_OK && vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK && Info.flags == 0 && vs_interop_session_set_vm_backend(Session.get(), Text("auto")) == VS_INTEROP_STATUS_OK && vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK && Info.flags == VS_INTEROP_VM_SELECTED, "automatic VM restores the product fallback"))
		{
			return false;
		}
		#endif
		Info.struct_size = sizeof(Info) - 1;
		const auto ShortInfo = Info;
		if (!Check(vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_INVALID_ARGUMENT && SameInfo(Info, ShortInfo) && vs_interop_session_get_vm_backend_info(nullptr, &Info) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_get_vm_backend_info(Session.get(), nullptr) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
		        "invalid VM metadata output is preserved"))
		{
			return false;
		}
		Info.struct_size = sizeof(Info);
		const auto Before = Info;
		vs_interop_status QueryStatus = VS_INTEROP_STATUS_OK, SetStatus = VS_INTEROP_STATUS_OK;
		std::thread Foreign([&]
		{
			QueryStatus = vs_interop_session_get_vm_backend_info(Session.get(), &Info);
			SetStatus = vs_interop_session_set_vm_backend(Session.get(), {});
		});
		Foreign.join();
		if (!Check(QueryStatus == VS_INTEROP_STATUS_WRONG_THREAD && SetStatus == VS_INTEROP_STATUS_WRONG_THREAD && SameInfo(Info, Before), "VM creator-thread checks preserve output"))
		{
			return false;
		}
		const char EmbeddedNull[] = {'a', '\0', 'b'};
		if (!Check(vs_interop_session_set_vm_backend(Session.get(), {nullptr, 1}) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_set_vm_backend(Session.get(), {EmbeddedNull, sizeof(EmbeddedNull)}) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_set_vm_backend(Session.get(), Text("missing-interop-vm-backend")) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK && Info.flags == Before.flags && Copy(Info.id) == Copy(Before.id) && Copy(Info.library_path) == Copy(Before.library_path), "failed VM loads preserve selection and views"))
		{
			return false;
		}
		return true;
	}

	bool TestClearAndRetry(const char* Path)
	{
		auto Session = CreateSession();
		vs_interop_vm_backend_info Info{sizeof(Info)};
		vs_interop_run_result Result{sizeof(Result)};
		if (!Check(Session && vs_interop_session_set_vm_backend(Session.get(), {}) == VS_INTEROP_STATUS_OK && vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK && Info.flags == 0 && Copy(Info.id) == "none" && Info.capabilities == 0 && vs_interop_session_load_source(Session.get(), Text("int answer(){return 42;}"), Text("vm-retry.vs"), &Result) == VS_INTEROP_STATUS_OK && Result.success, "clear VM before execution"))
		{
			return false;
		}
		vs_interop_value* Raw = nullptr;
		if (!Check(vs_interop_value_create(&Raw) == VS_INTEROP_STATUS_OK, "retry output")) return false;
		const std::unique_ptr<vs_interop_value, decltype(&vs_interop_value_destroy)> Value(Raw, vs_interop_value_destroy);
		int32_t Number = 0;
		if (!Check(vs_interop_value_set_int32(Raw, 17) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Session.get(), Text("answer"), nullptr, 0, Raw, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && Result.diagnostic_count != 0 && vs_interop_value_get_int32(Raw, &Number) == VS_INTEROP_STATUS_OK && Number == 17 && vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK && Info.flags == 0, "missing VM fails without publication or freezing"))
		{
			return false;
		}
		if (Path && *Path)
		{
			if (!Check(vs_interop_session_set_vm_backend(Session.get(), Text(Path)) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Session.get(), Text("answer"), nullptr, 0, Raw, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_value_get_int32(Raw, &Number) == VS_INTEROP_STATUS_OK && Number == 42,
			        "select dynamic VM after a not-entered call"))
			{
				return false;
			}
		}
		return true;
	}

	struct FProbe
	{
		vs_interop_session* Session = nullptr;
		unsigned Calls = 0, Destroyed = 0;
		bool Okay = true;
	};
	vs_interop_callback_result VS_INTEROP_CALL ProbeVm(vs_interop_call_context*, vs_interop_value*, void* Data)
	{
		auto& Probe = *static_cast<FProbe*>(Data);
		++Probe.Calls;
		vs_interop_vm_backend_info Info{sizeof(Info)};
		Probe.Okay &= vs_interop_session_get_vm_backend_info(Probe.Session, &Info) == VS_INTEROP_STATUS_OK && Info.flags == (VS_INTEROP_VM_SELECTED | VS_INTEROP_VM_FROZEN) && vs_interop_session_set_vm_backend(Probe.Session, {}) == VS_INTEROP_STATUS_ENGINE_ERROR;
		return VS_INTEROP_CALLBACK_OK;
	}
	vs_interop_callback_result VS_INTEROP_CALL Destroyed(vs_interop_call_context*, vs_interop_value*, void* Data)
	{
		++static_cast<FProbe*>(Data)->Destroyed;
		return VS_INTEROP_CALLBACK_OK;
	}

	bool TestExecutionAndFreeze(const char* Path)
	{
		FProbe Probe;
		auto Session = CreateSession();
		Probe.Session = Session.get();
		if (!Check(Session != nullptr, "VM execution session")) return false;
		if (Path && *Path)
		{
			std::string CopiedPath(Path);
			if (!Check(vs_interop_session_set_vm_backend(Session.get(), Text(CopiedPath.c_str())) == VS_INTEROP_STATUS_OK,
			        "select external VM"))
			{
				return false;
			}
			CopiedPath.assign(CopiedPath.size(), '!');
		}
		vs_interop_vm_backend_info Info{sizeof(Info)};
		if (!Check(vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK && Info.flags == VS_INTEROP_VM_SELECTED && Info.capabilities == Capabilities && (!(Path && *Path) || Info.library_path.size != 0), "selected VM owns its metadata"))
		{
			return false;
		}
		const auto LibraryPath = Copy(Info.library_path);
		if (Path && *Path && !Check(vs_interop_session_set_vm_backend(Session.get(), Info.library_path) == VS_INTEROP_STATUS_OK, "selection accepts its borrowed metadata path"))
		{
			return false;
		}
		vs_interop_config Config{sizeof(Config)};
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		Config.max_instructions = 100000;
		Config.enable_execution_control = 1;
		vs_interop_native_type_desc Void{sizeof(Void)};
		Void.type = VS_INTEROP_NATIVE_VOID;
		if (!Check(vs_interop_session_apply_config(Session.get(), &Config) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Session.get(), Text("probe_vm"), &Void, nullptr, 0, ProbeVm, &Probe) == VS_INTEROP_STATUS_OK && vs_interop_session_register_native(Session.get(), Text("destroyed"), &Void, nullptr, 0, Destroyed, &Probe) == VS_INTEROP_STATUS_OK,
		        "VM configuration and callbacks"))
		{
			return false;
		}
		vs_interop_value* Raw = nullptr;
		if (!Check(vs_interop_value_create(&Raw) == VS_INTEROP_STATUS_OK, "VM result value")) return false;
		const std::unique_ptr<vs_interop_value, decltype(&vs_interop_value_destroy)> Value(Raw, vs_interop_value_destroy);
		for (unsigned Run = 1; Run <= 2; ++Run)
		{
			vs_interop_run_result Result{sizeof(Result)};
			int32_t Number = 0;
			if (!Check(vs_interop_session_load_source(Session.get(), Text("class Item{public:~Item(){destroyed();}};int answer(){Item i=Item();probe_vm();return 42;}"),
			               Text("vm-selection.vs"), &Result) == VS_INTEROP_STATUS_OK &&
			            Result.success && vs_interop_session_call_function(Session.get(), Text("answer"), nullptr, 0, Raw, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_value_get_int32(Raw, &Number) == VS_INTEROP_STATUS_OK && Number == 42 && Probe.Okay && Probe.Calls == Run && Probe.Destroyed == Run,
			        "selected VM executes with governance, callback checks and exactly-once cleanup"))
			{
				return false;
			}
			if (!Check(vs_interop_session_set_vm_backend(Session.get(), {}) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_set_vm_backend(Session.get(), Text("missing-interop-vm-backend")) == VS_INTEROP_STATUS_INVALID_ARGUMENT && vs_interop_session_clear(Session.get()) == VS_INTEROP_STATUS_OK && vs_interop_session_get_vm_backend_info(Session.get(), &Info) == VS_INTEROP_STATUS_OK && Info.flags == (VS_INTEROP_VM_SELECTED | VS_INTEROP_VM_FROZEN) && Copy(Info.library_path) == LibraryPath,
			        "Clear preserves the frozen execution domain and selection"))
			{
				return false;
			}
		}
		return true;
	}
}

int main(int Argc, char** Argv)
{
	const char* Path = Argc > 1 ? Argv[1] : nullptr;
	if (!Check((vs_interop_get_capabilities() & VS_INTEROP_CAP_VM_BACKEND_SELECTION) != 0, "VM API discovery") || !TestMetadataAndFailedSelection() || !TestClearAndRetry(Path))
	{
		return EXIT_FAILURE;
	}
	#if VEROSCRIPT_INTEROP_TEST_DEFAULT_VM
	if (!TestExecutionAndFreeze(nullptr)) return EXIT_FAILURE;
	#endif
	if (Path && *Path && !TestExecutionAndFreeze(Path)) return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
