// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

/* External Interop SDK consumer example. */
#include "VeroScriptInterop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VEROSCRIPT_EXPECTED_RUNTIME_SEMVER
#define VEROSCRIPT_EXPECTED_RUNTIME_SEMVER "0.1.1"
#endif

static vs_interop_string_view view_of(const char* text)
{
	vs_interop_string_view view;
	view.data = text;
	view.size = (uint64_t)strlen(text);
	return view;
}

static void print_last_error(const vs_interop_session* session, const char* operation)
{
	uint64_t required_size = 0;
	char* message = NULL;

	(void)vs_interop_session_copy_last_error(session, NULL, 0, &required_size);
	if (required_size != 0)
	{
		message = (char*)malloc((size_t)required_size);
	}
	if (message != NULL
		&& vs_interop_session_copy_last_error(
			session, message, required_size, &required_size) == VS_INTEROP_STATUS_OK)
	{
		fprintf(stderr, "%s failed: %s\n", operation, message);
	}
	else
	{
		fprintf(stderr, "%s failed\n", operation);
	}
	free(message);
}

static int verify_memory(vs_interop_session* session)
{
	const uint64_t pattern = UINT64_C(0xFEDCBA9876543210);
	const uint8_t expected[] = {0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
	vs_interop_memory_limits limits = {0};
	vs_interop_memory_lease lease = {0};
	vs_interop_value* bytes = NULL;
	vs_interop_value* number = NULL;
	uint64_t read_back = 0;
	int success = 0;
	limits.struct_size = sizeof(limits);
	limits.max_buffer_bytes = 16;
	limits.max_retained_bytes = 32;
	lease.struct_size = sizeof(lease);
	if (vs_interop_session_set_memory_limits(session, &limits) != VS_INTEROP_STATUS_OK
		|| vs_interop_value_create(&bytes) != VS_INTEROP_STATUS_OK
		|| vs_interop_value_create(&number) != VS_INTEROP_STATUS_OK
		|| vs_interop_memory_create_buffer(session, NULL, 9, bytes) != VS_INTEROP_STATUS_OK
		|| vs_interop_value_set_uint64(number, pattern) != VS_INTEROP_STATUS_OK
		|| vs_interop_memory_write(session, NULL, bytes, 1, VS_INTEROP_NATIVE_UINT64,
			VS_INTEROP_MEMORY_BIG, number) != VS_INTEROP_STATUS_OK
		|| vs_interop_memory_read(session, NULL, bytes, 1, VS_INTEROP_NATIVE_UINT64,
			VS_INTEROP_MEMORY_BIG, number) != VS_INTEROP_STATUS_OK
		|| vs_interop_value_get_uint64(number, &read_back) != VS_INTEROP_STATUS_OK
		|| read_back != pattern
		|| vs_interop_memory_acquire_lease(session, NULL, bytes, 0, &lease) != VS_INTEROP_STATUS_OK)
	{
		print_last_error(session, "memory round trip");
		goto cleanup;
	}
	success = lease.size == 9 && lease.data != NULL && lease.writable_data == NULL
		&& lease.data[0] == 0 && memcmp(lease.data + 1, expected, sizeof(expected)) == 0;

cleanup:
	if (lease.token.serial != 0
		&& vs_interop_memory_release_lease(lease.token) != VS_INTEROP_STATUS_OK)
	{
		success = 0;
	}
	vs_interop_value_destroy(number);
	vs_interop_value_destroy(bytes);
	return success;
}

int main(int argc, char** argv)
{
	const vs_interop_string_view version = vs_interop_get_runtime_version();
	const size_t expected_version_size = strlen(VEROSCRIPT_EXPECTED_RUNTIME_SEMVER);
	const char* source =
		"int add_one(int value)\n"
		"{\n"
		"    return value + 1;\n"
		"}\n";
	vs_interop_session_options options = {0};
	vs_interop_run_result result = {0};
	vs_interop_jit_backend_info backend_info = {0};
	vs_interop_vm_backend_info vm_info = {0};
	const int use_jit = argc == 2;
	const int use_external_vm = argc == 3 && strcmp(argv[1], "--vm") == 0;
	vs_interop_session* session = NULL;
	vs_interop_value* argument = NULL;
	vs_interop_value* return_value = NULL;
	vs_interop_value* arguments[1];
	int32_t answer = 0;
	int exit_code = EXIT_FAILURE;

	if (vs_interop_get_abi_version() != VS_INTEROP_ABI_VERSION
		|| (vs_interop_get_capabilities() & (VS_INTEROP_CAP_EVAL_ONCE | VS_INTEROP_CAP_MEMORY))
			!= (VS_INTEROP_CAP_EVAL_ONCE | VS_INTEROP_CAP_MEMORY)
		|| version.data == NULL
		|| version.size < (uint64_t)expected_version_size
		|| memcmp(version.data, VEROSCRIPT_EXPECTED_RUNTIME_SEMVER, expected_version_size) != 0
		|| (version.size != (uint64_t)expected_version_size
			&& version.data[expected_version_size] != ' '))
	{
		fprintf(stderr, "VeroScript Interop ABI/runtime version mismatch\n");
		return EXIT_FAILURE;
	}
	if (argc != 1 && !use_jit && !use_external_vm)
	{
		fprintf(stderr, "Usage: %s [jit-backend-library-or-directory | --vm vm-backend-library-or-directory]\n", argv[0]);
		return EXIT_FAILURE;
	}

	options.struct_size = sizeof(options);
	result.struct_size = sizeof(result);
	if (vs_interop_session_create(&options, &session) != VS_INTEROP_STATUS_OK)
	{
		fprintf(stderr, "session creation failed\n");
		goto cleanup;
	}
	backend_info.struct_size = sizeof(backend_info);
	if (vs_interop_session_get_jit_backend_info(session, &backend_info)
			!= VS_INTEROP_STATUS_OK
		|| backend_info.backend != VS_INTEROP_JIT_BACKEND_NONE
		|| backend_info.native_code != 0 || backend_info.capabilities != 0)
	{
		fprintf(stderr, "A new session must not select an implicit JIT backend\n");
		goto cleanup;
	}
	vm_info.struct_size = sizeof(vm_info);
	if ((vs_interop_get_capabilities() & VS_INTEROP_CAP_VM_BACKEND_SELECTION) == 0
		|| vs_interop_session_get_vm_backend_info(session, &vm_info) != VS_INTEROP_STATUS_OK
		|| (vm_info.flags & VS_INTEROP_VM_FROZEN) != 0)
	{
		fprintf(stderr, "VM backend metadata is unavailable or already frozen\n");
		goto cleanup;
	}
	if (use_external_vm
		&& vs_interop_session_set_vm_backend(session, view_of(argv[2])) != VS_INTEROP_STATUS_OK)
	{
		print_last_error(session, "VM backend selection");
		goto cleanup;
	}
	if (use_jit)
	{
		vs_interop_config config = {0};
		config.struct_size = sizeof(config);
		config.execution_backend = VS_INTEROP_BACKEND_JIT;
		if (vs_interop_session_set_jit_backend(session, view_of(argv[1]))
				!= VS_INTEROP_STATUS_OK
			|| vs_interop_session_apply_config(session, &config) != VS_INTEROP_STATUS_OK)
		{
			print_last_error(session, "backend selection");
			goto cleanup;
		}
	}
	if (vs_interop_session_load_source(
			session, view_of(source), view_of("interop-minimal.vs"), &result)
			!= VS_INTEROP_STATUS_OK
		|| result.success == 0)
	{
		print_last_error(session, "source load");
		goto cleanup;
	}
	if (vs_interop_session_run(session, VS_INTEROP_RUN_EVAL_ONCE, &result)
			!= VS_INTEROP_STATUS_OK || result.success == 0)
	{
		print_last_error(session, "one-shot initialization");
		goto cleanup;
	}
	if (vs_interop_value_create(&argument) != VS_INTEROP_STATUS_OK
		|| vs_interop_value_create(&return_value) != VS_INTEROP_STATUS_OK
		|| vs_interop_value_set_int32(argument, 41) != VS_INTEROP_STATUS_OK)
	{
		fprintf(stderr, "value creation failed\n");
		goto cleanup;
	}

	arguments[0] = argument;
	result.struct_size = sizeof(result);
	if (vs_interop_session_call_function(
			session, view_of("add_one"), arguments, 1, return_value, &result)
			!= VS_INTEROP_STATUS_OK
		|| result.success == 0
		|| vs_interop_value_get_int32(return_value, &answer) != VS_INTEROP_STATUS_OK)
	{
		print_last_error(session, "function call");
		goto cleanup;
	}

	if (use_jit)
	{
		vs_interop_jit_stats stats = {0};
		stats.struct_size = sizeof(stats);
		if (vs_interop_session_get_last_jit_stats(session, &stats)
				!= VS_INTEROP_STATUS_OK || stats.dispatches == 0)
		{
			fprintf(stderr, "The selected JIT backend did not execute the call\n");
			goto cleanup;
		}
	}
	if (vs_interop_session_get_vm_backend_info(session, &vm_info) != VS_INTEROP_STATUS_OK
		|| (vm_info.flags & VS_INTEROP_VM_FROZEN) == 0
		|| (use_external_vm && ((vm_info.flags & VS_INTEROP_VM_SELECTED) == 0
			|| vm_info.library_path.size == 0
			|| (vm_info.capabilities & (VS_INTEROP_VM_CAP_BYTECODE | VS_INTEROP_VM_CAP_CLEANUP))
				!= (VS_INTEROP_VM_CAP_BYTECODE | VS_INTEROP_VM_CAP_CLEANUP))))
	{
		fprintf(stderr, "VM selection and execution metadata do not match\n");
		goto cleanup;
	}
	if (!verify_memory(session))
	{
		goto cleanup;
	}
	printf("VeroScript Interop %.*s: add_one(41) = %d\n",
		(int)version.size, version.data, (int)answer);
	exit_code = answer == 42 ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
	vs_interop_value_destroy(return_value);
	vs_interop_value_destroy(argument);
	if (session != NULL)
	{
		(void)vs_interop_session_destroy(session);
	}
	return exit_code;
}
