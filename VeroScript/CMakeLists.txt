# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScript)
vs_use_shared_private_headers(VeroScript)
vs_set_solution_folder(VeroScript "Libraries")

# Hash exported language/layout contracts independently of product versions.
file(GLOB_RECURSE _vs_jit_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h")
list(SORT _vs_jit_headers)
set(_vs_jit_header_hashes "")
foreach(_header IN LISTS _vs_jit_headers)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_header}")
	file(READ "${_header}" _text)
	string(REPLACE "\r\n" "\n" _text "${_text}")
	string(SHA256 _hash "${_text}")
	string(APPEND _vs_jit_header_hashes "${_hash}")
endforeach()
string(SHA256 _vs_jit_public_abi "${_vs_jit_header_hashes}")
file(GLOB_RECURSE _vs_core_implementation CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.inl")
foreach(_vs_shared_header IN ITEMS VeroScriptFloatParsing.h VeroScriptFloatFormatting.h VeroScriptJsonString.h)
	get_filename_component(_vs_shared_header_path
		"${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Private/${_vs_shared_header}" ABSOLUTE)
	list(APPEND _vs_core_implementation "${_vs_shared_header_path}")
endforeach()
list(FILTER _vs_core_implementation EXCLUDE REGEX "/Tests/")
list(SORT _vs_core_implementation)
set(_vs_core_hashes "")
foreach(_source IN LISTS _vs_core_implementation)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")
	file(READ "${_source}" _text)
	string(REPLACE "\r\n" "\n" _text "${_text}")
	string(SHA256 _hash "${_text}")
	string(APPEND _vs_core_hashes "${_hash}")
endforeach()
string(SHA256 _vs_core_implementation_id "${_vs_core_hashes}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/VeroScriptJitBuild.h"
	CONTENT "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n#pragma once\n\n#define VEROSCRIPT_JIT_PUBLIC_ABI \"@_vs_jit_public_abi@\"\n#define VEROSCRIPT_CORE_IMPLEMENTATION_ID \"@_vs_core_implementation_id@\"\n" @ONLY)
target_include_directories(VeroScript PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/Generated")

# The interpreter consumes headers and immutable Core callbacks, never Core objects.
add_library(VeroScriptExecutionContract INTERFACE)
target_compile_features(VeroScriptExecutionContract INTERFACE cxx_std_17)
target_include_directories(VeroScriptExecutionContract INTERFACE
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/Generated>"
	"$<INSTALL_INTERFACE:include/Script>")
include(CMakePackageConfigHelpers)
configure_package_config_file(Resources/Tools/CMake/VeroScriptExecutionContractConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptExecutionContractConfig.cmake"
	INSTALL_DESTINATION lib/cmake/VeroScriptExecutionContract)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VeroScriptExecutionContractConfigVersion.cmake"
	VERSION 0.1.1 COMPATIBILITY ExactVersion)
install(TARGETS VeroScriptExecutionContract EXPORT VeroScriptExecutionContractTargets
	COMPONENT VeroScriptExecutionContract)
install(EXPORT VeroScriptExecutionContractTargets NAMESPACE VeroScript::
	DESTINATION lib/cmake/VeroScriptExecutionContract COMPONENT VeroScriptExecutionContract)
install(DIRECTORY Public/ DESTINATION include/Script COMPONENT VeroScriptExecutionContract FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Generated/VeroScriptJitBuild.h"
	DESTINATION include/Script COMPONENT VeroScriptExecutionContract)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptExecutionContractConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptExecutionContractConfigVersion.cmake"
	DESTINATION lib/cmake/VeroScriptExecutionContract COMPONENT VeroScriptExecutionContract)

# MSVC AddressSanitizer is linked through a toolset DLL that is normally added
# to PATH by a Visual Studio developer prompt.  CTest and other clean launchers
# do not inherit that setup, and Windows reports the missing DLL as a process
# startup failure before any test code can run.  Keep sanitizer builds
# self-contained by placing the matching release/debug runtimes beside the
# executables.  The linker directory is the target-architecture tool directory
# for both Visual Studio and Ninja MSVC generators.
set(_VeroScript_msvc_asan_enabled OFF)
if(MSVC)
	set(_VeroScript_all_cxx_flags
		"${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_CXX_FLAGS_MINSIZEREL}")
	if(_VeroScript_all_cxx_flags MATCHES "/fsanitize=address|-fsanitize=address")
		set(_VeroScript_msvc_asan_enabled ON)
	endif()
