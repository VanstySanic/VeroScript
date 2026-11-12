# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

find_package(Python3 3.11...<3.16 COMPONENTS Interpreter QUIET)

# Standalone tool installations have neither checkout headers nor wheel metadata.
# Keep their version self-contained, using the same native declaration as wheels.
set(_VeroScriptPythonFrontend_version_header
	"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public/VeroScriptVersion.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${_VeroScriptPythonFrontend_version_header}")
file(READ "${_VeroScriptPythonFrontend_version_header}" _VeroScriptPythonFrontend_version_text)
if(NOT _VeroScriptPythonFrontend_version_text MATCHES
	"VEROSCRIPT_VERSION[ \t\r\n]*=[ \t\r\n]*\\{[ \t\r\n]*([0-9]+)[ \t\r\n]*,[ \t\r\n]*([0-9]+)[ \t\r\n]*,[ \t\r\n]*([0-9]+)[ \t\r\n]*\\}")
	message(FATAL_ERROR "Missing native VeroScript version declaration")
endif()
set(_VeroScriptPythonFrontend_version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/_version.json"
	"{\"version\":\"${_VeroScriptPythonFrontend_version}\"}\n")

add_custom_target(VeroScriptPythonFrontend SOURCES
	python/veroscript_python_frontend/__init__.py
	python/veroscript_python_frontend/__main__.py
	python/veroscript_python_frontend/_toolchain.py
	python/veroscript_python_frontend/_version.py
	python/veroscript_python_frontend/localization.py
	python/veroscript_python_frontend/py.typed)
vs_set_solution_folder(VeroScriptPythonFrontend "Libraries/PythonTools")

if(BUILD_TESTING AND Python3_Interpreter_FOUND)
	add_test(NAME VeroScriptPythonFrontendTests
		COMMAND "${Python3_EXECUTABLE}" -X dev
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frontend.py" -v)
	set_tests_properties(VeroScriptPythonFrontendTests PROPERTIES
		LABELS "VeroScript;Python;Frontend")
endif()

install(DIRECTORY python/veroscript_python_frontend
	DESTINATION share/veroscript/python-tools
	COMPONENT VeroScriptPythonTools
	PATTERN "__pycache__" EXCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_version.json"
	DESTINATION share/veroscript/python-tools/veroscript_python_frontend
	COMPONENT VeroScriptPythonTools)
if(DEFINED VEROSCRIPT_PYTHON_INSTALL_DIR)
	get_filename_component(_VeroScriptPythonFrontend_site_dir
		"${VEROSCRIPT_PYTHON_INSTALL_DIR}" DIRECTORY)
	if(_VeroScriptPythonFrontend_site_dir STREQUAL "")
		set(_VeroScriptPythonFrontend_site_dir ".")
	endif()
	install(DIRECTORY python/veroscript_python_frontend
		DESTINATION "${_VeroScriptPythonFrontend_site_dir}"
		COMPONENT VeroScriptPython
		PATTERN "__pycache__" EXCLUDE)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_version.json"
		DESTINATION "${_VeroScriptPythonFrontend_site_dir}/veroscript_python_frontend"
		COMPONENT VeroScriptPython)
endif()
vs_localization_roots(_VeroScriptPythonTools_localization_roots "${CMAKE_SOURCE_DIR}" python-tools)
foreach(_VeroScriptPythonTools_localization_root IN LISTS _VeroScriptPythonTools_localization_roots)
	install(DIRECTORY "${_VeroScriptPythonTools_localization_root}/"
		DESTINATION share/veroscript/python-tools/localization
		COMPONENT VeroScriptPythonTools
		FILES_MATCHING PATTERN "*.json")
endforeach()
