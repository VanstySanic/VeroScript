# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Repository-integrated embedding example and self-test.
vs_add_module_executable(VeroScriptEmbedExample)
vs_set_solution_folder(VeroScriptEmbedExample "Examples")
target_link_libraries(VeroScriptEmbedExample PRIVATE VeroScriptCpp VeroScriptTooling VeroScriptBytecodeVm)
if(MINGW)
	target_link_options(VeroScriptEmbedExample PRIVATE -municode)
endif()

vs_deploy_runtime_file(
	VeroScriptEmbedExample
	"${CMAKE_CURRENT_SOURCE_DIR}/Assets/embed_demo.vs")

if(BUILD_TESTING)
	add_test(
		NAME VeroScriptEmbedExample
		COMMAND $<TARGET_FILE:VeroScriptEmbedExample> selftest
		WORKING_DIRECTORY $<TARGET_FILE_DIR:VeroScriptEmbedExample>)
	# Both variants export the same catalog filenames in the binary directory.
	set_tests_properties(VeroScriptEmbedExample PROPERTIES
		RESOURCE_LOCK VeroScriptEmbedCatalogFiles
		LABELS "VeroScript" TIMEOUT 30)
	if(TARGET VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptEmbedJitExample
			COMMAND $<TARGET_FILE:VeroScriptEmbedExample> selftest $<TARGET_FILE:VeroScriptAsmJitBackend>)
		set_tests_properties(VeroScriptEmbedJitExample PROPERTIES
			WORKING_DIRECTORY $<TARGET_FILE_DIR:VeroScriptEmbedExample>
			RESOURCE_LOCK VeroScriptEmbedCatalogFiles
			LABELS "VeroScript;Jit" TIMEOUT 30)
	endif()
	find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
	if(Python3_Interpreter_FOUND)
		add_test(NAME VeroScriptEmbedTcpTests
			COMMAND "${Python3_EXECUTABLE}"
				"${CMAKE_CURRENT_SOURCE_DIR}/Tests/VeroScriptEmbedTcpTests.py"
				"$<TARGET_FILE:VeroScriptEmbedExample>")
		set_tests_properties(VeroScriptEmbedTcpTests PROPERTIES LABELS "VeroScript" TIMEOUT 45)
	endif()
endif()
