// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"

_Static_assert(VS_INTEROP_RUN_EVAL == UINT32_C(0)
	&& VS_INTEROP_RUN_SCRIPT == UINT32_C(1)
	&& VS_INTEROP_RUN_EVAL_ONCE == UINT32_C(2),
	"run mode values changed");
_Static_assert(VS_INTEROP_SCRIPT_CAP_PROCESS_DIRECTORY_CHANGE == UINT32_C(0x100),
	"process directory capability bit changed");
_Static_assert((VS_INTEROP_SCRIPT_CAP_ALL
	& VS_INTEROP_SCRIPT_CAP_PROCESS_DIRECTORY_CHANGE) != 0,
	"all-capabilities mask omits process directory change");
_Static_assert(offsetof(vs_interop_jit_policy, reuse_session_code) == 32,
	"legacy JIT policy prefix changed");
_Static_assert(sizeof(vs_interop_jit_policy) == 40,
	"current JIT policy layout changed");
_Static_assert(offsetof(vs_interop_jit_policy, enable_controlled_mixed) == 33,
	"controlled Mixed policy offset changed");
_Static_assert(VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION == UINT64_C(0x10),
	"controlled execution capability changed");
_Static_assert(sizeof(vs_interop_jit_cache_limits) == 24,
	"JIT cache limit layout changed");
_Static_assert(sizeof(vs_interop_jit_cache_stats) == 40,
	"JIT cache statistics layout changed");
_Static_assert(offsetof(vs_interop_jit_stats, cost_model_vm_fallbacks) == 328,
	"previous JIT statistics prefix changed");
_Static_assert(offsetof(vs_interop_jit_stats, native_typed_loop_executions) == 336,
	"native typed-loop statistic offset changed");
_Static_assert(sizeof(vs_interop_jit_stats) == 400,
	"current JIT statistics layout changed");
_Static_assert(offsetof(vs_interop_jit_stats, controlled_native_executions) == 344
	&& offsetof(vs_interop_jit_stats, aot_executions) == 352,
	"controlled/AOT statistics offsets changed");
_Static_assert(offsetof(vs_interop_jit_stats, native_leaf_executions) == 360
	&& offsetof(vs_interop_jit_stats, vm_trampoline_executions) == 392,
	"actual execution statistics must extend the existing prefix");

_Static_assert(VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE == UINT32_C(1),
	"annotation name policy flag changed");
_Static_assert(offsetof(vs_interop_annotation_desc, name_flags)
	>= offsetof(vs_interop_annotation_desc, argument_count) + sizeof(uint64_t),
	"annotation name policy must follow its argument list");

_Static_assert(sizeof(vs_interop_memory_limits) == 24, "memory limit layout");
_Static_assert(sizeof(vs_interop_memory_stats) == 32, "memory stats layout");
_Static_assert(sizeof(vs_interop_memory_lease_token) == 16, "memory token layout");
_Static_assert(sizeof(vs_interop_memory_lease) == (sizeof(void*) == 8 ? 48 : 40), "memory lease layout");

/* C11 compile/link proof for every public ABI symbol. Converting between
 * function-pointer types is defined as long as the result is not called. */
