# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(PackageManagerEmbed LANGUAGES CXX)
find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptPackageManager 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm CONFIG REQUIRED)

add_executable(package-manager-embed main.cpp)
target_link_libraries(package-manager-embed PRIVATE VeroScript::VeroScriptPackageManager VeroScript::VeroScriptBytecodeVm)
