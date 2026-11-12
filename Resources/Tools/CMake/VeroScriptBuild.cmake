# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# VeroScript owns its module ordering and aggregate native target membership.
include("${CMAKE_CURRENT_LIST_DIR}/VeroScriptPaths.cmake")

option(VEROSCRIPT_BUILD_JSON "Build the optional JSON module" OFF)
option(VEROSCRIPT_BUILD_NETWORK "Build the optional native network foundation" OFF)
option(VEROSCRIPT_BUILD_HTTP "Build the optional HTTP module" OFF)
option(VEROSCRIPT_BUILD_WEBSOCKET "Build the optional WebSocket module" OFF)

set(_vs_default_boost OFF)
if(VEROSCRIPT_BUILD_NETWORK OR VEROSCRIPT_BUILD_HTTP OR VEROSCRIPT_BUILD_WEBSOCKET OR VEROSCRIPT_BUILD_BOOST OR VEROSCRIPT_BUILD_CURL)
	set(_vs_default_boost ON)
endif()
option(VEROSCRIPT_BUILD_BOOST "Build the optional Boost network provider" ${_vs_default_boost})
option(VEROSCRIPT_BUILD_CURL "Build the optional curl HTTP client provider" ${VEROSCRIPT_BUILD_HTTP})
if(VEROSCRIPT_BUILD_CURL)
	set(VEROSCRIPT_BUILD_HTTP ON)
endif()

option(VEROSCRIPT_BUILD_VISUAL "Enable the default platform and multimedia foundation modules" OFF)
option(VEROSCRIPT_BUILD_IMAGE "Build CPU image decoding" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_BUILD_FONT "Build CPU font loading, shaping and rasterization" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_BUILD_STB "Build the stb service provider" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_BUILD_FONT_BACKEND "Build the FreeType/HarfBuzz font and SheenBidi text-layout providers" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_BUILD_AUDIO "Build public audio services" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_BUILD_PLATFORM "Build public platform contracts" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_BUILD_GRAPHICS "Build public graphics contracts" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_BUILD_SDL3 "Build the vendored SDL3 provider" ${VEROSCRIPT_BUILD_VISUAL})
option(VEROSCRIPT_SDL3_GPU "Enable SDL_GPU in the SDL3 provider" ${VEROSCRIPT_BUILD_GRAPHICS})
option(VEROSCRIPT_VISUAL_SCRIPT_BINDINGS "Build optional Core-backed script adapters" ON)

function(vs_get_veroscript_subdirectories result examples_result module_root)
	set(examples)
	if(VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY)
		set(subdirs
			"${module_root}/VeroScript"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCpp"
			"${module_root}/VeroScriptInterop"
		)
		if(VEROSCRIPT_BUILD_MOBILE_ADAPTERS)
			list(APPEND subdirs "${module_root}/Extensions/Platform/Mobile/VeroScriptMobile")
		endif()
		if(ANDROID AND VEROSCRIPT_BUILD_MOBILE_ADAPTERS)
			list(APPEND subdirs "${module_root}/Extensions/Platform/Mobile/VeroScriptAndroid")
		elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND VEROSCRIPT_BUILD_MOBILE_ADAPTERS)
			list(APPEND subdirs "${module_root}/Extensions/Platform/Mobile/VeroScriptIOS")
		endif()
	elseif(VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY)
		set(subdirs
			"${module_root}/VeroScript"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCpp"
			"${module_root}/Extensions/Platform/WebAssembly/VeroScriptWebAssembly"
		)
	elseif(VS_BUILD_VEROSCRIPT_PYTHON_ONLY)
		set(subdirs
			"${module_root}/VeroScript"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCpp"
			"${module_root}/VeroScriptInterop"
			"${module_root}/Extensions/Language/Python/VeroScriptPython"
		)
		if(NOT VEROSCRIPT_MOBILE_TARGET)
			list(APPEND subdirs
			"${module_root}/Extensions/Language/Python/VeroScriptPythonFrontend"
			"${module_root}/Extensions/Language/Python/VeroScriptPythonBindGen"
			"${module_root}/Extensions/Language/Python/VeroScriptPythonTranspiler"
			"${module_root}/Extensions/Language/Python/VeroScriptPythonAot"
			)
		endif()
	else()
		set(subdirs
			"${module_root}/VeroScript"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCpp"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCppFrontend"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCppBindGen"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCppTranspiler"
			"${module_root}/VeroScriptInterop"
			"${module_root}/Extensions/Language/Python/VeroScriptPython"
			"${module_root}/Extensions/Language/Python/VeroScriptPythonFrontend"
			"${module_root}/Extensions/Language/Python/VeroScriptPythonBindGen"
			"${module_root}/Extensions/Language/Python/VeroScriptPythonTranspiler"
			"${module_root}/Extensions/Language/CSharp/VeroScriptCSharp"
			"${module_root}/Extensions/Language/CSharp/VeroScriptCSharpFrontend"
			"${module_root}/Extensions/Language/CSharp/VeroScriptCSharpTranspiler"
			"${module_root}/Extensions/Language/CSharp/VeroScriptCSharpBindGen"
			"${module_root}/Extensions/Language/Cpp/VeroScriptCppAot"
			"${module_root}/Extensions/Language/Python/VeroScriptPythonAot"
			"${module_root}/Extensions/Language/CSharp/VeroScriptCSharpAot"
			"${module_root}/VeroScriptTooling"
			"${module_root}/VeroScriptCli"
			"${module_root}/Extensions/Platform/WebAssembly/VeroScriptWebAssembly"
		)
		set(examples
			"${module_root}/Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/Embed"
		)
		if(VEROSCRIPT_BUILD_MOBILE_ADAPTERS)
			list(APPEND subdirs "${module_root}/Extensions/Platform/Mobile/VeroScriptMobile")
		endif()
	endif()
	if(VEROSCRIPT_BUILD_PACKAGE_MANAGER OR VEROSCRIPT_BUILD_PACKAGE_RUNTIME)
		list(INSERT subdirs 1 "${module_root}/VeroScriptPackageManager")
	endif()
	if(VEROSCRIPT_BUILD_APPLICATION)
		list(INSERT subdirs 1 "${module_root}/VeroScriptApplication")
	endif()
	if(VEROSCRIPT_BUILD_ASMJIT_BACKEND)
		list(INSERT subdirs 1 "${module_root}/Modules/Execution/VeroScriptAsmJit")
	endif()
	if(VEROSCRIPT_BUILD_VM_BACKEND)
		list(INSERT subdirs 1 "${module_root}/Modules/Execution/VeroScriptBytecodeVm")
	endif()
	if(VEROSCRIPT_BUILD_BUILD_SYSTEM)
		list(FIND subdirs "${module_root}/VeroScriptCli" _build_cli_index)
		if(_build_cli_index LESS 0)
			list(APPEND subdirs "${module_root}/VeroScriptBuild")
		else()
			list(INSERT subdirs ${_build_cli_index} "${module_root}/VeroScriptBuild")
		endif()
	endif()
	if(VEROSCRIPT_BUILD_AUDIO OR VEROSCRIPT_BUILD_PLATFORM OR VEROSCRIPT_BUILD_GRAPHICS OR VEROSCRIPT_BUILD_SDL3 OR VEROSCRIPT_BUILD_IMAGE OR VEROSCRIPT_BUILD_FONT OR VEROSCRIPT_BUILD_STB OR VEROSCRIPT_BUILD_FONT_BACKEND)
		list(APPEND subdirs "${module_root}/Resources/Tools/CMake/Foundation")
	endif()
	if(VEROSCRIPT_BUILD_JSON)
		list(APPEND subdirs "${module_root}/Modules/Network/VeroScriptJson")
	endif()
	if(VEROSCRIPT_BUILD_NETWORK OR VEROSCRIPT_BUILD_HTTP OR VEROSCRIPT_BUILD_WEBSOCKET OR VEROSCRIPT_BUILD_BOOST OR VEROSCRIPT_BUILD_CURL)
		list(APPEND subdirs "${module_root}/Modules/Network")
	endif()
	if(VEROSCRIPT_BUILD_PACKAGE_MANAGER)
		list(APPEND subdirs "${module_root}/Resources/Tools/Packaging/ModulePackages")
	endif()
	set(${result} "${subdirs}" PARENT_SCOPE)
	set(${examples_result} "${examples}" PARENT_SCOPE)
