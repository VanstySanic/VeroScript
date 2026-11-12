# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptCliExample LANGUAGES CXX)
find_package(VeroScript CONFIG REQUIRED)
add_library(VeroScriptCliExample SHARED NativeModule.cpp)
target_compile_features(VeroScriptCliExample PRIVATE cxx_std_17)
# Consume the matched SDK's headers/definitions without linking a second Core.
target_include_directories(VeroScriptCliExample PRIVATE
	"$<TARGET_PROPERTY:VeroScript::VeroScript,INTERFACE_INCLUDE_DIRECTORIES>")
target_compile_definitions(VeroScriptCliExample PRIVATE
	"$<TARGET_PROPERTY:VeroScript::VeroScript,INTERFACE_COMPILE_DEFINITIONS>")
