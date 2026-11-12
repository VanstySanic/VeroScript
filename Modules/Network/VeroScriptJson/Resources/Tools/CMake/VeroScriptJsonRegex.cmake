# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Build only the standalone ECMAScript regex engine and Unicode tables.
enable_language(C)
set(_vs_json_regex_source "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/quickjs-regexp")
set(_vs_json_regex_generated "${CMAKE_CURRENT_BINARY_DIR}/RegexGenerated")
file(MAKE_DIRECTORY "${_vs_json_regex_generated}")

# Prefix the private C symbols so consumers can also link their own QuickJS.
set(_vs_json_regex_symbols "")
foreach(_header libregexp.h libunicode.h)
	file(READ "${_vs_json_regex_source}/${_header}" _text)
	string(REGEX MATCHALL "(lre_|unicode_|cr_)[a-zA-Z0-9_]+" _symbols "${_text}")
	list(APPEND _vs_json_regex_symbols ${_symbols})
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_json_regex_source}/${_header}")
endforeach()
list(REMOVE_DUPLICATES _vs_json_regex_symbols)
set(_prefix_header "// Generated private C symbol prefixes.\n#pragma once\n")
foreach(_symbol IN LISTS _vs_json_regex_symbols)
	string(APPEND _prefix_header "#define ${_symbol} veroscript_json_${_symbol}\n")
endforeach()
file(CONFIGURE OUTPUT "${_vs_json_regex_generated}/VeroScriptJsonRegexSymbols.h" CONTENT "${_prefix_header}" @ONLY)
add_library(VeroScriptJsonRegexBackend OBJECT
	"${_vs_json_regex_source}/libregexp.c" "${_vs_json_regex_source}/libunicode.c")
set_target_properties(VeroScriptJsonRegexBackend PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)
target_include_directories(VeroScriptJsonRegexBackend PRIVATE "${_vs_json_regex_source}")
if(MSVC)
	target_compile_options(VeroScriptJsonRegexBackend PRIVATE /utf-8
		"/FI${_vs_json_regex_generated}/VeroScriptJsonRegexSymbols.h")
	target_compile_definitions(VeroScriptJsonRegexBackend PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(VeroScriptJsonRegexBackend PRIVATE
		-include "${_vs_json_regex_generated}/VeroScriptJsonRegexSymbols.h")
endif()
target_sources(VeroScriptJson PRIVATE $<TARGET_OBJECTS:VeroScriptJsonRegexBackend>)
target_include_directories(VeroScriptJson PRIVATE "${_vs_json_regex_generated}")
target_include_directories(VeroScriptJson SYSTEM PRIVATE "${_vs_json_regex_source}")
