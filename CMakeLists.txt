# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
	message(FATAL_ERROR
		"VeroScript requires an out-of-source build. Use cmake --preset <preset> or cmake -S . -B build/native.")
endif()

project(VeroScript VERSION 0.1.1 LANGUAGES CXX)

# Windows product executables embed the maintained application icon. Library-only
# builds can still configure without a resource compiler.
if(WIN32 AND NOT CMAKE_RC_COMPILER_LOADED)
	include(CheckLanguage)
	check_language(RC)
	if(CMAKE_RC_COMPILER)
		enable_language(RC)
	endif()
endif()

# A single opt-in build gate keeps Core, backend and host ABI consumers aligned.
# Dependencies are collected after all module CMakeLists have been processed.
function(veroscript_add_mobile_execution_validation)
	if(NOT BUILD_TESTING OR CMAKE_CROSSCOMPILING)
		return()
	endif()
	add_custom_target(VeroScriptMobileExecutionValidation)
	foreach(test IN ITEMS VeroScriptControlledExecutionTests VeroScriptExecutionGovernanceTests
		VeroScriptCppCoreAotTests VeroScriptCoreAotGenerate VeroScriptBytecodeIOTests
		VeroScriptJitContractTests VeroScriptExceptionTests VeroScriptSessionApiTests
		VeroScriptMobileTests VeroScriptAndroidHostSmoke VeroScriptInteropCHeaderTest
		VeroScriptInteropExtendedTests VeroScriptAsmJitExceptionTests
		VeroScriptSessionJitCacheTests VeroScriptCppJitIntegrationTests VeroScriptBackendDifferentialTests)
		if(TARGET ${test})
			add_dependencies(VeroScriptMobileExecutionValidation ${test})
		endif()
	endforeach()
	vs_set_solution_folder(VeroScriptMobileExecutionValidation "Tests/Mobile")
endfunction()
cmake_language(DEFER CALL veroscript_add_mobile_execution_validation)

set(VEROSCRIPT_MOBILE_TARGET OFF)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
	set(VEROSCRIPT_MOBILE_TARGET ON)
endif()
set(_vs_default_embedded_only ${VEROSCRIPT_MOBILE_TARGET})
if(VS_BUILD_VEROSCRIPT_PYTHON_ONLY)
	set(_vs_default_embedded_only OFF)
endif()
option(VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY
	"Build Core, C++ embedding, C Interop and mobile adapters without desktop tools" ${_vs_default_embedded_only})
option(VEROSCRIPT_BUILD_MOBILE_ADAPTERS "Build mobile embedding adapters" ${VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY})
if(VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY OR VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY OR VS_BUILD_VEROSCRIPT_PYTHON_ONLY)
	set(_vs_default_package_manager OFF)
else()
	set(_vs_default_package_manager ON)
endif()
option(VEROSCRIPT_BUILD_PACKAGE_MANAGER "Build the optional package-management client" ${_vs_default_package_manager})
set(_vs_default_application ${_vs_default_package_manager})
if(ANDROID)
	set(_vs_default_application ON)
endif()
option(VEROSCRIPT_BUILD_APPLICATION "Build application containers and local runtime services" ${_vs_default_application})
option(VEROSCRIPT_BUILD_PACKAGE_RUNTIME "Build read-only package environment runtime services" ${VEROSCRIPT_BUILD_PACKAGE_MANAGER})
option(VEROSCRIPT_BUILD_BUILD_SYSTEM "Build the optional build and packaging orchestrator" ${_vs_default_package_manager})
if(VEROSCRIPT_BUILD_BUILD_SYSTEM AND NOT VEROSCRIPT_BUILD_APPLICATION)
	message(FATAL_ERROR "The build system requires VEROSCRIPT_BUILD_APPLICATION=ON")
endif()

if(NOT DEFINED CMAKE_CXX_STANDARD)
	set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_CXX_STANDARD LESS 17)
	message(FATAL_ERROR "VeroScript requires C++17 or newer")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optional release-only whole-program optimization. Kept opt-in because static
# library embedders may need to coordinate linker/LTO toolchains, while packaged
# CLI/application builds can enable it with -DVS_ENABLE_IPO=ON.
option(VS_ENABLE_IPO "Enable supported interprocedural optimization for Release builds" OFF)
option(VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY
	"Build only the VeroScript runtime and WebAssembly SDK modules" OFF)
