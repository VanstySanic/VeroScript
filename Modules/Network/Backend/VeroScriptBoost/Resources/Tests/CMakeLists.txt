# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

add_executable(VeroScriptBoostHostAsioTests BoostHostAsioTests.cpp)
target_link_libraries(VeroScriptBoostHostAsioTests PRIVATE VeroScriptNetwork)
target_include_directories(VeroScriptBoostHostAsioTests SYSTEM PRIVATE "${VEROSCRIPT_MODULE_ROOT}/Modules/Network/Backend/VeroScriptBoost/ThirdParty")
# Deliberately use a different host backend and header-only Asio, without the private prefix.
target_compile_definitions(VeroScriptBoostHostAsioTests PRIVATE BOOST_ASIO_DISABLE_IOCP
	BOOST_ASIO_DISABLE_EPOLL BOOST_ASIO_DISABLE_KQUEUE BOOST_ASIO_NO_DEPRECATED NOMINMAX WIN32_LEAN_AND_MEAN)
if(WIN32)
	target_compile_definitions(VeroScriptBoostHostAsioTests PRIVATE _WIN32_WINNT=0x0a00)
endif()
if(MSVC)
	target_compile_options(VeroScriptBoostHostAsioTests PRIVATE /EHsc /utf-8 /bigobj)
endif()
add_test(NAME VeroScriptBoostHostAsioTests COMMAND VeroScriptBoostHostAsioTests)
set_tests_properties(VeroScriptBoostHostAsioTests PROPERTIES TIMEOUT 30 LABELS "VeroScript;Network;Embedding")

veroscript_network_test(VeroScriptBoostHostAsioTests)
