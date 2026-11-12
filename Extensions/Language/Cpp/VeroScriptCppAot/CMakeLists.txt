# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScriptCppAot)
vs_set_solution_folder(VeroScriptCppAot "Libraries")
target_link_libraries(VeroScriptCppAot PUBLIC VeroScript)
set(_vs_console_header "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public/VeroScriptConsole.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_console_header}")
set(_vs_console_native "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private/VeroScriptConsoleNative.h")
set(_vs_console_api "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private/VeroScriptConsole.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_console_native}" "${_vs_console_api}")
file(READ "${_vs_console_header}" _vs_console_source)
file(READ "${_vs_console_native}" _vs_console_native_source)
file(READ "${_vs_console_api}" _vs_console_api_source)
string(REPLACE "#include \"VeroScriptConsole.h\"" "" _vs_console_native_source "${_vs_console_native_source}")
string(REPLACE "#include \"VeroScriptConsoleNative.h\"" "" _vs_console_api_source "${_vs_console_api_source}")
string(REPLACE "\n\tbool " "\n\tinline bool " _vs_console_api_source "${_vs_console_api_source}")
string(REPLACE "\n\tFInfo " "\n\tinline FInfo " _vs_console_api_source "${_vs_console_api_source}")
string(APPEND _vs_console_source "\n${_vs_console_native_source}\n${_vs_console_api_source}")
# Standalone output may be compiled beside the embedding SDK. Give its copy a
# private namespace so it does not redeclare SDK types or runtime symbols.
string(REPLACE "VeroScript::Console" "VsConsoleRuntime" _vs_console_source "${_vs_console_source}")
string(REPLACE "#pragma once" "" _vs_console_source "${_vs_console_source}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/VeroScriptConsoleSource.h"
	CONTENT "#pragma once\ninline constexpr const char* VsConsoleSource = R\"VS_CONSOLE(@_vs_console_source@)VS_CONSOLE\";\n" @ONLY)
