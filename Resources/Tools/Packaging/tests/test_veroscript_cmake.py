# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise source integration without building the runtime test suite."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[4]
CMAKE = shutil.which('cmake')


@unittest.skipUnless(CMAKE, 'CMake is required for native integration checks')
class CMakeIntegrationTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix='veroscript-cmake-')
		self.addCleanup(self.temporary.cleanup)
		self.work = Path(self.temporary.name).resolve()

	def run_cmake(self, *arguments):
		result = subprocess.run([CMAKE, *map(str, arguments)], cwd=self.work,
								capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=180)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		return result

	def test_in_source_build_is_rejected_before_generation(self):
		source = self.work / 'source'
		source.mkdir()
		shutil.copy2(ROOT / 'CMakeLists.txt', source / 'CMakeLists.txt')
		result = subprocess.run([CMAKE, '-S', str(source), '-B', str(source)],
								capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn('requires an out-of-source build', result.stdout + result.stderr)





	def test_visual_aggregate_install_preserves_other_components(self):
		source, build = self.work / 'source', self.work / 'build'
		source.mkdir()
		(source / 'payload.txt').write_text('installed payload', encoding='utf-8')
		(source / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.25)
project(InstallComponents NONE)
include("{(ROOT / 'Resources/Tools/CMake/VeroScriptFoundationSdk.cmake').as_posix()}")
install(FILES payload.txt DESTINATION platform COMPONENT VeroScriptPlatform)
set(VEROSCRIPT_VISUAL_COMPONENTS Platform)
vs_configure_foundation_release()
install(FILES payload.txt DESTINATION later COMPONENT LaterModule)
''', encoding='utf-8')
		self.run_cmake('-S', source, '-B', build)
		for component, platform, later in ((None, True, True), ('VeroScriptVisual', True, False), ('LaterModule', False, True)):
			with self.subTest(component=component):
				prefix = self.work / (component or 'all')
				arguments = ['--install', build, '--prefix', prefix]
				if component:
					arguments += ['--component', component]
				self.run_cmake(*arguments)
				self.assertEqual((prefix / 'platform/payload.txt').is_file(), platform)
				self.assertEqual((prefix / 'later/payload.txt').is_file(), later)
				manifest = build / ('install_manifest_' + component + '.txt' if component else 'install_manifest.txt')
				installed = {Path(path) for path in manifest.read_text(encoding='utf-8').splitlines()}
				self.assertEqual(prefix / 'platform/payload.txt' in installed, platform)
				self.assertEqual(prefix / 'later/payload.txt' in installed, later)

	def test_core_cli_regressions_keep_their_host_policy_with_optional_gui(self):
		for gui, scripting in (('ON', 'ON'), ('OFF', 'ON'), ('ON', 'OFF'), ('ON', None)):
			with self.subTest(gui=gui, scripting=scripting):
				source = self.work / f'cli-{gui}-{scripting}'
				source.mkdir()
				(source / 'dummy.cpp').write_text('int dummy(){return 0;}\n', encoding='utf-8')
				(source / 'main.cpp').write_text(
					'#ifndef VEROSCRIPT_WITH_PACKAGE_MANAGER\n#error missing package client\n#endif\n'
					'#ifndef VEROSCRIPT_RUNTIME_HAS_VM\n#error missing VM host\n#endif\n'
					'#ifdef VEROSCRIPT_WITH_GUI\n#error GUI leaked into Core host\n#endif\n'
					'int main(){return 0;}\n', encoding='utf-8')
				setting = f'set(VEROSCRIPT_GUI_SCRIPTING {scripting})\n' if scripting else ''
				separate = gui == 'ON' and scripting != 'OFF'
				target = 'VeroScriptCliCoreTests' if separate else 'VeroScriptCli'
				(source / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.25)
project(CliProfile LANGUAGES CXX)
enable_testing()
set(BUILD_TESTING ON)
set(VEROSCRIPT_BUILD_GUI {gui})
{setting}set(VEROSCRIPT_MODULE_ROOT "{ROOT.as_posix()}")
set(CMAKE_DISABLE_FIND_PACKAGE_Python3 TRUE)
function(vs_apply_cxx_defaults target)
  target_compile_features(${{target}} PRIVATE cxx_std_17)
endfunction()
function(vs_set_solution_folder target folder)
endfunction()
function(vs_add_cli_executable target)
  cmake_parse_arguments(ARG "" "" "SOURCES;LINK_LIBRARIES" ${{ARGN}})
  if(target STREQUAL "VeroScriptCliCoreTests")
    add_executable(${{target}} ${{ARG_SOURCES}})
  else()
    add_executable(${{target}} "{(source / 'main.cpp').as_posix()}")
  endif()
  target_link_libraries(${{target}} PRIVATE ${{ARG_LINK_LIBRARIES}})
endfunction()
add_library(VeroScript STATIC dummy.cpp)
add_library(VeroScriptPackageManager STATIC dummy.cpp)
foreach(target VeroScriptCpp VeroScriptCppAot VeroScriptCSharpAot VeroScriptPythonAot VeroScriptTooling)
  add_library(${{target}} INTERFACE)
endforeach()
add_custom_target(VeroScriptBytecodeVmBackend)
add_custom_target(VeroScriptRuntimeVmAssets)
add_subdirectory("{(ROOT / 'VeroScriptCli').as_posix()}" cli)
get_target_property(actual VeroScriptTestCli ALIASED_TARGET)
if(NOT actual STREQUAL "{target}")
  message(FATAL_ERROR "Wrong CLI test host: ${{actual}}")
endif()
''', encoding='utf-8')
				if separate:
					with (source / 'CMakeLists.txt').open('a', encoding='utf-8') as output:
						output.write('target_compile_definitions(VeroScriptCli PRIVATE VEROSCRIPT_WITH_GUI=1)\n')
				build = source / 'build'
				self.run_cmake('-S', source, '-B', build)
				self.run_cmake('--build', build, '--config', 'Release', '--target', target)

	def test_project_init_cli_tests_run_only_on_build_host(self):
		for cross in (False, True):
			for build_services in (False, True):
				with self.subTest(cross=cross, build_services=build_services):
					source = self.work / f'project-init-{cross}-{build_services}'
					source.mkdir()
					(source / 'dummy.cpp').write_text('int main(){return 0;}\n', encoding='utf-8', newline='\n')
					(source / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.25)
