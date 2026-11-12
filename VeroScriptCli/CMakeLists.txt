# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT TARGET VeroScriptBytecodeVmBackend)
	message(FATAL_ERROR "The desktop Runtime requires VEROSCRIPT_BUILD_VM_BACKEND=ON. Use the embedded-only build for pure Core.")
endif()

vs_add_cli_executable(VeroScriptCli
	PRODUCT_ICON
	SOURCES
		Private/VeroScriptCli.cpp
		Private/VeroScriptCliExtensions.cpp
		Private/VeroScriptCliProject.cpp
		Private/VeroScriptCliRepl.cpp
		Private/VeroScriptCliWatch.cpp
		Private/VeroScriptMain.cpp
	LINK_LIBRARIES VeroScript VeroScriptCpp VeroScriptCppAot VeroScriptCSharpAot
		VeroScriptPythonAot VeroScriptTooling)

target_include_directories(VeroScriptCli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_include_directories(VeroScriptCli PRIVATE "${VEROSCRIPT_MODULE_ROOT}/Modules/Execution/VeroScriptBytecodeVm/Public")
if(TARGET VeroScriptBytecodeVmBackend)
	add_dependencies(VeroScriptCli VeroScriptRuntimeVmAssets)
	target_compile_definitions(VeroScriptCli PRIVATE VEROSCRIPT_RUNTIME_HAS_VM=1)
endif()
if(TARGET VeroScriptBuild)
	target_sources(VeroScriptCli PRIVATE Private/VeroScriptCliBuild.cpp)
	target_link_libraries(VeroScriptCli PRIVATE VeroScriptBuild)
	target_compile_definitions(VeroScriptCli PRIVATE VEROSCRIPT_WITH_BUILD_SYSTEM=1)
endif()
if(TARGET VeroScriptApplicationPackages)
	target_link_libraries(VeroScriptCli PRIVATE VeroScriptApplicationPackages)
endif()
if(MINGW)
	target_link_options(VeroScriptCli PRIVATE -municode)
endif()

set_target_properties(VeroScriptCli PROPERTIES OUTPUT_NAME veroscript)
vs_set_solution_folder(VeroScriptCli "CLI")

if(BUILD_TESTING AND TARGET VeroScriptBuild AND TARGET VeroScriptRuntime AND NOT CMAKE_CROSSCOMPILING AND NOT APPLE)
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		set(_application_protection_test)
		if(VEROSCRIPT_TEST_SODIUM_LIBRARY)
			list(APPEND _application_protection_test --sodium "${VEROSCRIPT_TEST_SODIUM_LIBRARY}")
		endif()
		if(TARGET VeroScriptPackageManager)
			add_test(NAME VeroScriptBuildCliIntegration COMMAND "${Python3_EXECUTABLE}"
				"${VEROSCRIPT_MODULE_ROOT}/VeroScriptBuild/Private/Tests/BuildCliIntegration.py"
				--cli "$<TARGET_FILE:VeroScriptCli>" --runtime "$<TARGET_FILE:VeroScriptRuntime>"
				--repo "${VEROSCRIPT_MODULE_ROOT}" --work "${CMAKE_CURRENT_BINARY_DIR}/BuildIntegration"
				${_application_protection_test})
			set_tests_properties(VeroScriptBuildCliIntegration PROPERTIES LABELS "VeroScript;Application;Build;Cli" TIMEOUT 240)
		endif()
		if(TARGET VeroScriptPackageRuntime AND TARGET VeroScriptTooling)
			add_test(NAME VeroScriptApplicationSdkIntegration COMMAND "${Python3_EXECUTABLE}"
				"${VEROSCRIPT_MODULE_ROOT}/VeroScriptApplication/Private/Tests/ApplicationSdkIntegration.py"
				--repo "${VEROSCRIPT_MODULE_ROOT}" --build "${CMAKE_BINARY_DIR}"
				--cmake "${CMAKE_COMMAND}" --generator "${CMAKE_GENERATOR}" "--platform=${CMAKE_GENERATOR_PLATFORM}"
				--config "$<CONFIG>" --core "$<TARGET_FILE:VeroScript>" --tooling "$<TARGET_FILE:VeroScriptTooling>")
			set_tests_properties(VeroScriptApplicationSdkIntegration PROPERTIES LABELS "VeroScript;Application;Sdk" TIMEOUT 240)
		endif()
	endif()
endif()

if(BUILD_TESTING)
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	# Python launches the target CLI directly rather than through a target-side runner.
	if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
		set(_project_init_build_args)
		if(TARGET VeroScriptBuild)
			list(APPEND _project_init_build_args --build)
		endif()
		add_test(NAME VeroScriptProjectInitCliTests COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/ProjectInitTests.py"
			--cli "$<TARGET_FILE:VeroScriptCli>" --repo "${VEROSCRIPT_MODULE_ROOT}"
			${_project_init_build_args})
		set_tests_properties(VeroScriptProjectInitCliTests PROPERTIES LABELS "VeroScript;CLI;Project" TIMEOUT 120)
	endif()
	add_executable(VeroScriptCliRegistryTests Private/Tests/Modules/RegistryTests.cpp Private/VeroScriptCliExtensions.cpp)
	target_include_directories(VeroScriptCliRegistryTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
	target_link_libraries(VeroScriptCliRegistryTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptCliRegistryTests)
	add_test(NAME VeroScriptCliRegistryTests COMMAND VeroScriptCliRegistryTests)
	set_tests_properties(VeroScriptCliRegistryTests PROPERTIES LABELS "VeroScript;CLI;Modules" TIMEOUT 30)
	if(NOT CMAKE_SYSTEM_NAME MATCHES "^(iOS|Emscripten)$" AND
	   NOT (MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
		foreach(_cli_fixture IN ITEMS VeroScriptCliModuleFixture VeroScriptCliModuleConflict
				VeroScriptCliModuleThrowQuery VeroScriptCliModuleThrowCreate)
			add_library(${_cli_fixture} SHARED Private/Tests/Modules/NativeCliModule.cpp)
			target_include_directories(${_cli_fixture} PRIVATE
				"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public"
				"$<TARGET_PROPERTY:VeroScript,BINARY_DIR>/Generated")
			vs_apply_cxx_defaults(${_cli_fixture})
		endforeach()
		target_compile_definitions(VeroScriptCliModuleConflict PRIVATE VEROSCRIPT_CLI_CONFLICT=1)
		target_compile_definitions(VeroScriptCliModuleThrowQuery PRIVATE VEROSCRIPT_CLI_THROW_QUERY=1)
		target_compile_definitions(VeroScriptCliModuleThrowCreate PRIVATE VEROSCRIPT_CLI_THROW_CREATE=1)
		add_library(VeroScriptCliExampleModule SHARED Resources/Examples/NativeModule/NativeModule.cpp)
		target_include_directories(VeroScriptCliExampleModule PRIVATE
			"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public" "$<TARGET_PROPERTY:VeroScript,BINARY_DIR>/Generated")
		vs_apply_cxx_defaults(VeroScriptCliExampleModule)
		find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
		if(Python3_Interpreter_FOUND)
			add_test(NAME VeroScriptCliModulesTests COMMAND "${Python3_EXECUTABLE}"
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/Modules/test_cli_modules.py"
				--cli "$<TARGET_FILE:VeroScriptCli>" --module "$<TARGET_FILE:VeroScriptCliModuleFixture>"
				--conflict "$<TARGET_FILE:VeroScriptCliModuleConflict>"
				--throw-query "$<TARGET_FILE:VeroScriptCliModuleThrowQuery>"
				--throw-create "$<TARGET_FILE:VeroScriptCliModuleThrowCreate>"
				--example "$<TARGET_FILE:VeroScriptCliExampleModule>"
				--packages "$<BOOL:${VEROSCRIPT_BUILD_PACKAGE_MANAGER}>")
			set_tests_properties(VeroScriptCliModulesTests PROPERTIES LABELS "VeroScript;CLI;Modules" TIMEOUT 180)
		endif()
	endif()
	set(_cli_jit_args "")
	if(TARGET VeroScriptAsmJitBackend)
		set(_cli_jit_args "-DVERO_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>")
	endif()
	add_test(NAME VeroScriptAttributeCliTests COMMAND ${CMAKE_COMMAND}
		"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
		"-DEXAMPLES=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/examples"
		"-DWORK=${CMAKE_CURRENT_BINARY_DIR}/AttributeCli/$<CONFIG>"
		${_cli_jit_args}
		-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptAttributeCliTest.cmake")
	set_tests_properties(VeroScriptAttributeCliTests PROPERTIES LABELS "VeroScript;Core" TIMEOUT 180)
	if(VS_VEROSCRIPT_CSHARP_DOTNET)
		set(_attribute_dotnet "${VS_VEROSCRIPT_CSHARP_DOTNET}")
	else()
		find_program(_attribute_dotnet NAMES dotnet)
	endif()
	if(_attribute_dotnet)
		execute_process(COMMAND "${_attribute_dotnet}" --version
			RESULT_VARIABLE _attribute_sdk_result OUTPUT_VARIABLE _attribute_sdk_version
			OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
		if(NOT _attribute_sdk_result EQUAL 0 OR _attribute_sdk_version VERSION_LESS 8.0)
			set(_attribute_dotnet "")
		endif()
	endif()
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	foreach(_attribute_kind cpp csharp python)
		if(_attribute_kind STREQUAL "csharp" AND NOT _attribute_dotnet)
			continue()
		endif()
		if(_attribute_kind STREQUAL "python" AND NOT Python3_Interpreter_FOUND)
			continue()
		endif()
		add_test(NAME VeroScriptAttribute${_attribute_kind}AotTests COMMAND ${CMAKE_COMMAND}
			"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>" "-DKIND=${_attribute_kind}"
			"-DEXAMPLES=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/examples"
			"-DWORK=${CMAKE_CURRENT_BINARY_DIR}/AttributeAot/${_attribute_kind}/$<CONFIG>"
			"-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
			"-DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}" "-DDOTNET=${_attribute_dotnet}"
			"-DPYTHON=${Python3_EXECUTABLE}"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptAttributeAotTest.cmake")
		set_tests_properties(VeroScriptAttribute${_attribute_kind}AotTests PROPERTIES LABELS "VeroScript;Aot" TIMEOUT 300)
	endforeach()
	add_test(NAME VeroScriptMacroModulesCliTests COMMAND ${CMAKE_COMMAND}
		"-DVERO_CLI=$<TARGET_FILE:VeroScriptTestCli>"
		${_cli_jit_args}
		"-DVERO_WORK=${CMAKE_CURRENT_BINARY_DIR}/MacroModules/$<CONFIG>"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptMacroModulesCliTest.cmake")
	set_tests_properties(VeroScriptMacroModulesCliTests PROPERTIES LABELS "VeroScript;Core;Aot" TIMEOUT 90)
	add_test(NAME VeroScriptJitSelectionCliTests COMMAND ${CMAKE_COMMAND}
		"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
		${_cli_jit_args}
		-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptJitSelectionCliTest.cmake")
	add_test(
		NAME VeroScriptPackageCliTests
		COMMAND ${CMAKE_COMMAND}
			"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
			${_cli_jit_args}
			"-DVERO_PACKAGE=${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageCliTest.vsapp"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptPackageCliTest.cmake")

	add_test(
		NAME VeroScriptIntMainCliTests
		COMMAND ${CMAKE_COMMAND}
			"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
			${_cli_jit_args}
			"-DVERO_APP=${CMAKE_CURRENT_BINARY_DIR}/VeroScriptIntMainCliTest.vsapp"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptIntMainCliTest.cmake")

	add_test(
		NAME VeroScriptEvalCliTests
		COMMAND ${CMAKE_COMMAND}
			"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
			${_cli_jit_args}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptEvalCliTest.cmake")

	add_test(
		NAME VeroScriptUnicodeCliTests
		COMMAND ${CMAKE_COMMAND}
			"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
			${_cli_jit_args}
			"-DVERO_WORK=${CMAKE_CURRENT_BINARY_DIR}/VeroScriptUnicodeCliTest"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptUnicodeCliTest.cmake")

	add_test(
		NAME VeroScriptExamplesCliTests
		COMMAND ${CMAKE_COMMAND}
			"-DVERO_CLI=$<TARGET_FILE:VeroScriptTestCli>"
			${_cli_jit_args}
			"-DVERO_EXAMPLES_ROOT=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/examples"
			"-DVERO_PACKAGE_OUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ExamplesCli"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptExamplesCliTest.cmake")
	set_tests_properties(VeroScriptExamplesCliTests PROPERTIES
		LABELS "VeroScript"
		TIMEOUT 1800)

	if(TARGET VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptExamplesVmCliTests
			COMMAND ${CMAKE_COMMAND}
				"-DVERO_CLI=$<TARGET_FILE:VeroScriptTestCli>"
				"-DVERO_EXAMPLES_ROOT=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/examples"
				"-DVERO_PACKAGE_OUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/ExamplesVmCli"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptExamplesCliTest.cmake")
		set_tests_properties(VeroScriptExamplesVmCliTests PROPERTIES
			LABELS "VeroScript" TIMEOUT 1800)
	endif()

	add_test(
		NAME VeroScriptCSharpAotCliTests
		COMMAND ${CMAKE_COMMAND}
			"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
			${_cli_jit_args}
			"-DVERO_WORK=${CMAKE_CURRENT_BINARY_DIR}/VeroScriptCSharpAotCliTest"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptCSharpAotCliTest.cmake")
	set_tests_properties(VeroScriptCSharpAotCliTests PROPERTIES
		LABELS "VeroScript;CSharp;Aot")

	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(
			NAME VeroScriptPythonAotCliTests
			COMMAND ${CMAKE_COMMAND}
				"-DVERO_CLI=$<TARGET_FILE:VeroScriptCli>"
			${_cli_jit_args}
				"-DVERO_PYTHON=${Python3_EXECUTABLE}"
				"-DVERO_WORK=${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPythonAotCliTest"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptPythonAotCliTest.cmake")
		set_tests_properties(VeroScriptPythonAotCliTests PROPERTIES
			LABELS "VeroScript;Python;Aot")
	endif()
endif()

vs_add_cli_executable(VeroScriptDap
	PRODUCT_ICON
	SOURCES
		Private/VeroScriptDapMain.cpp
	LINK_LIBRARIES VeroScriptTooling)
set_target_properties(VeroScriptDap PROPERTIES OUTPUT_NAME veroscript-dap)

vs_add_cli_executable(VeroScriptLsp
	PRODUCT_ICON
	SOURCES
		Private/VeroScriptLspMain.cpp
	LINK_LIBRARIES VeroScriptTooling)
set_target_properties(VeroScriptLsp PROPERTIES OUTPUT_NAME veroscript-lsp)

foreach(_vm_host IN ITEMS VeroScriptDap VeroScriptLsp)
	if(TARGET VeroScriptBytecodeVm)
		target_link_libraries(${_vm_host} PRIVATE VeroScriptBytecodeVm)
		target_compile_definitions(${_vm_host} PRIVATE VEROSCRIPT_RUNTIME_HAS_VM=1)
	endif()
endforeach()

if(TARGET VeroScriptPackageManager)
	target_link_libraries(VeroScriptCli PRIVATE VeroScriptPackageManager)
	target_compile_definitions(VeroScriptCli PRIVATE VEROSCRIPT_WITH_PACKAGE_MANAGER=1)
endif()

if(BUILD_TESTING AND TARGET VeroScriptPackageManager)
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME VeroScriptPackageManagerCliTests COMMAND "${Python3_EXECUTABLE}" -B
			"${VEROSCRIPT_MODULE_ROOT}/VeroScriptPackageManager/Private/Tests/PackageManagerCliTests.py"
			--cli "$<TARGET_FILE:VeroScriptTestCli>"
			--examples "${VEROSCRIPT_MODULE_ROOT}/VeroScriptPackageManager/Resources/Examples")
		set_tests_properties(VeroScriptPackageManagerCliTests PROPERTIES LABELS "VeroScript;PackageManager;Cli" TIMEOUT 1200)
		add_test(NAME VeroScriptPackageManagerSdkTests COMMAND "${Python3_EXECUTABLE}" -B
			"${VEROSCRIPT_MODULE_ROOT}/VeroScriptPackageManager/Private/Tests/PackageManagerSdkTests.py"
			--source "${VEROSCRIPT_MODULE_ROOT}" --build "${CMAKE_BINARY_DIR}"
			--cmake "${CMAKE_COMMAND}" --generator "${CMAKE_GENERATOR}"
			"--platform=${CMAKE_GENERATOR_PLATFORM}" --config "$<CONFIG>"
			--core "$<TARGET_FILE:VeroScript>" --tooling "$<TARGET_FILE:VeroScriptTooling>"
			--core-generated "$<TARGET_PROPERTY:VeroScript,BINARY_DIR>/Generated"
			--cli "$<TARGET_FILE:VeroScriptCli>")
		set_tests_properties(VeroScriptPackageManagerSdkTests PROPERTIES LABELS "VeroScript;PackageManager;Sdk" TIMEOUT 240)
	endif()
endif()

# Core CLI regressions use the Core host policy. The GUI integration below the
# CLI module adds conservative framework imports for opaque macros/generators;
# its real executable is exercised separately by the GUI integration scenarios.
if(BUILD_TESTING)
	if(VEROSCRIPT_BUILD_GUI AND (NOT DEFINED VEROSCRIPT_GUI_SCRIPTING OR VEROSCRIPT_GUI_SCRIPTING))
		get_target_property(_vs_core_cli_sources VeroScriptCli SOURCES)
		get_target_property(_vs_core_cli_libraries VeroScriptCli LINK_LIBRARIES)
		vs_add_cli_executable(VeroScriptCliCoreTests
			SOURCES ${_vs_core_cli_sources}
			LINK_LIBRARIES ${_vs_core_cli_libraries})
		foreach(_vs_core_cli_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_OPTIONS)
			get_target_property(_vs_core_cli_values VeroScriptCli ${_vs_core_cli_property})
			if(_vs_core_cli_values)
				set_property(TARGET VeroScriptCliCoreTests PROPERTY ${_vs_core_cli_property} "${_vs_core_cli_values}")
			endif()
		endforeach()
		get_target_property(_vs_core_cli_dependencies VeroScriptCli MANUALLY_ADDED_DEPENDENCIES)
		if(_vs_core_cli_dependencies)
			add_dependencies(VeroScriptCliCoreTests ${_vs_core_cli_dependencies})
		endif()
		vs_set_solution_folder(VeroScriptCliCoreTests "Tests/CLI")
		add_executable(VeroScriptTestCli ALIAS VeroScriptCliCoreTests)
	else()
		add_executable(VeroScriptTestCli ALIAS VeroScriptCli)
	endif()
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	# This Python test launches the target CLI directly on the build host.
	if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
		add_test(NAME VeroScriptCliConfigurationTests COMMAND "${Python3_EXECUTABLE}" -B
			"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptCliConfigurationTests.py"
			$<TARGET_FILE:VeroScriptTestCli>
			"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/cli-configuration.json")
		set_tests_properties(VeroScriptCliConfigurationTests PROPERTIES TIMEOUT 60)
	endif()
endif()
