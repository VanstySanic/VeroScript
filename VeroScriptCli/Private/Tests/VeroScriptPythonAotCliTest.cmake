# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT DEFINED VERO_CLI OR NOT DEFINED VERO_PYTHON OR NOT DEFINED VERO_WORK)
	message(FATAL_ERROR "VERO_CLI, VERO_PYTHON, and VERO_WORK are required")
endif()

file(REMOVE_RECURSE "${VERO_WORK}")
file(MAKE_DIRECTORY "${VERO_WORK}")
set(_source "${VERO_WORK}/main.vs")
set(_generated "${VERO_WORK}/main.py")
file(WRITE "${_source}"
	"int square(int value) { return value * value; }\n"
	"void main() { print(square(7)); }\n")

execute_process(
	COMMAND "${VERO_CLI}" aot --target=python "${_source}" -o "${_generated}"
	RESULT_VARIABLE _emit_result
	OUTPUT_VARIABLE _emit_output
	ERROR_VARIABLE _emit_error)
if(NOT _emit_result EQUAL 0)
	message(FATAL_ERROR
		"Python AOT CLI emission failed (${_emit_result})\n${_emit_output}\n${_emit_error}")
endif()
if(NOT EXISTS "${_generated}")
	message(FATAL_ERROR "Python AOT CLI did not create ${_generated}")
endif()

execute_process(
	COMMAND "${VERO_PYTHON}" "${_generated}"
	RESULT_VARIABLE _run_result
	OUTPUT_VARIABLE _run_output
	ERROR_VARIABLE _run_error)
if(NOT _run_result EQUAL 0)
	message(FATAL_ERROR
		"Generated Python failed (${_run_result})\n${_run_output}\n${_run_error}")
endif()
string(REPLACE "\r\n" "\n" _run_output "${_run_output}")
if(NOT _run_output STREQUAL "49\n")
	message(FATAL_ERROR "Generated Python output mismatch: [${_run_output}]")
endif()

set(_bundle "${VERO_WORK}/bundle")
execute_process(
	COMMAND "${VERO_CLI}" aot --target=python "${_source}"
		--output-dir "${_bundle}" --bundle-name sample
	RESULT_VARIABLE _bundle_emit_result
	OUTPUT_VARIABLE _bundle_emit_output
	ERROR_VARIABLE _bundle_emit_error)
if(NOT _bundle_emit_result EQUAL 0)
	message(FATAL_ERROR
		"Python AOT bundle emission failed (${_bundle_emit_result})\n"
		"${_bundle_emit_output}\n${_bundle_emit_error}")
endif()
foreach(_required IN ITEMS
		"__main__.py"
		"__init__.py"
		"_veroscript_runtime.py"
		"_veroscript_program.py"
		"python-aot.manifest.json")
	if(NOT EXISTS "${_bundle}/${_required}")
		message(FATAL_ERROR "Python AOT bundle did not create ${_required}")
	endif()
endforeach()

execute_process(
	COMMAND "${VERO_PYTHON}" "${_bundle}"
	RESULT_VARIABLE _bundle_run_result
	OUTPUT_VARIABLE _bundle_run_output
	ERROR_VARIABLE _bundle_run_error)
if(NOT _bundle_run_result EQUAL 0)
	message(FATAL_ERROR
		"Generated Python bundle failed (${_bundle_run_result})\n"
		"${_bundle_run_output}\n${_bundle_run_error}")
endif()
string(REPLACE "\r\n" "\n" _bundle_run_output "${_bundle_run_output}")
if(NOT _bundle_run_output STREQUAL "49\n")
	message(FATAL_ERROR "Generated Python bundle output mismatch: [${_bundle_run_output}]")
endif()

execute_process(
	COMMAND "${VERO_PYTHON}" -m bundle
	WORKING_DIRECTORY "${VERO_WORK}"
	RESULT_VARIABLE _module_run_result
	OUTPUT_VARIABLE _module_run_output
	ERROR_VARIABLE _module_run_error)
if(NOT _module_run_result EQUAL 0)
	message(FATAL_ERROR
		"Generated Python package module failed (${_module_run_result})\n"
		"${_module_run_output}\n${_module_run_error}")
endif()
string(REPLACE "\r\n" "\n" _module_run_output "${_module_run_output}")
if(NOT _module_run_output STREQUAL "49\n")
	message(FATAL_ERROR "Generated Python package module output mismatch: [${_module_run_output}]")
endif()