option(VS_BUILD_VEROSCRIPT_PYTHON_ONLY
	"Build only the VeroScript runtime and CPython binding modules" OFF)
if((VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY AND VS_BUILD_VEROSCRIPT_PYTHON_ONLY)
	OR (VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY AND (VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY OR VS_BUILD_VEROSCRIPT_PYTHON_ONLY)))
	message(FATAL_ERROR
		"The EMBEDDED_ONLY, WEBASSEMBLY_ONLY and PYTHON_ONLY build modes are mutually exclusive")
endif()
set(VS_PGO_PHASE "OFF" CACHE STRING
	"Profile-guided optimization phase for Release: OFF, GENERATE, or USE")
set_property(CACHE VS_PGO_PHASE PROPERTY STRINGS OFF GENERATE USE)
set(VS_PGO_DATA "${CMAKE_BINARY_DIR}/pgo" CACHE PATH
	"PGO data directory (GNU) or merged .profdata file (Clang USE)")
string(TOUPPER "${VS_PGO_PHASE}" _vs_pgo_phase)
if(NOT _vs_pgo_phase MATCHES "^(OFF|GENERATE|USE)$")
	message(FATAL_ERROR
		"VS_PGO_PHASE must be OFF, GENERATE, or USE (got '${VS_PGO_PHASE}')")
endif()

# MSVC's /GENPROFILE and /USEPROFILE operate on LTCG images, so its PGO mode
# needs IPO. GCC/Clang instrumentation does not; keeping it independent avoids
# imposing LTO linker/plugin requirements on otherwise supported platforms.
if(VS_ENABLE_IPO OR (MSVC AND NOT _vs_pgo_phase STREQUAL "OFF"))
	include(CheckIPOSupported)
	check_ipo_supported(RESULT _vs_ipo_supported OUTPUT _vs_ipo_error LANGUAGES CXX)
	if(NOT _vs_ipo_supported)
		message(FATAL_ERROR "Release IPO requested but unsupported: ${_vs_ipo_error}")
	endif()
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# PGO is always explicit and Release-only. GENERATE and USE intentionally use
# the same build tree on MSVC/GNU so compiler-generated profile identities match.
if(_vs_pgo_phase STREQUAL "GENERATE")
	if(MSVC)
		add_link_options("$<$<CONFIG:Release>:/GENPROFILE>")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		file(MAKE_DIRECTORY "${VS_PGO_DATA}")
		add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release>>:-fprofile-generate=${VS_PGO_DATA}>")
		add_link_options("$<$<CONFIG:Release>:-fprofile-generate=${VS_PGO_DATA}>")
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release>>:-fprofile-instr-generate>")
		add_link_options("$<$<CONFIG:Release>:-fprofile-instr-generate>")
	else()
		message(FATAL_ERROR "VS_PGO_PHASE=GENERATE is unsupported by this compiler")
	endif()
elseif(_vs_pgo_phase STREQUAL "USE")
	if(MSVC)
		add_link_options("$<$<CONFIG:Release>:/USEPROFILE>")
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		if(NOT EXISTS "${VS_PGO_DATA}")
			message(FATAL_ERROR "GNU PGO data directory does not exist: ${VS_PGO_DATA}")
		endif()
		add_compile_options(
			"$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release>>:-fprofile-use=${VS_PGO_DATA}>"
			"$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release>>:-fprofile-correction>")
		add_link_options("$<$<CONFIG:Release>:-fprofile-use=${VS_PGO_DATA}>")
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		if(IS_DIRECTORY "${VS_PGO_DATA}" OR NOT EXISTS "${VS_PGO_DATA}")
			message(FATAL_ERROR
				"Clang VS_PGO_DATA must name an existing merged .profdata file")
		endif()
		add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release>>:-fprofile-instr-use=${VS_PGO_DATA}>")
		add_link_options("$<$<CONFIG:Release>:-fprofile-instr-use=${VS_PGO_DATA}>")
	else()
		message(FATAL_ERROR "VS_PGO_PHASE=USE is unsupported by this compiler")
	endif()
endif()

# Unified output directories for all generators (Visual Studio / Ninja).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")

