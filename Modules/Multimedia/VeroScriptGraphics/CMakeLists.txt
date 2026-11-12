# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

add_library(VeroScriptGraphics STATIC Private/VeroScriptGraphics.cpp Private/VeroScriptGraphics2D.cpp
	Private/VeroScriptGraphicsComposition2D.cpp)
add_library(VeroScript::Graphics ALIAS VeroScriptGraphics)
target_include_directories(VeroScriptGraphics PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/VeroScriptVisual>")
target_compile_features(VeroScriptGraphics PUBLIC cxx_std_17)
set_target_properties(VeroScriptGraphics PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Graphics
	CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
vs_apply_cxx_defaults(VeroScriptGraphics)
vs_set_solution_folder(VeroScriptGraphics "Libraries/Media")
install(TARGETS VeroScriptGraphics EXPORT VeroScriptVisualGraphicsTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptGraphics)
install(DIRECTORY Public/ DESTINATION include/VeroScriptVisual COMPONENT VeroScriptGraphics
	FILES_MATCHING PATTERN "*.h" PATTERN "*Bindings.h" EXCLUDE)

if(VEROSCRIPT_VISUAL_SCRIPT_BINDINGS)
	add_library(VeroScriptGraphicsBindings STATIC Private/Bindings/VeroScriptGraphicsBindings.cpp
		Private/Bindings/VeroScriptGraphicsBindingState.cpp
		Private/Bindings/VeroScriptGraphicsCompositionBindings.cpp Private/Bindings/VeroScriptGraphicsAsyncBindings.cpp)
	add_library(VeroScript::GraphicsBindings ALIAS VeroScriptGraphicsBindings)
	target_link_libraries(VeroScriptGraphicsBindings PUBLIC VeroScriptGraphics VeroScriptPlatformBindings)
	set_target_properties(VeroScriptGraphicsBindings PROPERTIES EXPORT_NAME GraphicsBindings
		POSITION_INDEPENDENT_CODE ON)
	vs_apply_cxx_defaults(VeroScriptGraphicsBindings)
	install(TARGETS VeroScriptGraphicsBindings EXPORT VeroScriptVisualGraphicsBindingsTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptGraphicsBindings)
	install(FILES Public/VeroScriptGraphicsBindings.h DESTINATION include/VeroScriptVisual
		COMPONENT VeroScriptGraphicsBindings)
endif()

include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptFoundationSdk.cmake")
vs_install_foundation_sdk(Graphics)
