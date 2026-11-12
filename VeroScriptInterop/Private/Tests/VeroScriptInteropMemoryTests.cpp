// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <thread>

namespace
{
	bool Check(bool Ok, const char* Label)
	{
		if (!Ok)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Ok;
	}
	vs_interop_string_view Text(const char* S)
	{
		return {S, std::strlen(S)};
	}
	struct FSessionOwner
	{
		FSessionOwner()
		{
			vs_interop_session_create(nullptr, &Value);
		}
		~FSessionOwner()
		{
			vs_interop_session_destroy(Value);
		}
		vs_interop_session* Value = nullptr;
	};
	struct FValueOwner
	{
		FValueOwner()
		{
			vs_interop_value_create(&Value);
		}
		~FValueOwner()
		{
			vs_interop_value_destroy(Value);
		}
		vs_interop_value* Value = nullptr;
	};
	struct FCallbackProbe
	{
		vs_interop_session* Session = nullptr;
		bool AddressesAllowed = true;
		bool Passed = false;
	};
	vs_interop_callback_result VS_INTEROP_CALL HostReferenceAddress(
	    vs_interop_call_context* Context, vs_interop_value* OutReturn, void* UserData)
	{
		const auto& Probe = *static_cast<FCallbackProbe*>(UserData);
		uint64_t Address = 0;
		const auto Status = vs_interop_memory_call_argument_address(Context, 0, &Address);
		if (!Check(Probe.AddressesAllowed ? Status == VS_INTEROP_STATUS_OK && Address != 0
		                                  : Status == VS_INTEROP_STATUS_PERMISSION_DENIED,
		        "original ref argument address"))
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return vs_interop_value_set_int32(OutReturn, 42) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}
	vs_interop_callback_result VS_INTEROP_CALL HostMemory(
	    vs_interop_call_context* Context, vs_interop_value* OutReturn, void* UserData)
	{
		auto& Probe = *static_cast<FCallbackProbe*>(UserData);
		const vs_interop_value* Argument = nullptr;
		FValueOwner Number;
		if (!Number.Value ||
		    vs_interop_call_context_get_argument(Context, 0, &Argument) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		bool Ok = Check(vs_interop_memory_create_buffer(Probe.Session, Context, 4, OutReturn) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "callback rejects two contexts");
		Ok &= Check(vs_interop_memory_create_buffer(Probe.Session, nullptr, 4, OutReturn) == VS_INTEROP_STATUS_BUSY, "callback rejects session reentry");
		const vs_interop_stdlib_limits StdLimits{sizeof(StdLimits), 0, 1, 1, 1};
		Ok &= Check(vs_interop_session_set_stdlib_limits(Probe.Session, &StdLimits) == VS_INTEROP_STATUS_BUSY, "callback rejects stdlib reconfiguration");
		Ok &= Check(vs_interop_memory_create_buffer(nullptr, Context, 4, OutReturn) == VS_INTEROP_STATUS_OK, "callback memory context");
		Ok &= Check(vs_interop_memory_write(nullptr, Context, OutReturn, 0,
		                VS_INTEROP_NATIVE_UINT8, VS_INTEROP_MEMORY_LITTLE, Argument) == VS_INTEROP_STATUS_OK,
		    "callback writes borrowed argument");
		Ok &= Check(vs_interop_memory_read(nullptr, Context, OutReturn, 0,
		                VS_INTEROP_NATIVE_UINT8, VS_INTEROP_MEMORY_LITTLE, Number.Value) == VS_INTEROP_STATUS_OK,
		    "callback reads memory");
		uint8_t Byte = 0;
		Ok &= Check(vs_interop_value_get_uint8(Number.Value, &Byte) == VS_INTEROP_STATUS_OK && Byte == 42, "callback byte value");
		Ok &= Check(vs_interop_memory_create_buffer(nullptr, Context, 1,
		                const_cast<vs_interop_value*>(Argument)) == VS_INTEROP_STATUS_INVALID_ARGUMENT,
		    "callback borrowed output stays read only");
		uint8_t Available = 2;
		Ok &= Check(vs_interop_memory_native_addresses_available(nullptr, Context, &Available) == VS_INTEROP_STATUS_OK && (Available != 0) == Probe.AddressesAllowed,
		    "callback address capability");
		uint64_t Address = 0;
		Ok &= Check(vs_interop_memory_call_argument_address(Context, 0, &Address) == (Probe.AddressesAllowed ? VS_INTEROP_STATUS_UNSUPPORTED : VS_INTEROP_STATUS_PERMISSION_DENIED), "by-value argument has no storage address");
		Ok &= Check(vs_interop_memory_data_address(nullptr, Context, OutReturn, &Address) == (Probe.AddressesAllowed ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_PERMISSION_DENIED),
		    "callback data address permission");
		Ok &= Check(vs_interop_memory_object_address(nullptr, Context, OutReturn, &Address) == (Probe.AddressesAllowed ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_PERMISSION_DENIED),
		    "callback object address permission");
		Probe.Passed = Ok;
		return Ok && vs_interop_value_set_int32(OutReturn, 42) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}
	bool TestExternalOwnerPublication()
	{
		int RejectedReleases = 0, AcceptedReleases = 0;
		uint8_t Bytes[4]{};
		FSessionOwner Session, ClosedOwner;
		FValueOwner Closed, Foreign, Output;
		if (!Check(Session.Value && ClosedOwner.Value && Closed.Value && Foreign.Value && Output.Value,
		        "external publication fixture"))
		{
			return false;
		}
		const auto PrepareRoot = [](vs_interop_session* Owner, vs_interop_value* Value)
		{
			vs_interop_run_result Result{sizeof(Result)};
			return Owner && vs_interop_session_load_source(Owner, Text("class Item{}; Item make(){return Item();}"), Text("external-output.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_call_function(Owner, Text("make"), nullptr, 0, Value, &Result) == VS_INTEROP_STATUS_OK;
		};
		if (!Check(PrepareRoot(ClosedOwner.Value, Closed.Value), "prepare closed external output")) return false;
		bool Ok = Check(vs_interop_session_destroy(ClosedOwner.Value) == VS_INTEROP_STATUS_OK,
		    "close external output owner");
		ClosedOwner.Value = nullptr;
		const auto Release = +[](void* Data)
		{
			++*static_cast<int*>(Data);
		};
		Ok &= Check(vs_interop_memory_wrap_external(Session.Value, nullptr, Bytes, sizeof(Bytes), 0,
		                &RejectedReleases, Release, Closed.Value) == VS_INTEROP_STATUS_ENGINE_ERROR &&
		        RejectedReleases == 0 && vs_interop_value_get_kind(Closed.Value) == VS_INTEROP_VALUE_INVALID,
		    "closed output rejects external ownership without publishing a region");
		std::promise<bool> Ready;
		auto ReadyFuture = Ready.get_future();
		std::promise<void> Finish;
		auto FinishFuture = Finish.get_future();
		std::thread Worker([&]
		{
			FSessionOwner Owner;
			Ready.set_value(PrepareRoot(Owner.Value, Foreign.Value));
			FinishFuture.wait();
		});
		const bool ForeignReady = ReadyFuture.get();
		Ok &= Check(ForeignReady, "prepare foreign-thread external output");
		if (ForeignReady)
		{
			Ok &= Check(vs_interop_memory_wrap_external(Session.Value, nullptr, Bytes, sizeof(Bytes), 0,
			                &RejectedReleases, Release, Foreign.Value) == VS_INTEROP_STATUS_WRONG_THREAD &&
			        RejectedReleases == 0,
			    "foreign output does not consume external ownership");
		}
		Finish.set_value();
		Worker.join();
		vs_interop_memory_stats Stats{sizeof(Stats)};
		Ok &= Check(vs_interop_memory_get_stats(Session.Value, nullptr, &Stats) == VS_INTEROP_STATUS_OK && Stats.borrowed_bytes == 0, "rejected external outputs do not retain borrowed budget");
		Ok &= Check(vs_interop_memory_wrap_external(Session.Value, nullptr, Bytes, sizeof(Bytes), 0,
		                &AcceptedReleases, Release, Output.Value) == VS_INTEROP_STATUS_OK &&
		        AcceptedReleases == 0,
		    "published external region adopts its owner");
		vs_interop_value* Copy = nullptr;
		Ok &= Check(vs_interop_value_clone(Output.Value, &Copy) == VS_INTEROP_STATUS_OK && Copy && vs_interop_value_reset(Output.Value) == VS_INTEROP_STATUS_OK && AcceptedReleases == 0,
		    "external clone retains the adopted owner");
		std::thread LastRelease([&]
		{
			vs_interop_value_destroy(Copy);
		});
		LastRelease.join();
		Ok &= Check(AcceptedReleases == 1, "external owner releases once on its final releasing thread");
		return Ok;
	}
	bool TestMemoryContracts()
	{
		FSessionOwner Session, Other;
		FValueOwner Buffer, View, Snapshot, Array, Number;
		if (!Check(Session.Value && Other.Value && Buffer.Value && View.Value && Snapshot.Value &&
		            Array.Value && Number.Value,
		        "contract test handles"))
		{
			return false;
		}
		auto* S = Session.Value;
		bool Ok = Check((vs_interop_get_capabilities() & VS_INTEROP_CAP_MEMORY) != 0,
		    "memory capability discovery");
		vs_interop_memory_limits Limits{sizeof(Limits), 0, 8, 16};
		Ok &= Check(vs_interop_session_set_memory_limits(S, &Limits) == VS_INTEROP_STATUS_OK,
		    "contract limits");
		Ok &= Check(vs_interop_memory_create_buffer(S, nullptr, -1, Buffer.Value) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "negative buffer size");
		Ok &= Check(vs_interop_memory_create_buffer(S, nullptr, 9, Buffer.Value) == VS_INTEROP_STATUS_LIMIT_EXCEEDED, "buffer capacity limit");
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		Ok &= Check(vs_interop_session_apply_config(S, &Config) == VS_INTEROP_STATUS_OK && vs_interop_memory_create_buffer(S, nullptr, 9, Buffer.Value) == VS_INTEROP_STATUS_LIMIT_EXCEEDED, "configuration preserves memory limits");
		Ok &= Check(vs_interop_memory_create_buffer(S, nullptr, 8, Buffer.Value) == VS_INTEROP_STATUS_OK,
		    "contract buffer");
		vs_interop_memory_limits Smaller{sizeof(Smaller), 0, 7, 16};
		Ok &= Check(vs_interop_session_set_memory_limits(S, &Smaller) == VS_INTEROP_STATUS_LIMIT_EXCEEDED,
		    "cannot reduce limit below retained capacity");
		int64_t Size = -1;
		Ok &= Check(vs_interop_memory_size(nullptr, nullptr, Buffer.Value, &Size) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "missing context");
		Ok &= Check(vs_interop_memory_size(Other.Value, nullptr, Buffer.Value, &Size) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "foreign memory domain");
		vs_interop_status ForeignStatus = VS_INTEROP_STATUS_OK;
		std::thread Thread(
		    [&]
		{
			ForeignStatus = vs_interop_memory_size(S, nullptr, Buffer.Value, &Size);
		});
		Thread.join();
		Ok &= Check(ForeignStatus == VS_INTEROP_STATUS_WRONG_THREAD, "memory owner thread");
		Ok &= Check(vs_interop_memory_fill(S, nullptr, Buffer.Value, 0, 8, 42) == VS_INTEROP_STATUS_OK && vs_interop_memory_clone(S, nullptr, Buffer.Value, Snapshot.Value) == VS_INTEROP_STATUS_OK,
		    "independent memory clone");
		uint8_t Equal = 0;
		Ok &= Check(vs_interop_memory_equal(S, nullptr, Buffer.Value, 0, Snapshot.Value, 0, 8, &Equal) == VS_INTEROP_STATUS_OK && Equal, "clone byte equality");
		Ok &= Check(vs_interop_memory_create_buffer(S, nullptr, 1, Array.Value) == VS_INTEROP_STATUS_LIMIT_EXCEEDED, "retained byte limit");
		int Releases = 0;
		uint8_t External[9]{};
		auto Release = +[](void* P)
		{
			++*static_cast<int*>(P);
		};
		Ok &= Check(vs_interop_memory_wrap_external(S, nullptr, External, sizeof(External), 0,
		                &Releases, Release, Array.Value) == VS_INTEROP_STATUS_LIMIT_EXCEEDED &&
		        Releases == 0,
		    "failed external registration preserves owner");
		Ok &= Check(vs_interop_memory_wrap_external(S, nullptr, nullptr, 1, 0,
		                &Releases, Release, Array.Value) == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
		        Releases == 0,
		    "null external storage rejected without adoption");
		Ok &= Check(vs_interop_memory_fill(S, nullptr, Buffer.Value, 0, 1, 7) == VS_INTEROP_STATUS_OK && vs_interop_memory_equal(S, nullptr, Buffer.Value, 0, Snapshot.Value, 0, 8, &Equal) == VS_INTEROP_STATUS_OK && !Equal, "clone does not alias bytes");
		Ok &= Check(vs_interop_memory_to_array(S, nullptr, Snapshot.Value, Array.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_reset(Snapshot.Value) == VS_INTEROP_STATUS_OK && vs_interop_memory_from_array(S, nullptr, Array.Value, Snapshot.Value) == VS_INTEROP_STATUS_OK,
		    "uint8 array round trip");
		Ok &= Check(vs_interop_memory_read(S, nullptr, Snapshot.Value, 0, VS_INTEROP_NATIVE_UINT8,
		                VS_INTEROP_MEMORY_LITTLE, Number.Value) == VS_INTEROP_STATUS_OK,
		    "snapshot array bytes");
		uint8_t Byte = 0;
		Ok &= Check(vs_interop_value_get_uint8(Number.Value, &Byte) == VS_INTEROP_STATUS_OK && Byte == 42,
		    "snapshot byte preserved");
		Ok &= Check(vs_interop_memory_from_array(S, nullptr, nullptr, Number.Value) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "null array argument");
		Ok &= Check(vs_interop_memory_from_array(S, nullptr, Number.Value, Snapshot.Value) == VS_INTEROP_STATUS_TYPE_MISMATCH, "non-array argument");
		Ok &= Check(vs_interop_memory_slice(S, nullptr, Buffer.Value, 0, 4, 1, View.Value) == VS_INTEROP_STATUS_OK && vs_interop_memory_slice(S, nullptr, View.Value, 0, 1, 0, Number.Value) == VS_INTEROP_STATUS_READ_ONLY, "read only slice cannot gain write permission");
		Ok &= Check(vs_interop_memory_resize(S, nullptr, Buffer.Value, 8, 1) == VS_INTEROP_STATUS_OK && vs_interop_memory_size(S, nullptr, View.Value, &Size) == VS_INTEROP_STATUS_OK && Size == 4,
		    "no-op reserve preserves view");
		Ok &= Check(vs_interop_memory_resize(S, nullptr, Buffer.Value, 7, 0) == VS_INTEROP_STATUS_OK && vs_interop_memory_size(S, nullptr, View.Value, &Size) == VS_INTEROP_STATUS_EXPIRED,
		    "resize expires view");
		Ok &= Check(vs_interop_memory_capacity(S, nullptr, Buffer.Value, &Size) == VS_INTEROP_STATUS_OK && Size == 8, "shrink preserves exact capacity");
		Ok &= Check(vs_interop_memory_create_view(S, nullptr, 1, View.Value) == VS_INTEROP_STATUS_OK && vs_interop_memory_size(Other.Value, nullptr, View.Value, &Size) == VS_INTEROP_STATUS_OK && Size == 0, "ownerless view is a valid empty region");
		Ok &= Check(vs_interop_memory_fill(S, nullptr, View.Value, 0, 0, 1) == VS_INTEROP_STATUS_READ_ONLY, "ownerless read only view rejects empty writes");
		Ok &= Check(vs_interop_memory_copy(S, nullptr, Buffer.Value, 1, Buffer.Value, 0, 6) == VS_INTEROP_STATUS_OK && vs_interop_memory_read(S, nullptr, Buffer.Value, 1, VS_INTEROP_NATIVE_UINT8, VS_INTEROP_MEMORY_LITTLE, Number.Value) == VS_INTEROP_STATUS_OK && vs_interop_value_get_uint8(Number.Value, &Byte) == VS_INTEROP_STATUS_OK && Byte == 7,
		    "overlapping copy");
		uint64_t Id = 0, CloneId = 0;
		Ok &= Check(vs_interop_memory_object_id(S, nullptr, Buffer.Value, &Id) == VS_INTEROP_STATUS_OK && vs_interop_memory_object_id(S, nullptr, Snapshot.Value, &CloneId) == VS_INTEROP_STATUS_OK && Id != 0 && CloneId != 0 && Id != CloneId, "buffer identities");
		vs_interop_memory_lease Lease{};
		Lease.struct_size = sizeof(Lease);
		uint64_t Before = 0, After = 0;
		Ok &= Check(vs_interop_memory_mutation_version(S, nullptr, Buffer.Value, &Before) == VS_INTEROP_STATUS_OK,
		    "version before lease");
		const auto Acquired = vs_interop_memory_acquire_lease(S, nullptr, Buffer.Value, 1, &Lease);
		Ok &= Check(Acquired == VS_INTEROP_STATUS_OK, "version write lease");
		if (Acquired == VS_INTEROP_STATUS_OK)
		{
			Ok &= Check(vs_interop_memory_release_lease(Lease.token) == VS_INTEROP_STATUS_OK && vs_interop_memory_mutation_version(S, nullptr, Buffer.Value, &After) == VS_INTEROP_STATUS_OK && After == Before + 1, "write lease commits version");
		}
		vs_interop_value_reset(Buffer.Value);
		vs_interop_value_reset(View.Value);
		vs_interop_value_reset(Snapshot.Value);
		FCallbackProbe Probe{S};
		vs_interop_native_type_desc Return{}, ParameterType{};
		Return.struct_size = sizeof(Return);
		Return.type = VS_INTEROP_NATIVE_INT32;
		ParameterType.struct_size = sizeof(ParameterType);
		ParameterType.type = VS_INTEROP_NATIVE_UINT8;
		vs_interop_native_param_desc Parameter{};
		Parameter.struct_size = sizeof(Parameter);
		Parameter.type = ParameterType;
		Ok &= Check(vs_interop_session_register_native(S, Text("host_memory"), &Return, &Parameter,
		                1, &HostMemory, &Probe) == VS_INTEROP_STATUS_OK,
		    "memory native registration");
		Parameter.type = Return;
		Parameter.flags = VS_INTEROP_PARAM_REF;
		Ok &= Check(vs_interop_session_register_native(S, Text("host_reference"), &Return, &Parameter,
		                1, &HostReferenceAddress, &Probe) == VS_INTEROP_STATUS_OK,
		    "ref memory native registration");
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		Ok &= Check(vs_interop_session_load_source(S, Text("int main(){uint8 value=42; int reference=42; "
		                                                   "assert(host_reference(ref reference)==42); return host_memory(value);}"),
		                Text("memory-context.vs"), &Result) == VS_INTEROP_STATUS_OK,
		    "memory callback load");
		if (!Result.success)
		{
			char Error[2048]{};
			uint64_t Required = 0;
			vs_interop_session_copy_last_error(S, Error, sizeof(Error), &Required);
			std::cerr << Error << '\n';
		}
		Ok &= Check(vs_interop_session_run(S, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42 && Probe.Passed, "memory callback run");
		Config.capability_policy_enabled = 1;
		Config.allowed_script_capabilities = VS_INTEROP_SCRIPT_CAP_ALL & ~VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS;
		Probe.AddressesAllowed = false;
		Probe.Passed = false;
		Ok &= Check(vs_interop_session_apply_config(S, &Config) == VS_INTEROP_STATUS_OK,
		    "disable address capability");
		Ok &= Check(vs_interop_session_run(S, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Probe.Passed, "callback observes address permission");
		if (const auto* Backend = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"); Backend && *Backend)
		{
			Ok &= Check(vs_interop_session_set_jit_backend(S, Text(Backend)) == VS_INTEROP_STATUS_OK,
			    "memory callback explicit backend");
			Probe.AddressesAllowed = true;
			Config.allowed_script_capabilities = VS_INTEROP_SCRIPT_CAP_ALL;
			for (const auto Mode : {VS_INTEROP_BACKEND_MIXED, VS_INTEROP_BACKEND_JIT})
			{
				Probe.Passed = false;
				Config.execution_backend = Mode;
				Ok &= Check(vs_interop_session_apply_config(S, &Config) == VS_INTEROP_STATUS_OK,
				    "memory callback backend policy");
				Ok &= Check(vs_interop_session_run(S, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42 && Probe.Passed,
				    "memory callback services with selected backend");
			}
		}
		return Ok;
	}
} // namespace
int main()
{
	bool Ok = TestExternalOwnerPublication();
	Ok &= TestMemoryContracts();
	vs_interop_session* S = nullptr;
	vs_interop_session_options Options{};
	Options.struct_size = sizeof(Options);
	Ok &= Check(vs_interop_session_create(&Options, &S) == VS_INTEROP_STATUS_OK, "session");
	if (!S)
	{
		return 1;
	}
	vs_interop_memory_limits Limits{sizeof(Limits), 0, 64, 128};
	Ok &= Check(vs_interop_session_set_memory_limits(S, &Limits) == VS_INTEROP_STATUS_OK, "limits");
	vs_interop_stdlib_limits StdLimits{sizeof(StdLimits), 0, 128, 256, 32};
	Ok &= Check(vs_interop_session_set_stdlib_limits(S, &StdLimits) == VS_INTEROP_STATUS_OK, "stdlib limits");
	StdLimits.reserved = 1;
	Ok &= Check(vs_interop_session_set_stdlib_limits(S, &StdLimits) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "stdlib reserved field");
	StdLimits.reserved = 0;
	--StdLimits.struct_size;
	Ok &= Check(vs_interop_session_set_stdlib_limits(S, &StdLimits) == VS_INTEROP_STATUS_INVALID_ARGUMENT, "stdlib short structure");
	StdLimits.struct_size = sizeof(StdLimits);
	vs_interop_status ForeignStdStatus = VS_INTEROP_STATUS_OK;
	std::thread ForeignStd([&]
	{
		ForeignStdStatus = vs_interop_session_set_stdlib_limits(S, &StdLimits);
	});
	ForeignStd.join();
	Ok &= Check(ForeignStdStatus == VS_INTEROP_STATUS_WRONG_THREAD, "stdlib limits owner thread");
	StdLimits.max_text_bytes = StdLimits.max_read_bytes = StdLimits.max_split_parts = 0;
	Ok &= Check(vs_interop_session_set_stdlib_limits(S, &StdLimits) == VS_INTEROP_STATUS_OK, "stdlib zero removes extra limits");
	vs_interop_value *B = nullptr, *V = nullptr, *Number = nullptr, *Out = nullptr;
	vs_interop_value_create(&B);
	vs_interop_value_create(&V);
	vs_interop_value_create(&Number);
	vs_interop_value_create(&Out);
	if (!B || !V || !Number || !Out)
	{
		vs_interop_value_destroy(B);
		vs_interop_value_destroy(V);
		vs_interop_value_destroy(Number);
		vs_interop_value_destroy(Out);
		vs_interop_session_destroy(S);
		return 1;
	}
	Ok &= Check(vs_interop_memory_create_buffer(S, nullptr, 16, B) == VS_INTEROP_STATUS_OK, "buffer");
	Ok &= Check(vs_interop_value_set_uint64(Number, UINT64_MAX) == VS_INTEROP_STATUS_OK, "uint64 input");
	Ok &= Check(vs_interop_memory_write(S, nullptr, B, 1, VS_INTEROP_NATIVE_UINT64, VS_INTEROP_MEMORY_BIG,
	                Number) == VS_INTEROP_STATUS_OK,
	    "unaligned write");
	Ok &= Check(vs_interop_memory_read(S, nullptr, B, 1, VS_INTEROP_NATIVE_UINT64, VS_INTEROP_MEMORY_BIG,
	                Out) == VS_INTEROP_STATUS_OK,
	    "read");
	uint64_t N = 0;
	Ok &= Check(vs_interop_value_get_uint64(Out, &N) == VS_INTEROP_STATUS_OK && N == UINT64_MAX,
	    "uint64 width");
	const uint64_t Pattern = UINT64_C(0xFEDCBA9876543210);
	const uint8_t BigBytes[] = {0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
	const uint8_t LittleBytes[] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
	for (const auto Order : {VS_INTEROP_MEMORY_BIG, VS_INTEROP_MEMORY_LITTLE})
	{
		Ok &= Check(vs_interop_value_set_uint64(Number, Pattern) == VS_INTEROP_STATUS_OK && vs_interop_memory_write(S, nullptr, B, 1, VS_INTEROP_NATIVE_UINT64, Order, Number) == VS_INTEROP_STATUS_OK && vs_interop_memory_read(S, nullptr, B, 1, VS_INTEROP_NATIVE_UINT64, Order, Out) == VS_INTEROP_STATUS_OK && vs_interop_value_get_uint64(Out, &N) == VS_INTEROP_STATUS_OK && N == Pattern,
		    "nonuniform uint64 round trip");
		vs_interop_memory_lease Bytes{};
		Bytes.struct_size = sizeof(Bytes);
		const auto Acquired = vs_interop_memory_acquire_lease(S, nullptr, B, 0, &Bytes);
		Ok &= Check(Acquired == VS_INTEROP_STATUS_OK, "byte order read lease");
		if (Acquired == VS_INTEROP_STATUS_OK)
		{
			const auto* Expected = Order == VS_INTEROP_MEMORY_BIG ? BigBytes : LittleBytes;
			Ok &= Check(Bytes.size == 16 && Bytes.data && !Bytes.writable_data && Bytes.data[0] == 0 && std::memcmp(Bytes.data + 1, Expected, sizeof(BigBytes)) == 0,
			    "exact unaligned byte order");
			Ok &= Check(vs_interop_memory_release_lease(Bytes.token) == VS_INTEROP_STATUS_OK,
			    "byte order lease release");
		}
	}
	Ok &= Check(vs_interop_memory_slice(S, nullptr, B, 0, 8, 1, V) == VS_INTEROP_STATUS_OK, "readonly slice");
	Ok &= Check(
	    vs_interop_memory_fill(S, nullptr, V, 0, 0, 1) == VS_INTEROP_STATUS_READ_ONLY, "readonly status");
	vs_interop_memory_lease Lease{};
	Lease.struct_size = sizeof(Lease);
	Ok &= Check(vs_interop_memory_acquire_lease(S, nullptr, B, 1, &Lease) == VS_INTEROP_STATUS_OK, "lease");
	Ok &= Check(vs_interop_memory_resize(S, nullptr, B, 17, 0) == VS_INTEROP_STATUS_BUSY, "lease conflict");
	if (Lease.writable_data)
	{
		Lease.writable_data[0] = 42;
	}
	std::thread T(
	    [&]
	{
		Ok &= Check(
		    vs_interop_memory_release_lease(Lease.token) == VS_INTEROP_STATUS_OK, "cross thread release");
	});
	T.join();
	Ok &= Check(vs_interop_memory_release_lease(Lease.token) == VS_INTEROP_STATUS_EXPIRED, "duplicate token");
	vs_interop_memory_stats Stats{};
	Stats.struct_size = sizeof(Stats);
	Ok &= Check(
	    vs_interop_memory_get_stats(S, nullptr, &Stats) == VS_INTEROP_STATUS_OK && Stats.owned_bytes == 16,
	    "stats");
	Ok &= Check(vs_interop_session_clear(S) == VS_INTEROP_STATUS_OK, "clear");
	Ok &= Check(vs_interop_memory_get_stats(S, nullptr, &Stats) == VS_INTEROP_STATUS_OK && Stats.owned_bytes == 16, "clear retains host buffer budget");
	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	const auto Source = Text(
	    "int main(){ auto b = new std::memory::buffer(8); b.set(0,42); assert(b.get(0)==42u); return 0; }");
	Ok &= Check(vs_interop_session_load_source(S, Source, Text("memory.vs"), &Result) == VS_INTEROP_STATUS_OK,
	    "load memory source");
	Ok &= Check(
	    vs_interop_session_run(S, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success,
	    "run memory source");
	int Releases = 0;
	uint8_t External[4]{};
	Ok &= Check(vs_interop_memory_wrap_external(
	                S, nullptr, External, 4, 0, &Releases,
	                [](void* P)
	{
		++*static_cast<int*>(P);
	},
	                Out) == VS_INTEROP_STATUS_OK,
	    "external");
	Lease = {};
	Lease.struct_size = sizeof(Lease);
	Ok &= Check(vs_interop_memory_acquire_lease(S, nullptr, Out, 0, &Lease) == VS_INTEROP_STATUS_OK,
	    "retained external lease");
	Ok &= Check(vs_interop_memory_invalidate_external(S, nullptr, Out) == VS_INTEROP_STATUS_BUSY,
	    "external lease excludes invalidate");
	vs_interop_value_destroy(Out);
	Out = nullptr;
	Ok &= Check(
	    vs_interop_session_destroy(S) == VS_INTEROP_STATUS_OK && Releases == 0, "lease survives session");
	S = nullptr;
	Ok &= Check(vs_interop_memory_release_lease(Lease.token) == VS_INTEROP_STATUS_OK && Releases == 1,
	    "external release once after session");
	vs_interop_value_destroy(B);
	vs_interop_value_destroy(V);
	vs_interop_value_destroy(Number);
	return Ok ? 0 : 1;
}
