# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

get_filename_component(_module "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(_group "${_module}/.." ABSOLUTE)
if(BUILD_TESTING AND VEROSCRIPT_BUILD_WEBSOCKET AND TARGET VeroScriptBoost)
	find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
	add_executable(VeroScriptWebSocketCompressionTests WebSocketCompressionTests.cpp)
	target_link_libraries(VeroScriptWebSocketCompressionTests PRIVATE VeroScriptWebSocket)
	if(MSVC)
		target_compile_options(VeroScriptWebSocketCompressionTests PRIVATE /EHsc /utf-8)
	endif()
	add_test(NAME VeroScriptWebSocketCompressionTests COMMAND VeroScriptWebSocketCompressionTests)
	add_test(NAME VeroScriptWebSocketCompressionInteropTests COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_CURRENT_SOURCE_DIR}/websocket_compression_runner.py" "$<TARGET_FILE:VeroScriptWebSocketCompressionTests>")
	set_tests_properties(VeroScriptWebSocketCompressionTests VeroScriptWebSocketCompressionInteropTests
		PROPERTIES TIMEOUT 180 LABELS "VeroScript;Network;WebSocket;Compression")
	add_executable(VeroScriptWebSocketProxyTests WebSocketProxyTests.cpp)
	target_link_libraries(VeroScriptWebSocketProxyTests PRIVATE VeroScriptWebSocket)
	target_compile_definitions(VeroScriptWebSocketProxyTests PRIVATE VEROSCRIPT_PROXY_TLS=$<BOOL:${VEROSCRIPT_NETWORK_TLS}>)
	if(TARGET VeroScriptBoostBindings AND TARGET VeroScriptNetworkBindings AND TARGET VeroScriptWebSocketBindings)
		target_link_libraries(VeroScriptWebSocketProxyTests PRIVATE VeroScriptNetworkBindings VeroScriptWebSocketBindings VeroScript)
		target_compile_definitions(VeroScriptWebSocketProxyTests PRIVATE VEROSCRIPT_PROXY_BINDINGS=1
			"VEROSCRIPT_PROXY_EXAMPLE_PATH=\"${_group}/VeroScriptWebSocket/Resources/Examples/proxy.vs\"")
	endif()
	if(MSVC)
		target_compile_options(VeroScriptWebSocketProxyTests PRIVATE /EHsc /utf-8)
	endif()
	if(TARGET VeroScriptNetworkModule AND TARGET VeroScriptWebSocketModule)
		add_dependencies(VeroScriptWebSocketProxyTests VeroScriptNetworkModule VeroScriptWebSocketModule)
		set(_proxy_modules "$<TARGET_FILE:VeroScriptNetworkModule>" "$<TARGET_FILE:VeroScriptWebSocketModule>")
	else()
		set(_proxy_modules)
	endif()
	add_test(NAME VeroScriptWebSocketProxyTests COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_CURRENT_SOURCE_DIR}/websocket_proxy_runner.py" "$<TARGET_FILE:VeroScriptWebSocketProxyTests>" ${_proxy_modules})
	set_tests_properties(VeroScriptWebSocketProxyTests PROPERTIES TIMEOUT 75 LABELS "VeroScript;Network;WebSocket;Proxy")
	add_test(NAME VeroScriptWebSocketReconnectTests COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_CURRENT_SOURCE_DIR}/websocket_proxy_runner.py" "$<TARGET_FILE:VeroScriptWebSocketProxyTests>" ${_proxy_modules} --reconnect)
	set_tests_properties(VeroScriptWebSocketReconnectTests PROPERTIES TIMEOUT 75 LABELS "VeroScript;Network;WebSocket;Reconnect")
endif()

get_property(_network_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_test IN LISTS _network_test_targets)
	veroscript_network_test(${_test})
endforeach()
