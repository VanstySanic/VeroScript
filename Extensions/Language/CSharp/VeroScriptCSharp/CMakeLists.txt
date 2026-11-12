# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

option(VS_BUILD_VEROSCRIPT_CSHARP "Build the .NET VeroScript binding" ON)

if(NOT VS_BUILD_VEROSCRIPT_CSHARP)
	message(STATUS "VeroScriptCSharp: disabled")
	return()
endif()

set(VS_VEROSCRIPT_CSHARP_DOTNET "" CACHE FILEPATH
	"Path to the dotnet executable used to build and test VeroScriptCSharp")
set(_VeroScriptCSharp_dotnet_is_explicit FALSE)
if(VS_VEROSCRIPT_CSHARP_DOTNET)
	if(NOT EXISTS "${VS_VEROSCRIPT_CSHARP_DOTNET}"
			OR IS_DIRECTORY "${VS_VEROSCRIPT_CSHARP_DOTNET}")
		message(FATAL_ERROR
			"VS_VEROSCRIPT_CSHARP_DOTNET is not a dotnet executable: ${VS_VEROSCRIPT_CSHARP_DOTNET}")
	endif()
	set(_VeroScriptCSharp_dotnet "${VS_VEROSCRIPT_CSHARP_DOTNET}")
	set(_VeroScriptCSharp_dotnet_is_explicit TRUE)
else()
	find_program(_VeroScriptCSharp_dotnet NAMES dotnet NO_CACHE)
endif()
if(NOT _VeroScriptCSharp_dotnet)
	message(STATUS "VeroScriptCSharp: skipped (.NET SDK is required)")
	return()
endif()

execute_process(
	COMMAND "${_VeroScriptCSharp_dotnet}" --version
	OUTPUT_VARIABLE _VeroScriptCSharp_sdk_version
	OUTPUT_STRIP_TRAILING_WHITESPACE
	RESULT_VARIABLE _VeroScriptCSharp_sdk_result)
if(NOT _VeroScriptCSharp_sdk_result EQUAL 0
		OR _VeroScriptCSharp_sdk_version VERSION_LESS 9.0)
	if(_VeroScriptCSharp_dotnet_is_explicit)
		message(FATAL_ERROR
			"VS_VEROSCRIPT_CSHARP_DOTNET must select a working .NET SDK 9.0 or newer: ${_VeroScriptCSharp_dotnet}")
	endif()
	message(STATUS
		"VeroScriptCSharp: skipped (.NET SDK 9.0 or newer is required)")
	return()
endif()
message(STATUS
	"VeroScriptCSharp: using .NET SDK ${_VeroScriptCSharp_sdk_version} via ${_VeroScriptCSharp_dotnet}")

set(VS_VEROSCRIPT_CSHARP_VERSION "0.1.1" CACHE STRING
	"Version assigned to the VeroScript C# assembly and NuGet package")
if(NOT VS_VEROSCRIPT_CSHARP_VERSION MATCHES
		"^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?$")
	message(FATAL_ERROR
		"VS_VEROSCRIPT_CSHARP_VERSION must be a three-part NuGet version with an optional prerelease suffix")
endif()

if(CMAKE_CONFIGURATION_TYPES)
	set(_VeroScriptCSharp_configuration
		"$<IF:$<CONFIG:Debug>,Debug,Release>")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(_VeroScriptCSharp_configuration Debug)
else()
	set(_VeroScriptCSharp_configuration Release)
endif()

