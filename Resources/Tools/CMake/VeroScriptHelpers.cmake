# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Shared native module helpers for the standalone VeroScript source tree.

function(vs_use_shared_private_headers target_name)
	get_filename_component(_vs_shared_private
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../Shared/Private" ABSOLUTE)
	target_include_directories(${target_name} PRIVATE "${_vs_shared_private}")
endfunction()

function(vs_install_windows_runtime destination component)
	if(NOT MSVC)
		return()
	endif()
	# Use the selected toolchain's redistributables, never system-directory DLLs.
	set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
	set(CMAKE_INSTALL_DEBUG_LIBRARIES FALSE)
	set(CMAKE_INSTALL_DEBUG_LIBRARIES_ONLY FALSE)
	set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
	set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS "")
	if(VEROSCRIPT_WINDOWS_REDIST_DIRECTORY)
		file(TO_CMAKE_PATH "${VEROSCRIPT_WINDOWS_REDIST_DIRECTORY}" _vs_windows_redist)
		if(NOT IS_ABSOLUTE "${_vs_windows_redist}" OR
			NOT EXISTS "${_vs_windows_redist}/msvcp140.dll" OR
			NOT EXISTS "${_vs_windows_redist}/vcruntime140.dll")
			message(FATAL_ERROR "VEROSCRIPT_WINDOWS_REDIST_DIRECTORY must contain the selected target's MSVC CRT")
		endif()
		file(GLOB CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS "${_vs_windows_redist}/*.dll")
	else()
		include(InstallRequiredSystemLibraries)
	endif()
	if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
		# This satellite in Microsoft's ARM64 redist is for emulated x64 apps.
		# Native ARM64 imports exception support from vcruntime140.dll.
		list(FILTER CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS EXCLUDE REGEX "/vcruntime140_1[.]dll$")
	endif()
	if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
		if(VS_VEROSCRIPT_PYTHON_PACKAGE_BUILD)
			message(FATAL_ERROR "Windows wheels require the MSVC redistributable libraries")
		endif()
		message(WARNING "MSVC redistributables are unavailable; Windows packaging will reject this build")
		return()
	endif()
	install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
		DESTINATION "${destination}" COMPONENT "${component}" CONFIGURATIONS Release)
	if(destination STREQUAL ".")
		set(_vs_runtime_notice_destination "licenses/MicrosoftVisualCpp")
	else()
		set(_vs_runtime_notice_destination "${destination}/licenses/MicrosoftVisualCpp")
	endif()
	install(FILES "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WindowsRuntimeNotice.txt"
		DESTINATION "${_vs_runtime_notice_destination}"
		RENAME NOTICE.txt COMPONENT "${component}" CONFIGURATIONS Release)
endfunction()

