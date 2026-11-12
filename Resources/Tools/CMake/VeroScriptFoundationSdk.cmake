# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

include_guard(GLOBAL)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Each module owns its dependency contract; this helper only installs the SDK.
function(vs_install_foundation_sdk module)
	set(_vs_package VeroScript${module})
	foreach(_vs_component ${module} ${module}Bindings)
		if(TARGET VeroScript${_vs_component})
			install(EXPORT VeroScriptVisual${_vs_component}Targets NAMESPACE VeroScript::
				DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/VeroScriptVisual" COMPONENT VeroScript${_vs_component})
		endif()
	endforeach()
	configure_package_config_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/${_vs_package}Config.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}Config.cmake"
		INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${_vs_package}")
	write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}ConfigVersion.cmake"
		VERSION 0.1.1 COMPATIBILITY ExactVersion)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}Config.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}ConfigVersion.cmake"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${_vs_package}" COMPONENT ${_vs_package})
	install(FILES "${VEROSCRIPT_MODULE_ROOT}/LICENSE" DESTINATION . COMPONENT ${_vs_package})
endfunction()

# Release evidence and the existing aggregate SDK span several independent modules.
function(vs_write_foundation_release)
	set(_vs_build_info "{\n")
	set(_vs_core_target "")
	foreach(_vs_component Platform Graphics Audio Sdl3 Image Font Stb FontBackend Gui RmlUi
			PlatformBindings GraphicsBindings AudioBindings ImageBindings FontBindings
			StbModule FontBackendModule GuiScript GuiToolsModule RmlUiModule)
		if(TARGET VeroScript${_vs_component})
			string(APPEND _vs_build_info "\"${_vs_component}\": \"$<TARGET_FILE:VeroScript${_vs_component}>\",\n")
		endif()
	endforeach()
	if(TARGET VeroScript)
		set(_vs_core_target VeroScript)
	elseif(TARGET VeroScript::VeroScript)
		set(_vs_core_target VeroScript::VeroScript)
	endif()
	if(_vs_core_target)
		string(APPEND _vs_build_info "\"Core\": \"$<TARGET_FILE:${_vs_core_target}>\",\n")
		get_target_property(_vs_core_imported ${_vs_core_target} IMPORTED)
		if(_vs_core_imported)
			get_target_property(_vs_core_includes ${_vs_core_target} INTERFACE_INCLUDE_DIRECTORIES)
			foreach(_vs_include IN LISTS _vs_core_includes)
				if(EXISTS "${_vs_include}/VeroScriptJitBuild.h")
					string(APPEND _vs_build_info "\"CoreBuildHeader\": \"${_vs_include}/VeroScriptJitBuild.h\",\n")
					break()
				endif()
			endforeach()
		else()
			get_target_property(_vs_core_binary ${_vs_core_target} BINARY_DIR)
			string(APPEND _vs_build_info "\"CoreBuildHeader\": \"${_vs_core_binary}/Generated/VeroScriptJitBuild.h\",\n")
		endif()
	endif()
	if(TARGET VeroScriptCpp)
		string(APPEND _vs_build_info "\"Cpp\": \"$<TARGET_FILE:VeroScriptCpp>\",\n")
	elseif(TARGET VeroScript::VeroScriptCpp)
		string(APPEND _vs_build_info "\"Cpp\": \"$<TARGET_FILE:VeroScript::VeroScriptCpp>\",\n")
	endif()
	# Test hosts choose their execution backend explicitly; it is not an SDK dependency.
	if(TARGET VeroScriptBytecodeVmBackend)
		string(APPEND _vs_build_info "\"TestVmBackend\": \"$<TARGET_FILE:VeroScriptBytecodeVmBackend>\",\n")
	elseif(VEROSCRIPT_TEST_VM_LIBRARY)
		get_filename_component(_vs_test_vm "${VEROSCRIPT_TEST_VM_LIBRARY}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
		file(TO_CMAKE_PATH "${_vs_test_vm}" _vs_test_vm)
		string(APPEND _vs_build_info "\"TestVmBackend\": \"${_vs_test_vm}\",\n")
	endif()
	string(APPEND _vs_build_info "\"version\": \"0.1.1\"\n}\n")
	file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/veroscript-foundation-$<CONFIG>.json" CONTENT "${_vs_build_info}")
endfunction()

function(vs_configure_foundation_release)
	get_property(_vs_scheduled GLOBAL PROPERTY VEROSCRIPT_FOUNDATION_RELEASE_SCHEDULED)
	if(NOT _vs_scheduled)
		set_property(GLOBAL PROPERTY VEROSCRIPT_FOUNDATION_RELEASE_SCHEDULED TRUE)
		cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL vs_write_foundation_release)
	endif()
	foreach(_vs_component IN LISTS VEROSCRIPT_VISUAL_COMPONENTS)
		if(_vs_component MATCHES "^(Platform|Graphics|Audio|Sdl3|PlatformBindings|GraphicsBindings|AudioBindings)$")
			# Child installers can recurse into other aggregates. Restore their
			# component selection while retaining the installed-file inventory.
			install(CODE "block(SCOPE_FOR VARIABLES PROPAGATE CMAKE_INSTALL_MANIFEST_FILES CMAKE_ABSOLUTE_DESTINATION_FILES)\nset(CMAKE_INSTALL_COMPONENT VeroScript${_vs_component})\ninclude(\"${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake\")\nendblock()"
				COMPONENT VeroScriptVisual)
		endif()
	endforeach()
endfunction()
