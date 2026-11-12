# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT TARGET VeroScriptBytecodeVm)
	message(FATAL_ERROR "The Web product requires the explicitly assembled static bytecode VM")
endif()

vs_add_module_library(VeroScriptWebAssembly)
vs_set_solution_folder(VeroScriptWebAssembly "Libraries")
vs_use_shared_private_headers(VeroScriptWebAssembly)
target_link_libraries(VeroScriptWebAssembly PUBLIC VeroScriptCpp PRIVATE VeroScriptBytecodeVm)

get_target_property(_VeroScriptWebAssembly_sources VeroScriptWebAssembly SOURCES)
list(FILTER _VeroScriptWebAssembly_sources EXCLUDE REGEX "/Tests/")
list(FILTER _VeroScriptWebAssembly_sources EXCLUDE REGEX "/VeroScriptWebAssemblyModule\\.cpp$")
set_target_properties(VeroScriptWebAssembly PROPERTIES SOURCES "${_VeroScriptWebAssembly_sources}")

if(EMSCRIPTEN)
	# The runtime relies on C++ exception handling for recoverable script/API
	# failures. Release builds use LTO plus the browser's optimizing Wasm engine;
	# VeroScript's native asmjit backend remains disabled by the build entrypoint.
	foreach(_vs_webassembly_dependency IN ITEMS VeroScript VeroScriptBytecodeVm VeroScriptCpp VeroScriptWebAssembly)
		target_compile_options(${_vs_webassembly_dependency} PRIVATE -fexceptions)
		set_property(TARGET ${_vs_webassembly_dependency}
			PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	endforeach()

	set(_vs_webassembly_stdlib_root "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	set(_vs_webassembly_localization_root "${CMAKE_CURRENT_BINARY_DIR}/Resources/Localization")
	vs_localization_roots(_vs_webassembly_localization_sources "${CMAKE_SOURCE_DIR}" cpp)
	add_custom_target(VeroScriptWebAssemblyLocalization
		COMMAND ${CMAKE_COMMAND}
			"-DVEROSCRIPT_SOURCE_ROOT=${VEROSCRIPT_MODULE_ROOT}"
			"-DVEROSCRIPT_LOCALIZATION_OUTPUT=${_vs_webassembly_localization_root}"
			"-DVEROSCRIPT_LOCALIZATION_PROFILE=cpp"
			-P "${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptLocalization.cmake"
		COMMENT "Assembling WebAssembly dependency localization")
	if(NOT IS_DIRECTORY "${_vs_webassembly_stdlib_root}"
		OR NOT IS_DIRECTORY "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Localization")
		message(FATAL_ERROR
			"VeroScript WebAssembly requires stdlib at ${_vs_webassembly_stdlib_root} "
			"and localization at ${_vs_webassembly_localization_root}")
	endif()
	file(GLOB_RECURSE _vs_webassembly_embedded_assets CONFIGURE_DEPENDS
		"${_vs_webassembly_stdlib_root}/*")
	foreach(_vs_webassembly_localization_source IN LISTS _vs_webassembly_localization_sources)
		file(GLOB_RECURSE _vs_webassembly_catalogs CONFIGURE_DEPENDS "${_vs_webassembly_localization_source}/*.json")
		list(APPEND _vs_webassembly_embedded_assets ${_vs_webassembly_catalogs})
	endforeach()
	# Removing an input changes the dependency list but does not make Ninja
	# relink an existing module. Track the list itself so removals and renames
	# also refresh the resources embedded in the Wasm.
	set(_vs_webassembly_embedded_manifest "${CMAKE_CURRENT_BINARY_DIR}/embedded-assets.txt")
	file(GENERATE OUTPUT "${_vs_webassembly_embedded_manifest}"
		CONTENT "${_vs_webassembly_embedded_assets}\n")
	list(APPEND _vs_webassembly_embedded_assets
		"${_vs_webassembly_embedded_manifest}"
		"${VEROSCRIPT_MODULE_ROOT}/Resources/Tools/CMake/VeroScriptLocalization.cmake")
	function(_vs_add_webassembly_module target output_name interactive)
		add_executable(${target} Private/VeroScriptWebAssemblyModule.cpp)
		target_link_libraries(${target} PRIVATE VeroScriptWebAssembly)
		add_dependencies(${target} VeroScriptWebAssemblyLocalization)
		target_compile_definitions(${target} PRIVATE
			VERO_WEBASSEMBLY_INTERACTIVE_MODULE=$<BOOL:${interactive}>)
		vs_apply_cxx_defaults(${target})
		vs_set_solution_folder(${target} "WebAssembly")
		set_target_properties(${target} PROPERTIES
			OUTPUT_NAME "${output_name}"
			SUFFIX ".mjs")
		set_property(TARGET ${target} APPEND PROPERTY
			LINK_DEPENDS "${_vs_webassembly_embedded_assets}")
		target_link_options(${target} PRIVATE
			"-fexceptions"
			# Binaryen's additional -O3 optimize-stack-ir/minification stage is
			# unstable for these large generated modules. -O2 retains Release
			# optimization and LTO while keeping the final Binaryen pass stable.
			"$<$<CONFIG:Release>:-O2>"
			"--no-entry"
			"-sMODULARIZE=1"
			"-sEXPORT_ES6=1"
			"-sEMIT_EMSCRIPTEN_LICENSE=1"
			"-sENVIRONMENT=web,worker,node"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sINITIAL_MEMORY=67108864"
			"-sMAXIMUM_MEMORY=536870912"
			"-sSTACK_SIZE=16777216"
			"-sWASM_BIGINT=1"
			"-sINVOKE_RUN=0"
			"-sASSERTIONS=$<IF:$<CONFIG:Debug>,1,0>"
			"SHELL:--embed-file \"${_vs_webassembly_stdlib_root}@/stdlib\""
			"SHELL:--embed-file \"${_vs_webassembly_localization_root}@/localization\""
			"-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU32','ccall']")

		if(interactive)
			# The compatible module preserves the C++/VM stack while awaiting
			# frontend input. Keep Asyncify out of the fast module because its
			# whole-program instrumentation materially slows ordinary IDE runs.
			target_link_options(${target} PRIVATE
				"-sASYNCIFY=1"
				"-sASYNCIFY_IMPORTS=['VeroWebAwaitInputLineAsync']"
				# Bound method calls reenter the VM through native frames. A
				# 64 KiB snapshot stack overflows at modest recursion depths
				# when these frames suspend for input. Keep 1 MiB of workspace
				# for suspension; Emscripten frees it after restoring the stack.
				"-sASYNCIFY_STACK_SIZE=1048576"
				"-sEXPORTED_FUNCTIONS=['_malloc','_free','_vero_web_memory_operation','_vero_web_memory_result','_vero_web_set_stdlib_limits','_vero_web_memory_release_lease','_vero_web_abi_version','_vero_web_supports_threads','_vero_web_create','_vero_web_destroy','_vero_web_set_language','_vero_web_get_language','_vero_web_get_available_languages','_vero_web_run_source','_vero_web_run_source_with_args','_vero_web_run_source_with_options','_vero_web_run_source_with_context','_vero_web_compile_source_with_context','_vero_web_run_program','_vero_web_run_program_interactive','_vero_web_destroy_program','_vero_web_run_source_interactive_with_context','_vero_web_check_source','_vero_web_check_source_with_context']")
		else()
			target_link_options(${target} PRIVATE
				"-sEXPORTED_FUNCTIONS=['_malloc','_free','_vero_web_memory_operation','_vero_web_memory_result','_vero_web_set_stdlib_limits','_vero_web_memory_release_lease','_vero_web_abi_version','_vero_web_supports_threads','_vero_web_create','_vero_web_destroy','_vero_web_set_language','_vero_web_get_language','_vero_web_get_available_languages','_vero_web_run_source','_vero_web_run_source_with_args','_vero_web_run_source_with_options','_vero_web_run_source_with_context','_vero_web_compile_source_with_context','_vero_web_run_program','_vero_web_destroy_program','_vero_web_check_source','_vero_web_check_source_with_context']")
		endif()
	endfunction()

	# Compatibility remains the default. Frontend IDEs that do not need an
	# asynchronous input suspension can opt into the substantially faster module.
	_vs_add_webassembly_module(VeroScriptWebAssemblyModule "veroscript-engine" TRUE)
	_vs_add_webassembly_module(VeroScriptWebAssemblyFastModule "veroscript-engine-fast" FALSE)

	add_custom_target(VeroScriptWebAssemblySdkSources ALL
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/Bindings/veroscript-webassembly.ts"
			"$<TARGET_FILE_DIR:VeroScriptWebAssemblyModule>/veroscript-webassembly.ts"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/Bindings/veroscript-webassembly.d.ts"
			"$<TARGET_FILE_DIR:VeroScriptWebAssemblyModule>/veroscript-webassembly.d.ts"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/Bindings/veroscript-engine.d.mts"
			"$<TARGET_FILE_DIR:VeroScriptWebAssemblyModule>/veroscript-engine.d.mts"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/Bindings/veroscript-engine-fast.d.mts"
			"$<TARGET_FILE_DIR:VeroScriptWebAssemblyModule>/veroscript-engine-fast.d.mts"
		DEPENDS VeroScriptWebAssemblyModule VeroScriptWebAssemblyFastModule
		COMMENT "Deploy VeroScript WebAssembly TypeScript SDK sources")
	vs_set_solution_folder(VeroScriptWebAssemblySdkSources "WebAssembly")
endif()

if(BUILD_TESTING AND NOT EMSCRIPTEN)
	add_executable(VeroScriptWebAssemblyTests Private/Tests/VeroScriptWebAssemblyTests.cpp)
	target_link_libraries(VeroScriptWebAssemblyTests PRIVATE VeroScriptWebAssembly)
	vs_apply_cxx_defaults(VeroScriptWebAssemblyTests)
	vs_set_solution_folder(VeroScriptWebAssemblyTests "Tests")
	add_test(NAME VeroScriptWebAssemblyTests COMMAND VeroScriptWebAssemblyTests)
endif()
