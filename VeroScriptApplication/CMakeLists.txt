# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

enable_language(C)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
set(_application_vendor "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")
add_library(VeroScriptApplicationZip STATIC
	"${_application_vendor}/miniz/miniz.c" "${_application_vendor}/miniz/miniz_tdef.c"
	"${_application_vendor}/miniz/miniz_tinfl.c" "${_application_vendor}/miniz/miniz_zip.c")
target_include_directories(VeroScriptApplicationZip PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VeroScriptApplicationZip PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(VeroScriptApplicationZip PROPERTIES POSITION_INDEPENDENT_CODE ON)
file(GLOB _application_sources CONFIGURE_DEPENDS "Private/*.cpp")
add_library(VeroScriptApplication STATIC ${_application_sources})
vs_use_shared_private_headers(VeroScriptApplication)
target_compile_features(VeroScriptApplication PUBLIC cxx_std_17)
target_include_directories(VeroScriptApplication PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/Application>"
	PRIVATE "${_application_vendor}" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(VeroScriptApplication PUBLIC "$<BUILD_INTERFACE:VeroScript>"
	"$<INSTALL_INTERFACE:VeroScript::VeroScript>" PRIVATE VeroScriptApplicationZip "${CMAKE_DL_LIBS}")
set_target_properties(VeroScriptApplication PROPERTIES POSITION_INDEPENDENT_CODE ON)
vs_apply_cxx_defaults(VeroScriptApplication)
vs_set_solution_folder(VeroScriptApplication "Libraries")
if(WIN32)
	target_link_libraries(VeroScriptApplication PRIVATE shell32 ole32)
endif()
if(VEROSCRIPT_BUILD_PACKAGE_RUNTIME OR VEROSCRIPT_BUILD_PACKAGE_MANAGER)
	add_library(VeroScriptApplicationPackages STATIC Private/Adapters/VeroScriptApplicationPackages.cpp)
	target_include_directories(VeroScriptApplicationPackages PRIVATE "${_application_vendor}")
	target_link_libraries(VeroScriptApplicationPackages PUBLIC VeroScriptApplication VeroScriptPackageRuntime)
	target_compile_features(VeroScriptApplicationPackages PUBLIC cxx_std_17)
	set_target_properties(VeroScriptApplicationPackages PROPERTIES POSITION_INDEPENDENT_CODE ON)
	vs_apply_cxx_defaults(VeroScriptApplicationPackages)
	vs_set_solution_folder(VeroScriptApplicationPackages "Libraries")
endif()
include(CMakePackageConfigHelpers)
configure_package_config_file(Resources/Tools/CMake/VeroScriptApplicationConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationConfig.cmake"
	INSTALL_DESTINATION lib/cmake/VeroScriptApplication)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationConfigVersion.cmake"
	VERSION 0.1.1 COMPATIBILITY ExactVersion)
install(TARGETS VeroScriptApplication VeroScriptApplicationZip EXPORT VeroScriptApplicationTargets
	ARCHIVE DESTINATION lib COMPONENT VeroScriptApplication)
install(EXPORT VeroScriptApplicationTargets NAMESPACE VeroScript:: DESTINATION lib/cmake/VeroScriptApplication
	COMPONENT VeroScriptApplication)
install(CODE "
    file(SHA256 \"$<TARGET_FILE:VeroScript>\" _application_core_sha)
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/lib/cmake/VeroScriptApplication/VeroScriptApplicationCoreBuild.cmake\"
        \"set(VeroScriptApplication_CORE_BUILD_ID \\\"\${_application_core_sha}\\\")\\n\")
" COMPONENT VeroScriptApplication)
install(DIRECTORY Public/ DESTINATION include/Application COMPONENT VeroScriptApplication
	PATTERN VeroScriptApplicationPackages.h EXCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationConfigVersion.cmake"
	DESTINATION lib/cmake/VeroScriptApplication COMPONENT VeroScriptApplication)
install(FILES "${_application_vendor}/miniz/LICENSE" DESTINATION licenses/application/miniz COMPONENT VeroScriptApplication)
install(FILES "${_application_vendor}/json/LICENSE.MIT" DESTINATION licenses/application/json COMPONENT VeroScriptApplication)
install(FILES "${_application_vendor}/sha256/LICENSE" DESTINATION licenses/application/sha256 COMPONENT VeroScriptApplication)
install(FILES "${_application_vendor}/sources.json" DESTINATION licenses/application COMPONENT VeroScriptApplication)
install(DIRECTORY Resources/Schemas DESTINATION share/VeroScript/application/Resources COMPONENT VeroScriptApplication)
if(TARGET VeroScriptApplicationPackages)
	install(FILES Public/VeroScriptApplicationPackages.h DESTINATION include/Application COMPONENT VeroScriptApplicationPackages)
	configure_package_config_file(Resources/Tools/CMake/VeroScriptApplicationPackagesConfig.cmake.in
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationPackagesConfig.cmake" INSTALL_DESTINATION lib/cmake/VeroScriptApplicationPackages)
	write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationPackagesConfigVersion.cmake"
		VERSION 0.1.1 COMPATIBILITY ExactVersion)
	install(TARGETS VeroScriptApplicationPackages EXPORT VeroScriptApplicationPackagesTargets
		ARCHIVE DESTINATION lib COMPONENT VeroScriptApplicationPackages)
	install(EXPORT VeroScriptApplicationPackagesTargets NAMESPACE VeroScript:: DESTINATION lib/cmake/VeroScriptApplicationPackages COMPONENT VeroScriptApplicationPackages)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationPackagesConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptApplicationPackagesConfigVersion.cmake"
		DESTINATION lib/cmake/VeroScriptApplicationPackages COMPONENT VeroScriptApplicationPackages)
endif()

if(BUILD_TESTING)
	add_executable(VeroScriptApplicationTests Private/Tests/VeroScriptApplicationTests.cpp)
	target_include_directories(VeroScriptApplicationTests PRIVATE "${_application_vendor}")
	target_link_libraries(VeroScriptApplicationTests PRIVATE VeroScriptApplication VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptApplicationTests)
	vs_set_solution_folder(VeroScriptApplicationTests "Tests/Application")
	set(VEROSCRIPT_TEST_SODIUM_LIBRARY "" CACHE FILEPATH "Explicit libsodium library used by protection integration tests")
	add_test(NAME VeroScriptApplicationTests COMMAND VeroScriptApplicationTests
		"${CMAKE_CURRENT_BINARY_DIR}/Tests/$<CONFIG>" ${VEROSCRIPT_TEST_SODIUM_LIBRARY})
	set_tests_properties(VeroScriptApplicationTests PROPERTIES LABELS "VeroScript;Application" TIMEOUT 120)
	if(VEROSCRIPT_BUILD_PACKAGE_MANAGER AND TARGET VeroScriptNativeExtensionFixture AND NOT CMAKE_CROSSCOMPILING)
		add_executable(VeroScriptApplicationPackageServicesTests Private/Tests/PackageServicesTests.cpp)
		target_include_directories(VeroScriptApplicationPackageServicesTests PRIVATE "${_application_vendor}")
		target_link_libraries(VeroScriptApplicationPackageServicesTests PRIVATE
			VeroScriptApplicationPackages VeroScriptPackageManager)
		add_dependencies(VeroScriptApplicationPackageServicesTests VeroScriptNativeExtensionFixture)
		vs_apply_cxx_defaults(VeroScriptApplicationPackageServicesTests)
		vs_set_solution_folder(VeroScriptApplicationPackageServicesTests "Tests/Application")
		add_test(NAME VeroScriptApplicationPackageServicesTests COMMAND VeroScriptApplicationPackageServicesTests
			"$<TARGET_FILE:VeroScriptNativeExtensionFixture>" "${CMAKE_CURRENT_BINARY_DIR}/Tests/$<CONFIG>")
		set_tests_properties(VeroScriptApplicationPackageServicesTests PROPERTIES LABELS "VeroScript;Application;Gui" TIMEOUT 60)
	endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux|Darwin)$")
	foreach(_application_tool IN ITEMS VeroScriptRuntime VeroScriptLauncher)
		add_executable(${_application_tool} Private/Tools/VeroScriptRuntimeMain.cpp)
		vs_use_product_icon(${_application_tool})
		target_link_libraries(${_application_tool} PRIVATE VeroScriptApplication VeroScriptCpp)
		if(TARGET VeroScriptBytecodeVmBackend)
			target_include_directories(${_application_tool} PRIVATE "${VEROSCRIPT_MODULE_ROOT}/Modules/Execution/VeroScriptBytecodeVm/Public")
			target_compile_definitions(${_application_tool} PRIVATE VEROSCRIPT_RUNTIME_HAS_VM=1)
			add_dependencies(${_application_tool} VeroScriptRuntimeVmAssets)
		endif()
		if(VEROSCRIPT_BUILD_PACKAGE_RUNTIME OR VEROSCRIPT_BUILD_PACKAGE_MANAGER)
			target_link_libraries(${_application_tool} PRIVATE VeroScriptApplicationPackages)
			target_compile_definitions(${_application_tool} PRIVATE VEROSCRIPT_APPLICATION_PACKAGES=1)
		endif()
		if(MINGW)
			target_link_options(${_application_tool} PRIVATE -municode)
		endif()
		vs_apply_cxx_defaults(${_application_tool})
		vs_set_solution_folder(${_application_tool} "Application")
	endforeach()
	set_target_properties(VeroScriptRuntime PROPERTIES OUTPUT_NAME veroscript-runtime)
	set_target_properties(VeroScriptLauncher PROPERTIES OUTPUT_NAME veroscript-launcher)
	target_compile_definitions(VeroScriptLauncher PRIVATE VEROSCRIPT_APPLICATION_BOOTSTRAP=1)
	install(TARGETS VeroScriptRuntime VeroScriptLauncher RUNTIME DESTINATION bin COMPONENT VeroScriptApplicationRuntime)
endif()
