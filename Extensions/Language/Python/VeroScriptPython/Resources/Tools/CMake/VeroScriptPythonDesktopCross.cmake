# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Build generators use the host Python; headers and extension ABI belong to the target.
find_package(Python3 3.11...<3.16 REQUIRED COMPONENTS Interpreter)
set(VEROSCRIPT_PYTHON_DESKTOP_SDK "" CACHE FILEPATH "Verified desktop target CPython SDK JSON")
if(NOT VEROSCRIPT_PYTHON_DESKTOP_SDK)
	message(FATAL_ERROR "Desktop cross Python requires VEROSCRIPT_PYTHON_DESKTOP_SDK; do not infer target artifacts from the host interpreter")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}"
	"${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/Python/Resources/Tools/Desktop/desktop_sdk.py"
	--sdk "${VEROSCRIPT_PYTHON_DESKTOP_SDK}" --system "${CMAKE_SYSTEM_NAME}"
	--architecture "${CMAKE_SYSTEM_PROCESSOR}" --pointer-size "${CMAKE_SIZEOF_VOID_P}"
	RESULT_VARIABLE _vs_python_sdk_status OUTPUT_VARIABLE _vs_python_sdk ERROR_VARIABLE _vs_python_sdk_error)
if(NOT _vs_python_sdk_status EQUAL 0)
	message(FATAL_ERROR "Invalid desktop CPython SDK: ${_vs_python_sdk_error}")
endif()
foreach(field IN ITEMS pythonVersion extensionSuffix includeDirectory architecture)
	string(JSON _vs_desktop_python_${field} GET "${_vs_python_sdk}" "${field}")
endforeach()
set(Python3_VERSION "${_vs_desktop_python_pythonVersion}")
string(REPLACE "." ";" _vs_python_version_parts "${Python3_VERSION}")
list(GET _vs_python_version_parts 0 Python3_VERSION_MAJOR)
list(GET _vs_python_version_parts 1 Python3_VERSION_MINOR)
add_library(VeroScriptPython MODULE "${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPython.cpp")
target_include_directories(VeroScriptPython PRIVATE "${_vs_desktop_python_includeDirectory}")
set_target_properties(VeroScriptPython PROPERTIES PREFIX "" OUTPUT_NAME "_veroscript"
	SUFFIX "${_vs_desktop_python_extensionSuffix}")
if(WIN32)
	string(JSON _vs_python_import_library GET "${_vs_python_sdk}" "importLibrary")
	target_link_libraries(VeroScriptPython PRIVATE "${_vs_python_import_library}")
elseif(APPLE)
	target_link_options(VeroScriptPython PRIVATE "LINKER:-undefined,dynamic_lookup")
endif()
message(STATUS "Desktop target Python: ${Python3_VERSION} ${_vs_desktop_python_architecture} ${_vs_desktop_python_extensionSuffix}")
