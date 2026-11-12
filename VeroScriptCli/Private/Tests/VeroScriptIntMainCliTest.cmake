# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT DEFINED VERO_CLI OR NOT DEFINED VERO_APP)
	message(FATAL_ERROR "VERO_CLI and VERO_APP are required")
endif()

set(_source "${VERO_APP}.vs")
file(REMOVE "${VERO_APP}" "${VERO_APP}.tmp" "${_source}")
file(WRITE "${_source}" "int main() { return 23; }\n")

execute_process(
	COMMAND "${VERO_CLI}" "${_source}"
	RESULT_VARIABLE _source_result
	OUTPUT_VARIABLE _source_stdout
	ERROR_VARIABLE _source_stderr)
if(NOT _source_result EQUAL 23)
	message(FATAL_ERROR
		"source int main returned ${_source_result}, expected 23\n"
		"stdout: ${_source_stdout}\nstderr: ${_source_stderr}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" --emit-package "${VERO_APP}" "${_source}"
	RESULT_VARIABLE _emit_result
	OUTPUT_VARIABLE _emit_stdout
	ERROR_VARIABLE _emit_stderr)
if(NOT _emit_result EQUAL 0)
	message(FATAL_ERROR
		"package emission failed with ${_emit_result}\n"
		"stdout: ${_emit_stdout}\nstderr: ${_emit_stderr}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" "${VERO_APP}"
	RESULT_VARIABLE _package_result
	OUTPUT_VARIABLE _package_stdout
	ERROR_VARIABLE _package_stderr)
if(NOT _package_result EQUAL 23)
	message(FATAL_ERROR
		"packaged int main returned ${_package_result}, expected 23\n"
		"stdout: ${_package_stdout}\nstderr: ${_package_stderr}")
endif()

file(REMOVE "${VERO_APP}" "${VERO_APP}.tmp" "${_source}")
