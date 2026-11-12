# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT COMMAND vs_collect_module_sources)
	function(vs_collect_module_sources out_var)
		file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
			LIST_DIRECTORIES false
			"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp")
		set(${out_var} "${_sources}" PARENT_SCOPE)
	endfunction()
	function(vs_apply_cxx_defaults target_name)
		if(MSVC)
			target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
		else()
			target_compile_options(${target_name} PRIVATE -Wall -Wextra)
		endif()
	endfunction()
	function(vs_set_solution_folder target_name folder)
		if(TARGET ${target_name})
			set_target_properties(${target_name} PROPERTIES FOLDER "${folder}")
		endif()
	endfunction()
endif()

vs_collect_module_sources(_VeroScriptInterop_sources)
list(FILTER _VeroScriptInterop_sources EXCLUDE REGEX "/Tests/")
file(GLOB_RECURSE _VeroScriptInterop_headers CONFIGURE_DEPENDS
	LIST_DIRECTORIES false
	"${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h")

# Keep the Windows module-definition ABI manifest synchronized with the public
# C header. The manifest is checked on every host so omissions are caught before
# a 32-bit Windows build is attempted.
file(STRINGS Public/VeroScriptInterop.h _VeroScriptInterop_api_lines
	REGEX "VS_INTEROP_API.*vs_interop_[A-Za-z0-9_]+\\(")
set(_VeroScriptInterop_header_symbols)
foreach(_VeroScriptInterop_api_line IN LISTS _VeroScriptInterop_api_lines)
	string(REGEX REPLACE
		".*(vs_interop_[A-Za-z0-9_]+)[ \t]*\\(.*" "\\1"
		_VeroScriptInterop_symbol "${_VeroScriptInterop_api_line}")
	list(APPEND _VeroScriptInterop_header_symbols "${_VeroScriptInterop_symbol}")
endforeach()
file(STRINGS Private/VeroScriptInterop.def _VeroScriptInterop_def_lines
	REGEX "^[ \t]+vs_interop_[A-Za-z0-9_]+[ \t]*$")
set(_VeroScriptInterop_def_symbols)
foreach(_VeroScriptInterop_def_line IN LISTS _VeroScriptInterop_def_lines)
	string(STRIP "${_VeroScriptInterop_def_line}" _VeroScriptInterop_symbol)
	list(APPEND _VeroScriptInterop_def_symbols "${_VeroScriptInterop_symbol}")
endforeach()
list(SORT _VeroScriptInterop_header_symbols)
list(SORT _VeroScriptInterop_def_symbols)
if(NOT "${_VeroScriptInterop_header_symbols}" STREQUAL "${_VeroScriptInterop_def_symbols}")
	message(FATAL_ERROR
		"VeroScriptInterop.def must list every public C ABI function exactly once\n"
		"Header: ${_VeroScriptInterop_header_symbols}\n"
		"DEF: ${_VeroScriptInterop_def_symbols}")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	set(_VeroScriptInterop_default_static ON)
else()
	set(_VeroScriptInterop_default_static OFF)
endif()
option(VEROSCRIPT_INTEROP_STATIC "Build the C embedding ABI as a static library" ${_VeroScriptInterop_default_static})
if(VEROSCRIPT_INTEROP_STATIC)
	set(_VeroScriptInterop_library_type STATIC)
else()
	set(_VeroScriptInterop_library_type SHARED)
endif()
add_library(VeroScriptInterop ${_VeroScriptInterop_library_type}
	${_VeroScriptInterop_sources}
	${_VeroScriptInterop_headers})
if(COMMAND vs_use_shared_private_headers)
	vs_use_shared_private_headers(VeroScriptInterop)
