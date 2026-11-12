# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Synthetic mobile archives for publication tests, never device acceptance."""
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import struct
import zipfile

from test_veroscript_mobile import aar_entries, ar, macho, elf, package_mobile
from veroscript_mobile_release import csharp_tool, load_tool, APPLICATION_NOTICES

REPO = Path(__file__).resolve().parents[4]


def native_cmake_fixture(root: Path, business: str, target: str) -> Path:
	"""Synthetic native objects for future archive/transaction tests, not a build."""
	from test_veroscript_mobile_native_static import relocatable
	from veroscript_mobile_native import (SLICES, BASE_LIBRARIES, CAPABILITY_OPTIONS, install_base,
		sdk_closure, source_header_map, native_libraries, selector, embedding_resources)
	from veroscript_mobile_release import payload_files
	stage = root / ('native-cmake-' + target) / business
	records, headers = {}, {}
	contract = package_mobile.aot_source_contract(REPO)
	for name, (arch, variant) in SLICES[target].items():
		build = root / ('fixture-build-' + business + '-' + name)
		(build / 'lib/Release').mkdir(parents=True)
		(build / 'VeroScript/Generated').mkdir(parents=True)
		(build / 'VeroScript/Generated/VeroScriptJitBuild.h').write_text(''.join(f'#define {key} "{value}"\n' for key, value in contract.items()), encoding='utf-8')
		data = ar(relocatable(183 if arch == 'arm64-v8a' else 62)) if target == 'android' else ar(macho(
			cpu=0x1000007 if arch == 'x64' else 0x100000C, platform=7 if variant == 'simulator' else 2))
		for library in BASE_LIBRARIES[business]:
			(build / 'lib/Release' / ('lib' + library + '.a')).write_bytes(data)
		prefix = stage / 'slices' / name
		install_base(REPO, build, prefix, business, '0.1.1')
		if target == 'android':
			(prefix / 'lib/libc++_shared.so').write_bytes(elf(machine=183 if arch == 'arm64-v8a' else 62))
			for notice in ('NOTICE', 'NOTICE.toolchain'):
				(prefix / notice).write_text('Synthetic NDK notice, fixture only.', encoding='utf-8')
		records[name] = native_libraries(prefix, target, name)
		headers[name] = source_header_map(REPO, prefix, sdk_closure(business))
	first = stage / 'slices' / next(iter(SLICES[target]))
	packages = sorted(path.parent.name for path in (first / 'lib/cmake').glob('*/*Config.cmake'))
	for package in packages:
		directory = stage / 'lib/cmake' / package
		directory.mkdir(parents=True)
		(directory / (package + 'Config.cmake')).write_text(selector(package, target), encoding='utf-8')
		shutil.copy2(first / 'lib/cmake' / package / (package + 'ConfigVersion.cmake'), directory)
	for route, data in embedding_resources(REPO, business).items():
		path = stage / 'assets' / route
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)
	shutil.copy2(REPO / 'LICENSE', stage / 'LICENSE')
	(stage / 'mobile-native-sdk.json').write_text(json.dumps({'schemaVersion': 1, 'product': 'VeroScript Mobile Native SDK',
		'business': business, 'target': target, 'nativeVersion': '0.1.1', 'profile': 'embedding-static-sdk',
		'bundledBusinesses': list(sdk_closure(business)), 'packages': packages, 'slices': records, 'sourceHeaders': headers,
		'capabilities': dict.fromkeys(CAPABILITY_OPTIONS, False), 'externalDependencies': [], 'coreContract': contract,
		'bundledNativeVersions': dict.fromkeys(sdk_closure(business), '0.1.1'),
		'consumerValidation': {'profile': 'whole-archive-cross-link', 'executed': False, 'slices': dict.fromkeys(SLICES[target], 'a' * 64),
			'templateSha256': hashlib.sha256((REPO / 'Resources/Tools/Packaging/MobileNativeConsumer/CMakeLists.txt').read_bytes()).hexdigest()},
		'minimumApi': 26 if target == 'android' else None, 'minimumOS': '15.0' if target == 'ios' else None,
		'toolchain': 'android-ndk-28.2.13676358' if target == 'android' else 'xcode-ios', 'targetAcceptance': 'required',
		'files': payload_files(stage)}), encoding='utf-8')
	return stage


