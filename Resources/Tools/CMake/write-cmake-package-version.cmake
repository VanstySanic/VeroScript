# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
	message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT DEFINED PACKAGE_SEMANTIC_VERSION)
	message(FATAL_ERROR "PACKAGE_SEMANTIC_VERSION must be major.minor.patch")
endif()
if(NOT "${PACKAGE_SEMANTIC_VERSION}" MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
	message(FATAL_ERROR "PACKAGE_SEMANTIC_VERSION must be major.minor.patch")
endif()
if(NOT DEFINED PACKAGE_POINTER_SIZE OR NOT PACKAGE_POINTER_SIZE MATCHES "^(4|8)$")
	message(FATAL_ERROR "PACKAGE_POINTER_SIZE must be 4 or 8")
endif()

# Match write_basic_package_version_file() from the normal CMake install path.
# The pointer size is supplied because script mode has no configured language.
set(CMAKE_SIZEOF_VOID_P "${PACKAGE_POINTER_SIZE}")
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${OUTPUT_FILE}"
  VERSION "${PACKAGE_SEMANTIC_VERSION}"
  COMPATIBILITY SameMajorVersion)
