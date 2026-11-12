# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_collect_module_sources(_VeroScriptCppBindGen_sources)
list(FILTER _VeroScriptCppBindGen_sources EXCLUDE REGEX "/Tests/")
list(FILTER _VeroScriptCppBindGen_sources EXCLUDE REGEX "VeroScriptCppBindGenMain\\.cpp$")

add_library(VeroScriptCppBindGenLib STATIC ${_VeroScriptCppBindGen_sources})
vs_use_shared_private_headers(VeroScriptCppBindGenLib)
vs_add_module_headers(VeroScriptCppBindGenLib)
vs_add_module_include_roots(VeroScriptCppBindGenLib)
vs_apply_cxx_defaults(VeroScriptCppBindGenLib)
vs_set_solution_folder(VeroScriptCppBindGenLib "Libraries/CppTools")
target_compile_features(VeroScriptCppBindGenLib PUBLIC cxx_std_17)
target_link_libraries(VeroScriptCppBindGenLib PUBLIC VeroScriptCppFrontend VeroScript)

add_executable(VeroScriptCppBindGen
	Private/VeroScriptCppBindGenMain.cpp)
target_link_libraries(VeroScriptCppBindGen PRIVATE VeroScriptCppBindGenLib)
target_compile_features(VeroScriptCppBindGen PRIVATE cxx_std_17)
vs_apply_cxx_defaults(VeroScriptCppBindGen)
vs_set_solution_folder(VeroScriptCppBindGen "CLI/CppTools")

if(BUILD_TESTING)
	add_executable(VeroScriptCppBindGenTests
		Private/Tests/VeroScriptCppBindGenTests.cpp)
	target_link_libraries(VeroScriptCppBindGenTests PRIVATE VeroScriptCppBindGenLib)
	target_compile_features(VeroScriptCppBindGenTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptCppBindGenTests)
	vs_set_solution_folder(VeroScriptCppBindGenTests "Tests/CppTools")
	add_test(NAME VeroScriptCppBindGenTests COMMAND VeroScriptCppBindGenTests)
	add_test(NAME VeroScriptCppBindGenCliVersion COMMAND VeroScriptCppBindGen --version)
	set_tests_properties(VeroScriptCppBindGenCliVersion PROPERTIES
		PASS_REGULAR_EXPRESSION "VeroScriptCppBindGen 0\\.1\\.1")
endif()

install(TARGETS VeroScriptCppBindGenLib VeroScriptCppBindGen
	ARCHIVE DESTINATION lib COMPONENT VeroScriptCppTools
	RUNTIME DESTINATION bin COMPONENT VeroScriptCppTools)
install(DIRECTORY Public/
	DESTINATION include/CppBindGen COMPONENT VeroScriptCppTools)
