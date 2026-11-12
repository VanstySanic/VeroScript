# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)

# The public release version also drives service matching and installed SDK metadata.
set(_vs_gui_version_header "${CMAKE_CURRENT_SOURCE_DIR}/Public/VeroScriptGuiVersion.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_gui_version_header}")
file(READ "${_vs_gui_version_header}" _vs_gui_version_source)
string(REGEX MATCH "char Version\\[\\][ \t]*=[ \t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
	_vs_gui_version_declaration "${_vs_gui_version_source}")
if(NOT _vs_gui_version_declaration)
	message(FATAL_ERROR "VeroScriptGuiVersion.h must declare the GUI release version")
endif()
set(VEROSCRIPT_GUI_VERSION "${CMAKE_MATCH_1}")

# Standalone entry point for the Gui/RmlUi SDK.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(VeroScriptGuiFramework VERSION "${VEROSCRIPT_GUI_VERSION}" LANGUAGES C CXX)
	include(CTest)
	include(GNUInstallDirs)
	include(CMakePackageConfigHelpers)
	find_package(Threads REQUIRED)
	get_filename_component(VEROSCRIPT_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
	include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptHelpers.cmake")
	option(VEROSCRIPT_VISUAL_SCRIPT_BINDINGS "Build the public foundation script adapters" OFF)
	if(VEROSCRIPT_VISUAL_SCRIPT_BINDINGS)
		find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)
		if(NOT TARGET VeroScript)
			add_library(VeroScript ALIAS VeroScript::VeroScript)
		endif()
	endif()
	option(VEROSCRIPT_GUI_USE_VISUAL_SDK "Reuse an installed Visual SDK instead of building its public contracts" OFF)
	if(VEROSCRIPT_GUI_USE_VISUAL_SDK)
		find_package(VeroScriptVisual 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Platform Graphics)
	else()
		add_subdirectory("${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/VeroScriptPlatform" Platform)
		add_subdirectory("${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/VeroScriptGraphics" Graphics)
	endif()
	set(VEROSCRIPT_GUI_STANDALONE ON)
endif()

find_package(Threads REQUIRED)

foreach(_vs_resource Image Font)
	if(NOT TARGET VeroScript::${_vs_resource})
		add_subdirectory("${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/VeroScript${_vs_resource}" "${CMAKE_CURRENT_BINARY_DIR}/${_vs_resource}")
	endif()
	get_target_property(_vs_resource_imported VeroScript::${_vs_resource} IMPORTED)
	if(NOT _vs_resource_imported)
		get_target_property(_vs_resource_binary VeroScript::${_vs_resource} BINARY_DIR)
		# A GUI component is a usable bundle, including the separate resource packages.
		install(CODE "block(SCOPE_FOR VARIABLES PROPAGATE CMAKE_INSTALL_MANIFEST_FILES CMAKE_ABSOLUTE_DESTINATION_FILES)\nset(CMAKE_INSTALL_COMPONENT VeroScript${_vs_resource})\ninclude(\"${_vs_resource_binary}/cmake_install.cmake\")\nendblock()"
			COMPONENT VeroScriptGui)
		install(CODE "block(SCOPE_FOR VARIABLES PROPAGATE CMAKE_INSTALL_MANIFEST_FILES CMAKE_ABSOLUTE_DESTINATION_FILES)\nset(CMAKE_INSTALL_COMPONENT VeroScript${_vs_resource}Bindings)\ninclude(\"${_vs_resource_binary}/cmake_install.cmake\")\nendblock()"
			COMPONENT VeroScriptGui)
	endif()
endforeach()

