# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

execute_process(COMMAND "${EXAMPLE}" "${SCRIPT}" RESULT_VARIABLE _result
	OUTPUT_VARIABLE _output ERROR_VARIABLE _error TIMEOUT 10)
if(NOT _result EQUAL 2 OR NOT _error MATCHES "Execution requires --vm-backend")
	message(FATAL_ERROR "Missing VM selection was not diagnosed: ${_result}: ${_output}${_error}")
endif()

execute_process(COMMAND "${EXAMPLE}" --vm-backend RESULT_VARIABLE _result
	OUTPUT_VARIABLE _output ERROR_VARIABLE _error TIMEOUT 10)
if(NOT _result EQUAL 2 OR NOT _error MATCHES "Usage:")
	message(FATAL_ERROR "Missing backend argument was not rejected: ${_result}: ${_output}${_error}")
endif()

execute_process(COMMAND "${EXAMPLE}" --check "${SCRIPT}" RESULT_VARIABLE _result
	OUTPUT_VARIABLE _output ERROR_VARIABLE _error TIMEOUT 10)
if(NOT _result EQUAL 0 OR NOT _output MATCHES "Script checked:")
	message(FATAL_ERROR "Compile-only validation unexpectedly requires a VM: ${_result}: ${_output}${_error}")
endif()
