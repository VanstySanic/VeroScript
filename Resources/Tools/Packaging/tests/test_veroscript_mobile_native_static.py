# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Mobile SDK static contracts. Never compile, create archives or publish.

CMake imports run with LANGUAGES NONE and no library files. They verify actual
package discovery and target properties independently of a native toolchain.
"""
from pathlib import Path
import json
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))

from veroscript_mobile_native import (SLICES, BASE_LIBRARIES, BASE_INCLUDES, BASE_PACKAGES, BASE_SOURCES,
	PRIMARY_HEADERS, ar_objects, verify_android_archive, selector, base_config, version_config, sdk_closure, parse_slice_paths,
	verify_consumer_validation, find_library, core_link_identity, execution_contract_config, consumer_executable)
from veroscript_mobile_release import artifact_names, load_tool
from veroscript_business_release import artifact_business, load_versions
from veroscript_products import (ALL_BUSINESSES, MOBILE_NATIVE_BUSINESSES, MOBILE_EMBEDDING_BUSINESSES,
	SDK_MODULE_BUSINESSES, MODULE_SOURCE_DIRECTORIES, mobile_coverage, target_businesses)
from veroscript_release_plan import release_plan


def relocatable(machine=183, kind=1):
	data = bytearray(64)
	data[:7] = b'\x7fELF\x02\x01\x01'
	struct.pack_into('<HHI', data, 16, kind, machine, 1)
	return bytes(data)


def memory_ar(data, name='member.o/'):
	return b'!<arch>\n' + (name.ljust(16) + '0'.ljust(12) + '0'.ljust(6) + '0'.ljust(6)
		+ '100644'.ljust(8) + str(len(data)).ljust(10) + '`\n').encode() + data + (b'\n' if len(data) & 1 else b'')


class MobileNativeStaticTests(unittest.TestCase):
	def test_consumer_executable_resolves_xcode_platform_directory(self):
		with tempfile.TemporaryDirectory(prefix='vero ${EFFECTIVE_PLATFORM_NAME} consumer ') as temporary:
			root = Path(temporary)
			for name, (_, variant) in SLICES['ios'].items():
				for variable in ('${EFFECTIVE_PLATFORM_NAME}', '$(EFFECTIVE_PLATFORM_NAME)'):
					with self.subTest(slice=name, variable=variable):
						suffix = '-iphoneos' if variant == 'device' else '-iphonesimulator'
						binary = root / ('Release' + suffix) / 'VeroScriptMobileSdkConsumer'
						binary.parent.mkdir(exist_ok=True)
						binary.write_bytes(b'linked target executable')
						(root / 'consumer-Release.txt').write_text(str(root / ('Release' + variable) / binary.name) + '\n')
						self.assertTrue(consumer_executable(root, 'ios', name).samefile(binary))

	def test_consumer_executable_requires_the_exact_owned_output(self):
		with tempfile.TemporaryDirectory(prefix='vero consumer output ') as temporary:
			root = Path(temporary)
			build = root / 'build'
			build.mkdir()
			unrelated = root / 'VeroScriptMobileSdkConsumer'
			unrelated.write_bytes(b'stale executable')
			(build / 'consumer-Release.txt').write_text(str(build / unrelated.name))
			with self.assertRaisesRegex(ValueError, 'did not produce'):
				consumer_executable(build, 'android', 'android-arm64-v8a')
			(build / 'consumer-Release.txt').write_text(str(unrelated))
			with self.assertRaisesRegex(ValueError, 'outside its build directory'):
				consumer_executable(build, 'android', 'android-arm64-v8a')

	def test_consumer_executable_preserves_non_xcode_output_paths(self):
		with tempfile.TemporaryDirectory(prefix='vero $ consumer ') as temporary:
			root = Path(temporary)
			binary = root / 'VeroScriptMobileSdkConsumer'
			binary.write_bytes(b'linked target executable')
			(root / 'consumer-Release.txt').write_text(str(binary))
			self.assertTrue(consumer_executable(root, 'android', 'android-arm64-v8a').samefile(binary))
			self.assertTrue(consumer_executable(root, 'ios', 'ios-arm64').samefile(binary))

	def test_every_product_has_an_explicit_mobile_policy(self):
		for target in SLICES:
			coverage = mobile_coverage(target)
			self.assertEqual(set(coverage), set(ALL_BUSINESSES))
			self.assertTrue(set(MOBILE_NATIVE_BUSINESSES).issubset(target_businesses(target, optional=True)))
			self.assertTrue(set(MOBILE_EMBEDDING_BUSINESSES).issubset(target_businesses(target)))
			for business in SDK_MODULE_BUSINESSES:
				self.assertEqual(coverage[business]['status'], 'optional-native-sdk')
			self.assertEqual(coverage['Runtime']['status'], 'excluded')
		self.assertEqual(mobile_coverage('ios')['Jit']['status'], 'excluded')

	def test_mobile_native_names_route_to_the_existing_businesses(self):
		for target in SLICES:
			for business in MOBILE_NATIVE_BUSINESSES:
				names = artifact_names(business, target, '0.1.1')
				self.assertEqual(len(names), 1)
				self.assertTrue(names[0].endswith('-' + target + '.zip'))
				self.assertEqual(artifact_business('mobile/' + names[0]), business)

	def test_plans_include_cpp_and_opt_in_modules_without_mobile_installers(self):
		config = load_versions(REPO)
		for target in SLICES:
			plan = release_plan(REPO, config, [target], list(target_businesses(target, optional=True)))
			root = plan['directories']['Release']
			self.assertIn(artifact_names('Cpp', target, '0.1.1')[0], root['Extensions']['Language']['Cpp'][target]['0.1.1']['sdks'])
			self.assertIn(artifact_names('Json', target, '0.1.1')[0], root['Modules']['Network']['Json'][target]['0.1.1']['sdks'])
			self.assertNotIn('Installer', json.dumps(root))
			self.assertEqual(set(plan['businessVersions']), set(target_businesses(target, optional=True)))

	def test_embedding_sdk_is_self_contained_and_omits_desktop_tools(self):
		self.assertEqual(sdk_closure('Cpp'), ('CoreSDK', 'Vm', 'Cpp'))
		self.assertEqual(sdk_closure('Interop'), ('CoreSDK', 'Vm', 'Cpp', 'Interop'))
		self.assertEqual(sdk_closure('Vm'), ('Vm',))
		self.assertIn('VeroScriptBytecodeVm', BASE_LIBRARIES['Cpp'])
		self.assertNotIn('Tooling', base_config('CoreSDK', '0.1.1', 'a' * 64))
		for business in MOBILE_NATIVE_BUSINESSES:
			source = REPO / (BASE_SOURCES.get(business) or MODULE_SOURCE_DIRECTORIES[business]) / 'Public'
			self.assertTrue((source / PRIMARY_HEADERS.get(business, 'VeroScript' + business + '.h')).is_file(), business)
		contract = {'VEROSCRIPT_JIT_PUBLIC_ABI': 'a' * 64, 'VEROSCRIPT_CORE_IMPLEMENTATION_ID': 'b' * 64}
		self.assertNotEqual(core_link_identity(contract), core_link_identity({**contract, 'VEROSCRIPT_JIT_PUBLIC_ABI': 'c' * 64}))

	def test_module_closures_include_transitive_services(self):
		self.assertEqual(set(sdk_closure('RmlUi')), {'Platform', 'Graphics', 'Image', 'Font', 'Gui', 'RmlUi'})
		self.assertEqual(set(sdk_closure('FontBackend')), {'Font', 'FontBackend'})
		self.assertEqual(set(sdk_closure('Curl')), {'Network', 'Http', 'Curl'})
		boost = sdk_closure('Boost', {'VEROSCRIPT_BOOST_HAS_HTTP_SERVER': 'ON', 'VEROSCRIPT_BOOST_HAS_WEBSOCKET': 'ON'})
		self.assertEqual(set(boost), {'Network', 'Http', 'WebSocket', 'Boost'})

	def test_android_static_archives_validate_every_object(self):
		for abi, machine in (('arm64-v8a', 183), ('x86_64', 62)):
			self.assertEqual(verify_android_archive(memory_ar(relocatable(machine)), abi), 1)
			for data in (memory_ar(relocatable(3)), memory_ar(relocatable(machine, 3)), memory_ar(b'BC\xc0\xde' + bytes(64)),
				b'!<thin>\n', b'!<arch>\n', memory_ar(relocatable(machine))[:-1]):
				with self.subTest(abi=abi, data=data[:16]), self.assertRaises(ValueError):
					verify_android_archive(data, abi)

	def test_release_archive_lookup_handles_xcode_mobile_output_suffixes(self):
		build = Path('not-written')
		for configuration in ('Release', 'Release-iphoneos', 'Release-iphonesimulator', ''):
			expected = build / 'lib' / configuration / 'libVeroScriptCpp.a'
			with patch.object(Path, 'is_file', lambda path: path == expected):
				self.assertEqual(find_library(build, 'VeroScriptCpp'), expected)
		with patch.object(Path, 'is_file', return_value=True), self.assertRaises(ValueError):
			find_library(build, 'VeroScriptCpp')

	def test_long_archive_member_names_are_bounded(self):
		name = b'a-long-object-name.o\0'
		self.assertEqual(list(ar_objects(memory_ar(name + relocatable(), '#1/' + str(len(name))))), [relocatable()])
		with self.assertRaises(ValueError):
			list(ar_objects(memory_ar(relocatable(), '/500')))

	def test_slice_inputs_require_complete_unique_platform_matrices(self):
		for target in SLICES:
			values = [name + '=input/' + name for name in SLICES[target]]
			self.assertEqual(set(parse_slice_paths(values, target)), set(SLICES[target]))
			for wrong in (values[:-1], values + values[:1], ['wrong=path'], [values[0].split('=')[0] + '=']):
				with self.assertRaises(ValueError):
					parse_slice_paths(wrong, target)

	def test_publishing_requires_cross_link_evidence_for_every_slice(self):
		for target in SLICES:
			validation = {'profile': 'whole-archive-cross-link', 'executed': False, 'slices': dict.fromkeys(SLICES[target], 'a' * 64),
				'templateSha256': hashlib.sha256((TOOLS / 'MobileNativeConsumer/CMakeLists.txt').read_bytes()).hexdigest()}
			verify_consumer_validation(validation, target, REPO)
			for wrong in (None, {}, {**validation, 'executed': True}, {**validation, 'slices': {}},
				{**validation, 'templateSha256': '0' * 64}):
				with self.assertRaises(ValueError):
					verify_consumer_validation(wrong, target, REPO)

	def test_source_build_profile_disables_cached_desktop_and_script_options(self):
		producer = load_tool('Resources/Tools/Packaging/package-veroscript-mobile-native.py', REPO)
		for target in SLICES:
			for name in SLICES[target]:
				args = producer.build_arguments(REPO, Path('not-written'), target, name, ['Cpp', 'RmlUi'], ndk=Path('not-inspected'))
				for option in ('VEROSCRIPT_BUILD_APPLICATION', 'VEROSCRIPT_BUILD_PACKAGE_MANAGER', 'VEROSCRIPT_BUILD_ASMJIT_BACKEND',
					'VEROSCRIPT_GUI_SCRIPTING', 'VEROSCRIPT_VISUAL_SCRIPT_BINDINGS', 'VEROSCRIPT_SDL3_NATIVE_EXTENSION'):
					self.assertIn('-D' + option + '=OFF', args)
				self.assertIn('-DVEROSCRIPT_BUILD_GUI=ON', args)
				self.assertIn('-DVEROSCRIPT_INTEROP_STATIC=ON', args)
				self.assertIn('-DSDL_SHARED=OFF', args)
				self.assertIn('-DVEROSCRIPT_NETWORK_TLS=OFF', args)
				if target == 'android':
					self.assertIn('-DCMAKE_ANDROID_NDK=not-inspected', args)
		args = producer.build_arguments(REPO, Path('not-written'), 'ios', 'ios-arm64', ['Boost'], network_tls=True)
		self.assertIn('-DVEROSCRIPT_NETWORK_TLS=ON', args)

	def test_native_inputs_are_not_ignored_for_unselected_businesses(self):
		entry = load_tool('Resources/Tools/Packaging/package-veroscript-mobile-release.py', REPO)
		values = dict(platform='android', csharp_package=None, csharp_symbols=None, csharp_native_root=None,
			python_sdk_directory=[], python_target_sdk=[], host_python=[], ndk=None, native_sdk=None, jit_aar=None,
			native_cmake_sdk=Path('not-inspected'), native_build_directory=[], native_ndk=None, native_dependency_prefix=[])
		with self.assertRaises(ValueError):
			entry.validate_input_options(SimpleNamespace(**values), ['Android'])
		entry.validate_input_options(SimpleNamespace(**values), ['Cpp'])
		values['native_ndk'] = Path('not-inspected')
		with self.assertRaises(ValueError):
			entry.validate_input_options(SimpleNamespace(**values), ['Cpp'])

	@unittest.skipUnless(shutil.which('cmake'), 'CMake unavailable for configuration-only import checks')
	def test_actual_cmake_discovery_selects_mobile_abi_without_a_compiler(self):
		with tempfile.TemporaryDirectory(prefix='vero-mobile-import-', dir=REPO / '.tmp') as temporary:
			root = Path(temporary)
			for target in SLICES:
				sdk = root / target
				for name in SLICES[target]:
					prefix = sdk / 'slices' / name
					for component in sdk_closure('Interop'):
						(prefix / 'include' / BASE_INCLUDES[component]).mkdir(parents=True, exist_ok=True)
						package = BASE_PACKAGES[component]
						directory = prefix / 'lib/cmake' / package
						directory.mkdir(parents=True)
						(directory / (package + 'Config.cmake')).write_text(base_config(component, '0.1.1', 'a' * 64), encoding='utf-8')
						(directory / (package + 'ConfigVersion.cmake')).write_text(version_config('0.1.1'), encoding='utf-8')
					contract = prefix / 'lib/cmake/VeroScriptExecutionContract'
					contract.mkdir()
					(contract / 'VeroScriptExecutionContractConfig.cmake').write_text(execution_contract_config('a' * 64), encoding='utf-8')
					(contract / 'VeroScriptExecutionContractConfigVersion.cmake').write_text(version_config('0.1.1'), encoding='utf-8')
				for package in (*BASE_PACKAGES.values(), 'VeroScriptExecutionContract'):
					directory = sdk / 'lib/cmake' / package
					directory.mkdir(parents=True)
					(directory / (package + 'Config.cmake')).write_text(selector(package, target), encoding='utf-8')
					(directory / (package + 'ConfigVersion.cmake')).write_text(version_config('0.1.1'), encoding='utf-8')
				for name, (arch, variant) in SLICES[target].items():
					project = root / ('consumer-' + name)
					project.mkdir()
					(project / 'FindThreads.cmake').write_text('set(Threads_FOUND TRUE)\nif(NOT TARGET Threads::Threads)\nadd_library(Threads::Threads INTERFACE IMPORTED)\nendif()\n', encoding='utf-8')
					settings = ('set(CMAKE_SYSTEM_NAME Android)\nset(CMAKE_SYSTEM_VERSION 26)\nset(ANDROID_ABI ' + arch + ')\nset(ANDROID_STL c++_shared)\n' if target == 'android' else
						'set(CMAKE_SYSTEM_NAME iOS)\nset(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)\nset(CMAKE_OSX_SYSROOT ' + ('iphonesimulator' if variant == 'simulator' else 'iphoneos') + ')\nset(CMAKE_OSX_ARCHITECTURES ' + ('x86_64' if arch == 'x64' else arch) + ')\n')
					(project / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(StaticImports LANGUAGES NONE)\n'
						+ settings + 'list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")\n'
						+ f'list(PREPEND CMAKE_PREFIX_PATH "{sdk.as_posix()}")\n'
						+ '''if(VERO_TEST_HOST)
  set(CMAKE_SYSTEM_NAME "${VERO_TEST_HOST}")
endif()
if(VERO_TEST_BUILTIN)
  set(CMAKE_ANDROID_ARCH_ABI "${ANDROID_ABI}")
  set(CMAKE_ANDROID_STL_TYPE "${ANDROID_STL}")
  unset(ANDROID_ABI)
  unset(ANDROID_STL)
endif()
if(VERO_TEST_STATIC_STL)
  set(CMAKE_ANDROID_STL_TYPE c++_static)
endif()
if(VERO_TEST_NDK_API)
  set(CMAKE_SYSTEM_VERSION 1)
  set(ANDROID_PLATFORM "${VERO_TEST_NDK_API}")
endif()
if(VERO_TEST_PREEXISTING)
  add_library(VeroScript::VeroScript STATIC IMPORTED)
endif()
set(VERO_TEST_COMPONENT Runtime CACHE STRING "Requested C++ SDK component")
find_package(VeroScriptCpp 0.1.1 EXACT REQUIRED COMPONENTS ${VERO_TEST_COMPONENT})
find_package(VeroScriptInterop 0.1.1 EXACT REQUIRED)
get_target_property(location VeroScript::VeroScriptCpp IMPORTED_LOCATION)
get_target_property(definitions VeroScript::VeroScriptInterop INTERFACE_COMPILE_DEFINITIONS)
if(NOT definitions MATCHES "VS_INTEROP_STATIC=1" OR TARGET VeroScript::VeroScriptTooling)
  message(FATAL_ERROR "Wrong mobile import contract")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${location}")
''', encoding='utf-8')
					result = subprocess.run(['cmake', '-S', str(project), '-B', str(project / 'build')], capture_output=True, text=True)
					self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
					self.assertIn('/slices/' + name + '/lib/libVeroScriptCpp.a', (project / 'build/selected.txt').read_text())
					if target == 'android':
						builtin = subprocess.run(['cmake', '-S', str(project), '-B', str(project / 'builtin'), '-DVERO_TEST_BUILTIN=ON'], capture_output=True, text=True)
						self.assertEqual(builtin.returncode, 0, builtin.stdout + builtin.stderr)
						self.assertIn('/slices/' + name + '/lib/libVeroScriptCpp.a', (project / 'builtin/selected.txt').read_text())
						for api, accepted in (('android-26', True), ('android-27', True), ('android-25', False)):
							legacy = subprocess.run(['cmake', '-S', str(project), '-B', str(project / ('ndk-' + api)),
								'-DVERO_TEST_NDK_API=' + api], capture_output=True, text=True)
							self.assertEqual(legacy.returncode == 0, accepted, legacy.stdout + legacy.stderr)
						static = subprocess.run(['cmake', '-S', str(project), '-B', str(project / 'static-stl'),
							'-DVERO_TEST_BUILTIN=ON', '-DVERO_TEST_STATIC_STL=ON'], capture_output=True, text=True)
						self.assertNotEqual(static.returncode, 0)
					if name == next(iter(SLICES[target])):
						for label, option in (('tools', '-DVERO_TEST_COMPONENT=Tools'), ('desktop', '-DVERO_TEST_HOST=Linux'),
							('conflict', '-DVERO_TEST_PREEXISTING=ON')):
							bad = subprocess.run(['cmake', '-S', str(project), '-B', str(project / ('bad-' + label)), option], capture_output=True, text=True)
							self.assertNotEqual(bad.returncode, 0, bad.stdout + bad.stderr)
			self.assertFalse(any(path.suffix in {'.a', '.so', '.zip', '.exe'} for path in root.rglob('*')))


if __name__ == '__main__':
	unittest.main()
