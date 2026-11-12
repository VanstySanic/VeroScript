// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace
{
	vs_interop_string_view Text(const char* Value)
	{
		return {Value, std::strlen(Value)};
	}
	struct FValue
	{
		FValue()
		{
			(void)vs_interop_value_create(&Handle);
		}
		~FValue()
		{
			vs_interop_value_destroy(Handle);
		}
		FValue(const FValue&) = delete;
		FValue& operator=(const FValue&) = delete;
		vs_interop_value* Handle = nullptr;
	};
	using FSession = std::unique_ptr<vs_interop_session, decltype(&vs_interop_session_destroy)>;
	using FCallable = std::unique_ptr<vs_interop_callable, decltype(&vs_interop_callable_destroy)>;
	using FBuffer = std::unique_ptr<vs_interop_buffer, decltype(&vs_interop_buffer_destroy)>;
	bool Check(bool Okay, const char* Stage)
	{
		if (!Okay)
		{
			std::cerr << "FAILED: " << Stage << '\n';
		}
		return Okay;
	}
	void VS_INTEROP_CALL Diagnostic(vs_interop_string_view Message, void*)
	{
		std::cerr.write(Message.data, static_cast<std::streamsize>(Message.size));
		std::cerr << '\n';
	}
	vs_interop_callback_result VS_INTEROP_CALL NotifyDestroyed(vs_interop_call_context*, vs_interop_value*, void* Data)
	{
		++*static_cast<int*>(Data);
		return VS_INTEROP_CALLBACK_OK;
	}

	constexpr char Source[] = R"vs(
int publicCounter = 9;
fn()->int make_mutable(int seed) { return [count = seed]() mutable -> int { return ++count; }; }
fn()->int make_reference() { int seed = 40; return [&value = seed]() -> int { return ++value; }; }
fn()->int make_readonly() { const int seed = 40; return [seed]() -> int { return seed; }; }
fn()->int make_shared_static(int seed) { return [seed]() -> int { static int count = seed; return ++count; }; }
int next_static(int seed) { static int count = seed; return ++count; }
int invoke(fn()->int operation) { return operation(); }
int static_batch() { auto a = make_shared_static(10); auto b = make_shared_static(90); return a() * 10000 + b() * 100 + a(); }
int plain_static_batch() { int first = next_static(30); return first * 100 + next_static(99); }
int backend_probe(int value) { return value + 1; }
class StaticLifetime { public: ~StaticLifetime() { state_destroyed(); } };
void touch_static() { static StaticLifetime instance = StaticLifetime(); }
)vs";

	bool TestState(const char* Vm, const char* Jit)
	{
		int Destroyed = 0;
		vs_interop_session_options Options{};
		Options.struct_size = sizeof(Options);
		Options.diagnostic_callback = Diagnostic;
		vs_interop_session* Raw = nullptr;
		if (!Check(vs_interop_session_create(&Options, &Raw) == VS_INTEROP_STATUS_OK, "create state session")) return false;
		FSession Session(Raw, vs_interop_session_destroy);
		if (Vm && !Check(vs_interop_session_set_vm_backend(Raw, Text(Vm)) == VS_INTEROP_STATUS_OK, "select state VM")) return false;
		// Persistent C calls use the existing retained-state option. JIT sessions
		// exercise static sharing inside a single call, preserving its fresh-state contract.
		const bool Persistent = !Jit;
		if (!Check(vs_interop_session_set_hot_reload_enabled(Raw, Persistent ? 1 : 0) == VS_INTEROP_STATUS_OK, "select persistent or fresh execution state"))
		{
			return false;
		}
		if (Jit)
		{
			vs_interop_config Config{};
			Config.struct_size = sizeof(Config);
			Config.execution_backend = VS_INTEROP_BACKEND_JIT;
			if (!Check(vs_interop_session_set_jit_backend(Raw, Text(Jit)) == VS_INTEROP_STATUS_OK && vs_interop_session_apply_config(Raw, &Config) == VS_INTEROP_STATUS_OK, "select state JIT"))
			{
				return false;
			}
		}
		vs_interop_native_type_desc Void{sizeof(Void)};
		Void.type = VS_INTEROP_NATIVE_VOID;
		vs_interop_run_result Run{sizeof(Run)};
		if (!Check(vs_interop_session_register_native(Raw, Text("state_destroyed"), &Void, nullptr, 0,
		               NotifyDestroyed, &Destroyed) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_load_source(Raw, Text(Source), Text("interop-language-state.vs"), &Run) == VS_INTEROP_STATUS_OK && Run.success,
		        "load closure and local static source"))
		{
			return false;
		}
		FValue Function, Other, Argument, Return;
		if (!Check(Function.Handle && Other.Handle && Argument.Handle && Return.Handle, "allocate state values")) return false;
		vs_interop_value* Arguments[] = {Argument.Handle};
		const auto Make = [&](const char* Name, int Seed, vs_interop_value* Output)
		{
			return vs_interop_value_set_int32(Argument.Handle, Seed) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Raw, Text(Name), Arguments, 1, Output, &Run) == VS_INTEROP_STATUS_OK && Run.success;
		};
		const auto Number = [&](int Expected)
		{
			std::int32_t Actual = 0;
			return vs_interop_value_get_int32(Return.Handle, &Actual) == VS_INTEROP_STATUS_OK && Actual == Expected;
		};
		const auto Invoke = [&](vs_interop_value* Fn, int Expected)
		{
			vs_interop_value* Functions[] = {Fn};
			return vs_interop_session_call_function(Raw, Text("invoke"), Functions, 1, Return.Handle, &Run) == VS_INTEROP_STATUS_OK && Run.success && Number(Expected);
		};
		if (!Check(Make("make_mutable", 40, Function.Handle) && Make("make_mutable", 100, Other.Handle), "make independent environments")) return false;
		vs_interop_value* Copy = nullptr;
		if (!Check(vs_interop_value_clone(Function.Handle, &Copy) == VS_INTEROP_STATUS_OK, "clone shared closure")) return false;
		const std::unique_ptr<vs_interop_value, decltype(&vs_interop_value_destroy)> Alias(Copy, vs_interop_value_destroy);
		vs_interop_callable* Held = nullptr;
		if (!Check(vs_interop_session_retain_callable(Raw, Function.Handle, 1, 0, &Held) == VS_INTEROP_STATUS_OK, "retain mutable closure"))
		{
			return false;
		}
		FCallable Callable(Held, vs_interop_callable_destroy);
		if (!Check(vs_interop_value_reset(Function.Handle) == VS_INTEROP_STATUS_OK && vs_interop_callable_invoke(Held, nullptr, 0, Return.Handle, &Run) == VS_INTEROP_STATUS_OK && Run.success && Number(41) && Invoke(Copy, 42) && Invoke(Other.Handle, 101),
		        "retained and cloned closures share one environment while new evaluations stay independent"))
		{
			return false;
		}
		if (!Check(vs_interop_session_call_function(Raw, Text("make_reference"), nullptr, 0, Function.Handle, &Run) == VS_INTEROP_STATUS_OK && Run.success && Invoke(Function.Handle, 41) && Invoke(Function.Handle, 42),
		        "reference initializer capture outlives the source frame"))
		{
			return false;
		}
		if (!Check(vs_interop_session_call_function(Raw, Text("make_readonly"), nullptr, 0, Function.Handle, &Run) == VS_INTEROP_STATUS_OK && Run.success && vs_interop_value_get_kind(Function.Handle) == VS_INTEROP_VALUE_FUNCTION && Invoke(Function.Handle, 40), "readonly capture stays an ordinary C function value"))
		{
			return false;
		}
		if (Persistent)
		{
			if (!Check(Make("make_shared_static", 10, Function.Handle) && Make("make_shared_static", 90, Other.Handle) && Invoke(Function.Handle, 11) && Invoke(Other.Handle, 12) && Invoke(Function.Handle, 13) && Make("next_static", 30, Return.Handle) && Number(31) && Make("next_static", 99, Return.Handle) && Number(32), "persistent state shares declaration statics across C calls"))
			{
				return false;
			}
		}
		else
		{
			for (int Repeat = 0; Repeat != 2; ++Repeat)
			{
				if (!Check(vs_interop_session_call_function(Raw, Text("static_batch"), nullptr, 0, Return.Handle, &Run) == VS_INTEROP_STATUS_OK && Run.success && Number(111213) && vs_interop_session_call_function(Raw, Text("plain_static_batch"), nullptr, 0, Return.Handle, &Run) == VS_INTEROP_STATUS_OK && Run.success && Number(3132), "fresh state shares statics inside a call and resets between C calls"))
				{
					return false;
				}
			}
		}
		if (!Check(vs_interop_session_call_function(Raw, Text("touch_static"), nullptr, 0, Return.Handle, &Run) == VS_INTEROP_STATUS_OK && Run.success && Destroyed == (Persistent ? 0 : 1), "static cleanup follows execution state lifetime"))
		{
			return false;
		}
		vs_interop_buffer* Json = nullptr;
		if (!Check(vs_interop_session_export_reflection_json(Raw, &Json) == VS_INTEROP_STATUS_OK, "reflect public globals")) return false;
		FBuffer Reflection(Json, vs_interop_buffer_destroy);
		const auto Bytes = vs_interop_buffer_view(Json);
		if (!Check(Bytes.data && Bytes.size, "reflection bytes are present")) return false;
		const std::string Metadata(reinterpret_cast<const char*>(Bytes.data), static_cast<std::size_t>(Bytes.size));
		if (!Check(Metadata.find("\"name\":\"publicCounter\"") != std::string::npos && Metadata.find("\"name\":\"count\"") == std::string::npos && Metadata.find("$static$") == std::string::npos,
		        "reflection does not expose private local static storage as globals"))
		{
			return false;
		}
		if (Jit)
		{
			if (!Check(Make("backend_probe", 41, Return.Handle) && Number(42), "JIT scalar probe")) return false;
			vs_interop_jit_stats Stats{sizeof(Stats)};
			if (!Check(vs_interop_session_get_last_jit_stats(Raw, &Stats) == VS_INTEROP_STATUS_OK && Stats.dispatches != 0, "selected JIT remains active after closure and static calls"))
			{
				return false;
			}
		}
		Session.reset();
		return Check(Destroyed == 1 && !vs_interop_callable_is_valid(Held), "session close destroys local statics once and invalidates callables");
	}
}

int main(int Argc, char** Argv)
{
	const char* Vm = Argc > 1 ? Argv[1] : nullptr;
	const char* Jit = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	if (!TestState(Vm, nullptr) || (Jit && *Jit && !TestState(Vm, Jit))) return EXIT_FAILURE;
	std::cout << "VeroScriptInteropLanguageStateTests passed\n";
	return EXIT_SUCCESS;
}
