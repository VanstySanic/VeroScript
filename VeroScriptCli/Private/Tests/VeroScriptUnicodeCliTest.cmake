# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

set(_jit_selection "")
if(VERO_JIT_BACKEND)
	set(_jit_selection "--jit-backend=${VERO_JIT_BACKEND}")
else()
	# Without a concrete backend, exercise the interpreter and mixed fallback.
	set(_jit_selection "--backend=mixed")
endif()

if(NOT DEFINED VERO_CLI OR NOT DEFINED VERO_WORK)
	message(FATAL_ERROR "VERO_CLI and VERO_WORK are required")
endif()

set(_root "${VERO_WORK}/路径-🚀")
set(_main "${_root}/入口.vs")
set(_helper "${_root}/模块.vs")
# Package identifiers intentionally use ASCII; exercise the Unicode parent path.
set(_package "${_root}/UnicodeApp.vsapp")
file(MAKE_DIRECTORY "${_root}")
file(WRITE "${_helper}" "export int imported() { return 23; }\n")
file(WRITE "${_main}" "import \"模块.vs\";\nint main(string[] args) { print(args[0]); return imported(); }\n")

foreach(_backend vm mixed jit)
	execute_process(COMMAND "${VERO_CLI}" ${_jit_selection} --bc-cache=off "--backend=${_backend}" "${_main}" "参数-🚀"
		RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr ENCODING UTF-8)
	if(NOT _result STREQUAL "23" OR NOT _stdout STREQUAL "参数-🚀\n" OR NOT _stderr STREQUAL "")
		message(FATAL_ERROR "Unicode source/argument execution (${_backend}) failed: ${_result}\n${_stdout}\n${_stderr}")
	endif()
endforeach()

execute_process(COMMAND "${VERO_CLI}" ${_jit_selection} --emit-package "${_package}" "${_main}"
	RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr ENCODING UTF-8)
if(NOT _result STREQUAL "0")
	message(FATAL_ERROR "Unicode package emission failed: ${_result}\n${_stdout}\n${_stderr}")
endif()
execute_process(COMMAND "${VERO_CLI}" ${_jit_selection} "${_package}" "参数-🚀"
	RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr ENCODING UTF-8)
if(NOT _result STREQUAL "23" OR NOT _stdout STREQUAL "参数-🚀\n" OR NOT _stderr STREQUAL "")
	message(FATAL_ERROR "Unicode package/argument execution failed: ${_result}\n${_stdout}\n${_stderr}")
endif()

file(REMOVE "${_main}" "${_helper}" "${_package}")
