# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

set(_forced_backend mixed)
set(_jit_selection "")
if(VERO_JIT_BACKEND)
	set(_forced_backend jit)
	set(_jit_selection "--jit-backend=${VERO_JIT_BACKEND}")
else()
	# Without a concrete backend, exercise the interpreter and mixed fallback.
	set(_jit_selection "--backend=mixed")
endif()

if(NOT DEFINED VERO_CLI)
	message(FATAL_ERROR "VERO_CLI is required")
endif()

set(_source
	"int sum = 0; for (int i = 0; i < 4; i++) { int doubled = i * 2; sum += doubled; } print(sum);")
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --backend=${_forced_backend} -e "${_source}"
	RESULT_VARIABLE _result
	OUTPUT_VARIABLE _stdout
	ERROR_VARIABLE _stderr)
if(NOT _result EQUAL 0 OR NOT _stdout STREQUAL "12\n" OR NOT _stderr STREQUAL "")
	message(FATAL_ERROR
		"eval for-loop failed with ${_result}\n"
		"stdout: ${_stdout}\nstderr: ${_stderr}")
endif()
