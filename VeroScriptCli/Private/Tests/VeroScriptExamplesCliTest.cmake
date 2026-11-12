# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

set(_jit_selection "")
if(VERO_JIT_BACKEND)
	set(_jit_selection "--jit-backend=${VERO_JIT_BACKEND}")
else()
	# Without a concrete backend, exercise the interpreter and mixed fallback.
	set(_jit_selection "--backend=mixed")
endif()

if(NOT DEFINED VERO_CLI OR NOT EXISTS "${VERO_CLI}")
	message(FATAL_ERROR "VERO_CLI must name the built veroscript executable")
endif()
if(NOT DEFINED VERO_EXAMPLES_ROOT OR NOT IS_DIRECTORY "${VERO_EXAMPLES_ROOT}")
	message(FATAL_ERROR "VERO_EXAMPLES_ROOT must name the script examples directory")
endif()

if(NOT DEFINED VERO_PROCESS_TIMEOUT_SECONDS)
	set(VERO_PROCESS_TIMEOUT_SECONDS 60)
endif()
if(NOT VERO_PROCESS_TIMEOUT_SECONDS MATCHES "^[1-9][0-9]*$" OR VERO_PROCESS_TIMEOUT_SECONDS GREATER 3600)
	message(FATAL_ERROR "VERO_PROCESS_TIMEOUT_SECONDS must be an integer from 1 to 3600")
endif()
set(_vero_process_timeout_seconds "${VERO_PROCESS_TIMEOUT_SECONDS}")

file(GLOB_RECURSE _vero_examples
	LIST_DIRECTORIES false
	"${VERO_EXAMPLES_ROOT}/*.vs")
list(SORT _vero_examples)

if(NOT _vero_examples)
	message(FATAL_ERROR "No VeroScript examples found under ${VERO_EXAMPLES_ROOT}")
endif()

set(_vero_failures "")
set(_vero_backends vm mixed)
if(VERO_JIT_BACKEND)
	list(APPEND _vero_backends jit)
