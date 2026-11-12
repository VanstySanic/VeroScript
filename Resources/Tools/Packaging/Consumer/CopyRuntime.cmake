# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Keep Unicode SDK paths out of cmd.exe build events. CMake reads the UTF-8
# script directly; the build event contains only its relative ASCII filename.
function(veroscript_consumer_copy_runtime target)
	set(_script "veroscript-runtime-${target}-$<CONFIG>.cmake")
	set(_content "")
	foreach(_source IN LISTS ARGN)
		string(APPEND _content
			"file(COPY_FILE [==[${_source}]==] [==[$<TARGET_FILE_DIR:${target}>/$<PATH:GET_FILENAME,${_source}>]==] ONLY_IF_DIFFERENT)\n")
	endforeach()
	file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${_script}" CONTENT "${_content}")
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -P "${_script}" VERBATIM)
endfunction()
