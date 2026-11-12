// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace
{
	void Check(bool Ok, const char* What)
	{
		if (!Ok)
		{
			std::cerr << What << '\n';
			std::exit(1);
		}
	}
	vs_interop_string_view Text(const char* S)
	{
		return {S, std::strlen(S)};
	}
	vs_interop_callback_result VS_INTEROP_CALL Callback(vs_interop_call_context* Call, vs_interop_value* Out,
	    void*)
	{
		vs_interop_value* Number = nullptr;
		if (vs_interop_value_create(&Number) != 0)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		const auto Status = vs_interop_numeric_parse(nullptr, Call, 1, Text("18446744073709551615"), Number);
		const auto Converted =
		    Status == 0 ? vs_interop_numeric_convert(nullptr, Call, Number, VS_INTEROP_NATIVE_UINT64, 0, Out)
		                : Status;
		vs_interop_value_destroy(Number);
		return Converted == 0 ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}

	struct FUnavailableValueProbe
	{
		vs_interop_value* Value = nullptr;
		bool NestedChecked = false;
		bool DeserializeChecked = false;
		bool ExceptionEscaped = false;
	};
	vs_interop_callback_result VS_INTEROP_CALL CheckUnavailableNestedValue(
	    vs_interop_call_context* Call, vs_interop_value* Out, void* User)
	{
		auto& Probe = *static_cast<FUnavailableValueProbe*>(User);
		try
		{
			vs_interop_value* Arguments[] = {Probe.Value};
			if (vs_interop_value_set_int32(Out, 99) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const auto Function = vs_interop_call_context_invoke_function(Call, Text("sink"), Arguments, 1, Out);
			const auto Callable = vs_interop_call_context_invoke_callable(Call, Probe.Value, nullptr, 0, Out);
			int32_t Unchanged = 0;
			Probe.NestedChecked = Function == VS_INTEROP_STATUS_ENGINE_ERROR && Callable == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_value_get_int32(Out, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 99;
			return Probe.NestedChecked ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
		}
		catch (...)
		{
			Probe.ExceptionEscaped = true;
			return VS_INTEROP_CALLBACK_ERROR;
		}
	}
	vs_interop_callback_result VS_INTEROP_CALL UnusedSerializer(vs_interop_external_serialize_context*, void*)
	{
		return VS_INTEROP_CALLBACK_ERROR;
	}
	vs_interop_callback_result VS_INTEROP_CALL CheckUnavailableDeserializationValue(
	    vs_interop_external_deserialize_context* Context, void* User)
	{
		auto& Probe = *static_cast<FUnavailableValueProbe*>(User);
		try
		{
			Probe.DeserializeChecked = vs_interop_external_deserialize_context_add_field(
			                               Context, Text("value"), Probe.Value) == VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		catch (...)
		{
			Probe.ExceptionEscaped = true;
		}
		if (!Probe.DeserializeChecked || Probe.ExceptionEscaped) return VS_INTEROP_CALLBACK_ERROR;
		vs_interop_value* Replacement = nullptr;
		if (vs_interop_value_create(&Replacement) != VS_INTEROP_STATUS_OK) return VS_INTEROP_CALLBACK_ERROR;
		const bool Added = vs_interop_value_set_int32(Replacement, 42) == VS_INTEROP_STATUS_OK && vs_interop_external_deserialize_context_add_field(Context, Text("value"), Replacement) == VS_INTEROP_STATUS_OK;
		vs_interop_value_destroy(Replacement);
		return Added ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}
	void TestUnavailableValueBoundaries()
	{
		vs_interop_session *Owner = nullptr, *Session = nullptr;
		vs_interop_value *Root = nullptr, *Output = nullptr, *Object = nullptr;
		Check(vs_interop_session_create(nullptr, &Owner) == VS_INTEROP_STATUS_OK && vs_interop_session_create(nullptr, &Session) == VS_INTEROP_STATUS_OK && vs_interop_value_create(&Root) == VS_INTEROP_STATUS_OK && vs_interop_value_create(&Output) == VS_INTEROP_STATUS_OK && vs_interop_value_create(&Object) == VS_INTEROP_STATUS_OK, "value boundary fixture");
		vs_interop_run_result Result{sizeof(Result)};
		Check(vs_interop_session_load_source(Owner, Text("class Item{}; Item make(){return Item();}"), Text("numeric-owned-value.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Owner, Text("make"), nullptr, 0, Root, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(Output, 99) == VS_INTEROP_STATUS_OK, "prepare script-owned input");
		bool Escaped = false;
		uint32_t Kind = 99;
		vs_interop_status ToText = VS_INTEROP_STATUS_OK, ToNumber = VS_INTEROP_STATUS_OK;
		std::thread Foreign([&]
		{
			try
			{
				Kind = vs_interop_value_numeric_kind(Root);
				ToText = vs_interop_numeric_to_text(Session, nullptr, Root, Output);
				ToNumber = vs_interop_numeric_convert(Session, nullptr, Root, VS_INTEROP_NATIVE_INT32, 0, Output);
			}
			catch (...)
			{
				Escaped = true;
			}
		});
		Foreign.join();
		Check(!Escaped && Kind == 0 && ToText == VS_INTEROP_STATUS_WRONG_THREAD && ToNumber == VS_INTEROP_STATUS_WRONG_THREAD, "foreign rooted input stays within the C ABI");
		Check(vs_interop_session_destroy(Owner) == VS_INTEROP_STATUS_OK, "close input owner");
		uint8_t HasMethod = 1;
		Check(vs_interop_session_has_method(Session, Root, Text("value"), 0, &HasMethod) == VS_INTEROP_STATUS_ENGINE_ERROR && HasMethod == 0,
		    "method query preserves closed-root failure status");
		const auto RejectedExecution = [&](auto Operation, const char* Label)
		{
			Result = {sizeof(Result)};
			Result.success = Result.exit_requested = 1;
			Result.exit_code = 77;
			Result.diagnostic_count = 9;
			Check(Operation() == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && !Result.exit_requested && Result.exit_code != 77 && Result.diagnostic_count == 0, Label);
		};
		vs_interop_value* ClosedArguments[] = {Root};
		RejectedExecution([&]
		{
			return vs_interop_session_call_function(Session, Text("sink"),
			    ClosedArguments, 1, Output, &Result);
		}, "unavailable arguments clear stale execution success");
		RejectedExecution([&]
		{
			return vs_interop_session_call_method(Session, Root, Text("value"),
			    nullptr, 0, Output, &Result);
		}, "unavailable receiver clears stale execution success");
		RejectedExecution([&]
		{
			return vs_interop_session_call_method_bypassing_interceptor(Session,
			    Root, Text("value"), nullptr, 0, Output, &Result);
		}, "bypass failure clears stale success");
		RejectedExecution([&]
		{
			return vs_interop_session_construct_object(Session, Text("Item"),
			    ClosedArguments, 1, Output, &Result);
		}, "construction failure clears stale success");
		RejectedExecution([&]
		{
			return vs_interop_session_get_field(Session, Root, Text("value"),
			    Output, &Result);
		}, "field failure clears stale success");
		RejectedExecution([&]
		{
			return vs_interop_session_set_field(Session, Root, Text("value"),
			    Output, &Result);
		}, "field assignment failure clears stale success");
		RejectedExecution([&]
		{
			return vs_interop_session_set_global(Session, Text("value"),
			    Root, &Result);
		}, "global assignment failure clears stale success");
		RejectedExecution([&]
		{
			return vs_interop_session_apply_class_defaults(Session, Root,
			    &Result);
		}, "class-default failure clears stale success");
		Check(vs_interop_value_numeric_kind(Root) == 0 && vs_interop_numeric_to_text(Session, nullptr, Root, Output) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_numeric_convert(Session, nullptr, Root, VS_INTEROP_NATIVE_INT32, 0, Output) == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_numeric_convert(Session, nullptr, Root, VS_INTEROP_NATIVE_DOUBLE, 1, Output) == VS_INTEROP_STATUS_ENGINE_ERROR,
		    "closed rooted input reports unavailable without unwinding");
		int32_t Unchanged = 0;
		Check(vs_interop_value_get_int32(Output, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 99,
		    "rejected numeric input preserves output");
		FUnavailableValueProbe Probe;
		Probe.Value = Root;
		vs_interop_native_type_desc Return{sizeof(Return)};
		Return.type = VS_INTEROP_NATIVE_INT32;
		Check(vs_interop_session_register_native(Session, Text("check_unavailable"), &Return, nullptr, 0,
		          CheckUnavailableNestedValue, &Probe) == VS_INTEROP_STATUS_OK &&
		        vs_interop_session_register_external_serializer(Session, Text("Counter"), 1,
		            UnusedSerializer, CheckUnavailableDeserializationValue, &Probe) == VS_INTEROP_STATUS_OK,
		    "register value boundary callbacks");
		Check(vs_interop_session_load_source(Session, Text("class Counter{public:int value=7;}; Counter make(){return Counter();} "
		                                                   "int sink(int value){return value;} int main(){return check_unavailable();}"),
		          Text("unavailable-callback-values.vs"), &Result) == VS_INTEROP_STATUS_OK &&
		        vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.exit_code == 99 && Probe.NestedChecked && !Probe.ExceptionEscaped,
		    "nested calls preserve value-access failure status and return slot");
		const uint8_t Bytes[] = {1};
		Check(vs_interop_session_call_function(Session, Text("make"), nullptr, 0, Object, &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_deserialize_object(Session, Object, {Bytes, sizeof(Bytes)}, 1) == VS_INTEROP_STATUS_OK && Probe.DeserializeChecked && !Probe.ExceptionEscaped && vs_interop_session_get_field(Session, Object, Text("value"), Output, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Output, &Unchanged) == VS_INTEROP_STATUS_OK && Unchanged == 42,
		    "rejected deserialization value leaves the field transaction usable");
		vs_interop_value_destroy(Object);
		vs_interop_value_destroy(Output);
		vs_interop_value_destroy(Root);
		Check(vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK, "close value boundary session");
	}
	void TestPublishedValueCleanupFailure()
	{
		vs_interop_session* NumericSession = nullptr;
		vs_interop_value *Number = nullptr, *Output = nullptr;
		Check(vs_interop_session_create(nullptr, &NumericSession) == VS_INTEROP_STATUS_OK && vs_interop_value_create(&Number) == VS_INTEROP_STATUS_OK && vs_interop_value_create(&Output) == VS_INTEROP_STATUS_OK && vs_interop_numeric_parse(NumericSession, nullptr, 1, Text("7"), Number) == VS_INTEROP_STATUS_OK,
		    "published value cleanup fixture");
		for (const bool Reset : {false, true})
		{
			vs_interop_session* Owner = nullptr;
			vs_interop_run_result Result{sizeof(Result)};
			Check(vs_interop_session_create(nullptr, &Owner) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Owner, Text("class Item{public:~Item(){assert(false);}};Item make(){return Item();}"), Text("published-value-cleanup.vs"), &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_session_call_function(Owner, Text("make"), nullptr, 0, Output, &Result) == VS_INTEROP_STATUS_OK && Result.success,
			    "prepare output with a failing language destructor");
			if (Reset)
			{
				Check(vs_interop_value_reset(Output) == VS_INTEROP_STATUS_OK && vs_interop_value_get_kind(Output) == VS_INTEROP_VALUE_INVALID,
				    "committed reset does not report a later cleanup failure");
			}
			else
			{
				vs_interop_string_view Formatted{};
				Check(vs_interop_numeric_to_text(NumericSession, nullptr, Number, Output) == VS_INTEROP_STATUS_OK && vs_interop_value_get_string(Output, &Formatted) == VS_INTEROP_STATUS_OK && std::string(Formatted.data, static_cast<std::size_t>(Formatted.size)) == "7",
				    "committed numeric output remains successful after old-owner cleanup fails");
			}
			uint64_t ErrorSize = 0;
			Check(vs_interop_session_copy_last_error(Owner, nullptr, 0, &ErrorSize) == VS_INTEROP_STATUS_BUFFER_TOO_SMALL && ErrorSize > 1,
			    "post-publication cleanup failure stays in the old owner's diagnostics");
			Check(vs_interop_session_destroy(Owner) == VS_INTEROP_STATUS_OK, "close failed-cleanup owner");
		}
		vs_interop_value_destroy(Output);
		vs_interop_value_destroy(Number);
		Check(vs_interop_session_destroy(NumericSession) == VS_INTEROP_STATUS_OK, "close published value fixture");
	}
} // namespace
int main()
{
	TestUnavailableValueBoundaries();
	TestPublishedValueCleanupFailure();
	vs_interop_session* Session = nullptr;
	vs_interop_value *Number = nullptr, *Output = nullptr;
	Check(vs_interop_session_create(nullptr, &Session) == 0 && Session, "create session");
	Check(vs_interop_value_create(&Number) == 0 && vs_interop_value_create(&Output) == 0, "create values");
	Check((vs_interop_get_capabilities() & VS_INTEROP_CAP_NUMERICS) != 0, "numeric capability");
	Check(vs_interop_numeric_parse(Session, nullptr, 1, Text("18446744073709551615"), Number) == 0 &&
	        vs_interop_value_numeric_kind(Number) == 1,
	    "exact bigint import");
	Check(vs_interop_numeric_convert(Session, nullptr, Number, VS_INTEROP_NATIVE_UINT64, 0, Output) == 0,
	    "uint64 convert");
	uint64_t U = 0;
	Check(vs_interop_value_get_uint64(Output, &U) == 0 && U == UINT64_MAX, "uint64 retains all bits");
	Check(vs_interop_numeric_convert(Session, nullptr, Number, VS_INTEROP_NATIVE_INT64, 0, Output) != 0,
	    "int64 range");
	Check(vs_interop_value_get_uint64(Output, &U) == 0 && U == UINT64_MAX, "failed conversion keeps output");
	Check(vs_interop_numeric_parse(Session, nullptr, 2, Text("6/18"), Number) == 0 &&
	        vs_interop_value_numeric_kind(Number) == 2,
	    "exact rational import");
	Check(vs_interop_numeric_to_text(Session, nullptr, Number, Output) == 0, "canonical text export");
	vs_interop_string_view S{};
	Check(vs_interop_value_get_string(Output, &S) == 0 &&
	        std::string(S.data, static_cast<std::size_t>(S.size)) == "1/3",
	    "rational normalization");
	Check(vs_interop_numeric_convert(Session, nullptr, Number, VS_INTEROP_NATIVE_DOUBLE, 1, Output) != 0,
	    "exact float rejects lost precision");
	Check(vs_interop_numeric_convert(Session, nullptr, Number, VS_INTEROP_NATIVE_DOUBLE, 0, Output) == 0,
	    "rounded float conversion");
	const char Nul[] = {'1', '\0', '2'};
	Check(vs_interop_numeric_parse(Session, nullptr, 1, {Nul, sizeof(Nul)}, Number) != 0 &&
	        vs_interop_value_numeric_kind(Number) == 2,
	    "embedded NUL is not truncated");
	vs_interop_numeric_limits Limits{};
	Limits.struct_size = sizeof(Limits);
	Limits.max_value_bits = 8;
	Check(vs_interop_session_set_numeric_limits(Session, &Limits) == 0, "numeric limits");
	Check(vs_interop_numeric_parse(Session, nullptr, 1, Text("256"), Number) ==
	        VS_INTEROP_STATUS_LIMIT_EXCEEDED,
	    "numeric budget");
	vs_interop_status ThreadStatus = 0;
	std::thread Worker([&]
	{
		ThreadStatus = vs_interop_numeric_to_text(Session, nullptr, Number, Output);
	});
	Worker.join();
	Check(ThreadStatus == VS_INTEROP_STATUS_WRONG_THREAD, "owner thread contract");
	Check(vs_interop_numeric_parse(nullptr, nullptr, 1, Text("1"), Number) ==
	        VS_INTEROP_STATUS_INVALID_ARGUMENT,
	    "context required");
	Check(vs_interop_numeric_parse(Session, nullptr, 1, {nullptr, 1}, Number) ==
	        VS_INTEROP_STATUS_INVALID_ARGUMENT,
	    "invalid pointer length");
	vs_interop_config Config{};
	Config.struct_size = sizeof(Config);
	Config.execution_backend = VS_INTEROP_BACKEND_VM;
	Check(vs_interop_session_apply_config(Session, &Config) == 0, "apply config");
	Check(vs_interop_numeric_parse(Session, nullptr, 1, Text("256"), Number) ==
	        VS_INTEROP_STATUS_LIMIT_EXCEEDED,
	    "configuration preserves numeric limits");
	Limits.max_value_bits = 0;
	Check(vs_interop_session_set_numeric_limits(Session, &Limits) == 0, "restore defaults");
	vs_interop_native_type_desc Return{};
	Return.struct_size = sizeof(Return);
	Return.type = VS_INTEROP_NATIVE_UINT64;
	Check(vs_interop_session_register_native(Session, Text("host_numeric"), &Return, nullptr, 0, Callback,
	          nullptr) == 0,
	    "register callback");
	vs_interop_run_result Run{};
	Run.struct_size = sizeof(Run);
	Check(vs_interop_session_load_source(
	          Session, Text("void main(){assert(host_numeric()==18446744073709551615u64);}"),
	          Text("numeric-callback.vs"), &Run) == 0 &&
	        Run.success,
	    "load callback source");
	Check(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Run) == 0 && Run.success,
	    "borrowed active numeric context");
	vs_interop_value_destroy(Number);
	vs_interop_value_destroy(Output);
	Check(vs_interop_session_destroy(Session) == 0, "destroy session");
	std::cout << "Interop numeric exchange, precision, budget and ownership checks passed\n";
	return 0;
}