else()
	target_include_directories(VeroScriptInterop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/Private")
endif()
option(VEROSCRIPT_INTEROP_DEFAULT_VM "Assemble the official static VM in the Interop product" ${VEROSCRIPT_BUILD_VM_BACKEND})
if(VEROSCRIPT_INTEROP_DEFAULT_VM)
	if(NOT TARGET VeroScriptBytecodeVm)
		message(FATAL_ERROR "VEROSCRIPT_INTEROP_DEFAULT_VM requires VeroScriptBytecodeVm")
	endif()
	target_link_libraries(VeroScriptInterop PRIVATE VeroScriptBytecodeVm)
	target_compile_definitions(VeroScriptInterop PRIVATE VEROSCRIPT_WITH_BYTECODE_VM=1)
endif()
if(TARGET VeroScriptApplication)
	target_link_libraries(VeroScriptInterop PRIVATE VeroScriptApplication)
	target_compile_definitions(VeroScriptInterop PRIVATE VEROSCRIPT_WITH_APPLICATION=1)
endif()
if(VEROSCRIPT_INTEROP_STATIC)
	target_compile_definitions(VeroScriptInterop PUBLIC VS_INTEROP_STATIC=1)
elseif(WIN32)
	# Freeze undecorated C ABI names as well as the calling convention. This is
	# significant on 32-bit Windows, where __cdecl otherwise decorates exports.
	target_sources(VeroScriptInterop PRIVATE
		Private/VeroScriptInterop.def)
elseif(APPLE)
	# Visibility annotations cannot hide every standard-library instantiation.
	# Use the same ABI manifest to constrain the final Mach-O export table.
	set(_VeroScriptInterop_export_file
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptInterop.exports")
	set(_VeroScriptInterop_export_content "")
	foreach(_VeroScriptInterop_symbol IN LISTS _VeroScriptInterop_def_symbols)
		string(APPEND _VeroScriptInterop_export_content "_${_VeroScriptInterop_symbol}\n")
	endforeach()
	# The native SDK bridge intentionally uses the matching C++ SDK ABI. Both
	# Clang on Mach-O and GCC/Clang on ELF use this Itanium name prefix; keep
	# this one entry visible without publishing the linked core's C++ symbols.
	string(APPEND _VeroScriptInterop_export_content
		"__ZN10VeroScript7Interop33CreateAnnotationProcessingServiceE*\n")
	string(APPEND _VeroScriptInterop_export_content
		"__ZN10VeroScript7Interop30CreateCompileProcessingServiceE*\n")
	string(APPEND _VeroScriptInterop_export_content
		"__ZN10VeroScript7Interop23CreateModuleEnvironmentE*\n")
	file(GENERATE OUTPUT "${_VeroScriptInterop_export_file}"
		CONTENT "${_VeroScriptInterop_export_content}")
	target_link_options(VeroScriptInterop PRIVATE
		"LINKER:-exported_symbols_list,${_VeroScriptInterop_export_file}")
elseif(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
	# Restrict ELF exports even when linked archives contain explicitly visible
	# C++ template instantiations. An anonymous map preserves unversioned ABI names.
	set(_VeroScriptInterop_export_file
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptInterop.map")
	set(_VeroScriptInterop_export_content "{\n  global:\n")
	foreach(_VeroScriptInterop_symbol IN LISTS _VeroScriptInterop_def_symbols)
		string(APPEND _VeroScriptInterop_export_content "    ${_VeroScriptInterop_symbol};\n")
	endforeach()
	string(APPEND _VeroScriptInterop_export_content
		"    _ZN10VeroScript7Interop33CreateAnnotationProcessingServiceE*;\n")
	string(APPEND _VeroScriptInterop_export_content
		"    _ZN10VeroScript7Interop30CreateCompileProcessingServiceE*;\n")
	string(APPEND _VeroScriptInterop_export_content
		"    _ZN10VeroScript7Interop23CreateModuleEnvironmentE*;\n")
	string(APPEND _VeroScriptInterop_export_content "  local: *;\n};\n")
	file(GENERATE OUTPUT "${_VeroScriptInterop_export_file}"
		CONTENT "${_VeroScriptInterop_export_content}")
	target_link_options(VeroScriptInterop PRIVATE
		"LINKER:--version-script,${_VeroScriptInterop_export_file}")
endif()
if(DEFINED _VeroScriptInterop_export_file)
	set_property(TARGET VeroScriptInterop APPEND PROPERTY LINK_DEPENDS
		"${_VeroScriptInterop_export_file}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Public/VeroScriptInterop.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/Private/VeroScriptInterop.def")
if(NOT VEROSCRIPT_INTEROP_STATIC)
	target_compile_definitions(VeroScriptInterop PRIVATE VS_INTEROP_BUILD=1)
endif()

if(ANDROID AND NOT VEROSCRIPT_INTEROP_STATIC)
	# Both ELF segment alignment and APK zip alignment need separate verification.
	target_link_options(VeroScriptInterop PRIVATE "LINKER:-z,max-page-size=16384")
endif()
target_compile_features(VeroScriptInterop PRIVATE cxx_std_17)
target_link_libraries(VeroScriptInterop PRIVATE VeroScriptCpp)
target_include_directories(VeroScriptInterop
	PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
		"$<INSTALL_INTERFACE:include/VeroScript>"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
vs_apply_cxx_defaults(VeroScriptInterop)
vs_set_solution_folder(VeroScriptInterop "Libraries")

# The shared ABI links the static core. ELF requires every contributing object
# to be position-independent; setting the dependency targets here keeps that
# policy local to builds which actually include Interop.
if(NOT WIN32)
	foreach(_VeroScriptInterop_pic_target IN ITEMS VeroScript VeroScriptCpp VeroScriptBytecodeVm VeroScriptAsmJit asmjit)
		if(TARGET ${_VeroScriptInterop_pic_target})
			set_property(TARGET ${_VeroScriptInterop_pic_target}
				PROPERTY POSITION_INDEPENDENT_CODE ON)
			# These are static archives linked into the process ABI. Compile their
			# objects hidden as well, otherwise ELF/Mach-O may publish internal C++
			# symbols even though the Interop target itself uses hidden visibility.
			set_target_properties(${_VeroScriptInterop_pic_target} PROPERTIES
				CXX_VISIBILITY_PRESET hidden
				VISIBILITY_INLINES_HIDDEN ON)
		endif()
	endforeach()
endif()

if(NOT WIN32)
	set_target_properties(VeroScriptInterop PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON)
endif()

if(BUILD_TESTING)
	enable_language(C)
	file(STRINGS Private/Tests/VeroScriptInteropCHeaderTest.c _VeroScriptInterop_c_symbols
		REGEX "^[ \t]+VS_REQUIRE_SYMBOL\\(vs_interop_[A-Za-z0-9_]+\\);")
	list(TRANSFORM _VeroScriptInterop_c_symbols REPLACE
		".*VS_REQUIRE_SYMBOL\\((vs_interop_[A-Za-z0-9_]+)\\);.*" "\\1")
	list(SORT _VeroScriptInterop_c_symbols)
	if(NOT "${_VeroScriptInterop_c_symbols}" STREQUAL "${_VeroScriptInterop_header_symbols}")
		message(FATAL_ERROR "The C11 header test must reference every public ABI function exactly once")
	endif()
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptInteropCHeaderTest.c")
	add_executable(VeroScriptInteropTests
		Private/Tests/VeroScriptInteropTests.cpp)
	target_link_libraries(VeroScriptInteropTests PRIVATE VeroScriptInterop)
	target_compile_definitions(VeroScriptInteropTests PRIVATE
		VEROSCRIPT_STDLIB_DIR="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	target_compile_features(VeroScriptInteropTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptInteropTests)
	vs_set_solution_folder(VeroScriptInteropTests "Tests")
	add_test(NAME VeroScriptInteropTests COMMAND VeroScriptInteropTests)

	add_executable(VeroScriptInteropVmBackendTests Private/Tests/VeroScriptInteropVmBackendTests.cpp)
	target_link_libraries(VeroScriptInteropVmBackendTests PRIVATE VeroScriptInterop)
	target_compile_definitions(VeroScriptInteropVmBackendTests PRIVATE
		VEROSCRIPT_INTEROP_TEST_DEFAULT_VM=$<BOOL:${VEROSCRIPT_INTEROP_DEFAULT_VM}>)
	vs_apply_cxx_defaults(VeroScriptInteropVmBackendTests)
	vs_set_solution_folder(VeroScriptInteropVmBackendTests "Tests")
	set(_VeroScriptInterop_vm_test_arguments)
	if(TARGET VeroScriptBytecodeVmBackend)
		add_dependencies(VeroScriptInteropVmBackendTests VeroScriptBytecodeVmBackend)
		list(APPEND _VeroScriptInterop_vm_test_arguments "$<TARGET_FILE:VeroScriptBytecodeVmBackend>")
	endif()
	add_test(NAME VeroScriptInteropVmBackendTests COMMAND VeroScriptInteropVmBackendTests
		${_VeroScriptInterop_vm_test_arguments})
	add_executable(VeroScriptInteropLanguageStateTests Private/Tests/VeroScriptInteropLanguageStateTests.cpp)
	target_link_libraries(VeroScriptInteropLanguageStateTests PRIVATE VeroScriptInterop)
	target_compile_features(VeroScriptInteropLanguageStateTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptInteropLanguageStateTests)
	vs_set_solution_folder(VeroScriptInteropLanguageStateTests "Tests")
	add_test(NAME VeroScriptInteropLanguageStateTests COMMAND VeroScriptInteropLanguageStateTests)
	if(TARGET VeroScriptBytecodeVmBackend)
		add_dependencies(VeroScriptInteropLanguageStateTests VeroScriptBytecodeVmBackend)
		add_test(NAME VeroScriptInteropLanguageStateVmTests COMMAND VeroScriptInteropLanguageStateTests
			"$<TARGET_FILE:VeroScriptBytecodeVmBackend>")
	endif()

	add_executable(VeroScriptInteropExtendedTests
		Private/Tests/VeroScriptInteropExtendedTests.cpp)
	target_link_libraries(VeroScriptInteropExtendedTests PRIVATE VeroScriptInterop)
	target_compile_features(VeroScriptInteropExtendedTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptInteropExtendedTests)
	vs_set_solution_folder(VeroScriptInteropExtendedTests "Tests")
	add_test(NAME VeroScriptInteropExtendedTests COMMAND VeroScriptInteropExtendedTests)

	add_executable(VeroScriptInteropTypeTests
		Private/Tests/VeroScriptInteropTypeTests.cpp)
	target_link_libraries(VeroScriptInteropTypeTests PRIVATE VeroScriptInterop)
	target_compile_features(VeroScriptInteropTypeTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptInteropTypeTests)
	vs_set_solution_folder(VeroScriptInteropTypeTests "Tests")
	add_test(NAME VeroScriptInteropTypeTests COMMAND VeroScriptInteropTypeTests)

	add_executable(VeroScriptInteropMemoryTests Private/Tests/VeroScriptInteropMemoryTests.cpp)
	target_link_libraries(VeroScriptInteropMemoryTests PRIVATE VeroScriptInterop)
	target_compile_features(VeroScriptInteropMemoryTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptInteropMemoryTests)
	vs_set_solution_folder(VeroScriptInteropMemoryTests "Tests")
	add_test(NAME VeroScriptInteropMemoryTests COMMAND VeroScriptInteropMemoryTests)
	add_executable(VeroScriptInteropNumericsTests Private/Tests/VeroScriptInteropNumericsTests.cpp)
	add_executable(VeroScriptInteropVariantTests Private/Tests/VeroScriptInteropVariantTests.cpp)
	target_link_libraries(VeroScriptInteropVariantTests PRIVATE VeroScriptInterop)
	vs_apply_cxx_defaults(VeroScriptInteropVariantTests)
	vs_set_solution_folder(VeroScriptInteropVariantTests "Tests")
	add_test(NAME VeroScriptInteropVariantTests COMMAND VeroScriptInteropVariantTests)
	target_link_libraries(VeroScriptInteropNumericsTests PRIVATE VeroScriptInterop)
	target_compile_features(VeroScriptInteropNumericsTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptInteropNumericsTests)
	vs_set_solution_folder(VeroScriptInteropNumericsTests "Tests")
	add_test(NAME VeroScriptInteropNumericsTests COMMAND VeroScriptInteropNumericsTests)
	add_executable(VeroScriptInteropScriptCompileTests Private/Tests/VeroScriptInteropScriptCompileTests.cpp)
	target_link_libraries(VeroScriptInteropScriptCompileTests PRIVATE VeroScriptInterop)
	target_compile_features(VeroScriptInteropScriptCompileTests PRIVATE cxx_std_17)
	vs_apply_cxx_defaults(VeroScriptInteropScriptCompileTests)
	vs_set_solution_folder(VeroScriptInteropScriptCompileTests "Tests")
	add_test(NAME VeroScriptInteropScriptCompileTests COMMAND VeroScriptInteropScriptCompileTests)
	if(TARGET VeroScriptBytecodeVmBackend)
		add_dependencies(VeroScriptInteropScriptCompileTests VeroScriptBytecodeVmBackend)
		add_test(NAME VeroScriptInteropScriptCompileVmTests COMMAND VeroScriptInteropScriptCompileTests
			"$<TARGET_FILE:VeroScriptBytecodeVmBackend>")
	endif()
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		target_compile_definitions(VeroScriptInteropScriptCompileTests PRIVATE VEROSCRIPT_TEST_EMULATOR=1)
	endif()

	add_executable(VeroScriptInteropCHeaderTest
		Private/Tests/VeroScriptInteropCHeaderTest.c)
	target_link_libraries(VeroScriptInteropCHeaderTest PRIVATE VeroScriptInterop)
	target_include_directories(VeroScriptInteropCHeaderTest PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/Public")
	set_target_properties(VeroScriptInteropCHeaderTest PROPERTIES
		C_STANDARD 11
		C_STANDARD_REQUIRED ON)
	vs_set_solution_folder(VeroScriptInteropCHeaderTest "Tests")
	add_test(NAME VeroScriptInteropCHeaderTest COMMAND VeroScriptInteropCHeaderTest)
	if(NOT CMAKE_CROSSCOMPILING AND NOT VEROSCRIPT_INTEROP_STATIC)
		set(_VeroScriptInterop_native_sdk_jit_target "")
		if(TARGET VeroScriptAsmJitBackend)
			set(_VeroScriptInterop_native_sdk_jit_target VeroScriptAsmJitBackend)
		endif()
		add_test(NAME VeroScriptInteropNativeSdkTests COMMAND "${CMAKE_COMMAND}"
			"-DBUILD=${CMAKE_BINARY_DIR}"
			"-DJIT_BACKEND_TARGET=${_VeroScriptInterop_native_sdk_jit_target}"
			"-DCONFIG=$<CONFIG>"
			"-DWORK=${CMAKE_CURRENT_BINARY_DIR}/native-sdk-test"
			"-DCORE_INCLUDE=${VEROSCRIPT_MODULE_ROOT}/VeroScript/Public"
			"-DCORE_GENERATED=$<TARGET_PROPERTY:VeroScript,BINARY_DIR>/Generated"
			"-DCORE_LIBRARY=$<TARGET_FILE:VeroScript>"
			"-DTEST_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Private/Tests/VeroScriptInteropNativeSdkTests.cpp"
			"-DGENERATOR=${CMAKE_GENERATOR}"
			"-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
			"-DCTEST=${CMAKE_CTEST_COMMAND}"
			-P "${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptInteropNativeSdkTest.cmake")
		set_tests_properties(VeroScriptInteropNativeSdkTests PROPERTIES LABELS "VeroScript;Interop;SDK")
	endif()

	if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT VEROSCRIPT_INTEROP_STATIC)
		add_executable(VeroScriptInteropBoundaryTests
			Private/Tests/VeroScriptInteropBoundaryTests.cpp)
		target_link_libraries(VeroScriptInteropBoundaryTests PRIVATE VeroScriptInterop)
		target_compile_features(VeroScriptInteropBoundaryTests PRIVATE cxx_std_17)
		target_link_options(VeroScriptInteropBoundaryTests PRIVATE "LINKER:--export-dynamic")
		vs_apply_cxx_defaults(VeroScriptInteropBoundaryTests)
		vs_set_solution_folder(VeroScriptInteropBoundaryTests "Tests")
		add_test(NAME VeroScriptInteropBoundaryTests COMMAND VeroScriptInteropBoundaryTests)
	endif()
endif()

include(GNUInstallDirs)
vs_localization_roots(_VeroScriptInterop_localization_roots "${CMAKE_SOURCE_DIR}" interop)
foreach(_VeroScriptInterop_localization_root IN LISTS _VeroScriptInterop_localization_roots)
	install(DIRECTORY "${_VeroScriptInterop_localization_root}/"
		DESTINATION "${CMAKE_INSTALL_BINDIR}/localization"
		COMPONENT VeroScriptInterop)
endforeach()
if(NOT VEROSCRIPT_INTEROP_STATIC)
	include(CMakePackageConfigHelpers)
	set(_VeroScriptInterop_cmake_install_dir
		"${CMAKE_INSTALL_LIBDIR}/cmake/VeroScriptInterop")
	configure_package_config_file(
		Resources/Tools/CMake/VeroScriptInteropConfig.cmake.in
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptInteropConfig.cmake"
		INSTALL_DESTINATION "${_VeroScriptInterop_cmake_install_dir}")
	write_basic_package_version_file(
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptInteropConfigVersion.cmake"
		VERSION 0.1.1
		COMPATIBILITY SameMajorVersion)
	install(TARGETS VeroScriptInterop
		EXPORT VeroScriptInteropTargets
		RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT VeroScriptInterop
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptInterop
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptInterop)
	install(FILES Public/VeroScriptInterop.h Public/VeroScriptInteropAnnotation.h Public/VeroScriptInteropCompileProcessing.h Public/VeroScriptInteropModuleEnvironment.h
		DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/VeroScript"
		COMPONENT VeroScriptInterop)
	install(EXPORT VeroScriptInteropTargets
		FILE VeroScriptInteropTargets.cmake
		NAMESPACE VeroScript::
		DESTINATION "${_VeroScriptInterop_cmake_install_dir}"
		COMPONENT VeroScriptInterop)
	install(FILES
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptInteropConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptInteropConfigVersion.cmake"
		DESTINATION "${_VeroScriptInterop_cmake_install_dir}"
		COMPONENT VeroScriptInterop)

# The Interop component is documented and consumed as a standalone SDK payload.
# Keep its notices self-contained instead of relying on another install component.
	install(FILES
		"${VEROSCRIPT_MODULE_ROOT}/LICENSE"
		DESTINATION "."
		COMPONENT VeroScriptInterop)
	if(TARGET VeroScriptApplication)
		set(_VeroScriptInterop_application_vendor
			"${VEROSCRIPT_MODULE_ROOT}/VeroScriptApplication/ThirdParty")
		foreach(_VeroScriptInterop_notice IN ITEMS
			"miniz/LICENSE" "json/LICENSE.MIT" "sha256/LICENSE" "sources.json")
			get_filename_component(_VeroScriptInterop_notice_directory
				"${_VeroScriptInterop_notice}" DIRECTORY)
			install(FILES
				"${_VeroScriptInterop_application_vendor}/${_VeroScriptInterop_notice}"
				DESTINATION "licenses/application/${_VeroScriptInterop_notice_directory}"
				COMPONENT VeroScriptInterop)
		endforeach()
	endif()
endif()



if(BUILD_TESTING AND TARGET VeroScriptAsmJitBackend)
	foreach(_VeroScriptInterop_jit_test IN ITEMS
		VeroScriptInteropTests VeroScriptInteropExtendedTests VeroScriptInteropMemoryTests
		VeroScriptInteropLanguageStateTests)
		if(TARGET "${_VeroScriptInterop_jit_test}")
			add_dependencies("${_VeroScriptInterop_jit_test}" VeroScriptAsmJitBackend)
		endif()
	endforeach()
	set_property(TEST VeroScriptInteropTests VeroScriptInteropExtendedTests VeroScriptInteropMemoryTests VeroScriptInteropLanguageStateTests APPEND PROPERTY ENVIRONMENT
		"VEROSCRIPT_TEST_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>")
	if(TARGET VeroScriptBytecodeVmBackend)
		set_property(TEST VeroScriptInteropLanguageStateVmTests APPEND PROPERTY ENVIRONMENT
			"VEROSCRIPT_TEST_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>")
	endif()
	if(NOT CMAKE_CROSSCOMPILING AND NOT VEROSCRIPT_INTEROP_STATIC)
		set_property(TEST VeroScriptInteropNativeSdkTests APPEND PROPERTY ENVIRONMENT
			"VEROSCRIPT_TEST_JIT_BACKEND=$<TARGET_FILE:VeroScriptAsmJitBackend>")
	endif()
endif()
