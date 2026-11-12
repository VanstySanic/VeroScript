# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptCompileProcessingExample LANGUAGES CXX)
set(VEROSCRIPT_SDK_PACKAGE_VERSION "0.1.1" CACHE STRING "Installed Core SDK version")
find_package(VeroScript ${VEROSCRIPT_SDK_PACKAGE_VERSION} EXACT REQUIRED CONFIG)
find_package(VeroScriptBytecodeVm CONFIG REQUIRED)

add_executable(veroscript-compile-processing-example main.cpp)
target_link_libraries(veroscript-compile-processing-example PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
include(CTest)
if(BUILD_TESTING)
	add_test(NAME compile-processing-example COMMAND veroscript-compile-processing-example)
	set_tests_properties(compile-processing-example PROPERTIES PASS_REGULAR_EXPRESSION "Save")
endif()