static int require_all_symbols(void)
{
	void (VS_INTEROP_CALL *volatile symbol)(void) = 0;
	#define VS_REQUIRE_SYMBOL(name) \
	do \
	{ \
		symbol = (void (VS_INTEROP_CALL *)(void))(name); \
		if (!symbol) \
		{ \
			return 0; \
		} \
	} \
	while (0)
	VS_REQUIRE_SYMBOL(vs_interop_aot_program_create);
	VS_REQUIRE_SYMBOL(vs_interop_aot_program_retain);
	VS_REQUIRE_SYMBOL(vs_interop_aot_program_release);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_aot_program);
	VS_REQUIRE_SYMBOL(vs_interop_application_services_create);
	VS_REQUIRE_SYMBOL(vs_interop_application_services_destroy);
	VS_REQUIRE_SYMBOL(vs_interop_session_load_application);
	VS_REQUIRE_SYMBOL(vs_interop_application_read);
	VS_REQUIRE_SYMBOL(vs_interop_get_abi_version);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_variant_empty);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_variant);
	VS_REQUIRE_SYMBOL(vs_interop_variant_has_value);
	VS_REQUIRE_SYMBOL(vs_interop_variant_get_type_name);
	VS_REQUIRE_SYMBOL(vs_interop_variant_get);
	VS_REQUIRE_SYMBOL(vs_interop_get_runtime_version);
	VS_REQUIRE_SYMBOL(vs_interop_get_host_platform);
	VS_REQUIRE_SYMBOL(vs_interop_locale_set_language);
	VS_REQUIRE_SYMBOL(vs_interop_locale_get_language);
	VS_REQUIRE_SYMBOL(vs_interop_locale_get_available_languages);
	VS_REQUIRE_SYMBOL(vs_interop_locale_translate);
	VS_REQUIRE_SYMBOL(vs_interop_locale_load_catalog_directory);
	VS_REQUIRE_SYMBOL(vs_interop_locale_merge_catalog_directory);
	VS_REQUIRE_SYMBOL(vs_interop_session_create);
	VS_REQUIRE_SYMBOL(vs_interop_session_destroy);
	VS_REQUIRE_SYMBOL(vs_interop_session_apply_config);
	VS_REQUIRE_SYMBOL(vs_interop_session_request_cancel);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_jit_backend);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_vm_backend);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_vm_backend_info);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_jit_backend);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_jit_backend_info);
	VS_REQUIRE_SYMBOL(vs_interop_session_load_source);
	VS_REQUIRE_SYMBOL(vs_interop_session_load_file);
	VS_REQUIRE_SYMBOL(vs_interop_session_run);
	VS_REQUIRE_SYMBOL(vs_interop_session_clear);
	VS_REQUIRE_SYMBOL(vs_interop_session_is_loaded);
	VS_REQUIRE_SYMBOL(vs_interop_session_call_function);
	VS_REQUIRE_SYMBOL(vs_interop_session_call_method);
	VS_REQUIRE_SYMBOL(vs_interop_session_call_method_bypassing_interceptor);
	VS_REQUIRE_SYMBOL(vs_interop_session_has_method);
	VS_REQUIRE_SYMBOL(vs_interop_session_has_function);
	VS_REQUIRE_SYMBOL(vs_interop_session_construct_object);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_field);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_field);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_global);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_global);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_native);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_host_function);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_annotation);
	VS_REQUIRE_SYMBOL(vs_interop_session_diagnostic_count);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_diagnostic);
	VS_REQUIRE_SYMBOL(vs_interop_session_diagnostic_argument_count);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_diagnostic_argument);
	VS_REQUIRE_SYMBOL(vs_interop_session_format_diagnostic);
	VS_REQUIRE_SYMBOL(vs_interop_session_copy_last_error);
	VS_REQUIRE_SYMBOL(vs_interop_value_create);
	VS_REQUIRE_SYMBOL(vs_interop_value_clone);
	VS_REQUIRE_SYMBOL(vs_interop_value_assign);
	VS_REQUIRE_SYMBOL(vs_interop_value_destroy);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_kind);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_type_name);
	VS_REQUIRE_SYMBOL(vs_interop_value_reset);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_null);
	VS_REQUIRE_SYMBOL(vs_interop_value_is_null);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_int32);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_int32);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_uint32);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_uint32);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_int64);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_int64);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_uint64);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_uint64);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_double);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_double);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_float);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_float);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_int8);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_int8);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_uint8);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_uint8);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_int16);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_int16);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_uint16);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_uint16);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_bool);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_bool);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_string);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_string);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_wstring);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_wstring);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_enum);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_enum);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_host_object);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_host_object_ex);
	VS_REQUIRE_SYMBOL(vs_interop_value_get_host_object);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_array);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_sequence);
	VS_REQUIRE_SYMBOL(vs_interop_value_sequence_count);
	VS_REQUIRE_SYMBOL(vs_interop_value_sequence_get);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_map);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_map_kind);
	VS_REQUIRE_SYMBOL(vs_interop_value_map_count);
	VS_REQUIRE_SYMBOL(vs_interop_value_map_get);
	VS_REQUIRE_SYMBOL(vs_interop_value_map_enumerate);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_set);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_set_kind);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_count);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_contains);
	VS_REQUIRE_SYMBOL(vs_interop_value_set_enumerate);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_channel);
	VS_REQUIRE_SYMBOL(vs_interop_value_create_bounded_channel);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_try_send);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_try_receive);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_send_for);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_receive_for);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_count);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_capacity);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_is_closed);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_send);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_receive);
	VS_REQUIRE_SYMBOL(vs_interop_value_channel_close);
	VS_REQUIRE_SYMBOL(vs_interop_generic_type_context_argument_count);
	VS_REQUIRE_SYMBOL(vs_interop_generic_type_context_get_argument);
	VS_REQUIRE_SYMBOL(vs_interop_generic_type_context_set_type);
	VS_REQUIRE_SYMBOL(vs_interop_generic_type_context_add_member);
	VS_REQUIRE_SYMBOL(vs_interop_generic_type_context_set_protocol);
	VS_REQUIRE_SYMBOL(vs_interop_generic_type_context_set_error);
	VS_REQUIRE_SYMBOL(vs_interop_session_retain_callable);
	VS_REQUIRE_SYMBOL(vs_interop_callable_destroy);
	VS_REQUIRE_SYMBOL(vs_interop_callable_is_valid);
	VS_REQUIRE_SYMBOL(vs_interop_callable_invoke);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_argument_count);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_get_argument);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_get_mutable_argument);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_host_this);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_program_generation);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_set_error);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_invoke_function);
	VS_REQUIRE_SYMBOL(vs_interop_call_context_invoke_callable);
	VS_REQUIRE_SYMBOL(vs_interop_get_capabilities);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_numeric_limits);
	VS_REQUIRE_SYMBOL(vs_interop_value_numeric_kind);
	VS_REQUIRE_SYMBOL(vs_interop_numeric_parse);
	VS_REQUIRE_SYMBOL(vs_interop_numeric_to_text);
	VS_REQUIRE_SYMBOL(vs_interop_numeric_convert);
	VS_REQUIRE_SYMBOL(vs_interop_buffer_destroy);
	VS_REQUIRE_SYMBOL(vs_interop_buffer_view);
	VS_REQUIRE_SYMBOL(vs_interop_buffer_copy);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_input);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_hot_reload_enabled);
	VS_REQUIRE_SYMBOL(vs_interop_session_is_hot_reload_enabled);
	VS_REQUIRE_SYMBOL(vs_interop_session_reload);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_reload_status);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_jit_policy);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_jit_cache_limits);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_jit_cache_limits);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_jit_cache_stats);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_last_jit_stats);
	VS_REQUIRE_SYMBOL(vs_interop_package_read);
	VS_REQUIRE_SYMBOL(vs_interop_package_destroy);
	VS_REQUIRE_SYMBOL(vs_interop_package_get_kind);
	VS_REQUIRE_SYMBOL(vs_interop_package_get_name);
	VS_REQUIRE_SYMBOL(vs_interop_package_get_version);
	VS_REQUIRE_SYMBOL(vs_interop_package_metadata_count);
	VS_REQUIRE_SYMBOL(vs_interop_package_get_metadata);
	VS_REQUIRE_SYMBOL(vs_interop_package_get_canonical_signed_bytes);
	VS_REQUIRE_SYMBOL(vs_interop_session_load_package);
	VS_REQUIRE_SYMBOL(vs_interop_session_reload_package);
	VS_REQUIRE_SYMBOL(vs_interop_session_build_package_file);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_host_type);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_host_member);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_host_protocol);
	VS_REQUIRE_SYMBOL(vs_interop_session_retain_callback_user_data);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_generic_host_type_template);
	VS_REQUIRE_SYMBOL(vs_interop_session_instantiate_generic_host_type);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_type_alias);
	VS_REQUIRE_SYMBOL(vs_interop_session_export_bindings_json);
	VS_REQUIRE_SYMBOL(vs_interop_session_export_reflection_json);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_instance_call_interceptor);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_mutation_callback);
	VS_REQUIRE_SYMBOL(vs_interop_session_apply_class_defaults);
	VS_REQUIRE_SYMBOL(vs_interop_session_register_external_serializer);
	VS_REQUIRE_SYMBOL(vs_interop_session_serialize_object);
	VS_REQUIRE_SYMBOL(vs_interop_session_deserialize_object);
	VS_REQUIRE_SYMBOL(vs_interop_session_enumerate_object_references);
	VS_REQUIRE_SYMBOL(vs_interop_intercept_context_receiver);
	VS_REQUIRE_SYMBOL(vs_interop_intercept_context_get_target);
	VS_REQUIRE_SYMBOL(vs_interop_intercept_context_argument_count);
	VS_REQUIRE_SYMBOL(vs_interop_intercept_context_get_argument);
	VS_REQUIRE_SYMBOL(vs_interop_intercept_context_proceed);
	VS_REQUIRE_SYMBOL(vs_interop_intercept_context_set_error);
	VS_REQUIRE_SYMBOL(vs_interop_external_serialize_context_type_name);
	VS_REQUIRE_SYMBOL(vs_interop_external_serialize_context_mutation_version);
	VS_REQUIRE_SYMBOL(vs_interop_external_serialize_context_field_count);
	VS_REQUIRE_SYMBOL(vs_interop_external_serialize_context_get_field);
	VS_REQUIRE_SYMBOL(vs_interop_external_serialize_context_set_bytes);
	VS_REQUIRE_SYMBOL(vs_interop_external_serialize_context_set_error);
	VS_REQUIRE_SYMBOL(vs_interop_external_deserialize_context_bytes);
	VS_REQUIRE_SYMBOL(vs_interop_external_deserialize_context_encoded_version);
	VS_REQUIRE_SYMBOL(vs_interop_external_deserialize_context_add_field);
	VS_REQUIRE_SYMBOL(vs_interop_external_deserialize_context_set_error);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_memory_limits);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_stdlib_limits);
	VS_REQUIRE_SYMBOL(vs_interop_memory_get_stats);
	VS_REQUIRE_SYMBOL(vs_interop_memory_create_buffer);
	VS_REQUIRE_SYMBOL(vs_interop_memory_create_view);
	VS_REQUIRE_SYMBOL(vs_interop_memory_size);
	VS_REQUIRE_SYMBOL(vs_interop_memory_capacity);
	VS_REQUIRE_SYMBOL(vs_interop_memory_resize);
	VS_REQUIRE_SYMBOL(vs_interop_memory_slice);
	VS_REQUIRE_SYMBOL(vs_interop_memory_copy);
	VS_REQUIRE_SYMBOL(vs_interop_memory_fill);
	VS_REQUIRE_SYMBOL(vs_interop_memory_equal);
	VS_REQUIRE_SYMBOL(vs_interop_memory_clone);
	VS_REQUIRE_SYMBOL(vs_interop_memory_from_array);
	VS_REQUIRE_SYMBOL(vs_interop_memory_to_array);
	VS_REQUIRE_SYMBOL(vs_interop_memory_read);
	VS_REQUIRE_SYMBOL(vs_interop_memory_write);
	VS_REQUIRE_SYMBOL(vs_interop_memory_native_addresses_available);
	VS_REQUIRE_SYMBOL(vs_interop_memory_object_id);
	VS_REQUIRE_SYMBOL(vs_interop_memory_object_address);
	VS_REQUIRE_SYMBOL(vs_interop_memory_data_address);
	VS_REQUIRE_SYMBOL(vs_interop_memory_call_argument_address);
	VS_REQUIRE_SYMBOL(vs_interop_memory_wrap_external);
	VS_REQUIRE_SYMBOL(vs_interop_memory_invalidate_external);
	VS_REQUIRE_SYMBOL(vs_interop_memory_acquire_lease);
	VS_REQUIRE_SYMBOL(vs_interop_memory_release_lease);
	VS_REQUIRE_SYMBOL(vs_interop_memory_mutation_version);
	VS_REQUIRE_SYMBOL(vs_interop_get_compile_capabilities);
	VS_REQUIRE_SYMBOL(vs_interop_compile_processing_service_retain);
	VS_REQUIRE_SYMBOL(vs_interop_compile_processing_service_release);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_compile_processing_service);
	VS_REQUIRE_SYMBOL(vs_interop_annotation_service_retain);
	VS_REQUIRE_SYMBOL(vs_interop_annotation_service_release);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_annotation_service);
	VS_REQUIRE_SYMBOL(vs_interop_session_set_script_compile_options);
	VS_REQUIRE_SYMBOL(vs_interop_session_get_diagnostic_severity);
	VS_REQUIRE_SYMBOL(vs_interop_module_environment_retain);
	VS_REQUIRE_SYMBOL(vs_interop_module_environment_release);
	VS_REQUIRE_SYMBOL(vs_interop_session_apply_config_with_environment);
	#undef VS_REQUIRE_SYMBOL
	return 1;
}