function(vs_collect_module_sources out_var)
	set(_vs_sources "")

	# Public/ is an exported-header surface. Silently ignoring a misplaced
	# implementation here is dangerous: the file remains visible in the
	# repository but none of its symbols reach the module binary.
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Public")
		file(GLOB_RECURSE _vs_misplaced_public_sources CONFIGURE_DEPENDS
			LIST_DIRECTORIES false
			"${CMAKE_CURRENT_SOURCE_DIR}/Public/*.cpp"
		)
		if(_vs_misplaced_public_sources)
			list(JOIN _vs_misplaced_public_sources "\n  " _vs_misplaced_public_sources_text)
			message(FATAL_ERROR
				"Implementation .cpp files must live at the module root or under Private/:\n"
				"  ${_vs_misplaced_public_sources_text}")
		endif()
	endif()

	file(GLOB _vs_root_sources CONFIGURE_DEPENDS
		LIST_DIRECTORIES false
		"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	)
	list(APPEND _vs_sources ${_vs_root_sources})

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Private")
		file(GLOB_RECURSE _vs_private_sources CONFIGURE_DEPENDS
			LIST_DIRECTORIES false
			"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
		)
		list(APPEND _vs_sources ${_vs_private_sources})
	endif()

	set(${out_var} "${_vs_sources}" PARENT_SCOPE)
endfunction()

function(vs_collect_module_headers out_var)
	set(_vs_headers "")

	file(GLOB _vs_root_headers CONFIGURE_DEPENDS
		LIST_DIRECTORIES false
		"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
	)
	list(APPEND _vs_headers ${_vs_root_headers})

	file(GLOB _vs_root_inls CONFIGURE_DEPENDS
		LIST_DIRECTORIES false
		"${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
	)
	list(APPEND _vs_headers ${_vs_root_inls})

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Public")
		file(GLOB_RECURSE _vs_public_headers CONFIGURE_DEPENDS
			LIST_DIRECTORIES false
			"${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
		)
		list(APPEND _vs_headers ${_vs_public_headers})

		file(GLOB_RECURSE _vs_public_inls CONFIGURE_DEPENDS
			LIST_DIRECTORIES false
			"${CMAKE_CURRENT_SOURCE_DIR}/Public/*.inl"
		)
		list(APPEND _vs_headers ${_vs_public_inls})
	endif()

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Private")
		file(GLOB_RECURSE _vs_private_headers CONFIGURE_DEPENDS
			LIST_DIRECTORIES false
			"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
		)
		list(APPEND _vs_headers ${_vs_private_headers})

		file(GLOB_RECURSE _vs_private_inls CONFIGURE_DEPENDS
			LIST_DIRECTORIES false
			"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.inl"
		)
		list(APPEND _vs_headers ${_vs_private_inls})
	endif()

	set(${out_var} "${_vs_headers}" PARENT_SCOPE)
endfunction()

function(vs_add_module_headers target_name)
	vs_collect_module_headers(_vs_headers)
	if(NOT _vs_headers)
		return()
	endif()
	set_source_files_properties(${_vs_headers} PROPERTIES HEADER_FILE_ONLY TRUE)
	target_sources(${target_name} PRIVATE ${_vs_headers})
	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
		PREFIX "Source"
		FILES ${_vs_headers})
endfunction()

