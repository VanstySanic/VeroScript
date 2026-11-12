# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT ANDROID)
	message(FATAL_ERROR "VeroScriptAndroid requires the Android NDK toolchain")
endif()
if(VEROSCRIPT_INTEROP_STATIC)
	message(FATAL_ERROR "The Android AAR requires VEROSCRIPT_INTEROP_STATIC=OFF")
endif()
add_library(VeroScriptAndroid SHARED Private/VeroScriptAndroid.cpp)
target_include_directories(VeroScriptAndroid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(VeroScriptAndroid PRIVATE VeroScriptMobile)
target_link_options(VeroScriptAndroid PRIVATE "LINKER:-z,max-page-size=16384")
set_target_properties(VeroScriptAndroid PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
vs_apply_cxx_defaults(VeroScriptAndroid)
vs_set_solution_folder(VeroScriptAndroid "Libraries/Mobile")
