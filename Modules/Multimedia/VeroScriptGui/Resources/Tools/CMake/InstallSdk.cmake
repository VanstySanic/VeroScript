# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(EXPORT VeroScriptGuiTargets NAMESPACE VeroScript::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/VeroScriptGui" COMPONENT VeroScriptGuiNative)
if(TARGET VeroScriptGuiScript)
	install(EXPORT VeroScriptGuiBindingsTargets NAMESPACE VeroScript::
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/VeroScriptGui" COMPONENT VeroScriptGuiBindings)
endif()
foreach(_vs_package VeroScriptGui VeroScriptGuiFramework)
	configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/${_vs_package}Config.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}Config.cmake"
		INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${_vs_package}")
	write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}ConfigVersion.cmake"
		VERSION "${VEROSCRIPT_GUI_VERSION}" COMPATIBILITY ExactVersion)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}Config.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/${_vs_package}ConfigVersion.cmake"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${_vs_package}" COMPONENT VeroScriptGuiNative)
endforeach()
install(FILES "${VEROSCRIPT_MODULE_ROOT}/LICENSE" DESTINATION . COMPONENT VeroScriptGuiNative)

# Keep the existing aggregate component usable for embedding hosts. Formal
# releases install the native/bindings components separately and pin dependencies.
get_target_property(_vs_gui_binary VeroScriptGui BINARY_DIR)
foreach(_vs_component VeroScriptGuiNative VeroScriptGuiBindings)
	install(CODE "block(SCOPE_FOR VARIABLES PROPAGATE CMAKE_INSTALL_MANIFEST_FILES CMAKE_ABSOLUTE_DESTINATION_FILES)\nset(CMAKE_INSTALL_COMPONENT ${_vs_component})\ninclude(\"${_vs_gui_binary}/cmake_install.cmake\")\nendblock()"
		COMPONENT VeroScriptGui)
endforeach()
