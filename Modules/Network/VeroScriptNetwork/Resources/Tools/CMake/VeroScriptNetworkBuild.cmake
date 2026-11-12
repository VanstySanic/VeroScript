# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

include_guard(GLOBAL)



function(veroscript_network_test target)
	target_link_libraries(${target} PRIVATE VeroScriptNetworkProvider)
	if(TARGET VeroScriptBoost)
		target_link_libraries(${target} PRIVATE VeroScriptBoost)
	endif()
	if(TARGET VeroScriptCurl)
		target_link_libraries(${target} PRIVATE VeroScriptCurl)
	endif()
	if(TARGET VeroScriptBoostBindings)
		target_link_libraries(${target} PRIVATE VeroScriptBoostBindings)
	endif()
	if(TARGET VeroScriptCurlBindings)
		target_link_libraries(${target} PRIVATE VeroScriptCurlBindings)
		target_compile_definitions(${target} PRIVATE VEROSCRIPT_NETWORK_TEST_CURL_PROVIDER=1)
	endif()
	foreach(_backend IN ITEMS Boost Curl)
		if(TARGET VeroScript${_backend}Module)
			add_dependencies(${target} VeroScript${_backend}Module)
			string(TOUPPER "${_backend}" _define)
			target_compile_definitions(${target} PRIVATE
				"VEROSCRIPT_TEST_${_define}_MODULE=\"$<TARGET_FILE:VeroScript${_backend}Module>\"")
		endif()
	endforeach()
	if(WIN32 AND TARGET VeroScriptBoostRuntime)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:VeroScriptBoostRuntime>"
				"$<TARGET_FILE_DIR:${target}>" VERBATIM)
	endif()
endfunction()

function(veroscript_network_target target)
	target_compile_features(${target} PUBLIC cxx_std_17)
	target_include_directories(${target}
		PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/${target}>"
		PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
	target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:VeroScriptNetworkProvider>")
	set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
	if(MSVC)
		target_compile_definitions(${target} PRIVATE _WIN32_WINNT=0x0a00)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc /bigobj)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -fexceptions)
	endif()
endfunction()

function(veroscript_network_binding target)
	if(NOT VEROSCRIPT_NETWORK_BUILD_BINDINGS OR NOT TARGET VeroScript)
		return()
	endif()
	get_filename_component(_foundation "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
	file(GLOB_RECURSE _inputs CONFIGURE_DEPENDS
		"${_foundation}/Public/*.h" "${_foundation}/Internal/*.h"
		"${_foundation}/Private/*.cpp" "${_foundation}/Private/*.h"
		"${_foundation}/Private/*.inl" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/*"
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/*"
		"${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/patches/*.patch"
		"${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.inl")
	list(FILTER _inputs EXCLUDE REGEX "/Private/Tests/")
	list(APPEND _inputs "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${_foundation}/CMakeLists.txt")
	list(REMOVE_DUPLICATES _inputs)
	list(SORT _inputs)
	set(_identity "${target}:0.1.1:${CMAKE_SYSTEM_NAME}:${CMAKE_SYSTEM_PROCESSOR}:${CMAKE_CXX_COMPILER_ID}:${CMAKE_CXX_COMPILER_VERSION}:${CMAKE_BUILD_TYPE}:${CMAKE_CXX_FLAGS}:${CMAKE_CXX_FLAGS_RELEASE}:${CMAKE_MSVC_RUNTIME_LIBRARY}")
	string(APPEND _identity ":provider-contracts=0.1.1")
	foreach(_input IN LISTS _inputs)
		file(READ "${_input}" _text)
		string(REPLACE "\r\n" "\n" _text "${_text}")
		string(SHA256 _hash "${_text}")
		string(APPEND _identity ":${_hash}")
	endforeach()
	string(SHA256 VEROSCRIPT_NETWORK_IMPLEMENTATION_ID "${_identity}")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inputs})
	configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/VeroScriptNetworkBuildInfo.h.in"
		Generated/VeroScriptNetworkBuildInfo.h @ONLY)
	add_library(${target}Bindings STATIC "Private/Binding/${target}Module.cpp")
	file(GLOB _binding_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Private/Binding/*.cpp")
	list(REMOVE_ITEM _binding_sources "${CMAKE_CURRENT_SOURCE_DIR}/Private/Binding/${target}Module.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/Binding/NetworkDynamic.cpp")
	target_sources(${target}Bindings PRIVATE ${_binding_sources})
	target_link_libraries(${target}Bindings PRIVATE "$<BUILD_INTERFACE:VeroScriptNetworkBindingSupport>")
	target_link_libraries(${target}Bindings PUBLIC ${target})
	target_link_libraries(${target}Bindings PUBLIC "$<INSTALL_INTERFACE:VeroScript::VeroScript>")
	target_include_directories(${target}Bindings PUBLIC
		"$<BUILD_INTERFACE:$<TARGET_PROPERTY:VeroScript,INTERFACE_INCLUDE_DIRECTORIES>>"
		PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private/Binding" "${CMAKE_CURRENT_SOURCE_DIR}/Internal" "${CMAKE_CURRENT_BINARY_DIR}/Generated")
	target_compile_features(${target}Bindings PUBLIC cxx_std_17)
	string(REGEX REPLACE "^VeroScript" "" _binding_export "${target}Bindings")
	set_target_properties(${target}Bindings PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME "${_binding_export}")
	install(TARGETS ${target}Bindings EXPORT ${target}BindingsTargets
		ARCHIVE DESTINATION lib COMPONENT ${target}Bindings)
	install(FILES "Public/${target}Module.h" DESTINATION "include/${target}" COMPONENT ${target}Bindings)
	if(MSVC)
		target_compile_options(${target}Bindings PRIVATE /W4 /utf-8 /EHsc)
	else()
		target_compile_options(${target}Bindings PRIVATE -Wall -Wextra -Wpedantic -fexceptions)
	endif()
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
		vs_install_windows_runtime(lib/veroscript/modules ${target}Bindings)
		add_library(${target}Module SHARED Private/Binding/NetworkDynamic.cpp)
		target_link_libraries(${target}Module PRIVATE ${target}Bindings)
		if(target STREQUAL "VeroScriptNetwork")
			# Generation leases use the public Core root service. Data SDKs stay independent.
			target_link_libraries(${target}Module PRIVATE VeroScript)
		endif()
		set_target_properties(${target}Module PROPERTIES CXX_VISIBILITY_PRESET hidden
			VISIBILITY_INLINES_HIDDEN YES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
		if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
			set_target_properties(${target}Module PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/../..")
			target_link_options(${target}Module PRIVATE "LINKER:--exclude-libs,ALL")
		elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
			set_target_properties(${target}Module PROPERTIES INSTALL_RPATH "@loader_path;@loader_path/../..")
			target_link_options(${target}Module PRIVATE "LINKER:-exported_symbol,_veroscript_extension_query")
		endif()
		install(TARGETS ${target}Module LIBRARY DESTINATION lib/veroscript/modules COMPONENT ${target}Bindings
			RUNTIME DESTINATION lib/veroscript/modules COMPONENT ${target}Bindings
			ARCHIVE DESTINATION lib COMPONENT ${target}Bindings)
		if(MSVC)
			target_compile_options(${target}Module PRIVATE /utf-8 /EHsc)
		endif()
	endif()
endfunction()
