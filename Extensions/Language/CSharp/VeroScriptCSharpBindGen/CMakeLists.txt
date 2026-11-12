# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

unset(_VeroScriptCSharpBindGen_dotnet)
if(VS_VEROSCRIPT_CSHARP_DOTNET)
	set(_VeroScriptCSharpBindGen_dotnet "${VS_VEROSCRIPT_CSHARP_DOTNET}")
else()
	find_program(_VeroScriptCSharpBindGen_dotnet NAMES dotnet NO_CACHE)
endif()
if(NOT _VeroScriptCSharpBindGen_dotnet)
	message(STATUS "VeroScriptCSharpBindGen: skipped (.NET SDK is required)")
	return()
endif()
execute_process(COMMAND "${_VeroScriptCSharpBindGen_dotnet}" --version
	OUTPUT_VARIABLE _VeroScriptCSharpBindGen_sdk_version OUTPUT_STRIP_TRAILING_WHITESPACE
	RESULT_VARIABLE _VeroScriptCSharpBindGen_sdk_result)
if(NOT _VeroScriptCSharpBindGen_sdk_result EQUAL 0 OR _VeroScriptCSharpBindGen_sdk_version VERSION_LESS 9.0)
	message(STATUS "VeroScriptCSharpBindGen: skipped (.NET SDK 9.0 or newer is required)")
	return()
endif()
if(CMAKE_CONFIGURATION_TYPES)
	set(_VeroScriptCSharpBindGen_configuration "$<IF:$<CONFIG:Debug>,Debug,Release>")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(_VeroScriptCSharpBindGen_configuration Debug)
else()
	set(_VeroScriptCSharpBindGen_configuration Release)
endif()
if(DEFINED VS_VEROSCRIPT_CSHARP_VERSION)
	set(_VeroScriptCSharpBindGen_package_version "${VS_VEROSCRIPT_CSHARP_VERSION}")
else()
	set(_VeroScriptCSharpBindGen_package_version "0.1.1")