endif()
set(_vero_checked_example_count 0)
foreach(_vero_example IN LISTS _vero_examples)
	file(RELATIVE_PATH _vero_example_relative
		"${VERO_EXAMPLES_ROOT}" "${_vero_example}")
	# This prototype module is exercised through its importing implementation.
	if(_vero_example_relative STREQUAL "template_methods_decl.vs")
		continue()
	endif()
	math(EXPR _vero_checked_example_count "${_vero_checked_example_count} + 1")
	unset(_vero_expected_stdout)
	set(_vero_example_args "")
	if(_vero_example_relative STREQUAL "hello.vs")
		set(_vero_expected_stdout "hello, VeroScript\n")
	elseif(_vero_example_relative STREQUAL "portable_basics.vs")
		set(_vero_expected_stdout "portable basics: 42\n")
	elseif(_vero_example_relative STREQUAL "include_alias.vs")
		set(_vero_expected_stdout "clamped: 10\n")
	elseif(_vero_example_relative STREQUAL "macro_scope.vs")
		set(_vero_expected_stdout "module value: 42\n")
	elseif(_vero_example_relative STREQUAL "macro_exports.vs")
		set(_vero_expected_stdout "public=42 private-result=7\n")
	elseif(_vero_example_relative STREQUAL "macro_implicit.vs")
		set(_vero_expected_stdout "public=42 private-result=7\n")
		set(_vero_example_args --implicit-import "${VERO_EXAMPLES_ROOT}/macro_exports_facade.vs")
	elseif(_vero_example_relative STREQUAL "reexport_main.vs")
		set(_vero_expected_stdout "a-b-c\n")
	elseif(_vero_example_relative STREQUAL "alias_exports.vs")
		set(_vero_expected_stdout "alias exports: direct 42\n")
	elseif(_vero_example_relative STREQUAL "alias_reexports.vs")
		set(_vero_expected_stdout "alias reexports: facade 42\n")
	elseif(_vero_example_relative MATCHES "^alias_exports_(module|facade)\\.vs$")
		set(_vero_expected_stdout "")
	elseif(_vero_example_relative STREQUAL "delegates.vs")
		set(_vero_expected_stdout "1\n2\n2\n42\n7\nok\n24\ndelegates: ok\n")
	elseif(_vero_example_relative STREQUAL "object_basics.vs")
		set(_vero_expected_stdout "3\nnull ok\ndefault configuration: true\n")
	elseif(_vero_example_relative STREQUAL "windows_path_text.vs")
		set(_vero_expected_stdout "C:\\Temp\\test.txt\n16\n")
	elseif(_vero_example_relative STREQUAL "memory_operations.vs")
		set(_vero_expected_stdout "memory example passed\n")
	elseif(_vero_example_relative STREQUAL "method_templates.vs")
		set(_vero_expected_stdout "7\nmethod\n9\n11\nbound\n")
	elseif(_vero_example_relative STREQUAL "value_template_parameters.vs")
		set(_vero_expected_stdout "5\n12\ntrue\n18446744073709551615\n")
	elseif(_vero_example_relative STREQUAL "template_methods_main.vs")
		set(_vero_expected_stdout "cross-file\n")
	elseif(_vero_example_relative STREQUAL "attribute_demo.vs")
		set(_vero_expected_stdout "attribute: fixed, copied, evaluated and reset\n")
	elseif(_vero_example_relative MATCHES "^attribute_(functions|methods|weak)\\.vs$")
		set(_vero_expected_stdout "attribute ${CMAKE_MATCH_1}: ok\n")
	elseif(_vero_example_relative STREQUAL "delegate_functions.vs")
		set(_vero_expected_stdout "0\n0\n1\n2\n3\nportable delegates ok\n")
	elseif(_vero_example_relative STREQUAL "console_demo.vs")
		set(_vero_expected_stdout "VeroScript console\nDone\n")
	endif()

	execute_process(
		COMMAND "${VERO_CLI}" ${_jit_selection} ${_vero_example_args} --bc-cache=off --check "${_vero_example}"
		WORKING_DIRECTORY "${VERO_EXAMPLES_ROOT}"
		RESULT_VARIABLE _vero_result
		OUTPUT_VARIABLE _vero_stdout
		ERROR_VARIABLE _vero_stderr
		TIMEOUT "${_vero_process_timeout_seconds}")
	if(NOT _vero_result EQUAL 0)
		string(APPEND _vero_failures
			"\n${_vero_example} [check] (exit ${_vero_result})\n"
			"${_vero_stdout}${_vero_stderr}")
		continue()
	endif()

	foreach(_vero_backend IN LISTS _vero_backends)
		execute_process(
			COMMAND "${VERO_CLI}" ${_jit_selection} ${_vero_example_args} --bc-cache=off
				"--backend=${_vero_backend}" "${_vero_example}"
			WORKING_DIRECTORY "${VERO_EXAMPLES_ROOT}"
			RESULT_VARIABLE _vero_result
			OUTPUT_VARIABLE _vero_stdout
			ERROR_VARIABLE _vero_stderr
			TIMEOUT "${_vero_process_timeout_seconds}")
		if(NOT _vero_result EQUAL 0)
			string(APPEND _vero_failures
				"\n${_vero_example} [${_vero_backend}] (exit ${_vero_result})\n"
				"${_vero_stdout}${_vero_stderr}")
		elseif(DEFINED _vero_expected_stdout)
			string(REPLACE "\r\n" "\n" _vero_normalized_stdout "${_vero_stdout}")
			if(NOT _vero_normalized_stdout STREQUAL _vero_expected_stdout)
				string(APPEND _vero_failures
					"\n${_vero_example} [${_vero_backend}] unexpected output\n"
					"Expected: ${_vero_expected_stdout}Actual: ${_vero_normalized_stdout}")
			endif()
		endif()
	endforeach()
endforeach()

