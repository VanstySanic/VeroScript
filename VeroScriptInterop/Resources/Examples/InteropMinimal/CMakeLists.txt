# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
# External Interop SDK consumer used by package smoke tests.
project(VeroScriptInteropMinimal LANGUAGES C)

set(VEROSCRIPT_SDK_PACKAGE_VERSION "0.1.1" CACHE STRING
  "Version of the SDK package under test")
set(VEROSCRIPT_NATIVE_VERSION "0.1.1" CACHE STRING "Version reported by the native runtime")
if(NOT VEROSCRIPT_SDK_PACKAGE_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
	message(FATAL_ERROR "VEROSCRIPT_SDK_PACKAGE_VERSION must be major.minor.patch")
endif()
string(REPLACE "." ";" _veroscript_version_parts "${VEROSCRIPT_SDK_PACKAGE_VERSION}")
list(GET _veroscript_version_parts 0 _veroscript_version_major)
list(GET _veroscript_version_parts 1 _veroscript_version_minor)
math(EXPR _veroscript_rejected_minor "${_veroscript_version_minor} + 1")
set(_veroscript_rejected_version
  "${_veroscript_version_major}.${_veroscript_rejected_minor}.0")
find_package(VeroScriptInterop ${_veroscript_rejected_version} QUIET CONFIG
  PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)
if(VeroScriptInterop_FOUND)
	message(FATAL_ERROR
	  "VeroScriptInterop ${VEROSCRIPT_SDK_PACKAGE_VERSION} accepted unsupported minimum ${_veroscript_rejected_version}")
endif()
find_package(VeroScriptInterop ${VEROSCRIPT_SDK_PACKAGE_VERSION} CONFIG REQUIRED
  PATHS ${CMAKE_PREFIX_PATH} NO_DEFAULT_PATH)

add_executable(veroscript-interop-minimal main.c)
set_target_properties(veroscript-interop-minimal PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_definitions(veroscript-interop-minimal PRIVATE
  VEROSCRIPT_EXPECTED_RUNTIME_SEMVER="${VEROSCRIPT_NATIVE_VERSION}")
target_link_libraries(veroscript-interop-minimal PRIVATE VeroScript::VeroScriptInterop)

if(WIN32)
	add_custom_command(TARGET veroscript-interop-minimal POST_BUILD
	  COMMAND ${CMAKE_COMMAND}
		"-DVEROSCRIPT_SDK_RUNTIME_DIR=$<TARGET_FILE_DIR:VeroScript::VeroScriptInterop>"
		"-DVEROSCRIPT_CONSUMER_DIR=$<TARGET_FILE_DIR:veroscript-interop-minimal>"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/copy-runtime.cmake"
	  VERBATIM)
endif()
enable_testing()
add_test(NAME interop-sdk-consumer COMMAND veroscript-interop-minimal)
set(VEROSCRIPT_SDK_ABI_TEST_SOURCE "" CACHE FILEPATH
  "Optional C11 full ABI test source supplied by the release packager")
if(VEROSCRIPT_SDK_ABI_TEST_SOURCE)
	add_executable(veroscript-interop-abi-proof "${VEROSCRIPT_SDK_ABI_TEST_SOURCE}")
	set_target_properties(veroscript-interop-abi-proof PROPERTIES
	  C_STANDARD 11 C_STANDARD_REQUIRED ON)
	target_link_libraries(veroscript-interop-abi-proof PRIVATE VeroScript::VeroScriptInterop)
	# The minimal target stages runtime files in their shared output directory.
	add_dependencies(veroscript-interop-abi-proof veroscript-interop-minimal)
	add_test(NAME interop-sdk-abi-proof COMMAND veroscript-interop-abi-proof)
endif()
set(VEROSCRIPT_SDK_JIT_BACKEND "" CACHE FILEPATH
  "Optional independently installed JIT backend to validate with this SDK")
if(VEROSCRIPT_SDK_JIT_BACKEND)
	add_test(NAME interop-sdk-consumer-jit COMMAND veroscript-interop-minimal
	  "${VEROSCRIPT_SDK_JIT_BACKEND}")
endif()
set(VEROSCRIPT_SDK_VM_BACKEND "" CACHE FILEPATH
  "Optional independently installed VM backend to validate with this SDK")
if(VEROSCRIPT_SDK_VM_BACKEND)
	add_test(NAME interop-sdk-consumer-vm COMMAND veroscript-interop-minimal
	  --vm "${VEROSCRIPT_SDK_VM_BACKEND}")
endif()
