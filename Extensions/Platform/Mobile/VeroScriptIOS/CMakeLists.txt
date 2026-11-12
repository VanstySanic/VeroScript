# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
	message(FATAL_ERROR "VeroScriptIOS requires an iOS device or simulator SDK")
endif()
if(NOT VEROSCRIPT_INTEROP_STATIC)
	message(FATAL_ERROR "The iOS SDK uses VEROSCRIPT_INTEROP_STATIC=ON")
endif()
enable_language(OBJCXX)
set(_ios_headers "${CMAKE_CURRENT_BINARY_DIR}/Headers")
file(MAKE_DIRECTORY "${_ios_headers}")
foreach(_ios_header VeroScriptIOS.h module.modulemap)
	configure_file("Public/${_ios_header}" "${_ios_headers}/${_ios_header}" COPYONLY)
endforeach()
configure_file("${VEROSCRIPT_MODULE_ROOT}/VeroScriptInterop/Public/VeroScriptInterop.h"
	"${_ios_headers}/VeroScriptInterop.h" COPYONLY)
add_library(VeroScriptIOS STATIC Private/VeroScriptIOS.mm)
set_target_properties(VeroScriptIOS PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
target_include_directories(VeroScriptIOS PUBLIC "${_ios_headers}")
target_link_libraries(VeroScriptIOS PUBLIC VeroScriptMobile "-framework Foundation")
target_compile_options(VeroScriptIOS PRIVATE -fobjc-arc)
vs_apply_cxx_defaults(VeroScriptIOS)
vs_set_solution_folder(VeroScriptIOS "Libraries/Mobile")
