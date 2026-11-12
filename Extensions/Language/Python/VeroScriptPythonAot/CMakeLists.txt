# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScriptPythonAot)
vs_use_shared_private_headers(VeroScriptPythonAot)
vs_set_solution_folder(VeroScriptPythonAot "Libraries")
target_link_libraries(VeroScriptPythonAot PUBLIC VeroScript)
add_dependencies(VeroScriptPythonAot VeroScriptPythonFrontend)
set_property(TARGET VeroScriptPythonAot APPEND PROPERTY SOURCES
	python/veroscript_python_aot/__init__.py
	python/veroscript_python_aot/__main__.py
	python/veroscript_python_aot/py.typed)

# The emitter uses the public compiler/AST surface exported by VeroScript.
# Keeping private core include directories out of this target ensures the
# standalone Python frontend/codegen layer remains SDK-buildable.

get_target_property(_VeroScriptPythonAot_sources VeroScriptPythonAot SOURCES)
list(FILTER _VeroScriptPythonAot_sources EXCLUDE REGEX "/Tests/")
set_target_properties(VeroScriptPythonAot PROPERTIES
	SOURCES "${_VeroScriptPythonAot_sources}")

if(BUILD_TESTING)
	find_package(Python3 3.11...<3.16 COMPONENTS Interpreter QUIET)
	add_executable(VeroScriptPythonAotTests
		Private/Tests/VeroScriptPythonAotTests.cpp)
	target_link_libraries(VeroScriptPythonAotTests PRIVATE VeroScriptPythonAot VeroScriptCpp)
	target_compile_definitions(VeroScriptPythonAotTests PRIVATE
		VEROSCRIPT_STDLIB_DIR="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	if(Python3_Interpreter_FOUND)
		set(_VeroScriptPythonAot_python_executable "${Python3_EXECUTABLE}")
		if(WIN32)
			# Escape native separators without treating POSIX ':' as a path list.
			file(TO_CMAKE_PATH "${Python3_EXECUTABLE}" _VeroScriptPythonAot_python_executable)
		endif()
		target_compile_definitions(VeroScriptPythonAotTests PRIVATE
			VEROSCRIPT_PYTHON_EXECUTABLE="${_VeroScriptPythonAot_python_executable}")
	endif()
	vs_apply_cxx_defaults(VeroScriptPythonAotTests)
	vs_set_solution_folder(VeroScriptPythonAotTests "Tests")
	add_test(NAME VeroScriptPythonAotTests COMMAND VeroScriptPythonAotTests)
	set_tests_properties(VeroScriptPythonAotTests PROPERTIES
		ENVIRONMENT "VERO_STDLIB_PATH=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
		TIMEOUT 120
		LABELS "VeroScript;Python;Aot")
	if(Python3_Interpreter_FOUND)
		if(NOT VS_BUILD_VEROSCRIPT_PYTHON_ONLY)
			add_test(NAME VeroScriptPythonAotApiTests
				COMMAND "${CMAKE_COMMAND}" -E env
					"VEROSCRIPT_EXECUTABLE=$<TARGET_FILE:VeroScriptCli>"
					"${Python3_EXECUTABLE}" -X dev
					"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aot.py" -v)
		else()
			add_test(NAME VeroScriptPythonAotApiTests
				COMMAND "${Python3_EXECUTABLE}" -X dev
					"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aot.py" -v)
		endif()
		set_tests_properties(VeroScriptPythonAotApiTests PROPERTIES
			TIMEOUT 120
			LABELS "VeroScript;Python;Aot;Codegen")
	endif()
endif()

install(DIRECTORY python/veroscript_python_aot
	DESTINATION share/veroscript/python-tools
	COMPONENT VeroScriptPythonTools
	PATTERN "__pycache__" EXCLUDE)
if(DEFINED VEROSCRIPT_PYTHON_INSTALL_DIR)
	get_filename_component(_VeroScriptPythonAot_site_dir
		"${VEROSCRIPT_PYTHON_INSTALL_DIR}" DIRECTORY)
	if(_VeroScriptPythonAot_site_dir STREQUAL "")
		set(_VeroScriptPythonAot_site_dir ".")
	endif()
	install(DIRECTORY python/veroscript_python_aot
		DESTINATION "${_VeroScriptPythonAot_site_dir}"
		COMPONENT VeroScriptPython
		PATTERN "__pycache__" EXCLUDE)
endif()
