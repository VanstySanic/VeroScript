# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

add_library(VeroScriptPlatform STATIC Private/VeroScriptPlatformApi.cpp)
add_library(VeroScript::Platform ALIAS VeroScriptPlatform)
target_include_directories(VeroScriptPlatform PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/VeroScriptVisual>")
target_compile_features(VeroScriptPlatform PUBLIC cxx_std_17)
set_target_properties(VeroScriptPlatform PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Platform
	CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
vs_apply_cxx_defaults(VeroScriptPlatform)
vs_set_solution_folder(VeroScriptPlatform "Libraries/Platform")
install(TARGETS VeroScriptPlatform EXPORT VeroScriptVisualPlatformTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptPlatform)
install(DIRECTORY Public/ DESTINATION include/VeroScriptVisual COMPONENT VeroScriptPlatform
	FILES_MATCHING PATTERN "*.h" PATTERN "*Bindings.h" EXCLUDE)

if(VEROSCRIPT_VISUAL_SCRIPT_BINDINGS)
	add_library(VeroScriptPlatformBindings STATIC Private/Bindings/VeroScriptPlatformBindings.cpp
		Private/Bindings/VeroScriptPlatformBindingState.cpp Private/Bindings/VeroScriptPlatformInputBindings.cpp
		Private/Bindings/VeroScriptPlatformWindowBindings.cpp Private/Bindings/VeroScriptPlatformControllerBindings.cpp)
	add_library(VeroScript::PlatformBindings ALIAS VeroScriptPlatformBindings)
	target_link_libraries(VeroScriptPlatformBindings PUBLIC VeroScriptPlatform
		"$<BUILD_INTERFACE:VeroScript>" "$<INSTALL_INTERFACE:VeroScript::Core>")
	set_target_properties(VeroScriptPlatformBindings PROPERTIES EXPORT_NAME PlatformBindings
		POSITION_INDEPENDENT_CODE ON)
	vs_apply_cxx_defaults(VeroScriptPlatformBindings)
	install(TARGETS VeroScriptPlatformBindings EXPORT VeroScriptVisualPlatformBindingsTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptPlatformBindings)
	install(FILES Public/VeroScriptPlatformBindings.h DESTINATION include/VeroScriptVisual
		COMPONENT VeroScriptPlatformBindings)
endif()

include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptFoundationSdk.cmake")
vs_install_foundation_sdk(Platform)