endif()
if(_VeroScript_msvc_asan_enabled AND CMAKE_LINKER)
	get_filename_component(_VeroScript_msvc_tool_bin "${CMAKE_LINKER}" DIRECTORY)
	file(GLOB _VeroScript_msvc_asan_runtimes
		"${_VeroScript_msvc_tool_bin}/clang_rt.asan*_dynamic-*.dll")
	add_custom_command(TARGET VeroScript POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory
			"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
	foreach(_VeroScript_msvc_asan_runtime IN LISTS _VeroScript_msvc_asan_runtimes)
		add_custom_command(TARGET VeroScript POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different
				"${_VeroScript_msvc_asan_runtime}"
				"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
	endforeach()
endif()

find_package(Threads REQUIRED)
target_link_libraries(VeroScript PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
target_include_directories(VeroScript PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")

get_target_property(_VeroScript_sources VeroScript SOURCES)
list(FILTER _VeroScript_sources EXCLUDE REGEX "/Tests/")
set_target_properties(VeroScript PROPERTIES SOURCES "${_VeroScript_sources}")

# Runtime assets: next to test/CLI binaries under CMAKE_RUNTIME_OUTPUT_DIRECTORY.
# Keep this as an explicit dependency rather than a POST_BUILD command: building a
# single test target must refresh script assets even when VeroScript is up to date.
include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptLocalization.cmake")
set(_VeroScript_localization_profile runtime)
if(VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY)
	set(_VeroScript_localization_profile cpp)
elseif(VS_BUILD_VEROSCRIPT_PYTHON_ONLY OR VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY)
	set(_VeroScript_localization_profile interop)
endif()
set(_VeroScript_stdlib_dir "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/stdlib")
if(CMAKE_CONFIGURATION_TYPES)
	set(_VeroScript_localization_out "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/localization")
	set(_VeroScript_stdlib_out "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/stdlib")
else()
	set(_VeroScript_localization_out "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/localization")
	set(_VeroScript_stdlib_out "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/stdlib")
endif()
set(_VeroScript_localization_dir "${_VeroScript_localization_out}")
add_custom_target(VeroScriptRuntimeAssets
	# Keep the split catalog tree exact during incremental builds. Otherwise a
	# renamed/deleted JSON file survives copy_directory and may duplicate keys.
	COMMAND ${CMAKE_COMMAND}
		"-DVEROSCRIPT_SOURCE_ROOT=${VEROSCRIPT_MODULE_ROOT}"
		"-DVEROSCRIPT_LOCALIZATION_OUTPUT=${_VeroScript_localization_out}"
		"-DVEROSCRIPT_LOCALIZATION_PROFILE=${_VeroScript_localization_profile}"
		-P "${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptLocalization.cmake"
	# Refreshing an existing build tree must remove the former module filename.
	COMMAND ${CMAKE_COMMAND} -E rm -f "${_VeroScript_stdlib_out}/events.vs"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${_VeroScript_stdlib_dir}"
		"${_VeroScript_stdlib_out}"
	COMMENT "Deploy VeroScript localization and stdlib assets")
add_dependencies(VeroScript VeroScriptRuntimeAssets)
vs_set_solution_folder(VeroScriptRuntimeAssets "Libraries")

if(BUILD_TESTING)
	add_executable(VeroScriptInvocationBenchmarks EXCLUDE_FROM_ALL Resources/Benchmarks/InvocationBenchmarks.cpp)
	target_link_libraries(VeroScriptInvocationBenchmarks PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptInvocationBenchmarks)
	vs_set_solution_folder(VeroScriptInvocationBenchmarks "Tests/Core")
	add_executable(VeroScriptPerformanceRegressionTests Private/Tests/VeroScriptPerformanceRegressionTests.cpp)
	target_link_libraries(VeroScriptPerformanceRegressionTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptPerformanceRegressionTests)
	add_test(NAME VeroScriptPerformanceRegressionTests COMMAND VeroScriptPerformanceRegressionTests)
	set_tests_properties(VeroScriptPerformanceRegressionTests PROPERTIES LABELS "Core;Performance" TIMEOUT 60)
	add_executable(VeroScriptExecutionGovernanceTests Private/Tests/VeroScriptExecutionGovernanceTests.cpp)
	target_link_libraries(VeroScriptExecutionGovernanceTests PRIVATE VeroScript)
	target_include_directories(VeroScriptExecutionGovernanceTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptExecutionGovernanceTests)
	add_test(NAME VeroScriptExecutionGovernanceTests COMMAND VeroScriptExecutionGovernanceTests)
	set_tests_properties(VeroScriptExecutionGovernanceTests PROPERTIES LABELS "Core;Mobile" TIMEOUT 30)
	add_executable(VeroScriptNumericsTests Private/Tests/VeroScriptNumericsTests.cpp)
	target_link_libraries(VeroScriptNumericsTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptNumericsTests)
	add_test(NAME VeroScriptNumericsTests COMMAND VeroScriptNumericsTests)
	set_tests_properties(VeroScriptNumericsTests PROPERTIES TIMEOUT 60 LABELS "Core;Numerics")
	add_executable(VeroScriptNumericIntegrationTests Private/Tests/VeroScriptNumericIntegrationTests.cpp)
	target_link_libraries(VeroScriptNumericIntegrationTests PRIVATE VeroScript)
	target_compile_definitions(VeroScriptNumericIntegrationTests PRIVATE VEROSCRIPT_TEST_STDLIB="${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptNumericIntegrationTests)
	vs_set_solution_folder(VeroScriptNumericIntegrationTests "Tests")
	add_test(NAME VeroScriptNumericIntegrationTests COMMAND VeroScriptNumericIntegrationTests)
	set_tests_properties(VeroScriptNumericIntegrationTests PROPERTIES TIMEOUT 120 LABELS "Core;Numerics")
	if(VEROSCRIPT_TEST_JIT_BACKEND)
		set_property(TEST VeroScriptNumericIntegrationTests APPEND PROPERTY ENVIRONMENT
			"VEROSCRIPT_TEST_JIT_BACKEND=${VEROSCRIPT_TEST_JIT_BACKEND}")
	endif()
	find_package(Python3 QUIET COMPONENTS Interpreter)
	if(Python3_Interpreter_FOUND)
		set(_VeroScript_numerics_runner)
		if(CMAKE_CROSSCOMPILING)
			set(_VeroScript_numerics_runner ${CMAKE_CROSSCOMPILING_EMULATOR})
		endif()
		add_test(NAME VeroScriptNumericsDifferential COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/NumericsDifferential.py"
			${_VeroScript_numerics_runner} "$<TARGET_FILE:VeroScriptNumericsTests>")
		set_tests_properties(VeroScriptNumericsDifferential PROPERTIES TIMEOUT 120 LABELS "Core;Numerics")
	endif()
	add_executable(VeroScriptStandardLibraryFoundationTests Private/Tests/VeroScriptStandardLibraryFoundationTests.cpp)
	vs_use_shared_private_headers(VeroScriptStandardLibraryFoundationTests)
	target_link_libraries(VeroScriptStandardLibraryFoundationTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptStandardLibraryFoundationTests)
	vs_set_solution_folder(VeroScriptStandardLibraryFoundationTests "Tests")
	add_test(NAME VeroScriptStandardLibraryFoundationTests COMMAND VeroScriptStandardLibraryFoundationTests)
	set_tests_properties(VeroScriptStandardLibraryFoundationTests PROPERTIES TIMEOUT 60 LABELS "Core")
	add_executable(VeroScriptStandardLibraryIntegrationTests Private/Tests/VeroScriptStandardLibraryIntegrationTests.cpp)
	target_link_libraries(VeroScriptStandardLibraryIntegrationTests PRIVATE VeroScript)
	target_compile_definitions(VeroScriptStandardLibraryIntegrationTests PRIVATE
		VEROSCRIPT_TEST_STDLIB="${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptStandardLibraryIntegrationTests)
	vs_set_solution_folder(VeroScriptStandardLibraryIntegrationTests "Tests")
	add_test(NAME VeroScriptStandardLibraryIntegrationTests COMMAND VeroScriptStandardLibraryIntegrationTests)
	set_tests_properties(VeroScriptStandardLibraryIntegrationTests PROPERTIES TIMEOUT 60 LABELS "Core")
	if(VEROSCRIPT_BUILD_VM_BACKEND OR TARGET VeroScriptBytecodeVm)
		add_executable(VeroScriptModuleEnvironmentExample Resources/Examples/ModuleEnvironment/main.cpp)
		target_link_libraries(VeroScriptModuleEnvironmentExample PRIVATE VeroScript VeroScriptBytecodeVm)
		vs_apply_cxx_defaults(VeroScriptModuleEnvironmentExample)
		vs_set_solution_folder(VeroScriptModuleEnvironmentExample "Examples")
		add_test(NAME VeroScriptModuleEnvironmentExample COMMAND VeroScriptModuleEnvironmentExample)
		set_tests_properties(VeroScriptModuleEnvironmentExample PROPERTIES PASS_REGULAR_EXPRESSION "42" TIMEOUT 30)
	endif()
	add_executable(VeroScriptModuleEnvironmentTests Private/Tests/VeroScriptModuleEnvironmentTests.cpp)
	add_executable(VeroScriptBackendSelectionTests Private/Tests/VeroScriptBackendSelectionTests.cpp)
	target_link_libraries(VeroScriptBackendSelectionTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptBackendSelectionTests)
	add_test(NAME VeroScriptBackendSelectionTests COMMAND VeroScriptBackendSelectionTests)
	set_tests_properties(VeroScriptBackendSelectionTests PROPERTIES LABELS "Core;BackendSelection" TIMEOUT 30)
	target_link_libraries(VeroScriptModuleEnvironmentTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptModuleEnvironmentTests)
	add_test(NAME VeroScriptModuleEnvironmentTests COMMAND VeroScriptModuleEnvironmentTests)
	set_tests_properties(VeroScriptModuleEnvironmentTests PROPERTIES TIMEOUT 60)
	add_executable(VeroScriptNativeRootTests Private/Tests/VeroScriptNativeRootTests.cpp)
	target_link_libraries(VeroScriptNativeRootTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptNativeRootTests)
	add_test(NAME VeroScriptNativeRootTests COMMAND VeroScriptNativeRootTests)
	set_tests_properties(VeroScriptNativeRootTests PROPERTIES TIMEOUT 30 LABELS "Core")
	add_executable(VeroScriptNativeExtensionTests Private/Tests/VeroScriptNativeExtensionTests.cpp)
	target_link_libraries(VeroScriptNativeExtensionTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptNativeExtensionTests)
	# Prove that a native extension consumes only public headers and the Core
	# host vtable. Linking the static Core into this fixture would hide defects.
	if(NOT CMAKE_SYSTEM_NAME MATCHES "^(iOS|Emscripten)$" AND
	   NOT (MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
		add_library(VeroScriptNativeExtensionFixture SHARED Private/Tests/NativeExtension/NativeFixture.cpp)
		target_include_directories(VeroScriptNativeExtensionFixture PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/Public" "${CMAKE_CURRENT_BINARY_DIR}/Generated")
		vs_apply_cxx_defaults(VeroScriptNativeExtensionFixture)
		add_dependencies(VeroScriptNativeExtensionTests VeroScriptNativeExtensionFixture)
		add_test(NAME VeroScriptNativeExtensionTests COMMAND VeroScriptNativeExtensionTests "$<TARGET_FILE:VeroScriptNativeExtensionFixture>")
	else()
		add_test(NAME VeroScriptNativeExtensionTests COMMAND VeroScriptNativeExtensionTests)
	endif()
	set_tests_properties(VeroScriptNativeExtensionTests PROPERTIES TIMEOUT 60)
	if(VEROSCRIPT_BUILD_VM_BACKEND OR TARGET VeroScriptBytecodeVm)
		add_executable(VeroScriptScriptCompileExample Resources/Examples/ScriptCompileExtensions/main.cpp)
		target_link_libraries(VeroScriptScriptCompileExample PRIVATE VeroScript VeroScriptBytecodeVm)
		if(MINGW)
			target_link_options(VeroScriptScriptCompileExample PRIVATE -municode)
		endif()
		vs_apply_cxx_defaults(VeroScriptScriptCompileExample)
		vs_set_solution_folder(VeroScriptScriptCompileExample "Examples")
		add_test(NAME VeroScriptScriptCompileExample COMMAND VeroScriptScriptCompileExample "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Examples/ScriptCompileExtensions/main.vs" "${_VeroScript_stdlib_dir}")
		set_tests_properties(VeroScriptScriptCompileExample PROPERTIES PASS_REGULAR_EXPRESSION "Save" TIMEOUT 30)
	endif()
	add_executable(VeroScriptScriptCompileExtensionsTests Private/Tests/VeroScriptScriptCompileExtensionsTests.cpp)
	target_link_libraries(VeroScriptScriptCompileExtensionsTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptScriptCompileExtensionsTests)
	add_test(NAME VeroScriptScriptCompileExtensionsTests COMMAND VeroScriptScriptCompileExtensionsTests)
	set_tests_properties(VeroScriptScriptCompileExtensionsTests PROPERTIES TIMEOUT 30)
	add_executable(VeroScriptCompileProcessingTests Private/Tests/VeroScriptCompileProcessingTests.cpp)
	target_link_libraries(VeroScriptCompileProcessingTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptCompileProcessingTests)
	add_test(NAME VeroScriptCompileProcessingTests COMMAND VeroScriptCompileProcessingTests)
	add_executable(VeroScriptSourceMetadataIOTests Private/Tests/VeroScriptSourceMetadataIOTests.cpp)
	target_link_libraries(VeroScriptSourceMetadataIOTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptSourceMetadataIOTests)
	add_test(NAME VeroScriptSourceMetadataIOTests COMMAND VeroScriptSourceMetadataIOTests)
	set_tests_properties(VeroScriptSourceMetadataIOTests PROPERTIES TIMEOUT 30)
	if(VEROSCRIPT_BUILD_VM_BACKEND OR TARGET VeroScriptBytecodeVm)
		add_executable(VeroScriptCompileProcessingExample Resources/Examples/CompileProcessing/main.cpp)
		target_link_libraries(VeroScriptCompileProcessingExample PRIVATE VeroScript VeroScriptBytecodeVm)
		vs_apply_cxx_defaults(VeroScriptCompileProcessingExample)
		vs_set_solution_folder(VeroScriptCompileProcessingExample "Examples")
		add_test(NAME VeroScriptCompileProcessingExample COMMAND VeroScriptCompileProcessingExample)
		set_tests_properties(VeroScriptCompileProcessingExample PROPERTIES PASS_REGULAR_EXPRESSION "Save")
		add_executable(VeroScriptAnnotationExample Resources/Examples/AnnotationProcessing/main.cpp)
		target_link_libraries(VeroScriptAnnotationExample PRIVATE VeroScript VeroScriptBytecodeVm)
		vs_apply_cxx_defaults(VeroScriptAnnotationExample)
		vs_set_solution_folder(VeroScriptAnnotationExample "Examples")
		add_test(NAME VeroScriptAnnotationExample COMMAND VeroScriptAnnotationExample)
		set_tests_properties(VeroScriptAnnotationExample PROPERTIES PASS_REGULAR_EXPRESSION "42")
	endif()
	add_executable(VeroScriptAnnotationProcessingTests Private/Tests/VeroScriptAnnotationProcessingTests.cpp)
	target_link_libraries(VeroScriptAnnotationProcessingTests PRIVATE VeroScript)
	if(MSVC)
		target_compile_options(VeroScriptAnnotationProcessingTests PRIVATE /EHsc /utf-8)
	endif()
	add_test(NAME VeroScriptAnnotationProcessingTests COMMAND VeroScriptAnnotationProcessingTests)

	add_executable(VeroScriptReflectionTypeTests Private/Tests/VeroScriptReflectionTypeTests.cpp)
	target_link_libraries(VeroScriptReflectionTypeTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptReflectionTypeTests)
	vs_set_solution_folder(VeroScriptReflectionTypeTests "Tests")
	add_test(NAME VeroScriptReflectionTypeTests COMMAND VeroScriptReflectionTypeTests)
	add_executable(VeroScriptMemoryTests Private/Tests/VeroScriptMemoryTests.cpp)
	add_executable(VeroScriptI18nTests Private/Tests/VeroScriptI18nTests.cpp)
	target_link_libraries(VeroScriptI18nTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptI18nTests)
	add_test(NAME VeroScriptI18nTests COMMAND VeroScriptI18nTests "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/stdlib")
	set_property(TEST VeroScriptI18nTests PROPERTY TIMEOUT 60)
	if(VEROSCRIPT_TEST_JIT_BACKEND)
		set_property(TEST VeroScriptI18nTests APPEND PROPERTY ENVIRONMENT
			"VEROSCRIPT_TEST_JIT_BACKEND=${VEROSCRIPT_TEST_JIT_BACKEND}")
	endif()
	if(VEROSCRIPT_BUILD_VM_BACKEND OR TARGET VeroScriptBytecodeVm)
		add_executable(VeroScriptI18nExample Resources/Examples/I18n/main.cpp)
		target_link_libraries(VeroScriptI18nExample PRIVATE VeroScript VeroScriptBytecodeVm)
		vs_apply_cxx_defaults(VeroScriptI18nExample)
		vs_set_solution_folder(VeroScriptI18nExample "Examples")
		add_test(NAME VeroScriptI18nExample COMMAND VeroScriptI18nExample)
		set_tests_properties(VeroScriptI18nExample PROPERTIES PASS_REGULAR_EXPRESSION "18446744073709551615" TIMEOUT 30)
	endif()
	target_link_libraries(VeroScriptMemoryTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptMemoryTests)
	add_test(NAME VeroScriptMemoryTests COMMAND VeroScriptMemoryTests)
	set_property(TEST VeroScriptMemoryTests PROPERTY TIMEOUT 60)
	# Each failure ordinal needs a fresh registry; replacement new/delete stays
	# isolated from all other memory and runtime tests.
	add_executable(VeroScriptMemoryAllocationTests Private/Tests/VeroScriptMemoryAllocationTests.cpp)
	target_link_libraries(VeroScriptMemoryAllocationTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptMemoryAllocationTests)
	vs_set_solution_folder(VeroScriptMemoryAllocationTests "Tests")
	foreach(_VeroScript_memory_failure RANGE 1 3)
		add_test(NAME VeroScriptMemoryAllocationFirstUse${_VeroScript_memory_failure}
			COMMAND VeroScriptMemoryAllocationTests ${_VeroScript_memory_failure})
		set_tests_properties(VeroScriptMemoryAllocationFirstUse${_VeroScript_memory_failure}
			PROPERTIES TIMEOUT 30 LABELS "VeroScript;Core;Memory")
	endforeach()
	function(vs_enable_large_test_stack target_name)
		if(MSVC)
			# Some intentionally broad test programs keep large compiler/VM
			# values and hostile-input regressions in one process.  Debug and
			# ASan frames can exceed the PE default 1 MiB stack; this is kept
			# test-only and never changes the runtime or CLI stack policy.
			target_link_options(${target_name} PRIVATE
				"$<$<CONFIG:Debug>:/STACK:8388608>")
			if(_VeroScript_msvc_asan_enabled)
				target_link_options(${target_name} PRIVATE "/STACK:8388608")
			endif()
		endif()
	endfunction()

	add_executable(VeroScriptLexerTests Private/Tests/VeroScriptLexerTests.cpp)
	target_link_libraries(VeroScriptLexerTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptLexerTests)
	vs_set_solution_folder(VeroScriptLexerTests "Tests")
	add_test(NAME VeroScriptLexerTests COMMAND VeroScriptLexerTests)

	# Compile every installed core header using only VeroScript's public usage
	# requirements. This prevents an exported header from depending on a file
	# that is absent from the SDK include tree.
	add_executable(VeroScriptPublicHeadersTests Private/Tests/VeroScriptPublicHeadersTests.cpp)
	target_link_libraries(VeroScriptPublicHeadersTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptPublicHeadersTests)
	vs_set_solution_folder(VeroScriptPublicHeadersTests "Tests")
	add_test(NAME VeroScriptPublicHeadersTests COMMAND VeroScriptPublicHeadersTests)

	add_executable(VeroScriptScalarStorageTests Private/Tests/VeroScriptScalarStorageTests.cpp)
	target_link_libraries(VeroScriptScalarStorageTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptScalarStorageTests)
	vs_set_solution_folder(VeroScriptScalarStorageTests "Tests")
	add_test(NAME VeroScriptScalarStorageTests COMMAND VeroScriptScalarStorageTests)
	set_property(TEST VeroScriptScalarStorageTests PROPERTY TIMEOUT 30)

	add_executable(VeroScriptJitContractTests Private/Tests/VeroScriptJitContractTests.cpp)
	target_link_libraries(VeroScriptJitContractTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptJitContractTests)
	vs_set_solution_folder(VeroScriptJitContractTests "Tests")
	add_test(NAME VeroScriptJitContractTests COMMAND VeroScriptJitContractTests)

	add_executable(VeroScriptCompilerReuseTests Private/Tests/VeroScriptCompilerReuseTests.cpp)
	target_link_libraries(VeroScriptCompilerReuseTests PRIVATE VeroScript)
	target_include_directories(VeroScriptCompilerReuseTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptCompilerReuseTests)
	vs_set_solution_folder(VeroScriptCompilerReuseTests "Tests")
	add_test(NAME VeroScriptCompilerReuseTests COMMAND VeroScriptCompilerReuseTests)

	add_executable(VeroScriptAnalysisBoundaryTests Private/Tests/VeroScriptAnalysisBoundaryTests.cpp)
	target_link_libraries(VeroScriptAnalysisBoundaryTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptAnalysisBoundaryTests)
	vs_set_solution_folder(VeroScriptAnalysisBoundaryTests "Tests")
	add_test(NAME VeroScriptAnalysisBoundaryTests COMMAND VeroScriptAnalysisBoundaryTests)
	set_property(TEST VeroScriptAnalysisBoundaryTests PROPERTY TIMEOUT 30)

	add_executable(VeroScriptM1Tests Private/Tests/VeroScriptM1Tests.cpp)
	target_link_libraries(VeroScriptM1Tests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptM1Tests)
	vs_enable_large_test_stack(VeroScriptM1Tests)
	vs_set_solution_folder(VeroScriptM1Tests "Tests")
	add_test(NAME VeroScriptM1Tests COMMAND VeroScriptM1Tests)

	add_executable(VeroScriptExceptionTests Private/Tests/VeroScriptExceptionTests.cpp)
	target_link_libraries(VeroScriptExceptionTests PRIVATE VeroScript)
	target_include_directories(VeroScriptExceptionTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptExceptionTests)
	vs_set_solution_folder(VeroScriptExceptionTests "Tests")
	add_test(NAME VeroScriptExceptionTests COMMAND VeroScriptExceptionTests)

	add_executable(VeroScriptM2Tests Private/Tests/VeroScriptM2Tests.cpp)
	target_link_libraries(VeroScriptM2Tests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptM2Tests)
	vs_set_solution_folder(VeroScriptM2Tests "Tests")
	add_test(NAME VeroScriptM2Tests COMMAND VeroScriptM2Tests WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts")

	add_executable(VeroScriptM3Tests Private/Tests/VeroScriptM3Tests.cpp)
	target_sources(VeroScriptM3Tests PRIVATE
		Private/Tests/Suites/VeroScriptM3/01_syntax_and_control_flow.inl
		Private/Tests/Suites/VeroScriptM3/02_types_and_operators.inl
		Private/Tests/Suites/VeroScriptM3/03_objects_and_containers.inl
		Private/Tests/Suites/VeroScriptM3/04_session_and_reload.inl
		Private/Tests/Suites/VeroScriptM3/05_advanced_containers.inl
		Private/Tests/Suites/VeroScriptM3/06_standard_library_and_runtime.inl)
	target_link_libraries(VeroScriptM3Tests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptM3Tests)
	vs_enable_large_test_stack(VeroScriptM3Tests)
	vs_set_solution_folder(VeroScriptM3Tests "Tests")
	add_test(NAME VeroScriptM3Tests COMMAND VeroScriptM3Tests WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts")

	add_executable(VeroScriptBytecodeIOTests Private/Tests/VeroScriptBytecodeIOTests.cpp)
	target_link_libraries(VeroScriptBytecodeIOTests PRIVATE VeroScript)
	target_include_directories(VeroScriptBytecodeIOTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptBytecodeIOTests)
	vs_set_solution_folder(VeroScriptBytecodeIOTests "Tests")
	add_test(NAME VeroScriptBytecodeIOTests COMMAND VeroScriptBytecodeIOTests WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts")

	# Always run a deterministic hostile-input corpus.  A separate libFuzzer
	# target below can consume the same entry point on Clang toolchains.
	add_executable(VeroScriptBytecodeFuzzTests Private/Tests/VeroScriptBytecodeFuzzTests.cpp)
	target_link_libraries(VeroScriptBytecodeFuzzTests PRIVATE VeroScript)
	target_include_directories(VeroScriptBytecodeFuzzTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptBytecodeFuzzTests)
	vs_set_solution_folder(VeroScriptBytecodeFuzzTests "Tests")
	add_test(NAME VeroScriptBytecodeFuzzTests COMMAND VeroScriptBytecodeFuzzTests WORKING_DIRECTORY "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts")

	# Opt-in only: fuzzers need a Clang runtime and are intentionally not part
	# of normal developer or release builds.  They use the same entry point as
	# the deterministic CTest above, so every discovered corpus input remains
	# reproducible without libFuzzer.
	option(VEROSCRIPT_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
	if(VEROSCRIPT_BUILD_FUZZERS)
		if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			message(FATAL_ERROR "VEROSCRIPT_BUILD_FUZZERS requires Clang/libFuzzer")
		endif()
		if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
			message(FATAL_ERROR
				"VEROSCRIPT_BUILD_FUZZERS requires an ASan/UBSan CMAKE_CXX_FLAGS configuration")
		endif()
		# VeroScript is a static library. Instrumenting only the fuzzer driver
		# would execute deserialization code but yield almost no coverage
		# feedback from it. fuzzer-no-link keeps the driver link-owned while
		# making every core parser/validator edge visible to libFuzzer.
		target_compile_options(VeroScript PRIVATE
			-fsanitize=fuzzer-no-link -fno-omit-frame-pointer)
		add_executable(VeroScriptBytecodeLibFuzzer Private/Tests/VeroScriptBytecodeFuzzTests.cpp)
		target_link_libraries(VeroScriptBytecodeLibFuzzer PRIVATE VeroScript)
		target_include_directories(VeroScriptBytecodeLibFuzzer PRIVATE Private)
		target_compile_definitions(VeroScriptBytecodeLibFuzzer PRIVATE VEROSCRIPT_LIBFUZZER=1)
		target_compile_options(VeroScriptBytecodeLibFuzzer PRIVATE
			-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
		target_link_options(VeroScriptBytecodeLibFuzzer PRIVATE
			-fsanitize=fuzzer,address,undefined)
		vs_apply_cxx_defaults(VeroScriptBytecodeLibFuzzer)
		vs_set_solution_folder(VeroScriptBytecodeLibFuzzer "Fuzzers")
	endif()

	add_executable(VeroScriptPackageTests Private/Tests/VeroScriptPackageTests.cpp)
	target_link_libraries(VeroScriptPackageTests PRIVATE VeroScript)
	target_include_directories(VeroScriptPackageTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptPackageTests)
	vs_set_solution_folder(VeroScriptPackageTests "Tests")
	add_test(NAME VeroScriptPackageTests COMMAND VeroScriptPackageTests WORKING_DIRECTORY "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts")

	add_executable(VeroScriptDeclMetaTests Private/Tests/VeroScriptDeclMetaTests.cpp)
	target_link_libraries(VeroScriptDeclMetaTests PRIVATE VeroScript)
	target_include_directories(VeroScriptDeclMetaTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptDeclMetaTests)
	vs_set_solution_folder(VeroScriptDeclMetaTests "Tests")
	add_test(NAME VeroScriptDeclMetaTests COMMAND VeroScriptDeclMetaTests WORKING_DIRECTORY "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts")

	add_executable(VeroScriptDebuggerTests Private/Tests/VeroScriptDebuggerTests.cpp)
	target_link_libraries(VeroScriptDebuggerTests PRIVATE VeroScript)
	target_include_directories(VeroScriptDebuggerTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptDebuggerTests)
	vs_set_solution_folder(VeroScriptDebuggerTests "Tests")
	add_test(NAME VeroScriptDebuggerTests COMMAND VeroScriptDebuggerTests)

	add_executable(VeroScriptConcurrentTests Private/Tests/VeroScriptConcurrentTests.cpp)
	target_link_libraries(VeroScriptConcurrentTests PRIVATE VeroScript)
	target_include_directories(VeroScriptConcurrentTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptConcurrentTests)
	vs_set_solution_folder(VeroScriptConcurrentTests "Tests")
	add_test(NAME VeroScriptConcurrentTests COMMAND VeroScriptConcurrentTests WORKING_DIRECTORY "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts")

	# Keep allocation failure injection in its own process: replacement new/delete
	# must not affect the rest of the language/runtime correctness tests.
	add_executable(VeroScriptTaskGroupAllocationTests Private/Tests/VeroScriptTaskGroupAllocationTests.cpp)
	target_link_libraries(VeroScriptTaskGroupAllocationTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptTaskGroupAllocationTests)
	vs_set_solution_folder(VeroScriptTaskGroupAllocationTests "Tests")
	add_test(NAME VeroScriptTaskGroupAllocationTests COMMAND VeroScriptTaskGroupAllocationTests)
	set_tests_properties(VeroScriptTaskGroupAllocationTests PROPERTIES TIMEOUT 30)

	add_executable(VeroScriptLambdaTests Private/Tests/VeroScriptLambdaTests.cpp)
	target_link_libraries(VeroScriptLambdaTests PRIVATE VeroScript)
	target_include_directories(VeroScriptLambdaTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptLambdaTests)
	vs_set_solution_folder(VeroScriptLambdaTests "Tests")
	add_test(NAME VeroScriptLambdaTests COMMAND VeroScriptLambdaTests WORKING_DIRECTORY "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts")

	add_executable(VeroScriptFormatIOTests Private/Tests/VeroScriptFormatIOTests.cpp)
	target_link_libraries(VeroScriptFormatIOTests PRIVATE VeroScript)
	target_include_directories(VeroScriptFormatIOTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptFormatIOTests)
	vs_set_solution_folder(VeroScriptFormatIOTests "Tests")
	add_test(NAME VeroScriptFormatIOTests COMMAND VeroScriptFormatIOTests)

	add_executable(VeroScriptFallthroughTests Private/Tests/VeroScriptFallthroughTests.cpp)
	target_link_libraries(VeroScriptFallthroughTests PRIVATE VeroScript)
	target_include_directories(VeroScriptFallthroughTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptFallthroughTests)
	vs_set_solution_folder(VeroScriptFallthroughTests "Tests")
	add_test(NAME VeroScriptFallthroughTests COMMAND VeroScriptFallthroughTests)

	add_executable(VeroScriptInheritanceTests Private/Tests/VeroScriptInheritanceTests.cpp)
	target_link_libraries(VeroScriptInheritanceTests PRIVATE VeroScript)
	target_include_directories(VeroScriptInheritanceTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptInheritanceTests)
	vs_set_solution_folder(VeroScriptInheritanceTests "Tests")
	add_test(NAME VeroScriptInheritanceTests COMMAND VeroScriptInheritanceTests)

	add_executable(VeroScriptInterfaceTests Private/Tests/VeroScriptInterfaceTests.cpp)
	target_link_libraries(VeroScriptInterfaceTests PRIVATE VeroScript)
	target_include_directories(VeroScriptInterfaceTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptInterfaceTests)
	vs_set_solution_folder(VeroScriptInterfaceTests "Tests")
	add_test(NAME VeroScriptInterfaceTests COMMAND VeroScriptInterfaceTests)

	add_executable(VeroScriptGenericInterfaceTests Private/Tests/VeroScriptGenericInterfaceTests.cpp)
	target_link_libraries(VeroScriptGenericInterfaceTests PRIVATE VeroScript)
	target_include_directories(VeroScriptGenericInterfaceTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptGenericInterfaceTests)
	vs_set_solution_folder(VeroScriptGenericInterfaceTests "Tests")
	add_test(NAME VeroScriptGenericInterfaceTests COMMAND VeroScriptGenericInterfaceTests)

	add_executable(VeroScriptTemplateTests Private/Tests/VeroScriptTemplateTests.cpp)
	target_link_libraries(VeroScriptTemplateTests PRIVATE VeroScript)
	target_compile_definitions(VeroScriptTemplateTests PRIVATE
		VEROSCRIPT_TEMPLATE_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/examples")
	vs_apply_cxx_defaults(VeroScriptTemplateTests)
	vs_enable_large_test_stack(VeroScriptTemplateTests)
	vs_set_solution_folder(VeroScriptTemplateTests "Tests")
	add_test(NAME VeroScriptTemplateTests COMMAND VeroScriptTemplateTests)
	set_tests_properties(VeroScriptTemplateTests PROPERTIES TIMEOUT 60 LABELS "Core;VeroScript;Templates")

	add_executable(VeroScriptMultiInheritanceTests Private/Tests/VeroScriptMultiInheritanceTests.cpp)
	target_link_libraries(VeroScriptMultiInheritanceTests PRIVATE VeroScript)
	target_include_directories(VeroScriptMultiInheritanceTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptMultiInheritanceTests)
	vs_set_solution_folder(VeroScriptMultiInheritanceTests "Tests")
	add_test(NAME VeroScriptMultiInheritanceTests COMMAND VeroScriptMultiInheritanceTests)

	add_executable(VeroScriptFreeFunctionOverloadTests Private/Tests/VeroScriptFreeFunctionOverloadTests.cpp)
	target_link_libraries(VeroScriptFreeFunctionOverloadTests PRIVATE VeroScript)
	target_include_directories(VeroScriptFreeFunctionOverloadTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptFreeFunctionOverloadTests)
	vs_set_solution_folder(VeroScriptFreeFunctionOverloadTests "Tests")
	add_test(NAME VeroScriptFreeFunctionOverloadTests COMMAND VeroScriptFreeFunctionOverloadTests)

	add_executable(VeroScriptDefaultParamTests Private/Tests/VeroScriptDefaultParamTests.cpp)
	target_link_libraries(VeroScriptDefaultParamTests PRIVATE VeroScript)
	target_include_directories(VeroScriptDefaultParamTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptDefaultParamTests)
	vs_set_solution_folder(VeroScriptDefaultParamTests "Tests")
	add_test(NAME VeroScriptDefaultParamTests COMMAND VeroScriptDefaultParamTests)

	add_executable(VeroScriptSpecialMemberTests Private/Tests/VeroScriptSpecialMemberTests.cpp)
	target_link_libraries(VeroScriptSpecialMemberTests PRIVATE VeroScript)
	target_compile_definitions(VeroScriptSpecialMemberTests PRIVATE
		VEROSCRIPT_SPECIAL_MEMBER_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/examples/default_special_members.vs")
	vs_apply_cxx_defaults(VeroScriptSpecialMemberTests)
	vs_set_solution_folder(VeroScriptSpecialMemberTests "Tests")
	add_test(NAME VeroScriptSpecialMemberTests COMMAND VeroScriptSpecialMemberTests)

	add_executable(VeroScriptVarargsTests Private/Tests/VeroScriptVarargsTests.cpp)
	target_link_libraries(VeroScriptVarargsTests PRIVATE VeroScript)
	target_include_directories(VeroScriptVarargsTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptVarargsTests)
	vs_set_solution_folder(VeroScriptVarargsTests "Tests")
	add_test(NAME VeroScriptVarargsTests COMMAND VeroScriptVarargsTests)

	add_executable(VeroScriptConceptBoundsTests Private/Tests/VeroScriptConceptBoundsTests.cpp)
	target_link_libraries(VeroScriptConceptBoundsTests PRIVATE VeroScript)
	target_include_directories(VeroScriptConceptBoundsTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptConceptBoundsTests)
	vs_set_solution_folder(VeroScriptConceptBoundsTests "Tests")
	add_test(NAME VeroScriptConceptBoundsTests COMMAND VeroScriptConceptBoundsTests)

	add_executable(VeroScriptAliasNamespaceTests Private/Tests/VeroScriptAliasNamespaceTests.cpp)
	add_executable(VeroScriptConsoleTests Private/Tests/VeroScriptConsoleTests.cpp)
	target_link_libraries(VeroScriptConsoleTests PRIVATE VeroScript)
	target_include_directories(VeroScriptConsoleTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptConsoleTests)
	vs_set_solution_folder(VeroScriptConsoleTests "Tests")
	add_test(NAME VeroScriptConsoleTests COMMAND VeroScriptConsoleTests)
	target_link_libraries(VeroScriptAliasNamespaceTests PRIVATE VeroScript)
	target_include_directories(VeroScriptAliasNamespaceTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptAliasNamespaceTests)
	vs_set_solution_folder(VeroScriptAliasNamespaceTests "Tests")
	add_test(NAME VeroScriptAliasNamespaceTests COMMAND VeroScriptAliasNamespaceTests)

	add_executable(VeroScriptStaticMemberTests Private/Tests/VeroScriptStaticMemberTests.cpp)
	target_link_libraries(VeroScriptStaticMemberTests PRIVATE VeroScript)
	target_include_directories(VeroScriptStaticMemberTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptStaticMemberTests)
	vs_set_solution_folder(VeroScriptStaticMemberTests "Tests")
	add_test(NAME VeroScriptStaticMemberTests COMMAND VeroScriptStaticMemberTests)

	add_executable(VeroScriptOperatorOverloadTests Private/Tests/VeroScriptOperatorOverloadTests.cpp)
	target_link_libraries(VeroScriptOperatorOverloadTests PRIVATE VeroScript)
	target_include_directories(VeroScriptOperatorOverloadTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptOperatorOverloadTests)
	vs_set_solution_folder(VeroScriptOperatorOverloadTests "Tests")
	add_test(NAME VeroScriptOperatorOverloadTests COMMAND VeroScriptOperatorOverloadTests)

	add_executable(VeroScriptLocaleTests Private/Tests/VeroScriptLocaleTests.cpp)
	target_link_libraries(VeroScriptLocaleTests PRIVATE VeroScript)
	target_include_directories(VeroScriptLocaleTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptLocaleTests)
	vs_set_solution_folder(VeroScriptLocaleTests "Tests")
	add_test(
		NAME VeroScriptLocaleTests
		COMMAND VeroScriptLocaleTests
		WORKING_DIRECTORY "$<TARGET_FILE_DIR:VeroScriptLocaleTests>")
	add_test(NAME VeroScriptLocaleSourceTests
		COMMAND "${CMAKE_COMMAND}"
			"-DTEST_EXECUTABLE=$<TARGET_FILE:VeroScriptLocaleTests>"
			"-DTEST_EMULATOR=$<TARGET_PROPERTY:VeroScriptLocaleTests,CROSSCOMPILING_EMULATOR>"
			"-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/locale-source-test"
			"-DMODULE_ROOT=${VEROSCRIPT_MODULE_ROOT}"
			-P "${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptLocaleSourceTest.cmake")
	set_tests_properties(VeroScriptLocaleSourceTests PROPERTIES LABELS "VeroScript;Core")

	add_executable(VeroScriptEnvironmentTests Private/Tests/VeroScriptEnvironmentTests.cpp)
	target_link_libraries(VeroScriptEnvironmentTests PRIVATE VeroScript)
	target_include_directories(VeroScriptEnvironmentTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptEnvironmentTests)
	vs_set_solution_folder(VeroScriptEnvironmentTests "Tests")
	add_test(NAME VeroScriptEnvironmentTests COMMAND VeroScriptEnvironmentTests)

	add_executable(VeroScriptPreprocessorTests Private/Tests/VeroScriptPreprocessorTests.cpp)
	target_link_libraries(VeroScriptPreprocessorTests PRIVATE VeroScript)
	target_include_directories(VeroScriptPreprocessorTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptPreprocessorTests)
	vs_set_solution_folder(VeroScriptPreprocessorTests "Tests")
	add_test(NAME VeroScriptPreprocessorTests COMMAND VeroScriptPreprocessorTests)

	add_executable(VeroScriptMacroModuleTests Private/Tests/VeroScriptMacroModuleTests.cpp)
	target_link_libraries(VeroScriptMacroModuleTests PRIVATE VeroScript)
	target_include_directories(VeroScriptMacroModuleTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptMacroModuleTests)
	vs_set_solution_folder(VeroScriptMacroModuleTests "Tests")
	add_test(NAME VeroScriptMacroModuleTests COMMAND VeroScriptMacroModuleTests)

	# Keep the backend-equivalence matrix separate from the large JIT feature
	# suite. It is intentionally small enough to run in every sanitizer and
	# asmjit-off configuration while comparing structured diagnostics exactly.


	add_executable(VeroScriptNativeCallSiteTests Private/Tests/VeroScriptNativeCallSiteTests.cpp)
	target_link_libraries(VeroScriptNativeCallSiteTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptNativeCallSiteTests)
	vs_set_solution_folder(VeroScriptNativeCallSiteTests "Tests")
	add_test(NAME VeroScriptNativeCallSiteTests COMMAND VeroScriptNativeCallSiteTests)

	add_executable(VeroScriptHostBindingTests Private/Tests/VeroScriptHostBindingTests.cpp)
	target_sources(VeroScriptHostBindingTests PRIVATE
		Private/Tests/Suites/VeroScriptHostBinding/01_catalog_and_registry.inl
		Private/Tests/Suites/VeroScriptHostBinding/02_host_values_and_inheritance.inl
		Private/Tests/Suites/VeroScriptHostBinding/03_containers_and_references.inl
		Private/Tests/Suites/VeroScriptHostBinding/04_metadata_and_enums.inl
		Private/Tests/Suites/VeroScriptHostBinding/05_generics_and_serialization.inl)
	target_link_libraries(VeroScriptHostBindingTests PRIVATE VeroScript)
	target_include_directories(VeroScriptHostBindingTests PRIVATE Private)
	vs_apply_cxx_defaults(VeroScriptHostBindingTests)
	vs_enable_large_test_stack(VeroScriptHostBindingTests)
	vs_set_solution_folder(VeroScriptHostBindingTests "Tests")
	add_test(NAME VeroScriptHostBindingTests COMMAND VeroScriptHostBindingTests)

	add_executable(VeroScriptScopedTypeTests Private/Tests/VeroScriptScopedTypeTests.cpp)
	target_link_libraries(VeroScriptScopedTypeTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptScopedTypeTests)
	vs_enable_large_test_stack(VeroScriptScopedTypeTests)
	vs_set_solution_folder(VeroScriptScopedTypeTests "Tests")
	add_test(NAME VeroScriptScopedTypeTests COMMAND VeroScriptScopedTypeTests)

	add_executable(VeroScriptNativeOverloadTests Private/Tests/VeroScriptNativeOverloadTests.cpp)
	target_link_libraries(VeroScriptNativeOverloadTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptNativeOverloadTests)
	vs_set_solution_folder(VeroScriptNativeOverloadTests "Tests")
	add_test(NAME VeroScriptNativeOverloadTests COMMAND VeroScriptNativeOverloadTests)



	add_executable(VeroScriptMethodBindingTests Private/Tests/VeroScriptMethodBindingTests.cpp)
	target_link_libraries(VeroScriptMethodBindingTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptMethodBindingTests)
	vs_enable_large_test_stack(VeroScriptMethodBindingTests)
	vs_set_solution_folder(VeroScriptMethodBindingTests "Tests")
	add_test(NAME VeroScriptMethodBindingTests COMMAND VeroScriptMethodBindingTests
		"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")

	add_executable(VeroScriptInheritancePathTests Private/Tests/VeroScriptInheritancePathTests.cpp)
	target_link_libraries(VeroScriptInheritancePathTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptInheritancePathTests)
	vs_set_solution_folder(VeroScriptInheritancePathTests "Tests")
	# Exercise validated inheritance metadata with the host's default stack size.
	add_test(NAME VeroScriptInheritancePathTests COMMAND VeroScriptInheritancePathTests
		"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")

	add_executable(VeroScriptAttributeTests Private/Tests/VeroScriptAttributeTests.cpp)
	target_link_libraries(VeroScriptAttributeTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptAttributeTests)
	vs_enable_large_test_stack(VeroScriptAttributeTests)
	vs_set_solution_folder(VeroScriptAttributeTests "Tests")
	add_test(NAME VeroScriptAttributeTests COMMAND VeroScriptAttributeTests
		"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")

	add_executable(VeroScriptSessionApiTests Private/Tests/VeroScriptSessionApiTests.cpp)
	target_link_libraries(VeroScriptSessionApiTests PRIVATE VeroScript)
	vs_apply_cxx_defaults(VeroScriptSessionApiTests)
	vs_set_solution_folder(VeroScriptSessionApiTests "Tests")
	add_test(
		NAME VeroScriptSessionApiTests
		COMMAND VeroScriptSessionApiTests
		WORKING_DIRECTORY "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts")

	# One architecture-focused correctness surface for local builds and CI. Keep
	# performance tests separate: their thresholds and scheduling requirements are
	# intentionally different from functional portability validation.
	set(_VeroScript_core_correctness_tests
		VeroScriptModuleEnvironmentTests
		VeroScriptNativeExtensionTests
		VeroScriptMethodBindingTests
		VeroScriptInheritancePathTests
		VeroScriptAttributeTests
		VeroScriptCompileProcessingTests
		VeroScriptSourceMetadataIOTests
		VeroScriptAnnotationProcessingTests
		VeroScriptReflectionTypeTests
		VeroScriptMacroModuleTests
		VeroScriptMemoryTests
		VeroScriptI18nTests
		VeroScriptLexerTests
		VeroScriptPublicHeadersTests
		VeroScriptScalarStorageTests
		VeroScriptCompilerReuseTests
		VeroScriptAnalysisBoundaryTests
		VeroScriptM1Tests
		VeroScriptExceptionTests
		VeroScriptM2Tests
		VeroScriptM3Tests
		VeroScriptBytecodeIOTests
		VeroScriptBytecodeFuzzTests
		VeroScriptPackageTests
		VeroScriptDeclMetaTests
		VeroScriptDebuggerTests
		VeroScriptConcurrentTests
		VeroScriptTaskGroupAllocationTests
		VeroScriptLambdaTests
		VeroScriptFormatIOTests
		VeroScriptFallthroughTests
		VeroScriptInheritanceTests
		VeroScriptInterfaceTests
		VeroScriptGenericInterfaceTests
		VeroScriptTemplateTests
		VeroScriptMultiInheritanceTests
		VeroScriptFreeFunctionOverloadTests
		VeroScriptDefaultParamTests
		VeroScriptSpecialMemberTests
		VeroScriptVarargsTests
		VeroScriptConceptBoundsTests
		VeroScriptAliasNamespaceTests
		VeroScriptStaticMemberTests
		VeroScriptOperatorOverloadTests
		VeroScriptLocaleTests
		VeroScriptEnvironmentTests
		VeroScriptPreprocessorTests
		VeroScriptJitContractTests
		VeroScriptHostBindingTests
		VeroScriptNativeOverloadTests
		VeroScriptScopedTypeTests
		VeroScriptNativeCallSiteTests
		VeroScriptSessionApiTests)
	foreach(_VeroScript_vm_example IN ITEMS VeroScriptModuleEnvironmentExample
		VeroScriptCompileProcessingExample VeroScriptI18nExample)
		if(TARGET ${_VeroScript_vm_example})
			list(APPEND _VeroScript_core_correctness_tests ${_VeroScript_vm_example})
		endif()
	endforeach()
	set_property(TEST ${_VeroScript_core_correctness_tests}
		APPEND PROPERTY LABELS VeroScript Core)
	add_custom_target(VeroScriptCoreTests
		DEPENDS ${_VeroScript_core_correctness_tests} VeroScriptMemoryAllocationTests
		COMMENT "Building VeroScript core correctness tests")
	vs_set_solution_folder(VeroScriptCoreTests "Tests")

	add_test(
		NAME VeroScriptArchitectureTests
		COMMAND "${CMAKE_COMMAND}"
			-DVS_SOURCE_ROOT=${VEROSCRIPT_MODULE_ROOT}
			-DVS_BINARY_ROOT=${PROJECT_BINARY_DIR}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptArchitectureTests.cmake")
	set_property(TEST VeroScriptArchitectureTests
		APPEND PROPERTY LABELS VeroScript Core Architecture)

	# These stress fixtures retain the same workload when run through an emulator.
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(VeroScriptScriptCompileExtensionsTests VeroScriptAnalysisBoundaryTests
			PROPERTIES TIMEOUT 120)
		set_tests_properties(VeroScriptTemplateTests VeroScriptScopedTypeTests
			VeroScriptPerformanceRegressionTests PROPERTIES TIMEOUT 600)
		if(TARGET VeroScriptScriptCompileExample)
			target_compile_definitions(VeroScriptScriptCompileExample PRIVATE
				VEROSCRIPT_EXAMPLE_COMPILE_TIMEOUT_MS=20000)
		endif()
	endif()
endif()
