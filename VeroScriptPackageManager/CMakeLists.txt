# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

enable_language(C)
# The installed static SDK and its private dependencies must also link into
# shared embedding hosts. Keep this default local to the package-manager tree.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(VEROSCRIPT_BUILD_PACKAGE_MANAGER)
	include(FetchContent)
	option(VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL "Use a host-provided CURL development package" OFF)
	if(TARGET CURL::libcurl)
		# A module may have already prepared the shared dependency graph.
	elseif(VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL)
		find_package(CURL 7.85 REQUIRED)
	else()
		set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
		set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
		set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
		# Keep dependency policy local; changing the cache here also changes sibling
		# modules and embedded hosts that build shared libraries.
		set(BUILD_SHARED_LIBS OFF)
		set(BUILD_STATIC_LIBS ON)
		set(_pm_testing "${BUILD_TESTING}")
		set(BUILD_TESTING OFF)
		set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
		set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
		set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
		set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
		set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
		set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
		set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
		set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
		if(WIN32)
			set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
			set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
		endif()
		FetchContent_Declare(vero_pm_curl URL https://curl.se/download/curl-8.23.0.tar.xz
			URL_HASH SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
		FetchContent_MakeAvailable(vero_pm_curl)
		set(BUILD_TESTING "${_pm_testing}")
	endif()
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
add_library(VeroScriptPackageZip STATIC ThirdParty/miniz/miniz.c ThirdParty/miniz/miniz_tdef.c
	ThirdParty/miniz/miniz_tinfl.c ThirdParty/miniz/miniz_zip.c)
target_include_directories(VeroScriptPackageZip PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VeroScriptPackageZip PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(VeroScriptPackageZip PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(_pm_runtime_sources
	Private/PackageModel.cpp Private/PackageFilesystem.cpp Private/PackageArchive.cpp
	Private/PackageEnvironment.cpp Private/PackageDeployment.cpp Private/PackageLocalization.cpp
	Private/PackageLibraries.cpp Private/PackageInstalledInternal.cpp Private/PackageJournal.cpp Private/PackageGraph.cpp
	Private/PackageOperation.cpp Private/PackageReadOnly.cpp Private/PackageSources.cpp Private/PackageSignatures.cpp)
add_library(VeroScriptPackageRuntime STATIC ${_pm_runtime_sources})
vs_use_shared_private_headers(VeroScriptPackageRuntime)
target_include_directories(VeroScriptPackageRuntime PRIVATE "${VEROSCRIPT_MODULE_ROOT}/Modules/Network/VeroScriptNetwork/Public")
target_include_directories(VeroScriptPackageRuntime PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
	"$<INSTALL_INTERFACE:include/PackageManager>" PRIVATE ThirdParty "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(VeroScriptPackageRuntime PUBLIC "$<BUILD_INTERFACE:VeroScript>"
	"$<INSTALL_INTERFACE:VeroScript::VeroScript>" PRIVATE VeroScriptPackageZip)
target_compile_features(VeroScriptPackageRuntime PUBLIC cxx_std_17)
target_compile_definitions(VeroScriptPackageRuntime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
vs_apply_cxx_defaults(VeroScriptPackageRuntime)
vs_set_solution_folder(VeroScriptPackageRuntime "Libraries")
if(WIN32)
	target_link_libraries(VeroScriptPackageRuntime PRIVATE shell32 ole32)
endif()
if(VEROSCRIPT_BUILD_PACKAGE_MANAGER)
	file(GLOB _pm_sources CONFIGURE_DEPENDS "Private/*.cpp")
	foreach(_s IN LISTS _pm_runtime_sources)
		list(REMOVE_ITEM _pm_sources "${CMAKE_CURRENT_SOURCE_DIR}/${_s}")
	endforeach()
	add_library(VeroScriptPackageManager STATIC ${_pm_sources})
	target_include_directories(VeroScriptPackageManager PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
		"$<INSTALL_INTERFACE:include/PackageManager>" PRIVATE ThirdParty "${CMAKE_CURRENT_BINARY_DIR}")
	target_link_libraries(VeroScriptPackageManager PUBLIC VeroScriptPackageRuntime PRIVATE CURL::libcurl)
	if(TARGET VeroScriptBytecodeVm)
		target_link_libraries(VeroScriptPackageManager PRIVATE VeroScriptBytecodeVm)
		target_compile_definitions(VeroScriptPackageManager PRIVATE VEROSCRIPT_PACKAGE_MANAGER_DEFAULT_VM=1)
		set(VEROSCRIPT_PACKAGE_MANAGER_HAS_VM ON)
	else()
		set(VEROSCRIPT_PACKAGE_MANAGER_HAS_VM OFF)
	endif()
	target_compile_features(VeroScriptPackageManager PUBLIC cxx_std_17)
	target_precompile_headers(VeroScriptPackageManager PRIVATE Private/PackageInternal.h)
	target_compile_definitions(VeroScriptPackageManager PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
	vs_apply_cxx_defaults(VeroScriptPackageManager)
	vs_set_solution_folder(VeroScriptPackageManager "Libraries")
endif()
include(CMakePackageConfigHelpers)
configure_package_config_file(Resources/Tools/CMake/VeroScriptPackageManagerConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageManagerConfig.cmake"
	INSTALL_DESTINATION lib/cmake/VeroScriptPackageManager)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageManagerConfigVersion.cmake"
	VERSION 0.1.1 COMPATIBILITY ExactVersion)
configure_package_config_file(Resources/Tools/CMake/VeroScriptPackageRuntimeConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageRuntimeConfig.cmake"
	INSTALL_DESTINATION lib/cmake/VeroScriptPackageRuntime)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageRuntimeConfigVersion.cmake"
	VERSION 0.1.1 COMPATIBILITY ExactVersion)
install(TARGETS VeroScriptPackageRuntime VeroScriptPackageZip EXPORT VeroScriptPackageRuntimeTargets
	ARCHIVE DESTINATION lib COMPONENT VeroScriptPackageRuntime)
install(EXPORT VeroScriptPackageRuntimeTargets NAMESPACE VeroScript:: DESTINATION lib/cmake/VeroScriptPackageRuntime COMPONENT VeroScriptPackageRuntime)
install(FILES Public/VeroScriptPackageManager.h DESTINATION include/PackageManager COMPONENT VeroScriptPackageRuntime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageRuntimeConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageRuntimeConfigVersion.cmake"
	DESTINATION lib/cmake/VeroScriptPackageRuntime COMPONENT VeroScriptPackageRuntime)
install(CODE "
    file(SHA256 \"$<TARGET_FILE:VeroScript>\" _pm_core_sha)
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/lib/cmake/VeroScriptPackageRuntime/VeroScriptPackageRuntimeCoreBuild.cmake\"
        \"set(VeroScriptPackageRuntime_CORE_BUILD_ID \\\"\${_pm_core_sha}\\\")\\n\")
" COMPONENT VeroScriptPackageRuntime)
if(TARGET VeroScriptPackageManager)
	# Include the actual bundled dependency, including when a sibling backend
	# created it first. Imported curl remains an external SDK dependency.
	get_target_property(_pm_curl_imported CURL::libcurl IMPORTED)
	if(NOT _pm_curl_imported AND IS_ABSOLUTE "${CURL_BINARY_DIR}")
		install(CODE "
            set(_pm_curl_install_component \"\${CMAKE_INSTALL_COMPONENT}\")
            set(CMAKE_INSTALL_COMPONENT \"Unspecified\")
            include(\"${CURL_BINARY_DIR}/cmake_install.cmake\")
            set(CMAKE_INSTALL_COMPONENT \"\${_pm_curl_install_component}\")
            unset(_pm_curl_install_component)
        " COMPONENT VeroScriptPackageManager)
	endif()
	# Installing the full client also installs its local runtime dependency.
	install(TARGETS VeroScriptPackageRuntime VeroScriptPackageZip ARCHIVE DESTINATION lib COMPONENT VeroScriptPackageManager)
	install(EXPORT VeroScriptPackageRuntimeTargets NAMESPACE VeroScript:: DESTINATION lib/cmake/VeroScriptPackageRuntime COMPONENT VeroScriptPackageManager)
	install(FILES Public/VeroScriptPackageManager.h DESTINATION include/PackageManager COMPONENT VeroScriptPackageManager)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageRuntimeConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageRuntimeConfigVersion.cmake"
		DESTINATION lib/cmake/VeroScriptPackageRuntime COMPONENT VeroScriptPackageManager)
	install(TARGETS VeroScriptPackageManager EXPORT VeroScriptPackageManagerTargets
		ARCHIVE DESTINATION lib COMPONENT VeroScriptPackageManager)
	install(EXPORT VeroScriptPackageManagerTargets NAMESPACE VeroScript:: DESTINATION lib/cmake/VeroScriptPackageManager COMPONENT VeroScriptPackageManager)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageManagerConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/VeroScriptPackageManagerConfigVersion.cmake"
		DESTINATION lib/cmake/VeroScriptPackageManager COMPONENT VeroScriptPackageManager)
	install(CODE "
        file(SHA256 \"$<TARGET_FILE:VeroScript>\" _pm_core_sha)
        file(WRITE \"\${CMAKE_INSTALL_PREFIX}/lib/cmake/VeroScriptPackageManager/VeroScriptPackageManagerCoreBuild.cmake\"
            \"set(VeroScriptPackageManager_CORE_BUILD_ID \\\"\${_pm_core_sha}\\\")\\n\")
        file(WRITE \"\${CMAKE_INSTALL_PREFIX}/lib/cmake/VeroScriptPackageRuntime/VeroScriptPackageRuntimeCoreBuild.cmake\"
            \"set(VeroScriptPackageRuntime_CORE_BUILD_ID \\\"\${_pm_core_sha}\\\")\\n\")
    " COMPONENT VeroScriptPackageManager)
endif()
install(FILES ThirdParty/json/LICENSE.MIT DESTINATION licenses/package-runtime/json COMPONENT VeroScriptPackageRuntime)
install(FILES ThirdParty/miniz/LICENSE DESTINATION licenses/package-runtime/miniz COMPONENT VeroScriptPackageRuntime)
install(FILES ThirdParty/sha256/LICENSE DESTINATION licenses/package-runtime/sha256 COMPONENT VeroScriptPackageRuntime)
install(DIRECTORY Resources/Schemas
	DESTINATION share/VeroScript/package-manager/Resources COMPONENT VeroScriptPackageManager)
install(FILES ThirdParty/sources.json DESTINATION share/VeroScript/package-manager/ThirdParty COMPONENT VeroScriptPackageManager)
install(FILES ThirdParty/json/LICENSE.MIT DESTINATION licenses/package-manager/json COMPONENT VeroScriptPackageManager)
install(FILES ThirdParty/miniz/LICENSE DESTINATION licenses/package-manager/miniz COMPONENT VeroScriptPackageManager)
install(FILES ThirdParty/sha256/LICENSE DESTINATION licenses/package-manager/sha256 COMPONENT VeroScriptPackageManager)
install(FILES ThirdParty/curl/COPYING DESTINATION licenses/package-manager/curl COMPONENT VeroScriptPackageManager)
install(FILES ThirdParty/sources.json DESTINATION licenses/package-manager COMPONENT VeroScriptPackageManager)
if(BUILD_TESTING AND TARGET VeroScriptPackageManager)
	add_executable(VeroScriptPackageManagerTests Private/Tests/PackageManagerTests.cpp)
	target_include_directories(VeroScriptPackageManagerTests PRIVATE ThirdParty "${CMAKE_CURRENT_BINARY_DIR}")
	target_link_libraries(VeroScriptPackageManagerTests PRIVATE VeroScriptPackageManager VeroScriptCpp)
	vs_apply_cxx_defaults(VeroScriptPackageManagerTests)
	set(_pm_fixture_args)
	if(TARGET VeroScriptNativeExtensionFixture)
		add_dependencies(VeroScriptPackageManagerTests VeroScriptNativeExtensionFixture)
		list(APPEND _pm_fixture_args "$<TARGET_FILE:VeroScriptNativeExtensionFixture>")
		if(TARGET VeroScriptAsmJitBackend)
			add_dependencies(VeroScriptPackageManagerTests VeroScriptAsmJitBackend)
			list(APPEND _pm_fixture_args "$<TARGET_FILE:VeroScriptAsmJitBackend>")
		endif()
	endif()
	add_test(NAME VeroScriptPackageManagerTests COMMAND VeroScriptPackageManagerTests ${_pm_fixture_args})
	set_tests_properties(VeroScriptPackageManagerTests PROPERTIES LABELS "VeroScript;PackageManager" TIMEOUT 180)
endif()
