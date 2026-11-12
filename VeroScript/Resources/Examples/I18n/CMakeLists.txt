# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptI18nExample LANGUAGES CXX)
set(VEROSCRIPT_SDK_PACKAGE_VERSION "0.1.1" CACHE STRING "Installed Core SDK version")
find_package(VeroScript ${VEROSCRIPT_SDK_PACKAGE_VERSION} EXACT REQUIRED CONFIG)
find_package(VeroScriptBytecodeVm CONFIG REQUIRED)

add_executable(veroscript-i18n-example main.cpp)
target_link_libraries(veroscript-i18n-example PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
if(MSVC)
	target_compile_options(veroscript-i18n-example PRIVATE /utf-8)
endif()
include(CTest)
if(BUILD_TESTING)
	add_test(NAME i18n-example COMMAND veroscript-i18n-example)
	set_tests_properties(i18n-example PROPERTIES PASS_REGULAR_EXPRESSION "18446744073709551615")
endif()