add_custom_target(VeroScriptCSharp
	COMMAND "${_VeroScriptCSharp_dotnet}" build
		"${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharp.csproj"
		--configuration "${_VeroScriptCSharp_configuration}"
		-p:Version="${VS_VEROSCRIPT_CSHARP_VERSION}"
		--nologo
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Building the VeroScript .NET binding")
add_dependencies(VeroScriptCSharp VeroScriptInterop)
vs_set_solution_folder(VeroScriptCSharp "Libraries")

add_custom_target(VeroScriptCSharpPackage
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/nuget"
	COMMAND "${_VeroScriptCSharp_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharp.csproj"
		--configuration "${_VeroScriptCSharp_configuration}"
		--no-build
		--output "${CMAKE_BINARY_DIR}/nuget"
		-p:Version="${VS_VEROSCRIPT_CSHARP_VERSION}"
		-p:PackageVersion="${VS_VEROSCRIPT_CSHARP_VERSION}"
		--nologo
	DEPENDS VeroScriptCSharp
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Packing the VeroScript .NET binding")
vs_set_solution_folder(VeroScriptCSharpPackage "Packaging")

if(BUILD_TESTING)
	add_library(VeroScriptCSharpLegacyMemoryRuntime SHARED Tests/LegacyMemoryRuntime.cpp)
	vs_apply_cxx_defaults(VeroScriptCSharpLegacyMemoryRuntime)
	vs_set_solution_folder(VeroScriptCSharpLegacyMemoryRuntime "Tests")
	add_library(VeroScriptCSharpAnnotationTestRuntime SHARED Tests/AnnotationServiceRuntime.cpp)
	target_link_libraries(VeroScriptCSharpAnnotationTestRuntime PRIVATE VeroScriptInterop VeroScript)
	vs_apply_cxx_defaults(VeroScriptCSharpAnnotationTestRuntime)
	vs_set_solution_folder(VeroScriptCSharpAnnotationTestRuntime "Tests")
	set(_VeroScriptCSharp_run_arch_args "")
	set(_VeroScriptCSharp_dotnet_arch "")
	if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(_VeroScriptCSharp_dotnet_arch "x86")
		list(APPEND _VeroScriptCSharp_run_arch_args
			--arch x86 --self-contained true)
	endif()
	set(_VeroScriptCSharp_native_aot_rid "")
	if(NOT CMAKE_CROSSCOMPILING AND CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(_VeroScriptCSharp_native_aot_arch "")
		if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
			set(_VeroScriptCSharp_native_aot_arch "arm64")
		elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64)$")
			set(_VeroScriptCSharp_native_aot_arch "x64")
		endif()
		# Visual Studio can target ARM64 while CMake itself runs under x64 emulation.
		if(WIN32)
			string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _VeroScriptCSharp_target_arch)
			if(NOT _VeroScriptCSharp_target_arch MATCHES "^(arm64|aarch64|x64|amd64|x86_64)$")
				string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" _VeroScriptCSharp_target_arch)
			endif()
			if(_VeroScriptCSharp_target_arch MATCHES "^(arm64|aarch64)$")
				set(_VeroScriptCSharp_native_aot_arch "arm64")
			elseif(_VeroScriptCSharp_target_arch MATCHES "^(x64|amd64|x86_64)$")
				set(_VeroScriptCSharp_native_aot_arch "x64")
			endif()
			if(_VeroScriptCSharp_native_aot_arch)
				set(_VeroScriptCSharp_dotnet_arch "${_VeroScriptCSharp_native_aot_arch}")
				list(APPEND _VeroScriptCSharp_run_arch_args
					--arch "${_VeroScriptCSharp_native_aot_arch}" --self-contained true)
			endif()
		endif()
		if(_VeroScriptCSharp_native_aot_arch)
			if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
				set(_VeroScriptCSharp_native_aot_rid
					"win-${_VeroScriptCSharp_native_aot_arch}")
			elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
				set(_VeroScriptCSharp_native_aot_rid
					"osx-${_VeroScriptCSharp_native_aot_arch}")
			elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
				set(_VeroScriptCSharp_native_aot_rid
					"linux-${_VeroScriptCSharp_native_aot_arch}")
			endif()
		endif()
	endif()

	foreach(_VeroScriptCSharp_test_tfm IN ITEMS net8.0 net9.0)
		add_test(NAME "VeroScriptCSharpLegacyMemoryTests-${_VeroScriptCSharp_test_tfm}"
			COMMAND "${_VeroScriptCSharp_dotnet}" run
				--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VeroScriptCSharp.Tests.csproj"
				--configuration "${_VeroScriptCSharp_configuration}"
				--framework "${_VeroScriptCSharp_test_tfm}"
				${_VeroScriptCSharp_run_arch_args} -- --memory-unsupported)
		set_tests_properties("VeroScriptCSharpLegacyMemoryTests-${_VeroScriptCSharp_test_tfm}" PROPERTIES
			ENVIRONMENT "VEROSCRIPT_INTEROP_LIBRARY=$<TARGET_FILE:VeroScriptCSharpLegacyMemoryRuntime>"
			LABELS "VeroScript;CSharp;Memory;Compatibility"
			RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 120)
		add_test(NAME "VeroScriptCSharpTests-${_VeroScriptCSharp_test_tfm}"
			COMMAND "${_VeroScriptCSharp_dotnet}" run
				--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VeroScriptCSharp.Tests.csproj"
				--configuration "${_VeroScriptCSharp_configuration}"
				--framework "${_VeroScriptCSharp_test_tfm}"
				${_VeroScriptCSharp_run_arch_args})
		set_tests_properties("VeroScriptCSharpTests-${_VeroScriptCSharp_test_tfm}" PROPERTIES
			ENVIRONMENT
				"VEROSCRIPT_INTEROP_LIBRARY=$<TARGET_FILE:VeroScriptInterop>;VEROSCRIPT_TEST_ANNOTATION_SERVICE_LIBRARY=$<TARGET_FILE:VeroScriptCSharpAnnotationTestRuntime>;VEROSCRIPT_STDLIB_DIR=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib;DOTNET_TieredCompilation=0"
			WORKING_DIRECTORY "$<TARGET_FILE_DIR:VeroScriptInterop>"
			LABELS "VeroScript;CSharp"
			RESOURCE_LOCK VeroScriptCSharpDotNet
			TIMEOUT 120)
		if(TARGET VeroScriptBytecodeVmBackend)
			add_test(NAME "VeroScriptCSharpVmBackendTests-${_VeroScriptCSharp_test_tfm}"
				COMMAND "${_VeroScriptCSharp_dotnet}" run
					--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VeroScriptCSharp.Tests.csproj"
					--configuration "${_VeroScriptCSharp_configuration}"
					--framework "${_VeroScriptCSharp_test_tfm}"
					${_VeroScriptCSharp_run_arch_args} -- --vm-backend-contracts)
			set_tests_properties("VeroScriptCSharpVmBackendTests-${_VeroScriptCSharp_test_tfm}" PROPERTIES
				ENVIRONMENT "VEROSCRIPT_INTEROP_LIBRARY=$<TARGET_FILE:VeroScriptInterop>;VEROSCRIPT_TEST_VM_BACKEND=$<TARGET_FILE:VeroScriptBytecodeVmBackend>"
				LABELS "VeroScript;CSharp;Vm" RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 120)
		endif()
	endforeach()

	add_test(NAME VeroScriptCSharpPackageTests
		COMMAND "${CMAKE_COMMAND}"
			-DVSC_DOTNET:FILEPATH=${_VeroScriptCSharp_dotnet}
			-DVSC_PROJECT:FILEPATH=${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharp.csproj
			-DVSC_CONFIGURATION:STRING=${_VeroScriptCSharp_configuration}
			-DVSC_VERSION:STRING=${VS_VEROSCRIPT_CSHARP_VERSION}
			-DVSC_PACKAGE_DIR:PATH=${CMAKE_BINARY_DIR}/nuget
			-DVSC_WORK_DIR:PATH=${CMAKE_BINARY_DIR}/VeroScriptCSharpPackageTest
			-DVSC_NATIVE_LIBRARY:FILEPATH=$<TARGET_FILE:VeroScriptInterop>
			-DVSC_NATIVE_ANNOTATION_LIBRARY:FILEPATH=$<TARGET_FILE:VeroScriptCSharpAnnotationTestRuntime>
			-DVSC_STDLIB_DIR:PATH=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib
			-DVSC_DOTNET_ARCH:STRING=${_VeroScriptCSharp_dotnet_arch}
			-DVSC_NATIVE_AOT_RID:STRING=${_VeroScriptCSharp_native_aot_rid}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyPackage.cmake")
	set_tests_properties(VeroScriptCSharpPackageTests PROPERTIES
		LABELS "VeroScript;CSharp;Package"
		RUN_SERIAL TRUE
		RESOURCE_LOCK VeroScriptCSharpDotNet
		TIMEOUT 600)

	# These host checks compile both native import profiles without mobile workloads.
	# Application linking, packaging and device acceptance remain platform gates.
	set(_VeroScriptCSharp_mobile_tfms net8.0 net9.0)
	if(_VeroScriptCSharp_sdk_version VERSION_GREATER_EQUAL 10.0)
		list(APPEND _VeroScriptCSharp_mobile_tfms net10.0)
	endif()
	foreach(_VeroScriptCSharp_mobile_tfm IN LISTS _VeroScriptCSharp_mobile_tfms)
		foreach(_VeroScriptCSharp_mobile_static IN ITEMS false true)
			set(_VeroScriptCSharp_mobile_test
				"VeroScriptCSharpMobileContracts-${_VeroScriptCSharp_mobile_tfm}-static-${_VeroScriptCSharp_mobile_static}")
			set(_VeroScriptCSharp_mobile_args "")
			if(NOT _VeroScriptCSharp_mobile_static)
				list(APPEND _VeroScriptCSharp_mobile_args -- "${VEROSCRIPT_MODULE_ROOT}")
			endif()
			add_test(NAME "${_VeroScriptCSharp_mobile_test}"
				COMMAND "${_VeroScriptCSharp_dotnet}" run
					--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Mobile/MobileContracts.csproj"
					--configuration "${_VeroScriptCSharp_configuration}"
					--framework "${_VeroScriptCSharp_mobile_tfm}"
					-p:VeroScriptMobileStaticProbe=${_VeroScriptCSharp_mobile_static}
					${_VeroScriptCSharp_run_arch_args} ${_VeroScriptCSharp_mobile_args})
			set_tests_properties("${_VeroScriptCSharp_mobile_test}" PROPERTIES
				ENVIRONMENT "VEROSCRIPT_INTEROP_LIBRARY=$<TARGET_FILE:VeroScriptInterop>"
				LABELS "VeroScript;CSharp;Mobile"
				RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 180)
		endforeach()
	endforeach()
	find_program(_VeroScriptCSharp_mobile_python NAMES python3 python NO_CACHE)
	if(_VeroScriptCSharp_mobile_python)
		add_test(NAME VeroScriptCSharpMobileSdkTests
			COMMAND "${_VeroScriptCSharp_mobile_python}"
				"${CMAKE_CURRENT_SOURCE_DIR}/Tests/Mobile/test_mobile_sdk.py" -v)
		set_tests_properties(VeroScriptCSharpMobileSdkTests PROPERTIES
			ENVIRONMENT "VEROSCRIPT_CSHARP_MOBILE_DOTNET=${_VeroScriptCSharp_dotnet}"
			LABELS "VeroScript;CSharp;Mobile;Package"
			RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 600)
	endif()
endif()

include(GNUInstallDirs)
foreach(_VeroScriptCSharp_tfm IN ITEMS net8.0 net9.0)
	install(FILES
		"${CMAKE_CURRENT_SOURCE_DIR}/bin/${_VeroScriptCSharp_configuration}/${_VeroScriptCSharp_tfm}/VeroScriptCSharp.dll"
		"${CMAKE_CURRENT_SOURCE_DIR}/bin/${_VeroScriptCSharp_configuration}/${_VeroScriptCSharp_tfm}/VeroScriptCSharp.xml"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/dotnet/${_VeroScriptCSharp_tfm}"
		COMPONENT VeroScriptCSharp
		OPTIONAL)
endforeach()
install(DIRECTORY "${CMAKE_BINARY_DIR}/nuget/"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/VeroScript/nuget"
	COMPONENT VeroScriptCSharp
	OPTIONAL
	FILES_MATCHING
		PATTERN "VeroScript.CSharp.*.nupkg"
		PATTERN "VeroScript.CSharp.*.snupkg")

if(BUILD_TESTING AND TARGET VeroScriptAsmJitBackend)
	set_property(TEST VeroScriptCSharpTests-net8.0 VeroScriptCSharpTests-net9.0 VeroScriptCSharpPackageTests APPEND PROPERTY ENVIRONMENT
		"VEROSCRIPT_TEST_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>")
endif()
