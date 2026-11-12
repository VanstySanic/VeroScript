# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

find_package(Python3 3.11...<3.16 COMPONENTS Interpreter QUIET)

add_custom_target(VeroScriptPythonTranspiler SOURCES
	python/veroscript_python_transpiler/__init__.py
	python/veroscript_python_transpiler/__main__.py
	python/veroscript_python_transpiler/py.typed)
add_dependencies(VeroScriptPythonTranspiler VeroScriptPythonFrontend)
vs_set_solution_folder(VeroScriptPythonTranspiler "Libraries/PythonTools")

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
	if(NOT VS_BUILD_VEROSCRIPT_PYTHON_ONLY)
		add_test(NAME VeroScriptPythonTranspilerTests
			COMMAND "${CMAKE_COMMAND}" -E env
				"VEROSCRIPT_EXECUTABLE=$<TARGET_FILE:VeroScriptCli>"
				"${Python3_EXECUTABLE}" -X dev
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_transpiler.py" -v)
	else()
		add_test(NAME VeroScriptPythonTranspilerTests
			COMMAND "${Python3_EXECUTABLE}" -X dev
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_transpiler.py" -v)
	endif()
	set_tests_properties(VeroScriptPythonTranspilerTests PROPERTIES
		LABELS "VeroScript;Python;Transpiler")
endif()

install(DIRECTORY python/veroscript_python_transpiler
	DESTINATION share/veroscript/python-tools
	COMPONENT VeroScriptPythonTools
	PATTERN "__pycache__" EXCLUDE)
if(DEFINED VEROSCRIPT_PYTHON_INSTALL_DIR)
	get_filename_component(_VeroScriptPythonTranspiler_site_dir
		"${VEROSCRIPT_PYTHON_INSTALL_DIR}" DIRECTORY)
	if(_VeroScriptPythonTranspiler_site_dir STREQUAL "")
		set(_VeroScriptPythonTranspiler_site_dir ".")
	endif()
	install(DIRECTORY python/veroscript_python_transpiler
		DESTINATION "${_VeroScriptPythonTranspiler_site_dir}"
		COMPONENT VeroScriptPython
		PATTERN "__pycache__" EXCLUDE)
endif()
