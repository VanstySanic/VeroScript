# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

option(VS_BUILD_VEROSCRIPT_PYTHON "Build the CPython VeroScript binding" ON)
option(VS_VEROSCRIPT_PYTHON_PACKAGE_BUILD
	"Fail configuration instead of skipping when wheel prerequisites are unavailable" OFF)

if(NOT VS_BUILD_VEROSCRIPT_PYTHON)
	message(STATUS "VeroScriptPython: disabled")
	return()
endif()

if(VEROSCRIPT_MOBILE_TARGET OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
	include("${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptPythonCross.cmake")
elseif(CMAKE_CROSSCOMPILING)
	include("${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptPythonDesktopCross.cmake")
else()
	find_package(Python3 3.11...<3.16 COMPONENTS Interpreter Development.Module QUIET)
	if(NOT Python3_Interpreter_FOUND OR NOT Python3_Development.Module_FOUND)
		if(VS_VEROSCRIPT_PYTHON_PACKAGE_BUILD)
			message(FATAL_ERROR
				"VeroScriptPython package build requires a CPython 3.11-3.15 interpreter and Development.Module")
		endif()
		message(STATUS
			"VeroScriptPython: skipped (CPython 3.11-3.15 interpreter and Development.Module are required)")
		return()
	endif()

# VeroScriptPython is a CPython extension, not an embedded Python distribution.
# The selected interpreter supplies the build-time headers, import library and
# ABI suffix; neither the interpreter nor its SDK is copied into the package.
	execute_process(
		COMMAND "${Python3_EXECUTABLE}" -c
			"import sys; print(sys.implementation.name, end='')"
		RESULT_VARIABLE _VeroScriptPython_implementation_status
		OUTPUT_VARIABLE _VeroScriptPython_implementation
		ERROR_QUIET)
	if(NOT _VeroScriptPython_implementation_status EQUAL 0
		OR NOT _VeroScriptPython_implementation STREQUAL "cpython")
		message(FATAL_ERROR
			"VeroScriptPython requires CPython; selected interpreter: ${Python3_EXECUTABLE}")
	endif()
	message(STATUS
		"VeroScriptPython: CPython ${Python3_VERSION} (${Python3_EXECUTABLE})")

	Python3_add_library(VeroScriptPython MODULE WITH_SOABI
		Private/VeroScriptPython.cpp)
	target_link_libraries(VeroScriptPython PRIVATE Python3::Module)
	set_target_properties(VeroScriptPython PROPERTIES PREFIX "" OUTPUT_NAME "_veroscript")
endif()
target_compile_features(VeroScriptPython PRIVATE cxx_std_17)
target_link_libraries(VeroScriptPython PRIVATE VeroScriptInterop)
if(MSVC)
	# Keep build-machine paths out of the Windows wheel binaries. MSVC otherwise
	# records the absolute PDB output path in each linked image.
	target_link_options(VeroScriptPython PRIVATE
		"/PDBALTPATH:$<TARGET_PDB_FILE_NAME:VeroScriptPython>")
	target_link_options(VeroScriptInterop PRIVATE
		"/PDBALTPATH:$<TARGET_PDB_FILE_NAME:VeroScriptInterop>")
endif()
target_include_directories(VeroScriptPython PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/Private")
set_target_properties(VeroScriptPython PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/$<CONFIG>/veroscript"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/$<CONFIG>/veroscript")
if(APPLE)
	set_property(TARGET VeroScriptPython PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
	set_property(TARGET VeroScriptPython PROPERTY INSTALL_RPATH "$ORIGIN")
endif()
vs_apply_cxx_defaults(VeroScriptPython)
vs_set_solution_folder(VeroScriptPython "Libraries")

# Keep pure-Python facade changes in the build-tree package dependency graph.
# A POST_BUILD command alone is not sufficient: editing only one of these files
# does not relink the native extension, so an incremental test run could import a
# stale facade even though the source tree and wheel/install rules are current.
set(_VeroScriptPython_package_output_dir
	"${CMAKE_BINARY_DIR}/python/$<CONFIG>/veroscript")
set(_VeroScriptPython_package_files
	"${CMAKE_CURRENT_SOURCE_DIR}/python/veroscript/__init__.py"
	"${CMAKE_CURRENT_SOURCE_DIR}/python/veroscript/_mobile.py"
	"${CMAKE_CURRENT_SOURCE_DIR}/python/veroscript/_veroscript.pyi"
	"${CMAKE_CURRENT_SOURCE_DIR}/python/veroscript/py.typed")
set(_VeroScriptPython_package_stamp
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPythonPackage-$<CONFIG>.stamp")
add_custom_command(
	OUTPUT "${_VeroScriptPython_package_stamp}"
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${_VeroScriptPython_package_output_dir}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${_VeroScriptPython_package_files}
		"${_VeroScriptPython_package_output_dir}"
	COMMAND ${CMAKE_COMMAND} -E touch "${_VeroScriptPython_package_stamp}"
	DEPENDS ${_VeroScriptPython_package_files}
	COMMENT "Deploying the VeroScript Python facade")
add_custom_target(VeroScriptPythonPackageFiles
	DEPENDS "${_VeroScriptPython_package_stamp}")
add_dependencies(VeroScriptPython VeroScriptPythonPackageFiles)
vs_set_solution_folder(VeroScriptPythonPackageFiles "Libraries")

# Keep the importable package self-contained in the build tree. The operating
# system loader searches beside the extension first, so deploying Interop here
# also avoids modifying PATH/LD_LIBRARY_PATH in users and tests. Refresh the
# copy on every build: Interop can change without relinking the extension.
if(NOT VEROSCRIPT_INTEROP_STATIC)
	add_custom_target(VeroScriptPythonInteropRuntime
		COMMAND ${CMAKE_COMMAND} -E make_directory
			"$<TARGET_FILE_DIR:VeroScriptPython>"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:VeroScriptInterop>"
			"$<TARGET_FILE_DIR:VeroScriptPython>/$<TARGET_FILE_NAME:VeroScriptInterop>"
		DEPENDS VeroScriptInterop
		COMMENT "Deploying the VeroScript Python Interop runtime")
	add_dependencies(VeroScriptPython VeroScriptPythonInteropRuntime)
	vs_set_solution_folder(VeroScriptPythonInteropRuntime "Libraries")
endif()

# Script-only edits do not relink the extension. Mirror the standard library on
# every build so the importable package cannot keep stale or removed scripts.
add_custom_target(VeroScriptPythonStandardLibrary
	COMMAND ${CMAKE_COMMAND} -E remove_directory
		"${_VeroScriptPython_package_output_dir}/stdlib"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
		"${_VeroScriptPython_package_output_dir}/stdlib"
	COMMENT "Deploying the VeroScript Python standard library")
add_dependencies(VeroScriptPython VeroScriptPythonStandardLibrary)
vs_set_solution_folder(VeroScriptPythonStandardLibrary "Libraries")

# Refresh resources even when the extension itself does not need relinking.
add_custom_target(VeroScriptPythonLocalization
	COMMAND ${CMAKE_COMMAND}
		"-DVEROSCRIPT_SOURCE_ROOT=${VEROSCRIPT_MODULE_ROOT}"
		"-DVEROSCRIPT_LOCALIZATION_OUTPUT=${_VeroScriptPython_package_output_dir}/localization"
		"-DVEROSCRIPT_LOCALIZATION_PROFILE=python"
		-P "${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptLocalization.cmake"
	COMMENT "Deploying Python runtime and tool localization")
add_dependencies(VeroScriptPython VeroScriptPythonLocalization)
vs_set_solution_folder(VeroScriptPythonLocalization "Libraries")

if(BUILD_TESTING)
	# Exercise the opaque handle using a real native SDK producer. This fixture
	# is never installed into a wheel or a mobile SDK.
	add_library(VeroScriptPythonAnnotationServiceTests SHARED
		Private/Tests/VeroScriptPythonAnnotationService.cpp)
	target_link_libraries(VeroScriptPythonAnnotationServiceTests PRIVATE VeroScriptInterop VeroScript)
	vs_apply_cxx_defaults(VeroScriptPythonAnnotationServiceTests)
	vs_set_solution_folder(VeroScriptPythonAnnotationServiceTests "Tests")
	add_dependencies(VeroScriptPython VeroScriptPythonAnnotationServiceTests)
	add_test(NAME VeroScriptPythonTests
		COMMAND "${Python3_EXECUTABLE}"
			-X dev
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_veroscript.py")
	set_tests_properties(VeroScriptPythonTests PROPERTIES
		ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:VeroScriptPython>/..;VEROSCRIPT_TEST_ANNOTATION_LIBRARY=$<TARGET_FILE:VeroScriptPythonAnnotationServiceTests>")
	add_test(NAME VeroScriptPythonVariantTests
		COMMAND "${Python3_EXECUTABLE}" -X dev
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_variant.py" -v)
	set_tests_properties(VeroScriptPythonVariantTests PROPERTIES
		LABELS "VeroScript;Python"
		ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:VeroScriptPython>/..")
	add_test(NAME VeroScriptPythonNumericTests
		COMMAND "${Python3_EXECUTABLE}" -X dev
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_numerics.py" -v)
	set_tests_properties(VeroScriptPythonNumericTests PROPERTIES
		LABELS "VeroScript;Python"
		ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:VeroScriptPython>/..")
	add_test(NAME VeroScriptPythonDistributionValidatorTests
		COMMAND "${Python3_EXECUTABLE}"
			-X dev
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_distribution_validator.py" -v)
	set_tests_properties(VeroScriptPythonDistributionValidatorTests PROPERTIES
		LABELS "VeroScript;Python;Packaging")
	add_test(NAME VeroScriptPythonMobileContractTests
		COMMAND "${Python3_EXECUTABLE}" -X dev
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mobile.py" -v)
	set_tests_properties(VeroScriptPythonMobileContractTests PROPERTIES
		LABELS "VeroScript;Python;Mobile;Packaging"
		ENVIRONMENT "VEROSCRIPT_TEST_CMAKE=${CMAKE_COMMAND};PYTHONPATH=$<TARGET_FILE_DIR:VeroScriptPython>/..")
endif()

include(GNUInstallDirs)
if(VEROSCRIPT_MOBILE_TARGET)
	set(_VeroScriptPython_install_dir "python/veroscript")
elseif(WIN32)
	set(_VeroScriptPython_install_dir "Lib/site-packages/veroscript")
else()
	set(_VeroScriptPython_install_dir
		"${CMAKE_INSTALL_LIBDIR}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/veroscript")
endif()
set(VEROSCRIPT_PYTHON_INSTALL_DIR "${_VeroScriptPython_install_dir}" CACHE STRING
	"Install destination for the VeroScript Python package")
if(VS_VEROSCRIPT_PYTHON_PACKAGE_BUILD
	AND IS_ABSOLUTE "${VEROSCRIPT_PYTHON_INSTALL_DIR}")
	message(FATAL_ERROR
		"VEROSCRIPT_PYTHON_INSTALL_DIR must be relative for a relocatable wheel")
endif()
if(ANDROID)
	install(TARGETS VeroScriptPython LIBRARY DESTINATION "jniLibs/${CMAKE_ANDROID_ARCH_ABI}"
		COMPONENT VeroScriptPython)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	install(TARGETS VeroScriptPython FRAMEWORK DESTINATION Frameworks COMPONENT VeroScriptPython)
else()
	install(TARGETS VeroScriptPython
		LIBRARY DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}" COMPONENT VeroScriptPython
		RUNTIME DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}" COMPONENT VeroScriptPython)
endif()
install(DIRECTORY python/veroscript/
	DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}"
	COMPONENT VeroScriptPython
	FILES_MATCHING PATTERN "*.py" PATTERN "*.pyi" PATTERN "py.typed")
vs_localization_roots(_VeroScriptPython_localization_roots "${CMAKE_SOURCE_DIR}" python)
foreach(_VeroScriptPython_localization_root IN LISTS _VeroScriptPython_localization_roots)
	install(DIRECTORY "${_VeroScriptPython_localization_root}/"
		DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}/localization"
		COMPONENT VeroScriptPython)
endforeach()
install(DIRECTORY
	"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
	DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}"
	COMPONENT VeroScriptPython)
if(NOT VEROSCRIPT_INTEROP_STATIC)
	install(FILES "$<TARGET_FILE:VeroScriptInterop>"
		DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}"
		COMPONENT VeroScriptPython)
endif()

if(TARGET VeroScriptApplication)
	set(_VeroScriptPython_application_vendor
		"${VEROSCRIPT_MODULE_ROOT}/VeroScriptApplication/ThirdParty")
	install(FILES "${_VeroScriptPython_application_vendor}/miniz/LICENSE"
		DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}/licenses/application/miniz"
		COMPONENT VeroScriptPython)
	install(FILES "${_VeroScriptPython_application_vendor}/json/LICENSE.MIT"
		DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}/licenses/application/json"
		COMPONENT VeroScriptPython)
	install(FILES "${_VeroScriptPython_application_vendor}/sha256/LICENSE"
		DESTINATION "${VEROSCRIPT_PYTHON_INSTALL_DIR}/licenses/application/sha256"
		COMPONENT VeroScriptPython)
endif()

if(BUILD_TESTING AND TARGET VeroScriptAsmJitBackend)
	set_property(TEST VeroScriptPythonTests APPEND PROPERTY ENVIRONMENT
		"VEROSCRIPT_TEST_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>")
endif()
if(BUILD_TESTING AND TARGET VeroScriptBytecodeVmBackend)
	set_property(TEST VeroScriptPythonTests APPEND PROPERTY ENVIRONMENT
		"VEROSCRIPT_TEST_VM_BACKEND=$<TARGET_FILE:VeroScriptBytecodeVmBackend>")
endif()