function(vs_apply_cxx_defaults target_name)
	if(MSVC)
		# The script/runtime code uses C++ exceptions for allocation, filesystem,
		# parsing, package validation, and public API failure boundaries.  MSVC
		# does not enable standard C++ stack-unwind semantics by default, unlike
		# GCC and Clang.  Keep the exception model consistent across platforms.
		# /FS coordinates compiler PDB writes when MSBuild /m compiles a /Zi
		# configuration (Debug, RelWithDebInfo, ASan) in parallel.
		target_compile_options(${target_name} PRIVATE
			"$<$<NOT:$<COMPILE_LANGUAGE:RC>>:/W4;/permissive-;/utf-8;/EHsc;/FS>"
			"$<$<AND:$<NOT:$<COMPILE_LANGUAGE:RC>>,$<CONFIG:Release>>:/Zi>")
		# Release artifacts remain optimized, but retain matching program
		# databases so the separately shipped Symbols package is useful.
		get_target_property(_vs_target_type ${target_name} TYPE)
		if(_vs_target_type STREQUAL "EXECUTABLE"
			OR _vs_target_type STREQUAL "SHARED_LIBRARY"
			OR _vs_target_type STREQUAL "MODULE_LIBRARY")
			target_link_options(${target_name} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
		endif()
	else()
		target_compile_options(${target_name} PRIVATE "$<$<NOT:$<COMPILE_LANGUAGE:RC>>:-Wall;-Wextra>")
	endif()
endfunction()

function(vs_set_solution_folder target_name folder)
	if(TARGET ${target_name})
		set_target_properties(${target_name} PROPERTIES FOLDER "${folder}")
	endif()
endfunction()

function(vs_add_module_include_roots target_name)
	if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Public")
		return()
	endif()

	# Only the declared public tree is part of a module's build interface.
	# Exposing the module root would also make Private/ reachable through an
	# explicitly qualified include, defeating the directory boundary.
	target_include_directories(${target_name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")

	file(GLOB _vs_public_children RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Public"
		LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/Public/*")
	foreach(_vs_child IN LISTS _vs_public_children)
		if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Public/${_vs_child}")
			target_include_directories(${target_name} PUBLIC
				"${CMAKE_CURRENT_SOURCE_DIR}/Public/${_vs_child}")
		endif()
	endforeach()
endfunction()

function(vs_add_module_library target_name)
	vs_collect_module_sources(_vs_sources)
	if(NOT _vs_sources)
		message(FATAL_ERROR "vs_add_module_library(${target_name}): no *.cpp under ${CMAKE_CURRENT_SOURCE_DIR} (module root or Private/)")
	endif()
	add_library(${target_name} STATIC ${_vs_sources})
	target_compile_features(${target_name} PUBLIC cxx_std_17)
	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
		PREFIX "Source"
		FILES ${_vs_sources})
	vs_add_module_headers(${target_name})
	vs_add_module_include_roots(${target_name})
	vs_apply_cxx_defaults(${target_name})
endfunction()

function(vs_add_module_executable target_name)
	vs_collect_module_sources(_vs_sources)
	if(NOT _vs_sources)
		message(FATAL_ERROR "vs_add_module_executable(${target_name}): no *.cpp under ${CMAKE_CURRENT_SOURCE_DIR} (module root or Private/)")
	endif()
	add_executable(${target_name} ${_vs_sources})
	source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
		PREFIX "Source"
		FILES ${_vs_sources})
	vs_add_module_headers(${target_name})
	vs_add_module_include_roots(${target_name})
	vs_apply_cxx_defaults(${target_name})
endfunction()

function(vs_deploy_runtime_file target_name source_file)
	if(NOT EXISTS "${source_file}")
		message(FATAL_ERROR "vs_deploy_runtime_file(${target_name}): source file not found: ${source_file}")
	endif()

	get_filename_component(_vs_runtime_file_name "${source_file}" NAME)
	add_custom_command(TARGET ${target_name} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${source_file}"
			"$<TARGET_FILE_DIR:${target_name}>/${_vs_runtime_file_name}"
		COMMENT "Deploy runtime file ${_vs_runtime_file_name} beside ${target_name}")
endfunction()

function(vs_use_product_icon target_name)
	if(NOT WIN32)
		return()
	endif()
	if(NOT CMAKE_RC_COMPILER_LOADED)
		message(FATAL_ERROR
			"Windows product icons require the RC language. Enable RC in the parent project with a Windows resource compiler.")
	endif()
	get_filename_component(_vs_product_icon
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../Assets/icons/veroscript-icon.ico" ABSOLUTE)
	if(NOT EXISTS "${_vs_product_icon}")
		message(FATAL_ERROR "Missing VeroScript product icon: ${_vs_product_icon}")
	endif()
	# Visual Studio does not honor OBJECT_DEPENDS for resource inputs. Include
	# the icon digest in the RC source so every generator rebuilds changed icons.
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_product_icon}")
	file(SHA256 "${_vs_product_icon}" _vs_product_icon_sha256)
	set(_vs_product_resource "${CMAKE_CURRENT_BINARY_DIR}/${target_name}-icon.rc")
	configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VeroScriptProductIcon.rc.in"
		"${_vs_product_resource}" @ONLY NEWLINE_STYLE LF)
	target_sources(${target_name} PRIVATE "${_vs_product_resource}")
endfunction()

function(vs_add_cli_executable target_name)
	set(_options PRODUCT_ICON)
	set(_one_value DEPLOY_RUNTIME_FILE)
	set(_multi_value SOURCES LINK_LIBRARIES)
	cmake_parse_arguments(_arg "${_options}" "${_one_value}" "${_multi_value}" ${ARGN})

	if(NOT _arg_SOURCES)
		message(FATAL_ERROR "vs_add_cli_executable(${target_name}): SOURCES is required")
	endif()

	add_executable(${target_name} ${_arg_SOURCES})
	if(_arg_LINK_LIBRARIES)
		target_link_libraries(${target_name} PRIVATE ${_arg_LINK_LIBRARIES})
	endif()
	vs_apply_cxx_defaults(${target_name})
	vs_set_solution_folder(${target_name} "CLI")

	if(_arg_PRODUCT_ICON)
		vs_use_product_icon(${target_name})
	endif()

	if(_arg_DEPLOY_RUNTIME_FILE)
		vs_deploy_runtime_file(${target_name} "${_arg_DEPLOY_RUNTIME_FILE}")
	endif()
endfunction()

function(vs_add_root_subdirectories)
	if(DEFINED VEROSCRIPT_MODULE_ROOT AND NOT VEROSCRIPT_MODULE_ROOT STREQUAL "")
		get_filename_component(VEROSCRIPT_MODULE_ROOT "${VEROSCRIPT_MODULE_ROOT}"
			ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
		set(_vs_script_build "${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptBuild.cmake")
	else()
		file(GLOB _vs_script_builds LIST_DIRECTORIES false
			"${CMAKE_SOURCE_DIR}/*/Resources/Tools/CMake/VeroScriptBuild.cmake")
		if(EXISTS "${CMAKE_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptBuild.cmake")
			list(APPEND _vs_script_builds
				"${CMAKE_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptBuild.cmake")
		endif()
		list(LENGTH _vs_script_builds _vs_script_build_count)
		if(NOT _vs_script_build_count EQUAL 1)
			message(FATAL_ERROR
				"Expected one VeroScript build module; set VEROSCRIPT_MODULE_ROOT explicitly")
		endif()
		list(GET _vs_script_builds 0 _vs_script_build)
		get_filename_component(_vs_script_build_dir "${_vs_script_build}" DIRECTORY)
		get_filename_component(VEROSCRIPT_MODULE_ROOT "${_vs_script_build_dir}/../../.." ABSOLUTE)
	endif()
	if(NOT EXISTS "${_vs_script_build}")
		message(FATAL_ERROR "Missing VeroScript build entry: ${_vs_script_build}")
	endif()
	include("${_vs_script_build}")
	vs_resolve_veroscript_root(VEROSCRIPT_MODULE_ROOT "${CMAKE_SOURCE_DIR}")
	set(VEROSCRIPT_MODULE_ROOT "${VEROSCRIPT_MODULE_ROOT}" PARENT_SCOPE)
	vs_get_veroscript_subdirectories(_vs_subdirs _vs_examples "${VEROSCRIPT_MODULE_ROOT}")
	foreach(_vs_source IN LISTS _vs_subdirs _vs_examples)
		if(IS_DIRECTORY "${_vs_source}" AND EXISTS "${_vs_source}/CMakeLists.txt")
			file(RELATIVE_PATH _vs_binary "${VEROSCRIPT_MODULE_ROOT}" "${_vs_source}")
			add_subdirectory("${_vs_source}" "${CMAKE_CURRENT_BINARY_DIR}/${_vs_binary}")
		endif()
	endforeach()
endfunction()

function(vs_add_aggregate_build_targets)
	vs_get_veroscript_native_targets(_vs_targets)
	set(_vs_dependencies)
	foreach(_vs_target IN LISTS _vs_targets)
		if(TARGET ${_vs_target})
			list(APPEND _vs_dependencies ${_vs_target})
		endif()
	endforeach()
	if(_vs_dependencies)
		add_custom_target(build_native DEPENDS ${_vs_dependencies}
			COMMENT "Building VeroScript native modules")
		vs_set_solution_folder(build_native "Build")
	endif()
endfunction()
