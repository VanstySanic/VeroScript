# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
# External Core SDK consumer used by package smoke tests.
project(VeroScriptSdkEmbedMinimal LANGUAGES CXX)

set(VEROSCRIPT_SDK_PACKAGE_VERSION "0.1.1" CACHE STRING
  "Version of the SDK package under test")
if(NOT VEROSCRIPT_SDK_PACKAGE_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
	message(FATAL_ERROR "VEROSCRIPT_SDK_PACKAGE_VERSION must be major.minor.patch")
endif()
string(REPLACE "." ";" _veroscript_version_parts "${VEROSCRIPT_SDK_PACKAGE_VERSION}")
list(GET _veroscript_version_parts 0 _veroscript_version_major)
list(GET _veroscript_version_parts 1 _veroscript_version_minor)
math(EXPR _veroscript_rejected_minor "${_veroscript_version_minor} + 1")
set(_veroscript_rejected_version
  "${_veroscript_version_major}.${_veroscript_rejected_minor}.0")
find_package(VeroScript ${_veroscript_rejected_version} QUIET CONFIG
  PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
if(VeroScript_FOUND)
	message(FATAL_ERROR
	  "VeroScript ${VEROSCRIPT_SDK_PACKAGE_VERSION} accepted unsupported minimum ${_veroscript_rejected_version}")
endif()
find_package(VeroScript ${VEROSCRIPT_SDK_PACKAGE_VERSION} CONFIG REQUIRED
  PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)

find_package(VeroScriptBytecodeVm CONFIG REQUIRED)

add_executable(veroscript-sdk-embed-minimal core.cpp)
target_link_libraries(veroscript-sdk-embed-minimal PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
if(MINGW)
	target_link_options(veroscript-sdk-embed-minimal PRIVATE -municode)
endif()

add_executable(veroscript-sdk-tooling-minimal tooling.cpp)
target_link_libraries(veroscript-sdk-tooling-minimal PRIVATE VeroScript::VeroScriptTooling VeroScript::VeroScriptBytecodeVm)

if(EXISTS "${VeroScript_DIR}/../VeroScriptPackageManager/VeroScriptPackageManagerConfig.cmake")
	find_package(VeroScriptPackageManager REQUIRED CONFIG
	  PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
	add_executable(veroscript-sdk-packages-minimal packages.cpp)
	target_link_libraries(veroscript-sdk-packages-minimal PRIVATE VeroScript::VeroScriptPackageManager)
endif()

add_executable(veroscript-sdk-template-minimal templates.cpp)
target_link_libraries(veroscript-sdk-template-minimal PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)

enable_testing()
add_test(NAME template-sdk-consumer COMMAND veroscript-sdk-template-minimal)
set_tests_properties(template-sdk-consumer PROPERTIES TIMEOUT 30)
add_test(NAME core-sdk-consumer COMMAND veroscript-sdk-embed-minimal)
add_test(NAME tooling-sdk-consumer COMMAND veroscript-sdk-tooling-minimal)
set_tests_properties(core-sdk-consumer tooling-sdk-consumer PROPERTIES TIMEOUT 30)
if(TARGET veroscript-sdk-packages-minimal)
	add_test(NAME package-manager-sdk-consumer COMMAND veroscript-sdk-packages-minimal)
	set_tests_properties(package-manager-sdk-consumer PROPERTIES TIMEOUT 30)
endif()

if(VEROSCRIPT_TEST_JIT_BACKEND)
	add_test(NAME dynamic-jit-sdk-consumer COMMAND veroscript-sdk-embed-minimal "${VEROSCRIPT_TEST_JIT_BACKEND}")
endif()