project(ProjectInitialization LANGUAGES CXX)
enable_testing()
set(BUILD_TESTING ON)
set(CMAKE_CROSSCOMPILING {'TRUE' if cross else 'FALSE'})
set(Python3_EXECUTABLE "{Path(sys.executable).as_posix()}")
set(VEROSCRIPT_MODULE_ROOT "{ROOT.as_posix()}")
function(vs_apply_cxx_defaults target)
endfunction()
function(vs_set_solution_folder target folder)
endfunction()
function(vs_add_cli_executable target)
  add_executable(${{target}} "{(source / 'dummy.cpp').as_posix()}")
endfunction()
add_library(VeroScript STATIC dummy.cpp)
foreach(target VeroScriptCpp VeroScriptCppAot VeroScriptCSharpAot VeroScriptPythonAot VeroScriptTooling)
  add_library(${{target}} INTERFACE)
endforeach()
add_custom_target(VeroScriptBytecodeVmBackend)
add_custom_target(VeroScriptRuntimeVmAssets)
if({'TRUE' if build_services else 'FALSE'})
  add_library(VeroScriptBuild INTERFACE)
endif()
add_subdirectory("{(ROOT / 'VeroScriptCli').as_posix()}" cli)
''', encoding='utf-8', newline='\n')
					build = source / 'build'
					self.run_cmake('-S', source, '-B', build)
					lines = (build / 'cli/CTestTestfile.cmake').read_text(encoding='utf-8').splitlines()
					commands = [line for line in lines if line.lstrip().startswith('add_test(')
						and 'VeroScriptProjectInitCliTests' in line and 'NOT_AVAILABLE' not in line]
					self.assertEqual(bool(commands), not cross)
					for command in commands:
						self.assertEqual('--build' in command, build_services)

	def test_project_init_compiles_with_cpp17_and_cpp20(self):
		source = self.work / 'project-init-standards'
		source.mkdir()
		(source / 'main.cpp').write_text('''
