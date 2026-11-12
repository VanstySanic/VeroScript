#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build or collect Android/iOS SDKs and publish through the unified Release contract."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

from veroscript_paths import module_root, repository_root
from veroscript_products import MOBILE_PRODUCTS, MOBILE_NATIVE_BUSINESSES, SDK_MODULE_BUSINESSES, MODULE_BUSINESSES, target_businesses, mobile_coverage
from veroscript_business_release import collection_root, load_versions, publish_businesses
from veroscript_release import safe_output, scratch_directory
from veroscript_mobile_release import load_tool, stage_mobile_candidate, guard_input, host_interpreters, MOBILE_PYTHON_VERSIONS, source_native_version


def command(*arguments) -> None:
	subprocess.run([str(argument) for argument in arguments], check=True)


def validate_input_options(args, selected) -> None:
	"""Reject ignored input groups before inspecting files or invoking a producer."""
	if 'CSharp' not in selected and (args.csharp_package or args.csharp_native_root or args.csharp_symbols):
		raise ValueError('CSharp inputs require selecting the CSharp business')
	if args.csharp_symbols and not args.csharp_package:
		raise ValueError('--csharp-symbols requires --csharp-package')
	if 'Python' not in selected and (args.python_sdk_directory or args.python_target_sdk or args.host_python or args.ndk):
		raise ValueError('Python inputs require selecting the Python business')
	if (args.host_python or args.ndk) and not args.python_target_sdk:
		raise ValueError('--host-python/--ndk require --python-target-sdk; existing deployments need no build tools')
	if args.native_sdk and MOBILE_PRODUCTS[args.platform] not in selected and not ('Jit' in selected and not args.jit_aar):
		raise ValueError('--native-sdk requires its native SDK business or an Android Jit input lookup')
	native_inputs = [getattr(args, key, None) for key in ('native_cmake_sdk', 'native_build_directory', 'native_ndk', 'native_dependency_prefix', 'native_network_tls')]
	if any(native_inputs) and not set(selected).intersection(MOBILE_NATIVE_BUSINESSES):
		raise ValueError('Native CMake SDK inputs require selecting a native SDK business')
	if getattr(args, 'native_cmake_sdk', None) and any(native_inputs[1:]):
		raise ValueError('Choose existing --native-cmake-sdk or native SDK build inputs')
	if getattr(args, 'native_build_directory', None) and any(native_inputs[2:]):
		raise ValueError('Existing native builds use their recorded toolchain; omit native build tool/dependency overrides')
	if args.platform != 'android' and getattr(args, 'native_ndk', None):
		raise ValueError('--native-ndk belongs to Android')
	if getattr(args, 'native_network_tls', False) and 'Boost' not in selected:
		raise ValueError('--native-network-tls requires selecting Boost')
	symbol_inputs = getattr(args, 'module_symbol_inputs', None)
	symbol_directory = getattr(args, 'module_symbol_directory', None)
	symbol_tools = [getattr(args, 'module_symbol_' + name, None) for name in ('objcopy', 'objdump', 'strip')]
	if (symbol_inputs or symbol_directory) and not set(selected).intersection(MODULE_BUSINESSES):
		raise ValueError('Module symbols require selecting their mobile module businesses')
	if any(symbol_tools) and not symbol_inputs:
		raise ValueError('Module symbol tools require --module-symbol-inputs')
	if args.platform != 'android' and any(getattr(args, 'module_symbol_' + name, None) for name in ('objcopy', 'objdump')):
		raise ValueError('ELF module symbol tools belong to Android')


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('platform', choices=tuple(MOBILE_PRODUCTS))
	parser.add_argument('--business', action='append', help='Repeat to select businesses; default: platform SDK, CoreSDK, Cpp, Interop, Vm, CSharp, Python; modules are opt-in')
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--versions-file', type=Path)
	parser.add_argument('--version', help='Explicit distribution-version override for selected businesses')
	parser.add_argument('--out-root', type=Path, help='Release collection root; default: source-root/Release')
	parser.add_argument('--candidate-directory', type=Path, help='Write an audited candidate instead of publishing')
	parser.add_argument('--native-sdk', type=Path, help='Reuse a complete output of package-veroscript-mobile.py')
	parser.add_argument('--native-cmake-sdk', type=Path, help='Reuse raw <business>/ SDKs from package-veroscript-mobile-native.py')
	parser.add_argument('--native-build-directory', action='append', default=[], help='Native CMake slice=build-directory; repeat for the complete platform matrix')
	parser.add_argument('--native-dependency-prefix', action='append', default=[], help='Native source-build dependencies slice=prefix; complete matrix, e.g. target OpenSSL')
	parser.add_argument('--native-ndk', type=Path, help='NDK r28c for native CMake SDK source builds (separate from Python --ndk)')
	parser.add_argument('--native-network-tls', action='store_true', help='Enable native Boost TLS with target OpenSSL SDK prefixes')
	parser.add_argument('--csharp-package', type=Path, help='Reuse the complete mobile-enabled VeroScript.CSharp NuGet')
	parser.add_argument('--csharp-symbols', type=Path, help='Optional matching .snupkg for --csharp-package')
	parser.add_argument('--csharp-native-root', type=Path, help='Verified combined Android/iOS native root for dotnet pack')
	parser.add_argument('--python-sdk-directory', type=Path, action='append', default=[], help='Reuse one complete CPython minor deployment; repeat for 3.13 and 3.14')
	parser.add_argument('--python-target-sdk', type=Path, action='append', default=[], help='Target CPython SDK descriptors for all selected slices; repeat')
	parser.add_argument('--partial-python', action='store_true', help='Allow an explicitly partial CPython minor matrix; fails --require-complete')
	parser.add_argument('--with-asmjit', action='store_true', help='Also publish the optional Android backend as Modules/Jit')
	parser.add_argument('--with-native-modules', action='store_true', help='Also publish all optional native-data module SDKs for this platform')
	parser.add_argument('--jit-aar', type=Path, help='Reuse the optional Android backend AAR')
	symbols = parser.add_mutually_exclusive_group()
	symbols.add_argument('--module-symbol-inputs', type=Path, help='Exact source-owned original/final native module mappings for symbol export; no filesystem discovery')
	symbols.add_argument('--module-symbol-directory', type=Path, help='Reuse already-sealed matching module symbol archives')
	for name in ('objcopy', 'objdump', 'strip'):
		parser.add_argument('--module-symbol-' + name, type=Path, help='Explicit target ' + name + ' for --module-symbol-inputs')
	parser.add_argument('--gradle', default='gradle')
	parser.add_argument('--cmake', default='cmake')
	parser.add_argument('--dotnet', default='dotnet')
	parser.add_argument('--host-python', action='append', default=[], help='HOST CPython executable; repeat for each target minor (3.13 and 3.14)')
	parser.add_argument('--ndk', type=Path)
	parser.add_argument('--plan', action='store_true', help='Display layout and required inputs without building or writing')
	args = parser.parse_args()
	selected = args.business or list(target_businesses(args.platform))
	if args.with_native_modules:
		selected = [*selected, *[business for business in SDK_MODULE_BUSINESSES if business not in selected]]
	if args.with_asmjit or args.jit_aar:
		selected = [*selected, *([] if 'Jit' in selected else ['Jit'])]
	if not selected or len(selected) != len(set(selected)) or not set(selected).issubset(target_businesses(args.platform, optional=True)):
		parser.error('Select unique businesses supported by the requested mobile platform')
	if args.partial_python and 'Python' not in selected:
		parser.error('--partial-python requires the Python business')
	if args.csharp_package and args.csharp_native_root:
		parser.error('Choose --csharp-package or --csharp-native-root')
	if args.python_sdk_directory and args.python_target_sdk:
		parser.error('Choose existing Python deployments or target SDK descriptors')
	if args.candidate_directory and args.out_root:
		parser.error('--candidate-directory and --out-root are mutually exclusive')
	try:
		validate_input_options(args, selected)
		from veroscript_mobile_native import parse_slice_paths
		native_builds = parse_slice_paths(args.native_build_directory, args.platform)
		native_prefixes = parse_slice_paths(args.native_dependency_prefix, args.platform)
	except ValueError as error:
		parser.error(str(error))
	repo = args.repo_root.resolve()
	config = load_versions(repo, args.versions_file, args.version, selected)
	if args.plan:
		from veroscript_release_plan import release_plan
		plan = release_plan(repo, config, [args.platform], selected)
		plan['requiredInputs'] = {
			'native': 'Existing --native-sdk, or Gradle/Android SDK/NDK for Android; macOS/Xcode/CMake for iOS.',
			'nativeCMake': '--native-cmake-sdk, complete --native-build-directory matrices, or --native-ndk / macOS Xcode source builds. Curl and TLS-enabled Boost need target OpenSSL dependency prefixes.',
			'CSharp': '--csharp-package, or --csharp-native-root with .NET 10 Android/iOS workloads on macOS.',
			'Python': 'Repeated --python-sdk-directory or --python-target-sdk; complete 3.13/3.14 platform slice matrices.',
			'moduleSymbols': '--module-symbol-directory for sealed owned symbols, or --module-symbol-inputs for exact original/final native mappings and recorded/explicit target tools. Debugless original inputs emit no empty symbol archive.',
		}
		plan['mobileCoverage'] = mobile_coverage(args.platform)
		print(json.dumps(plan, ensure_ascii=False, indent=2))
		return
	if config['nativeVersion'] != source_native_version(repo):
		parser.error('Configured nativeVersion must match VeroScriptVersion.h; distribution versions are independent')
	# Resolve all required inputs before running any producer or creating a collection.
	if 'CSharp' in selected and not (args.csharp_package or args.csharp_native_root):
		parser.error('CSharp requires --csharp-package or --csharp-native-root')
	if 'Python' in selected and not (args.python_sdk_directory or args.python_target_sdk):
		parser.error('Python requires --python-sdk-directory or --python-target-sdk')
	if args.csharp_native_root and platform.system() != 'Darwin':
		parser.error('Building the complete mobile-enabled C# NuGet requires macOS and both .NET mobile workloads')
	native_selected = [business for business in selected if business in MOBILE_NATIVE_BUSINESSES]
	if args.platform == 'ios' and (not args.native_sdk and MOBILE_PRODUCTS['ios'] in selected or args.python_target_sdk
			or native_selected and not args.native_cmake_sdk) and platform.system() != 'Darwin':
		parser.error('iOS build/install/link steps require macOS and full Xcode; use completed SDKs for portable auditing')
	if native_selected and not args.native_cmake_sdk and not native_builds:
		if args.platform == 'android' and (args.native_ndk is None or not (args.native_ndk / 'build/cmake/android.toolchain.cmake').is_file()):
			parser.error('Native Android SDK builds require --native-ndk, or existing native SDK/build inputs')
		if ('Curl' in native_selected or args.native_network_tls) and not native_prefixes:
			parser.error('Native Curl / Boost TLS builds require --native-dependency-prefix target OpenSSL SDKs')
	from veroscript_mobile_native import guard_build_input, validate_ndk
	for path in [*native_builds.values(), *native_prefixes.values(), *([args.native_ndk] if args.native_ndk else [])]:
		guard_build_input(path)
	if args.native_ndk:
		validate_ndk(args.native_ndk)
	for path in [args.native_sdk, args.native_cmake_sdk, args.module_symbol_inputs, args.module_symbol_directory,
		args.module_symbol_objcopy, args.module_symbol_objdump, args.module_symbol_strip,
		args.csharp_package, args.csharp_symbols, args.csharp_native_root, args.jit_aar, *args.python_sdk_directory, *args.python_target_sdk]:
		if path is not None:
			guard_input(path.absolute())
			if not path.exists():
				parser.error(f'Missing mobile input: {path}')
	module_symbol_inputs = None
	if args.module_symbol_inputs:
		try:
			value = json.loads(args.module_symbol_inputs.read_text(encoding='utf-8-sig'))
			module_symbol_inputs = value.get('libraries') if isinstance(value, dict) else value
			if not isinstance(module_symbol_inputs, list) or not module_symbol_inputs or any(
				not isinstance(row, dict) or row.get('business') not in selected or row.get('business') not in MODULE_BUSINESSES
				for row in module_symbol_inputs):
				raise ValueError('Every original module debug input must belong to a selected module business')
		except (ValueError, OSError) as error:
			parser.error(str(error))
	if args.native_cmake_sdk:
		for business in native_selected:
			if not (args.native_cmake_sdk / business).is_dir():
				parser.error('Missing native CMake SDK input: ' + business)
	python_groups, python_hosts = {}, {}
	if args.python_target_sdk:
		sdk_tool = load_tool('Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py', repo)
		for descriptor in args.python_target_sdk:
			record = sdk_tool.inspect_sdk(descriptor)
			minor = '.'.join(record['pythonVersion'].split('.')[:2])
			python_groups.setdefault(minor, []).append((descriptor, record))
		if not set(python_groups).issubset(MOBILE_PYTHON_VERSIONS) or not args.partial_python and set(python_groups) != set(MOBILE_PYTHON_VERSIONS):
			parser.error('Python builds require both 3.13/3.14 target matrices unless --partial-python is explicit')
		expected = ({('arm64-v8a', 'device'), ('x86_64', 'device')} if args.platform == 'android' else
			{('arm64', 'device'), ('arm64', 'simulator'), ('x86_64', 'simulator')})
		for minor, records in python_groups.items():
			if (len(records) != len(expected) or {(record['architecture'], record['variant']) for _, record in records} != expected
					or any(record['platform'] != args.platform for _, record in records)
					or len({record['pythonVersion'] for _, record in records}) != 1):
				parser.error('Incomplete or inconsistent CPython ' + minor + ' platform slice matrix')
		python_hosts = host_interpreters(python_groups, args.host_python)
		if args.platform == 'android' and (args.ndk is None or not (args.ndk / 'build/cmake/android.toolchain.cmake').is_file()):
			parser.error('Android CPython builds require --ndk with a valid Android toolchain')
	candidate_output = safe_output(repo, repo / args.candidate_directory, component=True) if args.candidate_directory else None
	if candidate_output is not None and candidate_output.exists():
		parser.error('Choose a new candidate directory')
	if candidate_output is None:
		output = collection_root(repo, repo / (args.out_root or module_root(repo) / 'Release'))
	with tempfile.TemporaryDirectory(prefix='veroscript-mobile-release-', dir=scratch_directory(repo)) as temporary:
		work = Path(temporary)
		# Freeze the exact version configuration for child builders as well.
		versions_file = work / 'versions.json'
		versions_file.write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
		inputs = {}
		if native_selected:
			native_cmake = args.native_cmake_sdk
			if native_cmake is None:
				native_cmake = work / 'native-cmake'
				arguments = [sys.executable, module_root(repo) / 'Resources/Tools/Packaging/package-veroscript-mobile-native.py',
					args.platform, '--repo-root', module_root(repo), '--out-root', native_cmake, '--versions-file', versions_file, '--cmake', args.cmake]
				for business in native_selected:
					arguments += ['--business', business]
				for value in args.native_build_directory:
					arguments += ['--build-directory', value]
				for value in args.native_dependency_prefix:
					arguments += ['--dependency-prefix', value]
				if args.native_ndk:
					arguments += ['--ndk', args.native_ndk]
				if args.native_network_tls:
					arguments += ['--network-tls']
				command(*arguments)
			inputs.update({business: [native_cmake / business] for business in native_selected})
		native = args.native_sdk
		if (MOBILE_PRODUCTS[args.platform] in selected or 'Jit' in selected and not args.jit_aar) and native is None:
			native = work / 'native'
			arguments = [sys.executable, module_root(repo) / 'Resources/Tools/Packaging/package-veroscript-mobile.py',
				args.platform, '--repo-root', module_root(repo), '--out-root', native, '--versions-file', versions_file,
				'--gradle', args.gradle, '--cmake', args.cmake]
			if 'Jit' in selected:
				arguments.append('--with-asmjit')
			command(*arguments)
		if MOBILE_PRODUCTS[args.platform] in selected:
			inputs[MOBILE_PRODUCTS[args.platform]] = [native]
		if 'Jit' in selected:
			backends = [args.jit_aar] if args.jit_aar else list(native.glob('vs-asmjit-*.aar'))
			if len(backends) != 1:
				parser.error('Expected one optional Android AsmJit AAR')
			inputs['Jit'] = backends
		if 'CSharp' in selected:
			package = args.csharp_package
			symbols = args.csharp_symbols
			if package is None:
				package_root = work / 'nuget'
				command(args.dotnet, 'pack', module_root(repo) / 'Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj',
					'-c', 'Release', '-p:VeroScriptIncludeMobile=true', '-p:Version=' + config['businesses']['CSharp'],
					'-p:VeroScriptMobileNativeRoot=' + str(args.csharp_native_root.resolve()),
					'-p:VeroScriptMobilePython=' + sys.executable, '-p:BaseIntermediateOutputPath=' + str(work / 'obj') + '/',
					'-p:BaseOutputPath=' + str(work / 'bin') + '/', '-o', package_root)
				package = package_root / f'VeroScript.CSharp.{config["businesses"]["CSharp"]}.nupkg'
				built_symbols = package.with_suffix('.snupkg')
				symbols = built_symbols if built_symbols.is_file() else None
			inputs['CSharp'] = [package]
			if symbols is not None:
				inputs['CSharp'].append(symbols)
		if 'Python' in selected:
			deployments = args.python_sdk_directory
			if args.python_target_sdk:
				deployments = []
				for minor, descriptors in sorted(python_groups.items()):
					deployment = work / ('python-' + minor)
					arguments = [sys.executable, module_root(repo) / 'Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py',
						args.platform, '--out-root', deployment, '--host-python', python_hosts[minor], '--cmake', args.cmake]
					for descriptor, _ in descriptors:
						arguments += ['--sdk', descriptor]
					if args.ndk:
						arguments += ['--ndk', args.ndk]
					command(*arguments)
					deployments.append(deployment)
			inputs['Python'] = deployments
		candidate = work / 'candidate'
		stage_mobile_candidate(repo, candidate, config, args.platform, inputs, partial_python=args.partial_python,
			module_symbol_directory=args.module_symbol_directory, module_symbol_inputs=module_symbol_inputs,
			module_symbol_tools={name: str(getattr(args, 'module_symbol_' + name)) for name in ('objcopy', 'objdump', 'strip')
				if getattr(args, 'module_symbol_' + name) is not None})
		if candidate_output is not None:
			candidate_output.parent.mkdir(parents=True, exist_ok=True)
			shutil.copytree(candidate, candidate_output)
			print(f'Audited mobile candidate: {candidate_output}; target/device acceptance remains required')
		else:
			publish_businesses(repo, candidate, output, list(inputs))
			print(f'Mobile release published: {output} ({args.platform}); target/device acceptance remains required')


if __name__ == '__main__':
	main()
