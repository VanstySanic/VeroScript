# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScriptTooling)
vs_use_shared_private_headers(VeroScriptTooling)
vs_set_solution_folder(VeroScriptTooling "Libraries")
target_link_libraries(VeroScriptTooling PUBLIC VeroScript)
if(WIN32)
	target_link_libraries(VeroScriptTooling PRIVATE ws2_32)
endif()

# Query internals walk the language AST (Private) and live under Private/Internal.
# Analysis/debug use VeroScriptCompile (language module), not the VeroScriptCpp embed layer.
target_include_directories(VeroScriptTooling PRIVATE
	"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private"
	"${CMAKE_CURRENT_SOURCE_DIR}/Private"
	"${CMAKE_CURRENT_SOURCE_DIR}/Private/Internal")

get_target_property(_VeroScriptTooling_sources VeroScriptTooling SOURCES)
list(FILTER _VeroScriptTooling_sources EXCLUDE REGEX "/Tests/")
set_target_properties(VeroScriptTooling PROPERTIES SOURCES "${_VeroScriptTooling_sources}")

# The VeroScript dependency deploys locale and stdlib assets for standalone
# Tooling, tests, DAP, LSP, and IDE hosts.

if(BUILD_TESTING)
	add_executable(VeroScriptToolingJsonTests Private/Tests/VeroScriptToolingJsonTests.cpp)
	target_include_directories(VeroScriptToolingJsonTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private/Internal")
	target_link_libraries(VeroScriptToolingJsonTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptToolingJsonTests)
	vs_set_solution_folder(VeroScriptToolingJsonTests "Tests")
	add_test(NAME VeroScriptToolingJsonTests COMMAND VeroScriptToolingJsonTests)

	add_executable(VeroScriptModuleEnvironmentToolingTests Private/Tests/VeroScriptModuleEnvironmentToolingTests.cpp)
	target_link_libraries(VeroScriptModuleEnvironmentToolingTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptModuleEnvironmentToolingTests)
	vs_set_solution_folder(VeroScriptModuleEnvironmentToolingTests "Tests")
	add_test(NAME VeroScriptModuleEnvironmentToolingTests COMMAND VeroScriptModuleEnvironmentToolingTests)
	set_tests_properties(VeroScriptModuleEnvironmentToolingTests PROPERTIES TIMEOUT 60)
	if(TARGET VeroScriptCpp AND TARGET VeroScriptInterop AND TARGET VeroScriptCppAot
		AND TARGET VeroScriptCSharpAot AND TARGET VeroScriptPythonAot)
		add_executable(VeroScriptScriptExtensionsIntegrationTests Private/Tests/VeroScriptScriptExtensionsIntegrationTests.cpp)
		target_link_libraries(VeroScriptScriptExtensionsIntegrationTests PRIVATE VeroScriptTooling VeroScriptCpp VeroScriptInterop VeroScriptCppAot VeroScriptCSharpAot VeroScriptPythonAot)
		vs_apply_cxx_defaults(VeroScriptScriptExtensionsIntegrationTests)
		vs_set_solution_folder(VeroScriptScriptExtensionsIntegrationTests "Tests")
		add_test(NAME VeroScriptScriptExtensionsIntegrationTests COMMAND VeroScriptScriptExtensionsIntegrationTests)
		set_tests_properties(VeroScriptScriptExtensionsIntegrationTests PROPERTIES TIMEOUT 60)
	endif()
	add_executable(VeroScriptCompileProcessingToolingTests Private/Tests/VeroScriptCompileProcessingToolingTests.cpp)
	target_link_libraries(VeroScriptCompileProcessingToolingTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptCompileProcessingToolingTests)
	vs_set_solution_folder(VeroScriptCompileProcessingToolingTests "Tests")
	add_test(NAME VeroScriptCompileProcessingToolingTests COMMAND VeroScriptCompileProcessingToolingTests)
	set_tests_properties(VeroScriptCompileProcessingToolingTests PROPERTIES TIMEOUT 60)

	add_executable(VeroScriptTypeHierarchyTests Private/Tests/VeroScriptTypeHierarchyTests.cpp)
	target_link_libraries(VeroScriptTypeHierarchyTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptTypeHierarchyTests)
	vs_set_solution_folder(VeroScriptTypeHierarchyTests "Tests")
	add_test(NAME VeroScriptTypeHierarchyTests COMMAND VeroScriptTypeHierarchyTests)
	set_tests_properties(VeroScriptTypeHierarchyTests PROPERTIES TIMEOUT 60)

	add_executable(VeroScriptQueryIndexTests Private/Tests/VeroScriptQueryIndexTests.cpp)
	target_link_libraries(VeroScriptQueryIndexTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptQueryIndexTests)
	vs_set_solution_folder(VeroScriptQueryIndexTests "Tests")
	add_test(NAME VeroScriptQueryIndexTests COMMAND VeroScriptQueryIndexTests)
	set_tests_properties(VeroScriptQueryIndexTests PROPERTIES TIMEOUT 60)

	add_executable(VeroScriptAnalyzerTests Private/Tests/VeroScriptAnalyzerTests.cpp)
	target_link_libraries(VeroScriptAnalyzerTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptAnalyzerTests)
	vs_set_solution_folder(VeroScriptAnalyzerTests "Tests")
	add_test(NAME VeroScriptAnalyzerTests COMMAND VeroScriptAnalyzerTests)

	add_executable(VeroScriptOutlineTests Private/Tests/VeroScriptOutlineTests.cpp)
	target_link_libraries(VeroScriptOutlineTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptOutlineTests)
	vs_set_solution_folder(VeroScriptOutlineTests "Tests")
	add_test(NAME VeroScriptOutlineTests COMMAND VeroScriptOutlineTests)

	add_executable(VeroScriptNavigationTests Private/Tests/VeroScriptNavigationTests.cpp)
	target_link_libraries(VeroScriptNavigationTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptNavigationTests)
	vs_set_solution_folder(VeroScriptNavigationTests "Tests")
	add_test(NAME VeroScriptNavigationTests COMMAND VeroScriptNavigationTests)

	add_executable(VeroScriptDebugSessionTests Private/Tests/VeroScriptDebugSessionTests.cpp)
	target_link_libraries(VeroScriptDebugSessionTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptDebugSessionTests)
	vs_set_solution_folder(VeroScriptDebugSessionTests "Tests")
	add_test(NAME VeroScriptDebugSessionTests COMMAND VeroScriptDebugSessionTests)

	add_executable(VeroScriptCompleteTests Private/Tests/VeroScriptCompleteTests.cpp)
	target_link_libraries(VeroScriptCompleteTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptCompleteTests)
	vs_set_solution_folder(VeroScriptCompleteTests "Tests")
	add_test(NAME VeroScriptCompleteTests COMMAND VeroScriptCompleteTests)

	add_executable(VeroScriptDapAdapterTests Private/Tests/VeroScriptDapAdapterTests.cpp)
	target_link_libraries(VeroScriptDapAdapterTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptDapAdapterTests)
	vs_set_solution_folder(VeroScriptDapAdapterTests "Tests")
	add_test(NAME VeroScriptDapAdapterTests COMMAND VeroScriptDapAdapterTests)

	add_executable(VeroScriptDapStdioTests Private/Tests/VeroScriptDapStdioTests.cpp)
	target_link_libraries(VeroScriptDapStdioTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptDapStdioTests)
	vs_set_solution_folder(VeroScriptDapStdioTests "Tests")
	add_test(NAME VeroScriptDapStdioTests COMMAND VeroScriptDapStdioTests)

	add_executable(VeroScriptDapTcpTests Private/Tests/VeroScriptDapTcpTests.cpp)
	target_link_libraries(VeroScriptDapTcpTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptDapTcpTests)
	vs_set_solution_folder(VeroScriptDapTcpTests "Tests")
	add_test(NAME VeroScriptDapTcpTests COMMAND VeroScriptDapTcpTests)

	add_executable(VeroScriptLspStdioTests Private/Tests/VeroScriptLspStdioTests.cpp)
	target_link_libraries(VeroScriptLspStdioTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptLspStdioTests)
	vs_set_solution_folder(VeroScriptLspStdioTests "Tests")
	add_test(NAME VeroScriptLspStdioTests COMMAND VeroScriptLspStdioTests)

	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME VeroScriptLambdaInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptLambdaInteractionTests.py"
				$<TARGET_FILE:VeroScriptLsp> $<TARGET_FILE:VeroScriptDap>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/lambda-interactions.json")
		set_tests_properties(VeroScriptLambdaInteractionTest PROPERTIES TIMEOUT 60)
		add_test(NAME VeroScriptDapInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptDapInteractionTests.py"
				$<TARGET_FILE:VeroScriptDap>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/dap-interactions.json")
		set_tests_properties(VeroScriptDapInteractionTest PROPERTIES TIMEOUT 60)
		add_test(NAME VeroScriptLspInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptLspInteractionTests.py"
				$<TARGET_FILE:VeroScriptLsp>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/lsp-interactions.json")
		# The functional corpus contains hundreds of protocol requests. Allow
		# startup and analysis on loaded hosts; responsiveness has separate tests.
		set_tests_properties(VeroScriptLspInteractionTest PROPERTIES TIMEOUT 120)
		add_test(NAME VeroScriptLspContextInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptLspContextInteractionTests.py"
				$<TARGET_FILE:VeroScriptLsp>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/lsp-context-interactions.json")
		set_tests_properties(VeroScriptLspContextInteractionTest PROPERTIES TIMEOUT 60)
		add_test(NAME VeroScriptLspResponsivenessTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptLspResponsivenessTest.py"
				$<TARGET_FILE:VeroScriptLsp>)
		# Request deadlines are measured without competing functional LSP servers.
		set_tests_properties(VeroScriptLspResponsivenessTest PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
		add_executable(VeroScriptAnnotationToolingFixture Private/Tests/VeroScriptAnnotationToolingFixture.cpp)
		add_executable(VeroScriptCompileProcessingToolingFixture Private/Tests/VeroScriptCompileProcessingToolingFixture.cpp)
		target_link_libraries(VeroScriptCompileProcessingToolingFixture PRIVATE VeroScriptTooling)
		vs_apply_cxx_defaults(VeroScriptCompileProcessingToolingFixture)
		vs_set_solution_folder(VeroScriptCompileProcessingToolingFixture "Tests")
		add_test(NAME VeroScriptCompileProcessingInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptCompileProcessingInteractionTests.py"
				$<TARGET_FILE:VeroScriptCompileProcessingToolingFixture>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/compile-processing-interactions.json")
		set_tests_properties(VeroScriptCompileProcessingInteractionTest PROPERTIES TIMEOUT 60)
		add_test(NAME VeroScriptScriptExtensionsInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptScriptExtensionsInteractionTests.py"
				$<TARGET_FILE:VeroScriptLsp> $<TARGET_FILE:VeroScriptDap>
				$<TARGET_FILE:VeroScriptCompileProcessingToolingFixture>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/script-extensions-interactions.json")
		set_tests_properties(VeroScriptScriptExtensionsInteractionTest PROPERTIES TIMEOUT 90)
		add_test(NAME VeroScriptProtocolReviewTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptProtocolReviewTests.py"
				$<TARGET_FILE:VeroScriptLsp> $<TARGET_FILE:VeroScriptDap>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/protocol-review.json")
		set_tests_properties(VeroScriptProtocolReviewTest PROPERTIES TIMEOUT 90)
		add_test(NAME VeroScriptModuleEnvironmentInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptModuleEnvironmentInteractionTests.py"
				$<TARGET_FILE:VeroScriptModuleEnvironmentToolingTests>
				$<TARGET_FILE:VeroScriptLsp>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/module-environment-interactions.json")
		set_tests_properties(VeroScriptModuleEnvironmentInteractionTest PROPERTIES TIMEOUT 60)
		target_link_libraries(VeroScriptAnnotationToolingFixture PRIVATE VeroScriptTooling)
		vs_apply_cxx_defaults(VeroScriptAnnotationToolingFixture)
		vs_set_solution_folder(VeroScriptAnnotationToolingFixture "Tests")
		add_test(NAME VeroScriptAnnotationInteractionTest
			COMMAND "${Python3_EXECUTABLE}" -B
				"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptAnnotationInteractionTests.py"
				$<TARGET_FILE:VeroScriptAnnotationToolingFixture>
				"${CMAKE_CURRENT_BINARY_DIR}/evidence/$<CONFIG>/annotation-interactions.json")
		set_tests_properties(VeroScriptAnnotationInteractionTest PROPERTIES TIMEOUT 60)
	endif()

	add_executable(VeroScriptHoverTests Private/Tests/VeroScriptHoverTests.cpp)
	target_link_libraries(VeroScriptHoverTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptHoverTests)
	vs_set_solution_folder(VeroScriptHoverTests "Tests")
	add_test(NAME VeroScriptHoverTests COMMAND VeroScriptHoverTests)

	add_executable(VeroScriptCallHelpTests Private/Tests/VeroScriptCallHelpTests.cpp)
	target_link_libraries(VeroScriptCallHelpTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptCallHelpTests)
	vs_set_solution_folder(VeroScriptCallHelpTests "Tests")
	add_test(NAME VeroScriptCallHelpTests COMMAND VeroScriptCallHelpTests)

	add_executable(VeroScriptFindReferencesTests Private/Tests/VeroScriptFindReferencesTests.cpp)
	target_link_libraries(VeroScriptFindReferencesTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptFindReferencesTests)
	vs_set_solution_folder(VeroScriptFindReferencesTests "Tests")
	add_test(NAME VeroScriptFindReferencesTests COMMAND VeroScriptFindReferencesTests)

	add_executable(VeroScriptDocumentHighlightTests Private/Tests/VeroScriptDocumentHighlightTests.cpp)
	target_link_libraries(VeroScriptDocumentHighlightTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptDocumentHighlightTests)
	vs_set_solution_folder(VeroScriptDocumentHighlightTests "Tests")
	add_test(NAME VeroScriptDocumentHighlightTests COMMAND VeroScriptDocumentHighlightTests)

	add_executable(VeroScriptWorkspaceSymbolsTests Private/Tests/VeroScriptWorkspaceSymbolsTests.cpp)
	target_link_libraries(VeroScriptWorkspaceSymbolsTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptWorkspaceSymbolsTests)
	vs_set_solution_folder(VeroScriptWorkspaceSymbolsTests "Tests")
	add_test(NAME VeroScriptWorkspaceSymbolsTests COMMAND VeroScriptWorkspaceSymbolsTests)

	add_executable(VeroScriptWorkspaceTests Private/Tests/VeroScriptWorkspaceTests.cpp)
	target_link_libraries(VeroScriptWorkspaceTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptWorkspaceTests)
	vs_set_solution_folder(VeroScriptWorkspaceTests "Tests")
	add_test(NAME VeroScriptWorkspaceTests COMMAND VeroScriptWorkspaceTests)

	add_executable(VeroScriptRenameTests Private/Tests/VeroScriptRenameTests.cpp)
	target_link_libraries(VeroScriptRenameTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptRenameTests)
	vs_set_solution_folder(VeroScriptRenameTests "Tests")
	add_test(NAME VeroScriptRenameTests COMMAND VeroScriptRenameTests)

	add_executable(VeroScriptFormatTests Private/Tests/VeroScriptFormatTests.cpp)
	target_link_libraries(VeroScriptFormatTests PRIVATE VeroScriptTooling)
	vs_apply_cxx_defaults(VeroScriptFormatTests)
	vs_set_solution_folder(VeroScriptFormatTests "Tests")
	add_test(NAME VeroScriptFormatTests COMMAND VeroScriptFormatTests)
endif()
