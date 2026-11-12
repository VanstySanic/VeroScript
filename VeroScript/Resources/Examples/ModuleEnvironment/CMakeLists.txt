# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)
project(VeroScriptModuleEnvironmentExample LANGUAGES CXX)
find_package(VeroScript CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm CONFIG REQUIRED)
add_executable(ModuleEnvironment main.cpp)
target_compile_features(ModuleEnvironment PRIVATE cxx_std_17)
target_link_libraries(ModuleEnvironment PRIVATE VeroScript::VeroScript VeroScript::VeroScriptBytecodeVm)