if(NOT DEFINED VEROSCRIPT_MODULE_ROOT OR VEROSCRIPT_MODULE_ROOT STREQUAL "")
	set(VEROSCRIPT_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake")
include(VeroScriptHelpers)

# enable_testing() registers CTest without CDash dashboard targets
# (Continuous / Experimental / Nightly / NightlyMemoryCheck).
# option(BUILD_TESTING) is required: enable_testing() alone leaves BUILD_TESTING
# unset, so if(BUILD_TESTING) skips all test executables on a clean configure.
enable_testing()
if(CMAKE_CROSSCOMPILING OR VEROSCRIPT_MOBILE_TARGET)
	set(_vs_default_testing OFF)
else()
	set(_vs_default_testing ON)
endif()
option(BUILD_TESTING "Build the testing tree (cross targets require a device runner)." ${_vs_default_testing})

if(EMSCRIPTEN OR VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY OR VEROSCRIPT_MOBILE_TARGET OR VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY)
	set(_vs_default_asmjit_backend OFF)
else()
	set(_vs_default_asmjit_backend ON)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND VEROSCRIPT_BUILD_ASMJIT_BACKEND
	AND VEROSCRIPT_ENABLE_ASMJIT)
	message(FATAL_ERROR "iOS does not permit the native AsmJit backend; set VEROSCRIPT_ENABLE_ASMJIT=OFF for static portable execution")
endif()
option(VEROSCRIPT_BUILD_ASMJIT_BACKEND "Build the optional VeroScript AsmJit backend module" ${_vs_default_asmjit_backend})
option(VEROSCRIPT_BUILD_VM_BACKEND "Build the official independent bytecode VM backend" ON)
set(VEROSCRIPT_ASMJIT_NATIVE_ENABLED OFF CACHE INTERNAL "Actual native emission capability" FORCE)
if((EMSCRIPTEN OR VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY) AND VEROSCRIPT_BUILD_ASMJIT_BACKEND
	AND (NOT DEFINED VEROSCRIPT_ENABLE_ASMJIT OR VEROSCRIPT_ENABLE_ASMJIT))
	message(FATAL_ERROR "WebAssembly does not support native AsmJit emission; disable VEROSCRIPT_BUILD_ASMJIT_BACKEND")
endif()

if(VEROSCRIPT_BUILD_ASMJIT_BACKEND)
	set(VEROSCRIPT_TEST_JIT_BACKEND "$<TARGET_FILE:VeroScriptAsmJitBackend>")
endif()
if(VEROSCRIPT_BUILD_CURL)
	include("${CMAKE_CURRENT_SOURCE_DIR}/Modules/Network/Backend/VeroScriptCurl/Resources/Tools/CMake/VeroScriptCurlDependencies.cmake")
	veroscript_curl_prepare()
endif()
option(VEROSCRIPT_BUILD_GUI "Build the GUI framework and optional RmlUi adapter" OFF)
if(VEROSCRIPT_BUILD_GUI)
	option(VEROSCRIPT_GUI_SCRIPTING "Build the GUI script and compiler adapter" ON)
	set(VEROSCRIPT_BUILD_IMAGE ON)
	set(VEROSCRIPT_BUILD_FONT ON)
	set(VEROSCRIPT_BUILD_PLATFORM ON)
	set(VEROSCRIPT_BUILD_GRAPHICS ON)
endif()
vs_add_root_subdirectories()
if(VEROSCRIPT_BUILD_GUI)
	include("${CMAKE_CURRENT_SOURCE_DIR}/Modules/Multimedia/VeroScriptGui/Resources/Tools/CMake/RootIntegration.cmake")
endif()
vs_add_aggregate_build_targets()
if(BUILD_TESTING)
	add_executable(VeroScriptJsonStringTests Shared/Private/Tests/VeroScriptJsonStringTests.cpp)
	vs_use_shared_private_headers(VeroScriptJsonStringTests)
	vs_apply_cxx_defaults(VeroScriptJsonStringTests)
	vs_set_solution_folder(VeroScriptJsonStringTests "Tests")
	add_test(NAME VeroScriptJsonStringTests COMMAND VeroScriptJsonStringTests)
	add_executable(VeroScriptFloatFormattingTests Shared/Private/Tests/VeroScriptFloatFormattingTests.cpp)
	vs_use_shared_private_headers(VeroScriptFloatFormattingTests)
	vs_apply_cxx_defaults(VeroScriptFloatFormattingTests)
	vs_set_solution_folder(VeroScriptFloatFormattingTests "Tests")
	add_test(NAME VeroScriptFloatFormattingTests COMMAND VeroScriptFloatFormattingTests)