static int check_annotation_name_policies(void)
{
	static const char exact_name[] = "@Exact";
	static const char folded_name[] = "@CasePolicy";
	static const char source[] =
		"@Exact @cASEpOLICY class Tagged{}; int main(){type<Tagged> t=type<Tagged>();"
		"return t.has_annotation(\"Exact\") && !t.has_annotation(\"exact\")"
		" && t.has_annotation(\"CASEPOLICY\")"
		" && !t.annotation(1).definition().case_sensitive() ? 42 : 1;}";
	static const char label[] = "c-annotation-policy.vs";
	const vs_interop_string_view source_view = {source, sizeof(source) - 1};
	const vs_interop_string_view label_view = {label, sizeof(label) - 1};
	vs_interop_session* session = NULL;
	vs_interop_annotation_desc definition = {0};
	vs_interop_run_result result = {0};
	int valid = 0;
	if (vs_interop_session_create(NULL, &session) != VS_INTEROP_STATUS_OK) return 0;
	definition.struct_size = (uint32_t)sizeof(definition);
	definition.name.data = exact_name;
	definition.name.size = sizeof(exact_name) - 1;
	if (vs_interop_session_register_annotation(session, &definition) == VS_INTEROP_STATUS_OK)
	{
		definition.name.data = folded_name;
		definition.name.size = sizeof(folded_name) - 1;
		definition.name_flags = VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE;
		result.struct_size = (uint32_t)sizeof(result);
		valid = vs_interop_session_register_annotation(session, &definition) == VS_INTEROP_STATUS_OK
			&& vs_interop_session_load_source(session, source_view, label_view, &result) == VS_INTEROP_STATUS_OK
			&& result.success
			&& vs_interop_session_run(session, VS_INTEROP_RUN_SCRIPT, &result) == VS_INTEROP_STATUS_OK
			&& result.success && result.exit_code == 42;
	}
	return vs_interop_session_destroy(session) == VS_INTEROP_STATUS_OK && valid;
}

int main(void)
{
	vs_interop_session_options options = {0};
	vs_interop_config config = {0};
	vs_interop_run_result result = {0};
	options.struct_size = (uint32_t)sizeof(options);
	config.struct_size = (uint32_t)sizeof(config);
	result.struct_size = (uint32_t)sizeof(result);
	return options.struct_size != 0 && config.struct_size != 0
		&& result.struct_size != 0
		&& require_all_symbols()
		&& check_annotation_name_policies()
		&& vs_interop_get_abi_version() == VS_INTEROP_ABI_VERSION
		&& vs_interop_get_runtime_version().size != 0 ? 0 : 1;
}