# The packaged executable uses private module identities inside generic
# reflection descriptors. A stripped package must preserve those identities.
if(DEFINED VERO_PACKAGE_OUTPUT_ROOT)
	set(_vero_reflection_output_root "${VERO_PACKAGE_OUTPUT_ROOT}")
else()
	set(_vero_reflection_output_root "${CMAKE_CURRENT_BINARY_DIR}")
endif()
file(MAKE_DIRECTORY "${_vero_reflection_output_root}")
set(_vero_reflection_package
	"${_vero_reflection_output_root}/reflection_executable_stripped_test.vsapp")
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --emit-package "${_vero_reflection_package}"
		--strip-package-debug
		"${VERO_EXAMPLES_ROOT}/reflection_executable.vs"
	WORKING_DIRECTORY "${VERO_EXAMPLES_ROOT}"
	RESULT_VARIABLE _vero_package_build_result
	OUTPUT_VARIABLE _vero_package_build_stdout
	ERROR_VARIABLE _vero_package_build_stderr
	TIMEOUT "${_vero_process_timeout_seconds}")
if(NOT _vero_package_build_result EQUAL 0)
	string(APPEND _vero_failures
		"\nreflection_executable.vs [stripped package build] (exit ${_vero_package_build_result})\n"
		"${_vero_package_build_stdout}${_vero_package_build_stderr}")
else()
	execute_process(
		COMMAND "${VERO_CLI}" ${_jit_selection} "${_vero_reflection_package}"
		WORKING_DIRECTORY "${VERO_EXAMPLES_ROOT}"
		RESULT_VARIABLE _vero_package_run_result
		OUTPUT_VARIABLE _vero_package_run_stdout
		ERROR_VARIABLE _vero_package_run_stderr
		TIMEOUT "${_vero_process_timeout_seconds}")
	string(REPLACE "\r\n" "\n"
		_vero_package_run_stdout "${_vero_package_run_stdout}")
	if(NOT _vero_package_run_result EQUAL 0
		OR NOT _vero_package_run_stdout STREQUAL "executable reflection ok\n")
		string(APPEND _vero_failures
			"\nreflection_executable.vs [stripped package run] (exit ${_vero_package_run_result})\n"
			"${_vero_package_run_stdout}${_vero_package_run_stderr}")
	endif()
endif()
file(REMOVE "${_vero_reflection_package}")

if(_vero_failures)
	message(FATAL_ERROR "Published VeroScript examples failed validation:${_vero_failures}")
endif()

set(_vero_bindings_sample "${VERO_EXAMPLES_ROOT}/bindings.sample.json")
execute_process(
	COMMAND "${VERO_CLI}" bindings validate "${_vero_bindings_sample}"
	RESULT_VARIABLE _vero_bindings_result
	OUTPUT_VARIABLE _vero_bindings_stdout
	ERROR_VARIABLE _vero_bindings_stderr
	TIMEOUT "${_vero_process_timeout_seconds}")
if(NOT _vero_bindings_result EQUAL 0)
	message(FATAL_ERROR
		"Binding catalog example failed validation (exit ${_vero_bindings_result}):\n"
		"${_vero_bindings_stdout}${_vero_bindings_stderr}")
endif()

set(_vero_workspace_sample "${VERO_EXAMPLES_ROOT}/workspace.analysis.sample.json")
file(READ "${_vero_workspace_sample}" _vero_workspace_json)
string(JSON _vero_workspace_import_count
	ERROR_VARIABLE _vero_workspace_json_error
	LENGTH "${_vero_workspace_json}" importPaths)
if(_vero_workspace_json_error)
	message(FATAL_ERROR
		"Workspace analysis example is not valid JSON: ${_vero_workspace_json_error}")
endif()

list(LENGTH _vero_examples _vero_example_count)
list(JOIN _vero_backends "/" _vero_backend_summary)
message(STATUS
	"Validated ${_vero_checked_example_count} scripts through check/${_vero_backend_summary} and 2 configuration examples; scanned ${_vero_example_count} script files including imported declarations")
