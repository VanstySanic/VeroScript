# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

vs_add_module_library(VeroScriptMobile)
get_target_property(_mobile_sources VeroScriptMobile SOURCES)
list(FILTER _mobile_sources EXCLUDE REGEX "/Tests/")
set_property(TARGET VeroScriptMobile PROPERTY SOURCES "${_mobile_sources}")
target_link_libraries(VeroScriptMobile PUBLIC VeroScriptInterop)
set_target_properties(VeroScriptMobile PROPERTIES POSITION_INDEPENDENT_CODE ON)
vs_set_solution_folder(VeroScriptMobile "Libraries/Mobile")
if(BUILD_TESTING)
	add_executable(VeroScriptMobileTests Private/Tests/VeroScriptMobileTests.cpp)
	target_link_libraries(VeroScriptMobileTests PRIVATE VeroScriptMobile)
	target_compile_definitions(VeroScriptMobileTests PRIVATE
		VEROSCRIPT_TEST_MOBILE_CATALOGS="${CMAKE_CURRENT_SOURCE_DIR}/Resources/Localization"
		VEROSCRIPT_TEST_STDLIB="${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib")
	vs_apply_cxx_defaults(VeroScriptMobileTests)
	add_test(NAME VeroScriptMobileTests COMMAND VeroScriptMobileTests)
	set_tests_properties(VeroScriptMobileTests PROPERTIES TIMEOUT 30)
	if(CMAKE_CROSSCOMPILING AND CMAKE_CROSSCOMPILING_EMULATOR)
		target_compile_definitions(VeroScriptMobileTests PRIVATE VEROSCRIPT_TEST_EMULATOR=1)
		set_tests_properties(VeroScriptMobileTests PROPERTIES TIMEOUT 120)
	endif()
endif()

# This compiles the real JNI adapter against a desktop JDK and a test-only
# AssetManager fixture. It checks the boundary; it is not Android device testing.
option(VEROSCRIPT_TEST_ANDROID_JNI_HOST "Enable desktop JNI boundary smoke test (requires a JDK)" OFF)
if(VEROSCRIPT_TEST_ANDROID_JNI_HOST AND BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT VEROSCRIPT_MOBILE_TARGET)
	# This adapter uses JNI only; headless JDKs do not provide AWT.
	find_package(JNI REQUIRED COMPONENTS JVM)
	find_package(Java 17 REQUIRED COMPONENTS Runtime)
	if(NOT Java_JAVAC_EXECUTABLE)
		message(FATAL_ERROR "Desktop JNI smoke tests require javac in JAVA_HOME")
	endif()
	if(VEROSCRIPT_INTEROP_STATIC)
		message(FATAL_ERROR "Desktop JNI smoke tests require the shared VeroScriptInterop target")
	endif()
	set(_android_root "${VEROSCRIPT_MODULE_ROOT}/Extensions/Platform/Mobile/VeroScriptAndroid")
	add_library(VeroScriptAndroidHostSmoke SHARED "${_android_root}/Private/VeroScriptAndroid.cpp")
	target_include_directories(VeroScriptAndroidHostSmoke PRIVATE ${JNI_INCLUDE_DIRS})
	target_link_libraries(VeroScriptAndroidHostSmoke PRIVATE VeroScriptMobile)
	set_target_properties(VeroScriptAndroidHostSmoke PROPERTIES OUTPUT_NAME VeroScriptAndroid)
	vs_apply_cxx_defaults(VeroScriptAndroidHostSmoke)
	set(_java_sources
		"${_android_root}/src/main/java/com/vanstysanic/veroscript/android/VeroScriptEngine.java"
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tests/AndroidHost/android/content/res/AssetManager.java"
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Tests/AndroidHost/MobileSmoke.java")
	set(_java_classes "${CMAKE_CURRENT_BINARY_DIR}/java-host-classes")
	add_custom_command(OUTPUT "${_java_classes}/MobileSmoke.class"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${_java_classes}"
		COMMAND "${Java_JAVAC_EXECUTABLE}" -encoding UTF-8 -d "${_java_classes}" ${_java_sources}
		DEPENDS ${_java_sources} VERBATIM)
	add_custom_target(VeroScriptAndroidHostJava DEPENDS "${_java_classes}/MobileSmoke.class")
	add_dependencies(VeroScriptAndroidHostSmoke VeroScriptAndroidHostJava)
	set(_android_host_vm_args "")
	if(TARGET VeroScriptBytecodeVmBackend)
		set(_android_host_vm_args "$<TARGET_FILE:VeroScriptBytecodeVmBackend>")
		add_dependencies(VeroScriptAndroidHostSmoke VeroScriptBytecodeVmBackend)
	endif()
	add_test(NAME VeroScriptAndroidJniHostSmoke COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni
		"-Djava.library.path=$<TARGET_FILE_DIR:VeroScriptAndroidHostSmoke>" -cp "${_java_classes}" MobileSmoke
		"${VEROSCRIPT_MODULE_ROOT}/VeroScript/Resources/Scripts/stdlib"
		"${CMAKE_CURRENT_SOURCE_DIR}/Resources/Localization" ${_android_host_vm_args})
	set_tests_properties(VeroScriptAndroidJniHostSmoke PROPERTIES TIMEOUT 30 LABELS "Mobile;JNI;HostOnly")
endif()
