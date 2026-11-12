# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(VEROSCRIPT_JSON_BUILD_BINDINGS AND TARGET VeroScript)
	file(GLOB_RECURSE _json_identity_inputs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Private/Binding/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/Dom/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Private/Text/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/PointerPatch/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Private/Internal/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/Schema/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/jsoncons/include/*.hpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/Mapping/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.inl"
		"${CMAKE_CURRENT_SOURCE_DIR}/Private/Binding/*.inl"
		"${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/quickjs-regexp/*" "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/patches/*"
		"${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/idna/*.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/*.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tools/CMake/*.in")
	get_filename_component(_vs_json_float_header "${CMAKE_CURRENT_SOURCE_DIR}/../../../Shared/Private/VeroScriptFloatParsing.h" ABSOLUTE)
	get_filename_component(_vs_json_format_header "${CMAKE_CURRENT_SOURCE_DIR}/../../../Shared/Private/VeroScriptFloatFormatting.h" ABSOLUTE)
	list(APPEND _json_identity_inputs "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/sources.json"
		"${_vs_json_float_header}"
		"${_vs_json_format_header}"
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/json.vs"
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/json-containers.vs")
	list(SORT _json_identity_inputs)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_json_identity_inputs})
	set(_json_identity "VeroScriptJson:0.1.1:${CMAKE_SYSTEM_NAME}:${CMAKE_SYSTEM_PROCESSOR}:${CMAKE_CXX_COMPILER_ID}:${CMAKE_CXX_COMPILER_VERSION}:${CMAKE_BUILD_TYPE}:${CMAKE_CXX_FLAGS}:${CMAKE_CXX_FLAGS_RELEASE}:${CMAKE_MSVC_RUNTIME_LIBRARY}")
	foreach(_input IN LISTS _json_identity_inputs)
		file(READ "${_input}" _text)
		string(REPLACE "\r\n" "\n" _text "${_text}")
		string(SHA256 _hash "${_text}")
		string(APPEND _json_identity ":${_hash}")
	endforeach()
	string(SHA256 VEROSCRIPT_JSON_IMPLEMENTATION_ID "${_json_identity}")
	configure_file(Resources/Tools/CMake/VeroScriptJsonBuildInfo.h.in Generated/VeroScriptJsonBuildInfo.h @ONLY)
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/json.vs" VEROSCRIPT_JSON_MAPPING_SCRIPT)
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Scripts/json-containers.vs" _json_containers_script)
	string(APPEND VEROSCRIPT_JSON_MAPPING_SCRIPT "\n${_json_containers_script}")
	configure_file(Resources/Tools/CMake/VeroScriptJsonMappingScript.h.in Generated/VeroScriptJsonMappingScript.h @ONLY)
	add_library(VeroScriptJsonBindings STATIC Private/Binding/VeroScriptJsonModule.cpp
		Private/Binding/JsonStreamBinding.cpp Private/Binding/JsonBuilderBinding.cpp Private/Binding/JsonOptionBinding.cpp
		Private/Binding/JsonSchemaBinding.cpp Private/Binding/JsonScriptMapping.cpp Private/Binding/JsonMappingProcessor.cpp
		Private/Binding/JsonValueBinding.cpp)
	vs_use_shared_private_headers(VeroScriptJsonBindings)
	target_link_libraries(VeroScriptJsonBindings PUBLIC VeroScriptJson)
	target_link_libraries(VeroScriptJsonBindings PUBLIC "$<INSTALL_INTERFACE:VeroScript::VeroScript>")
	target_include_directories(VeroScriptJsonBindings PUBLIC
		"$<BUILD_INTERFACE:$<TARGET_PROPERTY:VeroScript,INTERFACE_INCLUDE_DIRECTORIES>>"
		PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")
	target_compile_features(VeroScriptJsonBindings PUBLIC cxx_std_17)
	set_target_properties(VeroScriptJsonBindings PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME JsonBindings)
	install(TARGETS VeroScriptJsonBindings EXPORT VeroScriptJsonBindingsTargets
		ARCHIVE DESTINATION lib COMPONENT VeroScriptJsonBindings)
	install(EXPORT VeroScriptJsonBindingsTargets NAMESPACE VeroScript:: DESTINATION lib/cmake/VeroScriptJson
		COMPONENT VeroScriptJsonBindings)
	install(FILES Public/VeroScriptJsonModule.h DESTINATION include/VeroScriptJson COMPONENT VeroScriptJsonBindings)
	if(MSVC)
		target_compile_options(VeroScriptJsonBindings PRIVATE /W4 /permissive- /utf-8 /EHsc)
	else()
		target_compile_options(VeroScriptJsonBindings PRIVATE -Wall -Wextra -Wpedantic -fexceptions)
	endif()
	if(NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
		add_library(VeroScriptJsonModule SHARED Private/Binding/JsonDynamic.cpp)
		target_link_libraries(VeroScriptJsonModule PRIVATE VeroScriptJsonBindings VeroScript)
		set_target_properties(VeroScriptJsonModule PROPERTIES CXX_VISIBILITY_PRESET hidden
			VISIBILITY_INLINES_HIDDEN YES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
		if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
			target_link_options(VeroScriptJsonModule PRIVATE "LINKER:--exclude-libs,ALL")
		elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
			target_link_options(VeroScriptJsonModule PRIVATE "LINKER:-exported_symbol,_veroscript_extension_query")
		endif()
		install(TARGETS VeroScriptJsonModule LIBRARY DESTINATION lib/veroscript/modules COMPONENT VeroScriptJsonBindings
			RUNTIME DESTINATION lib/veroscript/modules COMPONENT VeroScriptJsonBindings
			ARCHIVE DESTINATION lib COMPONENT VeroScriptJsonBindings)
		if(MSVC)
			target_compile_options(VeroScriptJsonModule PRIVATE /utf-8 /EHsc)
		endif()
	endif()
	if(BUILD_TESTING)
		add_executable(VeroScriptJsonBindingTests Private/Tests/JsonBindingTests.cpp)
		target_link_libraries(VeroScriptJsonBindingTests PRIVATE VeroScriptJsonBindings VeroScript)
		if(MSVC)
			target_compile_options(VeroScriptJsonBindingTests PRIVATE /EHsc /utf-8)
		endif()
		if(MINGW)
			target_link_options(VeroScriptJsonBindingTests PRIVATE -municode)
		endif()
		set(VEROSCRIPT_JSON_TEST_JIT_BACKEND "" CACHE FILEPATH "Optional matching JIT backend for JSON integration tests")
		set(_json_test_jit_args)
		# The complete mapping corpus runs with static and dynamic registration;
		# a matching JIT backend adds VM, Mixed and JIT execution profiles.
		set(_json_test_timeout 240)
		set(_json_library_timeout 90)
		if(TARGET VeroScriptAsmJitBackend)
			add_dependencies(VeroScriptJsonBindingTests VeroScriptAsmJitBackend)
			set(_json_test_jit "$<TARGET_FILE:VeroScriptAsmJitBackend>")
		else()
			set(_json_test_jit "${VEROSCRIPT_JSON_TEST_JIT_BACKEND}")
		endif()
		if(_json_test_jit)
			set(_json_test_jit_args --jit-backend "${_json_test_jit}")
			set(_json_test_timeout 720)
			set(_json_library_timeout 180)
		endif()
		set(_json_budget_timeout 180)
		if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
			# Keep every registration/backend profile under instruction emulation.
			math(EXPR _json_test_timeout "${_json_test_timeout} * 5")
			math(EXPR _json_library_timeout "${_json_library_timeout} * 5")
			set(_json_budget_timeout 900)
		endif()
		if(TARGET VeroScriptJsonModule)
			add_dependencies(VeroScriptJsonBindingTests VeroScriptJsonModule)
			add_test(NAME VeroScriptJsonBindingTests COMMAND VeroScriptJsonBindingTests "$<TARGET_FILE:VeroScriptJsonModule>"
				"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Examples/hello.vs"
				"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tests/Mapping" ${_json_test_jit_args})
		else()
			add_test(NAME VeroScriptJsonBindingTests COMMAND VeroScriptJsonBindingTests --static-only
				"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Examples/hello.vs"
				"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tests/Mapping" ${_json_test_jit_args})
		endif()

		# The binding suite runs the complete script corpus with both static and dynamic registration.
		set_tests_properties(VeroScriptJsonBindingTests PROPERTIES TIMEOUT ${_json_test_timeout} LABELS "VeroScript;Json;Binding")
		add_test(NAME VeroScriptJsonMappingBudgetTests COMMAND VeroScriptJsonBindingTests --module-budget)
		set_tests_properties(VeroScriptJsonMappingBudgetTests PROPERTIES TIMEOUT ${_json_budget_timeout} LABELS "VeroScript;Json;Binding;Budget")
		if(TARGET VeroScriptJsonModule)
			set(_library_module "$<TARGET_FILE:VeroScriptJsonModule>")
		else()
			set(_library_module "--static-only")
		endif()
		add_test(NAME VeroScriptJsonLibraryMappingTests COMMAND VeroScriptJsonBindingTests --library
			"${_library_module}" "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tests/Mapping" ${_json_test_jit_args})
		set_tests_properties(VeroScriptJsonLibraryMappingTests PROPERTIES TIMEOUT ${_json_library_timeout} LABELS "VeroScript;Json;Binding;Package")
	endif()
endif()
