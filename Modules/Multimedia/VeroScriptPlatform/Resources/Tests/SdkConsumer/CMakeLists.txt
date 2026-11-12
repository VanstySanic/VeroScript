# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptPlatformConsumer LANGUAGES CXX)

find_package(VeroScriptVisual 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Platform)
# Repeated component discovery must be safe and must not import a backend.
find_package(VeroScriptVisual 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Platform)
if(TARGET VeroScript::Sdl3 OR TARGET SDL3::SDL3 OR TARGET Threads::Threads OR
		TARGET VeroScript::Graphics OR TARGET VeroScript::Core OR
		TARGET VeroScript::PlatformBindings OR TARGET VeroScript::GraphicsBindings)
	message(FATAL_ERROR "The public Platform contract imported an unrelated backend or script runtime")
endif()
add_executable(VeroScriptPlatformConsumer main.cpp)
target_link_libraries(VeroScriptPlatformConsumer PRIVATE VeroScript::Platform)
