// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#ifndef VEROSCRIPT_INTEROP_H
#define VEROSCRIPT_INTEROP_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define VS_INTEROP_CALL __cdecl
#if defined(VS_INTEROP_STATIC)
#define VS_INTEROP_API
#elif defined(VS_INTEROP_BUILD)
#define VS_INTEROP_API __declspec(dllexport)
#else
#define VS_INTEROP_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define VS_INTEROP_CALL
#define VS_INTEROP_API __attribute__((visibility("default")))
#else
#define VS_INTEROP_CALL
#define VS_INTEROP_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* ABI 0.1.1, encoded as (major << 16) | (minor << 8) | patch.
 * This release intentionally establishes a single clean ABI
 * baseline; callers must use the complete current vs_interop_config layout.
 */
#define VS_INTEROP_ABI_VERSION UINT32_C(0x00000101)

	typedef struct vs_interop_session vs_interop_session;
	typedef struct vs_interop_annotation_service vs_interop_annotation_service;
	typedef struct vs_interop_compile_processing_service vs_interop_compile_processing_service;
	typedef struct vs_interop_module_environment vs_interop_module_environment;

	typedef struct vs_interop_value vs_interop_value;
	typedef struct vs_interop_callable vs_interop_callable;
	typedef struct vs_interop_call_context vs_interop_call_context;
	typedef struct vs_interop_buffer vs_interop_buffer;
	typedef struct vs_interop_package vs_interop_package;
	typedef struct vs_interop_application_services vs_interop_application_services;
	typedef struct vs_interop_intercept_context vs_interop_intercept_context;
	typedef struct vs_interop_generic_type_context vs_interop_generic_type_context;
	typedef struct vs_interop_external_serialize_context vs_interop_external_serialize_context;
	typedef struct vs_interop_external_deserialize_context vs_interop_external_deserialize_context;

	typedef struct vs_interop_string_view
	{
		const char* data;
		uint64_t size;
	} vs_interop_string_view;

	typedef struct vs_interop_byte_view
	{
		const uint8_t* data;
		uint64_t size;
	} vs_interop_byte_view;

	typedef uint32_t vs_interop_status;
#define VS_INTEROP_STATUS_OK UINT32_C(0)
#define VS_INTEROP_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define VS_INTEROP_STATUS_OUT_OF_MEMORY UINT32_C(2)
#define VS_INTEROP_STATUS_WRONG_THREAD UINT32_C(3)
#define VS_INTEROP_STATUS_ENGINE_ERROR UINT32_C(4)
#define VS_INTEROP_STATUS_TYPE_MISMATCH UINT32_C(5)
#define VS_INTEROP_STATUS_BUFFER_TOO_SMALL UINT32_C(6)
#define VS_INTEROP_STATUS_CALLBACK_ERROR UINT32_C(7)
#define VS_INTEROP_STATUS_UNSUPPORTED UINT32_C(8)
#define VS_INTEROP_STATUS_INTERNAL_ERROR UINT32_C(9)
#define VS_INTEROP_STATUS_PERMISSION_DENIED UINT32_C(10)
#define VS_INTEROP_STATUS_EXPIRED UINT32_C(11)
#define VS_INTEROP_STATUS_READ_ONLY UINT32_C(12)
#define VS_INTEROP_STATUS_BUSY UINT32_C(13)
#define VS_INTEROP_STATUS_LIMIT_EXCEEDED UINT32_C(14)

/* Compile capabilities are independent of execution/backend capabilities. */
#define VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING UINT64_C(1)
#define VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS UINT64_C(2)
#define VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING UINT64_C(4)
	/* Native SDK services retain a frozen processor registry. Set on an idle
	 * creator-thread session; NULL clears it. ApplyConfig preserves the service.
	 * Processor code/data must survive the final service/session release. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_compile_processing_service_retain(
		const vs_interop_compile_processing_service* service, vs_interop_compile_processing_service** out_service);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_compile_processing_service_release(vs_interop_compile_processing_service* service);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_compile_processing_service(
		vs_interop_session* session, const vs_interop_compile_processing_service* service);
	typedef struct vs_interop_compile_extension_root
	{
		vs_interop_string_view path;
		vs_interop_string_view registration_entry;
	} vs_interop_compile_extension_root;
	typedef struct vs_interop_compile_configuration
	{
		vs_interop_string_view key;
		vs_interop_string_view value;
	} vs_interop_compile_configuration;
	typedef struct vs_interop_script_compile_options
	{
		uint32_t struct_size;
		uint8_t allow_source_imports;
		uint8_t reserved[3];
		const vs_interop_compile_extension_root* roots;
		uint64_t root_count;
		const vs_interop_compile_configuration* configuration;
		uint64_t configuration_count;
		uint64_t max_instructions;
		uint64_t timeout_milliseconds;
		const vs_interop_string_view* allowed_roots;
		uint64_t allowed_root_count;
	} vs_interop_script_compile_options;
	/* Copies all strings; NULL restores the core defaults. Creator thread and idle session. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_script_compile_options(
		vs_interop_session* session, const vs_interop_script_compile_options* options);
#define VS_INTEROP_DIAGNOSTIC_SEVERITY_ERROR UINT32_C(0)
#define VS_INTEROP_DIAGNOSTIC_SEVERITY_WARNING UINT32_C(1)
#define VS_INTEROP_DIAGNOSTIC_SEVERITY_INFORMATION UINT32_C(2)
#define VS_INTEROP_DIAGNOSTIC_SEVERITY_HINT UINT32_C(3)
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_get_compile_capabilities(void);
/* retain returns an independently owned handle, released once by its owner.
 * A session copies the configuration; the caller may release its service handle
 * after attaching it. ApplyConfig preserves it. Passing NULL clears it.
 * Set/query calls are creator-thread confined; setting requires an idle session.
 * Configuration reentry from service functor copy/destruction is rejected,
 * including while ApplyConfig prepares and releases its temporary configuration.
 * Native processor code and callback data must remain valid through their last use. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_annotation_service_retain(
		const vs_interop_annotation_service* service, vs_interop_annotation_service** out_service);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_annotation_service_release(vs_interop_annotation_service* service);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_annotation_service(
		vs_interop_session* session, const vs_interop_annotation_service* service);
/* Query severity separately to preserve the existing diagnostic struct layout.
 * A failed query leaves out_severity unchanged. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_diagnostic_severity(
		const vs_interop_session* session, uint64_t index, uint32_t* out_severity);

/* Feature discovery stays independent from the ABI version. */
#define VS_INTEROP_CAP_PACKAGES UINT64_C(0x00000001)
#define VS_INTEROP_CAP_HOT_RELOAD UINT64_C(0x00000002)
#define VS_INTEROP_CAP_HOST_BINDINGS UINT64_C(0x00000004)
#define VS_INTEROP_CAP_BINDING_JSON UINT64_C(0x00000008)
#define VS_INTEROP_CAP_REFLECTION_JSON UINT64_C(0x00000010)
#define VS_INTEROP_CAP_INSTANCE_INTERCEPT UINT64_C(0x00000020)
#define VS_INTEROP_CAP_MUTATION_CALLBACK UINT64_C(0x00000040)
#define VS_INTEROP_CAP_CONTAINER_VALUES UINT64_C(0x00000080)
#define VS_INTEROP_CAP_RETAINED_CALLABLES UINT64_C(0x00000100)
#define VS_INTEROP_CAP_GENERIC_HOST_TYPES UINT64_C(0x00000200)
#define VS_INTEROP_CAP_EXTERNAL_SERIALIZATION UINT64_C(0x00000400)
#define VS_INTEROP_CAP_JIT_DIAGNOSTICS UINT64_C(0x00000800)
#define VS_INTEROP_CAP_LOCALIZATION UINT64_C(0x00001000)
#define VS_INTEROP_CAP_DISTINCT_NUMERIC_PRIMITIVES UINT64_C(0x00002000)
#define VS_INTEROP_CAP_CONTAINER_ENUMERATION UINT64_C(0x00004000)
#define VS_INTEROP_CAP_EXECUTION_GOVERNANCE UINT64_C(0x00008000)
#define VS_INTEROP_CAP_SESSION_JIT_CACHE UINT64_C(0x00010000)
/* Retained values for older capability readers. The process query does not
 * set these bits; selected backend capabilities are session-local below. */
#define VS_INTEROP_CAP_NATIVE_JIT UINT64_C(0x00020000)
#define VS_INTEROP_CAP_NATIVE_TYPED_LOOPS UINT64_C(0x00040000)
#define VS_INTEROP_CAP_JIT_BACKEND_SELECTION UINT64_C(0x00080000)
#define VS_INTEROP_CAP_EVAL_ONCE UINT64_C(0x00100000)
#define VS_INTEROP_CAP_MEMORY UINT64_C(0x00200000)
#define VS_INTEROP_CAP_MODULE_ENVIRONMENT UINT64_C(0x00400000)
#define VS_INTEROP_CAP_NUMERICS UINT64_C(0x00800000)
#define VS_INTEROP_CAP_VARIANT UINT64_C(0x01000000)
#define VS_INTEROP_CAP_VM_BACKEND_SELECTION UINT64_C(0x02000000)
#define VS_INTEROP_CAP_RELOAD_STATUS UINT64_C(0x04000000)

