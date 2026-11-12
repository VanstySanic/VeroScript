# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

add_library(VeroScriptAudio STATIC Private/VeroScriptAudio.cpp)
add_library(VeroScript::Audio ALIAS VeroScriptAudio)
target_include_directories(VeroScriptAudio PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/VeroScriptVisual>")
target_compile_features(VeroScriptAudio PUBLIC cxx_std_17)
set_target_properties(VeroScriptAudio PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Audio
	CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
vs_apply_cxx_defaults(VeroScriptAudio)
vs_set_solution_folder(VeroScriptAudio "Libraries/Media")
install(TARGETS VeroScriptAudio EXPORT VeroScriptVisualAudioTargets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptAudio)
install(DIRECTORY Public/ DESTINATION include/VeroScriptVisual COMPONENT VeroScriptAudio
	FILES_MATCHING PATTERN "*.h" PATTERN "*Bindings.h" EXCLUDE)
if(VEROSCRIPT_VISUAL_SCRIPT_BINDINGS)
	add_library(VeroScriptAudioBindings STATIC Private/Bindings/VeroScriptAudioBindings.cpp
		Private/Bindings/VeroScriptAudioBindingState.cpp)
	add_library(VeroScript::AudioBindings ALIAS VeroScriptAudioBindings)
	target_link_libraries(VeroScriptAudioBindings PUBLIC VeroScriptAudio
		"$<BUILD_INTERFACE:VeroScript>" "$<INSTALL_INTERFACE:VeroScript::Core>")
	set_target_properties(VeroScriptAudioBindings PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME AudioBindings)
	vs_apply_cxx_defaults(VeroScriptAudioBindings)
	install(TARGETS VeroScriptAudioBindings EXPORT VeroScriptVisualAudioBindingsTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptAudioBindings)
	install(FILES Public/VeroScriptAudioBindings.h DESTINATION include/VeroScriptVisual
		COMPONENT VeroScriptAudioBindings)
endif()

include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptFoundationSdk.cmake")
vs_install_foundation_sdk(Audio)