option(VEROSCRIPT_GUI_BUILD_RESOURCE_BACKENDS "Build the default Stb and FontBackend resource providers" ON)
if(VEROSCRIPT_GUI_BUILD_RESOURCE_BACKENDS)
	foreach(_vs_backend Stb FontBackend)
		if(NOT TARGET VeroScript::${_vs_backend})
			add_subdirectory("${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/Backend/VeroScript${_vs_backend}" "${CMAKE_CURRENT_BINARY_DIR}/${_vs_backend}")
		endif()
		get_target_property(_vs_backend_imported VeroScript::${_vs_backend} IMPORTED)
		if(NOT _vs_backend_imported)
			get_target_property(_vs_backend_binary VeroScript::${_vs_backend} BINARY_DIR)
			install(CODE "block(SCOPE_FOR VARIABLES PROPAGATE CMAKE_INSTALL_MANIFEST_FILES CMAKE_ABSOLUTE_DESTINATION_FILES)\nset(CMAKE_INSTALL_COMPONENT VeroScript${_vs_backend})\ninclude(\"${_vs_backend_binary}/cmake_install.cmake\")\nendblock()"
				COMPONENT VeroScriptGui)
			install(CODE "block(SCOPE_FOR VARIABLES PROPAGATE CMAKE_INSTALL_MANIFEST_FILES CMAKE_ABSOLUTE_DESTINATION_FILES)\nset(CMAKE_INSTALL_COMPONENT VeroScript${_vs_backend}Bindings)\ninclude(\"${_vs_backend_binary}/cmake_install.cmake\")\nendblock()"
				COMPONENT VeroScriptGui)
		endif()
	endforeach()
endif()

add_library(VeroScriptGui STATIC
	Private/VeroScriptGuiControls.cpp Private/VeroScriptGuiAdditionalControls.cpp Private/VeroScriptGuiControlInput.cpp Private/VeroScriptGuiControlPaint.cpp
	Private/VeroScriptGuiPointer.cpp
	Private/VeroScriptGuiResources.cpp
	Private/VeroScriptGuiPaint.cpp
	Private/VeroScriptGuiAnimation.cpp
	Private/VeroScriptGuiApplication.cpp
	Private/VeroScriptGuiStyle.cpp
	Private/VeroScriptGuiTypes.cpp Private/VeroScriptGuiBackend.cpp Private/VeroScriptGuiEvent.cpp
	Private/VeroScriptGuiWidget.cpp Private/VeroScriptGuiContext.cpp Private/VeroScriptGuiMarkup.cpp
	Private/Compilation/VeroScriptGuiMarkupInternal.cpp Private/Compilation/VeroScriptGuiMarkupParser.cpp
	Private/Compilation/VeroScriptGuiStyleSheet.cpp Private/Compilation/VeroScriptGuiMarkupCompiler.cpp
	Private/Compilation/VeroScriptGuiMarkupRuntime.cpp)
