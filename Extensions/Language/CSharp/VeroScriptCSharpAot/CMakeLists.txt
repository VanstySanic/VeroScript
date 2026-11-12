# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScriptCSharpAot)
vs_use_shared_private_headers(VeroScriptCSharpAot)
vs_set_solution_folder(VeroScriptCSharpAot "Libraries")
target_link_libraries(VeroScriptCSharpAot PUBLIC VeroScript)
if(DEFINED VS_VEROSCRIPT_CSHARP_VERSION)
	set(_VeroScriptCSharpAot_version "${VS_VEROSCRIPT_CSHARP_VERSION}")
else()
	set(_VeroScriptCSharpAot_version "0.1.1")
endif()
target_compile_definitions(VeroScriptCSharpAot PRIVATE
	VEROSCRIPT_CSHARP_AOT_VERSION="${_VeroScriptCSharpAot_version}")

# The emitter walks the checked compiler AST. Keep that implementation detail
# private even though the backend contract itself is public.
target_include_directories(VeroScriptCSharpAot PRIVATE
	"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private")

get_target_property(_VeroScriptCSharpAot_sources VeroScriptCSharpAot SOURCES)
list(FILTER _VeroScriptCSharpAot_sources EXCLUDE REGEX "/Tests/")
set_target_properties(VeroScriptCSharpAot PROPERTIES
	SOURCES "${_VeroScriptCSharpAot_sources}")

