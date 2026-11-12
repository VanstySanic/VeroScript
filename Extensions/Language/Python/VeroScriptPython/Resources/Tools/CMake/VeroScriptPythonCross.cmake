# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Host interpreter discovery is deliberately separate from target SDK artifacts.
find_package(Python3 3.13...<3.15 REQUIRED COMPONENTS Interpreter)
set(VEROSCRIPT_PYTHON_TARGET_SDK "" CACHE FILEPATH "Target CPython SDK JSON descriptor")
if(NOT VEROSCRIPT_PYTHON_TARGET_SDK)
	message(FATAL_ERROR "Mobile Python requires VEROSCRIPT_PYTHON_TARGET_SDK; desktop headers cannot be used")
endif()
if(NOT VEROSCRIPT_PYTHON_BUILD_NONCE)
	string(RANDOM LENGTH 32 ALPHABET "0123456789abcdef" _vs_python_nonce)
	set(VEROSCRIPT_PYTHON_BUILD_NONCE "${_vs_python_nonce}" CACHE STRING "Mobile build instance identity" FORCE)
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}"
	"${VEROSCRIPT_MODULE_ROOT}/Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py"
	--sdk "${VEROSCRIPT_PYTHON_TARGET_SDK}" --check-host --build-nonce "${VEROSCRIPT_PYTHON_BUILD_NONCE}"
	RESULT_VARIABLE _vs_sdk_status OUTPUT_VARIABLE _vs_sdk ERROR_VARIABLE _vs_sdk_error)
if(NOT _vs_sdk_status EQUAL 0)
	message(FATAL_ERROR "Invalid target CPython SDK: ${_vs_sdk_error}")
endif()
foreach(field IN ITEMS platform architecture variant pythonVersion productVersion minimumVersion extensionSuffix includeDirectory library buildId)
	string(JSON _vs_python_${field} GET "${_vs_sdk}" "${field}")
endforeach()
if(NOT VEROSCRIPT_INTEROP_STATIC OR BUILD_TESTING OR VS_VEROSCRIPT_PYTHON_PACKAGE_BUILD)
	message(FATAL_ERROR "Mobile Python requires static Interop, BUILD_TESTING=OFF and desktop wheel packaging=OFF")
endif()
if(VEROSCRIPT_BUILD_ASMJIT_BACKEND)
	message(FATAL_ERROR "Mobile Python packages use VM/Mixed without an AsmJit module")
endif()
if(ANDROID)
	# The NDK legacy toolchain reports CMAKE_SYSTEM_VERSION=1. Its resolved
	# Android variables carry the actual compiler ABI and API; built-in CMake
	# Android toolchains instead expose the CMAKE_ANDROID_* / system values.
	set(_vs_python_android_abi "${CMAKE_ANDROID_ARCH_ABI}")
	set(_vs_python_android_api "${CMAKE_SYSTEM_VERSION}")
	if(DEFINED ANDROID_ABI)
		set(_vs_python_android_abi "${ANDROID_ABI}")
	endif()
	if(DEFINED ANDROID_PLATFORM_LEVEL)
		set(_vs_python_android_api "${ANDROID_PLATFORM_LEVEL}")
	endif()
	if(NOT _vs_python_platform STREQUAL "android" OR NOT _vs_python_android_abi STREQUAL _vs_python_architecture
		OR NOT _vs_python_android_api STREQUAL _vs_python_minimumVersion)
		message(FATAL_ERROR "Android toolchain ABI/API disagrees with the target CPython SDK")
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
	if(NOT _vs_python_platform STREQUAL "ios" OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL _vs_python_architecture
		OR NOT CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL _vs_python_minimumVersion)
		message(FATAL_ERROR "iOS toolchain architecture/minimum OS disagrees with the target CPython SDK")
	endif()
	string(TOLOWER "${CMAKE_OSX_SYSROOT}" _vs_python_sysroot)
	if((_vs_python_variant STREQUAL "device" AND NOT _vs_python_sysroot MATCHES "iphoneos")
		OR (_vs_python_variant STREQUAL "simulator" AND NOT _vs_python_sysroot MATCHES "iphonesimulator"))
		message(FATAL_ERROR "iOS device/simulator SDK disagreement")
	endif()
else()
	message(FATAL_ERROR "Cross CPython extension builds currently require Android or iOS")
endif()
add_library(VeroScriptPythonTarget INTERFACE)
target_include_directories(VeroScriptPythonTarget INTERFACE "${_vs_python_includeDirectory}")
target_link_libraries(VeroScriptPythonTarget INTERFACE "${_vs_python_library}")
add_library(VeroScriptPython SHARED Private/VeroScriptPython.cpp)
target_link_libraries(VeroScriptPython PRIVATE VeroScriptPythonTarget)
target_compile_definitions(VeroScriptPython PRIVATE VEROSCRIPT_PYTHON_MOBILE_BUILD=1
	"VEROSCRIPT_PYTHON_BUILD_ID=\"${_vs_python_buildId}\"")
set_target_properties(VeroScriptPython PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(ANDROID)
	set_target_properties(VeroScriptPython PROPERTIES PREFIX "lib" OUTPUT_NAME "veroscript_python" SUFFIX ".so")
	target_link_options(VeroScriptPython PRIVATE "LINKER:-z,max-page-size=16384"
		"LINKER:-z,common-page-size=16384" "LINKER:--exclude-libs,ALL")
else()
	set_target_properties(VeroScriptPython PROPERTIES FRAMEWORK TRUE OUTPUT_NAME "veroscript._veroscript"
		MACOSX_FRAMEWORK_IDENTIFIER "org.veroscript.python.extension"
		MACOSX_FRAMEWORK_BUNDLE_VERSION "${_vs_python_productVersion}"
		MACOSX_FRAMEWORK_SHORT_VERSION_STRING "${_vs_python_productVersion}"
		INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR TRUE)
	target_link_options(VeroScriptPython PRIVATE "LINKER:-exported_symbol,_PyInit__veroscript")
endif()
