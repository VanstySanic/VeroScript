# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT TARGET VeroScriptPlatform OR NOT TARGET VeroScriptGraphics)
	message(FATAL_ERROR "GUI requires the Platform and Graphics public contracts")
endif()
set(_vs_gui_script_default OFF)
if(TARGET VeroScriptCpp)
	set(_vs_gui_script_default ON)
endif()
option(VEROSCRIPT_GUI_SCRIPTING "Build the GUI script and compiler adapter" ${_vs_gui_script_default})
option(VEROSCRIPT_GUI_BUILD_RMLUI "Build the vendored RmlUi GUI adapter" ON)
set(VEROSCRIPT_GUI_USE_VISUAL_SDK ON)
add_subdirectory("${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/VeroScriptGui"
	"${CMAKE_CURRENT_BINARY_DIR}/Modules/Multimedia/VeroScriptGui")
if(TARGET VeroScriptRuntime AND TARGET VeroScriptGuiScript)
	target_link_libraries(VeroScriptRuntime PRIVATE VeroScriptGuiScript)
	target_compile_definitions(VeroScriptRuntime PRIVATE VEROSCRIPT_WITH_GUI=1)
endif()
if(TARGET VeroScriptCli AND TARGET VeroScriptGuiScript)
	target_link_libraries(VeroScriptCli PRIVATE VeroScriptGuiScript)
	target_compile_definitions(VeroScriptCli PRIVATE VEROSCRIPT_WITH_GUI=1)
	if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
		find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
		if(Python3_Interpreter_FOUND)
			set(_vs_gui_test_application OFF)
			if(TARGET VeroScriptApplication)
				set(_vs_gui_test_application ON)
			endif()
			set(_vs_gui_runtime_test_args)
			if(TARGET VeroScriptRuntime)
				list(APPEND _vs_gui_runtime_test_args --runtime "$<TARGET_FILE:VeroScriptRuntime>")
			endif()
			foreach(_vs_gui_cli_case Source Artifact Dispatch Metadata)
				string(TOLOWER "${_vs_gui_cli_case}" _vs_gui_cli_scenario)
				add_test(NAME "VeroScriptGuiCliIntegrationTests.${_vs_gui_cli_case}" COMMAND "${Python3_EXECUTABLE}"
					"${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/VeroScriptGui/Resources/Tests/CommandLineTests.py"
					--cli "$<TARGET_FILE:VeroScriptCli>"
					--stdlib "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
					--application ${_vs_gui_test_application}
					${_vs_gui_runtime_test_args}
					--scenario "${_vs_gui_cli_scenario}")
				# Bound each group separately while retaining every source/artifact command.
				set_tests_properties("VeroScriptGuiCliIntegrationTests.${_vs_gui_cli_case}"
					PROPERTIES TIMEOUT 1200 LABELS "VeroScript;Gui;CLI")
			endforeach()
		endif()
	endif()
endif()
if(VEROSCRIPT_GUI_BUILD_RMLUI)
	add_subdirectory("${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/Backend/VeroScriptRmlUi"
		"${CMAKE_CURRENT_BINARY_DIR}/Modules/Multimedia/Backend/VeroScriptRmlUi")
endif()

# Generate release evidence after Gui and all of its resource/provider targets.
include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptFoundationSdk.cmake")
vs_configure_foundation_release()
