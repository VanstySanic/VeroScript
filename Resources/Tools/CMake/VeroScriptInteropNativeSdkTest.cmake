# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

function(require_success result label)
	if(NOT "${result}" STREQUAL "0")
		message(FATAL_ERROR "${label} failed: ${result}\n${out}\n${err}")
	endif()
endfunction()

set(sdk_build_targets VeroScriptInterop)
if(JIT_BACKEND_TARGET)
	list(APPEND sdk_build_targets "${JIT_BACKEND_TARGET}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD}" --config "${CONFIG}"
	--target ${sdk_build_targets}
	RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
require_success("${status}" "Interop SDK prerequisites")

execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --config "${CONFIG}"
	--prefix "${WORK}/sdk" --component VeroScriptInterop
	RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
require_success("${status}" "Interop installation")

# Stage the matching native core SDK inputs separately. The consumer compiles
# only against these headers and archives plus the installed Interop package.
file(MAKE_DIRECTORY "${WORK}/core/include" "${WORK}/core/lib" "${WORK}/source")
file(COPY "${CORE_INCLUDE}/" DESTINATION "${WORK}/core/include")
file(COPY_FILE "${CORE_GENERATED}/VeroScriptJitBuild.h" "${WORK}/core/include/VeroScriptJitBuild.h")
get_filename_component(core_library_name "${CORE_LIBRARY}" NAME)
file(COPY_FILE "${CORE_LIBRARY}" "${WORK}/core/lib/${core_library_name}")
file(COPY_FILE "${TEST_SOURCE}" "${WORK}/source/main.cpp")
file(WRITE "${WORK}/source/CMakeLists.txt" "cmake_minimum_required(VERSION 3.25)
project(InteropNativeSdk LANGUAGES CXX)
find_package(VeroScriptInterop 0.1.1 EXACT CONFIG REQUIRED PATHS \"${WORK}/sdk\" NO_DEFAULT_PATH)
add_library(Core STATIC IMPORTED)
set_target_properties(Core PROPERTIES IMPORTED_LOCATION \"${WORK}/core/lib/${core_library_name}\"
    INTERFACE_INCLUDE_DIRECTORIES \"${WORK}/core/include\")
add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_17)
target_link_libraries(consumer PRIVATE Core VeroScript::VeroScriptInterop)
if(WIN32)
    add_custom_command(TARGET consumer POST_BUILD COMMAND \"${CMAKE_COMMAND}\" -E copy_directory
        \"${WORK}/sdk/bin\" \"$<TARGET_FILE_DIR:consumer>\" VERBATIM)
endif()
enable_testing()
add_test(NAME native-annotation-consumer COMMAND consumer)
")
# Match the installed archive when a single-config generator ignores --config.
set(generator_args -G "${GENERATOR}" "-DCMAKE_BUILD_TYPE=${CONFIG}")
if(PLATFORM)
	list(APPEND generator_args -A "${PLATFORM}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${WORK}/source" -B "${WORK}/consumer" ${generator_args}
	RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
require_success("${status}" "native SDK configure")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/consumer" --config "${CONFIG}"
	RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
require_success("${status}" "native SDK compile/link")
execute_process(COMMAND "${CTEST}" --test-dir "${WORK}/consumer" -C "${CONFIG}"
	--output-on-failure --no-tests=error
	RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
require_success("${status}" "native SDK execution")
