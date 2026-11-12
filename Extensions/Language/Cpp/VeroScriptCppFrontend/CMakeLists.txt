# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScriptCppFrontend)
vs_set_solution_folder(VeroScriptCppFrontend "Libraries/CppTools")
target_compile_features(VeroScriptCppFrontend PUBLIC cxx_std_17)

get_target_property(_VeroScriptCppFrontend_sources VeroScriptCppFrontend SOURCES)
list(FILTER _VeroScriptCppFrontend_sources EXCLUDE REGEX "/Tests/")
set_target_properties(VeroScriptCppFrontend PROPERTIES
	SOURCES "${_VeroScriptCppFrontend_sources}")

if(BUILD_TESTING)
	add_executable(VeroScriptCppFrontendTests
		Private/Tests/VeroScriptCppFrontendTests.cpp)
	target_link_libraries(VeroScriptCppFrontendTests PRIVATE VeroScriptCppFrontend)
	target_compile_features(VeroScriptCppFrontendTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptCppFrontendTests)
	# The native enum oracle must use the standard enumerator types. MSVC's
	# legacy mode is not enabled by /permissive- and differs for unfixed enums.
	if(MSVC AND MSVC_VERSION GREATER_EQUAL 1934)
		target_compile_options(VeroScriptCppFrontendTests PRIVATE /Zc:enumTypes)
	elseif(MSVC)
		target_compile_definitions(VeroScriptCppFrontendTests PRIVATE
			VEROSCRIPT_CPP_TEST_LEGACY_ENUM_TYPES=1)
	endif()
	vs_set_solution_folder(VeroScriptCppFrontendTests "Tests/CppTools")
	add_test(NAME VeroScriptCppFrontendTests COMMAND VeroScriptCppFrontendTests)
endif()

install(TARGETS VeroScriptCppFrontend
	ARCHIVE DESTINATION lib COMPONENT VeroScriptCppTools)
install(DIRECTORY Public/
	DESTINATION include/CppFrontend COMPONENT VeroScriptCppTools)
