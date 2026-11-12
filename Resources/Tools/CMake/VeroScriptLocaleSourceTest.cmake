# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Put an owned source fixture before deployed/ancestor catalogs in discovery.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef test_id)
get_filename_component(test_root "${TEST_ROOT}" ABSOLUTE)
set(sandbox "${test_root}/${test_id}")
set(module "${sandbox}/Engine Sources")
file(MAKE_DIRECTORY "${sandbox}/bin" "${module}/VeroScript/Public")
file(WRITE "${sandbox}/CMakeLists.txt" "# Source repository fixture\n")
configure_file("${MODULE_ROOT}/VeroScript/Public/VeroScriptVersion.h"
	"${module}/VeroScript/Public/VeroScriptVersion.h" COPYONLY)
foreach(component IN ITEMS VeroScript Extensions/Language/Cpp/VeroScriptCpp VeroScriptTooling VeroScriptInterop)
	file(COPY "${MODULE_ROOT}/${component}/Resources/Localization"
		DESTINATION "${module}/${component}/Resources")
endforeach()
file(WRITE "${module}/VeroScript/Resources/Localization/en-US/fixture.json"
	"{\"test.source_fixture\":\"renamed module\"}\n")
file(MAKE_DIRECTORY "${module}/VeroScript/Resources/Scripts/stdlib")
file(WRITE "${module}/VeroScript/Resources/Scripts/stdlib/source_layout_fixture.vs"
	"int source_layout_fixture() { return 42; }\n")
get_filename_component(executable_name "${TEST_EXECUTABLE}" NAME)
configure_file("${TEST_EXECUTABLE}" "${sandbox}/bin/${executable_name}" COPYONLY)
execute_process(
	COMMAND "${CMAKE_COMMAND}" -E env --unset=VERO_LOCALIZATION_DIR --unset=VERO_LANG
		${TEST_EMULATOR} "${sandbox}/bin/${executable_name}" --source-module-catalogs --fixture
	WORKING_DIRECTORY "${sandbox}"
	RESULT_VARIABLE result)
file(REAL_PATH "${sandbox}" resolved_sandbox)
file(REAL_PATH "${test_root}" resolved_root)
cmake_path(IS_PREFIX resolved_root "${resolved_sandbox}" NORMALIZE contained)
if(NOT contained OR resolved_root STREQUAL resolved_sandbox)
	message(FATAL_ERROR "Source localization test directory escaped its root")
endif()
file(REMOVE_RECURSE "${sandbox}")
if(NOT result EQUAL 0)
	message(FATAL_ERROR "Source localization test failed: ${result}")
endif()