endif()
if(BUILD_TESTING AND TARGET VeroScriptCpp AND TARGET VeroScriptBytecodeVm)
	add_executable(VeroScriptVmContractTests Modules/Execution/VeroScriptBytecodeVm/Resources/Tests/Contract.cpp)
	target_link_libraries(VeroScriptVmContractTests PRIVATE VeroScriptCpp VeroScriptBytecodeVm)
	vs_apply_cxx_defaults(VeroScriptVmContractTests)
	if(TARGET VeroScriptBytecodeVmBackend)
		add_dependencies(VeroScriptVmContractTests VeroScriptBytecodeVmBackend)
		add_test(NAME VeroScriptVmContractTests COMMAND VeroScriptVmContractTests "$<TARGET_FILE:VeroScriptBytecodeVmBackend>")
		if(TARGET VeroScriptAsmJitBackend)
			add_dependencies(VeroScriptVmContractTests VeroScriptAsmJitBackend)
			add_test(NAME VeroScriptVmJitDomainTests COMMAND VeroScriptVmContractTests
				"$<TARGET_FILE:VeroScriptBytecodeVmBackend>" "$<TARGET_FILE:VeroScriptAsmJitBackend>")
		endif()
	else()
		add_test(NAME VeroScriptVmContractTests COMMAND VeroScriptVmContractTests)
	endif()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptVmTests.cmake")