#include "VeroScriptCliProject.h"
#include <sstream>
int main()
{
	std::ostringstream output, errors;
	return VeroScript::Cli::RunProject({"--help"}, {}, output, errors);
}
''', encoding='utf-8', newline='\n')
		(source / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.25)
project(ProjectInitializationStandards LANGUAGES CXX)
enable_testing()
foreach(standard 17 20)
  add_executable(init${{standard}} main.cpp "{(ROOT / 'VeroScriptCli/Private/VeroScriptCliProject.cpp').as_posix()}")
  target_include_directories(init${{standard}} PRIVATE "{(ROOT / 'VeroScriptCli/Private').as_posix()}")
  set_target_properties(init${{standard}} PROPERTIES CXX_STANDARD ${{standard}} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(init${{standard}} PRIVATE /W4 /WX /utf-8 /permissive-)
  else()
    target_compile_options(init${{standard}} PRIVATE -Wall -Wextra -Werror -pedantic)
  endif()
  add_test(NAME init${{standard}} COMMAND init${{standard}})
endforeach()
''', encoding='utf-8', newline='\n')
		build = source / 'build'
		self.run_cmake('-S', source, '-B', build)
		self.run_cmake('--build', build, '--config', 'Release', '--parallel', '1')
		ctest = Path(CMAKE).with_name('ctest.exe' if sys.platform == 'win32' else 'ctest')
		result = subprocess.run([str(ctest), '--test-dir', str(build), '-C', 'Release', '--output-on-failure', '--no-tests=error'],
			capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_cpp17_requirement_reaches_consumer(self):
		source, build = self.work / 'source', self.work / 'build'
		module = source / 'module'
		(module / 'Public').mkdir(parents=True)
		(module / 'Private').mkdir()
		(module / 'Public/Example.h').write_text(
			'#include <string_view>\nstd::string_view example();\n', encoding='utf-8')
		(module / 'Private/Example.cpp').write_text(
			'#include "Example.h"\nstd::string_view example() { return "ready"; }\n', encoding='utf-8')
		(module / 'CMakeLists.txt').write_text('vs_add_module_library(Example)\n', encoding='utf-8')
		(source / 'main.cpp').write_text(
			'#include "Example.h"\nint main() { return example() == "ready" ? 0 : 1; }\n', encoding='utf-8')
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\n'
			'project(CxxConsumer LANGUAGES CXX)\n'
			'set(CMAKE_CXX_STANDARD 11)\n'
			f'include("{(ROOT / "Resources/Tools/CMake/VeroScriptHelpers.cmake").as_posix()}")\n'
			'add_subdirectory(module)\n'
			'add_executable(consumer main.cpp)\n'
			'target_link_libraries(consumer PRIVATE Example)\n', encoding='utf-8')
		self.run_cmake('-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release')
		self.run_cmake('--build', build, '--config', 'Release', '--target', 'consumer', '--parallel', '2')

	def test_shared_private_headers_are_core_free_and_do_not_leak(self):
		source, build = self.work / 'renamed source', self.work / 'build'
		helpers = source / 'Resources/Tools/CMake'
		helpers.mkdir(parents=True)
		shutil.copy2(ROOT / 'Resources/Tools/CMake/VeroScriptHelpers.cmake', helpers)
		shared = source / 'Shared/Private'
		shared.mkdir(parents=True)
		for name in ('VeroScriptFloatParsing.h', 'VeroScriptFloatFormatting.h', 'VeroScriptJsonString.h'):
			shutil.copy2(ROOT / 'Shared/Private' / name, shared)
		module = source / 'module'
		(module / 'Public').mkdir(parents=True)
		(module / 'Private').mkdir()
		(module / 'Public/Example.h').write_text('double shared_number();\n', encoding='utf-8')
		(module / 'Private/Example.cpp').write_text(
			'#include "Example.h"\n#include "VeroScriptFloatParsing.h"\n'
			'#include "VeroScriptFloatFormatting.h"\n'
			'#include "VeroScriptJsonString.h"\n'
			'double shared_number() { double value = 0.;\n'
			'auto parsed = VeroScript::FloatParsingDetail::FromCharsDecimalFallback("1.5", value);\n'
			'char text[32]; auto formatted = VeroScript::FloatFormattingDetail::ToCharsGeneral(text, text + sizeof(text), value);\n'
			'auto escaped = VeroScript::JsonStringDetail::Escape("text",\n'
			'VeroScript::JsonStringDetail::EControlEscapeStyle::Unicode);\n'
			'return parsed.ec == std::errc{} && formatted.ec == std::errc{} && escaped == "text" ? value : 0.; }\n', encoding='utf-8')
		(module / 'CMakeLists.txt').write_text(
			'vs_add_module_library(Example)\nvs_use_shared_private_headers(Example)\n'
			'get_target_property(public_includes Example INTERFACE_INCLUDE_DIRECTORIES)\n'
			'if(public_includes MATCHES "Shared/Private")\n'
			'  message(FATAL_ERROR "Private support leaked into the exported interface")\n'
			'endif()\n', encoding='utf-8')
		(source / 'main.cpp').write_text(
			'#include "Example.h"\nint main() { return shared_number() == 1.5 ? 0 : 1; }\n', encoding='utf-8')
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\nproject(PrivateSupport LANGUAGES CXX)\n'
			'include(Resources/Tools/CMake/VeroScriptHelpers.cmake)\nadd_subdirectory(module)\n'
			'add_executable(consumer main.cpp)\ntarget_link_libraries(consumer PRIVATE Example)\n'
			'if(TARGET VeroScript)\nmessage(FATAL_ERROR "Private support pulled in Core")\nendif()\n', encoding='utf-8')
		self.run_cmake('-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release')
		self.run_cmake('--build', build, '--config', 'Release', '--target', 'consumer', '--parallel', '2')

	def test_nested_project_has_own_build_and_runtime_assets(self):
		source, build = self.work / 'host source', self.work / 'host build'
		source.mkdir()
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\n'
			'project(Host LANGUAGES CXX)\n'
			f'add_subdirectory("{ROOT.as_posix()}" embedded)\n'
			'get_target_property(core_binary VeroScript BINARY_DIR)\n'
			'if(NOT core_binary STREQUAL "${CMAKE_BINARY_DIR}/embedded/VeroScript")\n'
			'  message(FATAL_ERROR "Core escaped its binary directory: ${core_binary}")\n'
			'endif()\n', encoding='utf-8')
		self.run_cmake('-S', source, '-B', build, '-DBUILD_TESTING=OFF',
					   '-DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON',
					   '-DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF',
					   '-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF', '-DCMAKE_BUILD_TYPE=Release')
		self.run_cmake('--build', build, '--config', 'Release', '--target', 'VeroScriptRuntimeAssets')
		output = build / 'embedded/bin'
		catalogs = list(output.rglob('localization/**/*.json'))
		self.assertTrue(catalogs, 'No localization catalogs were deployed')
		self.assertTrue(list(output.rglob('stdlib/delegate.vs')), 'No standard library was deployed')
		self.assertFalse((source / 'VeroScript').exists(), 'Build wrote into the host source tree')

	def test_json_test_setup_reuses_helper_from_a_sibling_directory(self):
		source, build = self.work / 'source', self.work / 'build'
		preload = source / 'preload'
		preload.mkdir(parents=True)
		(source / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
		(preload / 'CMakeLists.txt').write_text(
			f'set(VEROSCRIPT_MODULE_ROOT "{ROOT.as_posix()}")\n'
			f'include("{(ROOT / "Resources/Tools/CMake/VeroScriptVmTests.cmake").as_posix()}")\n', encoding='utf-8')
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\n'
			'project(JsonTestSetup LANGUAGES C CXX)\n'
			'set(BUILD_TESTING ON)\n'
			'add_library(VeroScript INTERFACE IMPORTED GLOBAL)\n'
			'add_library(VeroScript::VeroScript ALIAS VeroScript)\n'
			f'target_include_directories(VeroScript INTERFACE "{(ROOT / "VeroScript/Public").as_posix()}")\n'
			'add_library(VeroScriptBytecodeVmBackend MODULE main.cpp)\n'
			'add_subdirectory(preload)\n'
			f'add_subdirectory("{(ROOT / "Modules/Network/VeroScriptJson").as_posix()}" json)\n'
			'get_target_property(includes VeroScriptJsonBindingTests INCLUDE_DIRECTORIES)\n'
			f'if(NOT "{(ROOT / "VeroScript/Private/Tests").as_posix()}" IN_LIST includes)\n'
			'  message(FATAL_ERROR "JSON binding tests lost their Core test include directory")\n'
			'endif()\n', encoding='utf-8')
		self.run_cmake('-S', source, '-B', build)

	def test_interop_minimal_helpers_keep_shared_headers_private(self):
		source, build = self.work / 'source', self.work / 'build'
		source.mkdir()
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\n'
			'project(InteropFallback LANGUAGES CXX)\n'
			'set(BUILD_TESTING OFF)\nset(VEROSCRIPT_INTEROP_STATIC ON)\n'
			'set(VEROSCRIPT_INTEROP_DEFAULT_VM OFF)\n'
			f'set(VEROSCRIPT_MODULE_ROOT "{ROOT.as_posix()}")\n'
			f'include("{(ROOT / "Resources/Tools/CMake/VeroScriptLocalization.cmake").as_posix()}")\n'
			'add_library(VeroScriptCpp INTERFACE)\n'
			f'add_subdirectory("{(ROOT / "VeroScriptInterop").as_posix()}" interop)\n'
			'get_target_property(includes VeroScriptInterop INCLUDE_DIRECTORIES)\n'
			'set(found OFF)\nforeach(directory IN LISTS includes)\n'
			'  if(EXISTS "${directory}/VeroScriptJsonString.h")\nset(found ON)\nendif()\n'
			'endforeach()\nif(NOT found)\nmessage(FATAL_ERROR "Missing private shared header")\nendif()\n'
			'get_target_property(public_includes VeroScriptInterop INTERFACE_INCLUDE_DIRECTORIES)\n'
			'if(public_includes MATCHES "Shared/Private")\n'
			'  message(FATAL_ERROR "Private support leaked into the exported interface")\n'
			'endif()\n', encoding='utf-8')
		self.run_cmake('-S', source, '-B', build)

	def test_native_sdk_preparation_builds_product_and_optional_backend(self):
		source = self.work / 'source'
		source.mkdir()
		(source / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\nproject(SdkPrerequisites LANGUAGES CXX)\n'
			'add_library(VeroScriptInterop MODULE main.cpp)\n'
			'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/product-$<CONFIG>.txt" '
			'CONTENT "$<TARGET_FILE:VeroScriptInterop>")\n'
			'if(TEST_BACKEND)\nadd_library(VeroScriptAsmJitBackend MODULE main.cpp)\n'
			'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/backend-$<CONFIG>.txt" '
			'CONTENT "$<TARGET_FILE:VeroScriptAsmJitBackend>")\nendif()\n', encoding='utf-8')
		script = (ROOT / 'Resources/Tools/CMake/VeroScriptInteropNativeSdkTest.cmake').read_text(encoding='utf-8')
		setup = script.split('execute_process(COMMAND "${CMAKE_COMMAND}" --install', 1)[0]
		(source / 'prepare.cmake').write_text(setup, encoding='utf-8')
		for use_backend in (False, True):
			with self.subTest(jit=use_backend):
				build = self.work / ('with-backend' if use_backend else 'without-backend')
				self.run_cmake('-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release',
					'-DTEST_BACKEND=' + ('ON' if use_backend else 'OFF'))
				arguments = ['-DBUILD=' + str(build), '-DCONFIG=Release']
				if use_backend:
					arguments.append('-DJIT_BACKEND_TARGET=VeroScriptAsmJitBackend')
				self.run_cmake(*arguments, '-P', source / 'prepare.cmake')
				self.assertTrue(Path((build / 'product-Release.txt').read_text(encoding='utf-8')).is_file())
				if use_backend:
					self.assertTrue(Path((build / 'backend-Release.txt').read_text(encoding='utf-8')).is_file())

	def test_interop_test_builds_its_configured_jit_backend(self):
		source, build = self.work / 'source', self.work / 'build'
		source.mkdir()
		(source / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
		interop = (ROOT / 'VeroScriptInterop/CMakeLists.txt').read_text(encoding='utf-8')
		setup = interop[interop.index('if(BUILD_TESTING AND TARGET VeroScriptAsmJitBackend)'):]
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\n'
			'project(InteropBackendDependency LANGUAGES CXX)\n'
			'enable_testing()\nset(BUILD_TESTING ON)\nset(VEROSCRIPT_INTEROP_STATIC ON)\n'
			'add_library(VeroScriptAsmJitBackend MODULE main.cpp)\n'
			'foreach(name Tests ExtendedTests MemoryTests LanguageStateTests)\n'
			'  add_executable(VeroScriptInterop${name} main.cpp)\n'
			'  add_test(NAME VeroScriptInterop${name} COMMAND VeroScriptInterop${name})\n'
			'endforeach()\n'
			'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/backend-$<CONFIG>.txt" '
			'CONTENT "$<TARGET_FILE:VeroScriptAsmJitBackend>")\n' + setup, encoding='utf-8')
		self.run_cmake('-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release')
		self.run_cmake('--build', build, '--config', 'Release', '--target', 'VeroScriptInteropTests', '--parallel', '2')
		backend = Path((build / 'backend-Release.txt').read_text(encoding='utf-8'))
		self.assertTrue(backend.is_file(), 'Building the test must produce its configured dynamic backend')

	def test_vm_setup_reaches_tests_declared_with_relative_filenames(self):
		source, build = self.work / 'source', self.work / 'build'
		tests = source / 'Resources/Tests'
		tests.mkdir(parents=True)
		(tests / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
		(tests / 'CMakeLists.txt').write_text(
			'add_executable(relative-test main.cpp)\n'
			'add_custom_target(test-assets)\n'
			'add_library(test-contract INTERFACE)\n', encoding='utf-8')
		(source / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
		(source / 'CMakeLists.txt').write_text(
			'cmake_minimum_required(VERSION 3.25)\n'
			'project(VmTestSetup LANGUAGES CXX)\n'
			'set(BUILD_TESTING ON)\n'
			f'set(VEROSCRIPT_MODULE_ROOT "{ROOT.as_posix()}")\n'
			'add_library(VeroScriptBytecodeVmBackend MODULE main.cpp)\n'
			'add_executable(production main.cpp)\n'
			'add_executable(explicit-test Resources/Tests/main.cpp)\n'
			f'include("{(ROOT / "Resources/Tools/CMake/VeroScriptVmTests.cmake").as_posix()}")\n'
			'add_subdirectory(Resources/Tests)\n'
			'function(check_vm_tests)\n'
			'  foreach(target relative-test explicit-test)\n'
			'    get_target_property(includes ${target} INCLUDE_DIRECTORIES)\n'
			'    get_target_property(definitions ${target} COMPILE_DEFINITIONS)\n'
			'    get_target_property(dependencies ${target} MANUALLY_ADDED_DEPENDENCIES)\n'
			'    if(NOT "${VEROSCRIPT_MODULE_ROOT}/VeroScript/Private/Tests" IN_LIST includes OR\n'
			'        NOT definitions MATCHES "VEROSCRIPT_TEST_VM_LIBRARY=" OR\n'
			'        NOT "VeroScriptBytecodeVmBackend" IN_LIST dependencies)\n'
			'      message(FATAL_ERROR "Missing explicit VM setup for ${target}")\n'
			'    endif()\n'
			'  endforeach()\n'
			'  get_target_property(definitions production COMPILE_DEFINITIONS)\n'
			'  if(definitions MATCHES "VEROSCRIPT_TEST_VM_LIBRARY")\n'
			'    message(FATAL_ERROR "VM test setup leaked into a production target")\n'
			'  endif()\n'
			'endfunction()\n'
			'cmake_language(DEFER CALL check_vm_tests)\n', encoding='utf-8')
		self.run_cmake('-S', source, '-B', build)


if __name__ == '__main__':
	unittest.main()
