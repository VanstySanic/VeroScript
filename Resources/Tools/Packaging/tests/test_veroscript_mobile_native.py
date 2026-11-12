# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise mobile SDK staging with synthetic objects and mocked installers.

No source product is compiled, no real toolchain is invoked, and no Release
collection is published. The tests cover the raw producer before ZIP creation.
"""
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))
from veroscript_mobile_native import (SLICES, CAPABILITY_OPTIONS, MODULE_NOTICES, sdk_capabilities,
	stage_native_sdk, verify_native_cmake_sdk, version_config, required_service_libraries, check_build, android_build_settings,
	guard_build_input, validate_ndk)
from veroscript_mobile_release import load_tool
from veroscript_products import SDK_MODULE_BUSINESSES, MODULE_SOURCE_DIRECTORIES
from veroscript_mobile_release import stage_mobile_candidate
from veroscript_business_release import load_versions, publish_businesses, verify_collection, business_root, read_business
from test_veroscript_mobile import ar, macho, elf
from test_veroscript_mobile_native_static import relocatable


class NativeMobileStageTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix='vero-native-stage-', dir=REPO / '.tmp')
		self.addCleanup(self.temporary.cleanup)
		self.root = Path(self.temporary.name)
		self.ndk = self.root / 'fake-ndk'
		self.ndk.mkdir()
		(self.ndk / 'build/cmake').mkdir(parents=True)
		(self.ndk / 'build/cmake/android.toolchain.cmake').write_text('# Synthetic toolchain\n', encoding='utf-8')
		(self.ndk / 'source.properties').write_text('Pkg.Revision = 28.2.13676358\n', encoding='utf-8')
		for name in ('NOTICE', 'NOTICE.toolchain'):
			(self.ndk / name).write_text('Synthetic toolchain notice.\n', encoding='utf-8')
		for abi, triple in (('arm64-v8a', 'aarch64-linux-android'), ('x86_64', 'x86_64-linux-android')):
			path = self.ndk / 'toolchains/llvm/prebuilt/fixture/sysroot/usr/lib' / triple / 'libc++_shared.so'
			path.parent.mkdir(parents=True)
			path.write_bytes(elf(machine=183 if abi == 'arm64-v8a' else 62))

	def builds(self, target):
		builds = {}
		for name, (arch, variant) in SLICES[target].items():
			build = self.root / ('build-' + name)
			build.mkdir(exist_ok=True)
			settings = dict(CMAKE_HOME_DIRECTORY=str(REPO), CMAKE_BUILD_TYPE='Release', CMAKE_INSTALL_LIBDIR='lib',
				VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY='ON', VEROSCRIPT_BUILD_VM_BACKEND='ON', VEROSCRIPT_INTEROP_STATIC='ON',
				CMAKE_COMMAND='mock-cmake', VEROSCRIPT_CURL_BUNDLED='ON', SDL_SHARED='OFF', SDL_STATIC='ON',
				VEROSCRIPT_SDL3_GPU='ON', VEROSCRIPT_SDL3_AUDIO='ON', VEROSCRIPT_BOOST_HAS_HTTP_SERVER='ON',
				VEROSCRIPT_BOOST_HAS_WEBSOCKET='ON')
			curl = build / 'curl'
			curl.mkdir(exist_ok=True)
			settings['CURL_BINARY_DIR'] = str(curl)
			if target == 'android':
				settings.update(CMAKE_SYSTEM_NAME='Android', ANDROID_ABI=arch, ANDROID_PLATFORM='android-26',
					ANDROID_STL='c++_shared', CMAKE_ANDROID_NDK=str(self.ndk))
			else:
				settings.update(CMAKE_SYSTEM_NAME='iOS', CMAKE_OSX_DEPLOYMENT_TARGET='15.0',
					CMAKE_OSX_ARCHITECTURES='x86_64' if arch == 'x64' else arch,
					CMAKE_OSX_SYSROOT='iphonesimulator' if variant == 'simulator' else 'iphoneos')
			(build / 'CMakeCache.txt').write_text(''.join(f'{key}:STRING={value}\n' for key, value in settings.items()), encoding='utf-8')
			builds[name] = build
		return builds

	def installer(self, target, *, wrong_sdl_kind=False):
		def run(arguments, **kwargs):
			self.assertEqual(arguments[1], '--install')
			prefix = Path(arguments[arguments.index('--prefix') + 1])
			name = prefix.name
			arch, variant = SLICES[target][name]
			binary = ar(relocatable(183 if arch == 'arm64-v8a' else 62)) if target == 'android' else ar(macho(
				cpu=0x1000007 if arch == 'x64' else 0x100000C, platform=7 if variant == 'simulator' else 2))
			def write(route, data):
				path = prefix / route
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_bytes(data)
			def config(package, version='0.1.1'):
				write(f'lib/cmake/{package}/{package}Config.cmake', b'# Synthetic installed package\n')
				write(f'lib/cmake/{package}/{package}ConfigVersion.cmake', version_config(version).encode())
			if '--component' not in arguments:
				write('lib/libcurl.a', binary)
				config('CURL', '8.13.0')
				return
			component = arguments[arguments.index('--component') + 1].removeprefix('VeroScript')
			component = 'Gui' if component == 'GuiNative' else component
			shared = component == 'Sdl3' and target == 'android' and not wrong_sdl_kind
			write('lib/libVeroScript' + component + ('.so' if shared else '.a'), elf(
				machine=183 if arch == 'arm64-v8a' else 62) if shared else binary)
			if component == 'Boost':
				write('lib/veroscript/modules/libVeroScriptBoostRuntime.so' if target == 'android' else 'lib/libVeroScriptBoostRuntime.a',
					elf(machine=183 if arch == 'arm64-v8a' else 62) if target == 'android' else binary)
			if component == 'Sdl3':
				write('lib/libSDL3.a', binary)
				config('SDL3', '3.4.18')
			if component == 'RmlUi':
				write('lib/librmlui.a', binary)
				config('RmlUi', '6.1.0')
			if component == 'FontBackend':
				for filename in ('libfreetype.a', 'libVeroScriptFontBackendHarfBuzz.a', 'libVeroScriptFontBackendSheenBidi.a'):
					write('lib/' + filename, binary)
			for path in (REPO / MODULE_SOURCE_DIRECTORIES[component] / 'Public').rglob('*.h'):
				write('include/' + component + '/' + path.name, path.read_bytes())
			for notice in MODULE_NOTICES.get(component, ()):
				write(notice, b'Synthetic redistribution notice\n')
			config('VeroScript' + component)
		return run

	def seal_qualification(self, sdk, target):
		path = sdk / 'mobile-native-sdk.json'
		metadata = json.loads(path.read_text(encoding='utf-8'))
		metadata['consumerValidation'] = {'profile': 'whole-archive-cross-link', 'executed': False,
			'slices': dict.fromkeys(SLICES[target], 'a' * 64),
			'templateSha256': hashlib.sha256((TOOLS / 'MobileNativeConsumer/CMakeLists.txt').read_bytes()).hexdigest()}
		path.write_text(json.dumps(metadata), encoding='utf-8')

	def test_every_optional_sdk_stages_all_platform_slices_and_rechecks(self):
		for target in SLICES:
			builds = self.builds(target)
			for business in SDK_MODULE_BUSINESSES:
				with self.subTest(target=target, business=business):
					sdk = self.root / (target + '-' + business)
					with patch('veroscript_mobile_native.subprocess.run', side_effect=self.installer(target)):
						stage_native_sdk(REPO, sdk, business, target, builds, '0.1.1')
					self.seal_qualification(sdk, target)
					verify_native_cmake_sdk(sdk, business, target, '0.1.1', REPO)

	def test_android_sdl_bridge_is_shared_and_wrong_static_kind_is_rejected(self):
		self.assertEqual(required_service_libraries('Sdl3', 'android'), {'lib/libVeroScriptSdl3.so'})
		self.assertEqual(required_service_libraries('Sdl3', 'ios'), {'lib/libVeroScriptSdl3.a'})
		with patch('veroscript_mobile_native.subprocess.run', side_effect=self.installer('android', wrong_sdl_kind=True)):
			with self.assertRaisesRegex(ValueError, 'service library: Sdl3'):
				stage_native_sdk(REPO, self.root / 'bad-sdl', 'Sdl3', 'android', self.builds('android'), '0.1.1')
	def test_curl_private_dependencies_normalize_before_inventory_and_reject_resealed_paths(self):
		for target in SLICES:
			with self.subTest(target=target):
				install = self.installer(target)
				def legacy_install(arguments, **kwargs):
					install(arguments, **kwargs)
					if '--component' not in arguments:
						prefix = Path(arguments[arguments.index('--prefix') + 1])
						(prefix / 'lib/cmake/CURL/CURLConfig.cmake').write_text('set(CURL_LIBRARIES CURL::libcurl)\nset(CURL_LIBRARIES_PRIVATE "/former-publisher/libssl.a")\n')
				sdk = self.root / ('relocatable-curl-' + target)
				with patch('veroscript_mobile_native.subprocess.run', side_effect=legacy_install):
					stage_native_sdk(REPO, sdk, 'Curl', target, self.builds(target), '0.1.1')
				self.seal_qualification(sdk, target)
				verify_native_cmake_sdk(sdk, 'Curl', target, '0.1.1', REPO)
				metadata_path = sdk / 'mobile-native-sdk.json'
				metadata = json.loads(metadata_path.read_text())
				relative = 'slices/' + next(iter(SLICES[target])) + '/lib/cmake/CURL/CURLConfig.cmake'
				config = sdk / relative
				self.assertIn('get_target_property(CURL_LIBRARIES_PRIVATE', config.read_text())
				config.write_text('set(CURL_LIBRARIES_PRIVATE "/another-machine/libcrypto.a")\n')
				metadata['files'][relative] = hashlib.sha256(config.read_bytes()).hexdigest()
				metadata_path.write_text(json.dumps(metadata))
				with self.assertRaisesRegex(ValueError, 'Non-relocatable'):
					verify_native_cmake_sdk(sdk, 'Curl', target, '0.1.1', REPO)

	def test_android_nested_runtime_plugins_are_stripped_before_inventory(self):
		from veroscript_mobile import strip_mobile_debug
		with patch('veroscript_mobile_native.subprocess.run', side_effect=self.installer('android')), \
				patch('veroscript_mobile_native.strip_mobile_debug', wraps=strip_mobile_debug) as strip:
			stage_native_sdk(REPO, self.root / 'nested-boost', 'Boost', 'android', self.builds('android'), '0.1.1')
		plugins = [call.args[0] for call in strip.call_args_list if call.args[0].name == 'libVeroScriptBoostRuntime.so']
		self.assertEqual(len(plugins), len(SLICES['android']))
		self.assertTrue(all(path.parent.as_posix().endswith('/lib/veroscript/modules') for path in plugins))

	def test_capabilities_describe_only_the_bundled_providers(self):
		settings = dict.fromkeys(CAPABILITY_OPTIONS, 'ON')
		for bundled in (['CoreSDK'], ['Cpp'], ['Vm'], ['Json'], ['Gui', 'Image', 'Font']):
			self.assertFalse(any(sdk_capabilities(bundled, settings).values()))
		self.assertTrue(sdk_capabilities(['Boost'], settings)['VEROSCRIPT_NETWORK_TLS'])
		self.assertFalse(sdk_capabilities(['Boost'], settings)['VEROSCRIPT_SDL3_GPU'])
		self.assertFalse(sdk_capabilities(['Boost'], {**settings, 'VEROSCRIPT_BOOST_HAS_WEBSOCKET': 'OFF'})['VEROSCRIPT_WEBSOCKET_COMPRESSION'])

	def test_cmake_android_cache_keys_preserve_abi_api_and_shared_stl(self):
		builds = self.builds('android')
		for name, build in builds.items():
			cache = build / 'CMakeCache.txt'
			text = cache.read_text(encoding='utf-8').replace('ANDROID_ABI:', 'CMAKE_ANDROID_ARCH_ABI:')
			text = text.replace('ANDROID_PLATFORM:STRING=android-26', 'CMAKE_SYSTEM_VERSION:STRING=26')
			text = text.replace('ANDROID_STL:', 'CMAKE_ANDROID_STL_TYPE:')
			cache.write_text(text, encoding='utf-8')
			settings = check_build(REPO, build, 'android', name)
			self.assertEqual(android_build_settings(settings)['abi'], SLICES['android'][name][0])
			self.assertEqual(android_build_settings(settings)['stl'], 'c++_shared')
			cache.write_text(text.replace('c++_shared', 'c++_static'), encoding='utf-8')
			with self.assertRaisesRegex(ValueError, 'shared libc'):
				check_build(REPO, build, 'android', name)
			cache.write_text('\n'.join(line for line in text.splitlines() if not line.startswith('CMAKE_ANDROID_STL_TYPE:')), encoding='utf-8')
			with self.assertRaisesRegex(ValueError, 'shared libc'):
				check_build(REPO, build, 'android', name)
		with self.assertRaisesRegex(ValueError, 'Conflicting Android'):
			android_build_settings({'ANDROID_STL': 'c++_shared', 'CMAKE_ANDROID_STL_TYPE': 'c++_static'})

	def test_multi_config_builds_use_the_release_configuration_not_build_type(self):
		for name, build in self.builds('ios').items():
			cache = build / 'CMakeCache.txt'
			original = cache.read_text(encoding='utf-8')
			text = original.replace('CMAKE_BUILD_TYPE:STRING=Release', 'CMAKE_BUILD_TYPE:STRING=Debug')
			text += 'CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;RelWithDebInfo\n'
			cache.write_text(text, encoding='utf-8')
			check_build(REPO, build, 'ios', name)
			cache.write_text(text.replace('Debug;Release;RelWithDebInfo', 'Debug'), encoding='utf-8')
			with self.assertRaisesRegex(ValueError, 'Release embedded build'):
				check_build(REPO, build, 'ios', name)
			cache.write_text(original.replace('CMAKE_BUILD_TYPE:STRING=Release\n', ''), encoding='utf-8')
			with self.assertRaisesRegex(ValueError, 'Release embedded build'):
				check_build(REPO, build, 'ios', name)

	def test_toolchain_inputs_are_not_walked_as_distributable_payloads(self):
		with patch('veroscript_business_release.guard_tree', side_effect=AssertionError('Toolchains are not release payloads')):
			self.assertEqual(guard_build_input(self.ndk), self.ndk.resolve())
			self.assertEqual(validate_ndk(self.ndk), self.ndk.resolve())
		(self.ndk / 'source.properties').write_text('Pkg.Revision = 29.0.0\n', encoding='utf-8')
		with self.assertRaisesRegex(ValueError, 'r28c'):
			validate_ndk(self.ndk)

	def test_optional_modules_publish_and_update_with_the_mobile_business_contract(self):
		config = load_versions(REPO)
		collection = self.root / 'collection'
		for target in SLICES:
			builds = self.builds(target)
			inputs = {}
			for business in ('Json', 'RmlUi', 'Sdl3'):
				sdk = self.root / ('publish-' + target + '-' + business)
				with patch('veroscript_mobile_native.subprocess.run', side_effect=self.installer(target)):
					stage_native_sdk(REPO, sdk, business, target, builds, '0.1.1')
				self.seal_qualification(sdk, target)
				inputs[business] = [sdk]
			candidate = self.root / ('candidate-' + target)
			stage_mobile_candidate(REPO, candidate, config, target, inputs)
			publish_businesses(REPO, candidate, collection)
			index = verify_collection(collection, target, REPO)
			self.assertEqual(set(index['businesses']), set(inputs))
			for business in inputs:
				manifest = read_business(business_root(collection, business, target) / '0.1.1', target)
				self.assertEqual(manifest['mobileNativeSdkPolicyVersion'], 1)
				self.assertEqual(manifest['dependencies'], {})
				self.assertNotIn('modulePackagePolicyVersion', manifest)
			changed = copy.deepcopy(config)
			changed['businesses']['RmlUi'] = '0.1.2'
			candidate = self.root / ('update-' + target)
			stage_mobile_candidate(REPO, candidate, changed, target, {'RmlUi': inputs['RmlUi']})
			publish_businesses(REPO, candidate, collection)
			index = verify_collection(collection, target, REPO)
			self.assertEqual(index['businesses']['RmlUi']['version'], '0.1.2')
			self.assertEqual(index['businesses']['Json']['version'], '0.1.1')
			self.assertFalse((business_root(collection, 'RmlUi', target) / '0.1.1').exists())

	def test_ios_build_reuse_still_needs_xcode_before_inspecting_inputs(self):
		for entry_name, arguments in (
			('package-veroscript-mobile-native.py', ['--out-root', '.tmp/not-created', '--build-directory']),
			('package-veroscript-mobile-release.py', ['--native-build-directory'])):
			entry = load_tool('Resources/Tools/Packaging/' + entry_name, REPO)
			argv = [entry_name, 'ios', '--business', 'Cpp']
			if '--out-root' in arguments:
				argv += arguments[:2]
			option = arguments[-1]
			for name in SLICES['ios']:
				argv += [option, name + '=not-inspected']
			with self.subTest(entry=entry_name), patch.object(sys, 'argv', argv), patch.object(entry.platform, 'system', return_value='Windows'), \
				patch('veroscript_mobile_native.guard_build_input', side_effect=AssertionError('Input must not be inspected')), contextlib.redirect_stderr(io.StringIO()):
				with self.assertRaises(SystemExit) as error:
					entry.main()
				self.assertEqual(error.exception.code, 2)


if __name__ == '__main__':
	unittest.main()
