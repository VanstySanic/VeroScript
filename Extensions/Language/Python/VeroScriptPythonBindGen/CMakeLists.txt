# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

find_package(Python3 3.11...<3.16 COMPONENTS Interpreter QUIET)

add_custom_target(VeroScriptPythonBindGen SOURCES
	python/veroscript_python_bindgen/__init__.py
	python/veroscript_python_bindgen/__main__.py
	python/veroscript_python_bindgen/py.typed)
add_dependencies(VeroScriptPythonBindGen VeroScriptPythonFrontend)
vs_set_solution_folder(VeroScriptPythonBindGen "Libraries/PythonTools")

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
	if(NOT VS_BUILD_VEROSCRIPT_PYTHON_ONLY)
		add_test(NAME VeroScriptPythonBindGenTests
			COMMAND "${CMAKE_COMMAND}" -E env
				"VEROSCRIPT_EXECUTABLE=$<TARGET_FILE:VeroScriptCli>"
				"${Python3_EXECUTABLE}" -X dev
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bindgen.py" -v)
	else()
		add_test(NAME VeroScriptPythonBindGenTests
			COMMAND "${Python3_EXECUTABLE}" -X dev
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bindgen.py" -v)
	endif()
	set_tests_properties(VeroScriptPythonBindGenTests PROPERTIES
		LABELS "VeroScript;Python;BindGen")
endif()

install(DIRECTORY python/veroscript_python_bindgen
	DESTINATION share/veroscript/python-tools
	COMPONENT VeroScriptPythonTools
	PATTERN "__pycache__" EXCLUDE)
if(DEFINED VEROSCRIPT_PYTHON_INSTALL_DIR)
	get_filename_component(_VeroScriptPythonBindGen_site_dir
		"${VEROSCRIPT_PYTHON_INSTALL_DIR}" DIRECTORY)
	if(_VeroScriptPythonBindGen_site_dir STREQUAL "")
		set(_VeroScriptPythonBindGen_site_dir ".")
	endif()
	install(DIRECTORY python/veroscript_python_bindgen
		DESTINATION "${_VeroScriptPythonBindGen_site_dir}"
		COMPONENT VeroScriptPython
		PATTERN "__pycache__" EXCLUDE)
endif()
