# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptAnnotationExample LANGUAGES CXX)
set(VEROSCRIPT_SDK_PACKAGE_VERSION "0.1.1" CACHE STRING "Installed Core SDK version")
find_package(VeroScript ${VEROSCRIPT_SDK_PACKAGE_VERSION} EXACT REQUIRED CONFIG)
find_package(VeroScriptBytecodeVm CONFIG REQUIRED)

add_executable(veroscript-annotation-example main.cpp)
target_link_libraries(veroscript-annotation-example PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
include(CTest)
if(BUILD_TESTING)
	add_test(NAME annotation-example COMMAND veroscript-annotation-example)
	set_tests_properties(annotation-example PROPERTIES PASS_REGULAR_EXPRESSION "42")
endif()
