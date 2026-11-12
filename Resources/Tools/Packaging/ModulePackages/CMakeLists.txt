# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Producer/consumer helper; uses the same package client as the public CLI.
add_executable(VeroScriptModulePackageTool ModulePackageTool.cpp)
target_link_libraries(VeroScriptModulePackageTool PRIVATE VeroScriptPackageManager VeroScript)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(_packaging_contract "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackagingContract.cpp")
execute_process(COMMAND "${Python3_EXECUTABLE}"
	"${CMAKE_CURRENT_LIST_DIR}/../veroscript_target_contract.py" generate
	--repo-root "${VEROSCRIPT_MODULE_ROOT}" --output "${_packaging_contract}"
	COMMAND_ERROR_IS_FATAL ANY)
add_library(VeroScriptPackagingContract STATIC "${_packaging_contract}")
set_target_properties(VeroScriptPackagingContract PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
target_link_libraries(VeroScriptPackagingContract PRIVATE VeroScript)
vs_apply_cxx_defaults(VeroScriptPackagingContract)
vs_set_solution_folder(VeroScriptPackagingContract "Tools")
add_dependencies(VeroScriptModulePackageTool VeroScriptPackagingContract)
if(TARGET VeroScriptBytecodeVm)
	target_link_libraries(VeroScriptModulePackageTool PRIVATE VeroScriptBytecodeVm)
	target_compile_definitions(VeroScriptModulePackageTool PRIVATE VEROSCRIPT_PACKAGE_TOOL_VM=1)
endif()

function(vs_add_extension_package_tests)
	if(NOT BUILD_TESTING OR CMAKE_CROSSCOMPILING OR NOT TARGET VeroScriptCli)
		return()
	endif()
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	if(NOT Python3_Interpreter_FOUND)
		return()
	endif()
	set(_kinds)
	if(TARGET VeroScriptAsmJitBackend AND VEROSCRIPT_ASMJIT_NATIVE_ENABLED)
		list(APPEND _kinds jit)
	endif()
	if(TARGET VeroScriptGuiToolsModule AND TARGET VeroScriptSdl3 AND TARGET VeroScriptRmlUiModule
		AND VEROSCRIPT_VISUAL_SCRIPT_BINDINGS AND VEROSCRIPT_SDL3_NATIVE_EXTENSION)
		list(APPEND _kinds foundation)
		foreach(_target IN ITEMS VeroScriptPlatformBindings VeroScriptGraphicsBindings VeroScriptAudioBindings
			VeroScriptSdl3 VeroScriptGuiToolsModule VeroScriptRmlUiModule VeroScriptStbModule VeroScriptFontBackendModule)
			if(TARGET ${_target})
				add_dependencies(VeroScriptModulePackageTool ${_target})
			endif()
		endforeach()
	endif()
	foreach(_kind IN LISTS _kinds)
		add_test(NAME VeroScriptExtensionPackageTests.${_kind} COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ExtensionPackageTests.py"
			--repo "${VEROSCRIPT_MODULE_ROOT}" --build "${CMAKE_BINARY_DIR}"
			--tool "$<TARGET_FILE:VeroScriptModulePackageTool>" --cli "$<TARGET_FILE:VeroScriptCli>"
			--config "$<CONFIG>" --kind "${_kind}" --work "${CMAKE_BINARY_DIR}/ExtensionPackageTests/$<CONFIG>"
			CONFIGURATIONS Release)
		set_tests_properties(VeroScriptExtensionPackageTests.${_kind} PROPERTIES TIMEOUT 1200 LABELS "PackageManager;Packaging")
	endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL vs_add_extension_package_tests)
target_include_directories(VeroScriptModulePackageTool PRIVATE "${VEROSCRIPT_MODULE_ROOT}/VeroScriptPackageManager/ThirdParty")
target_compile_definitions(VeroScriptModulePackageTool PRIVATE
	VEROSCRIPT_PACKAGE_TOOL_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
vs_apply_cxx_defaults(VeroScriptModulePackageTool)
vs_set_solution_folder(VeroScriptModulePackageTool "Tools")
if(BUILD_TESTING)
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
endif()
if(BUILD_TESTING AND Python3_Interpreter_FOUND AND TARGET VeroScriptBytecodeVmBackend AND TARGET VeroScriptCli)
	add_test(NAME VeroScriptVmPackageTests COMMAND "${Python3_EXECUTABLE}"
		"${VEROSCRIPT_MODULE_ROOT}/Modules/Execution/VeroScriptBytecodeVm/Resources/Tests/Package.py"
		--repo "${VEROSCRIPT_MODULE_ROOT}" --build "${CMAKE_BINARY_DIR}"
		--tool "$<TARGET_FILE:VeroScriptModulePackageTool>" --cli "$<TARGET_FILE:VeroScriptCli>" --config "$<CONFIG>")
	set_tests_properties(VeroScriptVmPackageTests PROPERTIES LABELS "Vm;PackageManager" TIMEOUT 90)
endif()
