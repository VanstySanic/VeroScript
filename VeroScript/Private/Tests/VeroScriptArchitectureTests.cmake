# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT DEFINED VS_SOURCE_ROOT OR NOT EXISTS "${VS_SOURCE_ROOT}/Resources/Tools/CMake/VeroScriptHelpers.cmake")
	message(FATAL_ERROR "VS_SOURCE_ROOT must name the repository root")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../Resources/Tools/CMake/VeroScriptPaths.cmake")
vs_resolve_veroscript_root(VEROSCRIPT_MODULE_ROOT "${VS_SOURCE_ROOT}")

# Private core headers are not an SDK surface. A few source-tree tooling/AOT
# modules still require them; keep that debt explicit so a new dependency cannot
# silently turn an implementation header into a de-facto public API.
if(DEFINED VS_BINARY_ROOT)
	# In-source builds must still inspect their source modules.
	cmake_path(IS_PREFIX VS_BINARY_ROOT "${VEROSCRIPT_MODULE_ROOT}" NORMALIZE _vs_build_contains_source)
endif()

function(_vs_collect_module_cmake_files _vs_directory _vs_result)
	file(GLOB _vs_entries LIST_DIRECTORIES true "${_vs_directory}/*")
	set(_vs_files "")
	foreach(_vs_entry IN LISTS _vs_entries)
		get_filename_component(_vs_name "${_vs_entry}" NAME)
		if(IS_DIRECTORY "${_vs_entry}")
			# Prune generated, metadata and vendor trees before traversing them.
			# Filtering a completed recursive glob still visits all their files.
			if(_vs_name MATCHES "^(\\..*|build|out|CMakeFiles|Release|dist|ThirdParty|node_modules)$")
				continue()
			endif()
			if(DEFINED VS_BINARY_ROOT AND NOT _vs_build_contains_source)
				cmake_path(IS_PREFIX VS_BINARY_ROOT "${_vs_entry}" NORMALIZE _vs_in_build)
				if(_vs_in_build)
					continue()
				endif()
			endif()
			if(NOT IS_SYMLINK "${_vs_entry}")
				_vs_collect_module_cmake_files("${_vs_entry}" _vs_nested_files)
				list(APPEND _vs_files ${_vs_nested_files})
			endif()
		elseif(_vs_name STREQUAL "CMakeLists.txt" AND NOT _vs_directory STREQUAL VEROSCRIPT_MODULE_ROOT)
			list(APPEND _vs_files "${_vs_entry}")
		endif()
	endforeach()
	set(${_vs_result} "${_vs_files}" PARENT_SCOPE)
endfunction()

_vs_collect_module_cmake_files("${VEROSCRIPT_MODULE_ROOT}" _vs_module_cmake_files)
set(_vs_private_core_consumers "")
foreach(_vs_cmake_file IN LISTS _vs_module_cmake_files)
	file(RELATIVE_PATH _vs_relative_path "${VEROSCRIPT_MODULE_ROOT}" "${_vs_cmake_file}")
	string(REPLACE "\\" "/" _vs_relative_path "${_vs_relative_path}")
	# Filter generated/temporary projects before reading: concurrent tests can
	# remove their CMakeLists.txt after the directory scan has observed the path.
	if(_vs_relative_path MATCHES "^(\\.tmp|build|out|CMakeFiles)/")
		continue()
	endif()
	if(DEFINED VS_BINARY_ROOT AND NOT _vs_build_contains_source)
		cmake_path(IS_PREFIX VS_BINARY_ROOT "${_vs_cmake_file}" NORMALIZE _vs_in_build)
		if(_vs_in_build)
			continue()
		endif()
	endif()
	file(READ "${_vs_cmake_file}" _vs_cmake_text)
	string(FIND "${_vs_cmake_text}" "VeroScript/Private" _vs_private_pos)
	if(NOT _vs_private_pos EQUAL -1)
		list(APPEND _vs_private_core_consumers "${_vs_relative_path}")
	endif()
endforeach()
list(SORT _vs_private_core_consumers)

set(_vs_expected_private_core_consumers
	"Extensions/Language/CSharp/VeroScriptCSharpAot/CMakeLists.txt"
	"Extensions/Language/Cpp/VeroScriptCpp/CMakeLists.txt"
	"Extensions/Language/Cpp/VeroScriptCppAot/CMakeLists.txt"
	"VeroScriptTooling/CMakeLists.txt")
