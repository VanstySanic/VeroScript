# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptApplicationConsumer LANGUAGES CXX)
find_package(VeroScriptApplication 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptPackageRuntime 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBuild 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptBytecodeVm CONFIG REQUIRED)

add_executable(application-consumer main.cpp)
target_link_libraries(application-consumer PRIVATE VeroScript::VeroScriptApplication
	VeroScript::VeroScriptPackageRuntime VeroScript::VeroScriptBuild)

target_link_libraries(application-consumer PRIVATE VeroScript::VeroScriptBytecodeVm)