if(BUILD_TESTING AND TARGET VeroScriptSpecialMemberTests AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptSpecialMemberTests VeroScriptAsmJitBackend)
	add_test(NAME VeroScriptSpecialMemberBackendTests COMMAND VeroScriptSpecialMemberTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	set_tests_properties(VeroScriptSpecialMemberBackendTests PROPERTIES TIMEOUT 60 LABELS "Core;Jit;VeroScript")
endif()
if(BUILD_TESTING AND TARGET VeroScriptTemplateTests AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptTemplateTests VeroScriptAsmJitBackend)
	add_test(NAME VeroScriptTemplateBackendTests COMMAND VeroScriptTemplateTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	set_tests_properties(VeroScriptTemplateBackendTests PROPERTIES TIMEOUT 60 LABELS "Core;Jit;VeroScript;Templates")
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(VeroScriptTemplateBackendTests PROPERTIES TIMEOUT 600)
	endif()
endif()
if(BUILD_TESTING AND TARGET VeroScriptScopedTypeTests AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptScopedTypeTests VeroScriptAsmJitBackend)
	add_test(NAME VeroScriptScopedTypeBackendTests COMMAND VeroScriptScopedTypeTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	set_tests_properties(VeroScriptScopedTypeBackendTests PROPERTIES TIMEOUT 60 LABELS "Core;Jit;VeroScript")
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(VeroScriptScopedTypeBackendTests PROPERTIES TIMEOUT 600)
	endif()
endif()
if(BUILD_TESTING AND TARGET VeroScriptAttributeTests AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptAttributeTests VeroScriptAsmJitBackend)
	add_test(NAME VeroScriptAttributeBackendTests COMMAND VeroScriptAttributeTests
		"${CMAKE_CURRENT_SOURCE_DIR}/VeroScript/Resources/Scripts/stdlib" "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	set_tests_properties(VeroScriptAttributeBackendTests PROPERTIES TIMEOUT 60 LABELS "Core;Jit;VeroScript")
endif()
if(BUILD_TESTING AND TARGET VeroScriptNativeOverloadTests AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptNativeOverloadTests VeroScriptAsmJitBackend)
	add_test(NAME VeroScriptNativeOverloadBackendTests COMMAND VeroScriptNativeOverloadTests
		"$<TARGET_FILE:VeroScriptAsmJitBackend>")
	set_tests_properties(VeroScriptNativeOverloadBackendTests PROPERTIES TIMEOUT 60 LABELS "Core;Jit;VeroScript")
endif()
if(TARGET build_native AND TARGET VeroScriptGui)
	add_dependencies(build_native VeroScriptGui)
	foreach(_vs_gui_target VeroScriptGuiScript VeroScriptGuiToolsModule VeroScriptRmlUi VeroScriptRmlUiModule)
		if(TARGET ${_vs_gui_target})
			add_dependencies(build_native ${_vs_gui_target})
		endif()
	endforeach()
endif()

if(BUILD_TESTING AND TARGET VeroScriptNativeExtensionTests AND TARGET VeroScriptNativeExtensionFixture AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptNativeExtensionTests VeroScriptAsmJitBackend)
	add_test(NAME VeroScriptNativeExtensionJitTests COMMAND VeroScriptNativeExtensionTests "$<TARGET_FILE:VeroScriptNativeExtensionFixture>")
	set_tests_properties(VeroScriptNativeExtensionJitTests PROPERTIES TIMEOUT 60 ENVIRONMENT
		"VEROSCRIPT_TEST_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>" LABELS "VeroScript;Core;Jit")
endif()

if(BUILD_TESTING AND TARGET VeroScriptI18nTests AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptI18nTests VeroScriptAsmJitBackend)
endif()

if(BUILD_TESTING AND TARGET VeroScriptNumericIntegrationTests AND TARGET VeroScriptAsmJitBackend)
	add_dependencies(VeroScriptNumericIntegrationTests VeroScriptAsmJitBackend)
endif()

# Keep native Windows deployment dependencies with their consuming products.
vs_install_windows_runtime("." VeroScriptWindowsRuntime)
if(TARGET VeroScriptInterop)
	vs_install_windows_runtime("bin" VeroScriptInterop)
endif()
if(TARGET VeroScriptCppBindGen)
	vs_install_windows_runtime("bin" VeroScriptCppTools)
endif()
if(TARGET VeroScriptPython)
	vs_install_windows_runtime("${VEROSCRIPT_PYTHON_INSTALL_DIR}" VeroScriptPython)
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
	if(TARGET VeroScriptCli)
		set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT VeroScriptCli)
	elseif(TARGET VeroScriptWebAssemblyModule)
		set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT VeroScriptWebAssemblyModule)
	endif()
endif()

if(BUILD_TESTING AND TARGET VeroScriptTooling AND TARGET VeroScriptCppAot
	AND TARGET VeroScriptCSharpAot AND TARGET VeroScriptPythonAot AND TARGET VeroScriptInterop)
	add_executable(VeroScriptAnnotationIntegrationTests VeroScript/Private/Tests/VeroScriptAnnotationIntegrationTests.cpp)
	target_link_libraries(VeroScriptAnnotationIntegrationTests PRIVATE VeroScript VeroScriptTooling
		VeroScriptCppAot VeroScriptCSharpAot VeroScriptPythonAot VeroScriptInterop)
	vs_apply_cxx_defaults(VeroScriptAnnotationIntegrationTests)
	vs_set_solution_folder(VeroScriptAnnotationIntegrationTests "Tests")
	add_test(NAME VeroScriptAnnotationIntegrationTests COMMAND VeroScriptAnnotationIntegrationTests
		"${CMAKE_BINARY_DIR}/annotation-integration-output")
	set_tests_properties(VeroScriptAnnotationIntegrationTests PROPERTIES FIXTURES_SETUP AnnotationGeneratedSources)
	if(TARGET VeroScriptAsmJitBackend)
		set_property(TEST VeroScriptAnnotationIntegrationTests APPEND PROPERTY ENVIRONMENT
			"VEROSCRIPT_TEST_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>")
	endif()
	find_package(Python3 QUIET COMPONENTS Interpreter)
	find_program(_annotation_dotnet dotnet)
	set(_annotation_kinds cpp)
	if(Python3_Interpreter_FOUND)
		list(APPEND _annotation_kinds python)
	endif()
	if(_annotation_dotnet)
		execute_process(COMMAND "${_annotation_dotnet}" --list-sdks OUTPUT_VARIABLE _annotation_sdks)
		if(_annotation_sdks MATCHES "10[.]")
			list(APPEND _annotation_kinds csharp)
		endif()
	endif()
	foreach(_kind IN LISTS _annotation_kinds)
		add_test(NAME "VeroScriptAnnotationAot-${_kind}"
			COMMAND "${CMAKE_COMMAND}" "-DKIND=${_kind}"
				"-DWORK=${CMAKE_BINARY_DIR}/annotation-integration-output"
				"-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
				"-DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}" "-DDOTNET=${_annotation_dotnet}"
				"-DPYTHON=${Python3_EXECUTABLE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptAnnotationAotTest.cmake")
		set_tests_properties("VeroScriptAnnotationAot-${_kind}" PROPERTIES FIXTURES_REQUIRED AnnotationGeneratedSources TIMEOUT 120)
	endforeach()
endif()
