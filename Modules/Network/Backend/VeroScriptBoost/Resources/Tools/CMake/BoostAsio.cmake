# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

function(veroscript_network_asio target)
	get_filename_component(_backend "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../.." ABSOLUTE)
	# Boost is a private implementation dependency. Keep its C++ symbols separate
	# from a host application's independently configured/versioned Boost library.
	target_compile_definitions(${target} PRIVATE BOOST_ASIO_SEPARATE_COMPILATION
		BOOST_ASIO_NO_DEPRECATED NOMINMAX WIN32_LEAN_AND_MEAN)
	if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
		target_compile_definitions(${target} PRIVATE BOOST_ASIO_DYN_LINK)
	else()
		target_compile_definitions(${target} PRIVATE VEROSCRIPT_NETWORK_STATIC_RUNTIME)
	endif()
	if(WIN32)
		target_compile_definitions(${target} PRIVATE _WIN32_WINNT=0x0a00)
	endif()
	if(MSVC)
		target_compile_options(${target} PRIVATE /utf-8 /EHsc /bigobj "/FI${_backend}/Internal/NetworkBoost.h")
	else()
		target_compile_options(${target} PRIVATE -include "${_backend}/Internal/NetworkBoost.h")
	endif()
endfunction()
