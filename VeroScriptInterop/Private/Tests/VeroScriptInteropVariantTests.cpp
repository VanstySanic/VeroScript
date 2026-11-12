// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace
{
	void Check(bool Ok, const char* Name)
	{
		if (!Ok)
		{
			std::cerr << Name << '\n';
			std::exit(1);
		}
	}
	vs_interop_string_view Text(const char* S)
	{
		return {S, std::strlen(S)};
	}
	std::string String(vs_interop_string_view V)
	{
		return std::string(V.data ? V.data : "", static_cast<std::size_t>(V.size));
	}
	vs_interop_value* New()
	{
		vs_interop_value* V = nullptr;
		Check(vs_interop_value_create(&V) == 0, "create value");
		return V;
	}
	vs_interop_callback_result VS_INTEROP_CALL Echo(vs_interop_call_context* Call, vs_interop_value* Out, void*)
	{
		const vs_interop_value* Arg = nullptr;
		return vs_interop_call_context_get_argument(Call, 0, &Arg) == 0 && vs_interop_value_assign(Out, Arg) == 0 ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}
	void VS_INTEROP_CALL Diagnostic(vs_interop_string_view Line, void*)
	{
		std::cerr << String(Line) << '\n';
	}
	vs_interop_callback_result VS_INTEROP_CALL Replace(vs_interop_call_context* Call, vs_interop_value*, void* User)
	{
		vs_interop_value* Arg = nullptr;
		if (vs_interop_call_context_get_mutable_argument(Call, 0, &Arg) != 0) return VS_INTEROP_CALLBACK_ERROR;
		auto* Payload = New();
		auto* Box = New();
		const bool Ok = vs_interop_value_set_int32(Payload, 81) == 0 && vs_interop_value_set_variant(static_cast<vs_interop_session*>(User), Box, Text("int"), Payload) == 0 && vs_interop_value_set_variant_empty(Arg) == 0 && vs_interop_value_assign(Arg, Box) == 0;
		vs_interop_value_destroy(Box);
		vs_interop_value_destroy(Payload);
		return Ok ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}
	struct FTransitState
	{
		vs_interop_value* Held = nullptr;
		int Destroyed = 0;
	};
	vs_interop_callback_result VS_INTEROP_CALL TakeRoot(vs_interop_call_context*, vs_interop_value* Out, void* User)
	{
		auto& State = *static_cast<FTransitState*>(User);
		return vs_interop_value_assign(Out, State.Held) == 0 && vs_interop_value_set_variant_empty(State.Held) == 0
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}
	vs_interop_callback_result VS_INTEROP_CALL TransitDestroyed(vs_interop_call_context*, vs_interop_value*, void* User)
	{
		++static_cast<FTransitState*>(User)->Destroyed;
		return VS_INTEROP_CALLBACK_OK;
	}
	struct FCloseState
	{
		vs_interop_session* Session = nullptr;
		int Destroyed = 0;
	};
	vs_interop_callback_result VS_INTEROP_CALL CloseDuringCall(vs_interop_call_context* Call, vs_interop_value* Out, void* User)
	{
		const vs_interop_value* Arg = nullptr;
		return vs_interop_call_context_get_argument(Call, 0, &Arg) == 0 && vs_interop_value_assign(Out, Arg) == 0 && vs_interop_session_destroy(static_cast<FCloseState*>(User)->Session) == 0 ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}
	vs_interop_callback_result VS_INTEROP_CALL Notify(vs_interop_call_context*, vs_interop_value*, void* User)
	{
		auto& State = *static_cast<FCloseState*>(User);
		++State.Destroyed;
		Check(vs_interop_session_destroy(State.Session) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "closing shell stays valid through cleanup callbacks");
		return VS_INTEROP_CALLBACK_OK;
	}
	struct FForeignBoxing
	{
		vs_interop_session* Target;
		vs_interop_value* Box;
		bool Function;
	};
	vs_interop_callback_result VS_INTEROP_CALL BoxBorrowedValue(vs_interop_call_context* Call, vs_interop_value* Out, void* User)
	{
		auto& State = *static_cast<FForeignBoxing*>(User);
		const vs_interop_value* Argument = nullptr;
		if (vs_interop_call_context_get_argument(Call, 0, &Argument) != VS_INTEROP_STATUS_OK || vs_interop_value_set_int32(State.Box, 73) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		const auto Status = vs_interop_value_set_variant(State.Target, State.Box,
		    Text(State.Function ? "fn()->int" : "int"), Argument);
		std::int32_t Number = 0;
		bool Passed = false;
		if (State.Function)
		{
			Passed = Status == VS_INTEROP_STATUS_ENGINE_ERROR && vs_interop_value_get_int32(State.Box, &Number) == VS_INTEROP_STATUS_OK && Number == 73;
		}
		else
		{
			auto* Extracted = New();
			Passed = Status == VS_INTEROP_STATUS_OK && vs_interop_variant_get(State.Box, Extracted) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Extracted, &Number) == VS_INTEROP_STATUS_OK && Number == 42 && vs_interop_value_set_int32(Out, 42) == VS_INTEROP_STATUS_OK && vs_interop_value_set_variant(State.Target, State.Box, Text("int"), Out) == VS_INTEROP_STATUS_OK && vs_interop_variant_get(State.Box, Extracted) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(Extracted, &Number) == VS_INTEROP_STATUS_OK && Number == 42;
			vs_interop_value_destroy(Extracted);
		}
		return vs_interop_value_set_int32(Out, Passed ? 1 : 0) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}
	void TestBorrowedVariantTransfer()
	{
		vs_interop_session* Source = nullptr;
		vs_interop_session* Target = nullptr;
		Check(vs_interop_session_create(nullptr, &Source) == VS_INTEROP_STATUS_OK && vs_interop_session_create(nullptr, &Target) == VS_INTEROP_STATUS_OK, "create foreign boxing sessions");
		auto* Box = New();
		auto* ResultValue = New();
		FForeignBoxing Scalar{Target, Box, false}, Function{Target, Box, true};
		vs_interop_native_type_desc Integer{sizeof(Integer)};
		Integer.type = VS_INTEROP_NATIVE_INT32;
		vs_interop_native_param_desc Parameter{sizeof(Parameter)};
		Parameter.type = Integer;
		Check(vs_interop_session_register_native(Source, Text("box_int"), &Integer, &Parameter, 1,
		          BoxBorrowedValue, &Scalar) == VS_INTEROP_STATUS_OK,
		    "register borrowed scalar boxing");
		Parameter.type.type = VS_INTEROP_NATIVE_FUNCTION;
		Parameter.type.name = Text("fn()->int");
		Check(vs_interop_session_register_native(Source, Text("box_function"), &Integer, &Parameter, 1,
		          BoxBorrowedValue, &Function) == VS_INTEROP_STATUS_OK,
		    "register borrowed function boxing");
		vs_interop_run_result Result{sizeof(Result)};
		Check(vs_interop_session_load_source(Source, Text("int probe_scalar(){return box_int(42);} int probe_function(){return box_function([]()->int{return 42;});}"),
		          Text("borrowed-variant-transfer.vs"), &Result) == VS_INTEROP_STATUS_OK &&
		        Result.success,
		    "load borrowed variant transfer fixture");
		for (const char* Name : {"probe_scalar", "probe_function"})
		{
			std::int32_t Passed = 0;
			Check(vs_interop_session_call_function(Source, Text(Name), nullptr, 0, ResultValue, &Result) == VS_INTEROP_STATUS_OK && Result.success && vs_interop_value_get_int32(ResultValue, &Passed) == VS_INTEROP_STATUS_OK && Passed == 1,
			    "foreign borrowed scalar transfers while a function graph is rejected with output preserved");
		}
		vs_interop_value_destroy(ResultValue);
		vs_interop_value_destroy(Box);
		Check(vs_interop_session_destroy(Target) == VS_INTEROP_STATUS_OK && vs_interop_session_destroy(Source) == VS_INTEROP_STATUS_OK, "close foreign boxing sessions");
	}
}
int main()
{
	TestBorrowedVariantTransfer();
	vs_interop_session* Session = nullptr;
	vs_interop_session_options Options{};
	Options.struct_size = sizeof(Options);
	Options.diagnostic_callback = Diagnostic;
	Check(vs_interop_session_create(&Options, &Session) == 0, "create session");
	vs_interop_native_type_desc VariantType{};
	VariantType.struct_size = sizeof(VariantType);
	VariantType.type = VS_INTEROP_NATIVE_VARIANT;
	vs_interop_native_param_desc Parameter{};
	Parameter.struct_size = sizeof(Parameter);
	Parameter.type = VariantType;
	Check(vs_interop_session_register_native(Session, Text("echo_variant"), &VariantType, &Parameter, 1, Echo, nullptr) == 0, "register native variant signature");
	vs_interop_native_type_desc VoidType{};
	VoidType.struct_size = sizeof(VoidType);
	VoidType.type = VS_INTEROP_NATIVE_VOID;
	Parameter.flags = VS_INTEROP_PARAM_REF;
	Check(vs_interop_session_register_native(Session, Text("replace_variant"), &VoidType, &Parameter, 1, Replace, Session) == 0, "register mutable variant signature");
	Check((vs_interop_get_capabilities() & VS_INTEROP_CAP_VARIANT) != 0, "variant capability");
	vs_interop_value *Box = New(), *Payload = New(), *Out = New(), *Copy = nullptr;
	Check(vs_interop_value_set_variant_empty(Box) == 0 && vs_interop_value_get_kind(Box) == VS_INTEROP_VALUE_VARIANT, "empty kind");
	uint8_t Present = 1;
	vs_interop_string_view Type{};
	Check(vs_interop_variant_has_value(Box, &Present) == 0 && Present == 0, "empty state");
	Check(vs_interop_variant_get_type_name(Box, &Type) == 0 && String(Type).empty(), "empty type");
	Check(vs_interop_value_set_int32(Payload, 42) == 0 && vs_interop_value_set_int32(Out, 73) == 0, "set payload");
	Check(vs_interop_variant_get(Box, Out) != 0, "empty extraction fails");
	int32_t N = 0;
	Check(vs_interop_value_get_int32(Out, &N) == 0 && N == 73, "empty failure preserves output");
	Check(vs_interop_value_set_variant(Session, Box, Text("int"), Payload) == 0, "box scalar");
	Check(vs_interop_variant_has_value(Box, &Present) == 0 && Present == 1, "present state");
	Check(vs_interop_variant_get_type_name(Box, &Type) == 0 && String(Type) == "int", "canonical payload type");
	Check(vs_interop_value_get_type_name(Box, &Type) == 0 && String(Type) == "variant", "outer type");
	Check(vs_interop_variant_get(Box, Out) == 0 && vs_interop_value_get_int32(Out, &N) == 0 && N == 42, "extract scalar");
	for (const auto* Name : {"variant", "std::variant", "std.variant", " variant "})
	{
		Check(vs_interop_value_set_variant(Session, Out, Text(Name), Box) == 0 && vs_interop_variant_get_type_name(Out, &Type) == 0 && String(Type) == "int", "canonical flat box copy");
	}
	Check(vs_interop_value_set_variant(Session, Box, Text("double"), Payload) != 0, "explicit ABI boxing does not coerce");
	Check(vs_interop_value_clone(Box, &Copy) == 0 && vs_interop_value_set_variant_empty(Box) == 0, "clone independent slots");
	Check(vs_interop_variant_get(Copy, Out) == 0 && vs_interop_value_get_int32(Out, &N) == 0 && N == 42, "clone survives reset");
	vs_interop_value_destroy(Copy);
	Copy = nullptr;
	Check(vs_interop_session_set_hot_reload_enabled(Session, 1) == 0, "retain global state");
	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	Check(vs_interop_session_load_source(Session, Text("int destroyed=0; class Trace {public:~Trace(){destroyed+=1;}}; "
	                                                   "variant make(){return Trace();} int count(){return destroyed;} variant bounced(){return echo_variant(19);} "
	                                                   "int replace(){variant value=Trace(); replace_variant(ref value); return cast<int>(value);}"),
	          Text("variant-interop.vs"), &Result) == 0,
	    "load boxed object");
	Check(vs_interop_session_call_function(Session, Text("bounced"), nullptr, 0, Box, &Result) == 0 && vs_interop_variant_get(Box, Out) == 0 && vs_interop_value_get_int32(Out, &N) == 0 && N == 19, "implicit boxing before native callback");
	Check(vs_interop_session_call_function(Session, Text("make"), nullptr, 0, Box, &Result) == 0, "export rooted variant");
	Check(vs_interop_value_clone(Box, &Copy) == 0, "clone rooted variant");
	Check(vs_interop_variant_get(Box, Out) == 0 && vs_interop_value_get_kind(Out) == VS_INTEROP_VALUE_OBJECT, "root extracted object");
	const vs_interop_value* Items[] = {Box};
	Check(vs_interop_value_create_array(Payload, Text("variant"), Items, 1) == 0, "root container snapshot");
	Check(vs_interop_session_load_source(Session, Text("int replacement=1;"), Text("new.vs"), &Result) == VS_INTEROP_STATUS_ENGINE_ERROR, "live root blocks replacement");
	Check(vs_interop_value_reset(Box) == 0 && vs_interop_value_reset(Out) == 0 && vs_interop_value_reset(Payload) == 0, "retire old snapshots");
	std::thread Retire([&]
	{
		vs_interop_value_destroy(Copy);
	});
	Retire.join();
	Copy = nullptr;
	Check(vs_interop_session_call_function(Session, Text("count"), nullptr, 0, Out, &Result) == 0 && vs_interop_value_get_int32(Out, &N) == 0 && N == 1, "foreign retirement drains with script destructor");
	Check(vs_interop_session_call_function(Session, Text("replace"), nullptr, 0, Out, &Result) == 0 && vs_interop_value_get_int32(Out, &N) == 0 && N == 81, "native mutable variant staging");
	Check(vs_interop_session_call_function(Session, Text("make"), nullptr, 0, nullptr, &Result) == 0, "discard unused boxed return");
	Check(vs_interop_session_call_function(Session, Text("count"), nullptr, 0, Out, &Result) == 0 && vs_interop_value_get_int32(Out, &N) == 0 && N == 3, "native overwritten payload and ignored return each destroyed once");
	Check(vs_interop_session_call_function(Session, Text("make"), nullptr, 0, Box, &Result) == 0, "prepare close root");
	Check(vs_interop_session_destroy(Session) == 0, "close owner");
	Check(vs_interop_value_get_kind(Box) == VS_INTEROP_VALUE_INVALID, "closed root invalid");
	Check(vs_interop_variant_has_value(Box, &Present) == VS_INTEROP_STATUS_ENGINE_ERROR, "closed read rejected");
	Check(vs_interop_value_set_int32(Box, 1) == VS_INTEROP_STATUS_ENGINE_ERROR, "closed write rejected");
	vs_interop_value_destroy(Box);
	vs_interop_value_destroy(Payload);
	vs_interop_value_destroy(Out);
	{
		FTransitState Transit;
		vs_interop_session* Owner = nullptr;
		Check(vs_interop_session_create(&Options, &Owner) == 0, "create transit session");
		Parameter.flags = 0;
		Check(vs_interop_session_register_native(Owner, Text("take_root"), &VariantType, nullptr, 0, TakeRoot, &Transit) == 0 && vs_interop_session_register_native(Owner, Text("notify_transit"), &VoidType, nullptr, 0, TransitDestroyed, &Transit) == 0, "register transit callbacks");
		Check(vs_interop_session_load_source(Owner, Text("class Item{public:~Item(){notify_transit();}}; variant make(){return Item();} variant relay(){return take_root();} "
		                                                 "int read_number(variant value){return cast<int>(value);} int select(variant value){return 42;} int select(int value){return 7;} "
		                                                 "class Reader{public:variant saved;Reader(variant value){saved=value;} int read(variant value){return cast<int>(value);}};"),
		          Text("transit.vs"), &Result) == 0,
		    "load transit fixture");
		auto* Input = New();
		auto* Receiver = New();
		auto* Number = New();
		auto* Output = New();
		Transit.Held = New();
		Check(vs_interop_value_set_int32(Number, 42) == 0 && vs_interop_value_set_variant(Owner, Input, Text("int"), Number) == 0, "prepare variant argument");
		vs_interop_value* Args[] = {Input};
		Check(vs_interop_session_call_function(Owner, Text("read_number"), Args, 1, Output, &Result) == 0 && vs_interop_value_get_int32(Output, &N) == 0 && N == 42, "host free function resolves variant argument");
		Check(vs_interop_session_call_function(Owner, Text("select"), Args, 1, Output, &Result) == 0 && vs_interop_value_get_int32(Output, &N) == 0 && N == 42, "variant overload preserves outer type");
		Check(vs_interop_session_construct_object(Owner, Text("Reader"), Args, 1, Receiver, &Result) == 0, "host constructor resolves variant argument");
		Check(vs_interop_session_call_method(Owner, Receiver, Text("read"), Args, 1, Output, &Result) == 0 && vs_interop_value_get_int32(Output, &N) == 0 && N == 42, "host method resolves variant argument");
		Check(vs_interop_session_call_function(Owner, Text("make"), nullptr, 0, Transit.Held, &Result) == 0 && vs_interop_session_call_function(Owner, Text("relay"), nullptr, 0, Output, &Result) == 0 && vs_interop_variant_get_type_name(Output, &Type) == 0 && !String(Type).empty() && Transit.Destroyed == 0,
		    "returned variant survives retirement of its original wrapper");
		Check(vs_interop_value_reset(Output) == 0 && vs_interop_session_call_function(Owner, Text("select"), Args, 1, Output, &Result) == 0 && Transit.Destroyed == 1, "transferred payload is eventually destroyed once");
		for (auto* Value : {Input, Receiver, Number, Output, Transit.Held})
		{
			vs_interop_value_destroy(Value);
		}
		Check(vs_interop_session_destroy(Owner) == 0, "close transit session");
	}
	{
		FCloseState Close;
		Check(vs_interop_session_create(&Options, &Close.Session) == 0, "create closing session");
		Parameter.flags = 0;
		Check(vs_interop_session_register_native(Close.Session, Text("close_value"), &VariantType, &Parameter, 1, CloseDuringCall, &Close) == 0 && vs_interop_session_register_native(Close.Session, Text("notify"), &VoidType, nullptr, 0, Notify, &Close) == 0, "register closing callbacks");
		Check(vs_interop_session_load_source(Close.Session, Text("class Item {public:~Item(){notify();}}; variant invoke(){return close_value(Item());}"), Text("close.vs"), &Result) == 0, "load closing fixture");
		auto* ResultValue = New();
		Check(vs_interop_session_call_function(Close.Session, Text("invoke"), nullptr, 0, ResultValue, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR, "close during callback rejects dependent export");
		Check(Close.Destroyed == 1, "close defers cleanup until active call finishes");
		vs_interop_value_destroy(ResultValue);
	}
	return 0;
}
