# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

function(vs_font_backend_add_dependencies)
	set(BUILD_SHARED_LIBS OFF)
	set(BUILD_FRAMEWORK OFF)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
	set(SKIP_INSTALL_ALL ON)
	foreach(_dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
		set(FT_DISABLE_${_dependency} ON)
	endforeach()
	# Upstream also reads inherited discovery results after skipping find_package.
	# Keep this private dependency independent of the embedding application's SDKs.
	foreach(_found ZLIB_FOUND BZIP2_FOUND PNG_FOUND HARFBUZZ_FOUND HarfBuzz_FOUND BROTLIDEC_FOUND)
		set(${_found} FALSE)
	endforeach()
	add_subdirectory(ThirdParty/FreeType FreeType)
endfunction()
vs_font_backend_add_dependencies()
set_target_properties(freetype PROPERTIES EXPORT_NAME FreeType)
add_library(VeroScriptFontBackendHarfBuzz STATIC ThirdParty/HarfBuzz/src/harfbuzz.cc)
target_include_directories(VeroScriptFontBackendHarfBuzz PRIVATE ThirdParty/HarfBuzz/src)
target_compile_definitions(VeroScriptFontBackendHarfBuzz PRIVATE HAVE_FREETYPE=1)
target_link_libraries(VeroScriptFontBackendHarfBuzz PRIVATE freetype)
target_compile_features(VeroScriptFontBackendHarfBuzz PRIVATE cxx_std_17)
set_target_properties(VeroScriptFontBackendHarfBuzz PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME HarfBuzz)
if(MSVC)
	target_compile_options(VeroScriptFontBackendHarfBuzz PRIVATE /bigobj /utf-8)
endif()
add_library(VeroScriptFontBackendSheenBidi STATIC ThirdParty/SheenBidi/Source/SheenBidi.c)
target_include_directories(VeroScriptFontBackendSheenBidi PRIVATE
	ThirdParty/SheenBidi/Headers ThirdParty/SheenBidi/Source)
# Avoid upstream's shared lazy scratch allocator during concurrent analysis.
target_compile_definitions(VeroScriptFontBackendSheenBidi PRIVATE SB_CONFIG_UNITY SB_CONFIG_DISABLE_SCRATCH_MEMORY)
target_compile_features(VeroScriptFontBackendSheenBidi PRIVATE c_std_11)
set_target_properties(VeroScriptFontBackendSheenBidi PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME SheenBidi)
install(TARGETS freetype VeroScriptFontBackendHarfBuzz VeroScriptFontBackendSheenBidi
	EXPORT VeroScriptFontBackendDependenciesTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT VeroScriptFontBackend)
install(EXPORT VeroScriptFontBackendDependenciesTargets NAMESPACE VeroScriptFontBackendDependency:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/VeroScriptFontBackend" COMPONENT VeroScriptFontBackend)
install(DIRECTORY ThirdParty/FreeType/include/ DESTINATION include/freetype2 COMPONENT VeroScriptFontBackend PATTERN internal EXCLUDE PATTERN ftconfig.h EXCLUDE PATTERN ftoption.h EXCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/FreeType/include/freetype/config/ftconfig.h" "${CMAKE_CURRENT_BINARY_DIR}/FreeType/include/freetype/config/ftoption.h" DESTINATION include/freetype2/freetype/config COMPONENT VeroScriptFontBackend)
