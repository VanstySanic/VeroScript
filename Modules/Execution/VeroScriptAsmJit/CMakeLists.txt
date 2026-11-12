# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

set(VS_VEROSCRIPT_ASMJIT_VERSION "0.1.1" CACHE STRING "Independent AsmJit backend version")
if(NOT VS_VEROSCRIPT_ASMJIT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
	message(FATAL_ERROR "VS_VEROSCRIPT_ASMJIT_VERSION must be a three-part version")
endif()
string(LENGTH "${VS_VEROSCRIPT_ASMJIT_VERSION}" _vs_asmjit_version_length)
if(_vs_asmjit_version_length GREATER 31)
	message(FATAL_ERROR "VS_VEROSCRIPT_ASMJIT_VERSION exceeds the module descriptor's 31-character limit")
endif()
vs_add_module_library(VeroScriptAsmJit)
vs_set_solution_folder(VeroScriptAsmJit "Libraries/Jit")
get_target_property(_sources VeroScriptAsmJit SOURCES)
list(FILTER _sources EXCLUDE REGEX "/Tests/.*\\.cpp$")
set_property(TARGET VeroScriptAsmJit PROPERTY SOURCES "${_sources}")
target_link_libraries(VeroScriptAsmJit PUBLIC VeroScript)
target_include_directories(VeroScriptAsmJit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
option(VEROSCRIPT_ENABLE_JIT_TEST_HOOKS "Enable private JIT path observers (tests only)" OFF)
target_compile_definitions(VeroScriptAsmJit PRIVATE
	VEROSCRIPT_ENABLE_JIT_TEST_HOOKS=$<BOOL:${VEROSCRIPT_ENABLE_JIT_TEST_HOOKS}>)

# Optional asmjit native leaf codegen.
# Host backends: x86_64 (MS x64 ABI), AArch64 (AAPCS64), and x86 (cdecl leaves).
if(VEROSCRIPT_MOBILE_TARGET)
	set(_VeroScript_default_native OFF)
else()
	set(_VeroScript_default_native ON)
endif()
option(VEROSCRIPT_ENABLE_ASMJIT "Build VeroScript JIT native leaves with asmjit" ${_VeroScript_default_native})
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND VEROSCRIPT_ENABLE_ASMJIT)
	message(FATAL_ERROR "iOS native JIT is unsupported; use VEROSCRIPT_ENABLE_ASMJIT=OFF")
endif()
set(_VeroScript_use_asmjit OFF)
set(_VeroScript_asmjit_arch "")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _VeroScript_proc)
if(_VeroScript_proc STREQUAL "" AND NOT CMAKE_CROSSCOMPILING)
	string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _VeroScript_proc)
endif()
if(DEFINED CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
	string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" _VeroScript_cxx_arch)
else()
	set(_VeroScript_cxx_arch "")
endif()
if(DEFINED CMAKE_GENERATOR_PLATFORM)
	string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _VeroScript_generator_platform)
else()
	set(_VeroScript_generator_platform "")
endif()

# Visual Studio reports the host processor even when -A selects another ISA.
# Prefer explicit target information before considering that processor fallback.
if(_VeroScript_generator_platform MATCHES "^(arm64|aarch64|x64|amd64|x86_64|win32|x86)$")
	set(_VeroScript_proc "${_VeroScript_generator_platform}")
	set(_VeroScript_cxx_arch "${_VeroScript_generator_platform}")
