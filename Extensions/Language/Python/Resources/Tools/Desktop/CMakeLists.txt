# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptDesktopPythonExtension LANGUAGES CXX)
if(NOT VEROSCRIPT_MODULE_ROOT OR NOT VEROSCRIPT_INTEROP_PREFIX)
	message(FATAL_ERROR "Provide the source root and the already built target Interop SDK")
endif()
find_package(VeroScriptInterop 0.1.1 EXACT REQUIRED CONFIG PATHS "${VEROSCRIPT_INTEROP_PREFIX}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
include("${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/Python/VeroScriptPython/Resources/Tools/CMake/VeroScriptPythonDesktopCross.cmake")
target_link_libraries(VeroScriptPython PRIVATE VeroScript::VeroScriptInterop)
target_include_directories(VeroScriptPython PRIVATE "${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/Python/VeroScriptPython/Private")
target_compile_features(VeroScriptPython PRIVATE cxx_std_17)
set_target_properties(VeroScriptPython PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extension"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extension"
	BUILD_WITH_INSTALL_RPATH ON)
if(APPLE)
	set_property(TARGET VeroScriptPython PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
	set_property(TARGET VeroScriptPython PROPERTY INSTALL_RPATH "$ORIGIN")
elseif(MSVC)
	target_link_options(VeroScriptPython PRIVATE "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:VeroScriptPython>")
endif()