add_library(VeroScript::Gui ALIAS VeroScriptGui)
vs_use_shared_private_headers(VeroScriptGui)
target_link_libraries(VeroScriptGui PUBLIC VeroScript::Platform VeroScript::Graphics VeroScript::Image VeroScript::Font Threads::Threads)
target_include_directories(VeroScriptGui PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/VeroScriptVisual>")
target_compile_features(VeroScriptGui PUBLIC cxx_std_17)
set_target_properties(VeroScriptGui PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Gui
	VERSION "${VEROSCRIPT_GUI_VERSION}")
vs_apply_cxx_defaults(VeroScriptGui)
vs_set_solution_folder(VeroScriptGui "Libraries/Gui")
install(TARGETS VeroScriptGui EXPORT VeroScriptGuiTargets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptGuiNative)
install(DIRECTORY Public/ DESTINATION include/VeroScriptVisual COMPONENT VeroScriptGuiNative FILES_MATCHING PATTERN "*.h"
	PATTERN "VeroScriptGuiScript.h" EXCLUDE PATTERN "VeroScriptGuiSchema.h" EXCLUDE
	PATTERN "VeroScriptGuiCompilation.h" EXCLUDE PATTERN "VeroScriptGuiLocalization.h" EXCLUDE
	PATTERN "VeroScriptGuiTools.h" EXCLUDE PATTERN "VeroScriptGuiServices.h" EXCLUDE
	PATTERN "VeroScriptGuiCommandLine.h" EXCLUDE PATTERN "VeroScriptGuiModule.h" EXCLUDE)
install(FILES README.md DESTINATION share/VeroScriptGui COMPONENT VeroScriptGuiNative)
install(FILES "${VEROSCRIPT_MODULE_ROOT}/LICENSE" DESTINATION licenses/VeroScript COMPONENT VeroScriptGuiNative)
install(DIRECTORY Resources/Docs/ DESTINATION share/VeroScriptGui/Resources/Docs COMPONENT VeroScriptGuiNative)
install(DIRECTORY Resources/Examples/ DESTINATION share/VeroScriptGui/Resources/Examples COMPONENT VeroScriptGuiNative)
install(DIRECTORY Resources/Tests/Installed/ DESTINATION share/VeroScriptGui/Resources/Tests/Installed COMPONENT VeroScriptGuiNative)
install(DIRECTORY Resources/Scripts/ DESTINATION share/VeroScriptGui/Resources/Scripts COMPONENT VeroScriptGuiNative)

option(VEROSCRIPT_GUI_SCRIPTING "Build the Core/C++ adapter for ML script blocks and expressions" OFF)
if(VEROSCRIPT_GUI_SCRIPTING)
	foreach(_vs_resource Image Font)
		if(NOT TARGET VeroScript::${_vs_resource}Bindings)
			find_package(VeroScript${_vs_resource} 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Bindings)
		endif()
	endforeach()
	if(TARGET VeroScriptCpp)
		set(_vs_gui_cpp VeroScriptCpp)
	elseif(TARGET VeroScript::VeroScriptCpp)
		set(_vs_gui_cpp VeroScript::VeroScriptCpp)
	else()
		find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
		set(_vs_gui_cpp VeroScript::VeroScriptCpp)
	endif()
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS Resources/Scripts/VeroScriptGui.vs)
	file(READ Resources/Scripts/VeroScriptGui.vs VEROSCRIPT_GUI_LIBRARY_SOURCE)
	file(READ Resources/Scripts/Gui/Widget.vs VEROSCRIPT_GUI_WIDGET_SOURCE)
	file(READ Resources/Scripts/Gui/Runtime.vs VEROSCRIPT_GUI_RUNTIME_SOURCE)
	file(READ Resources/Scripts/Gui/Binding.vs VEROSCRIPT_GUI_BINDING_SOURCE)
	file(READ Resources/Scripts/Gui/Controls.vs VEROSCRIPT_GUI_CONTROLS_SOURCE)
	file(READ Resources/Scripts/Gui/Resources.vs VEROSCRIPT_GUI_RESOURCES_SOURCE)
	file(READ Resources/Scripts/Gui/CompositeSupport.vs VEROSCRIPT_GUI_COMPOSITE_SOURCE)
	file(READ Resources/Scripts/Gui/Collections.vs VEROSCRIPT_GUI_COLLECTIONS_SOURCE)
	file(READ Resources/Scripts/Gui/Virtualization.vs VEROSCRIPT_GUI_VIRTUALIZATION_SOURCE)
	file(READ Resources/Scripts/Gui/Table.vs VEROSCRIPT_GUI_TABLE_SOURCE)
	file(READ Resources/Scripts/Gui/Navigation.vs VEROSCRIPT_GUI_NAVIGATION_SOURCE)
	file(READ Resources/Scripts/Gui/Menus.vs VEROSCRIPT_GUI_MENUS_SOURCE)
	file(READ Resources/Scripts/Gui/Dialogs.vs VEROSCRIPT_GUI_DIALOGS_SOURCE)
	set(_vs_gui_compilation_identity "")
	foreach(_vs_gui_source ../../../Shared/Private/VeroScriptFloatParsing.h
		../../../Shared/Private/VeroScriptFloatFormatting.h
		Private/Compilation/VeroScriptGuiCompilation.cpp
		Private/Compilation/VeroScriptGuiSchema.cpp Public/VeroScriptGuiSchema.h Public/VeroScriptGuiWidget.h
		Public/VeroScriptGuiContext.h Private/VeroScriptGuiContext.cpp Resources/Scripts/VeroScriptGui.vs
		Resources/Scripts/Gui/Widget.vs Resources/Scripts/Gui/Runtime.vs
		Resources/Scripts/Gui/Binding.vs Resources/Scripts/Gui/Resources.vs Resources/Scripts/Gui/Controls.vs
		Resources/Scripts/Gui/CompositeSupport.vs Resources/Scripts/Gui/Collections.vs Resources/Scripts/Gui/Table.vs
		Resources/Scripts/Gui/Virtualization.vs
		Resources/Scripts/Gui/Navigation.vs Resources/Scripts/Gui/Menus.vs Resources/Scripts/Gui/Dialogs.vs
		Public/VeroScriptGuiControls.h Private/VeroScriptGuiControlPaint.h Private/VeroScriptGuiControls.cpp
		Private/VeroScriptGuiControlInput.cpp Private/VeroScriptGuiAdditionalControls.cpp Private/VeroScriptGuiControlPaint.cpp
		Private/Compilation/VeroScriptGuiSource.cpp Private/Compilation/VeroScriptGuiSource.h
		Private/Compilation/VeroScriptGuiSourceImports.cpp Private/Compilation/VeroScriptGuiSourceImports.h
		Private/Bindings/VeroScriptGuiScriptLibrary.cpp Private/Bindings/VeroScriptGuiScriptLibrary.h
		Private/Compilation/VeroScriptGuiMarkupGeneration.cpp Private/Compilation/VeroScriptGuiMarkupGeneration.h
		Private/Compilation/VeroScriptGuiScriptExpression.cpp Private/Compilation/VeroScriptGuiScriptExpression.h
		Private/Compilation/VeroScriptGuiMarkupParser.cpp Private/Compilation/VeroScriptGuiMarkupCompiler.cpp
		Private/Compilation/VeroScriptGuiMarkupInternal.cpp Private/Compilation/VeroScriptGuiMarkupInternal.h
		Private/Compilation/VeroScriptGuiStyleSheet.cpp Public/VeroScriptGuiMarkup.h
		Private/VeroScriptGuiStyle.cpp Private/VeroScriptGuiTypes.cpp Public/VeroScriptGuiTypes.h
		Private/VeroScriptGuiEvent.cpp Public/VeroScriptGuiEvent.h
		Private/VeroScriptGuiPointer.cpp Public/VeroScriptGuiPointer.h
		Private/Tools/VeroScriptGuiArtifact.h
		Public/VeroScriptGuiStyle.h Public/VeroScriptGuiCompilation.h Public/VeroScriptGuiScript.h
		Public/VeroScriptGuiBackend.h Public/VeroScriptGuiResources.h Public/VeroScriptGuiPaint.h
		Private/Bindings/VeroScriptGuiScript.cpp Private/Bindings/VeroScriptGuiScriptInternal.h
		Private/Bindings/VeroScriptGuiRuntimeBindings.cpp Private/Bindings/VeroScriptGuiStyleBindings.cpp
		Private/Bindings/VeroScriptGuiResourceBindings.cpp Private/Bindings/VeroScriptGuiPaintBindings.cpp
		Private/Bindings/VeroScriptGuiControlBindings.cpp Private/Bindings/VeroScriptGuiLifecycleBindings.cpp
		Private/Bindings/VeroScriptGuiNotificationBindings.cpp)
		get_filename_component(_vs_gui_source_path "${CMAKE_CURRENT_SOURCE_DIR}/${_vs_gui_source}" ABSOLUTE)
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_gui_source_path}")
		file(SHA256 "${_vs_gui_source_path}" _vs_gui_source_hash)
		string(APPEND _vs_gui_compilation_identity "${_vs_gui_source}:${_vs_gui_source_hash}\n")
	endforeach()
	foreach(_vs_resource Image Font)
		foreach(_vs_resource_source Public/VeroScript${_vs_resource}.h Public/VeroScript${_vs_resource}Bindings.h Private/VeroScript${_vs_resource}Bindings.cpp)
			set(_vs_resource_path "${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/VeroScript${_vs_resource}/${_vs_resource_source}")
			set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_vs_resource_path}")
			file(SHA256 "${_vs_resource_path}" _vs_resource_hash)
			string(APPEND _vs_gui_compilation_identity "${_vs_resource}:${_vs_resource_source}:${_vs_resource_hash}\n")
		endforeach()
	endforeach()
	string(SHA256 VEROSCRIPT_GUI_COMPILATION_FINGERPRINT "${_vs_gui_compilation_identity}")
	configure_file(Resources/Tools/CMake/VeroScriptGuiEmbeddedLibrary.h.in
		"${CMAKE_CURRENT_BINARY_DIR}/Generated/VeroScriptGuiEmbeddedLibrary.h" @ONLY)
	add_library(VeroScriptGuiScript STATIC Private/Bindings/VeroScriptGuiScript.cpp
		Private/Bindings/VeroScriptGuiScriptInternal.cpp
		Private/Tools/VeroScriptGuiTools.cpp
		Private/Tools/VeroScriptGuiCommandLine.cpp
		Private/Tools/VeroScriptGuiArtifact.cpp
		Private/Bindings/VeroScriptGuiLocalization.cpp
		Private/Bindings/VeroScriptGuiServices.cpp
		Private/Bindings/VeroScriptGuiStyleBindings.cpp
		Private/Bindings/VeroScriptGuiResourceBindings.cpp
		Private/Bindings/VeroScriptGuiRuntimeBindings.cpp Private/Bindings/VeroScriptGuiEventBindings.cpp Private/Bindings/VeroScriptGuiControlBindings.cpp
		Private/Bindings/VeroScriptGuiLifecycleBindings.cpp
		Private/Bindings/VeroScriptGuiNotificationBindings.cpp
		Private/Bindings/VeroScriptGuiPaintBindings.cpp
		Private/Bindings/VeroScriptGuiScriptLibrary.cpp
		Private/Bindings/VeroScriptGuiScriptCatalog.cpp
		Private/Compilation/VeroScriptGuiSchema.cpp
		Private/Compilation/VeroScriptGuiSource.cpp
		Private/Compilation/VeroScriptGuiSourceImports.cpp
		Private/Compilation/VeroScriptGuiMarkupGeneration.cpp
		Private/Compilation/VeroScriptGuiCompilation.cpp
		Private/Compilation/VeroScriptGuiScriptExpression.cpp)
	add_library(VeroScript::GuiScript ALIAS VeroScriptGuiScript)
	target_link_libraries(VeroScriptGuiScript PUBLIC VeroScriptGui VeroScript::ImageBindings VeroScript::FontBindings
		"$<BUILD_INTERFACE:${_vs_gui_cpp}>" "$<INSTALL_INTERFACE:VeroScript::VeroScriptCpp>")
	target_include_directories(VeroScriptGuiScript PRIVATE Private "${CMAKE_CURRENT_BINARY_DIR}/Generated")
	vs_use_shared_private_headers(VeroScriptGuiScript)
	set_target_properties(VeroScriptGuiScript PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME GuiScript)
	vs_apply_cxx_defaults(VeroScriptGuiScript)
	vs_set_solution_folder(VeroScriptGuiScript "Libraries/Gui")
	install(TARGETS VeroScriptGuiScript EXPORT VeroScriptGuiBindingsTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptGuiBindings)
	install(FILES Public/VeroScriptGuiScript.h Public/VeroScriptGuiSchema.h Public/VeroScriptGuiCompilation.h
		Public/VeroScriptGuiLocalization.h
		Public/VeroScriptGuiTools.h
		Public/VeroScriptGuiServices.h
		Public/VeroScriptGuiCommandLine.h
		Public/VeroScriptGuiModule.h
		DESTINATION include/VeroScriptVisual COMPONENT VeroScriptGuiBindings)

	set(_vs_gui_static_tools_default OFF)
	if(CMAKE_SYSTEM_NAME MATCHES "^(Android|iOS|Emscripten)$" OR
		(MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
		set(_vs_gui_static_tools_default ON)
	endif()
	option(VEROSCRIPT_GUI_STATIC_TOOLS "Build a statically registered GUI CLI adapter" ${_vs_gui_static_tools_default})
	if(VEROSCRIPT_GUI_STATIC_TOOLS)
		add_library(VeroScriptGuiToolsModule STATIC Private/Tools/VeroScriptGuiModule.cpp)
		target_compile_definitions(VeroScriptGuiToolsModule PUBLIC VEROSCRIPT_GUI_STATIC_TOOLS=1)
	else()
		add_library(VeroScriptGuiToolsModule SHARED Private/Tools/VeroScriptGuiModule.cpp)
	endif()
	target_compile_definitions(VeroScriptGuiToolsModule PRIVATE VEROSCRIPT_GUI_MODULE_EXPORTS=1)
	target_compile_features(VeroScriptGuiToolsModule PRIVATE cxx_std_17)
	target_include_directories(VeroScriptGuiToolsModule PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>" "$<INSTALL_INTERFACE:include/VeroScriptVisual>")
	# This forwarding adapter uses the host's typed task service and never embeds Core.
	target_include_directories(VeroScriptGuiToolsModule PRIVATE "$<TARGET_PROPERTY:${_vs_gui_cpp},INTERFACE_INCLUDE_DIRECTORIES>")
	if(TARGET VeroScript)
		target_include_directories(VeroScriptGuiToolsModule PRIVATE "$<TARGET_PROPERTY:VeroScript,INTERFACE_INCLUDE_DIRECTORIES>")
	elseif(TARGET VeroScriptCore)
		target_include_directories(VeroScriptGuiToolsModule PRIVATE "$<TARGET_PROPERTY:VeroScriptCore,INTERFACE_INCLUDE_DIRECTORIES>")
	elseif(TARGET VeroScript::Core)
		target_include_directories(VeroScriptGuiToolsModule PRIVATE "$<TARGET_PROPERTY:VeroScript::Core,INTERFACE_INCLUDE_DIRECTORIES>")
	else()
		target_include_directories(VeroScriptGuiToolsModule PRIVATE "$<TARGET_PROPERTY:VeroScript::VeroScript,INTERFACE_INCLUDE_DIRECTORIES>")
	endif()
	set_target_properties(VeroScriptGuiToolsModule PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME GuiToolsModule)
	vs_apply_cxx_defaults(VeroScriptGuiToolsModule)
	vs_install_windows_runtime(bin VeroScriptGuiBindings)
	vs_set_solution_folder(VeroScriptGuiToolsModule "Libraries/Gui")
	install(TARGETS VeroScriptGuiToolsModule EXPORT VeroScriptGuiBindingsTargets
		RUNTIME DESTINATION bin COMPONENT VeroScriptGuiBindings
		LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptGuiBindings
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptGuiBindings)
endif()

if(BUILD_TESTING)
	add_executable(VeroScriptGuiApplicationTests Resources/Tests/ApplicationTests.cpp)
	target_link_libraries(VeroScriptGuiApplicationTests PRIVATE VeroScriptGui)
	vs_apply_cxx_defaults(VeroScriptGuiApplicationTests)
	add_test(NAME VeroScriptGuiApplicationTests COMMAND VeroScriptGuiApplicationTests)
	set_tests_properties(VeroScriptGuiApplicationTests PROPERTIES TIMEOUT 30)
	if(VEROSCRIPT_GUI_SCRIPTING)
		# The source-backed application check compiles the complete GUI script library.
		# Match the cold-compilation budget of the other script integration checks.
		set_tests_properties(VeroScriptGuiApplicationTests PROPERTIES TIMEOUT 600)
		if(TARGET VeroScript::Stb AND TARGET VeroScript::FontBackend)
			add_executable(VeroScriptGuiResourceModulesTests Resources/Tests/ResourceModulesTests.cpp)
			target_link_libraries(VeroScriptGuiResourceModulesTests PRIVATE VeroScript::ImageBindings VeroScript::FontBindings VeroScript::Stb VeroScript::FontBackend ${_vs_gui_cpp})
			vs_apply_cxx_defaults(VeroScriptGuiResourceModulesTests)
			add_test(NAME VeroScriptGuiResourceModulesTests COMMAND VeroScriptGuiResourceModulesTests)
		endif()
		add_executable(VeroScriptGuiToolsTests Resources/Tests/ToolsTests.cpp)
		target_link_libraries(VeroScriptGuiToolsTests PRIVATE VeroScriptGuiScript)
		target_compile_definitions(VeroScriptGuiToolsTests PRIVATE
			VEROSCRIPT_GUI_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
		vs_apply_cxx_defaults(VeroScriptGuiToolsTests)
		if(NOT VEROSCRIPT_GUI_STATIC_TOOLS)
			add_dependencies(VeroScriptGuiToolsTests VeroScriptGuiToolsModule)
		endif()
		foreach(_vs_gui_tool_case Content Check Artifact Host SourceImports SourceDetection BackendSelection)
			if(NOT VEROSCRIPT_GUI_STATIC_TOOLS)
				add_test(NAME "VeroScriptGuiToolsTests.${_vs_gui_tool_case}" COMMAND VeroScriptGuiToolsTests
					"$<TARGET_FILE:VeroScriptGuiToolsModule>" --scenario "${_vs_gui_tool_case}")
			else()
				add_test(NAME "VeroScriptGuiToolsTests.${_vs_gui_tool_case}" COMMAND VeroScriptGuiToolsTests
					--scenario "${_vs_gui_tool_case}")
			endif()
			# Each group retains at most three independent cold compilations of the full library.
			set_tests_properties("VeroScriptGuiToolsTests.${_vs_gui_tool_case}" PROPERTIES TIMEOUT 1200)
		endforeach()
		set_tests_properties(VeroScriptGuiToolsTests.SourceDetection PROPERTIES TIMEOUT 30)
		set_tests_properties(VeroScriptGuiToolsTests.BackendSelection PROPERTIES TIMEOUT 60)
		add_executable(VeroScriptGuiMarkupGenerationTests Resources/Tests/MarkupGenerationTests.cpp)
		target_link_libraries(VeroScriptGuiMarkupGenerationTests PRIVATE VeroScriptGuiScript)
		target_compile_definitions(VeroScriptGuiMarkupGenerationTests PRIVATE
			VEROSCRIPT_GUI_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
		vs_apply_cxx_defaults(VeroScriptGuiMarkupGenerationTests)
		# Keep independent cold-compilation groups within their own integration deadline.
		foreach(_vs_gui_generation_case Runtime ScriptDiagnostics ExpressionDiagnostics Artifact)
			add_test(NAME "VeroScriptGuiMarkupGenerationTests.${_vs_gui_generation_case}"
				COMMAND VeroScriptGuiMarkupGenerationTests "${_vs_gui_generation_case}")
			set_tests_properties("VeroScriptGuiMarkupGenerationTests.${_vs_gui_generation_case}"
				PROPERTIES TIMEOUT 1200)
		endforeach()
		target_link_libraries(VeroScriptGuiApplicationTests PRIVATE VeroScriptGuiScript)
		target_compile_definitions(VeroScriptGuiApplicationTests PRIVATE VEROSCRIPT_GUI_SCRIPTING_TESTS=1
			VEROSCRIPT_GUI_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
		add_executable(VeroScriptGuiSchemaTests Resources/Tests/SchemaTests.cpp)
		target_link_libraries(VeroScriptGuiSchemaTests PRIVATE VeroScriptGuiScript)
		target_include_directories(VeroScriptGuiSchemaTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
		target_compile_definitions(VeroScriptGuiSchemaTests PRIVATE
			VEROSCRIPT_GUI_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
		vs_apply_cxx_defaults(VeroScriptGuiSchemaTests)
		add_test(NAME VeroScriptGuiSchemaTests COMMAND VeroScriptGuiSchemaTests)
		set_tests_properties(VeroScriptGuiSchemaTests PROPERTIES TIMEOUT 60)
		add_executable(VeroScriptGuiScriptExpressionTests Resources/Tests/ScriptExpressionTests.cpp)
		target_link_libraries(VeroScriptGuiScriptExpressionTests PRIVATE VeroScriptGuiScript)
		target_include_directories(VeroScriptGuiScriptExpressionTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
		vs_apply_cxx_defaults(VeroScriptGuiScriptExpressionTests)
		add_test(NAME VeroScriptGuiScriptExpressionTests COMMAND VeroScriptGuiScriptExpressionTests)
		set_tests_properties(VeroScriptGuiScriptExpressionTests PROPERTIES TIMEOUT 30)
		add_executable(VeroScriptGuiScriptTests Resources/Tests/ScriptTests.cpp)
		target_link_libraries(VeroScriptGuiScriptTests PRIVATE VeroScriptGuiScript)
		target_include_directories(VeroScriptGuiScriptTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
		target_compile_definitions(VeroScriptGuiScriptTests PRIVATE
			VEROSCRIPT_GUI_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
			VEROSCRIPT_GUI_MARKUP_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/Resources/Examples/Markup")
		vs_apply_cxx_defaults(VeroScriptGuiScriptTests)
		add_executable(VeroScriptGuiNotificationTests Resources/Tests/NotificationTests.cpp)
		target_link_libraries(VeroScriptGuiNotificationTests PRIVATE VeroScriptGuiScript)
		target_compile_definitions(VeroScriptGuiNotificationTests PRIVATE
			VEROSCRIPT_GUI_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
		vs_apply_cxx_defaults(VeroScriptGuiNotificationTests)
		add_test(NAME VeroScriptGuiNotificationTests COMMAND VeroScriptGuiNotificationTests)
		set_tests_properties(VeroScriptGuiNotificationTests PROPERTIES TIMEOUT 600)
		# Independent script scenarios can run in parallel without one aggregate cold-compilation deadline.
		foreach(_vs_gui_script_case NativeTree SourceSnapshots ConstQueries BackendSelection PublicRuntime Synchronization ComponentProperties ExternalState Components
				Environment Attributes Files ReservedNames Inline)
			add_test(NAME "VeroScriptGuiScriptTests.${_vs_gui_script_case}"
				COMMAND VeroScriptGuiScriptTests "${_vs_gui_script_case}")
			set_tests_properties("VeroScriptGuiScriptTests.${_vs_gui_script_case}" PROPERTIES TIMEOUT 600)
		endforeach()
		# These scenarios create isolated instances from multiple sources, each with cold compilation.
		set_tests_properties(VeroScriptGuiScriptTests.Environment VeroScriptGuiScriptTests.ExternalState
			PROPERTIES TIMEOUT 1200)
		# Inline also compiles an invalid third source before checking resource collection.
		set_tests_properties(VeroScriptGuiScriptTests.Inline PROPERTIES TIMEOUT 1800)
		if(TARGET VeroScriptAsmJitBackend)
			add_dependencies(VeroScriptGuiNotificationTests VeroScriptAsmJitBackend)
			add_test(NAME VeroScriptGuiNotificationTests.Jit
				COMMAND VeroScriptGuiNotificationTests "$<TARGET_FILE:VeroScriptAsmJitBackend>")
			set_tests_properties(VeroScriptGuiNotificationTests.Jit PROPERTIES
				TIMEOUT 600 LABELS "VeroScript;Gui;Jit")
			add_dependencies(VeroScriptGuiScriptTests VeroScriptAsmJitBackend)
			add_test(NAME VeroScriptGuiScriptTests.JitSynchronization
				COMMAND VeroScriptGuiScriptTests JitSynchronization "$<TARGET_FILE:VeroScriptAsmJitBackend>")
			set_tests_properties(VeroScriptGuiScriptTests.JitSynchronization PROPERTIES
				TIMEOUT 600 LABELS "VeroScript;Gui;Jit")
		endif()
	endif()
	add_executable(VeroScriptGuiMarkupTests Resources/Tests/MarkupTests.cpp)
	target_link_libraries(VeroScriptGuiMarkupTests PRIVATE VeroScriptGui)
	target_compile_definitions(VeroScriptGuiMarkupTests PRIVATE
		VEROSCRIPT_GUI_MARKUP_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/Resources/Examples/Markup")
	vs_apply_cxx_defaults(VeroScriptGuiMarkupTests)
	add_test(NAME VeroScriptGuiMarkupTests COMMAND VeroScriptGuiMarkupTests)
	set_tests_properties(VeroScriptGuiMarkupTests PROPERTIES TIMEOUT 30)
	add_executable(VeroScriptGuiTests Resources/Tests/GuiTests.cpp)
	target_link_libraries(VeroScriptGuiTests PRIVATE VeroScriptGui)
	vs_apply_cxx_defaults(VeroScriptGuiTests)
	add_test(NAME VeroScriptGuiTests COMMAND VeroScriptGuiTests)
	set_tests_properties(VeroScriptGuiTests PROPERTIES TIMEOUT 30)
	add_executable(VeroScriptGuiLifecycleTests Resources/Tests/LifecycleTests.cpp)
	target_link_libraries(VeroScriptGuiLifecycleTests PRIVATE VeroScriptGui)
	vs_apply_cxx_defaults(VeroScriptGuiLifecycleTests)
	add_test(NAME VeroScriptGuiLifecycleTests COMMAND VeroScriptGuiLifecycleTests)
	set_tests_properties(VeroScriptGuiLifecycleTests PROPERTIES TIMEOUT 30)
endif()

if(VEROSCRIPT_GUI_STANDALONE)
	option(VEROSCRIPT_GUI_BUILD_RMLUI "Build the vendored RmlUi adapter" ON)
	if(VEROSCRIPT_GUI_BUILD_RMLUI)
		add_subdirectory("${VEROSCRIPT_MODULE_ROOT}/Modules/Multimedia/Backend/VeroScriptRmlUi" RmlUi)
	endif()
	foreach(_vs_contract Platform Graphics)
		if(TARGET VeroScript${_vs_contract})
			get_target_property(_vs_contract_binary VeroScript${_vs_contract} BINARY_DIR)
			install(CODE "block(SCOPE_FOR VARIABLES PROPAGATE CMAKE_INSTALL_MANIFEST_FILES CMAKE_ABSOLUTE_DESTINATION_FILES)\nset(CMAKE_INSTALL_COMPONENT VeroScript${_vs_contract})\ninclude(\"${_vs_contract_binary}/cmake_install.cmake\")\nendblock()"
				COMPONENT VeroScriptGui)
		endif()
	endforeach()
	include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptFoundationSdk.cmake")
	vs_configure_foundation_release()
endif()
include(Resources/Tools/CMake/InstallSdk.cmake)

if(VEROSCRIPT_GUI_STANDALONE AND BUILD_TESTING AND VEROSCRIPT_GUI_SCRIPTING)
	include("${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptVmTests.cmake")
endif()