endif()
add_custom_target(VeroScriptCSharpBindGen
	COMMAND "${_VeroScriptCSharpBindGen_dotnet}" build "${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharpBindGen.csproj"
		--configuration "${_VeroScriptCSharpBindGen_configuration}" --nologo
		-p:Version=${_VeroScriptCSharpBindGen_package_version}
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" COMMENT "Building the VeroScript C# binding generator")
add_dependencies(VeroScriptCSharpBindGen VeroScriptCSharpFrontend)
vs_set_solution_folder(VeroScriptCSharpBindGen "Libraries/CSharpTools")
add_custom_target(VeroScriptCSharpBindGenCli
	COMMAND "${_VeroScriptCSharpBindGen_dotnet}" build
		"${CMAKE_CURRENT_SOURCE_DIR}/Cli/VeroScriptCSharpBindGen.Cli.csproj"
		--configuration "${_VeroScriptCSharpBindGen_configuration}" --nologo
		-p:Version=${_VeroScriptCSharpBindGen_package_version}
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" COMMENT "Building the VeroScript C# binding generator CLI")
add_dependencies(VeroScriptCSharpBindGenCli VeroScriptCSharpBindGen)
vs_set_solution_folder(VeroScriptCSharpBindGenCli "Applications/CSharpTools")
add_custom_target(VeroScriptCSharpToolsPackage
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/nuget"
	COMMAND "${_VeroScriptCSharpBindGen_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/../VeroScriptCSharpFrontend/VeroScriptCSharpFrontend.csproj"
		--configuration "${_VeroScriptCSharpBindGen_configuration}" --no-build
		--output "${CMAKE_BINARY_DIR}/nuget" -p:Version=${_VeroScriptCSharpBindGen_package_version}
		-p:PackageVersion=${_VeroScriptCSharpBindGen_package_version} --nologo
	COMMAND "${_VeroScriptCSharpBindGen_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/VeroScriptCSharpBindGen.csproj"
		--configuration "${_VeroScriptCSharpBindGen_configuration}" --no-build
		--output "${CMAKE_BINARY_DIR}/nuget" -p:Version=${_VeroScriptCSharpBindGen_package_version}
		-p:PackageVersion=${_VeroScriptCSharpBindGen_package_version} --nologo
	COMMAND "${_VeroScriptCSharpBindGen_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/Cli/VeroScriptCSharpBindGen.Cli.csproj"
		--configuration "${_VeroScriptCSharpBindGen_configuration}" --no-build
		--output "${CMAKE_BINARY_DIR}/nuget" -p:Version=${_VeroScriptCSharpBindGen_package_version}
		-p:PackageVersion=${_VeroScriptCSharpBindGen_package_version} --nologo
	COMMAND "${_VeroScriptCSharpBindGen_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/../VeroScriptCSharpTranspiler/VeroScriptCSharpTranspiler.csproj"
		--configuration "${_VeroScriptCSharpBindGen_configuration}" --no-build
		--output "${CMAKE_BINARY_DIR}/nuget" -p:Version=${_VeroScriptCSharpBindGen_package_version}
		-p:PackageVersion=${_VeroScriptCSharpBindGen_package_version} --nologo
	COMMAND "${_VeroScriptCSharpBindGen_dotnet}" pack
		"${CMAKE_CURRENT_SOURCE_DIR}/../VeroScriptCSharpTranspiler/Cli/VeroScriptCSharpTranspiler.Cli.csproj"
		--configuration "${_VeroScriptCSharpBindGen_configuration}" --no-build
		--output "${CMAKE_BINARY_DIR}/nuget" -p:Version=${_VeroScriptCSharpBindGen_package_version}
		-p:PackageVersion=${_VeroScriptCSharpBindGen_package_version} --nologo
	DEPENDS VeroScriptCSharpBindGenCli VeroScriptCSharpTranspilerCli
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Packing the VeroScript C# build-time tools")
vs_set_solution_folder(VeroScriptCSharpToolsPackage "Packaging")
if(BUILD_TESTING)
	set(_VeroScriptCSharpBindGen_run_arch_args "")
	if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
		list(APPEND _VeroScriptCSharpBindGen_run_arch_args
			--arch x86 --self-contained true)
	endif()
	foreach(_VeroScriptCSharpBindGen_tfm IN ITEMS net8.0 net9.0)
		add_test(NAME "VeroScriptCSharpBindGenTests-${_VeroScriptCSharpBindGen_tfm}"
			COMMAND "${_VeroScriptCSharpBindGen_dotnet}" run
				--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VeroScriptCSharpBindGen.Tests.csproj"
				--configuration "${_VeroScriptCSharpBindGen_configuration}" --framework "${_VeroScriptCSharpBindGen_tfm}")
		set_tests_properties("VeroScriptCSharpBindGenTests-${_VeroScriptCSharpBindGen_tfm}" PROPERTIES
			LABELS "VeroScript;CSharp;BindGen" RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 120)
	endforeach()
	set(_VeroScriptCSharpBindGen_contract_catalog
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptCSharpBindGen.contract.json")
	add_test(NAME VeroScriptCSharpBindGenGenerateContractCatalog
		COMMAND "${_VeroScriptCSharpBindGen_dotnet}" run
			--project "${CMAKE_CURRENT_SOURCE_DIR}/Cli/VeroScriptCSharpBindGen.Cli.csproj"
			--configuration "${_VeroScriptCSharpBindGen_configuration}" --framework net9.0
			-- "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Examples/BindingInput.cs"
			--catalog "${_VeroScriptCSharpBindGen_contract_catalog}"
			--registration "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptCSharpBindGen.contract.g.cs")
	set_tests_properties(VeroScriptCSharpBindGenGenerateContractCatalog PROPERTIES
		LABELS "VeroScript;CSharp;BindGen;Contract" RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 120)
	add_test(NAME VeroScriptCSharpBindGenGeneratedRegistrationCompileTests
		COMMAND "${_VeroScriptCSharpBindGen_dotnet}" build
			"${CMAKE_CURRENT_SOURCE_DIR}/Tests/GeneratedRegistration/VeroScriptCSharpBindGen.Generated.Tests.csproj"
			--configuration "${_VeroScriptCSharpBindGen_configuration}" --nologo
			"-p:GeneratedRegistrationSource=${CMAKE_CURRENT_BINARY_DIR}/VeroScriptCSharpBindGen.contract.g.cs")
	set_tests_properties(VeroScriptCSharpBindGenGeneratedRegistrationCompileTests PROPERTIES
		DEPENDS VeroScriptCSharpBindGenGenerateContractCatalog
		LABELS "VeroScript;CSharp;BindGen;Generated" RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 120)
	add_test(NAME VeroScriptCSharpBindGenGeneratedRegistrationRuntimeTests
		COMMAND "${_VeroScriptCSharpBindGen_dotnet}" run
			--project "${CMAKE_CURRENT_SOURCE_DIR}/Tests/GeneratedRegistration/VeroScriptCSharpBindGen.Generated.Tests.csproj"
			--configuration "${_VeroScriptCSharpBindGen_configuration}" --framework net9.0
			${_VeroScriptCSharpBindGen_run_arch_args}
			"-p:GeneratedRegistrationSource=${CMAKE_CURRENT_BINARY_DIR}/VeroScriptCSharpBindGen.contract.g.cs")
	set_tests_properties(VeroScriptCSharpBindGenGeneratedRegistrationRuntimeTests PROPERTIES
		DEPENDS VeroScriptCSharpBindGenGeneratedRegistrationCompileTests
		ENVIRONMENT "VEROSCRIPT_INTEROP_LIBRARY=$<TARGET_FILE:VeroScriptInterop>"
		WORKING_DIRECTORY "$<TARGET_FILE_DIR:VeroScriptInterop>"
		LABELS "VeroScript;CSharp;BindGen;Generated;Runtime"
		RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 120)
	add_executable(VeroScriptCSharpBindGenCatalogImportTests Tests/CatalogImportTests.cpp)
	target_link_libraries(VeroScriptCSharpBindGenCatalogImportTests PRIVATE VeroScript)
	vs_set_solution_folder(VeroScriptCSharpBindGenCatalogImportTests "Tests/CSharpTools")
	add_test(NAME VeroScriptCSharpBindGenCatalogImportTests
		COMMAND VeroScriptCSharpBindGenCatalogImportTests "${_VeroScriptCSharpBindGen_contract_catalog}")
	set_tests_properties(VeroScriptCSharpBindGenCatalogImportTests PROPERTIES
		DEPENDS VeroScriptCSharpBindGenGenerateContractCatalog
		LABELS "VeroScript;CSharp;BindGen;Contract" TIMEOUT 30)
	add_test(NAME VeroScriptCSharpToolsPackageTests
		COMMAND "${CMAKE_COMMAND}"
			-DVSC_DOTNET:FILEPATH=${_VeroScriptCSharpBindGen_dotnet}
			-DVSC_ROOT:PATH=${CMAKE_CURRENT_SOURCE_DIR}/..
			-DVSC_VERSION:STRING=${_VeroScriptCSharpBindGen_package_version}
			-DVSC_WORK_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/PackageTest
			-DVSC_EXECUTABLE:FILEPATH=$<TARGET_FILE:VeroScriptCli>
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyPackages.cmake")
	set_tests_properties(VeroScriptCSharpToolsPackageTests PROPERTIES
		LABELS "VeroScript;CSharp;BindGen;Package" RUN_SERIAL TRUE
		RESOURCE_LOCK VeroScriptCSharpDotNet TIMEOUT 240)
endif()
include(GNUInstallDirs)
foreach(_VeroScriptCSharpBindGen_tfm IN ITEMS net8.0 net9.0)
	install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/${_VeroScriptCSharpBindGen_configuration}/${_VeroScriptCSharpBindGen_tfm}/"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/dotnet/${_VeroScriptCSharpBindGen_tfm}/bindgen"
		COMPONENT VeroScriptCSharpTools OPTIONAL FILES_MATCHING PATTERN "*.dll" PATTERN "*.xml" PATTERN "*.txt" PATTERN "*.rtf")
endforeach()
# Shared checkouts can retain the other OS's .NET apphost after a rebuild.
set(_VeroScriptCSharpBindGen_foreign_apphost "VeroScriptCSharpBindGen.Cli.exe")
if(WIN32)
	set(_VeroScriptCSharpBindGen_foreign_apphost "VeroScriptCSharpBindGen.Cli")
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Cli/bin/${_VeroScriptCSharpBindGen_configuration}/net9.0/"
	DESTINATION "${CMAKE_INSTALL_BINDIR}/dotnet-tools/veroscript-csharp-bindgen"
	COMPONENT VeroScriptCSharpTools OPTIONAL USE_SOURCE_PERMISSIONS
	PATTERN "publish" EXCLUDE PATTERN "${_VeroScriptCSharpBindGen_foreign_apphost}" EXCLUDE)
install(DIRECTORY "${CMAKE_BINARY_DIR}/nuget/"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/VeroScript/nuget"
	COMPONENT VeroScriptCSharpTools OPTIONAL FILES_MATCHING
	PATTERN "VeroScript.CSharpFrontend.*.nupkg"
	PATTERN "VeroScript.CSharpFrontend.*.snupkg"
	PATTERN "VeroScript.CSharpBindGen.*.nupkg"
	PATTERN "VeroScript.CSharpBindGen.*.snupkg"
	PATTERN "VeroScript.CSharpTranspiler.*.nupkg"
	PATTERN "VeroScript.CSharpTranspiler.*.snupkg")