def write_zip(path, entries):
	path.parent.mkdir(parents=True, exist_ok=True)
	with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
		for name, data in entries.items():
			archive.writestr(name, data)


def managed_pe():
	data = bytearray(1024)
	data[:2] = b'MZ'
	struct.pack_into('<I', data, 60, 64)
	data[64:68] = b'PE\0\0'
	struct.pack_into('<H', data, 70, 1)
	struct.pack_into('<H', data, 84, 224)
	struct.pack_into('<H', data, 88, 0x10b)
	struct.pack_into('<II', data, 88 + 96 + 14 * 8, 0x2000, 72)
	struct.pack_into('<IIII', data, 88 + 224 + 8, 256, 0x2000, 256, 512)
	struct.pack_into('<IHHIII', data, 512, 72, 2, 5, 0x2050, 4, 1)
	data[592:596] = b'BSJB'
	return bytes(data)


def create_inputs(root: Path):
	helper = csharp_tool(REPO)
	android = root / 'android'
	android.mkdir(parents=True)
	aar = android / 'VeroScriptAndroid-0.1.1.aar'
	entries = aar_entries()
	for name, source in {'LICENSE': 'LICENSE', **APPLICATION_NOTICES}.items():
		entries['assets/veroscript/licenses/' + name] = (REPO / source).read_bytes()
	entries['prefab/modules/VeroScriptInterop/include/VeroScriptInterop.h'] = (REPO / 'VeroScriptInterop/Public/VeroScriptInterop.h').read_bytes()
	for name, data in helper.resources().items():
		entries['assets/veroscript/' + name] = data
	write_zip(aar, entries)
	ios = root / 'ios'
	framework = ios / 'VeroScriptIOS.xcframework'
	libraries = []
	for identifier, architectures, simulator in (('ios-arm64', ['arm64'], False), ('ios-arm64_x86_64-simulator', ['arm64', 'x86_64'], True)):
		directory = framework / identifier
		(directory / 'Headers').mkdir(parents=True)
		(directory / 'Headers/VeroScriptInterop.h').write_bytes((REPO / 'VeroScriptInterop/Public/VeroScriptInterop.h').read_bytes())
		for name in ('VeroScriptIOS.h', 'module.modulemap'):
			(directory / 'Headers' / name).write_bytes((REPO / 'Extensions/Platform/Mobile/VeroScriptIOS/Public' / name).read_bytes())
		arm = ar(macho(platform=7 if simulator else 2))
		if simulator:
			intel = ar(macho(cpu=0x1000007, platform=7))
			arm = (struct.pack('>II', 0xCAFEBABE, 2) + struct.pack('>IIIII', 0x100000C, 0, 48, len(arm), 0)
				+ struct.pack('>IIIII', 0x1000007, 0, 48 + len(arm), len(intel), 0) + arm + intel)
		(directory / 'mobile.a').write_bytes(arm)
		library = {'LibraryIdentifier': identifier, 'SupportedArchitectures': architectures, 'SupportedPlatform': 'ios',
			'LibraryPath': 'mobile.a', 'HeadersPath': 'Headers'}
		if simulator:
			library['SupportedPlatformVariant'] = 'simulator'
		libraries.append(library)
	(framework / 'Info.plist').write_bytes(plistlib.dumps({'AvailableLibraries': libraries}))
	for name, data in helper.resources().items():
		file = ios / 'assets' / name
		file.parent.mkdir(parents=True, exist_ok=True)
		file.write_bytes(data)
	contract = package_mobile.aot_source_contract(REPO)
	header = root / 'VeroScriptJitBuild.h'
	header.write_text(''.join(f'#define {name} "{value}"\n' for name, value in contract.items()), encoding='utf-8')
	for platform, stage in (('android', android), ('ios', ios)):
		aot = package_mobile.stage_aot_headers(REPO, [header], stage)
		(stage / 'LICENSE').write_bytes((REPO / 'LICENSE').read_bytes())
		(stage / 'mobile-sdk.json').write_text(json.dumps({'product': 'VeroScript Mobile SDK', 'version': '0.1.1',
			'platform': platform, 'nativeJit': False, 'defaultExecution': 'vm', 'aotHeaders': aot}), encoding='utf-8')
	native = root / 'csharp-native'
	helper.prepare(aar, ios, native)
	package = root / 'VeroScript.CSharp.0.1.1.nupkg'
	managed = {f'lib/{tfm}/VeroScriptCSharp.dll': managed_pe() for tfm in ('net8.0', 'net9.0', 'net10.0-android36.0', 'net10.0-ios26.0')}
	managed['VeroScript.CSharp.nuspec'] = b'<package><metadata><id>VeroScript.CSharp</id><version>0.1.1</version></metadata></package>'
	managed['LICENSE'] = (REPO / 'LICENSE').read_bytes()
	managed['buildTransitive/VeroScript.CSharp.targets'] = (REPO / 'Extensions/Language/CSharp/VeroScriptCSharp/buildTransitive/VeroScript.CSharp.targets').read_bytes()
	managed.update({'native/' + file.relative_to(native).as_posix(): file.read_bytes() for file in native.rglob('*') if file.is_file()})
	write_zip(package, managed)
	python = load_tool('Extensions/Language/Python/VeroScriptPython/tests/test_mobile.py', REPO)
	deployments = {}
	for platform in ('android', 'ios'):
		deployments[platform] = []
		for minor in ('3.13', '3.14'):
			stage = root / ('python-' + platform + '-' + minor)
			stage.mkdir()
			records = []
			for arch, variant in ((('arm64-v8a', 'device'), ('x86_64', 'device')) if platform == 'android' else
				(('arm64', 'device'), ('arm64', 'simulator'), ('x86_64', 'simulator'))):
				record = python.record(platform, arch, variant)
				record['pythonVersion'] = minor + '.7'
				record['target'] = record['target'].replace('cp314', 'cp' + minor.replace('.', ''))
				record['extensionSuffix'] = record['extensionSuffix'].replace('314', minor.replace('.', ''))
				slice_root = stage / 'slices' / record['target']
				python.slice_files(slice_root, record)
				if platform == 'android':
					(slice_root / 'jniLibs' / arch / 'libveroscript_python.so').write_bytes(python.elf(arch, build_id=record['buildId'], dependency='libpython' + minor + '.so'))
				records.append(record)
			if platform == 'ios':
				device, arm, intel = [stage / 'slices' / r['target'] / 'Frameworks/veroscript._veroscript.framework' for r in records]
				simulator = root / ('simulator-' + minor) / 'veroscript._veroscript.framework'
				shutil.copytree(arm, simulator)
				(simulator / 'veroscript._veroscript').write_bytes(python.fat_macho((arm / 'veroscript._veroscript').read_bytes(), (intel / 'veroscript._veroscript').read_bytes()))
				python.create_xcframework([device, simulator], stage / 'VeroScriptPython.xcframework')
			python.inventory(stage, records)
			for name, source in {'LICENSE': 'LICENSE',
				'THIRD-PARTY-NOTICES.md': 'Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md',
				'device_smoke.py': 'Extensions/Language/Python/Resources/Tools/Mobile/device_smoke.py'}.items():
				(stage / name).write_bytes((REPO / source).read_bytes())
			metadata_path = stage / 'python-mobile.json'
			metadata = json.loads(metadata_path.read_text())
			metadata['files'] = {file.relative_to(stage).as_posix(): hashlib.sha256(file.read_bytes()).hexdigest()
				for file in stage.rglob('*') if file.is_file() and file != metadata_path}
			metadata_path.write_text(json.dumps(metadata))
			deployments[platform].append(stage)
	from veroscript_products import MOBILE_EMBEDDING_BUSINESSES
	return {target: {('Android' if target == 'android' else 'iOS'): [android if target == 'android' else ios],
		'CSharp': [package], 'Python': deployments[target],
		**{business: [native_cmake_fixture(root, business, target)] for business in MOBILE_EMBEDDING_BUSINESSES}}
		for target in ('android', 'ios')}
