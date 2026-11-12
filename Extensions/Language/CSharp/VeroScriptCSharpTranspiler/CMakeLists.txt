# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

unset(_VeroScriptCSharpTranspiler_dotnet)
if(VS_VEROSCRIPT_CSHARP_DOTNET)
	set(_VeroScriptCSharpTranspiler_dotnet "${VS_VEROSCRIPT_CSHARP_DOTNET}")
else()
	find_program(_VeroScriptCSharpTranspiler_dotnet NAMES dotnet NO_CACHE)
endif()
if(NOT _VeroScriptCSharpTranspiler_dotnet)
	message(STATUS "VeroScriptCSharpTranspiler: skipped (.NET SDK is required)")
	return()
endif()

execute_process(COMMAND "${_VeroScriptCSharpTranspiler_dotnet}" --version
	OUTPUT_VARIABLE _VeroScriptCSharpTranspiler_sdk_version OUTPUT_STRIP_TRAILING_WHITESPACE
	RESULT_VARIABLE _VeroScriptCSharpTranspiler_sdk_result)
if(NOT _VeroScriptCSharpTranspiler_sdk_result EQUAL 0
		OR _VeroScriptCSharpTranspiler_sdk_version VERSION_LESS 9.0)
	message(STATUS "VeroScriptCSharpTranspiler: skipped (.NET SDK 9.0 or newer is required)")
	return()
endif()

if(CMAKE_CONFIGURATION_TYPES)
	set(_VeroScriptCSharpTranspiler_configuration "$<IF:$<CONFIG:Debug>,Debug,Release>")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(_VeroScriptCSharpTranspiler_configuration Debug)
else()
	set(_VeroScriptCSharpTranspiler_configuration Release)
endif()
if(DEFINED VS_VEROSCRIPT_CSHARP_VERSION)
	set(_VeroScriptCSharpTranspiler_package_version "${VS_VEROSCRIPT_CSHARP_VERSION}")
else()
	set(_VeroScriptCSharpTranspiler_package_version "0.1.1")
endif()

add_custom_target(VeroScriptCSharpTranspiler
	COMMAND "${_VeroScriptCSharpTranspiler_dotnet}" build
		"${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharpTranspiler.csproj"
		--configuration "${_VeroScriptCSharpTranspiler_configuration}"
		-p:Version=${_VeroScriptCSharpTranspiler_package_version} --nologo
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Building the VeroScript C# transpiler")
add_dependencies(VeroScriptCSharpTranspiler VeroScriptCSharpFrontend)
vs_set_solution_folder(VeroScriptCSharpTranspiler "Libraries/CSharpTools")

add_custom_target(VeroScriptCSharpTranspilerCli
	COMMAND "${_VeroScriptCSharpTranspiler_dotnet}" build
		"${CMAKE_CURRENT_SOURCE_DIR}/Cli/VeroScriptCSharpTranspiler.Cli.csproj"
		--configuration "${_VeroScriptCSharpTranspiler_configuration}"
		-p:Version=${_VeroScriptCSharpTranspiler_package_version} --nologo
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Building the VeroScript C# transpiler CLI")
add_dependencies(VeroScriptCSharpTranspilerCli VeroScriptCSharpTranspiler)
vs_set_solution_folder(VeroScriptCSharpTranspilerCli "Applications/CSharpTools")

add_custom_target(VeroScriptCSharpTranspilerPackage
	COMMAND "${_VeroScriptCSharpTranspiler_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharpTranspiler.csproj"
		--configuration "${_VeroScriptCSharpTranspiler_configuration}" --no-build
		--output "${CMAKE_BINARY_DIR}/nuget"
		-p:Version=${_VeroScriptCSharpTranspiler_package_version}
		-p:PackageVersion=${_VeroScriptCSharpTranspiler_package_version} --nologo
	COMMAND "${_VeroScriptCSharpTranspiler_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/Cli/VeroScriptCSharpTranspiler.Cli.csproj"
		--configuration "${_VeroScriptCSharpTranspiler_configuration}" --no-build
		--output "${CMAKE_BINARY_DIR}/nuget"
		-p:Version=${_VeroScriptCSharpTranspiler_package_version}
		-p:PackageVersion=${_VeroScriptCSharpTranspiler_package_version} --nologo
	DEPENDS VeroScriptCSharpTranspilerCli WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
vs_set_solution_folder(VeroScriptCSharpTranspilerPackage "Packaging")

if(BUILD_TESTING)
	foreach(_VeroScriptCSharpTranspiler_tfm IN ITEMS net8.0 net9.0)
		add_test(NAME "VeroScriptCSharpTranspilerTests-${_VeroScriptCSharpTranspiler_tfm}"
			COMMAND "${_VeroScriptCSharpTranspiler_dotnet}" run
				--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VeroScriptCSharpTranspiler.Tests.csproj"
				--configuration "${_VeroScriptCSharpTranspiler_configuration}"
				--framework "${_VeroScriptCSharpTranspiler_tfm}"
				-- "$<TARGET_FILE:VeroScriptCli>")
		set_tests_properties("VeroScriptCSharpTranspilerTests-${_VeroScriptCSharpTranspiler_tfm}" PROPERTIES
			LABELS "VeroScript;CSharp;Transpiler" RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 180)
	endforeach()
	add_test(NAME VeroScriptCSharpTranspilerCliVersion
		COMMAND "${_VeroScriptCSharpTranspiler_dotnet}" run
			--project "${CMAKE_CURRENT_SOURCE_DIR}/Cli/VeroScriptCSharpTranspiler.Cli.csproj"
			--configuration "${_VeroScriptCSharpTranspiler_configuration}"
			--framework net9.0 -- --version)
	set_tests_properties(VeroScriptCSharpTranspilerCliVersion PROPERTIES
		LABELS "VeroScript;CSharp;Transpiler;Cli" RESOURCE_LOCK VeroScriptCSharpDotNet
		PASS_REGULAR_EXPRESSION
			"VeroScriptCSharpTranspiler ${_VeroScriptCSharpTranspiler_package_version}"
		TIMEOUT 120)
endif()

include(GNUInstallDirs)
foreach(_VeroScriptCSharpTranspiler_tfm IN ITEMS net8.0 net9.0)
	install(DIRECTORY
		"${CMAKE_CURRENT_SOURCE_DIR}/bin/${_VeroScriptCSharpTranspiler_configuration}/${_VeroScriptCSharpTranspiler_tfm}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/dotnet/${_VeroScriptCSharpTranspiler_tfm}/transpiler"
		COMPONENT VeroScriptCSharpTools OPTIONAL FILES_MATCHING PATTERN "*.dll" PATTERN "*.xml" PATTERN "*.txt" PATTERN "*.rtf")
endforeach()
# Shared checkouts can retain the other OS's .NET apphost after a rebuild.
set(_VeroScriptCSharpTranspiler_foreign_apphost "VeroScriptCSharpTranspiler.Cli.exe")
if(WIN32)
	set(_VeroScriptCSharpTranspiler_foreign_apphost "VeroScriptCSharpTranspiler.Cli")
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Cli/bin/${_VeroScriptCSharpTranspiler_configuration}/net9.0/"
	DESTINATION "${CMAKE_INSTALL_BINDIR}/dotnet-tools/veroscript-csharp-transpiler"
	COMPONENT VeroScriptCSharpTools OPTIONAL USE_SOURCE_PERMISSIONS
	PATTERN "publish" EXCLUDE PATTERN "${_VeroScriptCSharpTranspiler_foreign_apphost}" EXCLUDE)
