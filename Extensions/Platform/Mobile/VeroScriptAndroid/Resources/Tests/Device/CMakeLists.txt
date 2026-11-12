# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
project(VeroScriptAndroidDeviceSmoke LANGUAGES C CXX)
find_package(VeroScriptAndroid REQUIRED CONFIG)
add_library(VeroScriptDeviceSmoke SHARED native.cpp c_header.c)
target_compile_features(VeroScriptDeviceSmoke PRIVATE c_std_11 cxx_std_17)
target_compile_options(VeroScriptDeviceSmoke PRIVATE -Wall -Wextra -Werror)
target_link_libraries(VeroScriptDeviceSmoke PRIVATE VeroScriptAndroid::VeroScriptInterop VeroScriptAndroid::VeroScriptAndroid)
