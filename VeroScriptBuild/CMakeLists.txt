# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

file(GLOB _build_sources CONFIGURE_DEPENDS Private/*.cpp)
add_library(VeroScriptBuild STATIC ${_build_sources})
target_include_directories(VeroScriptBuild PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/Build>"
	PRIVATE ThirdParty)
target_link_libraries(VeroScriptBuild PUBLIC VeroScriptApplication)
target_compile_features(VeroScriptBuild PUBLIC cxx_std_17)
set_target_properties(VeroScriptBuild PROPERTIES POSITION_INDEPENDENT_CODE ON)
vs_apply_cxx_defaults(VeroScriptBuild)
vs_set_solution_folder(VeroScriptBuild "Libraries")
include(CMakePackageConfigHelpers)
configure_package_config_file(Resources/Tools/CMake/VeroScriptBuildConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBuildConfig.cmake" INSTALL_DESTINATION lib/cmake/VeroScriptBuild)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBuildConfigVersion.cmake"
	VERSION 0.1.1 COMPATIBILITY ExactVersion)
install(TARGETS VeroScriptBuild EXPORT VeroScriptBuildTargets ARCHIVE DESTINATION lib COMPONENT VeroScriptBuild)
install(EXPORT VeroScriptBuildTargets NAMESPACE VeroScript:: DESTINATION lib/cmake/VeroScriptBuild COMPONENT VeroScriptBuild)
install(DIRECTORY Public/ DESTINATION include/Build COMPONENT VeroScriptBuild)
install(DIRECTORY Resources/Schemas DESTINATION share/VeroScript/build/Resources COMPONENT VeroScriptBuild)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBuildConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptBuildConfigVersion.cmake"
	DESTINATION lib/cmake/VeroScriptBuild COMPONENT VeroScriptBuild)
install(FILES ThirdParty/json/LICENSE.MIT DESTINATION licenses/build/json COMPONENT VeroScriptBuild)
install(FILES ThirdParty/sources.json DESTINATION licenses/build COMPONENT VeroScriptBuild)
if(BUILD_TESTING)
	add_executable(VeroScriptBuildTests Private/Tests/VeroScriptBuildTests.cpp)
	target_include_directories(VeroScriptBuildTests PRIVATE ThirdParty)
	target_link_libraries(VeroScriptBuildTests PRIVATE VeroScriptBuild VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptBuildTests)
	vs_set_solution_folder(VeroScriptBuildTests "Tests/Build")
	add_test(NAME VeroScriptBuildTests COMMAND VeroScriptBuildTests "${CMAKE_CURRENT_BINARY_DIR}/Tests/$<CONFIG>")
	set_tests_properties(VeroScriptBuildTests PROPERTIES LABELS "VeroScript;Build" TIMEOUT 180)
endif()