target_include_directories(VeroScriptCppAot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")

# Standalone output uses the same locale-independent floating conversion as Core.
set(_vs_float_header "${VEROSCRIPT_MODULE_ROOT}/Shared/Private/VeroScriptFloatParsing.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_float_header}")
file(READ "${_vs_float_header}" _vs_float_source)
string(REPLACE "#pragma once" "" _vs_float_source "${_vs_float_source}")
string(REPLACE "VeroScript::FloatParsingDetail" "VsFloatParsingDetail" _vs_float_source "${_vs_float_source}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/VeroScriptFloatParsingSource.h"
	CONTENT "#pragma once\ninline constexpr const char* VsFloatParsingSource = R\"VS_FLOAT(@_vs_float_source@)VS_FLOAT\";\n" @ONLY)

# Container/type canonicalization and module visibility are shared with their
# authoritative implementations. Keep this dependency private so generated-code
# consumers see only public SDK headers and AOT cannot expose runtime internals.
target_include_directories(VeroScriptCppAot PRIVATE
	"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private")

get_target_property(_VeroScriptCppAot_sources VeroScriptCppAot SOURCES)
list(FILTER _VeroScriptCppAot_sources EXCLUDE REGEX "/Tests/")
set_target_properties(VeroScriptCppAot PROPERTIES SOURCES "${_VeroScriptCppAot_sources}")

if(NOT CMAKE_CROSSCOMPILING)
	add_executable(VeroScriptCoreAotGenerate EXCLUDE_FROM_ALL Resources/Examples/CoreAot/Generate.cpp)
	target_link_libraries(VeroScriptCoreAotGenerate PRIVATE VeroScriptCppAot)
	vs_apply_cxx_defaults(VeroScriptCoreAotGenerate)
	if(MINGW)
		target_link_options(VeroScriptCoreAotGenerate PRIVATE -municode)
	endif()
	vs_set_solution_folder(VeroScriptCoreAotGenerate "Examples")
endif()

if(BUILD_TESTING)
	add_executable(VeroScriptCppCoreAotTests Private/Tests/VeroScriptCppCoreAotTests.cpp)
	target_link_libraries(VeroScriptCppCoreAotTests PRIVATE VeroScriptCppAot)
	vs_apply_cxx_defaults(VeroScriptCppCoreAotTests)
	add_test(NAME VeroScriptCppCoreAotTests COMMAND VeroScriptCppCoreAotTests)
	set_tests_properties(VeroScriptCppCoreAotTests PROPERTIES LABELS "Core;Aot;Mobile" TIMEOUT 60)
	if(NOT CMAKE_CROSSCOMPILING AND TARGET VeroScriptInterop AND NOT VEROSCRIPT_INTEROP_STATIC)
		add_dependencies(VeroScriptCppCoreAotTests VeroScriptInterop)
		set(_core_aot_mobile_args)
		if(VEROSCRIPT_BUILD_MOBILE_ADAPTERS)
			add_dependencies(VeroScriptCppCoreAotTests VeroScriptMobile)
			list(APPEND _core_aot_mobile_args "-DMOBILE=$<TARGET_FILE:VeroScriptMobile>"
				"-DMOBILE_INCLUDE=${VEROSCRIPT_MODULE_ROOT}/Extensions/Platform/Mobile/VeroScriptMobile/Public")
		endif()
		get_property(_core_aot_runtime_set TARGET VeroScript PROPERTY MSVC_RUNTIME_LIBRARY SET)
		add_test(NAME VeroScriptCppCoreAotNativeTests COMMAND "${CMAKE_COMMAND}"
			"-DVM_STATIC=$<TARGET_FILE:VeroScriptBytecodeVm>"
			"-DVM_INCLUDE=${VEROSCRIPT_MODULE_ROOT}/Modules/Execution/VeroScriptBytecodeVm/Public"
			"-DEMITTER=$<TARGET_FILE:VeroScriptCppCoreAotTests>"
			"-DWORK=${CMAKE_CURRENT_BINARY_DIR}/core-aot-consumer"
			"-DCONSUMER=${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/CoreAotNativeConsumer.cpp"
			"-DCORE=$<TARGET_LINKER_FILE:VeroScript>"
			"-DINCLUDES=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public"
			"-DGENERATED=$<TARGET_PROPERTY:VeroScript,BINARY_DIR>/Generated"
			"-DINTEROP=$<TARGET_LINKER_FILE:VeroScriptInterop>"
			"-DINTEROP_RUNTIME=$<TARGET_FILE:VeroScriptInterop>"
			"-DINTEROP_INCLUDE=${VEROSCRIPT_MODULE_ROOT}/VeroScriptInterop/Public"
			"-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
			"-DCONFIG=$<CONFIG>" "-DCXX_STANDARD=${CMAKE_CXX_STANDARD}"
			"-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
			"-DOSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
			"-DOSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
			"-DOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
			"-DMSVC_RUNTIME_LIBRARY_SET=${_core_aot_runtime_set}"
			"-DMSVC_RUNTIME_LIBRARY=$<TARGET_GENEX_EVAL:VeroScript,$<TARGET_PROPERTY:VeroScript,MSVC_RUNTIME_LIBRARY>>"
			"-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
			"-DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
			${_core_aot_mobile_args}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/CoreAotNativeTest.cmake")
		set_tests_properties(VeroScriptCppCoreAotNativeTests PROPERTIES LABELS "Core;Aot;Mobile" RUN_SERIAL TRUE TIMEOUT 180)
	endif()
	set(_cpp_script_fixture "${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/Cpp/VeroScriptCpp/Private/Tests")
	# The build defines C++ input tools before AOT. Exercise their complete native
	# round trip only when both modules and a runnable desktop toolchain are present.
	if(TARGET VeroScriptCppTranspilerTests AND NOT CMAKE_CROSSCOMPILING)
		target_link_libraries(VeroScriptCppTranspilerTests PRIVATE VeroScriptCppAot)
		target_compile_definitions(VeroScriptCppTranspilerTests PRIVATE VEROSCRIPT_CPP_TRANSPILE_AOT_TEST=1)
		add_test(NAME VeroScriptCppTranspilerNativeTests COMMAND "${CMAKE_COMMAND}"
			"-DTEST_EXE=$<TARGET_FILE:VeroScriptCppTranspilerTests>"
			"-DWORK=${CMAKE_CURRENT_BINARY_DIR}/transpiled-native-test"
			"-DGENERATOR=${CMAKE_GENERATOR}" "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
			"-DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
			-P "${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/Cpp/VeroScriptCppTranspiler/Private/Tests/VeroScriptCppTranspilerNativeTest.cmake")
		set_tests_properties(VeroScriptCppTranspilerNativeTests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
	endif()
	add_executable(VeroScriptCppAotEmitTests Private/Tests/VeroScriptCppAotEmitTests.cpp)
	target_include_directories(VeroScriptCppAotEmitTests PRIVATE "${_cpp_script_fixture}")
	target_link_libraries(VeroScriptCppAotEmitTests PRIVATE VeroScriptCppAot)
	target_compile_definitions(VeroScriptCppAotEmitTests PRIVATE
		VEROSCRIPT_STDLIB_DIR="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptCppAotEmitTests)
	vs_set_solution_folder(VeroScriptCppAotEmitTests "Tests")
	add_test(NAME VeroScriptCppAotEmitTests COMMAND VeroScriptCppAotEmitTests)
	add_test(NAME VeroScriptCppAotConstructionLookupTests
		COMMAND VeroScriptCppAotEmitTests --construction-diamond-only)
	set_tests_properties(VeroScriptCppAotConstructionLookupTests PROPERTIES TIMEOUT 30)
	set_tests_properties(VeroScriptCppAotEmitTests PROPERTIES
		ENVIRONMENT "VERO_STDLIB_PATH=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")

	add_executable(VeroScriptCppAotBundleTests Private/Tests/VeroScriptCppAotBundleTests.cpp)
	target_link_libraries(VeroScriptCppAotBundleTests PRIVATE VeroScriptCppAot)
	vs_apply_cxx_defaults(VeroScriptCppAotBundleTests)
	vs_set_solution_folder(VeroScriptCppAotBundleTests "Tests")
	add_test(NAME VeroScriptCppAotBundleTests COMMAND VeroScriptCppAotBundleTests)
	set_tests_properties(VeroScriptCppAotBundleTests PROPERTIES TIMEOUT 30)

	add_executable(VeroScriptCppAotHelloTests Private/Tests/VeroScriptCppAotHelloTests.cpp)
	target_include_directories(VeroScriptCppAotHelloTests PRIVATE "${_cpp_script_fixture}")
	target_link_libraries(VeroScriptCppAotHelloTests PRIVATE VeroScriptCppAot)
	target_compile_definitions(VeroScriptCppAotHelloTests PRIVATE
		VEROSCRIPT_STDLIB_DIR="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	if(MSVC)
		target_compile_definitions(VeroScriptCppAotHelloTests PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
	vs_apply_cxx_defaults(VeroScriptCppAotHelloTests)
	vs_set_solution_folder(VeroScriptCppAotHelloTests "Tests")
	add_test(NAME VeroScriptCppAotHelloTests COMMAND VeroScriptCppAotHelloTests)
	# Native DualRun (MSVC vcvars / g++) repeatedly starts a native compiler and
	# linker.  Running it beside the other generated-code suites can starve those
	# child processes long enough to trip an otherwise healthy timeout.
	set_tests_properties(VeroScriptCppAotHelloTests PROPERTIES
		RUN_SERIAL TRUE
		TIMEOUT 900
		ENVIRONMENT "VERO_STDLIB_PATH=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
endif()
