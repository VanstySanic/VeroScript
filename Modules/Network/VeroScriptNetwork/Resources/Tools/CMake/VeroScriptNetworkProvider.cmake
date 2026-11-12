# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

include_guard(GLOBAL)

function(veroscript_network_provider target)
	if(NOT VEROSCRIPT_NETWORK_BUILD_BINDINGS)
		return()
	endif()
	if(NOT TARGET VeroScript)
		find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
		add_library(VeroScript ALIAS VeroScript::VeroScript)
	endif()
	file(GLOB_RECURSE _inputs CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.inl" "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/*"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptNetwork/Public/*.h"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptNetwork/Internal/*.h"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptNetwork/Private/*.h"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptNetwork/Private/*.cpp"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptNetwork/Private/*.inl"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptHttp/Public/*.h"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptHttp/Internal/*.h"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptWebSocket/Public/*.h"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptWebSocket/Internal/*.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/*.json" "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/patches/*.patch")
	list(APPEND _inputs "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
	list(APPEND _inputs "${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptNetwork/CMakeLists.txt")
	list(FILTER _inputs EXCLUDE REGEX "/VeroScriptNetwork/(Private|Internal)/Binding/")
	list(SORT _inputs)
	set(_identity "${target}:0.1.1:${CMAKE_SYSTEM_NAME}:${CMAKE_SYSTEM_PROCESSOR}:${CMAKE_CXX_COMPILER_ID}:${CMAKE_CXX_COMPILER_VERSION}:${CMAKE_BUILD_TYPE}:${CMAKE_CXX_FLAGS}:${CMAKE_CXX_FLAGS_RELEASE}:${CMAKE_MSVC_RUNTIME_LIBRARY}:tls=${VEROSCRIPT_NETWORK_TLS}:http=${VEROSCRIPT_BOOST_HTTP}:ws=${VEROSCRIPT_BOOST_WEBSOCKET}:compression=${VEROSCRIPT_WEBSOCKET_COMPRESSION}:openssl=${OPENSSL_VERSION}:curl=${CURL_VERSION_STRING}")
	foreach(_input IN LISTS _inputs)
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_input}")
		file(READ "${_input}" _text)
		string(REPLACE "\r\n" "\n" _text "${_text}")
		string(SHA256 _hash "${_text}")
		string(APPEND _identity ":${_hash}")
	endforeach()
	string(SHA256 _identity "${_identity}")
	file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated")
	file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/VeroScriptBackendBuildInfo.h"
		CONTENT "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n#pragma once\n\n#define VEROSCRIPT_BACKEND_IMPLEMENTATION_ID \"@_identity@\"\n" @ONLY)
	add_library(${target}Bindings STATIC "Private/Binding/${target}Module.cpp")
	target_link_libraries(${target}Bindings PUBLIC ${target} "$<INSTALL_INTERFACE:VeroScript::VeroScript>")
	target_include_directories(${target}Bindings PUBLIC
		"$<BUILD_INTERFACE:$<TARGET_PROPERTY:VeroScript,INTERFACE_INCLUDE_DIRECTORIES>>"
		PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")
	target_compile_features(${target}Bindings PUBLIC cxx_std_17)
	target_compile_definitions(${target}Bindings PRIVATE
		VEROSCRIPT_BOOST_HTTP=$<BOOL:${VEROSCRIPT_BOOST_HTTP}>
		VEROSCRIPT_BOOST_WEBSOCKET=$<BOOL:${VEROSCRIPT_BOOST_WEBSOCKET}>)
	string(REGEX REPLACE "^VeroScript" "" _export "${target}")
	set_target_properties(${target}Bindings PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME "${_export}Bindings")
	vs_apply_cxx_defaults(${target}Bindings)
	install(TARGETS ${target}Bindings EXPORT ${target}BindingsTargets ARCHIVE DESTINATION lib COMPONENT ${target}Bindings)
	install(FILES "Public/${target}Module.h" DESTINATION "include/${target}" COMPONENT ${target}Bindings)
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
		add_library(${target}Module SHARED Private/Binding/BackendDynamic.cpp)
		target_link_libraries(${target}Module PRIVATE ${target}Bindings)
		set_target_properties(${target}Module PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
		if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
			set_target_properties(${target}Module PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/../..")
			target_link_options(${target}Module PRIVATE "LINKER:--exclude-libs,ALL")
		elseif(APPLE)
			set_target_properties(${target}Module PROPERTIES INSTALL_RPATH "@loader_path;@loader_path/../..")
			target_link_options(${target}Module PRIVATE "LINKER:-exported_symbol,_veroscript_extension_query")
		endif()
		vs_apply_cxx_defaults(${target}Module)
		install(TARGETS ${target}Module
			RUNTIME DESTINATION lib/veroscript/modules COMPONENT ${target}Bindings
			LIBRARY DESTINATION lib/veroscript/modules COMPONENT ${target}Bindings
			ARCHIVE DESTINATION lib COMPONENT ${target}Bindings)
		vs_install_windows_runtime(lib/veroscript/modules ${target}Bindings)
	endif()
	install(EXPORT ${target}BindingsTargets NAMESPACE VeroScript:: DESTINATION "lib/cmake/${target}" COMPONENT ${target}Bindings)
endfunction()
