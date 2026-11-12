// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <string>

namespace
{
	thread_local bool bFailNextAllocation = false;
	thread_local int AllocationCountdown = -1;
	thread_local bool bNumberedAllocationFailed = false;
	bool bMemoryGuardContained = false;
	bool bSerializerFailureContained = false;

	vs_interop_string_view Text(const char* Value)
	{
		return {Value, std::strlen(Value)};
	}

	vs_interop_callback_result VS_INTEROP_CALL AllocationProbe(
	    vs_interop_call_context*, vs_interop_value* Out, void*)
	{
		return vs_interop_value_set_int32(Out, 42) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	vs_interop_callback_result VS_INTEROP_CALL AllocationArrayProbe(
	    vs_interop_call_context*, vs_interop_value* Out, void*)
	{
		vs_interop_value* Element = nullptr;
		if (vs_interop_value_create(&Element) != VS_INTEROP_STATUS_OK) return VS_INTEROP_CALLBACK_ERROR;
		const vs_interop_value* Elements[] = {Element};
		const bool Passed = vs_interop_value_set_int32(Element, 42) == VS_INTEROP_STATUS_OK && vs_interop_value_create_array(Out, Text("int"), Elements, 1) == VS_INTEROP_STATUS_OK;
		vs_interop_value_destroy(Element);
		return Passed ? VS_INTEROP_CALLBACK_OK : VS_INTEROP_CALLBACK_ERROR;
	}

	bool TestConfigurationCancellationAllocationFailure(const bool InitiallyEnabled)
	{
		for (int FailureIndex = 0; FailureIndex < 128; ++FailureIndex)
		{
			struct FCleanup
			{
				~FCleanup()
				{
					vs_interop_value_destroy(Value);
					vs_interop_session_destroy(Session);
				}

				vs_interop_session* Session = nullptr;

				vs_interop_value* Value = nullptr;
			} C;
			vs_interop_config Config{};
			Config.struct_size = sizeof(Config);
			Config.execution_backend = VS_INTEROP_BACKEND_VM;
			Config.enable_execution_control = InitiallyEnabled ? 1 : 0;
			vs_interop_run_result Result{};
			Result.struct_size = sizeof(Result);
			if (vs_interop_session_create(nullptr, &C.Session) != VS_INTEROP_STATUS_OK ||
			    vs_interop_value_create(&C.Value) != VS_INTEROP_STATUS_OK ||
			    vs_interop_session_apply_config(C.Session, &Config) != VS_INTEROP_STATUS_OK ||
			    vs_interop_session_load_source(C.Session, Text("int answer(){return 42;}"),
			        Text("config-cancel.vs"), &Result) != VS_INTEROP_STATUS_OK)
			{
				return false;
			}

			const std::string Directory(128, 'd'), OverlayPath(128, 'p'), OverlaySource(128, ' ');
			const auto SearchPath = Text(Directory.c_str());
			const vs_interop_source_overlay Overlay{Text(OverlayPath.c_str()), Text(OverlaySource.c_str())};
			Config.enable_execution_control = InitiallyEnabled ? 0 : 1;
			Config.stdlib_search_paths = {&SearchPath, 1};
			Config.source_overlays = &Overlay;
			Config.source_overlay_count = 1;
			bNumberedAllocationFailed = false;
			AllocationCountdown = FailureIndex;
			const auto Status = vs_interop_session_apply_config(C.Session, &Config);
			const bool FailedAllocation = bNumberedAllocationFailed;
			AllocationCountdown = -1;
			if (Status != (FailedAllocation ? VS_INTEROP_STATUS_OUT_OF_MEMORY : VS_INTEROP_STATUS_OK)) return false;
			// Core configuration copies can fail after changing its control token.
			// The public cancellation result must describe the actual installed token.
			const auto CancelStatus = vs_interop_session_request_cancel(C.Session);
			const auto CallStatus = vs_interop_session_call_function(C.Session, Text("answer"), nullptr, 0, C.Value, &Result);
			std::int32_t Answer = 0;
			const bool Consistent = CancelStatus == VS_INTEROP_STATUS_OK
			    ? CallStatus == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success
			    : CancelStatus == VS_INTEROP_STATUS_INVALID_ARGUMENT && CallStatus == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(C.Value, &Answer) == VS_INTEROP_STATUS_OK && Answer == 42;
			if (!Consistent)
			{
				std::fprintf(stderr, "configuration cancellation mismatch at allocation %d: config=%u cancel=%u call=%u\n",
				    FailureIndex, Status, CancelStatus, CallStatus);
				return false;
			}
			Config.enable_execution_control = 1;
			if (vs_interop_session_apply_config(C.Session, &Config) != VS_INTEROP_STATUS_OK ||
			    vs_interop_session_call_function(C.Session, Text("answer"), nullptr, 0, C.Value, &Result) !=
			        VS_INTEROP_STATUS_OK ||
			    vs_interop_session_request_cancel(C.Session) != VS_INTEROP_STATUS_OK ||
			    vs_interop_session_call_function(C.Session, Text("answer"), nullptr, 0, C.Value, &Result) !=
			        VS_INTEROP_STATUS_ENGINE_ERROR)
			{
				return false;
			}

			if (!FailedAllocation) return true;
		}
		return false;
	}

	bool TestProtocolOwnershipAllocationFailure()
	{
		for (int FailureIndex = 0; FailureIndex < 16; ++FailureIndex)
		{
			int Releases = 0;
			vs_interop_session* Session = nullptr;
			if (vs_interop_session_create(nullptr, &Session) != VS_INTEROP_STATUS_OK) return false;
			vs_interop_host_type_desc Type{};
			Type.struct_size = sizeof(Type);
			Type.name = Text("OwnershipProbe");
			Type.kind = VS_INTEROP_HOST_STRUCT;
			uint32_t TypeId = 0;
			vs_interop_host_type_protocol Protocol{};
			Protocol.struct_size = sizeof(Protocol);
			Protocol.clone_callback = [](vs_interop_call_context*, vs_interop_value*,
			                              void*) -> vs_interop_callback_result
			{
				return VS_INTEROP_CALLBACK_ERROR;
			};
			Protocol.clone_user_data = &Releases;
			bool Passed =
			    vs_interop_session_register_host_type(Session, &Type, &TypeId) == VS_INTEROP_STATUS_OK &&
			    vs_interop_session_register_host_protocol(Session, TypeId, &Protocol) == VS_INTEROP_STATUS_OK;
			const auto Release = +[](void* Data)
			{
				++*static_cast<int*>(Data);
			};
			AllocationCountdown = FailureIndex;
			bNumberedAllocationFailed = false;
			const auto Status = vs_interop_session_retain_callback_user_data(Session, &Releases, Release);
			const bool FailedAllocation = bNumberedAllocationFailed;
			AllocationCountdown = -1;
			Passed &= Releases == 0 && (FailedAllocation ? Status == VS_INTEROP_STATUS_OUT_OF_MEMORY && vs_interop_session_retain_callback_user_data(Session, &Releases, Release) == VS_INTEROP_STATUS_OK : Status == VS_INTEROP_STATUS_OK);
			vs_interop_session_destroy(Session);
			if (!Passed || Releases != 1) return false;
			if (!FailedAllocation) return true;
		}
		return false;
	}

	bool TestHostMemberAllocationRollback()
	{
		// Sweep allocation failures without relying on a specific allocator count.
		for (int FailureIndex = 0; FailureIndex < 1024; ++FailureIndex)
		{
			vs_interop_session* Session = nullptr;
			if (vs_interop_session_create(nullptr, &Session) != VS_INTEROP_STATUS_OK) return false;
			vs_interop_host_type_desc Type{};
			Type.struct_size = sizeof(Type);
			Type.name = Text("AllocationProbeType");
			Type.kind = VS_INTEROP_HOST_CLASS;
			uint32_t TypeId = 0;
			if (vs_interop_session_register_host_type(Session, &Type, &TypeId) != VS_INTEROP_STATUS_OK)
			{
				vs_interop_session_destroy(Session);
				return false;
			}
			vs_interop_host_member_desc Member{};
			Member.struct_size = sizeof(Member);
			Member.kind = VS_INTEROP_HOST_STATIC_METHOD;
			Member.name = Text("value");
			Member.return_type.struct_size = sizeof(Member.return_type);
			Member.return_type.type = VS_INTEROP_NATIVE_INT32;
			Member.callback = &AllocationProbe;
			bNumberedAllocationFailed = false;
			AllocationCountdown = FailureIndex;
			const auto Status = vs_interop_session_register_host_member(Session, TypeId, &Member);
			const bool FailedAllocation = bNumberedAllocationFailed;
			AllocationCountdown = -1;
			const auto Retry = Status == VS_INTEROP_STATUS_OK ? VS_INTEROP_STATUS_OK
			                                                  : vs_interop_session_register_host_member(Session, TypeId, &Member);
			vs_interop_run_result Result{};
			Result.struct_size = sizeof(Result);
			const bool Passed = Retry == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, Text("int main(){return AllocationProbeType::value();}"), Text("allocation-rollback.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42;
			vs_interop_session_destroy(Session);
			if (!Passed)
			{
				std::fprintf(stderr, "Host member allocation rollback failed: allocation=%d status=%u retry=%u\n",
				    FailureIndex, Status, Retry);
				return false;
			}
			if (!FailedAllocation) return Status == VS_INTEROP_STATUS_OK;
		}
		return false;
	}

	bool TestNativeRegistrationAllocationRollback(const bool RichMetadata, const bool ArrayReturn)
	{
		for (int FailureIndex = 0; FailureIndex < 2048; ++FailureIndex)
		{
			vs_interop_session* Session = nullptr;
			if (vs_interop_session_create(nullptr, &Session) != VS_INTEROP_STATUS_OK) return false;
			vs_interop_native_type_desc Return{};
			Return.struct_size = sizeof(Return);
			Return.type = VS_INTEROP_NATIVE_INT32;
			const vs_interop_native_type_desc Element = Return;
			if (ArrayReturn)
			{
				Return.type = VS_INTEROP_NATIVE_ARRAY;
				Return.type_arguments = &Element;
				Return.type_argument_count = 1;
			}
			vs_interop_host_function_desc Function{};
			Function.struct_size = sizeof(Function);
			Function.name = Text("allocation_probe");
			Function.return_type = Return;
			Function.callback = ArrayReturn ? &AllocationArrayProbe : &AllocationProbe;
			const auto Register = [&]
			{
				return RichMetadata
				    ? vs_interop_session_register_host_function(Session, &Function)
				    : vs_interop_session_register_native(Session, Function.name, &Return,
				          nullptr, 0, Function.callback, nullptr);
			};
			bNumberedAllocationFailed = false;
			AllocationCountdown = FailureIndex;
			const auto Status = Register();
			const bool FailedAllocation = bNumberedAllocationFailed;
			AllocationCountdown = -1;
			bool Passed = true;
			if (Status != VS_INTEROP_STATUS_OK)
			{
				vs_interop_buffer* Json = nullptr;
				Passed = FailedAllocation && Status == VS_INTEROP_STATUS_OUT_OF_MEMORY && vs_interop_session_export_bindings_json(Session, &Json) == VS_INTEROP_STATUS_OK;
				if (Passed)
				{
					const auto Bytes = vs_interop_buffer_view(Json);
					const std::string Metadata(reinterpret_cast<const char*>(Bytes.data),
					    static_cast<std::size_t>(Bytes.size));
					Passed = Metadata.find("allocation_probe") == std::string::npos;
				}
				vs_interop_buffer_destroy(Json);
				Passed = Passed && Register() == VS_INTEROP_STATUS_OK;
			}
			vs_interop_run_result Result{};
			Result.struct_size = sizeof(Result);
			Passed = Passed && vs_interop_session_load_source(Session, Text(ArrayReturn ? "int main(){return allocation_probe()[0];}" : "int main(){return allocation_probe();}"), Text("native-allocation-rollback.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42;
			vs_interop_session_destroy(Session);
			if (!Passed)
			{
				std::fprintf(stderr, "Native registration allocation rollback failed: rich=%d array=%d allocation=%d status=%u\n",
				    RichMetadata, ArrayReturn, FailureIndex, Status);
				return false;
			}
			if (!FailedAllocation) return Status == VS_INTEROP_STATUS_OK;
		}
		return false;
	}

	vs_interop_callback_result VS_INTEROP_CALL TestMemoryGuard(
	    vs_interop_call_context*, vs_interop_value* OutReturn, void* UserData)
	{
		auto* Session = static_cast<vs_interop_session*>(UserData);
		vs_interop_memory_stats Stats{};
		Stats.struct_size = sizeof(Stats);
		Stats.owned_bytes = 123;
		vs_interop_status Status = VS_INTEROP_STATUS_OK;
		bool bExceptionEscaped = false;
		bFailNextAllocation = true;
		try
		{
			Status = vs_interop_memory_get_stats(Session, nullptr, &Stats);
		}
		catch (...)
		{
			bExceptionEscaped = true;
		}
		const bool bAllocationFailed = !bFailNextAllocation;
		bFailNextAllocation = false;
		// RequireIdleSession reports INTERNAL_ERROR if its diagnostic allocation
		// fails. Memory calls must preserve that status rather than mask it as BUSY.
		bMemoryGuardContained = bAllocationFailed && !bExceptionEscaped && Status == VS_INTEROP_STATUS_INTERNAL_ERROR && Stats.owned_bytes == 123 && vs_interop_memory_get_stats(Session, nullptr, &Stats) == VS_INTEROP_STATUS_BUSY && Stats.owned_bytes == 123;
		return vs_interop_value_set_int32(OutReturn, 42) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	vs_interop_callback_result VS_INTEROP_CALL TestSerializerFailure(
	    vs_interop_external_serialize_context* Context, void*)
	{
		const uint8_t Original[] = {4, 3, 2, 1};
		const uint8_t Replacement[256]{};
		if (vs_interop_external_serialize_context_set_bytes(Context, {Original, sizeof(Original)}) !=
		    VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		bFailNextAllocation = true;
		const auto Status = vs_interop_external_serialize_context_set_bytes(
		    Context, {Replacement, sizeof(Replacement)});
		bSerializerFailureContained = !bFailNextAllocation && Status == VS_INTEROP_STATUS_OUT_OF_MEMORY;
		bFailNextAllocation = false;
		// Keep the previously accepted payload when a replacement cannot allocate.
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL UnusedDeserializer(
	    vs_interop_external_deserialize_context*, void*)
	{
		return VS_INTEROP_CALLBACK_OK;
	}

	bool TestMultisetCount(vs_interop_session* Session)
	{
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		vs_interop_value* Value = nullptr;
		const bool bSetup = vs_interop_value_create(&Value) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, Text("multiset<int> unordered_set(){multiset<int> s={2,1,2,2};return s;}"
		                                                                                                                            "ordered_multiset<int> ordered_set(){ordered_multiset<int> s={2,1,2,2};return s;}"),
		                                                                                   Text("multiset-count.vs"), &Result) == VS_INTEROP_STATUS_OK;
		bool bPassed = bSetup;
		for (const char* Name : {"unordered_set", "ordered_set"})
		{
			if (!bPassed)
			{
				break;
			}
			bPassed = vs_interop_session_call_function(Session, Text(Name), nullptr, 0, Value, &Result) == VS_INTEROP_STATUS_OK;
			uint64_t Count = 0;
			bFailNextAllocation = true;
			const auto Status = vs_interop_value_set_count(Value, &Count);
			bPassed = bPassed && Status == VS_INTEROP_STATUS_OK && Count == 4 && bFailNextAllocation;
			bFailNextAllocation = false;
		}
		vs_interop_value_destroy(Value);
		return bPassed;
	}

	bool TestSerializerReplacement(vs_interop_session* Session)
	{
		vs_interop_value* Object = nullptr;
		vs_interop_buffer* Bytes = nullptr;
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		uint32_t Version = 0;
		const bool bSetup = vs_interop_value_create(&Object) == VS_INTEROP_STATUS_OK && vs_interop_session_register_external_serializer(Session, Text("Serializable"), 1, &TestSerializerFailure, &UnusedDeserializer, nullptr) == VS_INTEROP_STATUS_OK && vs_interop_session_load_source(Session, Text("class Serializable {public: int value; Serializable(){value=42;}};"), Text("serializer-replacement.vs"), &Result) == VS_INTEROP_STATUS_OK && vs_interop_session_construct_object(Session, Text("Serializable"), nullptr, 0, Object, &Result) == VS_INTEROP_STATUS_OK;
		bool bPassed = bSetup && vs_interop_session_serialize_object(Session, Object, &Bytes, &Version) == VS_INTEROP_STATUS_OK && Version == 1 && bSerializerFailureContained;
		const auto View = vs_interop_buffer_view(Bytes);
		const uint8_t Expected[] = {4, 3, 2, 1};
		bPassed = bPassed && View.size == sizeof(Expected) && View.data && std::memcmp(View.data, Expected, sizeof(Expected)) == 0;
		vs_interop_buffer_destroy(Bytes);
		vs_interop_value_destroy(Object);
		return bPassed;
	}

	bool TestUtf16AllocationFailure()
	{
		const uint16_t Units[] = {0x0041, 0xd83d, 0xde00, 0x0000, 0x4e2d, 0xd800, 0xffff};
		const uint64_t UnitCount = sizeof(Units) / sizeof(Units[0]);
		vs_interop_value* Value = nullptr;
		if (vs_interop_value_create(&Value) != VS_INTEROP_STATUS_OK) return false;
		bool bPassed = vs_interop_value_set_wstring(Value, Units, UnitCount) == VS_INTEROP_STATUS_OK;
		const uint16_t* Data = Units;
		uint64_t Size = 999;
		if (bPassed)
		{
			bool bExceptionEscaped = false;
			vs_interop_status Status = VS_INTEROP_STATUS_OK;
			bFailNextAllocation = true;
			try
			{
				Status = vs_interop_value_get_wstring(Value, &Data, &Size);
			}
			catch (...)
			{
				bExceptionEscaped = true;
			}
			const bool bAllocationFailed = !bFailNextAllocation;
			bFailNextAllocation = false;
			bPassed = bAllocationFailed && !bExceptionEscaped && Status == VS_INTEROP_STATUS_OUT_OF_MEMORY && Data == Units && Size == 999;
			// A failed cache allocation must preserve the original code units and
			// allow a normal retry, including embedded NUL and surrogate units.
			const bool bRetried = vs_interop_value_get_wstring(Value, &Data, &Size) == VS_INTEROP_STATUS_OK && Size == UnitCount && Data && std::memcmp(Data, Units, sizeof(Units)) == 0;
			const uint16_t* const Cached = Data;
			bFailNextAllocation = true;
			const bool bReused = vs_interop_value_get_wstring(Value, &Data, &Size) == VS_INTEROP_STATUS_OK && Data == Cached && Size == UnitCount && bFailNextAllocation;
			bFailNextAllocation = false;
			bPassed = bPassed && bRetried && bReused;
		}
		vs_interop_value_destroy(Value);
		return bPassed;
	}

	bool TestAotErrorAllocationFailure()
	{
		// Empty digests pass C descriptor validation, then Core rejects the
		// description and the ABI must allocate its optional error buffer.
		vs_interop_aot_description Description{};
		Description.struct_size = sizeof(Description);
		int FailedAllocations = 0;
		for (int FailureIndex = 0; FailureIndex < 32; ++FailureIndex)
		{
			vs_interop_aot_program* Program = nullptr;
			vs_interop_buffer* Error = nullptr;
			vs_interop_status Status = VS_INTEROP_STATUS_OK;
			bool bExceptionEscaped = false;
			bNumberedAllocationFailed = false;
			AllocationCountdown = FailureIndex;
			try
			{
				Status = vs_interop_aot_program_create(&Description, nullptr, 0, &Program, &Error);
			}
			catch (...)
			{
				bExceptionEscaped = true;
			}
			const bool bAllocationFailed = bNumberedAllocationFailed;
			AllocationCountdown = -1;
			const auto View = vs_interop_buffer_view(Error);
			const bool bPassed = !bExceptionEscaped && !Program && (bAllocationFailed ? Status == VS_INTEROP_STATUS_OUT_OF_MEMORY && !Error : Status == VS_INTEROP_STATUS_INVALID_ARGUMENT && Error && View.data && View.size != 0);
			vs_interop_buffer_destroy(Error);
			vs_interop_aot_program_release(Program);
			if (!bPassed) return false;
			if (!bAllocationFailed) return FailedAllocations >= 2;
			++FailedAllocations;
		}
		return false;
	}

	bool TestQueryNameAllocationFailure()
	{
		struct FCleanup
		{
			~FCleanup()
			{
				vs_interop_value_destroy(Object);
				vs_interop_session_destroy(Session);
			}
			vs_interop_session* Session = nullptr;
			vs_interop_value* Object = nullptr;
		} C;
		const std::string FunctionName(128, 'f'), MethodName(128, 'm');
		const std::string Source = "class QueryProbe {public: int " + MethodName + "(){return 7;}}; QueryProbe make(){return QueryProbe();} int " + FunctionName + "(){return 42;}";
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		if (vs_interop_session_create(nullptr, &C.Session) != VS_INTEROP_STATUS_OK || vs_interop_value_create(&C.Object) != VS_INTEROP_STATUS_OK || vs_interop_session_load_source(C.Session, Text(Source.c_str()), Text("query-allocation.vs"), &Result) != VS_INTEROP_STATUS_OK || !Result.success || vs_interop_session_call_function(C.Session, Text("make"), nullptr, 0, C.Object, &Result) != VS_INTEROP_STATUS_OK || !Result.success)
		{
			return false;
		}
		for (const bool bMethod : {false, true})
		{
			uint8_t Has = 1;
			vs_interop_status Status = VS_INTEROP_STATUS_OK;
			bool bExceptionEscaped = false;
			bFailNextAllocation = true;
			try
			{
				Status = bMethod
				    ? vs_interop_session_has_method(C.Session, C.Object, Text(MethodName.c_str()), 0, &Has)
				    : vs_interop_session_has_function(C.Session, Text(FunctionName.c_str()), 0, &Has);
			}
			catch (...)
			{
				bExceptionEscaped = true;
			}
			const bool bAllocationFailed = !bFailNextAllocation;
			bFailNextAllocation = false;
			if (!bAllocationFailed || bExceptionEscaped || Status != VS_INTEROP_STATUS_OUT_OF_MEMORY || Has != 0)
			{
				return false;
			}
			const auto Retry = bMethod
			    ? vs_interop_session_has_method(C.Session, C.Object, Text(MethodName.c_str()), 0, &Has)
			    : vs_interop_session_has_function(C.Session, Text(FunctionName.c_str()), 0, &Has);
			if (Retry != VS_INTEROP_STATUS_OK || Has != 1) return false;
		}
		return true;
	}
}

// ELF symbol interposition lets this executable exercise allocation failure
// inside the shared ABI without adding production test hooks to the library.
void* operator new(const std::size_t Size)
{
	if (bFailNextAllocation || AllocationCountdown == 0)
	{
		bNumberedAllocationFailed = AllocationCountdown == 0;
		AllocationCountdown = -1;
		bFailNextAllocation = false;
		throw std::bad_alloc{};
	}
	if (AllocationCountdown > 0)
	{
		--AllocationCountdown;
	}

	if (void* const Memory = std::malloc(Size == 0 ? 1 : Size)) return Memory;
	throw std::bad_alloc{};
}

void operator delete(void* const Memory) noexcept
{
	std::free(Memory);
}

void operator delete(void* const Memory, std::size_t) noexcept
{
	std::free(Memory);
}

int main()
{
	bool bVersionExceptionEscaped = false;
	vs_interop_string_view Version{};
	bFailNextAllocation = true;
	try
	{
		Version = vs_interop_get_runtime_version();
	}
	catch (...)
	{
		bVersionExceptionEscaped = true;
	}
	const bool bVersionContained = !bFailNextAllocation && !bVersionExceptionEscaped && !Version.data && Version.size == 0;
	bFailNextAllocation = false;
	const auto RetriedVersion = vs_interop_get_runtime_version();
	const bool bVersionRecovered = RetriedVersion.data && RetriedVersion.size > 0;
	vs_interop_session* Session = nullptr;
	if (vs_interop_session_create(nullptr, &Session) != VS_INTEROP_STATUS_OK) return EXIT_FAILURE;
	vs_interop_buffer* Output = nullptr;
	vs_interop_status Status = VS_INTEROP_STATUS_OK;
	bool bExceptionEscaped = false;
	bFailNextAllocation = true;
	try
	{
		Status = vs_interop_session_export_reflection_json(Session, &Output);
	}
	catch (...)
	{
		bExceptionEscaped = true;
	}
	bFailNextAllocation = false;
	const bool bContained = !bExceptionEscaped && Status == VS_INTEROP_STATUS_OUT_OF_MEMORY && Output == nullptr;
	vs_interop_buffer_destroy(Output);
	Output = nullptr;
	const bool bOrdinaryFailure = vs_interop_session_export_reflection_json(Session, &Output) == VS_INTEROP_STATUS_ENGINE_ERROR && Output == nullptr;
	vs_interop_buffer_destroy(Output);
	vs_interop_native_type_desc Return{};
	Return.struct_size = sizeof(Return);
	Return.type = VS_INTEROP_NATIVE_INT32;
	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	const bool bMemorySetup = vs_interop_session_register_native(Session, Text("memory_guard"),
	                              &Return, nullptr, 0, &TestMemoryGuard, Session) == VS_INTEROP_STATUS_OK &&
	    vs_interop_session_load_source(Session, Text("int main(){return memory_guard();}"),
	        Text("memory-guard.vs"), &Result) == VS_INTEROP_STATUS_OK;
	const bool bMemoryRun = bMemorySetup && vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success && Result.exit_code == 42 && bMemoryGuardContained;
	const bool bMultisetCount = TestMultisetCount(Session);
	const bool bSerializerReplacement = TestSerializerReplacement(Session);
	const bool bUtf16Allocation = TestUtf16AllocationFailure();
	const bool bAotErrorAllocation = TestAotErrorAllocationFailure();
	const bool bQueryNameAllocation = TestQueryNameAllocationFailure();
	const bool bHostMemberRollback = TestHostMemberAllocationRollback();
	const bool bNativeRollback = TestNativeRegistrationAllocationRollback(false, false) && TestNativeRegistrationAllocationRollback(true, false) && TestNativeRegistrationAllocationRollback(false, true) && TestNativeRegistrationAllocationRollback(true, true);
	const bool bProtocolOwnership = TestProtocolOwnershipAllocationFailure();
	const bool bConfigurationCancellation = TestConfigurationCancellationAllocationFailure(false) && TestConfigurationCancellationAllocationFailure(true);
	const bool bDestroyed = vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK;
	if (!bVersionContained || !bVersionRecovered || !bContained || !bOrdinaryFailure || !bMemoryRun || !bMultisetCount || !bSerializerReplacement || !bUtf16Allocation || !bHostMemberRollback || !bNativeRollback || !bProtocolOwnership || !bConfigurationCancellation || !bAotErrorAllocation || !bQueryNameAllocation || !bDestroyed)
	{
		std::fprintf(stderr, "C ABI failure containment regression: version=%d retry=%d reflection=%d ordinary=%d memory=%d multiset=%d serializer=%d utf16=%d member=%d native=%d protocol=%d config_cancel=%d aot_error=%d query_name=%d destroy=%d\n",
		    bVersionContained, bVersionRecovered, bContained, bOrdinaryFailure, bMemoryRun,
		    bMultisetCount, bSerializerReplacement, bUtf16Allocation, bHostMemberRollback, bNativeRollback,
		    bProtocolOwnership, bConfigurationCancellation, bAotErrorAllocation, bQueryNameAllocation, bDestroyed);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
