# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Native modules link the foundation statically. Its opaque state must match across images.
file(GLOB_RECURSE _foundation_inputs CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Internal/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.inl")
list(FILTER _foundation_inputs EXCLUDE REGEX "/Binding/")
get_filename_component(_foundation_build_helper
	"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptNetworkBuild.cmake" ABSOLUTE)
list(APPEND _foundation_inputs "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${_foundation_build_helper}")
list(SORT _foundation_inputs)
set(_foundation_identity "${CMAKE_SYSTEM_NAME}:${CMAKE_SYSTEM_PROCESSOR}:${CMAKE_SIZEOF_VOID_P}:${CMAKE_CXX_COMPILER_ID}:${CMAKE_CXX_COMPILER_VERSION}:${CMAKE_CXX_FLAGS}:${CMAKE_CXX_FLAGS_RELEASE}:${CMAKE_CXX_FLAGS_DEBUG}:${CMAKE_CXX_FLAGS_RELWITHDEBINFO}:${CMAKE_CXX_FLAGS_MINSIZEREL}:${CMAKE_MSVC_RUNTIME_LIBRARY}")
foreach(_input IN LISTS _foundation_inputs)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_input}")
	file(READ "${_input}" _text)
	string(REPLACE "\r\n" "\n" _text "${_text}")
	string(SHA256 _hash "${_text}")
	string(APPEND _foundation_identity ":${_hash}")
endforeach()
string(SHA256 _foundation_identity "${_foundation_identity}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/$<CONFIG>/VeroScriptNetworkIdentity.h"
	CONTENT "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n#pragma once\n\n#define VEROSCRIPT_NETWORK_FOUNDATION_ID \"${_foundation_identity}:$<CONFIG>\"\n")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/veroscript-network-identity-$<CONFIG>.txt"
	CONTENT "${_foundation_identity}:$<CONFIG>\n")
add_library(VeroScriptNetwork STATIC Private/VeroScriptNetwork.cpp Private/NetworkMemory.cpp Private/NetworkProvider.cpp
	Private/NetworkUrl.cpp Private/NetworkUrlResolve.cpp Private/NetworkStream.cpp Private/NetworkBackend.cpp
	Private/NetworkFiles.cpp Private/NetworkFileNative.cpp Private/NetworkUpgrade.cpp)
add_library(VeroScript::Network ALIAS VeroScriptNetwork)
veroscript_network_target(VeroScriptNetwork)
target_include_directories(VeroScriptNetwork PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated/$<CONFIG>")
set_target_properties(VeroScriptNetwork PROPERTIES EXPORT_NAME Network)
target_link_libraries(VeroScriptNetwork PUBLIC Threads::Threads)
if(NOT WIN32)
	target_compile_definitions(VeroScriptNetwork PRIVATE _FILE_OFFSET_BITS=64)
endif()
install(TARGETS VeroScriptNetwork EXPORT VeroScriptNetworkTargets ARCHIVE DESTINATION lib COMPONENT VeroScriptNetwork)
install(DIRECTORY Public/ DESTINATION include/VeroScriptNetwork COMPONENT VeroScriptNetwork
	FILES_MATCHING PATTERN "*.h" PATTERN "*Module.h" EXCLUDE PATTERN "*Providers.h" EXCLUDE PATTERN "*ProviderHost.h" EXCLUDE)
install(FILES Internal/NetworkProvider.h Internal/NetworkStream.h Internal/NetworkDiagnostics.h Internal/NetworkUpgrade.h
	DESTINATION include/VeroScriptNetwork/Backend COMPONENT VeroScriptNetwork)
install(TARGETS VeroScriptNetworkProvider EXPORT VeroScriptNetworkTargets COMPONENT VeroScriptNetwork)
install(DIRECTORY Resources/Localization DESTINATION share/VeroScriptNetwork/Resources COMPONENT VeroScriptNetwork)
install(DIRECTORY Resources/Docs DESTINATION share/VeroScriptNetwork/Resources COMPONENT VeroScriptNetwork)
install(DIRECTORY Resources/Examples DESTINATION share/VeroScriptNetwork/Resources COMPONENT VeroScriptNetworkExamples)
install(FILES "${VEROSCRIPT_MODULE_ROOT}/LICENSE" DESTINATION licenses/VeroScriptNetwork COMPONENT VeroScriptNetwork)
veroscript_network_binding(VeroScriptNetwork)
if(TARGET VeroScriptNetworkBindings)
	install(FILES Public/VeroScriptNetworkProviders.h Public/VeroScriptNetworkProviderHost.h
		DESTINATION include/VeroScriptNetwork COMPONENT VeroScriptNetworkBindings)
endif()
