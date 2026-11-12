// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptWebAssemblyVersion.h"

#include <stdint.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define VERO_WEB_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define VERO_WEB_KEEPALIVE
#endif

#ifdef __cplusplus
extern "C"
{
#endif

	typedef void* VeroWebRuntimeHandle;

	typedef enum VeroWebExecutionBackend
	{
		VERO_WEB_BACKEND_VM = 0,
	VERO_WEB_BACKEND_MIXED = 1,
	VERO_WEB_BACKEND_JIT = 2,
	} VeroWebExecutionBackend;

	/* Memory handles belong to this runtime. All 64-bit arguments/results use Wasm bigint.
	   Operations: 1 buffer, 2 view, 3 slice, 4 resize/reserve, 5 copy, 6 fill,
	   7 equal, 8 clone, 9 size, 10 capacity, 11 object ID, 12 version,
	   13 lease, 14 destroy value, 15 limits (a/b = retained high/low, number = single),
	   16 stats, 17 scalar read, 18 scalar write (x = offset, y = scalar type,
	   z = byte order; write bits in number, read bits in result field 0).
	   Scalar types: int8/uint8/int16/uint16/int32/uint32/int64/uint64/float/double = 0..9.
	   Byte order: 0 little, 1 big. Signed integers use two's complement; floats use IEEE bits.
	   Status: 0 ok, 1 invalid argument, 2 out of memory, 3 wrong thread, 4 unsupported,
	   5 type mismatch, 6 permission denied, 7 expired, 8 read only, 9 busy,
	   10 limit exceeded, 11 interrupted.
	   Result fields: 0 value/number, 1 lease pointer, 2 lease length,
	   3 runtime token, 4 serial token; stats fields 0 owned, 1 borrowed, 2 peak.
	   Hosts wrap owned byte snapshots through leases; no arbitrary pointer registration. */
	VERO_WEB_KEEPALIVE uint32_t vero_web_memory_operation(VeroWebRuntimeHandle runtime, uint32_t operation,
		uint32_t a, uint32_t b, int64_t x, int64_t y, int64_t z, uint64_t number);
	VERO_WEB_KEEPALIVE uint64_t vero_web_memory_result(VeroWebRuntimeHandle runtime, uint32_t field);
	/* Idle runtime only; zero adds no operation-level limit. Applies to subsequent
	 * source runs and runs of existing compiled program handles. Status: 0/1/9. */
	VERO_WEB_KEEPALIVE uint32_t vero_web_set_stdlib_limits(VeroWebRuntimeHandle runtime,
		uint64_t max_text_bytes, uint64_t max_read_bytes, uint64_t max_split_parts);
	/* Valid after runtime destruction. Complete native work before release. */
	VERO_WEB_KEEPALIVE uint32_t vero_web_memory_release_lease(uint64_t runtime_token, uint64_t serial_token);

	// Version of this C ABI. It is independent from the language/bytecode version.
	VERO_WEB_KEEPALIVE const char* vero_web_abi_version(void);

	/**
 * Return 1 when this artifact was compiled with WebAssembly pthread support.
 * The default browser artifacts deliberately return 0 and require a Worker per
 * concurrently executing script instead of exposing script-level spawn.
 */
	VERO_WEB_KEEPALIVE uint32_t vero_web_supports_threads(void);

	/**
 * A Wasm module instance has a single controlling caller. Do not use its
 * runtime handles concurrently. While an Asyncify interactive call is awaiting
 * input, create/plain-text getters return null, text operations return an API
 * failure, numeric getters/program operations return 0, and destroy is ignored.
 * Memory operations return Busy (9), memory result getters return 0, and lease
 * release remains available. Retry stateful operations after the interactive call.
 */
	VERO_WEB_KEEPALIVE VeroWebRuntimeHandle vero_web_create(void);
	VERO_WEB_KEEPALIVE void vero_web_destroy(VeroWebRuntimeHandle runtime);

	/**
 * Select the diagnostic language for subsequent calls on this runtime.
 * Returns 1 on success and 0 for an invalid runtime or unsupported tag.
 */
	VERO_WEB_KEEPALIVE uint32_t vero_web_set_language(
	VeroWebRuntimeHandle runtime,
	const char* language_tag);

	// Canonical BCP 47 tag; the pointer remains valid until a successful
	// vero_web_set_language call or vero_web_destroy().
	VERO_WEB_KEEPALIVE const char* vero_web_get_language(VeroWebRuntimeHandle runtime);

	/**
 * Return a UTF-8 JSON string array containing the deployed catalog tags.
 * The pointer is owned by the runtime and remains valid until the next
 * vero_web_get_available_languages call or vero_web_destroy().
 */
	VERO_WEB_KEEPALIVE const char* vero_web_get_available_languages(
	VeroWebRuntimeHandle runtime);

	/**
 * Return UTF-8 JSON owned by runtime. The pointer remains valid until the next
 * run/check/compile/program call on that runtime or until vero_web_destroy(). Arguments may be
 * null; null source is reported as a structured API error.
 */
	VERO_WEB_KEEPALIVE const char* vero_web_run_source(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label,
	const char* input);

	/**
 * Run with arguments for `main(string[] args)`. `args` may be null only when
 * `arg_count` is zero. Strings are borrowed for the duration of this call.
 */
	VERO_WEB_KEEPALIVE const char* vero_web_run_source_with_args(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label,
	const char* input,
	const char* const* args,
	uint32_t arg_count);

	/**
 * Run using the requested backend policy. These modules select no JIT backend:
 * Vm and Mixed execute through the VM; Jit reports a structured missing-backend
 * diagnostic before script initialization. The backend IDs remain stable.
 */
	VERO_WEB_KEEPALIVE const char* vero_web_run_source_with_options(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label,
	const char* input,
	const char* const* args,
	uint32_t arg_count,
	uint32_t backend);

	/**
 * Run with in-memory virtual files. file_paths/file_sources contain file_count
 * borrowed UTF-8 pairs and may be null only when file_count is zero. Imports
 * resolve against these overlays without writing browser-visible files.
 */
	VERO_WEB_KEEPALIVE const char* vero_web_run_source_with_context(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label,
	const char* input,
	const char* const* args,
	uint32_t arg_count,
	uint32_t backend,
	const char* const* file_paths,
	const char* const* file_sources,
	uint32_t file_count);

	/**
 * Compile source and virtual files once. On success the JSON result contains a
 * non-zero programHandle owned by runtime. Release it with
 * vero_web_destroy_program. This avoids repeating parsing and bytecode
 * compilation for subsequent runs.
 */
	VERO_WEB_KEEPALIVE const char* vero_web_compile_source_with_context(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label,
	const char* const* file_paths,
	const char* const* file_sources,
	uint32_t file_count);

	// Run a previously compiled program with fresh execution state.
	VERO_WEB_KEEPALIVE const char* vero_web_run_program(
	VeroWebRuntimeHandle runtime,
	uint32_t program_handle,
	const char* input,
	const char* const* args,
	uint32_t arg_count,
	uint32_t backend);

	// Asyncify-only interactive counterpart of vero_web_run_program.
	// The interactive Wasm target exports this explicitly; do not mark it
	// KEEPALIVE because the fast target must not expose an unusable stub.
	const char* vero_web_run_program_interactive(
	VeroWebRuntimeHandle runtime,
	uint32_t program_handle,
	const char* const* args,
	uint32_t arg_count,
	uint32_t backend);

	// Releases a compiled program and reports whether the handle was valid.
	VERO_WEB_KEEPALIVE uint32_t vero_web_destroy_program(
	VeroWebRuntimeHandle runtime,
	uint32_t program_handle);

	/**
 * Asyncify-only interactive counterpart of vero_web_run_source_with_context.
 * The JavaScript SDK supplies each logical input line through its provider.
 */
	const char* vero_web_run_source_interactive_with_context(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label,
	const char* const* args,
	uint32_t arg_count,
	uint32_t backend,
	const char* const* file_paths,
	const char* const* file_sources,
	uint32_t file_count);

	VERO_WEB_KEEPALIVE const char* vero_web_check_source(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label);

	VERO_WEB_KEEPALIVE const char* vero_web_check_source_with_context(
	VeroWebRuntimeHandle runtime,
	const char* source,
	const char* file_label,
	const char* const* file_paths,
	const char* const* file_sources,
	uint32_t file_count);

#ifdef __cplusplus
}
#endif

#undef VERO_WEB_KEEPALIVE
