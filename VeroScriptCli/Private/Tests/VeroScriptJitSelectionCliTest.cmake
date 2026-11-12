# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

execute_process(COMMAND "${VERO_CLI}" --version
	RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "jit-backend=none.*native=off")
	message(FATAL_ERROR "Default VM identity failed: ${result} ${output}${error}")
endif()
execute_process(COMMAND "${VERO_CLI}" --lang en-US --backend=jit --eval "print(99);"
	RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT output STREQUAL "" OR NOT error MATCHES "explicitly selected backend")
	message(FATAL_ERROR "Forced JIT without a backend executed script effects: ${result} ${output}${error}")
endif()
execute_process(COMMAND "${VERO_CLI}" --backend=mixed --eval "print(49);"
	RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
string(REPLACE "\r\n" "\n" output "${output}")
if(NOT result EQUAL 0 OR NOT output STREQUAL "49\n")
	message(FATAL_ERROR "Mixed VM fallback failed: ${result} ${output}${error}")
endif()
execute_process(COMMAND "${VERO_CLI}" --jit-backend=missing-backend.dll --version
	RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 2)
	message(FATAL_ERROR "Missing backend was accepted: ${result} ${output}${error}")
endif()
if(VERO_JIT_BACKEND)
	foreach(kind IN ITEMS file directory)
		set(path "${VERO_JIT_BACKEND}")
		if(kind STREQUAL "directory")
			get_filename_component(path "${path}" DIRECTORY)
		endif()
		execute_process(COMMAND "${VERO_CLI}" "--jit-backend=${path}" --backend=jit --eval
			"int value(){return 42;}print(value());"
			RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
		string(REPLACE "\r\n" "\n" output "${output}")
		if(NOT result EQUAL 0 OR NOT output STREQUAL "42\n")
			message(FATAL_ERROR "Explicit backend ${kind} failed: ${result} ${output}${error}")
		endif()
	endforeach()
endif()
