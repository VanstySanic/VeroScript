# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
# External C++ integration consumer used by package smoke tests.
project(VeroScriptCppSdkMinimal LANGUAGES CXX)

set(VEROSCRIPT_SDK_PACKAGE_VERSION "0.1.1" CACHE STRING
  "Version of the C++ SDK package under test")
set(VEROSCRIPT_NATIVE_VERSION "0.1.1" CACHE STRING "Version reported by the native tools")
if(NOT VEROSCRIPT_SDK_PACKAGE_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
	message(FATAL_ERROR "VEROSCRIPT_SDK_PACKAGE_VERSION must be major.minor.patch")
endif()
string(REPLACE "." ";" _veroscript_version_parts "${VEROSCRIPT_SDK_PACKAGE_VERSION}")
list(GET _veroscript_version_parts 0 _veroscript_version_major)
list(GET _veroscript_version_parts 1 _veroscript_version_minor)
math(EXPR _veroscript_rejected_minor "${_veroscript_version_minor} + 1")
set(_veroscript_rejected_version
  "${_veroscript_version_major}.${_veroscript_rejected_minor}.0")

find_package(VeroScriptCpp ${_veroscript_rejected_version} QUIET CONFIG
  PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
if(VeroScriptCpp_FOUND)
	message(FATAL_ERROR
	  "VeroScriptCpp ${VEROSCRIPT_SDK_PACKAGE_VERSION} accepted unsupported minimum ${_veroscript_rejected_version}")
endif()
find_package(VeroScriptCpp ${VEROSCRIPT_SDK_PACKAGE_VERSION} EXACT CONFIG REQUIRED
  COMPONENTS Runtime Tools Aot PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
if(NOT VeroScriptCpp_BUILD_ID STREQUAL VeroScript_BUILD_ID)
	message(FATAL_ERROR "The C++ and Core SDK build identities do not match")
endif()
foreach(_required_target IN ITEMS
	VeroScript::VeroScriptCpp
	VeroScript::VeroScriptCppFrontend
	VeroScript::VeroScriptCppBindGen
	VeroScript::VeroScriptCppTranspiler
	VeroScript::VeroScriptCppAot
	VeroScript::VeroScriptCppBindGenCli
	VeroScript::VeroScriptCppTranspilerCli)
	if(NOT TARGET ${_required_target})
		message(FATAL_ERROR "The C++ SDK is missing imported target ${_required_target}")
	endif()
endforeach()

find_package(VeroScriptBytecodeVm CONFIG REQUIRED)

add_executable(veroscript-cpp-sdk-minimal main.cpp)
target_link_libraries(veroscript-cpp-sdk-minimal PRIVATE VeroScript::VeroScriptBytecodeVm
  VeroScript::VeroScriptCpp
  VeroScript::VeroScriptCppFrontend
  VeroScript::VeroScriptCppBindGen
  VeroScript::VeroScriptCppTranspiler
  VeroScript::VeroScriptCppAot)

# Deploy the matching Core SDK's runtime data beside the consumer. The executable
# is built outside the SDK tree, so executable-relative discovery cannot reach
# Core/assets and must not depend on a developer's environment or checkout.
get_target_property(_veroscript_core_archive VeroScript::VeroScript IMPORTED_LOCATION)
get_filename_component(_veroscript_core_lib_dir "${_veroscript_core_archive}" DIRECTORY)
get_filename_component(_veroscript_core_sdk "${_veroscript_core_lib_dir}/.." ABSOLUTE)
if(NOT EXISTS "${_veroscript_core_sdk}/assets/stdlib/delegate.vs"
	OR NOT EXISTS "${_veroscript_core_sdk}/assets/stdlib/compiler.vs")
	message(FATAL_ERROR "The matching Core SDK is missing its standard library assets")
endif()
# A POST_BUILD command on the executable is skipped by Ninja/Make when linking
# is unnecessary. Refresh resources on every consumer build, including repairs
# after a deployed asset was removed or the matching SDK's assets changed.
add_custom_target(veroscript-cpp-sdk-runtime-assets
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
	"${_veroscript_core_sdk}/assets/stdlib" "$<TARGET_FILE_DIR:veroscript-cpp-sdk-minimal>/stdlib"
  VERBATIM)
if(IS_DIRECTORY "${_veroscript_core_sdk}/assets/localization")
	add_custom_command(TARGET veroscript-cpp-sdk-runtime-assets POST_BUILD
	  COMMAND "${CMAKE_COMMAND}" -E copy_directory
		"${_veroscript_core_sdk}/assets/localization" "$<TARGET_FILE_DIR:veroscript-cpp-sdk-minimal>/localization"
	  VERBATIM)
endif()
add_dependencies(veroscript-cpp-sdk-minimal veroscript-cpp-sdk-runtime-assets)

enable_testing()
add_test(NAME cpp-sdk-consumer COMMAND veroscript-cpp-sdk-minimal)

# Compile and link old function pointer signatures and the new file entry using
# only the Runtime component's exported include paths and transitive libraries.
add_executable(veroscript-cpp-sdk-engine engine-compatibility.cpp)
target_link_libraries(veroscript-cpp-sdk-engine PRIVATE VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
if(MINGW)
	target_link_options(veroscript-cpp-sdk-minimal PRIVATE -municode)
	target_link_options(veroscript-cpp-sdk-engine PRIVATE -municode)
endif()
add_test(NAME cpp-sdk-engine-consumer COMMAND veroscript-cpp-sdk-engine
  "${CMAKE_CURRENT_SOURCE_DIR}/engine-compatibility.vs")
set(_veroscript_unicode_script "${CMAKE_CURRENT_BINARY_DIR}/unicode-路径/入口.vs")
configure_file(engine-compatibility.vs "${_veroscript_unicode_script}" COPYONLY)
add_test(NAME cpp-sdk-unicode-engine-consumer COMMAND veroscript-cpp-sdk-engine
  "${_veroscript_unicode_script}")
if(VEROSCRIPT_TEST_JIT_BACKEND)
	add_test(NAME cpp-sdk-jit-consumer COMMAND veroscript-cpp-sdk-minimal "${VEROSCRIPT_TEST_JIT_BACKEND}")
	add_test(NAME cpp-sdk-jit-engine-consumer COMMAND veroscript-cpp-sdk-engine
	  "${CMAKE_CURRENT_SOURCE_DIR}/engine-compatibility.vs" "${VEROSCRIPT_TEST_JIT_BACKEND}")
	add_test(NAME cpp-sdk-unicode-jit-engine-consumer COMMAND veroscript-cpp-sdk-engine
	  "${_veroscript_unicode_script}" "${VEROSCRIPT_TEST_JIT_BACKEND}")
endif()
add_test(NAME cpp-sdk-bindgen-version COMMAND VeroScript::VeroScriptCppBindGenCli --version)
set_tests_properties(cpp-sdk-bindgen-version PROPERTIES
  PASS_REGULAR_EXPRESSION "VeroScriptCppBindGen ${VEROSCRIPT_NATIVE_VERSION}")
add_test(NAME cpp-sdk-transpiler-version COMMAND VeroScript::VeroScriptCppTranspilerCli --version)
set_tests_properties(cpp-sdk-transpiler-version PROPERTIES
  PASS_REGULAR_EXPRESSION "VeroScriptCppTranspiler ${VEROSCRIPT_NATIVE_VERSION}")

# Managed bytes use Core services and a persistent C++ Session.
add_executable(veroscript-memory-sdk-consumer memory.cpp)
target_link_libraries(veroscript-memory-sdk-consumer PRIVATE VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
add_test(NAME memory-sdk-consumer COMMAND veroscript-memory-sdk-consumer)