list(SORT _vs_expected_private_core_consumers)

if(NOT "${_vs_private_core_consumers}" STREQUAL
		"${_vs_expected_private_core_consumers}")
	string(JOIN "\n  " _vs_actual_text ${_vs_private_core_consumers})
	string(JOIN "\n  " _vs_expected_text ${_vs_expected_private_core_consumers})
	message(FATAL_ERROR
		"VeroScript private-header consumer allow-list changed.\n"
		"Expected:\n  ${_vs_expected_text}\n"
		"Actual:\n  ${_vs_actual_text}\n"
		"Remove the dependency or obtain a core-boundary review and update the allow-list.")
endif()

message(STATUS
	"VeroScript private-header boundary verified (${_vs_private_core_consumers})")

# Cache policy is public, but rapidly evolving cache storage is not installed
# object layout. Keep this source-level C++ service behind one opaque owner.
set(_vs_jit_header
	"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public/VeroScriptJitBackend.h")
file(READ "${_vs_jit_header}" _vs_jit_header_text)
string(FIND "${_vs_jit_header_text}" "struct FImpl;" _vs_jit_pimpl_decl)
string(FIND "${_vs_jit_header_text}" "std::unique_ptr<FImpl> Impl;" _vs_jit_pimpl_owner)
string(FIND "${_vs_jit_header_text}"
	"std::vector<std::optional<FJitFunctionEntry>> Entries;"
	_vs_jit_public_entries)
if(_vs_jit_pimpl_decl EQUAL -1
	OR _vs_jit_pimpl_owner EQUAL -1
	OR NOT _vs_jit_public_entries EQUAL -1)
	message(FATAL_ERROR
		"FJitBackendCache must retain its opaque private implementation layout")
endif()

file(READ "${VEROSCRIPT_MODULE_ROOT}/VeroScript/CMakeLists.txt" _vs_core_build)
string(REGEX REPLACE "#[^\n]*" "" _vs_core_build "${_vs_core_build}")
foreach(_vs_forbidden IN ITEMS "asmjit" "VEROSCRIPT_HAS_ASMJIT" "VEROSCRIPT_ASMJIT_")
	string(FIND "${_vs_core_build}" "${_vs_forbidden}" _vs_found)
	if(NOT _vs_found EQUAL -1)
		message(FATAL_ERROR "Core build depends on concrete backend: ${_vs_forbidden}")
	endif()
endforeach()
file(GLOB_RECURSE _vs_backend_sources LIST_DIRECTORIES false
	"${VEROSCRIPT_MODULE_ROOT}/Modules/Execution/VeroScriptAsmJit/Private/*.cpp"
	"${VEROSCRIPT_MODULE_ROOT}/Modules/Execution/VeroScriptAsmJit/Private/*.h"
	"${VEROSCRIPT_MODULE_ROOT}/Modules/Execution/VeroScriptAsmJit/Private/*.inl")
foreach(_vs_backend_file IN LISTS _vs_backend_sources)
	if(_vs_backend_file MATCHES "/Tests/")
		continue()
	endif()
	file(READ "${_vs_backend_file}" _vs_backend_text)
	if(_vs_backend_text MATCHES "VeroScriptVm\\.h|VeroScript/Private/|reinterpret_cast[^;]*VeroScriptVm")
		message(FATAL_ERROR "Backend bypasses public Core services: ${_vs_backend_file}")
	endif()
endforeach()

# The supported process ABI is the language-neutral C11 Interop header. C++
# namespaces or standard-library types would silently bind it to a C++ ABI.
set(_vs_interop_header
	"${VEROSCRIPT_MODULE_ROOT}/VeroScriptInterop/Public/VeroScriptInterop.h")
file(READ "${_vs_interop_header}" _vs_interop_header_text)
foreach(_vs_forbidden_interop_text IN ITEMS "namespace VeroScript" "std::")
	string(FIND "${_vs_interop_header_text}" "${_vs_forbidden_interop_text}"
		_vs_forbidden_interop_pos)
	if(NOT _vs_forbidden_interop_pos EQUAL -1)
		message(FATAL_ERROR
			"VeroScriptInterop public C ABI contains forbidden C++ surface: "
			"${_vs_forbidden_interop_text}")
	endif()
endforeach()

message(STATUS "VeroScript C++ layout and C11 process-ABI boundaries verified")