endfunction()

function(vs_get_veroscript_native_targets result)
	set(targets
		VeroScript
		VeroScriptCpp
		VeroScriptCppFrontend
		VeroScriptCppBindGenLib
		VeroScriptCppBindGen
		VeroScriptCppTranspilerLib
		VeroScriptCppTranspiler
		VeroScriptInterop
		VeroScriptPython
		VeroScriptPythonFrontend
		VeroScriptPythonBindGen
		VeroScriptPythonTranspiler
		VeroScriptCSharp
		VeroScriptCSharpTranspiler
		VeroScriptCSharpTranspilerCli
		VeroScriptWebAssembly
		VeroScriptWebAssemblyModule
		VeroScriptCppAot
		VeroScriptPythonAot
		VeroScriptCSharpAot
		VeroScriptTooling
		VeroScriptCli
		VeroScriptPackageManager
		VeroScriptApplication
		VeroScriptBuild
		VeroScriptPackageRuntime
		VeroScriptApplicationPackages
		VeroScriptRuntime
		VeroScriptLauncher
		VeroScriptDap
		VeroScriptLsp
		VeroScriptEmbedExample
		VeroScriptMobile
		VeroScriptAndroid
		VeroScriptIOS
		VeroScriptAudio
		VeroScriptAudioBindings
		VeroScriptPlatform
		VeroScriptPlatformBindings
		VeroScriptGraphics
		VeroScriptGraphicsBindings
		VeroScriptSdl3
		VeroScriptImage
		VeroScriptFont
		VeroScriptStb
		VeroScriptStbModule
		VeroScriptFontBackend
		VeroScriptFontBackendModule
		VeroScriptImageBindings
		VeroScriptFontBindings
		VeroScriptJson
		VeroScriptJsonBindings
		VeroScriptJsonModule
		VeroScriptNetwork
		VeroScriptNetworkBindings
		VeroScriptNetworkModule
		VeroScriptHttp
		VeroScriptHttpBindings
		VeroScriptHttpModule
		VeroScriptWebSocket
		VeroScriptWebSocketBindings
		VeroScriptWebSocketModule
		VeroScriptBoost
		VeroScriptBoostRuntime
		VeroScriptBoostBindings
		VeroScriptBoostModule
		VeroScriptCurl
		VeroScriptCurlBindings
		VeroScriptCurlModule
	)
	if(VEROSCRIPT_BUILD_ASMJIT_BACKEND)
		list(APPEND targets VeroScriptAsmJit VeroScriptAsmJitBackend)
	endif()
	set(${result} "${targets}" PARENT_SCOPE)
endfunction()
