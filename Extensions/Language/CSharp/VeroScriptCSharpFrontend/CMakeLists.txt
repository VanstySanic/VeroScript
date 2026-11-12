# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

unset(_VeroScriptCSharpFrontend_dotnet)
if(VS_VEROSCRIPT_CSHARP_DOTNET)
	set(_VeroScriptCSharpFrontend_dotnet "${VS_VEROSCRIPT_CSHARP_DOTNET}")
else()
	find_program(_VeroScriptCSharpFrontend_dotnet NAMES dotnet NO_CACHE)
endif()
if(NOT _VeroScriptCSharpFrontend_dotnet)
	message(STATUS "VeroScriptCSharpFrontend: skipped (.NET SDK is required)")
	return()
endif()

execute_process(
	COMMAND "${_VeroScriptCSharpFrontend_dotnet}" --version
	OUTPUT_VARIABLE _VeroScriptCSharpFrontend_sdk_version
	OUTPUT_STRIP_TRAILING_WHITESPACE
	RESULT_VARIABLE _VeroScriptCSharpFrontend_sdk_result)
if(NOT _VeroScriptCSharpFrontend_sdk_result EQUAL 0
		OR _VeroScriptCSharpFrontend_sdk_version VERSION_LESS 9.0)
	message(STATUS "VeroScriptCSharpFrontend: skipped (.NET SDK 9.0 or newer is required)")
	return()
endif()

if(CMAKE_CONFIGURATION_TYPES)
	set(_VeroScriptCSharpFrontend_configuration "$<IF:$<CONFIG:Debug>,Debug,Release>")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(_VeroScriptCSharpFrontend_configuration Debug)
else()
	set(_VeroScriptCSharpFrontend_configuration Release)
endif()
if(DEFINED VS_VEROSCRIPT_CSHARP_VERSION)
	set(_VeroScriptCSharpFrontend_version "${VS_VEROSCRIPT_CSHARP_VERSION}")
else()
	set(_VeroScriptCSharpFrontend_version "0.1.1")
endif()

add_custom_target(VeroScriptCSharpFrontend
	COMMAND "${_VeroScriptCSharpFrontend_dotnet}" build
		"${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharpFrontend.csproj"
		--configuration "${_VeroScriptCSharpFrontend_configuration}"
		-p:Version=${_VeroScriptCSharpFrontend_version}
		--nologo
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Building the VeroScript C# frontend")
vs_set_solution_folder(VeroScriptCSharpFrontend "Libraries/CSharpTools")

if(BUILD_TESTING)
	foreach(_VeroScriptCSharpFrontend_tfm IN ITEMS net8.0 net9.0)
		add_test(NAME "VeroScriptCSharpFrontendTests-${_VeroScriptCSharpFrontend_tfm}"
			COMMAND "${_VeroScriptCSharpFrontend_dotnet}" run
				--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VeroScriptCSharpFrontend.Tests.csproj"
				--configuration "${_VeroScriptCSharpFrontend_configuration}"
				--framework "${_VeroScriptCSharpFrontend_tfm}")
		set_tests_properties("VeroScriptCSharpFrontendTests-${_VeroScriptCSharpFrontend_tfm}" PROPERTIES
			LABELS "VeroScript;CSharp;Frontend"
			RESOURCE_LOCK VeroScriptCSharpDotNet
			TIMEOUT 120)
	endforeach()
endif()

include(GNUInstallDirs)
foreach(_VeroScriptCSharpFrontend_tfm IN ITEMS net8.0 net9.0)
	install(DIRECTORY
		"${CMAKE_CURRENT_SOURCE_DIR}/bin/${_VeroScriptCSharpFrontend_configuration}/${_VeroScriptCSharpFrontend_tfm}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/dotnet/${_VeroScriptCSharpFrontend_tfm}/frontend"
		COMPONENT VeroScriptCSharpTools
		OPTIONAL
		FILES_MATCHING PATTERN "*.dll" PATTERN "*.xml" PATTERN "*.txt" PATTERN "*.rtf")
endforeach()