if(BUILD_TESTING)
	add_executable(VeroScriptCSharpAotEmitTests
		Private/Tests/VeroScriptCSharpAotEmitTests.cpp)
	target_link_libraries(VeroScriptCSharpAotEmitTests PRIVATE VeroScriptCSharpAot)
	target_compile_definitions(VeroScriptCSharpAotEmitTests PRIVATE
		VEROSCRIPT_STDLIB_DIR="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
		VEROSCRIPT_CSHARP_AOT_TEST_VERSION="${_VeroScriptCSharpAot_version}")
	vs_apply_cxx_defaults(VeroScriptCSharpAotEmitTests)
	vs_set_solution_folder(VeroScriptCSharpAotEmitTests "Tests")
	add_test(NAME VeroScriptCSharpAotEmitTests COMMAND VeroScriptCSharpAotEmitTests)
	set_tests_properties(VeroScriptCSharpAotEmitTests PROPERTIES
		ENVIRONMENT "VERO_STDLIB_PATH=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
		LABELS "VeroScript;CSharp;Aot")

	add_executable(VeroScriptCSharpAotBundleTests
		Private/Tests/VeroScriptCSharpAotBundleTests.cpp)
	target_link_libraries(VeroScriptCSharpAotBundleTests PRIVATE VeroScriptCSharpAot)
	vs_apply_cxx_defaults(VeroScriptCSharpAotBundleTests)
	vs_set_solution_folder(VeroScriptCSharpAotBundleTests "Tests")
	add_test(NAME VeroScriptCSharpAotBundleTests COMMAND VeroScriptCSharpAotBundleTests)
	set_tests_properties(VeroScriptCSharpAotBundleTests PROPERTIES
		LABELS "VeroScript;CSharp;Aot;Bundle")

	if(VS_VEROSCRIPT_CSHARP_DOTNET)
		set(_VeroScriptCSharpAot_dotnet "${VS_VEROSCRIPT_CSHARP_DOTNET}")
	else()
		unset(_VeroScriptCSharpAot_dotnet)
		find_program(_VeroScriptCSharpAot_dotnet NAMES dotnet NO_CACHE)
	endif()
	if(_VeroScriptCSharpAot_dotnet)
		execute_process(
			COMMAND "${_VeroScriptCSharpAot_dotnet}" --version
			OUTPUT_VARIABLE _VeroScriptCSharpAot_sdk_version
			OUTPUT_STRIP_TRAILING_WHITESPACE
			RESULT_VARIABLE _VeroScriptCSharpAot_sdk_result)
		if(NOT _VeroScriptCSharpAot_sdk_result EQUAL 0
				OR _VeroScriptCSharpAot_sdk_version VERSION_LESS 8.0)
			message(STATUS
				"VeroScriptCSharpAot: generated C# execution test skipped (.NET SDK 8.0 or newer is required)")
			set(_VeroScriptCSharpAot_dotnet "")
		endif()
	endif()
	if(_VeroScriptCSharpAot_dotnet)
		add_executable(VeroScriptCSharpAotBindingTests
			Private/Tests/VeroScriptCSharpAotBindingTests.cpp)
		target_link_libraries(VeroScriptCSharpAotBindingTests PRIVATE VeroScriptCSharpAot)
		target_compile_definitions(VeroScriptCSharpAotBindingTests PRIVATE
			VEROSCRIPT_CSHARP_AOT_DOTNET="${_VeroScriptCSharpAot_dotnet}"
			VEROSCRIPT_CSHARP_AOT_BINDING_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/GeneratedBindingTest"
			VEROSCRIPT_CSHARP_BINDGEN_CLI_PROJECT="${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/CSharp/VeroScriptCSharpBindGen/Cli/VeroScriptCSharpBindGen.Cli.csproj")
		vs_apply_cxx_defaults(VeroScriptCSharpAotBindingTests)
		vs_set_solution_folder(VeroScriptCSharpAotBindingTests "Tests")
		add_test(NAME VeroScriptCSharpAotBindingTests COMMAND VeroScriptCSharpAotBindingTests)
		set_tests_properties(VeroScriptCSharpAotBindingTests PROPERTIES
			LABELS "VeroScript;CSharp;Aot;Binding;BindGen"
			RESOURCE_LOCK VeroScriptCSharpDotNet
			TIMEOUT 180)

		set(_VeroScriptCSharpAot_test_native 0)
		set(_VeroScriptCSharpAot_rid "")
		if(NOT CMAKE_CROSSCOMPILING AND CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(_VeroScriptCSharpAot_arch "")
			if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
				set(_VeroScriptCSharpAot_arch "arm64")
			elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64)$")
				set(_VeroScriptCSharpAot_arch "x64")
			endif()
			if(_VeroScriptCSharpAot_arch)
				if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
					set(_VeroScriptCSharpAot_rid "win-${_VeroScriptCSharpAot_arch}")
				elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
					set(_VeroScriptCSharpAot_rid "osx-${_VeroScriptCSharpAot_arch}")
				elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
					set(_VeroScriptCSharpAot_rid "linux-${_VeroScriptCSharpAot_arch}")
				endif()
			endif()
			if(_VeroScriptCSharpAot_rid)
				set(_VeroScriptCSharpAot_test_native 1)
			endif()
		endif()
		target_compile_definitions(VeroScriptCSharpAotBindingTests PRIVATE
			VEROSCRIPT_CSHARP_AOT_RID="${_VeroScriptCSharpAot_rid}"
			VEROSCRIPT_CSHARP_AOT_TEST_NATIVE=${_VeroScriptCSharpAot_test_native})
		add_executable(VeroScriptCSharpAotHelloTests
			Private/Tests/VeroScriptCSharpAotHelloTests.cpp)
		target_link_libraries(VeroScriptCSharpAotHelloTests PRIVATE VeroScriptCSharpAot)
		target_compile_definitions(VeroScriptCSharpAotHelloTests PRIVATE
			VEROSCRIPT_CSHARP_AOT_DOTNET="${_VeroScriptCSharpAot_dotnet}"
			VEROSCRIPT_CSHARP_AOT_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/GeneratedTest"
			VEROSCRIPT_CSHARP_AOT_RID="${_VeroScriptCSharpAot_rid}"
			VEROSCRIPT_CSHARP_AOT_TEST_NATIVE=${_VeroScriptCSharpAot_test_native})
		vs_apply_cxx_defaults(VeroScriptCSharpAotHelloTests)
		vs_set_solution_folder(VeroScriptCSharpAotHelloTests "Tests")
		add_test(NAME VeroScriptCSharpAotHelloTests COMMAND VeroScriptCSharpAotHelloTests)
		set_tests_properties(VeroScriptCSharpAotHelloTests PROPERTIES
			LABELS "VeroScript;CSharp;Aot"
			RESOURCE_LOCK VeroScriptCSharpDotNet
			TIMEOUT 300)
	else()
		message(STATUS "VeroScriptCSharpAot: generated C# execution test skipped (dotnet not found)")
	endif()
endif()

include(GNUInstallDirs)
install(TARGETS VeroScriptCSharpAot
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	COMPONENT VeroScriptCSharpAot)
install(DIRECTORY Public/
	DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/VeroScript"
	COMPONENT VeroScriptCSharpAot)
