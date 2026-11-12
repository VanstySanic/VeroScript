#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build/stage native CMake SDKs for mobile; the unified release entry owns ZIPs."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import subprocess
import tempfile

from veroscript_paths import repository_root, module_root
from veroscript_products import MOBILE_EMBEDDING_BUSINESSES, MOBILE_NATIVE_BUSINESSES, SDK_MODULE_BUSINESSES
from veroscript_business_release import load_versions
from veroscript_release import safe_output, scratch_directory
from veroscript_mobile_release import source_native_version
from veroscript_mobile_native import SLICES, sdk_closure, parse_slice_paths, check_build, stage_native_sdk, qualify_sdk, verify_native_cmake_sdk, guard_build_input, validate_ndk


def build_arguments(repo, build, target, slice_name, selected, *, ndk=None, dependency_prefix=None, network_tls=False):
	"""Pin every cache-sensitive profile option; never inherit desktop defaults."""
	needed = set().union(*(sdk_closure(business) for business in selected))
	settings = {
		'CMAKE_INSTALL_LIBDIR': 'lib', 'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_POSITION_INDEPENDENT_CODE': 'ON',
		'BUILD_TESTING': 'OFF', 'VS_ENABLE_IPO': 'OFF', 'VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY': 'ON',
		'CMAKE_INTERPROCEDURAL_OPTIMIZATION': 'OFF',
		'VS_BUILD_VEROSCRIPT_PYTHON_ONLY': 'OFF', 'VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY': 'OFF',
		'VEROSCRIPT_BUILD_MOBILE_ADAPTERS': 'OFF', 'VEROSCRIPT_BUILD_APPLICATION': 'OFF',
		'VEROSCRIPT_BUILD_PACKAGE_MANAGER': 'OFF', 'VEROSCRIPT_BUILD_PACKAGE_RUNTIME': 'OFF',
		'VEROSCRIPT_BUILD_BUILD_SYSTEM': 'OFF', 'VEROSCRIPT_ENABLE_ASMJIT': 'OFF',
		'VEROSCRIPT_BUILD_ASMJIT_BACKEND': 'OFF', 'VEROSCRIPT_BUILD_VM_BACKEND': 'ON',
		'VEROSCRIPT_INTEROP_STATIC': 'ON', 'VEROSCRIPT_INTEROP_DEFAULT_VM': 'ON',
		'VEROSCRIPT_JSON_BUILD_BINDINGS': 'OFF', 'VEROSCRIPT_NETWORK_BUILD_BINDINGS': 'OFF',
		'VEROSCRIPT_VISUAL_SCRIPT_BINDINGS': 'OFF', 'VEROSCRIPT_GUI_SCRIPTING': 'OFF',
		'VEROSCRIPT_SDL3_NATIVE_EXTENSION': 'OFF', 'VEROSCRIPT_BUILD_VISUAL': 'OFF',
		'VEROSCRIPT_STB_NATIVE_EXTENSION': 'OFF', 'VEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION': 'OFF',
		'VEROSCRIPT_RMLUI_NATIVE_EXTENSION': 'OFF',
		'VEROSCRIPT_SDL3_D3D11': 'OFF',
		'SDL_SHARED': 'OFF', 'SDL_STATIC': 'ON',
		'VEROSCRIPT_GUI_BUILD_RMLUI': 'ON' if 'RmlUi' in needed else 'OFF',
		'VEROSCRIPT_BOOST_HTTP': 'ON' if 'Http' in needed else 'OFF',
		'VEROSCRIPT_BOOST_WEBSOCKET': 'ON' if 'WebSocket' in needed else 'OFF',
		'VEROSCRIPT_CURL_USE_SYSTEM_LIBRARY': 'OFF',
		'VEROSCRIPT_NETWORK_TLS': 'ON' if network_tls else 'OFF',
	}
	for business in SDK_MODULE_BUSINESSES:
		option = {'FontBackend': 'FONT_BACKEND', 'RmlUi': None}.get(business, business.upper())
		if option:
			settings['VEROSCRIPT_BUILD_' + option] = 'ON' if business in needed else 'OFF'
	arch, variant = SLICES[target][slice_name]
	if target == 'android':
		if ndk is None:
			raise ValueError('Android source builds require the pinned --ndk')
		settings.update(CMAKE_TOOLCHAIN_FILE=str(ndk / 'build/cmake/android.toolchain.cmake'), CMAKE_ANDROID_NDK=str(ndk),
			CMAKE_SYSTEM_NAME='Android',
			ANDROID_ABI=arch, ANDROID_PLATFORM='android-26', ANDROID_STL='c++_shared', CMAKE_BUILD_TYPE='Release',
			CMAKE_SHARED_LINKER_FLAGS='-Wl,-z,max-page-size=16384')
		generator = 'Ninja'
	else:
		settings.update(CMAKE_SYSTEM_NAME='iOS', CMAKE_OSX_SYSROOT='iphonesimulator' if variant == 'simulator' else 'iphoneos',
			CMAKE_OSX_ARCHITECTURES='x86_64' if arch == 'x64' else arch, CMAKE_OSX_DEPLOYMENT_TARGET='15.0',
			CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED='NO', CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE='NO')
		generator = 'Xcode'
	if dependency_prefix:
		settings.update(CMAKE_PREFIX_PATH=str(dependency_prefix), CMAKE_FIND_ROOT_PATH=str(dependency_prefix),
			CMAKE_FIND_ROOT_PATH_MODE_PACKAGE='ONLY')
	return ['-S', str(module_root(repo)), '-B', str(build), '-G', generator,
		*['-D' + key + '=' + value for key, value in settings.items()]]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('platform', choices=tuple(SLICES))
	parser.add_argument('--business', choices=MOBILE_NATIVE_BUSINESSES, action='append')
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--out-root', type=Path, required=True, help='New directory containing one raw SDK directory per business')
	parser.add_argument('--versions-file', type=Path)
	parser.add_argument('--build-directory', action='append', default=[], help='Reuse slice=build-directory; provide the complete platform matrix')
	parser.add_argument('--dependency-prefix', action='append', default=[], help='Cross-built dependency SDK slice=prefix; complete matrix (e.g. OpenSSL)')
	parser.add_argument('--ndk', type=Path, help='Android NDK 28.2.13676358 for source builds')
	parser.add_argument('--network-tls', action='store_true', help='Enable Boost TLS; requires target OpenSSL dependency prefixes')
	parser.add_argument('--cmake', default='cmake')
	parser.add_argument('--plan', action='store_true')
	args = parser.parse_args()
	selected = args.business or list(MOBILE_EMBEDDING_BUSINESSES)
	if len(selected) != len(set(selected)):
		parser.error('Select unique mobile native SDK businesses')
	repo = module_root(args.repo_root.resolve())
	config = load_versions(repo, args.versions_file)
	try:
		builds = parse_slice_paths(args.build_directory, args.platform)
		prefixes = parse_slice_paths(args.dependency_prefix, args.platform)
	except ValueError as error:
		parser.error(str(error))
	if builds and (prefixes or args.ndk or args.network_tls):
		parser.error('Existing build directories use their recorded toolchain/dependencies; omit --ndk/--dependency-prefix')
	if args.network_tls and 'Boost' not in selected:
		parser.error('--network-tls requires selecting Boost')
	if args.ndk and args.platform != 'android':
		parser.error('--ndk belongs to Android source builds')
	if args.plan:
		print(json.dumps({'previewOnly': True, 'platform': args.platform, 'businesses': selected,
			'profile': 'C++ static embedding / module native-data SDK', 'slices': SLICES[args.platform],
			'inputs': 'Complete recorded build matrix, or Android NDK r28c / macOS full Xcode. Curl / Boost TLS source builds require target OpenSSL SDK prefixes.',
			'outputs': {business: str(args.out_root / business) for business in selected}}, indent=2))
		return
	if config['nativeVersion'] != source_native_version(repo):
		parser.error('Configured nativeVersion must match the source header')
	if args.platform == 'ios' and platform.system() != 'Darwin':
		parser.error('Staging or qualifying iOS build directories requires macOS and full Xcode; reuse completed SDKs for portable auditing')
	if not builds:
		if args.platform == 'android' and (args.ndk is None or not (args.ndk / 'build/cmake/android.toolchain.cmake').is_file()):
			parser.error('Android source builds require --ndk with the pinned NDK toolchain')
		if ('Curl' in selected or args.network_tls) and not prefixes:
			parser.error('Curl / Boost TLS source builds require complete --dependency-prefix target OpenSSL SDKs')
	for path in [*builds.values(), *prefixes.values(), *([args.ndk] if args.ndk else [])]:
		guard_build_input(path)
	if args.ndk:
		validate_ndk(args.ndk)
	for name, build in builds.items():
		check_build(repo, build, args.platform, name)
	output = safe_output(repo, repo / args.out_root, component=True)
	if output.exists():
		parser.error('Choose a new raw mobile SDK output directory')
	if any(output.is_relative_to(path.resolve()) or path.resolve().is_relative_to(output)
			for path in [*builds.values(), *prefixes.values()]):
		parser.error('Mobile SDK input and output directories must not overlap')
	with tempfile.TemporaryDirectory(prefix='veroscript-mobile-native-', dir=scratch_directory(repo)) as temporary:
		work = Path(temporary)
		if not builds:
			builds = {name: work / name for name in SLICES[args.platform]}
			for name, build in builds.items():
				subprocess.run([args.cmake, *build_arguments(repo, build, args.platform, name, selected,
					ndk=args.ndk.resolve() if args.ndk else None, dependency_prefix=prefixes.get(name), network_tls=args.network_tls)], check=True)
				# Build the selected public targets; their CMake dependency graph owns
				# bundled providers. No tests, CLI tools or mobile adapters are built.
				targets = set().union(*(sdk_closure(business) for business in selected))
				targets = {'VeroScript' if b == 'CoreSDK' else 'VeroScriptBytecodeVm' if b == 'Vm' else 'VeroScript' + b for b in targets}
				subprocess.run([args.cmake, '--build', str(build), '--config', 'Release', '--target', *sorted(targets)], check=True)
		staged = work / 'SDKs'
		for business in selected:
			stage_native_sdk(repo, staged / business, business, args.platform, builds, config['nativeVersion'])
			qualification = qualify_sdk(repo, staged / business, business, args.platform, builds, work / 'consumers')
			metadata_path = staged / business / 'mobile-native-sdk.json'
			metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
			metadata['consumerValidation'] = qualification
			metadata_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
			verify_native_cmake_sdk(staged / business, business, args.platform, config['nativeVersion'], repo)
		output.parent.mkdir(parents=True, exist_ok=True)
		staged.rename(output)
	print('Audited raw mobile CMake SDKs: ' + str(output) + '; device/application acceptance remains required')


if __name__ == '__main__':
	main()
