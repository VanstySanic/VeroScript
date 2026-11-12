# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScriptCpp)
vs_set_solution_folder(VeroScriptCpp "Libraries")
target_link_libraries(VeroScriptCpp PUBLIC VeroScript)
if(TARGET VeroScriptApplication)
	target_link_libraries(VeroScriptCpp PUBLIC VeroScriptApplication)
	target_compile_definitions(VeroScriptCpp PUBLIC VEROSCRIPT_WITH_APPLICATION=1)
endif()

target_include_directories(VeroScriptCpp PRIVATE
	"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private"
	"${CMAKE_CURRENT_SOURCE_DIR}/Private")

get_target_property(_VeroScriptCpp_sources VeroScriptCpp SOURCES)
list(FILTER _VeroScriptCpp_sources EXCLUDE REGEX "/Tests/")
set_target_properties(VeroScriptCpp PROPERTIES SOURCES "${_VeroScriptCpp_sources}")

# Engine/session tests exercise the public C++ embedding layer after the core target exists.
if(BUILD_TESTING)
	add_executable(VeroScriptBoundedCleanupTests Private/Tests/VeroScriptBoundedCleanupTests.cpp)
	target_link_libraries(VeroScriptBoundedCleanupTests PRIVATE VeroScriptCpp)
	target_include_directories(VeroScriptBoundedCleanupTests PRIVATE "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private")
	vs_apply_cxx_defaults(VeroScriptBoundedCleanupTests)
	vs_set_solution_folder(VeroScriptBoundedCleanupTests "Tests/Core")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptBoundedCleanupTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptBoundedCleanupTests COMMAND VeroScriptBoundedCleanupTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptBoundedCleanupTests COMMAND VeroScriptBoundedCleanupTests)
	endif()
	set_tests_properties(VeroScriptBoundedCleanupTests PROPERTIES TIMEOUT 30 LABELS "Core;Governance")
	add_executable(VeroScriptCppNumericsTests Private/Tests/VeroScriptCppNumericsTests.cpp)
	target_link_libraries(VeroScriptCppNumericsTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppNumericsTests)
	vs_set_solution_folder(VeroScriptCppNumericsTests "Tests/Cpp")
	add_test(NAME VeroScriptCppNumericsTests COMMAND VeroScriptCppNumericsTests)
	add_executable(VeroScriptCppModuleEnvironmentTests Private/Tests/VeroScriptCppModuleEnvironmentTests.cpp)
	target_link_libraries(VeroScriptCppModuleEnvironmentTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppModuleEnvironmentTests)
	vs_set_solution_folder(VeroScriptCppModuleEnvironmentTests "Tests/Cpp")
	add_test(NAME VeroScriptCppModuleEnvironmentTests COMMAND VeroScriptCppModuleEnvironmentTests)
	set_tests_properties(VeroScriptCppModuleEnvironmentTests PROPERTIES TIMEOUT 30)

	add_executable(VeroScriptCppScriptCompileExtensionsTests Private/Tests/VeroScriptCppScriptCompileExtensionsTests.cpp)
	target_link_libraries(VeroScriptCppScriptCompileExtensionsTests PRIVATE VeroScriptCpp)
	target_compile_definitions(VeroScriptCppScriptCompileExtensionsTests PRIVATE
		VEROSCRIPT_CPP_STDLIB_DIR="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptCppScriptCompileExtensionsTests)
	vs_set_solution_folder(VeroScriptCppScriptCompileExtensionsTests "Tests/Cpp")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptCppScriptCompileExtensionsTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptCppScriptCompileExtensionsTests COMMAND VeroScriptCppScriptCompileExtensionsTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptCppScriptCompileExtensionsTests COMMAND VeroScriptCppScriptCompileExtensionsTests)
	endif()
	set_tests_properties(VeroScriptCppScriptCompileExtensionsTests PROPERTIES TIMEOUT 60)

	add_executable(VeroScriptCppCompileProcessingTests Private/Tests/VeroScriptCppCompileProcessingTests.cpp)
	target_link_libraries(VeroScriptCppCompileProcessingTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppCompileProcessingTests)
	vs_set_solution_folder(VeroScriptCppCompileProcessingTests "Tests/Cpp")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptCppCompileProcessingTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptCppCompileProcessingTests COMMAND VeroScriptCppCompileProcessingTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptCppCompileProcessingTests COMMAND VeroScriptCppCompileProcessingTests)
	endif()
	set_tests_properties(VeroScriptCppCompileProcessingTests PROPERTIES TIMEOUT 30)

	add_executable(VeroScriptCppSessionDebugTests
		Private/Tests/VeroScriptCppSessionDebugTests.cpp)
	target_link_libraries(VeroScriptCppSessionDebugTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppSessionDebugTests)
	vs_set_solution_folder(VeroScriptCppSessionDebugTests "Tests/Cpp")
	add_test(NAME VeroScriptCppSessionDebugTests COMMAND VeroScriptCppSessionDebugTests)
	set_tests_properties(VeroScriptCppSessionDebugTests PROPERTIES TIMEOUT 30)

	add_executable(VeroScriptCppPublicHeadersTests
		Private/Tests/VeroScriptCppPublicHeadersTests.cpp
		Private/Tests/VeroScriptCppEngineHeaderTest.cpp
		Private/Tests/VeroScriptCppHostHeaderTest.cpp
		Private/Tests/VeroScriptCppSessionHeaderTest.cpp)
	target_link_libraries(VeroScriptCppPublicHeadersTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppPublicHeadersTests)
	vs_set_solution_folder(VeroScriptCppPublicHeadersTests "Tests/Cpp")
	add_test(NAME VeroScriptCppPublicHeadersTests COMMAND VeroScriptCppPublicHeadersTests)


	add_executable(VeroScriptCppJitContractTests
		Private/Tests/VeroScriptCppJitContractTests.cpp)
	target_link_libraries(VeroScriptCppJitContractTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppJitContractTests)
	vs_set_solution_folder(VeroScriptCppJitContractTests "Tests/Cpp")
	add_test(NAME VeroScriptCppJitContractTests COMMAND VeroScriptCppJitContractTests)

	add_executable(VeroScriptCppJitDiagnosticsTests
		Private/Tests/VeroScriptCppJitDiagnosticsTests.cpp)
	target_link_libraries(VeroScriptCppJitDiagnosticsTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppJitDiagnosticsTests)
	vs_set_solution_folder(VeroScriptCppJitDiagnosticsTests "Tests/Cpp")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptCppJitDiagnosticsTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptCppJitDiagnosticsTests COMMAND VeroScriptCppJitDiagnosticsTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptCppJitDiagnosticsTests COMMAND VeroScriptCppJitDiagnosticsTests)
	endif()
	set_tests_properties(VeroScriptCppJitDiagnosticsTests PROPERTIES TIMEOUT 30)

	add_executable(VeroScriptCppEngineIntegrationTests
		Private/Tests/VeroScriptCppEngineIntegrationTests.cpp)
	target_link_libraries(VeroScriptCppEngineIntegrationTests PRIVATE VeroScriptCpp)
	target_compile_definitions(VeroScriptCppEngineIntegrationTests PRIVATE
		VEROSCRIPT_STDLIB_DIR="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptCppEngineIntegrationTests)
	vs_set_solution_folder(VeroScriptCppEngineIntegrationTests "Tests/Cpp")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptCppEngineIntegrationTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptCppEngineIntegrationTests COMMAND VeroScriptCppEngineIntegrationTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptCppEngineIntegrationTests COMMAND VeroScriptCppEngineIntegrationTests)
	endif()
	set_tests_properties(VeroScriptCppEngineIntegrationTests PROPERTIES TIMEOUT 30)
	add_executable(VeroScriptCppSessionLifetimeTests
		Private/Tests/VeroScriptCppSessionLifetimeTests.cpp)
	target_link_libraries(VeroScriptCppSessionLifetimeTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppSessionLifetimeTests)
	vs_set_solution_folder(VeroScriptCppSessionLifetimeTests "Tests/Cpp")
	add_test(NAME VeroScriptCppSessionLifetimeTests COMMAND VeroScriptCppSessionLifetimeTests)
	set_tests_properties(VeroScriptCppSessionLifetimeTests PROPERTIES TIMEOUT 30)
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(VeroScriptCppEngineIntegrationTests VeroScriptCppSessionLifetimeTests VeroScriptCppScriptCompileExtensionsTests
			PROPERTIES TIMEOUT 120)
	endif()

	add_executable(VeroScriptCppAnnotationTests Private/Tests/VeroScriptCppAnnotationTests.cpp)
	target_link_libraries(VeroScriptCppAnnotationTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppAnnotationTests)
	vs_set_solution_folder(VeroScriptCppAnnotationTests "Tests/Cpp")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptCppAnnotationTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptCppAnnotationTests COMMAND VeroScriptCppAnnotationTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptCppAnnotationTests COMMAND VeroScriptCppAnnotationTests)
	endif()
	set_tests_properties(VeroScriptCppAnnotationTests PROPERTIES TIMEOUT 30)
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		set_tests_properties(VeroScriptCppAnnotationTests PROPERTIES TIMEOUT 120)
	endif()
	add_executable(VeroScriptCppConsoleTests Private/Tests/VeroScriptCppConsoleTests.cpp)
	target_link_libraries(VeroScriptCppConsoleTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppConsoleTests)
	vs_set_solution_folder(VeroScriptCppConsoleTests "Tests/Cpp")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptCppConsoleTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptCppConsoleTests COMMAND VeroScriptCppConsoleTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptCppConsoleTests COMMAND VeroScriptCppConsoleTests)
	endif()
	set_tests_properties(VeroScriptCppConsoleTests PROPERTIES TIMEOUT 30)

	add_executable(VeroScriptCppMemoryIntegrationTests
		Private/Tests/VeroScriptCppMemoryIntegrationTests.cpp)
	target_link_libraries(VeroScriptCppMemoryIntegrationTests PRIVATE VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptCppMemoryIntegrationTests)
	vs_set_solution_folder(VeroScriptCppMemoryIntegrationTests "Tests/Cpp")
	if(MINGW)
		target_link_options(VeroScriptCppMemoryIntegrationTests PRIVATE -municode)
	endif()
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptCppMemoryIntegrationTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptCppMemoryIntegrationTests COMMAND VeroScriptCppMemoryIntegrationTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptCppMemoryIntegrationTests COMMAND VeroScriptCppMemoryIntegrationTests)
	endif()
	set_tests_properties(VeroScriptCppMemoryIntegrationTests PROPERTIES TIMEOUT 30)

	# These integration cases use the session/root API, so keep the optional
	# C++ SDK dependency in this module rather than in the Core-only build.
	add_executable(VeroScriptVariantTests
		"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private/Tests/VeroScriptVariantTests.cpp")
	target_link_libraries(VeroScriptVariantTests PRIVATE VeroScriptCpp)
	target_compile_definitions(VeroScriptVariantTests PRIVATE
		VEROSCRIPT_TEST_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptVariantTests)
	vs_set_solution_folder(VeroScriptVariantTests "Tests")
	add_test(NAME VeroScriptVariantTests COMMAND VeroScriptVariantTests)
	# This suite repeatedly compiles independent programs and imports the stdlib.
	# Cross-filesystem WSL I/O can take over a minute without a runtime regression.
	set_tests_properties(VeroScriptVariantTests PROPERTIES TIMEOUT 180 LABELS "Core")
	add_executable(VeroScriptNativeCompletionTests
		"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private/Tests/VeroScriptNativeCompletionTests.cpp")
	target_link_libraries(VeroScriptNativeCompletionTests PRIVATE VeroScriptCpp)
	target_compile_definitions(VeroScriptNativeCompletionTests PRIVATE
		VEROSCRIPT_TEST_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptNativeCompletionTests)
	vs_set_solution_folder(VeroScriptNativeCompletionTests "Tests")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptNativeCompletionTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptNativeCompletionTests COMMAND VeroScriptNativeCompletionTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptNativeCompletionTests COMMAND VeroScriptNativeCompletionTests)
	endif()
	set_tests_properties(VeroScriptNativeCompletionTests PROPERTIES TIMEOUT 120 LABELS "Core;Native;Completion")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptVariantTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptVariantJitTests COMMAND VeroScriptVariantTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
		# This suite repeats scripts under VM, Mixed and JIT. Allow headroom for
		# stdlib loading on mounted filesystems without changing script limits.
		set_tests_properties(VeroScriptVariantJitTests PROPERTIES TIMEOUT 120 LABELS "Core;Jit")
		if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
			set_tests_properties(VeroScriptVariantJitTests PROPERTIES TIMEOUT 600)
		endif()
	endif()
	foreach(_vs_engine_test IN ITEMS
		VeroScriptVariantTests
		VeroScriptStandardLibraryIntegrationTests
		VeroScriptM1Tests
		VeroScriptM2Tests
		VeroScriptM3Tests
		VeroScriptLambdaTests
		VeroScriptConcurrentTests
		VeroScriptInterfaceTests
		VeroScriptGenericInterfaceTests
		VeroScriptAliasNamespaceTests
		VeroScriptPackageTests
		VeroScriptSessionApiTests)
		if(TARGET ${_vs_engine_test})
			target_link_libraries(${_vs_engine_test} PRIVATE VeroScriptCpp)
		endif()
	endforeach()
	if(TARGET VeroScriptStandardLibraryIntegrationTests AND TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptStandardLibraryIntegrationTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptStandardLibraryJitTests COMMAND VeroScriptStandardLibraryIntegrationTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
		set_tests_properties(VeroScriptStandardLibraryJitTests PROPERTIES TIMEOUT 120 LABELS "Core;Jit")
	endif()
endif()

if(BUILD_TESTING AND TARGET VeroScriptAsmJitBackend)
	add_library(VeroScriptJitIncompatibleModule SHARED Private/Tests/VeroScriptJitIncompatibleModule.cpp)
	target_include_directories(VeroScriptJitIncompatibleModule PRIVATE "$<TARGET_PROPERTY:VeroScript,INTERFACE_INCLUDE_DIRECTORIES>")
	vs_apply_cxx_defaults(VeroScriptJitIncompatibleModule)
	add_library(VeroScriptJitInvalidFactoryModule SHARED Private/Tests/VeroScriptJitIncompatibleModule.cpp)
	target_include_directories(VeroScriptJitInvalidFactoryModule PRIVATE "$<TARGET_PROPERTY:VeroScript,INTERFACE_INCLUDE_DIRECTORIES>")
	target_compile_definitions(VeroScriptJitInvalidFactoryModule PRIVATE VEROSCRIPT_TEST_JIT_INVALID_FACTORY=1)
	set_target_properties(VeroScriptJitInvalidFactoryModule PROPERTIES
		CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_compile_options(VeroScriptJitInvalidFactoryModule PRIVATE -fno-gnu-unique)
	endif()
	vs_apply_cxx_defaults(VeroScriptJitInvalidFactoryModule)
	add_executable(VeroScriptJitLoaderTests Private/Tests/VeroScriptJitLoaderTests.cpp)
	target_link_libraries(VeroScriptJitLoaderTests PRIVATE VeroScriptCpp ${CMAKE_DL_LIBS})
	vs_apply_cxx_defaults(VeroScriptJitLoaderTests)
	if(MINGW)
		target_link_options(VeroScriptJitLoaderTests PRIVATE -municode)
	endif()
	add_dependencies(VeroScriptJitLoaderTests VeroScriptAsmJitBackend VeroScriptJitIncompatibleModule VeroScriptJitInvalidFactoryModule)
	add_test(NAME VeroScriptJitLoaderTests COMMAND VeroScriptJitLoaderTests
		"$<TARGET_FILE:VeroScriptAsmJitBackend>" "$<TARGET_FILE:VeroScriptJitIncompatibleModule>"
		"$<TARGET_FILE:VeroScriptJitInvalidFactoryModule>")
	set_tests_properties(VeroScriptJitLoaderTests PROPERTIES LABELS "VeroScript;Jit;Loader")
endif()