/* Exact numeric values remain HOST_OBJECT values. The verified numeric kind is
 * 0 (other/unavailable), 1 (bigint), or 2 (rational). Text uses explicit UTF-8 lengths.
 * The kind query returns 0 for closed or foreign-thread script-owned values.
 * Numeric calls require exactly one context: an idle owner-thread session or a
 * borrowed native callback context. Outputs are unchanged on failure.
 * to_text writes an ordinary string value; convert takes VS_INTEROP_NATIVE_*
 * scalar types (integers/float/double), with exact_float 0 or 1.
 * Zero limits select finite Core defaults. This is not a whole-process heap cap. */
	typedef struct vs_interop_numeric_limits
	{
		uint32_t struct_size;
		uint32_t reserved;
		uint64_t max_value_bits, max_input_bytes, max_output_bytes, max_scratch_bytes, max_work_units;
	} vs_interop_numeric_limits;
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_numeric_limits(
		vs_interop_session* session, const vs_interop_numeric_limits* limits);
	VS_INTEROP_API uint32_t VS_INTEROP_CALL vs_interop_value_numeric_kind(const vs_interop_value* value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_numeric_parse(
		vs_interop_session* session, const vs_interop_call_context* call, uint32_t kind,
		vs_interop_string_view text, vs_interop_value* output);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_numeric_to_text(
		vs_interop_session* session, const vs_interop_call_context* call,
		const vs_interop_value* value, vs_interop_value* output);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_numeric_convert(
		vs_interop_session* session, const vs_interop_call_context* call,
		const vs_interop_value* value, uint32_t target_native_type, uint32_t exact_float,
		vs_interop_value* output);

/* Native SDK environments are immutable resolved snapshots. retain creates an
 * independently owned handle; release each handle once. Sessions retain the
 * snapshot after a successful configuration; releasing a handle does not detach it. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_module_environment_retain(
		const vs_interop_module_environment* environment, vs_interop_module_environment** out_environment);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_module_environment_release(vs_interop_module_environment* environment);

/* Standard-library side-effect permission bits for vs_interop_config. */
#define VS_INTEROP_SCRIPT_CAP_FILE_READ UINT32_C(0x00000001)
#define VS_INTEROP_SCRIPT_CAP_FILE_WRITE UINT32_C(0x00000002)
#define VS_INTEROP_SCRIPT_CAP_FILE_DELETE UINT32_C(0x00000004)
#define VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_READ UINT32_C(0x00000008)
#define VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_WRITE UINT32_C(0x00000010)
#define VS_INTEROP_SCRIPT_CAP_CONSOLE_INPUT UINT32_C(0x00000020)
#define VS_INTEROP_SCRIPT_CAP_CONSOLE_OUTPUT UINT32_C(0x00000040)
#define VS_INTEROP_SCRIPT_CAP_TASK_SPAWN UINT32_C(0x00000080)
/* chdir changes the host process working directory. It also requires
 * VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_WRITE when the policy is enabled. */
#define VS_INTEROP_SCRIPT_CAP_PROCESS_DIRECTORY_CHANGE UINT32_C(0x00000100)
#define VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS UINT32_C(0x00000200)
#define VS_INTEROP_SCRIPT_CAP_ALL                                                                            \
	(VS_INTEROP_SCRIPT_CAP_FILE_READ | VS_INTEROP_SCRIPT_CAP_FILE_WRITE |                                    \
		VS_INTEROP_SCRIPT_CAP_FILE_DELETE | VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_READ |                         \
		VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_WRITE | VS_INTEROP_SCRIPT_CAP_CONSOLE_INPUT |                      \
		VS_INTEROP_SCRIPT_CAP_CONSOLE_OUTPUT | VS_INTEROP_SCRIPT_CAP_TASK_SPAWN |                            \
		VS_INTEROP_SCRIPT_CAP_PROCESS_DIRECTORY_CHANGE | VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS)

	typedef uint32_t vs_interop_value_kind;
	/* Memory calls use exactly one context: an idle owner-thread session, or a
	 * borrowed native call context valid only during that callback. Values remain
	 * ordinary vs_interop_value handles. No addresses/tokens are serialized.
	 * Initialize struct_size before passing limits, stats or leases. Limits of
	 * zero are unlimited. Configuration/Run/Clear preserve retained accounting. */
	typedef struct vs_interop_memory_limits
	{
		uint32_t struct_size;
		uint32_t reserved;
		uint64_t max_buffer_bytes;
		uint64_t max_retained_bytes;
	} vs_interop_memory_limits;
	/* Operation-level standard-library limits. Zero adds no limit. This setter
	 * requires an idle session on its owner thread; active workers are retained
	 * until actual exit. All fields are validated before the configuration changes. */
	typedef struct vs_interop_stdlib_limits
	{
		uint32_t struct_size;
		uint32_t reserved;
		uint64_t max_text_bytes;
		uint64_t max_read_bytes;
		uint64_t max_split_parts;
	} vs_interop_stdlib_limits;
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_stdlib_limits(
		vs_interop_session* session, const vs_interop_stdlib_limits* limits);
	typedef struct vs_interop_memory_stats
	{
		uint32_t struct_size;
		uint32_t reserved;
		uint64_t owned_bytes;
		uint64_t borrowed_bytes;
		uint64_t peak_owned_bytes;
	} vs_interop_memory_stats;
	typedef struct vs_interop_memory_lease_token
	{
		uint64_t runtime;
		uint64_t serial;
	} vs_interop_memory_lease_token;
	typedef struct vs_interop_memory_lease
	{
		uint32_t struct_size;
		uint32_t reserved;
		const uint8_t* data;
		uint8_t* writable_data;
		uint64_t size;
		vs_interop_memory_lease_token token;
	} vs_interop_memory_lease;
	typedef void(VS_INTEROP_CALL* vs_interop_memory_release_callback)(void* user_data);
#define VS_INTEROP_MEMORY_LITTLE UINT32_C(0)
#define VS_INTEROP_MEMORY_BIG UINT32_C(1)
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_memory_limits(
		vs_interop_session* session, const vs_interop_memory_limits* limits);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_get_stats(
		vs_interop_session* session, const vs_interop_call_context* call, vs_interop_memory_stats* out_stats);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_create_buffer(
		vs_interop_session* session, const vs_interop_call_context* call, int64_t size,
		vs_interop_value* out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_create_view(
		vs_interop_session* session, const vs_interop_call_context* call, uint8_t read_only,
		vs_interop_value* out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_size(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* region, int64_t* out_size);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_capacity(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* buffer, int64_t* out_capacity);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_resize(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* buffer, int64_t size,
		uint8_t reserve_only);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_slice(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* region, int64_t offset, int64_t count,
		uint8_t read_only, vs_interop_value* out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_copy(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* destination, int64_t destination_offset,
		const vs_interop_value* source, int64_t source_offset, int64_t count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_fill(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* destination, int64_t offset,
		int64_t count, uint8_t value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_equal(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* left, int64_t left_offset,
		const vs_interop_value* right, int64_t right_offset, int64_t count, uint8_t* out_equal);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_clone(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* source, vs_interop_value* out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_from_array(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* source, vs_interop_value* out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_to_array(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* source, vs_interop_value* out_value);
	/* type is a VS_INTEROP_NATIVE fixed-width numeric tag; bool/enums/objects reject. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_read(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* source, int64_t offset, uint32_t type,
		uint32_t byte_order, vs_interop_value* out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_write(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* destination, int64_t offset,
		uint32_t type, uint32_t byte_order, const vs_interop_value* value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_native_addresses_available(
		vs_interop_session* session, const vs_interop_call_context* call, uint8_t* out_available);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_object_id(vs_interop_session* session,
		const vs_interop_call_context* call, const vs_interop_value* value, uint64_t* out_id);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_object_address(
		vs_interop_session* session, const vs_interop_call_context* call, const vs_interop_value* value,
		uint64_t* out_address);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_data_address(
		vs_interop_session* session, const vs_interop_call_context* call, const vs_interop_value* value,
		uint64_t* out_address);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_call_argument_address(
		const vs_interop_call_context* call, uint64_t index, uint64_t* out_address);
	/* Owner is non-null and remains the caller's responsibility on failure. On
	 * success release(owner) runs once, on the final releasing thread, outside
	 * locks. It must not throw. Region length is fixed and may be invalidated. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_wrap_external(
		vs_interop_session* session, const vs_interop_call_context* call, void* data, uint64_t size,
		uint8_t read_only, void* owner, vs_interop_memory_release_callback release,
		vs_interop_value* out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_invalidate_external(
		vs_interop_session* session, const vs_interop_call_context* call, const vs_interop_value* region);
	/* On success the lease retains the region until its token is released.
	 * Failure leaves out_lease unchanged; release any existing lease before
	 * reusing that output structure. Pointer use requires native synchronization. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_acquire_lease(
		vs_interop_session* session, const vs_interop_call_context* call, const vs_interop_value* region,
		uint8_t write, vs_interop_memory_lease* out_lease);
	/* Release is thread safe and independent of Session lifetime. Synchronize
	 * all native use before calling; duplicate/foreign tokens fail. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_release_lease(
		vs_interop_memory_lease_token token);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_memory_mutation_version(
		vs_interop_session* session, const vs_interop_call_context* call, const vs_interop_value* region,
		uint64_t* out_version);
#define VS_INTEROP_VALUE_INVALID UINT32_C(0)
#define VS_INTEROP_VALUE_INT32 UINT32_C(1)
#define VS_INTEROP_VALUE_INT64 UINT32_C(2)
#define VS_INTEROP_VALUE_DOUBLE UINT32_C(3)
#define VS_INTEROP_VALUE_STRING UINT32_C(4)
#define VS_INTEROP_VALUE_BOOL UINT32_C(5)
#define VS_INTEROP_VALUE_OBJECT UINT32_C(6)
#define VS_INTEROP_VALUE_ARRAY UINT32_C(7)
#define VS_INTEROP_VALUE_UINT32 UINT32_C(8)
#define VS_INTEROP_VALUE_UINT64 UINT32_C(9)
#define VS_INTEROP_VALUE_WSTRING UINT32_C(10)
#define VS_INTEROP_VALUE_MAP UINT32_C(11)
#define VS_INTEROP_VALUE_SET UINT32_C(12)
#define VS_INTEROP_VALUE_QUEUE UINT32_C(13)
#define VS_INTEROP_VALUE_STACK UINT32_C(14)
#define VS_INTEROP_VALUE_ORDERED_MAP UINT32_C(15)
#define VS_INTEROP_VALUE_DEQUE UINT32_C(16)
#define VS_INTEROP_VALUE_CHANNEL UINT32_C(17)
#define VS_INTEROP_VALUE_FUNCTION UINT32_C(18)
#define VS_INTEROP_VALUE_ENUM UINT32_C(19)
#define VS_INTEROP_VALUE_HOST_OBJECT UINT32_C(20)
#define VS_INTEROP_VALUE_NATIVE_ARRAY UINT32_C(21)
#define VS_INTEROP_VALUE_FLOAT UINT32_C(22)
#define VS_INTEROP_VALUE_INT8 UINT32_C(23)
#define VS_INTEROP_VALUE_UINT8 UINT32_C(24)
#define VS_INTEROP_VALUE_INT16 UINT32_C(25)
#define VS_INTEROP_VALUE_UINT16 UINT32_C(26)
#define VS_INTEROP_VALUE_NULL UINT32_C(27)
#define VS_INTEROP_VALUE_MULTI_MAP UINT32_C(28)
#define VS_INTEROP_VALUE_ORDERED_MULTI_MAP UINT32_C(29)
#define VS_INTEROP_VALUE_PRIORITY_QUEUE UINT32_C(30)
#define VS_INTEROP_VALUE_MULTI_SET UINT32_C(31)
#define VS_INTEROP_VALUE_ORDERED_MULTI_SET UINT32_C(32)
#define VS_INTEROP_VALUE_VARIANT UINT32_C(33)

	typedef uint32_t vs_interop_native_type;
#define VS_INTEROP_NATIVE_VOID UINT32_C(0)
#define VS_INTEROP_NATIVE_INT32 UINT32_C(1)
#define VS_INTEROP_NATIVE_BOOL UINT32_C(2)
#define VS_INTEROP_NATIVE_DOUBLE UINT32_C(3)
#define VS_INTEROP_NATIVE_STRING UINT32_C(4)
#define VS_INTEROP_NATIVE_UINT32 UINT32_C(5)
#define VS_INTEROP_NATIVE_HOST_OBJECT UINT32_C(6)
#define VS_INTEROP_NATIVE_ARRAY UINT32_C(7)
#define VS_INTEROP_NATIVE_MAP UINT32_C(8)
#define VS_INTEROP_NATIVE_SET UINT32_C(9)
#define VS_INTEROP_NATIVE_INT64 UINT32_C(10)
#define VS_INTEROP_NATIVE_UINT64 UINT32_C(11)
#define VS_INTEROP_NATIVE_ENUM UINT32_C(12)
#define VS_INTEROP_NATIVE_QUEUE UINT32_C(13)
#define VS_INTEROP_NATIVE_STACK UINT32_C(14)
#define VS_INTEROP_NATIVE_ORDERED_MAP UINT32_C(15)
#define VS_INTEROP_NATIVE_DEQUE UINT32_C(16)
#define VS_INTEROP_NATIVE_CHANNEL UINT32_C(17)
#define VS_INTEROP_NATIVE_MULTI_MAP UINT32_C(18)
#define VS_INTEROP_NATIVE_ORDERED_MULTI_MAP UINT32_C(19)
#define VS_INTEROP_NATIVE_PRIORITY_QUEUE UINT32_C(20)
#define VS_INTEROP_NATIVE_MULTI_SET UINT32_C(21)
#define VS_INTEROP_NATIVE_ORDERED_MULTI_SET UINT32_C(22)
#define VS_INTEROP_NATIVE_FUNCTION UINT32_C(23)
#define VS_INTEROP_NATIVE_FLOAT UINT32_C(24)
#define VS_INTEROP_NATIVE_INT8 UINT32_C(25)
#define VS_INTEROP_NATIVE_UINT8 UINT32_C(26)
#define VS_INTEROP_NATIVE_INT16 UINT32_C(27)
#define VS_INTEROP_NATIVE_UINT16 UINT32_C(28)
#define VS_INTEROP_NATIVE_VARIANT UINT32_C(29)

/* Explicit dynamic-value boxing. type_name is a canonical semantic type from
 * session; it is never inferred from an object pointer. Output is preserved on
 * failure. An empty variant is distinct from a boxed typed null. Extracted values
 * are independent wrappers and retain the owner of module-dependent payloads.
 * Owner-bound values may only be accessed on that session's creator thread and
 * become invalid when it closes. destroy may retire a wrapper on any thread. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_variant_empty(vs_interop_value* value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_variant(
		vs_interop_session* session, vs_interop_value* value, vs_interop_string_view type_name, const vs_interop_value* payload);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_variant_has_value(const vs_interop_value* value, uint8_t* output);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_variant_get_type_name(const vs_interop_value* value, vs_interop_string_view* output);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_variant_get(const vs_interop_value* value, vs_interop_value* output);

#define VS_INTEROP_PARAM_REF UINT32_C(0x01)
#define VS_INTEROP_PARAM_CONST_REF UINT32_C(0x02)

	typedef uint32_t vs_interop_execution_backend;
#define VS_INTEROP_BACKEND_VM UINT32_C(0)
#define VS_INTEROP_BACKEND_MIXED UINT32_C(1)
#define VS_INTEROP_BACKEND_JIT UINT32_C(2)

	typedef uint32_t vs_interop_jit_backend;
#define VS_INTEROP_JIT_BACKEND_NONE UINT32_C(0)
#define VS_INTEROP_JIT_BACKEND_EXTERNAL UINT32_C(1)

/* Capability bits returned by vs_interop_jit_backend_info. */
#define VS_INTEROP_JIT_CAP_NATIVE_CODE UINT64_C(0x1)
#define VS_INTEROP_JIT_CAP_PORTABLE_EXECUTION UINT64_C(0x2)
#define VS_INTEROP_JIT_CAP_NATIVE_TYPED_LOOPS UINT64_C(0x4)
#define VS_INTEROP_JIT_CAP_DIRECT_SCALAR_CALLS UINT64_C(0x8)
#define VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION UINT64_C(0x10)

	typedef struct vs_interop_jit_backend_info
	{
		uint32_t struct_size;
		vs_interop_jit_backend backend;
		vs_interop_string_view id;
		vs_interop_string_view target;
		uint64_t capabilities;
		uint8_t native_code;
		uint8_t reserved[7];
	} vs_interop_jit_backend_info;

	/* Metadata views last until backend replacement or session destruction.
	 * Backend selection is independent of VM/Mixed/JIT execution policy.
	 * A file/directory or installed backend ID is explicit. "auto" resolves the
	 * supplied module environment; "none" or an empty path disables JIT.
	 * Failed loads preserve the current selection. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_backend_info(
		vs_interop_session* session, vs_interop_jit_backend_info* out_info);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_jit_backend(
		vs_interop_session* session, vs_interop_string_view path);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_backend(
		vs_interop_session* session, vs_interop_jit_backend* out_backend);

	/* The standard Interop product supplies its assembled static VM as fallback.
	 * An installed module environment may supply the automatic default. Custom builds
	 * may omit it with VEROSCRIPT_INTEROP_DEFAULT_VM=OFF. Selection is independent
	 * of VM/Mixed/JIT policy. Set on the idle creator thread before execution;
	 * "auto" resolves the environment/product fallback; IDs select installed backends;
	 * "none" or an empty path clears the selection. Failed changes preserve it. Once Core
	 * commits execution, selection remains frozen even after session_clear. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_vm_backend(
		vs_interop_session* session, vs_interop_string_view path);

#define VS_INTEROP_VM_SELECTED UINT32_C(1)
#define VS_INTEROP_VM_FROZEN UINT32_C(2)
/* Capability bits returned by vs_interop_vm_backend_info. */
#define VS_INTEROP_VM_CAP_BYTECODE UINT64_C(0x1)
#define VS_INTEROP_VM_CAP_GOVERNANCE UINT64_C(0x2)
#define VS_INTEROP_VM_CAP_DEBUG UINT64_C(0x4)
#define VS_INTEROP_VM_CAP_HOT_RELOAD UINT64_C(0x8)
#define VS_INTEROP_VM_CAP_CLEANUP UINT64_C(0x10)
	typedef struct vs_interop_vm_backend_info
	{
		uint32_t struct_size;
		uint32_t flags;
		vs_interop_string_view id;
		vs_interop_string_view version;
		vs_interop_string_view target;
		vs_interop_string_view library_path;
		uint64_t capabilities;
	} vs_interop_vm_backend_info;
	/* Creator thread. Initialize struct_size to sizeof(vs_interop_vm_backend_info).
	 * Failure preserves out_info. Borrowed text lasts until selection replacement
	 * or session destruction. A static selection has an empty library_path. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_vm_backend_info(
		vs_interop_session* session, vs_interop_vm_backend_info* out_info);

	typedef uint32_t vs_interop_run_mode;
#define VS_INTEROP_RUN_EVAL UINT32_C(0)
#define VS_INTEROP_RUN_SCRIPT UINT32_C(1)
/* Ordinary runs initialize once per call without invoking main. Unlike
 * incremental Eval, the initializer may use the selected JIT backend.
 * Hot-reload sessions retain initialized globals. Retained initialization
 * uses the VM; eligible execution uses the selected backend. */
#define VS_INTEROP_RUN_EVAL_ONCE UINT32_C(2)

	typedef uint32_t vs_interop_reload_state;
#define VS_INTEROP_RELOAD_NONE UINT32_C(0)
#define VS_INTEROP_RELOAD_REJECTED UINT32_C(1)
#define VS_INTEROP_RELOAD_PENDING UINT32_C(2)
#define VS_INTEROP_RELOAD_ROLLED_BACK UINT32_C(3)
#define VS_INTEROP_RELOAD_APPLIED UINT32_C(4)
#define VS_INTEROP_RELOAD_APPLIED_WITH_ERRORS UINT32_C(5)
#define VS_INTEROP_RELOAD_RESET_REQUIRED UINT32_C(6)

	typedef struct vs_interop_reload_status
	{
		uint32_t struct_size;
		vs_interop_reload_state state;
		uint64_t operation_id;
		uint64_t previous_generation;
		uint64_t candidate_generation;
		uint64_t current_generation;
		uint64_t committed_generation;
	} vs_interop_reload_status;

	typedef uint32_t vs_interop_package_kind;
#define VS_INTEROP_PACKAGE_APPLICATION UINT32_C(1)
#define VS_INTEROP_PACKAGE_LIBRARY UINT32_C(2)

	typedef struct vs_interop_package_metadata
	{
		vs_interop_string_view key;
		vs_interop_byte_view value;
	} vs_interop_package_metadata;

	typedef struct vs_interop_package_build_options
	{
		uint32_t struct_size;
		vs_interop_package_kind kind;
		vs_interop_string_view package_name;
		uint64_t package_version;
		const vs_interop_package_metadata* host_metadata;
		uint64_t host_metadata_count;
		uint8_t strip_debug_info;
		uint8_t reserved_u8[7];
	} vs_interop_package_build_options;

	/* Execution entry points initialize a valid output as a failure on ENGINE_ERROR.
 * Reentry also stores a session diagnostic when this optional output is NULL. */
	typedef struct vs_interop_run_result
	{
		uint32_t struct_size;
		/* Normal compilation/execution, independent of the script process code. */
		uint8_t success;
		/* Set only by exit(n); an int main return does not set this flag. */
		uint8_t exit_requested;
		uint8_t reserved_u8[2];
		/* On success: int main()/exit(n). On failure: the engine failure code. */
		int32_t exit_code;
		uint32_t diagnostic_count;
	} vs_interop_run_result;

	typedef struct vs_interop_diagnostic
	{
		uint32_t struct_size;
		vs_interop_string_view message;
		vs_interop_string_view message_id;
		vs_interop_string_view file_label;
		uint32_t line;
		uint32_t column;
	} vs_interop_diagnostic;

	typedef void (VS_INTEROP_CALL *vs_interop_line_callback)(
	vs_interop_string_view line,
	void* user_data);

	/* Terminal controls are opt-in and independent of line output callbacks.
	 * Flags: output terminal=1, color=2, cursor=4, clear=8. Dimensions are visible
	 * columns/rows, zero when unknown. query returns 1 on success, 0 on failure.
	 * Operations: clear=0, clear_line=1 (after=0/before=1/all=2), set_color=2
	 * (ANSI palette 0..15, default=-1), reset_style=3, move_cursor=4 (column,row,
	 * zero based), set_cursor_visible=5 (0/1). control returns 1 on success.
	 * Calls are synchronous, may occur concurrently on VM workers, and must not
	 * unwind across this ABI. user_data must survive session destruction/cleanup. */
	typedef struct vs_interop_console_info
	{
		uint32_t struct_size;
		uint32_t flags;
		int32_t width;
		int32_t height;
	} vs_interop_console_info;
#define VS_INTEROP_CONSOLE_OUTPUT_TERMINAL UINT32_C(1)
#define VS_INTEROP_CONSOLE_COLOR UINT32_C(2)
#define VS_INTEROP_CONSOLE_CURSOR UINT32_C(4)
#define VS_INTEROP_CONSOLE_CLEAR UINT32_C(8)
#define VS_INTEROP_CONSOLE_OP_CLEAR UINT32_C(0)
#define VS_INTEROP_CONSOLE_OP_CLEAR_LINE UINT32_C(1)
#define VS_INTEROP_CONSOLE_OP_SET_COLOR UINT32_C(2)
#define VS_INTEROP_CONSOLE_OP_RESET_STYLE UINT32_C(3)
#define VS_INTEROP_CONSOLE_OP_MOVE_CURSOR UINT32_C(4)
#define VS_INTEROP_CONSOLE_OP_SET_CURSOR_VISIBLE UINT32_C(5)
	typedef uint8_t (VS_INTEROP_CALL *vs_interop_console_query_callback)(
		vs_interop_console_info* out_info, void* user_data);
	typedef uint8_t (VS_INTEROP_CALL *vs_interop_console_control_callback)(
		uint32_t operation, int32_t first, int32_t second, void* user_data);
	/* Exact console::write text, without added newline. 1=success, 0=failure.
	 * Omit to retain the buffered print callback output behavior. */
	typedef uint8_t (VS_INTEROP_CALL *vs_interop_console_write_callback)(
		vs_interop_string_view text, void* user_data);

	/* Callbacks caused by script execution (line/native/interceptor/mutation and
 * host-object destruction) may run on VM worker threads and concurrently.
 * Adapters must attach/acquire their foreign runtime and synchronize user_data.
 * Only callback-context invocation APIs may be used for nested script calls;
 * ordinary session APIs retain their creator-thread contract. Generic factories
 * and external serialization callbacks run synchronously on the session owner. */

	typedef struct vs_interop_session_options
	{
		uint32_t struct_size;
		vs_interop_line_callback print_callback;
		vs_interop_line_callback diagnostic_callback;
		void* user_data;
		/* Optional trailing callbacks; the original options prefix remains valid.
		 * Only complete fields covered by struct_size are read. */
		vs_interop_console_query_callback console_query_callback;
		vs_interop_console_control_callback console_control_callback;
		vs_interop_console_write_callback console_write_callback;
	} vs_interop_session_options;

	typedef struct vs_interop_string_list
	{
		const vs_interop_string_view* values;
		uint64_t count;
	} vs_interop_string_list;

	typedef struct vs_interop_predefined_macro
	{
		uint32_t struct_size;
		vs_interop_string_view name;
		vs_interop_string_list parameters;
		vs_interop_string_view body;
		vs_interop_string_view source_name;
		uint8_t variadic;
		uint8_t reserved_u8[7];
	} vs_interop_predefined_macro;

	typedef struct vs_interop_source_overlay
	{
		vs_interop_string_view path;
		vs_interop_string_view source;
	} vs_interop_source_overlay;

	typedef struct vs_interop_config
	{
		uint32_t struct_size;
		vs_interop_string_list import_paths;
		vs_interop_string_list implicit_import_paths;
		vs_interop_string_list script_args;
		vs_interop_string_list defines;
		vs_interop_string_list stdlib_search_paths;
		const vs_interop_source_overlay* source_overlays;
		uint64_t source_overlay_count;
		vs_interop_string_view bytecode_cache_directory;
		uint64_t max_call_depth;
		vs_interop_execution_backend execution_backend;
		uint8_t use_bytecode_cache;
		uint8_t allow_circular_imports;
		uint8_t allow_project_wide_access;
		uint8_t reserved_u8;
		const vs_interop_predefined_macro* predefined_macros;
		uint64_t predefined_macro_count;
		/* Zero means unlimited. VM and eligible controlled JIT paths preserve
		 * the same instruction accounting and cooperative checks. */
		uint64_t max_instructions;
		uint64_t timeout_milliseconds;
		/* Only used when capability_policy_enabled is non-zero. */
		uint32_t allowed_script_capabilities;
		/* Applying a configuration with this set also resets a prior cancellation. */
		uint8_t enable_execution_control;
		uint8_t capability_policy_enabled;
		uint8_t reserved_u8_v2[2];
	} vs_interop_config;

	typedef struct vs_interop_jit_policy
	{
		uint32_t struct_size;
		uint32_t mixed_hot_call_threshold;
		uint64_t max_compile_attempts;
		uint64_t max_cached_entries;
		uint64_t eager_compile_budget;
		/* Optional trailing field: a 32-byte descriptor defaults to enabled. */
		uint8_t reuse_session_code;
		/* Governed Mixed defaults to VM; opt in only after measuring the target.
		 * Explicit JIT selection is independent. Unsupported or VM-preferred
		 * controlled entries retain Core's VM fallback and governance checks. */
		uint8_t enable_controlled_mixed;
		uint8_t reserved_u8[6];
	} vs_interop_jit_policy;

	typedef struct vs_interop_jit_cache_limits
	{
		uint32_t struct_size;
		uint32_t reserved_u32;
		/* Zero permits no retained payload of the corresponding kind. */
		uint64_t max_native_bytes;
		uint64_t max_retained_payload_bytes;
	} vs_interop_jit_cache_limits;

	typedef struct vs_interop_jit_cache_stats
	{
		uint32_t struct_size;
		uint32_t reserved_u32;
		/* Cumulative for the current reusable ordinary execution context. */
		uint64_t compile_attempts;
		uint64_t retained_payload_bytes;
		uint64_t native_bytes;
		uint8_t resource_limit_reached;
		uint8_t reserved_u8[7];
	} vs_interop_jit_cache_stats;

typedef struct vs_interop_aot_program vs_interop_aot_program;
/* Generated entries receive the invocation-scoped Core service interface.
 * Native entries are trusted SDK code and must not retain this pointer. */
typedef int (VS_INTEROP_CALL *vs_interop_aot_entry)(void*);
typedef struct vs_interop_aot_function
{
	uint16_t function_index;
	uint16_t reserved;
	uint32_t reserved2;
	vs_interop_aot_entry entry;
	uint64_t governance_capabilities;
} vs_interop_aot_function;
typedef struct vs_interop_aot_description
{
	uint32_t struct_size;
	/* Product version encoded as major:8/minor:8/patch:16; not a separate version. */
	uint32_t core_version;
	vs_interop_string_view program_digest;
	vs_interop_string_view native_bindings_digest;
	vs_interop_string_view service_abi;
	vs_interop_string_view core_implementation;
	vs_interop_string_view target;
} vs_interop_aot_description;

/* Copies metadata into a Core-owned handle. Generated code must remain linked
 * while any handle/session uses it. out_error is optional and owns its buffer. */
VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_aot_program_create(
	const vs_interop_aot_description*, const vs_interop_aot_function*, uint64_t,
	vs_interop_aot_program** out_program, vs_interop_buffer** out_error);
VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_aot_program_retain(
	const vs_interop_aot_program*, vs_interop_aot_program** out_program);
VS_INTEROP_API void VS_INTEROP_CALL vs_interop_aot_program_release(vs_interop_aot_program*);
/* Creator-thread, idle-only. NULL clears; the session retains its own owner.
 * ApplyConfig preserves this separate native service selection. */
VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_aot_program(
	vs_interop_session*, const vs_interop_aot_program*);


	typedef struct vs_interop_jit_stats
	{
		uint32_t struct_size;
		uint32_t reserved_u32;
		uint64_t compile_attempts;
		uint64_t cache_hits;
		uint64_t compiled_entries;
		uint64_t native_leaf_entries;
		uint64_t native_cfg_entries;
		uint64_t bridge_trampoline_entries;
		uint64_t linear_trampoline_entries;
		uint64_t vm_trampoline_entries;
		uint64_t unsupported_entries;
		uint64_t dispatch_attempts;
		uint64_t dispatches;
		uint64_t fallbacks;
		uint64_t failures;
		uint64_t cache_invalidations;
		uint64_t hotness_deferrals;
		uint64_t hot_functions;
		uint64_t compile_budget_skips;
		uint64_t cache_capacity_skips;
		uint64_t eager_compile_skips;
		uint64_t array_fast_path_pushes;
		uint64_t array_fast_path_gets;
		uint64_t array_fast_path_sets;
		uint64_t scalar_map_fast_path_gets;
		uint64_t scalar_map_fast_path_sets;
		uint64_t scalar_method_fast_path_i32;
		uint64_t scalar_method_fast_path_u32;
		uint64_t scalar_method_fast_path_f64;
		uint64_t scalar_function_fast_path_i32;
		uint64_t scalar_function_fast_path_u32;
		uint64_t scalar_function_fast_path_f64;
		uint64_t scalar_native_fast_path_calls;
		uint64_t inlined_call_sites;
		uint64_t linear_frame_reuses;
		uint64_t linear_scratch_reuses;
		uint64_t linear_scratch_allocations;
		uint64_t linear_argument_scratch_leases;
		uint64_t linear_argument_scratch_reuses;
		/* Optional trailing fields, as indicated by struct_size. */
		uint64_t scalar_method_fast_path_mixed;
		uint64_t scalar_function_fast_path_mixed;
		/* Generation-scoped NativeCfg calls that bypassed the VM/FValue bridge. */
		uint64_t direct_native_calls;
		/* Optional trailing fields: a 328-byte output omits these fields. */
		/* Subset of fallbacks deliberately selected by the backend cost model. */
		uint64_t cost_model_vm_fallbacks;
		/* Typed-loop kernels invoked through the native backend, including kernels
	 * that terminate by reporting a source-visible runtime failure. */
		uint64_t native_typed_loop_executions;
		uint64_t controlled_native_executions;
		uint64_t aot_executions;
		/* Actual executable entries, including error/control-transfer exits.
		 * Compilation counts are separate; direct calls and loops are nested events. */
		uint64_t native_leaf_executions;
		uint64_t native_cfg_executions;
		uint64_t bridge_executions;
		uint64_t linear_executions;
		uint64_t vm_trampoline_executions;
	} vs_interop_jit_stats;

	typedef struct vs_interop_native_type_desc
	{
		uint32_t struct_size;
		vs_interop_native_type type;
		/* Host/enum name, function signature, or legacy container element name. */
		vs_interop_string_view name;
		const struct vs_interop_native_type_desc* type_arguments;
		uint64_t type_argument_count;
	} vs_interop_native_type_desc;

	typedef struct vs_interop_native_param_desc
	{
		uint32_t struct_size;
		vs_interop_native_type_desc type;
		uint32_t flags;
	} vs_interop_native_param_desc;

	typedef uint32_t vs_interop_callback_result;
#define VS_INTEROP_CALLBACK_OK UINT32_C(0)
#define VS_INTEROP_CALLBACK_ERROR UINT32_C(1)

	typedef vs_interop_callback_result (VS_INTEROP_CALL *vs_interop_native_callback)(
	vs_interop_call_context* context,
	vs_interop_value* out_return,
	void* user_data);

	typedef uint32_t vs_interop_host_type_kind;
#define VS_INTEROP_HOST_ENUM UINT32_C(0)
#define VS_INTEROP_HOST_STRUCT UINT32_C(1)
#define VS_INTEROP_HOST_CLASS UINT32_C(2)

	typedef uint32_t vs_interop_host_member_kind;
#define VS_INTEROP_HOST_CONSTRUCTOR UINT32_C(0)
#define VS_INTEROP_HOST_METHOD UINT32_C(1)
#define VS_INTEROP_HOST_STATIC_METHOD UINT32_C(2)
#define VS_INTEROP_HOST_PROPERTY_GETTER UINT32_C(3)
#define VS_INTEROP_HOST_PROPERTY_SETTER UINT32_C(4)
#define VS_INTEROP_HOST_ENUM_MEMBER UINT32_C(5)
#define VS_INTEROP_HOST_FIELD UINT32_C(6)
#define VS_INTEROP_HOST_STATIC_CONSTANT UINT32_C(7)

	typedef struct vs_interop_annotation_argument
	{
		vs_interop_string_view name;
		vs_interop_string_view value;
		uint8_t has_value;
		uint8_t reserved_u8[7];
	} vs_interop_annotation_argument;

	typedef struct vs_interop_annotation
	{
		uint32_t struct_size;
		vs_interop_string_view name;
		const vs_interop_annotation_argument* arguments;
		uint64_t argument_count;
	} vs_interop_annotation;

	typedef struct vs_interop_annotation_list
	{
		const vs_interop_annotation* values;
		uint64_t count;
	} vs_interop_annotation_list;

	/* Tooling vocabulary for one supported annotation argument. This is distinct
 * from vs_interop_annotation_argument, which describes an argument actually
 * attached to a declaration. */
	typedef struct vs_interop_annotation_argument_desc
	{
		uint32_t struct_size;
		vs_interop_string_view name;
		vs_interop_string_view detail;
		vs_interop_string_view documentation;
	} vs_interop_annotation_argument_desc;

	/* Zero keeps exact matching. IGNORE_CASE folds ASCII annotation names only;
	 * argument names and metadata keys remain exact. Unknown flags are rejected.
	 * Initialize struct_size to sizeof(vs_interop_annotation_desc). */
#define VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE UINT32_C(1)
	typedef struct vs_interop_annotation_desc
	{
		uint32_t struct_size;
		vs_interop_string_view name;
		vs_interop_string_view detail;
		vs_interop_string_view documentation;
		const vs_interop_annotation_argument_desc* arguments;
		uint64_t argument_count;
		uint32_t name_flags;
	} vs_interop_annotation_desc;

	/* Preferred free-function registration descriptor. The pointed-to strings,
 * arrays, and callback user_data remain owned by the caller; all declaration
 * metadata is copied synchronously before this call returns. */
	typedef struct vs_interop_host_function_desc
	{
		uint32_t struct_size;
		vs_interop_string_view name;
		vs_interop_native_type_desc return_type;
		const vs_interop_native_param_desc* parameters;
		uint64_t parameter_count;
		vs_interop_native_callback callback;
		void* user_data;
		vs_interop_string_view documentation;
		vs_interop_string_list parameter_names;
		vs_interop_string_list parameter_documentation;
		vs_interop_string_view return_documentation;
		vs_interop_annotation_list annotations;
		const vs_interop_annotation_list* parameter_annotations;
		uint64_t parameter_annotation_count;
		vs_interop_string_list type_parameters;
		vs_interop_string_view script_return_type_name;
		uint8_t is_generic_host_object_cast;
		uint8_t reserved_u8[7];
	} vs_interop_host_function_desc;

	typedef struct vs_interop_host_type_desc
	{
		uint32_t struct_size;
		vs_interop_string_view name;
		vs_interop_host_type_kind kind;
		vs_interop_string_view documentation;
		vs_interop_annotation_list annotations;
		uint8_t inheritable;
		uint8_t scoped_enum;
		uint8_t reserved_u8[6];
		/* Optional canonical enum storage spelling (for example "int64" or
	 * "unsigned int"). Empty uses the core default "int". */
		vs_interop_string_view enum_underlying_type_name;
	} vs_interop_host_type_desc;

	typedef struct vs_interop_host_member_desc
	{
		uint32_t struct_size;
		vs_interop_host_member_kind kind;
		vs_interop_string_view name;
		vs_interop_native_type_desc return_type;
		const vs_interop_native_param_desc* parameters;
		uint64_t parameter_count;
		vs_interop_native_callback callback;
		void* user_data;
		int64_t enum_value;
		vs_interop_string_view documentation;
		vs_interop_string_list parameter_names;
		vs_interop_string_list parameter_documentation;
		vs_interop_string_view return_documentation;
		vs_interop_annotation_list annotations;
		const vs_interop_annotation_list* parameter_annotations;
		uint64_t parameter_annotation_count;
		uint8_t is_operator;
		uint8_t is_const_method;
		uint8_t mutable_this;
		uint8_t reserved_u8[5];
	} vs_interop_host_member_desc;

	typedef struct vs_interop_host_type_protocol
	{
		uint32_t struct_size;
		vs_interop_native_callback clone_callback;
		void* clone_user_data;
		vs_interop_native_callback equals_callback;
		void* equals_user_data;
		vs_interop_native_callback hash_callback;
		void* hash_user_data;
		vs_interop_native_callback subclass_factory_callback;
		void* subclass_factory_user_data;
	} vs_interop_host_type_protocol;

	/* A generic factory is invoked synchronously for one concrete type argument
 * list. It fills context with a type, members, and optional value protocol.
 * Context and all borrowed argument views expire when the callback returns. */
	typedef vs_interop_callback_result (VS_INTEROP_CALL *vs_interop_generic_host_type_callback)(
	vs_interop_generic_type_context* context,
	void* user_data);

	/* Foreign callbacks must never unwind an exception across the C ABI boundary. */
	typedef void (VS_INTEROP_CALL *vs_interop_host_object_destroy)(void* payload);
	/* Returns nonzero when payload is compatible with expected_type_name. The
 * callback may be invoked by the VM during overload/type validation and must
 * not throw or retain expected_type_name. */
	typedef uint8_t (VS_INTEROP_CALL *vs_interop_host_object_is_type)(
	void* payload,
	void* user_data,
	vs_interop_string_view expected_type_name);

	typedef uint32_t vs_interop_intercept_result;
#define VS_INTEROP_INTERCEPT_UNHANDLED UINT32_C(0)
#define VS_INTEROP_INTERCEPT_HANDLED UINT32_C(1)
#define VS_INTEROP_INTERCEPT_FAILED UINT32_C(2)

	typedef struct vs_interop_resolved_method
	{
		uint32_t struct_size;
		uint64_t program_generation;
		uint64_t module_identity;
		uint64_t declaring_type_identity;
		uint64_t method_identity;
		uint32_t function_index;
		vs_interop_string_view module_name;
		vs_interop_string_view declaring_type_name;
		vs_interop_string_view method_name;
		vs_interop_string_view qualified_name;
		uint8_t is_virtual;
		uint8_t is_const;
		uint8_t returns_void;
		uint8_t reserved_u8;
	} vs_interop_resolved_method;

	typedef vs_interop_intercept_result (VS_INTEROP_CALL *vs_interop_instance_call_interceptor)(
	vs_interop_intercept_context* context,
	vs_interop_value* out_return,
	void* user_data);

	typedef uint32_t vs_interop_mutation_kind;
#define VS_INTEROP_MUTATION_FIELD_WRITE UINT32_C(0)
#define VS_INTEROP_MUTATION_CONTAINER_ADD UINT32_C(1)
#define VS_INTEROP_MUTATION_CONTAINER_REMOVE UINT32_C(2)
#define VS_INTEROP_MUTATION_CONTAINER_SET UINT32_C(3)
#define VS_INTEROP_MUTATION_CONTAINER_CLEAR UINT32_C(4)

	typedef struct vs_interop_mutation_event
	{
		uint32_t struct_size;
		const vs_interop_value* object;
		vs_interop_string_view object_type_name;
		vs_interop_string_view field_name;
		uint32_t field_index;
		vs_interop_mutation_kind kind;
		const vs_interop_value* value;
		uint64_t object_mutation_version;
	} vs_interop_mutation_event;

	typedef void (VS_INTEROP_CALL *vs_interop_mutation_callback)(
	const vs_interop_mutation_event* event,
	void* user_data);

	typedef vs_interop_callback_result (VS_INTEROP_CALL *vs_interop_external_serialize_callback)(
	vs_interop_external_serialize_context* context,
	void* user_data);
	typedef vs_interop_callback_result (VS_INTEROP_CALL *vs_interop_external_deserialize_callback)(
	vs_interop_external_deserialize_context* context,
	void* user_data);

	typedef uint32_t vs_interop_external_reference_kind;
#define VS_INTEROP_EXTERNAL_SCRIPT_OBJECT UINT32_C(0)
#define VS_INTEROP_EXTERNAL_HOST_OBJECT UINT32_C(1)
	typedef vs_interop_callback_result (VS_INTEROP_CALL *vs_interop_external_reference_visitor)(
	vs_interop_external_reference_kind kind,
	const vs_interop_value* value,
	void* user_data);

	/* Container visitors receive read-only values borrowed from a stable shallow
 * snapshot. Structural changes to the source do not invalidate the traversal;
 * nested handles retain normal reference semantics. Values expire when the
 * visitor returns, so clone any value that must outlive the callback. Returning
 * CALLBACK_ERROR stops enumeration. */
	typedef vs_interop_callback_result (VS_INTEROP_CALL *vs_interop_map_entry_visitor)(
	const vs_interop_value* key,
	const vs_interop_value* mapped_value,
	void* user_data);
	typedef vs_interop_callback_result (VS_INTEROP_CALL *vs_interop_set_element_visitor)(
	const vs_interop_value* element,
	void* user_data);

	VS_INTEROP_API uint32_t VS_INTEROP_CALL vs_interop_get_abi_version(void);
	/* Process-lifetime view; returns {NULL, 0} if lazy initialization fails.
	 * A later call may retry initialization. No exception crosses the C ABI. */
	VS_INTEROP_API vs_interop_string_view VS_INTEROP_CALL vs_interop_get_runtime_version(void);
	VS_INTEROP_API vs_interop_string_view VS_INTEROP_CALL vs_interop_get_host_platform(void);
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_get_capabilities(void);

	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_buffer_destroy(vs_interop_buffer* buffer);
	VS_INTEROP_API vs_interop_byte_view VS_INTEROP_CALL vs_interop_buffer_view(
	const vs_interop_buffer* buffer);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_buffer_copy(
	const vs_interop_buffer* buffer,
	uint8_t* destination,
	uint64_t destination_size,
	uint64_t* required_size);

	/* Localization is process-wide in the core and thread-safe. Language tags use
 * canonical culture names such as en-US and zh-CN. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_locale_set_language(
	vs_interop_string_view language_tag);
	VS_INTEROP_API vs_interop_string_view VS_INTEROP_CALL vs_interop_locale_get_language(void);
	/* Translate an English diagnostic through the currently loaded process catalogs.
	 * Unknown messages are copied unchanged. Input is borrowed for this call;
	 * output is module-owned UTF-8 (without a terminator), released with buffer_destroy.
	 * Uses normal locale initialization; mobile hosts load catalogs explicitly. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_locale_translate(
		vs_interop_string_view english_message,
		vs_interop_buffer** out_translation);
	/* UTF-8 JSON array of canonical tags. Destroy the returned buffer with
 * vs_interop_buffer_destroy. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_locale_get_available_languages(
	vs_interop_buffer** out_languages_json);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_locale_load_catalog_directory(
	vs_interop_string_view directory,
	vs_interop_buffer** out_error);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_locale_merge_catalog_directory(
	vs_interop_string_view directory,
	uint8_t override_existing,
	vs_interop_buffer** out_error);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_create(
	const vs_interop_session_options* options,
	vs_interop_session** out_session);
	/* Compilation/execution and configuration entry points reject session reentry,
 * including from host lifecycle callbacks before the VM begins execution.
 * Nested script work during a native callback uses call-context invocation.
 * Wrong-thread destruction is rejected without consuming session. */
	/* Synchronous finalization marks the session closed before running callbacks.
 * The handle cannot be used after destroy returns. If destruction is requested
 * inside an active call, callback data must survive until that call returns. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_destroy(
	vs_interop_session* session);
	/* Preserves the selected module environment. Invalid descriptors are rejected before application. Allocation/internal
 * failures during Core application may leave configuration partly applied;
 * retry a complete configuration before relying on its settings. Cancellation
 * remains bound to the control actually installed in the session. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_apply_config(
	vs_interop_session* session,
	const vs_interop_config* config);
	/* Applies the configuration and explicitly selects an environment; NULL removes
	 * it. Clear a loaded program before changing snapshots. A conflicting local
	 * binding catalog is rejected before any configuration/cancellation changes.
	 * Other thread, idle, failure and retry rules are the same as apply_config. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_apply_config_with_environment(
		vs_interop_session* session, const vs_interop_config* config,
		const vs_interop_module_environment* environment);
	/* Thread-safe cooperative cancellation for an execution-control-enabled session.
 * Does not asynchronously terminate host/native callbacks. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_request_cancel(
	vs_interop_session* session);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_load_source(
	vs_interop_session* session,
	vs_interop_string_view source,
	vs_interop_string_view file_label,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_load_file(
	vs_interop_session* session,
	vs_interop_string_view file_path,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_run(
	vs_interop_session* session,
	vs_interop_run_mode mode,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_clear(
	vs_interop_session* session);
	VS_INTEROP_API uint8_t VS_INTEROP_CALL vs_interop_session_is_loaded(
	const vs_interop_session* session);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_input(
	vs_interop_session* session,
	vs_interop_string_view utf8_input);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_hot_reload_enabled(
	vs_interop_session* session,
	uint8_t enabled);
	VS_INTEROP_API uint8_t VS_INTEROP_CALL vs_interop_session_is_hot_reload_enabled(
	const vs_interop_session* session);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_reload(
	vs_interop_session* session,
	vs_interop_run_result* out_result);
	/* Creator thread. Initialize struct_size to sizeof(vs_interop_reload_status).
	 * Failure preserves out_status. PENDING means initialization has yet to settle.
	 * APPLIED_WITH_ERRORS means the replacement committed before a later error.
	 * RESET_REQUIRED requires resetting the session before executing again.
	 * operation_id identifies a reload independently of program generations.
	 * A request blocked by live external values does not start an operation. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_reload_status(
	const vs_interop_session* session,
	vs_interop_reload_status* out_status);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_jit_policy(
	vs_interop_session* session,
	const vs_interop_jit_policy* policy);
	/* Requires VS_INTEROP_CAP_SESSION_JIT_CACHE and invalidates retained code. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_jit_cache_limits(
	vs_interop_session* session,
	const vs_interop_jit_cache_limits* limits);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_cache_limits(
	const vs_interop_session* session,
	vs_interop_jit_cache_limits* out_limits);
	/* Settled cumulative context statistics, distinct from last-run statistics. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_jit_cache_stats(
	const vs_interop_session* session,
	vs_interop_jit_cache_stats* out_stats);
	/* A zero struct_size is the legacy ABI form and writes only the original prefix.
 * Current callers must provide sizeof(vs_interop_jit_stats) to receive all fields. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_last_jit_stats(
	const vs_interop_session* session,
	vs_interop_jit_stats* out_stats);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_package_read(
	vs_interop_byte_view bytes,
	vs_interop_package** out_package,
	vs_interop_buffer** out_error_utf8);

	/* Optional Application service. Options are strict host JSON with absolute
	 * paths (cacheRoot, stdlibSearchPaths, cryptoLibrary, keyring and trust policy).
	 * The frozen environment and native module/key leases are retained by value.
	 * Destroying the service does not invalidate an already opened application. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_application_services_create(
		vs_interop_string_view options_json, const vs_interop_module_environment* environment,
		vs_interop_application_services** out_services, vs_interop_buffer** out_error_utf8);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_application_services_destroy(vs_interop_application_services* services);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_load_application(
		vs_interop_session* session, vs_interop_string_view absolute_path,
		const vs_interop_application_services* services, vs_interop_run_result* out_result);
	/* signatures_json is empty for an absent outer signature. A present signature
	 * uses the common record array. Content signatures remain inside the ZIP. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_application_read(
		vs_interop_byte_view bytes, vs_interop_string_view signatures_json,
		const vs_interop_application_services* services, vs_interop_package** out_package,
		vs_interop_buffer** out_error_utf8);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_package_destroy(vs_interop_package* package);
	VS_INTEROP_API vs_interop_package_kind VS_INTEROP_CALL vs_interop_package_get_kind(
	const vs_interop_package* package);
	VS_INTEROP_API vs_interop_string_view VS_INTEROP_CALL vs_interop_package_get_name(
	const vs_interop_package* package);
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_package_get_version(
	const vs_interop_package* package);
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_package_metadata_count(
	const vs_interop_package* package);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_package_get_metadata(
	const vs_interop_package* package,
	uint64_t index,
	vs_interop_package_metadata* out_metadata);
	VS_INTEROP_API vs_interop_byte_view VS_INTEROP_CALL vs_interop_package_get_canonical_signed_bytes(
	const vs_interop_package* package);
	/* Loading consumes the package module only on success; package metadata remains readable. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_load_package(
	vs_interop_session* session,
	vs_interop_package* package,
	vs_interop_string_view logical_path,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_reload_package(
	vs_interop_session* session,
	vs_interop_package* package,
	vs_interop_string_view logical_path,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_build_package_file(
	vs_interop_session* session,
	vs_interop_string_view source_path,
	const vs_interop_package_build_options* options,
	vs_interop_buffer** out_package_bytes,
	vs_interop_run_result* out_result);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_call_function(
	vs_interop_session* session,
	vs_interop_string_view function_name,
	vs_interop_value* const* arguments,
	uint64_t argument_count,
	vs_interop_value* out_return,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_call_method(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_string_view method_name,
	vs_interop_value* const* arguments,
	uint64_t argument_count,
	vs_interop_value* out_return,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_call_method_bypassing_interceptor(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_string_view method_name,
	vs_interop_value* const* arguments,
	uint64_t argument_count,
	vs_interop_value* out_return,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_has_method(
	const vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_string_view method_name,
	uint32_t argument_count,
	uint8_t* out_has_method);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_has_function(
	const vs_interop_session* session,
	vs_interop_string_view function_name,
	uint32_t argument_count,
	uint8_t* out_has_function);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_construct_object(
	vs_interop_session* session,
	vs_interop_string_view type_name,
	vs_interop_value* const* arguments,
	uint64_t argument_count,
	vs_interop_value* out_object,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_field(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_string_view field_name,
	vs_interop_value* out_value,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_field(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_string_view field_name,
	const vs_interop_value* value,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_global(
	vs_interop_session* session,
	vs_interop_string_view global_name,
	vs_interop_value* out_value,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_global(
	vs_interop_session* session,
	vs_interop_string_view global_name,
	const vs_interop_value* value,
	vs_interop_run_result* out_result);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_native(
	vs_interop_session* session,
	vs_interop_string_view name,
	const vs_interop_native_type_desc* return_type,
	const vs_interop_native_param_desc* parameters,
	uint64_t parameter_count,
	vs_interop_native_callback callback,
	void* user_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_function(
	vs_interop_session* session,
	const vs_interop_host_function_desc* function);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_annotation(
	vs_interop_session* session,
	const vs_interop_annotation_desc* annotation);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_type(
	vs_interop_session* session,
	const vs_interop_host_type_desc* type,
	uint32_t* out_type_id);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_member(
	vs_interop_session* session,
	uint32_t type_id,
	const vs_interop_host_member_desc* member);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_host_protocol(
	vs_interop_session* session,
	uint32_t type_id,
	const vs_interop_host_type_protocol* protocol);
	/* Retain registered host-protocol user_data until its final stamped value is
	 * released. Ownership is transferred only on OK; release is called exactly
	 * once, possibly after session destruction and on another thread. Repeated
	 * retention of the same user_data is rejected without consuming ownership.
	 * Later protocol registrations using that user_data share the existing owner. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_retain_callback_user_data(
	vs_interop_session* session, void* user_data, vs_interop_host_object_destroy release);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_generic_host_type_template(
	vs_interop_session* session,
	vs_interop_string_view name,
	uint32_t arity,
	vs_interop_string_view documentation,
	vs_interop_generic_host_type_callback callback,
	void* user_data,
	uint32_t* out_template_id);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_instantiate_generic_host_type(
	vs_interop_session* session,
	uint32_t template_id,
	vs_interop_string_list type_arguments,
	uint32_t* out_type_id);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_type_alias(
	vs_interop_session* session,
	vs_interop_string_view name,
	vs_interop_string_list type_parameters,
	vs_interop_string_view target_type_name,
	vs_interop_string_view documentation);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_export_bindings_json(
	vs_interop_session* session,
	vs_interop_buffer** out_json);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_export_reflection_json(
	vs_interop_session* session,
	vs_interop_buffer** out_json);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_instance_call_interceptor(
	vs_interop_session* session,
	vs_interop_instance_call_interceptor interceptor,
	void* user_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_set_mutation_callback(
	vs_interop_session* session,
	vs_interop_mutation_callback callback,
	void* user_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_apply_class_defaults(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_run_result* out_result);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_register_external_serializer(
	vs_interop_session* session,
	vs_interop_string_view type_name,
	uint32_t version,
	vs_interop_external_serialize_callback serialize_callback,
	vs_interop_external_deserialize_callback deserialize_callback,
	void* user_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_serialize_object(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_buffer** out_bytes,
	uint32_t* out_version);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_deserialize_object(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_byte_view bytes,
	uint32_t encoded_version);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_enumerate_object_references(
	vs_interop_session* session,
	const vs_interop_value* object,
	vs_interop_external_reference_visitor visitor,
	void* user_data);

	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_session_diagnostic_count(
	const vs_interop_session* session);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_diagnostic(
	const vs_interop_session* session,
	uint64_t index,
	vs_interop_diagnostic* out_diagnostic);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_diagnostic_argument_count(
	const vs_interop_session* session,
	uint64_t diagnostic_index,
	uint64_t* out_count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_get_diagnostic_argument(
	const vs_interop_session* session,
	uint64_t diagnostic_index,
	uint64_t argument_index,
	vs_interop_string_view* out_argument);
	/* Format a stored structured diagnostic using the currently selected language. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_format_diagnostic(
	const vs_interop_session* session,
	uint64_t diagnostic_index,
	vs_interop_buffer** out_text);
	/* Two-call UTF-8 copy API. required_size includes the trailing NUL. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_copy_last_error(
	const vs_interop_session* session,
	char* buffer,
	uint64_t buffer_size,
	uint64_t* required_size);

	/* Module-dependent values are confined to their session's creator thread and
 * become unavailable when it closes. Mutable containers with module-value slots
 * retain this ownership even while empty; C-created containers bind when first
 * passed to a session. Shared aliases/clones retain their graph until released.
 * Worker callbacks may export only independent owned values; empty containers
 * with variant/function slots cannot be exported or supplied as external storage. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create(
	vs_interop_value** out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_clone(
	const vs_interop_value* value,
	vs_interop_value** out_value);
	/* Copy the complete runtime value into a writable destination. This preserves
 * container/object identity semantics and is valid for owned handles and
 * mutable callback return/ref slots; read-only borrowed values are rejected. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_assign(
	vs_interop_value* destination,
	const vs_interop_value* source);
	/* Destroys only handles returned by value_create/value_clone. Callback values
 * are borrowed wrappers; passing one here is a safe no-op. */
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_value_destroy(vs_interop_value* value);
	/* Views and raw pointers returned by value getters are borrowed from value and
 * remain valid only until that value is mutated or destroyed. */
	VS_INTEROP_API vs_interop_value_kind VS_INTEROP_CALL vs_interop_value_get_kind(
	const vs_interop_value* value);
	/* Returned view is borrowed from value and is invalidated by mutation/destruction. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_type_name(
	const vs_interop_value* value,
	vs_interop_string_view* out_type_name);
	/* Reset clears an owned handle. Borrowed callback values, including mutable
 * ref/out arguments, always retain a valid core value and cannot be reset. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_reset(
	vs_interop_value* value);
	/* Represents script nullptr. A typed native call normalizes this to the
 * declared nullable object/reference ABI as needed. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_null(
	vs_interop_value* value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_is_null(
	const vs_interop_value* value,
	uint8_t* out_is_null);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_int32(vs_interop_value* value, int32_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_int32(const vs_interop_value* value, int32_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_uint32(vs_interop_value* value, uint32_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_uint32(const vs_interop_value* value, uint32_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_int64(vs_interop_value* value, int64_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_int64(const vs_interop_value* value, int64_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_uint64(vs_interop_value* value, uint64_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_uint64(const vs_interop_value* value, uint64_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_double(vs_interop_value* value, double data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_double(const vs_interop_value* value, double* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_float(vs_interop_value* value, float data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_float(const vs_interop_value* value, float* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_int8(vs_interop_value* value, int8_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_int8(const vs_interop_value* value, int8_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_uint8(vs_interop_value* value, uint8_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_uint8(const vs_interop_value* value, uint8_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_int16(vs_interop_value* value, int16_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_int16(const vs_interop_value* value, int16_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_uint16(vs_interop_value* value, uint16_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_uint16(const vs_interop_value* value, uint16_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_bool(vs_interop_value* value, uint8_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_bool(const vs_interop_value* value, uint8_t* out_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_string(vs_interop_value* value, vs_interop_string_view data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_string(const vs_interop_value* value, vs_interop_string_view* out_data);
	/* Copies code units; data may be a subrange of this value's UTF-16 view. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_wstring(vs_interop_value* value, const uint16_t* data, uint64_t size);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_wstring(const vs_interop_value* value, const uint16_t** out_data, uint64_t* out_size);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_enum(vs_interop_value* value, vs_interop_string_view type_name, int64_t data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_enum(const vs_interop_value* value, vs_interop_string_view* out_type_name, int64_t* out_data);
	/* On success, destroy owns payload and runs exactly once when the final value
 * reference is released; on failure, ownership remains with the caller. The
 * callback may run on the thread releasing that reference and must not throw. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_host_object(
	vs_interop_value* value,
	uint32_t type_id,
	vs_interop_string_view type_name,
	void* payload,
	vs_interop_host_object_destroy destroy);
	/* Extended HostObject construction retaining the host runtime subtype protocol.
 * The legacy constructor is equivalent to this function with is_type=null. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_host_object_ex(
	vs_interop_value* value,
	uint32_t type_id,
	vs_interop_string_view type_name,
	void* payload,
	vs_interop_host_object_destroy destroy,
	vs_interop_host_object_is_type is_type,
	void* is_type_user_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_get_host_object(
	const vs_interop_value* value,
	uint32_t* out_type_id,
	vs_interop_string_view* out_type_name,
	void** out_payload);
	/* Array creation canonicalizes element_type_name and rejects mismatched elements.
 * Object slots accept script-derived objects implementing the declared base or
 * interface, using the source value's module metadata. Container declarations
 * remain invariant; this does not add numeric or container conversions.
 * Read-only sequence access supports native/dynamic arrays, queue, stack, and deque. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_array(
	vs_interop_value* value,
	vs_interop_string_view element_type_name,
	const vs_interop_value* const* elements,
	uint64_t element_count);
	/* Sequence construction accepts ARRAY, QUEUE, PRIORITY_QUEUE, STACK, or DEQUE.
 * Elements are front-to-back (arrays/deques/queues), bottom-to-top (stacks),
 * and arbitrary insertion order (priority queues, normalized by the VM). */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_sequence(
	vs_interop_value* value,
	vs_interop_value_kind kind,
	vs_interop_string_view element_type_name,
	const vs_interop_value* const* elements,
	uint64_t element_count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_sequence_count(
	const vs_interop_value* value,
	uint64_t* out_count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_sequence_get(
	const vs_interop_value* value,
	uint64_t index,
	vs_interop_value* out_element);
	/* Map construction uses normal script key semantics and is transactional. A
 * duplicate key replaces its value; ordered maps preserve first insertion order.
 * Unsupported builtin key declarations return INVALID_ARGUMENT even for empty
 * maps; a rejected construction does not replace value. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_map(
	vs_interop_value* value,
	vs_interop_string_view key_type_name,
	vs_interop_string_view value_type_name,
	uint8_t ordered,
	const vs_interop_value* const* keys,
	const vs_interop_value* const* values,
	uint64_t entry_count);
	/* General map construction. MULTI_MAP and ORDERED_MULTI_MAP preserve every
 * repeated key/value association; ordinary maps replace a duplicate key. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_map_kind(
	vs_interop_value* value,
	vs_interop_value_kind kind,
	vs_interop_string_view key_type_name,
	vs_interop_string_view value_type_name,
	const vs_interop_value* const* keys,
	const vs_interop_value* const* values,
	uint64_t entry_count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_map_count(
	const vs_interop_value* value,
	uint64_t* out_count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_map_get(
	const vs_interop_value* value,
	const vs_interop_value* key,
	vs_interop_value* out_mapped_value,
	uint8_t* out_found);
	/* ordered_map follows insertion order; map traversal order is unspecified. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_map_enumerate(
	const vs_interop_value* value,
	vs_interop_map_entry_visitor visitor,
	void* user_data);
	/* Set and multiset key declarations follow the map key contract, including
 * validation before constructing an empty container. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_set(
	vs_interop_value* value,
	vs_interop_string_view element_type_name,
	const vs_interop_value* const* elements,
	uint64_t element_count);
	/* General set construction. MULTI_SET variants preserve duplicate elements. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_set_kind(
	vs_interop_value* value,
	vs_interop_value_kind kind,
	vs_interop_string_view element_type_name,
	const vs_interop_value* const* elements,
	uint64_t element_count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_count(
	const vs_interop_value* value,
	uint64_t* out_count);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_contains(
	const vs_interop_value* value,
	const vs_interop_value* element,
	uint8_t* out_contains);
	/* Set traversal order is unspecified. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_set_enumerate(
	const vs_interop_value* value,
	vs_interop_set_element_visitor visitor,
	void* user_data);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_channel(
	vs_interop_value* value,
	vs_interop_string_view element_type_name);
	/* Capacity zero creates an unbounded channel. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_create_bounded_channel(
	vs_interop_value* value,
	vs_interop_string_view element_type_name,
	uint64_t capacity);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_send(
	const vs_interop_value* channel,
	const vs_interop_value* value,
	vs_interop_buffer** out_error_utf8);
	/* Non-blocking/timed send returns OK with out_sent=0 when full or closed. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_try_send(
	const vs_interop_value* channel,
	const vs_interop_value* value,
	uint8_t* out_sent);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_send_for(
	const vs_interop_value* channel,
	const vs_interop_value* value,
	uint64_t timeout_ms,
	uint8_t* out_sent);
	/* Receive blocks until a value arrives or the channel closes. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_receive(
	const vs_interop_value* channel,
	vs_interop_value* out_value,
	vs_interop_buffer** out_error_utf8);
	/* Non-blocking/timed receive leaves out_value unchanged when out_received=0. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_try_receive(
	const vs_interop_value* channel,
	vs_interop_value* out_value,
	uint8_t* out_received);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_receive_for(
	const vs_interop_value* channel,
	uint64_t timeout_ms,
	vs_interop_value* out_value,
	uint8_t* out_received);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_count(
	const vs_interop_value* channel,
	uint64_t* out_count);
	/* Capacity zero denotes an unbounded channel. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_capacity(
	const vs_interop_value* channel,
	uint64_t* out_capacity);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_is_closed(
	const vs_interop_value* channel,
	uint8_t* out_closed);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_close(
	const vs_interop_value* channel);

	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_generic_type_context_argument_count(
	const vs_interop_generic_type_context* context);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_get_argument(
	const vs_interop_generic_type_context* context,
	uint64_t index,
	vs_interop_string_view* out_argument);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_set_type(
	vs_interop_generic_type_context* context,
	const vs_interop_host_type_desc* type);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_add_member(
	vs_interop_generic_type_context* context,
	const vs_interop_host_member_desc* member);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_generic_type_context_set_protocol(
	vs_interop_generic_type_context* context,
	const vs_interop_host_type_protocol* protocol);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_generic_type_context_set_error(
	vs_interop_generic_type_context* context,
	vs_interop_string_view error);

	VS_INTEROP_API vs_interop_string_view VS_INTEROP_CALL vs_interop_external_serialize_context_type_name(
	const vs_interop_external_serialize_context* context);
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_external_serialize_context_mutation_version(
	const vs_interop_external_serialize_context* context);
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_external_serialize_context_field_count(
	const vs_interop_external_serialize_context* context);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_external_serialize_context_get_field(
	const vs_interop_external_serialize_context* context,
	uint64_t index,
	vs_interop_string_view* out_name,
	vs_interop_string_view* out_declared_type,
	vs_interop_string_view* out_runtime_type,
	uint32_t* out_field_index,
	const vs_interop_value** out_value);
	/* Copies bytes and replaces the accepted payload only on success. */
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_external_serialize_context_set_bytes(
	vs_interop_external_serialize_context* context,
	vs_interop_byte_view bytes);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_external_serialize_context_set_error(
	vs_interop_external_serialize_context* context,
	vs_interop_string_view error);
	VS_INTEROP_API vs_interop_byte_view VS_INTEROP_CALL vs_interop_external_deserialize_context_bytes(
	const vs_interop_external_deserialize_context* context);
	VS_INTEROP_API uint32_t VS_INTEROP_CALL vs_interop_external_deserialize_context_encoded_version(
	const vs_interop_external_deserialize_context* context);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_external_deserialize_context_add_field(
	vs_interop_external_deserialize_context* context,
	vs_interop_string_view field_name,
	const vs_interop_value* value);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_external_deserialize_context_set_error(
	vs_interop_external_deserialize_context* context,
	vs_interop_string_view error);

	VS_INTEROP_API const vs_interop_value* VS_INTEROP_CALL vs_interop_intercept_context_receiver(
	const vs_interop_intercept_context* context);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_intercept_context_get_target(
	const vs_interop_intercept_context* context,
	vs_interop_resolved_method* out_target);
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_intercept_context_argument_count(
	const vs_interop_intercept_context* context);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_intercept_context_get_argument(
	const vs_interop_intercept_context* context,
	uint64_t index,
	const vs_interop_value** out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_intercept_context_proceed(
	vs_interop_intercept_context* context,
	vs_interop_value* out_return);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_intercept_context_set_error(
	vs_interop_intercept_context* context,
	vs_interop_string_view error);

	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_session_retain_callable(
	vs_interop_session* session,
	const vs_interop_value* function_value,
	/* False permits cross-thread invoke only when the host serializes all session access. */
		uint8_t owner_thread_only,
	uint8_t weak_this,
	vs_interop_callable** out_callable);
	/* Destroy owner-thread-only callables on their owner thread for prompt release.
 * A foreign-thread finalizer may still discard the wrapper safely; the retained
 * core capture is then reclaimed when its session is destroyed. */
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_callable_destroy(vs_interop_callable* callable);
	VS_INTEROP_API uint8_t VS_INTEROP_CALL vs_interop_callable_is_valid(const vs_interop_callable* callable);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_callable_invoke(
	vs_interop_callable* callable,
	vs_interop_value* const* arguments,
	uint64_t argument_count,
	vs_interop_value* out_return,
	vs_interop_run_result* out_result);

	/* A native call context and every value borrowed from it are valid only for the
 * duration of the current vs_interop_native_callback invocation. */
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_call_context_argument_count(
	const vs_interop_call_context* context);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_call_context_get_argument(
	const vs_interop_call_context* context,
	uint64_t index,
	const vs_interop_value** out_value);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_call_context_get_mutable_argument(
	vs_interop_call_context* context,
	uint64_t index,
	vs_interop_value** out_value);
	VS_INTEROP_API void* VS_INTEROP_CALL vs_interop_call_context_host_this(
	const vs_interop_call_context* context);
	VS_INTEROP_API uint64_t VS_INTEROP_CALL vs_interop_call_context_program_generation(
	const vs_interop_call_context* context);
	VS_INTEROP_API void VS_INTEROP_CALL vs_interop_call_context_set_error(
	vs_interop_call_context* context,
	vs_interop_string_view error);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_call_context_invoke_function(
	vs_interop_call_context* context,
	vs_interop_string_view function_name,
	vs_interop_value* const* arguments,
	uint64_t argument_count,
	vs_interop_value* out_return);
	VS_INTEROP_API vs_interop_status VS_INTEROP_CALL vs_interop_call_context_invoke_callable(
	vs_interop_call_context* context,
	const vs_interop_value* function_value,
	vs_interop_value* const* arguments,
	uint64_t argument_count,
	vs_interop_value* out_return);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VEROSCRIPT_INTEROP_H */