elseif(_VeroScript_cxx_arch MATCHES "^(arm64|aarch64|x64|amd64|x86_64|x86|i[3-6]86)$")
	set(_VeroScript_proc "${_VeroScript_cxx_arch}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)

	set(_VeroScript_is_x64 OFF)
	set(_VeroScript_is_a64 OFF)
	if(_VeroScript_generator_platform MATCHES "^(arm64|aarch64)$"
		OR _VeroScript_proc MATCHES "^(aarch64|arm64)$"
		OR _VeroScript_cxx_arch MATCHES "^(arm64|aarch64)$")
		set(_VeroScript_is_a64 ON)
	elseif(_VeroScript_generator_platform MATCHES "^(x64|amd64|x86_64)$"
		OR _VeroScript_proc MATCHES "^(x86_64|amd64|x64)$"
		OR _VeroScript_cxx_arch MATCHES "^(x64|x86_64|amd64)$")
		set(_VeroScript_is_x64 ON)
	elseif(WIN32 AND NOT CMAKE_CROSSCOMPILING
		AND NOT _VeroScript_proc MATCHES "arm"
		AND NOT _VeroScript_cxx_arch MATCHES "arm")
		# VS generators may leave PROCESSOR empty or as AMD64; exclude Windows ARM64.
		set(_VeroScript_is_x64 ON)
	endif()

	# Apple: prefer the slice architecture when CMAKE_OSX_ARCHITECTURES is set.
	if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64"
		AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
		set(_VeroScript_is_x64 OFF)
		set(_VeroScript_is_a64 ON)
	elseif(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64"
		AND NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
		set(_VeroScript_is_x64 ON)
		set(_VeroScript_is_a64 OFF)
	elseif(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64"
		AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
		# Universal fat binary: do not enable a single asmjit emit ISA.
		set(_VeroScript_is_x64 OFF)
		set(_VeroScript_is_a64 OFF)
	endif()

	if(_VeroScript_is_x64)
		set(_VeroScript_asmjit_arch "x64")
	elseif(_VeroScript_is_a64)
		set(_VeroScript_asmjit_arch "a64")
	endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	if(_VeroScript_generator_platform MATCHES "^(win32|x86)$"
		OR _VeroScript_proc MATCHES "^(x86|i[3-6]86)$"
		OR _VeroScript_cxx_arch MATCHES "^(x86|i[3-6]86)$"
		OR (WIN32 AND NOT CMAKE_CROSSCOMPILING AND NOT _VeroScript_proc MATCHES "arm"))
		set(_VeroScript_asmjit_arch "x86")
	endif()
endif()

if(VEROSCRIPT_ENABLE_ASMJIT AND NOT _VeroScript_asmjit_arch STREQUAL "")
	set(_VeroScript_asmjit_dir "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asmjit")
	if(NOT EXISTS "${_VeroScript_asmjit_dir}/CMakeLists.txt")
		message(FATAL_ERROR
			"asmjit enabled but missing vendored tree at Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit. "
			"Expected ${_VeroScript_asmjit_dir}/CMakeLists.txt")
	endif()
	set(ASMJIT_STATIC TRUE CACHE BOOL "" FORCE)
	set(ASMJIT_TEST FALSE CACHE BOOL "" FORCE)
	# Host-only backend (x86 on x64 hosts, AArch64 on arm64 hosts).
	set(ASMJIT_NO_FOREIGN TRUE CACHE BOOL "" FORCE)
	# Vendored under Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit (offline, pinned); do not FetchContent.
	if(NOT TARGET asmjit AND NOT TARGET asmjit::asmjit)
		add_subdirectory("${_VeroScript_asmjit_dir}" "${CMAKE_BINARY_DIR}/_deps/asmjit-build")
	endif()
	if(TARGET asmjit::asmjit)
		set(_VeroScript_asmjit_link_target asmjit::asmjit)
	elseif(TARGET asmjit)
		set(_VeroScript_asmjit_link_target asmjit)
	else()
		message(FATAL_ERROR "asmjit target not found after add_subdirectory(${_VeroScript_asmjit_dir})")
	endif()
	target_link_libraries(VeroScriptAsmJit PRIVATE ${_VeroScript_asmjit_link_target})
	target_compile_definitions(VeroScriptAsmJit PRIVATE VEROSCRIPT_HAS_ASMJIT=1)
	if(_VeroScript_asmjit_arch STREQUAL "x64")
		target_compile_definitions(VeroScriptAsmJit PRIVATE VEROSCRIPT_ASMJIT_X64=1 VEROSCRIPT_ASMJIT_A64=0 VEROSCRIPT_ASMJIT_X86=0)
		message(STATUS "VeroScript asmjit native leaves: ON (x86_64 / MS x64 ABI)")
	elseif(_VeroScript_asmjit_arch STREQUAL "a64")
		target_compile_definitions(VeroScriptAsmJit PRIVATE VEROSCRIPT_ASMJIT_X64=0 VEROSCRIPT_ASMJIT_A64=1 VEROSCRIPT_ASMJIT_X86=0)
		message(STATUS "VeroScript asmjit native leaves: ON (AArch64 / AAPCS64; scalar CFG enabled)")
	else()
		target_compile_definitions(VeroScriptAsmJit PRIVATE VEROSCRIPT_ASMJIT_X64=0 VEROSCRIPT_ASMJIT_A64=0 VEROSCRIPT_ASMJIT_X86=1)
		message(STATUS "VeroScript asmjit native leaves: ON (x86 / cdecl scalar leaves)")
	endif()
	set(_VeroScript_use_asmjit ON)
endif()
set(VEROSCRIPT_ASMJIT_NATIVE_ENABLED ${_VeroScript_use_asmjit} CACHE INTERNAL "Actual native emission capability" FORCE)
if(NOT _VeroScript_use_asmjit)
	target_compile_definitions(VeroScriptAsmJit PRIVATE
		VEROSCRIPT_HAS_ASMJIT=0
		VEROSCRIPT_ASMJIT_X64=0
		VEROSCRIPT_ASMJIT_A64=0
		VEROSCRIPT_ASMJIT_X86=0)
	message(STATUS "VeroScript asmjit native leaves: OFF (${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR})")
endif()

# iOS portable optimization is available through static factory injection only.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
# Runtime backend links headers and the emitter only, never another core copy.
	find_package(Threads REQUIRED)
	get_target_property(_backend_sources VeroScriptAsmJit SOURCES)
	add_library(VeroScriptAsmJitBackend SHARED ${_backend_sources})
	vs_add_module_include_roots(VeroScriptAsmJitBackend)
	target_include_directories(VeroScriptAsmJitBackend PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/Private"
		"$<TARGET_PROPERTY:VeroScript,INTERFACE_INCLUDE_DIRECTORIES>")
	get_target_property(_backend_definitions VeroScriptAsmJit COMPILE_DEFINITIONS)
	target_compile_definitions(VeroScriptAsmJitBackend PRIVATE ${_backend_definitions} VEROSCRIPT_ASMJIT_DYNAMIC=1
		VEROSCRIPT_ASMJIT_MODULE_VERSION="${VS_VEROSCRIPT_ASMJIT_VERSION}")
	target_link_libraries(VeroScriptAsmJitBackend PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
	if(_VeroScript_use_asmjit)
		target_link_libraries(VeroScriptAsmJitBackend PRIVATE ${_VeroScript_asmjit_link_target})
		get_target_property(_VeroScript_asmjit_compile_target ${_VeroScript_asmjit_link_target} ALIASED_TARGET)
		if(NOT _VeroScript_asmjit_compile_target)
			set(_VeroScript_asmjit_compile_target ${_VeroScript_asmjit_link_target})
		endif()
		get_target_property(_VeroScript_asmjit_imported ${_VeroScript_asmjit_compile_target} IMPORTED)
		# An imported emitter is already built; its owner supplies PIC/runtime dependencies.
		if(NOT _VeroScript_asmjit_imported)
			set_property(TARGET ${_VeroScript_asmjit_compile_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
		endif()
	endif()
# GNU unique template objects cause the ELF loader to pin a DSO after dlclose.
# Backend leases, rather than implicit NODELETE behavior, own the code lifetime.
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_compile_options(VeroScriptAsmJitBackend PRIVATE -fno-gnu-unique)
		if(_VeroScript_use_asmjit AND NOT _VeroScript_asmjit_imported)
			target_compile_options(${_VeroScript_asmjit_compile_target} PRIVATE -fno-gnu-unique)
		endif()
	endif()
	vs_apply_cxx_defaults(VeroScriptAsmJitBackend)
	set_target_properties(VeroScriptAsmJitBackend PROPERTIES
		OUTPUT_NAME VeroScriptJitAsmJit
		CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
		BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
	if(APPLE)
		set_target_properties(VeroScriptAsmJitBackend PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
	endif()
	if(UNIX AND NOT APPLE)
		target_link_options(VeroScriptAsmJitBackend PRIVATE -Wl,--no-undefined)
	endif()
	if(ANDROID)
		target_link_options(VeroScriptAsmJitBackend PRIVATE "LINKER:-z,max-page-size=16384")
	endif()
	vs_set_solution_folder(VeroScriptAsmJitBackend "Libraries/Jit")
	install(TARGETS VeroScriptAsmJitBackend
		RUNTIME DESTINATION . COMPONENT VeroScriptAsmJitBackend
		LIBRARY DESTINATION . COMPONENT VeroScriptAsmJitBackend)
	install(FILES "${VEROSCRIPT_MODULE_ROOT}/LICENSE" DESTINATION . COMPONENT VeroScriptAsmJitBackend)
	install(FILES ThirdParty/asmjit/VERSION.txt ThirdParty/asmjit/LICENSE.md
		DESTINATION licenses/asmjit COMPONENT VeroScriptAsmJitBackend)
	vs_install_windows_runtime("." VeroScriptAsmJitBackend)
endif()

if(BUILD_TESTING)
	if(TARGET VeroScript AND TARGET VeroScriptBytecodeVmBackend AND TARGET VeroScriptAsmJitBackend)
		add_executable(VeroScriptJitDiagnosticsTests Private/Tests/VeroScriptJitDiagnosticsTests.cpp)
		target_link_libraries(VeroScriptJitDiagnosticsTests PRIVATE VeroScript)
		vs_apply_cxx_defaults(VeroScriptJitDiagnosticsTests)
		add_dependencies(VeroScriptJitDiagnosticsTests VeroScriptBytecodeVmBackend VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptJitDiagnosticsTests COMMAND VeroScriptJitDiagnosticsTests
			"$<TARGET_FILE:VeroScriptBytecodeVmBackend>" "$<TARGET_FILE:VeroScriptAsmJitBackend>"
			"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
		set_tests_properties(VeroScriptJitDiagnosticsTests PROPERTIES TIMEOUT 60 LABELS "Core;Jit;Diagnostics")
	endif()
	if(TARGET VeroScriptPerformanceRegressionTests AND TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptPerformanceRegressionTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptAsmJitPerformanceRegressionTests COMMAND VeroScriptPerformanceRegressionTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
		set_tests_properties(VeroScriptAsmJitPerformanceRegressionTests PROPERTIES LABELS "Core;Jit;Performance" TIMEOUT 120)
	endif()
	if(TARGET VeroScriptInheritancePathTests AND TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptInheritancePathTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptAsmJitInheritancePathTests COMMAND VeroScriptInheritancePathTests
			"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib" "$<TARGET_FILE:VeroScriptAsmJitBackend>")
		set_tests_properties(VeroScriptAsmJitInheritancePathTests PROPERTIES LABELS "Core;Jit" TIMEOUT 120)
	endif()
	add_executable(VeroScriptControlledExecutionTests Private/Tests/VeroScriptControlledExecutionTests.cpp)
	target_link_libraries(VeroScriptControlledExecutionTests PRIVATE VeroScriptAsmJit)
	vs_apply_cxx_defaults(VeroScriptControlledExecutionTests)
	add_test(NAME VeroScriptControlledExecutionTests COMMAND VeroScriptControlledExecutionTests)
	set_tests_properties(VeroScriptControlledExecutionTests PROPERTIES LABELS "Core;Jit;Mobile" TIMEOUT 120)
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptControlledExecutionTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptControlledDynamicExecutionTests COMMAND VeroScriptControlledExecutionTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
		set_tests_properties(VeroScriptControlledDynamicExecutionTests PROPERTIES LABELS "Core;Jit;Mobile" TIMEOUT 120)
	endif()
	if(_VeroScript_use_asmjit AND _VeroScript_asmjit_arch STREQUAL "x64")
		# Compile the AArch64 emitter on the desktop as an additional syntax gate.
		# This is not a target-toolchain, ABI, executable-memory or device test.
		add_library(VeroScriptControlledA64Syntax OBJECT EXCLUDE_FROM_ALL Private/VeroScriptAsmJitControlled.cpp)
		target_include_directories(VeroScriptControlledA64Syntax PRIVATE Private
			"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public" "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asmjit")
		target_compile_definitions(VeroScriptControlledA64Syntax PRIVATE ASMJIT_STATIC=1
			VEROSCRIPT_HAS_ASMJIT=1 VEROSCRIPT_ASMJIT_A64=1 VEROSCRIPT_ASMJIT_X64=0 VEROSCRIPT_ASMJIT_X86=0)
		vs_apply_cxx_defaults(VeroScriptControlledA64Syntax)
		add_dependencies(VeroScriptControlledExecutionTests VeroScriptControlledA64Syntax)
	endif()
	# Concrete backend benchmarks are opt-in and never part of functional CTest.
	foreach(_benchmark IN ITEMS RuntimeBenchmarks HostBenchmarks SessionCacheResources ReviewReplay CrossReplay)
		if(NOT WIN32 AND _benchmark MATCHES "Replay$")
			continue()
		endif()
		if(_benchmark STREQUAL "ReviewReplay")
			set(_source review.cpp)
		elseif(_benchmark STREQUAL "CrossReplay")
			set(_source cross.cpp)
		else()
			set(_source "${_benchmark}.cpp")
		endif()
		add_executable(VeroScript${_benchmark} EXCLUDE_FROM_ALL "Resources/Benchmarks/${_source}")
		target_link_libraries(VeroScript${_benchmark} PRIVATE VeroScriptAsmJit VeroScriptCpp)
		target_include_directories(VeroScript${_benchmark} PRIVATE Private
			"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Benchmarks/ReviewBaseline/2026-09-04")
		vs_apply_cxx_defaults(VeroScript${_benchmark})
		vs_enable_large_test_stack(VeroScript${_benchmark})
		vs_set_solution_folder(VeroScript${_benchmark} "Tests/Jit")
		if(WIN32 AND _benchmark MATCHES "SessionCacheResources|Replay$")
			target_link_libraries(VeroScript${_benchmark} PRIVATE Psapi)
		endif()
	endforeach()
	add_executable(VeroScriptAsmJitTests Private/Tests/VeroScriptAsmJitTests.cpp)
	target_link_libraries(VeroScriptAsmJitTests PRIVATE VeroScriptAsmJit)
	if(_VeroScript_use_asmjit)
		target_link_libraries(VeroScriptAsmJitTests PRIVATE ${_VeroScript_asmjit_link_target})
	endif()
	target_include_directories(VeroScriptAsmJitTests PRIVATE Private)
	get_target_property(_backend_definitions VeroScriptAsmJit COMPILE_DEFINITIONS)
	target_compile_definitions(VeroScriptAsmJitTests PRIVATE ${_backend_definitions})
	vs_apply_cxx_defaults(VeroScriptAsmJitTests)
	vs_enable_large_test_stack(VeroScriptAsmJitTests)
	vs_set_solution_folder(VeroScriptAsmJitTests "Tests")
	add_test(NAME VeroScriptAsmJitTests COMMAND VeroScriptAsmJitTests)
	add_executable(VeroScriptAsmJitProcessingTests Private/Tests/VeroScriptAsmJitProcessingTests.cpp)
	target_link_libraries(VeroScriptAsmJitProcessingTests PRIVATE VeroScriptAsmJit)
	target_compile_definitions(VeroScriptAsmJitProcessingTests PRIVATE ${_backend_definitions})
	vs_apply_cxx_defaults(VeroScriptAsmJitProcessingTests)
	vs_set_solution_folder(VeroScriptAsmJitProcessingTests "Tests/Jit")
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptAsmJitProcessingTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptAsmJitProcessingTests COMMAND VeroScriptAsmJitProcessingTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>")
	else()
		add_test(NAME VeroScriptAsmJitProcessingTests COMMAND VeroScriptAsmJitProcessingTests)
	endif()
	set_tests_properties(VeroScriptAsmJitProcessingTests PROPERTIES LABELS "VeroScript;Jit" TIMEOUT 60)
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		target_compile_definitions(VeroScriptAsmJitProcessingTests PRIVATE VEROSCRIPT_TEST_EMULATOR=1)
		set_tests_properties(VeroScriptAsmJitProcessingTests PROPERTIES TIMEOUT 120)
		if(TEST VeroScriptAsmJitPerformanceRegressionTests)
			set_tests_properties(VeroScriptAsmJitPerformanceRegressionTests PROPERTIES TIMEOUT 600)
		endif()
		if(TEST VeroScriptAsmJitInheritancePathTests)
			set_tests_properties(VeroScriptAsmJitInheritancePathTests PROPERTIES TIMEOUT 600)
		endif()
	endif()
	foreach(_test IN ITEMS VeroScriptPerfTests VeroScriptBackendDifferentialTests VeroScriptSessionJitCacheTests VeroScriptSessionReloadTests VeroScriptCppJitIntegrationTests VeroScriptAsmJitExceptionTests)
		add_executable(${_test} Private/Tests/${_test}.cpp)
		target_link_libraries(${_test} PRIVATE VeroScriptAsmJit VeroScriptCpp)
		target_include_directories(${_test} PRIVATE Private)
		target_compile_definitions(${_test} PRIVATE ${_backend_definitions})
		vs_apply_cxx_defaults(${_test})
		vs_enable_large_test_stack(${_test})
		vs_set_solution_folder(${_test} "Tests/Jit")
		add_test(NAME ${_test} COMMAND ${_test} WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
		set_property(TEST ${_test} APPEND PROPERTY LABELS VeroScript Jit)
	endforeach()
	if(TARGET VeroScriptAsmJitBackend)
		add_dependencies(VeroScriptSessionReloadTests VeroScriptAsmJitBackend)
		add_test(NAME VeroScriptSessionReloadDynamicTests COMMAND VeroScriptSessionReloadTests
			"$<TARGET_FILE:VeroScriptAsmJitBackend>" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
		set_tests_properties(VeroScriptSessionReloadDynamicTests PROPERTIES LABELS "VeroScript;Jit" TIMEOUT 60)
	endif()
	add_test(NAME VeroScriptBackendPerfTests COMMAND VeroScriptPerfTests --compare-backends WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
	set_tests_properties(VeroScriptPerfTests VeroScriptBackendPerfTests PROPERTIES RUN_SERIAL TRUE)
endif()
