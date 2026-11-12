# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
set(VS_VEROSCRIPT_VM_VERSION "0.1.1" CACHE STRING "Official bytecode VM product version")
if(NOT VS_VEROSCRIPT_VM_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
	message(FATAL_ERROR "VS_VEROSCRIPT_VM_VERSION must be a numeric major.minor.patch version")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(VeroScriptBytecodeVm VERSION ${VS_VEROSCRIPT_VM_VERSION} LANGUAGES CXX)
	find_package(VeroScriptExecutionContract 0.1.1 EXACT CONFIG REQUIRED)
	add_library(VeroScriptExecutionContract ALIAS VeroScript::VeroScriptExecutionContract)
	include("${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources/Tools/CMake/VeroScriptHelpers.cmake")
endif()


file(GLOB_RECURSE _vm_sources CONFIGURE_DEPENDS "Private/*.cpp")
file(GLOB_RECURSE _vm_identity CONFIGURE_DEPENDS "Private/*.cpp" "Private/*.h" "Private/*.inl" "Private/*.order" "Public/*.h")
list(SORT _vm_identity)
set(_vm_hashes "")
foreach(_source IN LISTS _vm_identity)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")
	file(READ "${_source}" _text)
	string(REPLACE "\r\n" "\n" _text "${_text}")
	string(SHA256 _hash "${_text}")
	string(APPEND _vm_hashes "${_hash}")
endforeach()
string(SHA256 _vm_id "${_vm_hashes}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/VeroScriptVmBuild.h"
	CONTENT "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n#pragma once\n\n#define VEROSCRIPT_VM_IMPLEMENTATION_ID \"@_vm_id@\"\n#define VEROSCRIPT_VM_VERSION \"@VS_VEROSCRIPT_VM_VERSION@\"\n" @ONLY)

add_library(VeroScriptBytecodeVm STATIC ${_vm_sources})
set_target_properties(VeroScriptBytecodeVm PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(VeroScriptBytecodeVm PUBLIC VeroScriptExecutionContract)
target_include_directories(VeroScriptBytecodeVm PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
	"$<INSTALL_INTERFACE:include/Vm>")
target_include_directories(VeroScriptBytecodeVm PRIVATE Private "${CMAKE_CURRENT_BINARY_DIR}/Generated")
target_compile_definitions(VeroScriptBytecodeVm PRIVATE VEROSCRIPT_BYTECODE_VM_DYNAMIC=0)
vs_apply_cxx_defaults(VeroScriptBytecodeVm)
if(MSVC)
	target_compile_options(VeroScriptBytecodeVm PRIVATE "$<$<CONFIG:Release>:/Ob3>")
endif()
vs_set_solution_folder(VeroScriptBytecodeVm "Libraries/Vm")

if(NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
	add_library(VeroScriptBytecodeVmBackend MODULE ${_vm_sources})
	target_link_libraries(VeroScriptBytecodeVmBackend PRIVATE VeroScriptExecutionContract)
	target_include_directories(VeroScriptBytecodeVmBackend PRIVATE Public Private "${CMAKE_CURRENT_BINARY_DIR}/Generated")
	target_compile_definitions(VeroScriptBytecodeVmBackend PRIVATE VEROSCRIPT_BYTECODE_VM_DYNAMIC=1)
	set_target_properties(VeroScriptBytecodeVmBackend PROPERTIES OUTPUT_NAME VeroScriptVmBytecode
		PREFIX "${CMAKE_SHARED_LIBRARY_PREFIX}" SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
		CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
	vs_apply_cxx_defaults(VeroScriptBytecodeVmBackend)
	if(MSVC)
		target_compile_options(VeroScriptBytecodeVmBackend PRIVATE "$<$<CONFIG:Release>:/Ob3>")
		# /DEBUG:FULL keeps release symbols but otherwise disables dead-code
		# elimination. Retain reference pruning without enabling function folding.
		target_link_options(VeroScriptBytecodeVmBackend PRIVATE "$<$<CONFIG:Release>:/OPT:REF>")
	endif()
	# Keep the ordinary dispatcher stable when cold governed helpers are added.
	# Identical opcode code otherwise moves across instruction-cache boundaries.
	# Profile-guided/LTO builds retain their compiler-selected function ordering.
	if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
		AND NOT VS_ENABLE_IPO AND (NOT DEFINED VS_PGO_PHASE OR VS_PGO_PHASE STREQUAL "OFF"))
		set(_vm_hot_order_file "${CMAKE_CURRENT_SOURCE_DIR}/Private/VeroScriptVmHotFunctions.order")
		set(_vm_use_hot_order "$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:$<TARGET_PROPERTY:INTERPROCEDURAL_OPTIMIZATION>>>,$<NOT:$<BOOL:$<TARGET_PROPERTY:INTERPROCEDURAL_OPTIMIZATION_RELEASE>>>>")
		target_link_options(VeroScriptBytecodeVmBackend PRIVATE "$<${_vm_use_hot_order}:/ORDER:@${_vm_hot_order_file}>")
		set_property(TARGET VeroScriptBytecodeVmBackend APPEND PROPERTY LINK_DEPENDS "${_vm_hot_order_file}")
	endif()
	vs_set_solution_folder(VeroScriptBytecodeVmBackend "Libraries/Vm")
	if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
		set(_vm_runtime_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
		get_property(_vm_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
		if(_vm_multi_config)
			string(APPEND _vm_runtime_directory "/$<CONFIG>")
		endif()
		# Run on each product build, including builds that do not relink its executable.
		# One shared target also avoids parallel writers to the products' common Vm directory.
		add_custom_target(VeroScriptRuntimeVmAssets
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${_vm_runtime_directory}/Vm"
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:VeroScriptBytecodeVmBackend>"
				"${_vm_runtime_directory}/Vm/$<TARGET_FILE_NAME:VeroScriptBytecodeVmBackend>"
			DEPENDS VeroScriptBytecodeVmBackend VERBATIM)
		vs_set_solution_folder(VeroScriptRuntimeVmAssets "Libraries/Vm")
	endif()
	install(TARGETS VeroScriptBytecodeVmBackend LIBRARY DESTINATION . COMPONENT VeroScriptBytecodeVmBackend
		RUNTIME DESTINATION . COMPONENT VeroScriptBytecodeVmBackend)
	install(TARGETS VeroScriptBytecodeVmBackend LIBRARY DESTINATION Vm COMPONENT VeroScriptRuntimeVm
		RUNTIME DESTINATION Vm COMPONENT VeroScriptRuntimeVm)
	vs_install_windows_runtime("." VeroScriptBytecodeVmBackend)
	vs_install_windows_runtime("Vm" VeroScriptRuntimeVm)
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(Resources/VeroScriptBytecodeVmConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBytecodeVmConfig.cmake"
	INSTALL_DESTINATION lib/cmake/VeroScriptBytecodeVm)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBytecodeVmConfigVersion.cmake"
	VERSION ${VS_VEROSCRIPT_VM_VERSION} COMPATIBILITY ExactVersion)
install(TARGETS VeroScriptBytecodeVm EXPORT VeroScriptBytecodeVmTargets
	ARCHIVE DESTINATION lib COMPONENT VeroScriptBytecodeVmSdk)
install(EXPORT VeroScriptBytecodeVmTargets NAMESPACE VeroScript::
	DESTINATION lib/cmake/VeroScriptBytecodeVm COMPONENT VeroScriptBytecodeVmSdk)
install(DIRECTORY Public/ DESTINATION include/Vm COMPONENT VeroScriptBytecodeVmSdk FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBytecodeVmConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBytecodeVmConfigVersion.cmake"
	DESTINATION lib/cmake/VeroScriptBytecodeVm COMPONENT VeroScriptBytecodeVmSdk)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../../LICENSE" DESTINATION . COMPONENT VeroScriptBytecodeVmBackend)

if(BUILD_TESTING AND TARGET VeroScript)
	add_executable(VeroScriptVmBackendTests Resources/Tests/Backend.cpp)
	target_link_libraries(VeroScriptVmBackendTests PRIVATE VeroScript VeroScriptBytecodeVm)
	vs_apply_cxx_defaults(VeroScriptVmBackendTests)
	add_test(NAME VeroScriptVmBackendStaticTests COMMAND VeroScriptVmBackendTests)
	if(TARGET VeroScriptBytecodeVmBackend)
		add_dependencies(VeroScriptVmBackendTests VeroScriptBytecodeVmBackend)
		add_test(NAME VeroScriptVmBackendDynamicTests COMMAND VeroScriptVmBackendTests "$<TARGET_FILE:VeroScriptBytecodeVmBackend>")
	endif()
endif()
