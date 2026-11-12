# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptAudioSdkTest LANGUAGES CXX)
find_package(VeroScriptVisual 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Audio)
foreach(_vs_unrelated SDL3::SDL3 Threads::Threads VeroScript::Platform VeroScript::Graphics VeroScript::Core)
	if(TARGET ${_vs_unrelated})
		message(FATAL_ERROR "Audio imported unrelated target ${_vs_unrelated}")
	endif()
endforeach()
get_filename_component(_vs_audio "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_executable(VeroScriptAudioSdkTest "${_vs_audio}/Resources/Tests/SdkConsumer.cpp")
target_link_libraries(VeroScriptAudioSdkTest PRIVATE VeroScript::Audio)
enable_testing()
add_test(NAME AudioContractSdk COMMAND VeroScriptAudioSdkTest)
