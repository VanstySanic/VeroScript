# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Mobile SDK release contracts, independent of the packaging host's platform.

Android and iOS identify multi-ABI distributions, never desktop native targets.
The ordinary publisher owns locks, transactions, checksums, indices and pruning.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

from veroscript_paths import module_root
from veroscript_products import (MOBILE_PRODUCTS, MOBILE_NATIVE_BUSINESSES, MOBILE_NATIVE_PREFIXES,
	MODULE_BUSINESSES, target_businesses, verify_functional_payload)
from veroscript_release import (archive_entries, archive_tree, digest, linked_path,
	MOBILE_TARGETS, safe_member, scratch_directory, version, write_manifest)
from veroscript_mobile import (mobile_catalog_files, verify_android_aar,
	verify_android_elf, verify_android_jit_aar, verify_ios_archive)

MOBILE_PYTHON_VERSIONS = ('3.13', '3.14')
SDK_PREFIXES = {'Android': 'VeroScriptAndroidSDK', 'iOS': 'VeroScriptIOSSDK', **MOBILE_NATIVE_PREFIXES}
APPLICATION_NOTICES = {'MINIZ-LICENSE.txt': 'VeroScriptApplication/ThirdParty/miniz/LICENSE',
	'JSON-LICENSE.txt': 'VeroScriptApplication/ThirdParty/json/LICENSE.MIT',
	'SHA256-LICENSE.txt': 'VeroScriptApplication/ThirdParty/sha256/LICENSE'}


def source_native_version(repo: Path) -> str:
	declarations = re.findall(r'\bVEROSCRIPT_VERSION\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}\s*;',
		(module_root(repo) / 'VeroScript/Public/VeroScriptVersion.h').read_text(encoding='utf-8-sig'))
	if len(declarations) != 1:
		raise ValueError('Missing or ambiguous Core source version')
	return '.'.join(str(int(value)) for value in declarations[0])


def load_tool(relative: str, repo: Path | None = None):
	"""Load a source-owned validator without requiring the caller's working directory."""
	path = module_root(repo) / relative
	name = '_vero_mobile_' + hashlib.sha256(str(path).encode()).hexdigest()[:16]
	spec = importlib.util.spec_from_file_location(name, path)
	if spec is None or spec.loader is None:
		raise ValueError(f'Cannot load mobile validator: {path}')
	module = importlib.util.module_from_spec(spec)
	# The Python validator imports its adjacent mobile_sdk module.
	previous_path = sys.path[:]
	previous_sdk = sys.modules.get('mobile_sdk')
	isolated_sdk = path.name != 'mobile_sdk.py' and path.with_name('mobile_sdk.py').is_file()
	try:
		sys.path.insert(0, str(path.parent))
		if isolated_sdk:
			sys.modules['mobile_sdk'] = load_tool(path.with_name('mobile_sdk.py').relative_to(module_root(repo)).as_posix(), repo)
		spec.loader.exec_module(module)
	finally:
		sys.path[:] = previous_path
		if isolated_sdk:
			if previous_sdk is None:
				sys.modules.pop('mobile_sdk', None)
			else:
				sys.modules['mobile_sdk'] = previous_sdk
	return module


def csharp_tool(repo: Path | None = None):
	return load_tool('Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py', repo)


def python_tool(repo: Path | None = None):
	return load_tool('Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py', repo)


def payload_files(root: Path) -> dict[str, str]:
	from veroscript_business_release import guard_tree
	guard_tree(root)
	return {file.relative_to(root).as_posix(): digest(file)
			for file in sorted(root.rglob('*')) if file.is_file()}


def guard_input(path: Path) -> None:
	from veroscript_business_release import guard_tree
	guard_tree(path if path.is_dir() else path.parent)
	if linked_path(path) or not (path.is_dir() or path.is_file()):
		raise ValueError(f'Missing or linked mobile input: {path}')


def verify_inventory(root: Path, filename: str) -> dict:
	metadata = json.loads((root / filename).read_text(encoding='utf-8-sig'))
	if not isinstance(metadata, dict):
		raise ValueError('Expected a mobile payload metadata object')
	if type(metadata.get('schemaVersion')) is not int or metadata['schemaVersion'] != 1:
		raise ValueError('Invalid mobile payload schema version')
	files = metadata.get('files')
	if not isinstance(files, dict) or not files:
		raise ValueError('Missing mobile payload inventory')
	for name, checksum in files.items():
		if portable_member(name) != name or not re.fullmatch('[0-9a-f]{64}', str(checksum)):
			raise ValueError('Invalid mobile payload path/digest')
	actual = payload_files(root)
	actual.pop(filename, None)
	if actual != files:
		raise ValueError('Missing, changed or unexpected mobile payload')
	verify_functional_payload(actual)
	return metadata


def portable_member(name: str) -> str:
	"""Reject path aliases that would be rewritten by Windows during extraction."""
	if not isinstance(name, str) or safe_member(name) != name:
		raise ValueError('Non-canonical mobile archive path')
	for part in name.split('/'):
		device = part.split('.')[0].rstrip(' ').upper()
		if (not part or part.endswith(('.', ' ')) or any(ord(char) < 32 or char in '<>"|?*' for char in part)
				or device in {'CON', 'PRN', 'AUX', 'NUL', 'CONIN$', 'CONOUT$'}
				or re.fullmatch(r'(?:COM|LPT)[1-9\u00b9\u00b2\u00b3]', device)):
			raise ValueError(f'Non-portable mobile archive path: {name}')
	return name


def validate_zip_members(members) -> None:
	"""Check directory spelling and file types as well as individual filenames."""
	spellings, files, directories = {}, set(), set()
	for item in members:
		if getattr(item, 'orig_filename', item.filename) != item.filename:
			raise ValueError('Mobile ZIP filename was normalized before validation')
		name = portable_member(item.filename[:-1] if item.is_dir() else item.filename)
		mode = stat.S_IFMT(item.external_attr >> 16)
		if mode not in ({0, stat.S_IFDIR} if item.is_dir() else {0, stat.S_IFREG}):
			raise ValueError('Unsupported mobile ZIP entry type: ' + name)
		parts = name.split('/')
		for index in range(1, len(parts) + 1):
			prefix = '/'.join(parts[:index])
			key = prefix.casefold()
			if key in spellings and spellings[key] != prefix:
				raise ValueError('Case-colliding mobile archive entries: ' + prefix)
			spellings[key] = prefix
			if index < len(parts) or item.is_dir():
				if key in files:
					raise ValueError('Mobile archive file/directory collision: ' + prefix)
				directories.add(key)
			else:
				if key in files or key in directories:
					raise ValueError('Duplicate/colliding mobile archive file: ' + prefix)
				files.add(key)


def extract_archive(path: Path, destination: Path, *, enclosing: str | None = None) -> None:
	"""Validate every entry before streaming files; never follow archive links."""
	entries = checked_zip_entries(path, max_entry_bytes=0)
	verify_functional_payload(entries)
	if len({name.casefold() for name in entries}) != len(entries):
		raise ValueError('Case-colliding mobile archive entries')
	if enclosing is not None and any(not name.startswith(enclosing + '/') for name in entries):
		raise ValueError('Mobile archive must have its canonical enclosing directory')
	with zipfile.ZipFile(path) as archive:
		validate_zip_members(archive.infolist())
		for item in archive.infolist():
			if item.is_dir():
				continue
			name = safe_member(item.filename)
			if name not in entries:
				raise ValueError('Unexpected mobile ZIP member')
			output = destination / name
			output.parent.mkdir(parents=True, exist_ok=True)
			with archive.open(item) as source, output.open('wb') as target:
				shutil.copyfileobj(source, target)


def checked_zip_entries(path: Path, *, max_entry_bytes: int = 0):
	with zipfile.ZipFile(path) as archive:
		validate_zip_members(archive.infolist())
	return archive_entries(path, max_entry_bytes=max_entry_bytes)


def verify_xcframework(root: Path, repo: Path | None = None, *, require_ios_api: bool = False) -> None:
	"""Audit static device/simulator objects, including universal simulator slices."""
	helper = csharp_tool()
	info = plistlib.loads((root / 'Info.plist').read_bytes())
	seen, native = set(), set()
	for library in info.get('AvailableLibraries', []):
		variant = library.get('SupportedPlatformVariant', 'device')
		if library.get('SupportedPlatform') != 'ios' or variant not in {'device', 'simulator'}:
			raise ValueError('Mobile XCFramework contains a non-iOS slice')
		directory = root / safe_member(library['LibraryIdentifier'])
		binary = directory / safe_member(library['LibraryPath'])
		headers = directory / safe_member(library['HeadersPath'])
		if binary.suffix != '.a' or not (headers / 'VeroScriptInterop.h').is_file():
			raise ValueError('Mobile XCFramework requires a static library and Interop headers')
		if require_ios_api:
			for name in ('VeroScriptIOS.h', 'module.modulemap'):
				if not (headers / name).is_file() or not (headers / name).read_bytes():
					raise ValueError('Missing iOS adapter header/module map: ' + name)
				if repo and (headers / name).read_bytes() != (module_root(repo) / 'Extensions/Platform/Mobile/VeroScriptIOS/Public' / name).read_bytes():
					raise ValueError('Stale iOS adapter header/module map: ' + name)
		if repo and (headers / 'VeroScriptInterop.h').read_bytes() != (module_root(repo) / 'VeroScriptInterop/Public/VeroScriptInterop.h').read_bytes():
			raise ValueError('Stale iOS Interop headers')
		native.add(binary.relative_to(root).as_posix())
		architectures = library.get('SupportedArchitectures', [])
		if not architectures or len(set(architectures)) != len(architectures):
			raise ValueError('Invalid XCFramework architecture list')
		expected = {'x64' if arch == 'x86_64' else arch for arch in architectures}
		slices = helper.archive_slices(binary.read_bytes())
		if set(slices) == {'thin'} and len(expected) == 1:
			slices = {next(iter(expected)): slices['thin']}
		if set(slices) != expected:
			raise ValueError('XCFramework architecture metadata disagrees with native slices')
		with tempfile.TemporaryDirectory(prefix='vero-mobile-objects-') as temporary:
			for arch, data in slices.items():
				identity = (variant, arch)
				if identity in seen or identity not in {('device', 'arm64'), ('simulator', 'arm64'), ('simulator', 'x64')}:
					raise ValueError('Duplicate or unsupported iOS slice')
				seen.add(identity)
				path = Path(temporary) / 'slice.a'
				path.write_bytes(data)
				verify_ios_archive(path, arch, variant == 'simulator')
	if seen != {('device', 'arm64'), ('simulator', 'arm64'), ('simulator', 'x64')}:
		raise ValueError('Missing iOS device or simulator slice')
	helper.verify_native_inventory({name: (root / name).read_bytes() for name in payload_files(root)}, native)


def verify_aot_headers(root: Path, metadata: dict, repo: Path | None) -> None:
	contract = metadata.get('aotHeaders', {})
	if contract.get('profile') != 'core-linked-aot' or contract.get('includeDirectory') != 'aot/include':
		raise ValueError('Missing mobile AOT service contract')
	for key in ('publicAbi', 'coreImplementation'):
		if not re.fullmatch('[0-9a-f]{64}', str(contract.get(key))):
			raise ValueError('Invalid mobile AOT identity')
	header = root / 'aot/include/VeroScriptJitBuild.h'
	values = {'VEROSCRIPT_JIT_PUBLIC_ABI': contract['publicAbi'],
			  'VEROSCRIPT_CORE_IMPLEMENTATION_ID': contract['coreImplementation']}
	for name, expected in values.items():
		if re.findall(rb'^#define ' + name.encode() + rb' "([0-9a-f]{64})"\r?$', header.read_bytes(), re.MULTILINE) != [expected.encode()]:
			raise ValueError('Stale mobile AOT identity header')
	if repo:
		builder = load_tool('Resources/Tools/Packaging/package-veroscript-mobile.py', repo)
		if values != builder.aot_source_contract(module_root(repo)):
			raise ValueError('Mobile AOT headers do not match current Core sources')
		expected = {p.relative_to(module_root(repo) / 'VeroScript/Public').as_posix(): p.read_bytes()
			for p in (module_root(repo) / 'VeroScript/Public').rglob('*') if p.is_file()}
		actual = {p.relative_to(root / 'aot/include').as_posix(): p.read_bytes()
			for p in (root / 'aot/include').rglob('*') if p.is_file() and p != header}
		if actual != expected:
			raise ValueError('Incomplete or stale mobile Core service headers')


def verify_native_sdk(root: Path, target: str, native_version: str, repo: Path | None = None) -> None:
	metadata = json.loads((root / 'mobile-sdk.json').read_text(encoding='utf-8-sig'))
	if (metadata.get('product') != 'VeroScript Mobile SDK' or metadata.get('platform') != target
			or metadata.get('version') != native_version or metadata.get('nativeJit') is not False
			or metadata.get('defaultExecution') != 'vm' or not (root / 'LICENSE').read_bytes()):
		raise ValueError('Mobile SDK target/version/execution identity mismatch')
	verify_aot_headers(root, metadata, repo)
	if repo and (root / 'LICENSE').read_bytes() != (module_root(repo) / 'LICENSE').read_bytes():
		raise ValueError('Stale mobile project license')
	expected = {'mobile-sdk.json', 'LICENSE', 'aot'} | ({f'VeroScriptAndroid-{native_version}.aar'} if target == 'android' else {'assets', 'VeroScriptIOS.xcframework'})
	if {p.name for p in root.iterdir()} != expected:
		raise ValueError('Unexpected native mobile SDK payload')
	# Embedded framework archives are audited separately below. Headers/resources
	# cannot authorize an additional executable merely by hashing it.
	helper = csharp_tool()
	helper.verify_native_inventory({name: (root / name).read_bytes() for name in payload_files(root)
		if not name.startswith('VeroScriptIOS.xcframework/') and not name.endswith('.aar')}, set())
	if target == 'android':
		archives = list(root.glob('VeroScriptAndroid-*.aar'))
		if len(archives) != 1 or archives[0].name != f'VeroScriptAndroid-{native_version}.aar':
			raise ValueError('Expected exactly one matching Android AAR')
		verify_android_aar(archives[0], module_root(repo) / 'VeroScript/Resources/Scripts/stdlib' if repo else None,
			mobile_catalog_files(module_root(repo)) if repo else None,
			module_root(repo) / 'VeroScriptInterop/Public/VeroScriptInterop.h' if repo else None)
		verify_functional_payload(checked_zip_entries(archives[0]))
		if repo:
			with zipfile.ZipFile(archives[0]) as archive:
				for name, source in {'LICENSE': 'LICENSE', **APPLICATION_NOTICES}.items():
					if archive.read('assets/veroscript/licenses/' + name) != (module_root(repo) / source).read_bytes():
						raise ValueError('Stale Android redistribution notice: ' + name)
	else:
		verify_xcframework(root / 'VeroScriptIOS.xcframework', repo, require_ios_api=True)
		if not (root / 'assets/stdlib/core.vs').is_file():
			raise ValueError('Missing iOS standard library')
		if repo:
			expected = {'stdlib/' + p.relative_to(module_root(repo) / 'VeroScript/Resources/Scripts/stdlib').as_posix(): p.read_bytes()
				for p in (module_root(repo) / 'VeroScript/Resources/Scripts/stdlib').rglob('*') if p.is_file()}
			expected.update({'localization/' + name: data for name, data in mobile_catalog_files(module_root(repo)).items()})
			actual = {name: (root / 'assets' / name).read_bytes() for name in payload_files(root / 'assets')}
			if actual != expected:
				raise ValueError('Incomplete or stale iOS functional resources')


def managed_assembly(data: bytes) -> bool:
	"""Require a PE CLR directory, rather than accepting a renamed native DLL."""
	if len(data) < 64 or data[:2] != b'MZ':
		return False
	pe = struct.unpack_from('<I', data, 60)[0]
	if pe + 24 > len(data) or data[pe:pe + 4] != b'PE\0\0':
		return False
	size = struct.unpack_from('<H', data, pe + 20)[0]
	optional = pe + 24
	if optional + size > len(data) or size < 2:
		return False
	magic = struct.unpack_from('<H', data, optional)[0]
	directories = optional + (96 if magic == 0x10b else 112 if magic == 0x20b else size)
	if directories + 15 * 8 > optional + size:
		return False
	rva, length = struct.unpack_from('<II', data, directories + 14 * 8)
	count = struct.unpack_from('<H', data, pe + 6)[0]
	if not 0 < count <= 96 or length < 72:
		return False
	sections = optional + size
	if sections + 40 * count > len(data):
		return False
	def offset(address, length):
		for index in range(count):
			virtual_size, virtual_address, raw_size, raw = struct.unpack_from('<IIII', data, sections + 40 * index + 8)
			start = address - virtual_address
			if 0 <= start and start + length <= raw_size and raw + start + length <= len(data):
				return raw + start
		return None
	cli = offset(rva, 72)
	if cli is None or struct.unpack_from('<I', data, cli)[0] < 72:
		return False
	metadata_rva, metadata_size = struct.unpack_from('<II', data, cli + 8)
	metadata = offset(metadata_rva, metadata_size)
	return metadata_size >= 4 and metadata is not None and data[metadata:metadata + 4] == b'BSJB'


def csharp_frameworks(frameworks) -> bool:
	return (len(frameworks) == 4 and len(set(frameworks)) == 4 and {'net8.0', 'net9.0'}.issubset(frameworks)
		and sum(bool(re.fullmatch(r'net10\.0-android(?:\d+(?:\.\d+)*)?', name)) for name in frameworks) == 1
		and sum(bool(re.fullmatch(r'net10\.0-ios(?:\d+(?:\.\d+)*)?', name)) for name in frameworks) == 1)


def validate_csharp_symbol_entries(entries, frameworks) -> None:
	if not csharp_frameworks(frameworks):
		raise ValueError('Invalid C# mobile symbol framework matrix')
	verify_functional_payload(entries)
	expected = {f'lib/{framework}/VeroScriptCSharp.pdb' for framework in frameworks}
	actual = {name for name in entries if name.startswith('lib/')}
	if actual != expected or any(entries[name][0] != b'BSJB' for name in expected):
		raise ValueError('C# mobile symbols must match all managed target frameworks')
	for name in entries:
		if name not in expected and not (name.endswith('.nuspec') or name in {'[Content_Types].xml', '_rels/.rels', '.signature.p7s'}
				or name.startswith('package/services/metadata/core-properties/') and name.endswith('.psmdcp')):
			raise ValueError('Unexpected C# mobile symbol payload: ' + name)


def verify_csharp_symbols(path: Path, release_version: str, frameworks) -> None:
	from veroscript_business_release import distribution_identity
	if path.name != f'VeroScript.CSharp.{release_version}.snupkg' or distribution_identity(path) != ('CSharp', release_version):
		raise ValueError('C# mobile symbol package identity/version mismatch')
	with zipfile.ZipFile(path) as archive:
		nuspecs = [name for name in archive.namelist() if name.endswith('.nuspec')]
		document = ET.fromstring(archive.read(nuspecs[0]))
		types = [node.get('name') for node in document.iter() if node.tag.split('}')[-1] == 'packageType']
		if types != ['SymbolsPackage'] or any(node.tag.split('}')[-1] == 'license' for node in document.iter()):
			raise ValueError('C# .snupkg must declare SymbolsPackage and use its main package license')
	validate_csharp_symbol_entries(checked_zip_entries(path, max_entry_bytes=4), frameworks)


def verify_csharp_package(path: Path, release_version: str, repo: Path | None = None):
	from veroscript_business_release import distribution_identity
	if distribution_identity(path) != ('CSharp', release_version) or path.name != f'VeroScript.CSharp.{release_version}.nupkg':
		raise ValueError('C# mobile NuGet identity/version mismatch')
	with tempfile.TemporaryDirectory(prefix='vero-csharp-mobile-audit-') as temporary:
		root = Path(temporary)
		extract_archive(path, root)
		assemblies = list((root / 'lib').glob('*/VeroScriptCSharp.dll'))
		frameworks = [p.parent.name for p in assemblies]
		if not csharp_frameworks(frameworks) or not all(managed_assembly(p.read_bytes()) for p in assemblies):
			raise ValueError('Mobile NuGet requires complete desktop, Android and iOS managed assemblies')
		build = root / 'buildTransitive/VeroScript.CSharp.targets'
		if not build.is_file() or not (root / 'LICENSE').is_file():
			raise ValueError('Missing C# mobile build integration/license')
		metadata = verify_inventory(root / 'native', 'mobile-sdk.json')
		if metadata.get('schemaVersion') != 1 or metadata.get('abiVersion') != '0x00000101' or metadata.get('nativeJit') is not False:
			raise ValueError('Invalid C# native mobile contract')
		for abi in ('arm64-v8a', 'x86_64'):
			for name in ('libVeroScriptInterop.so', 'libc++_shared.so'):
				verify_android_elf((root / 'native/android' / abi / name).read_bytes(), abi)
		verify_xcframework(root / 'native/ios/VeroScriptIOS.xcframework', repo)
		helper = csharp_tool()
		helper.verify_native_inventory({name: (root / 'native' / name).read_bytes() for name in metadata['files']
			if not name.startswith('ios/VeroScriptIOS.xcframework/')},
			{f'android/{abi}/{name}' for abi in ('arm64-v8a', 'x86_64') for name in ('libVeroScriptInterop.so', 'libc++_shared.so')})
		resources = json.loads((root / 'native/assets/resources.json').read_text(encoding='utf-8'))
		actual = {name: value for name, value in payload_files(root / 'native/assets').items() if name != 'resources.json'}
		if resources != {'schemaVersion': 1, 'files': actual} or 'stdlib/core.vs' not in actual:
			raise ValueError('C# mobile resource inventory mismatch')
		for name in ('LICENSE', 'NOTICE', 'NOTICE.toolchain', *APPLICATION_NOTICES):
			if not (root / 'native/licenses' / name).read_bytes():
				raise ValueError('Missing C# mobile redistribution notice')
		ET.parse(root / 'native/ios/VeroScriptInterop.symbols.targets')
		if repo:
			helper = csharp_tool(repo)
			helper.verify(root / 'native')
			for name, source in {'LICENSE': 'LICENSE', **APPLICATION_NOTICES}.items():
				if (root / 'native/licenses' / name).read_bytes() != (module_root(repo) / source).read_bytes():
					raise ValueError('Stale C# redistribution notice: ' + name)
			if (root / 'LICENSE').read_bytes() != (module_root(repo) / 'LICENSE').read_bytes():
				raise ValueError('Stale C# project license')
			if build.read_bytes() != (module_root(repo) / 'Extensions/Language/CSharp/VeroScriptCSharp/buildTransitive/VeroScript.CSharp.targets').read_bytes():
				raise ValueError('Stale C# mobile build integration')
		return tuple(frameworks)


def verify_python_sdk(root: Path, target: str, native_version: str, repo: Path | None = None) -> str:
	metadata = verify_inventory(root, 'python-mobile.json')
	if (metadata.get('schemaVersion') != 1 or metadata.get('product') != 'VeroScript Python Mobile'
			or metadata.get('version') != native_version or metadata.get('includesCPythonRuntime') is not False
			or metadata.get('nativeJit') is not False):
		raise ValueError('Mobile Python product/version contract mismatch')
	records = metadata.get('slices', [])
	helper = python_tool()
	expected = ({('arm64-v8a', 'device'), ('x86_64', 'device')} if target == 'android' else
		{('arm64', 'device'), ('arm64', 'simulator'), ('x86_64', 'simulator')})
	if (len(records) != len(expected) or {(r.get('architecture'), r.get('variant')) for r in records} != expected
			or any(r.get('platform') != target or r.get('productVersion') != native_version for r in records)
			or len({r.get('pythonVersion') for r in records}) != 1):
		raise ValueError('Incomplete mobile Python platform/ABI matrix')
	minor = '.'.join(records[0]['pythonVersion'].split('.')[:2])
	if minor not in MOBILE_PYTHON_VERSIONS:
		raise ValueError('Unsupported mobile CPython version')
	for record in records:
		helper.validate_record(record)
		stage = root / 'slices' / safe_member(record['target'])
		if target == 'android':
			binary = (stage / 'jniLibs' / record['architecture'] / 'libveroscript_python.so').read_bytes()
			verify_android_elf(binary, record['architecture'])
			verify_android_elf((stage / 'jniLibs' / record['architecture'] / 'libc++_shared.so').read_bytes(), record['architecture'])
			exports, dependencies = helper.elf_symbols_and_dependencies(binary)
			allowed = {f'libpython{minor}.so', 'libc++_shared.so', 'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so'}
			if 'PyInit__veroscript' not in exports or f'libpython{minor}.so' not in dependencies or dependencies - allowed:
				raise ValueError('Invalid mobile Python initializer/dependencies')
			helper.directory_entries(stage, {'python', 'jniLibs'})
			helper.directory_entries(stage / 'jniLibs', {record['architecture']})
			helper.directory_entries(stage / 'jniLibs' / record['architecture'], {'libveroscript_python.so', 'libc++_shared.so'})
		else:
			binary = (stage / 'Frameworks/veroscript._veroscript.framework/veroscript._veroscript').read_bytes()
			# The original verifier checks framework identities, markers and all slices.
			identity = helper.ios_binary(binary, record['architecture'], record['variant'] == 'simulator', '15.0')
			if identity['installName'] != '@rpath/veroscript._veroscript.framework/veroscript._veroscript' or '_PyInit__veroscript' not in helper.macho_exports(binary):
				raise ValueError('Missing iOS Python initializer')
			dependencies = helper.macho_dependencies(binary)
			if '@rpath/Python.framework/Python' not in dependencies or any(name != '@rpath/Python.framework/Python'
				and not name.startswith(('/usr/lib/', '/System/Library/Frameworks/')) for name in dependencies):
				raise ValueError('Unexpected iOS Python native dependencies')
			framework = stage / 'Frameworks/veroscript._veroscript.framework'
			info = plistlib.loads((framework / 'Info.plist').read_bytes())
			if (info.get('CFBundleExecutable') != 'veroscript._veroscript' or info.get('CFBundlePackageType') != 'FMWK'
					or info.get('CFBundleVersion') != native_version or info.get('CFBundleShortVersionString') != native_version
					or (framework / 'veroscript._veroscript.origin').read_text(encoding='utf-8').strip() != 'python/veroscript/_veroscript.fwork'
					or (stage / 'python/veroscript/_veroscript.fwork').read_text(encoding='utf-8').strip() != 'Frameworks/veroscript._veroscript.framework/veroscript._veroscript'):
				raise ValueError('Invalid iOS Python framework metadata/markers')
			helper.directory_entries(stage, {'python', 'Frameworks'})
			helper.directory_entries(stage / 'Frameworks', {'veroscript._veroscript.framework'})
			helper.directory_entries(stage / 'python', {'veroscript'})
		if record['buildId'].encode() not in binary:
			raise ValueError('Mobile Python binary/build identity mismatch')
		for name in ('__init__.py', '_mobile.py', '_veroscript.pyi', 'py.typed', 'stdlib/core.vs'):
			if not (stage / 'python/veroscript' / name).is_file():
				raise ValueError('Missing mobile Python runtime resources')
	for name in ('LICENSE', 'THIRD-PARTY-NOTICES.md', 'device_smoke.py', *(['ANDROID-NDK-NOTICE.txt'] if target == 'android' else [])):
		if not (root / name).is_file() or not (root / name).read_bytes():
			raise ValueError('Missing mobile Python license/acceptance tool')
	if target == 'ios':
		helper.audit_xcframework(root, records)
	helper.audit_native_inventory(root, records)
	helper.directory_entries(root / 'slices', {record['target'] for record in records})
	helper.directory_entries(root, {'python-mobile.json', 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'device_smoke.py', 'slices'} |
		({'VeroScriptPython.xcframework'} if target == 'ios' else {'ANDROID-NDK-NOTICE.txt'}))
	if repo:
		python_tool(repo).verify_package(root)
		for name, source in {'LICENSE': 'LICENSE',
			'THIRD-PARTY-NOTICES.md': 'Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md',
			'device_smoke.py': 'Extensions/Language/Python/Resources/Tools/Mobile/device_smoke.py'}.items():
			if (root / name).read_bytes() != (module_root(repo) / source).read_bytes():
				raise ValueError('Stale Python mobile license/acceptance tool: ' + name)
	return minor


def artifact_names(business: str, target: str, release_version: str) -> list[str]:
	if target not in MOBILE_TARGETS or business not in target_businesses(target, optional=True):
		raise ValueError('Unsupported mobile artifact business/target')
	if business in SDK_PREFIXES:
		return [f'{SDK_PREFIXES[business]}-{release_version}-{target}.zip']
	if business == 'CSharp':
		return [f'VeroScript.CSharp.{release_version}.nupkg']
	if business == 'Jit' and target == 'android':
		return [f'vs-asmjit-{release_version}-android.aar']
	return [f'VeroScriptPythonMobile-{release_version}-{target}-cp{minor.replace(".", "")}.zip' for minor in MOBILE_PYTHON_VERSIONS]


def optional_artifact_names(business: str, target: str, release_version: str) -> list[str]:
	artifact_names(business, target, release_version)
	if business in MODULE_BUSINESSES:
		from veroscript_module_symbols import symbol_archive_name
		return [symbol_archive_name(business, release_version, target)]
	if business != 'CSharp':
		return []
	from veroscript_csharp_packages import PACKAGE_IDS
	return [f'VeroScript.CSharp.{release_version}.snupkg',
		*(f'{package_id}.{release_version}.{suffix}' for package_id in PACKAGE_IDS[1:] for suffix in ('nupkg', 'snupkg'))]


def host_interpreters(required, explicit) -> dict[str, str]:
	"""Select HOST CPython per target minor; never execute a target interpreter."""
	from veroscript_python_wheels import interpreter_identity
	if not required or not set(required).issubset(MOBILE_PYTHON_VERSIONS):
		raise ValueError('Unsupported mobile host Python matrix')
	requested = list(explicit)
	candidates = [*requested, sys.executable]
	for minor in required:
		path = shutil.which('python' + minor)
		if path:
			candidates.append(path)
	found = {}
	for executable in dict.fromkeys(candidates):
		try:
			identity = interpreter_identity(executable)
		except (OSError, ValueError, subprocess.SubprocessError) as error:
			if executable in requested:
				raise ValueError(f'Cannot inspect requested host CPython: {executable}') from error
			continue
		if (identity['implementation'] != 'cpython' or identity['debug'] or identity['freeThreaded']):
			if executable in requested:
				raise ValueError('Mobile builds require normal, non-debug, non-free-threaded host CPython')
			continue
		if identity['version'] in required:
			found.setdefault(identity['version'], identity['executable'])
	missing = set(required) - found.keys()
	if missing:
		raise ValueError(f'Missing matching HOST CPython {sorted(missing)}; repeat --host-python for each target minor')
	return found


def artifact_build_id(items) -> str:
	values = sorted(item['sha256'] for item in items)
	return values[0] if len(values) == 1 else hashlib.sha256(''.join(values).encode()).hexdigest()


def mobile_native_versions(manifest: dict, selected) -> dict:
	native_version = version(manifest['nativeVersion'])
	values = manifest.get('businessNativeVersions', {business: native_version for business in selected})
	if not isinstance(values, dict) or set(values) != set(selected):
		raise ValueError('Invalid per-business mobile native versions')
	for value in values.values():
		version(value)
	return values


def verify_mobile_release(root: Path, manifest: dict, repo: Path | None, *, businesses=None, partial_businesses=None) -> dict:
	from veroscript_business_release import artifact_business
	target = manifest['target']
	selected = manifest.get('selectedBusinesses')
	if (not isinstance(selected, list) or not selected or any(not isinstance(b, str) for b in selected) or len(set(selected)) != len(selected)
			or not set(selected).issubset(target_businesses(target, optional=True))
			or businesses is not None and set(selected) != set(businesses)):
		raise ValueError('Invalid mobile release business selection')
	declared_partial = manifest.get('partialBusinesses', [])
	if not isinstance(declared_partial, list) or any(not isinstance(b, str) for b in declared_partial) or len(set(declared_partial)) != len(declared_partial):
		raise ValueError('Invalid partial mobile business selection')
	partial = set(declared_partial)
	if not partial.issubset({'Python'} & set(selected)) or partial_businesses is not None and partial != set(partial_businesses):
		raise ValueError('Invalid partial mobile business selection')
	versions = manifest.get('businessVersions', {})
	from veroscript_products import ALL_BUSINESSES
	if not isinstance(versions, dict) or not set(versions).issubset(ALL_BUSINESSES):
		raise ValueError('Unknown mobile business versions')
	for value in versions.values():
		version(value)
	native_versions = mobile_native_versions(manifest, selected)
	for business in selected:
		version(versions[business])
	if set(manifest.get('businessBuildIds', {})) != set(selected):
		raise ValueError('Missing or unknown mobile business build identity')
	if repo:
		from veroscript_business_release import load_versions
		source_version = source_native_version(repo)
		if source_version != load_versions(repo)['nativeVersion'] or any(value != source_version for value in native_versions.values()):
			raise ValueError('Mobile native version does not match the source release')
	groups = {business: [] for business in selected}
	for item in manifest['artifacts']:
		business = artifact_business(item['path'])
		if business not in groups:
			raise ValueError('Unexpected mobile release artifact owner')
		name = Path(item['path']).name
		symbol = business in MODULE_BUSINESSES and name in optional_artifact_names(business, target, versions[business])
		if item['path'] != ('symbols/' if symbol else 'mobile/') + name:
			raise ValueError('Unexpected mobile release artifact route')
		groups[business].append(item)
	for business, items in groups.items():
		v = versions[business]
		native_version = native_versions[business]
		expected = set(artifact_names(business, target, v))
		allowed = expected | set(optional_artifact_names(business, target, v))
		actual = {Path(item['path']).name for item in items}
		if not actual or not actual.issubset(allowed) or business not in partial and not expected.issubset(actual):
			raise ValueError(f'Incomplete or unexpected mobile {business} artifacts')
		if manifest.get('businessBuildIds', {}).get(business) != artifact_build_id(items):
			raise ValueError('Mobile business artifact/build identity mismatch')
		if business == 'CSharp':
			package = root / 'mobile' / next(iter(expected))
			frameworks = verify_csharp_package(package, v, repo)
			for item in items:
				if Path(item['path']).name == f'VeroScript.CSharp.{v}.snupkg':
					verify_csharp_symbols(root / item['path'], v, frameworks)
				elif Path(item['path']).name != f'VeroScript.CSharp.{v}.nupkg':
					from veroscript_business_release import artifact_support, distribution_identity
					path = root / item['path']
					if distribution_identity(path) != ('CSharp', v):
						raise ValueError('C# companion package identity/version mismatch')
					verify_functional_payload(checked_zip_entries(path, max_entry_bytes=4096))
					artifact_support(path, item['path'], 'CSharp', 'any')
			continue
		for item in items:
			path = root / item['path']
			if business in MODULE_BUSINESSES and path.name in optional_artifact_names(business, target, v):
				from veroscript_module_symbols import verify_module_symbols
				verify_module_symbols(path, business, v, target, peer_directory=root)
				continue
			if business == 'Jit':
				verify_functional_payload(checked_zip_entries(path))
				verify_android_jit_aar(path)
				if repo:
					with zipfile.ZipFile(path) as archive:
						for name, source in {'LICENSE': 'LICENSE', 'LICENSE.md': 'Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md',
							'VERSION.txt': 'Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/VERSION.txt'}.items():
							if archive.read('assets/veroscript/licenses/asmjit/' + name) != (module_root(repo) / source).read_bytes():
								raise ValueError('Stale Android backend redistribution notice: ' + name)
			else:
				with tempfile.TemporaryDirectory(prefix='vero-mobile-archive-audit-') as temporary:
					enclosing = path.stem
					extract_archive(path, Path(temporary), enclosing=enclosing)
					payload = Path(temporary) / enclosing
					seal = verify_inventory(payload, 'mobile-release.json')
					if any(seal.get(key) != value for key, value in {
						'schemaVersion': 1, 'business': business, 'target': target, 'version': v, 'nativeVersion': native_version}.items()):
						raise ValueError('Mobile archive identity/version mismatch')
					# The inner producer's inventory deliberately excludes the outer release seal.
					(payload / 'mobile-release.json').unlink()
					if business == 'Python':
						minor = verify_python_sdk(payload, target, native_version, repo)
						if path.name != f'VeroScriptPythonMobile-{v}-{target}-cp{minor.replace(".", "")}.zip':
							raise ValueError('Mobile Python filename/ABI mismatch')
					elif business in MOBILE_NATIVE_BUSINESSES:
						from veroscript_mobile_native import verify_native_cmake_sdk
						verify_native_cmake_sdk(payload, business, target, native_version, repo)
					else:
						verify_native_sdk(payload, target, native_version, repo)
	return manifest


def stage_mobile_candidate(repo: Path, stage: Path, config: dict, target: str, inputs: dict, *, partial_python: bool = False,
		module_symbol_directory: Path | None = None, module_symbol_inputs: list | None = None,
		module_symbol_tools: dict | None = None) -> None:
	"""Assemble an audited candidate from real producer outputs; never invent a slice."""
	from veroscript_business_release import guard_tree
	if target not in MOBILE_TARGETS or not inputs or not set(inputs).issubset(target_businesses(target, optional=True)):
		raise ValueError('Invalid mobile platform/business selection')
	if module_symbol_directory is not None and module_symbol_inputs is not None:
		raise ValueError('Choose existing module symbols or exact original module debug inputs')
	if module_symbol_tools and module_symbol_inputs is None:
		raise ValueError('Module symbol tools require original module debug inputs')
	if module_symbol_inputs is not None and (not isinstance(module_symbol_inputs, list) or not module_symbol_inputs
			or any(not isinstance(row, dict) or row.get('business') not in inputs or row.get('business') not in MODULE_BUSINESSES for row in module_symbol_inputs)):
		raise ValueError('Module debug inputs must belong to selected mobile module businesses')
	if module_symbol_directory is not None:
		guard_input(module_symbol_directory)
		if not module_symbol_directory.is_dir():
			raise ValueError('Expected an existing module symbol directory')
	canonical = os.environ.get('VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY')
	if 'CSharp' in inputs and canonical:
		from veroscript_csharp_packages import csharp_package_inventory
		inventory = csharp_package_inventory(Path(canonical), config['businesses']['CSharp'])
		expected = {path.name: checksum for _, path, checksum in inventory}
		if any(path.name not in expected or digest(path) != expected[path.name] for path in inputs['CSharp']):
			raise ValueError('Mobile C# input differs from the canonical common feed')
		inputs = {**inputs, 'CSharp': [path for _, path, _ in inventory]}
	guard_tree(stage)
	if stage.exists() and any(stage.iterdir()):
		raise ValueError('Mobile candidate directory must be new or empty')
	stage.mkdir(parents=True, exist_ok=True)
	for paths in inputs.values():
		for path in paths:
			guard_input(path)
			if stage.resolve().is_relative_to(path.resolve()) or path.resolve().is_relative_to(stage.resolve()):
				raise ValueError('Mobile input and candidate directories overlap')
	with tempfile.TemporaryDirectory(prefix='vero-mobile-payload-', dir=scratch_directory(repo)) as temporary:
		work = Path(temporary)
		for business, paths in inputs.items():
			v = config['businesses'][business]
			if business in {'CSharp', 'Jit'}:
				required = set(artifact_names(business, target, v))
				allowed = required | set(optional_artifact_names(business, target, v))
				names = [path.name for path in paths]
				if not required.issubset(names) or not set(names).issubset(allowed) or len(names) != len(set(names)):
					raise ValueError('Expected one correctly versioned mobile NuGet/AAR')
				for source in paths:
					output = stage / 'mobile' / source.name
					output.parent.mkdir(parents=True, exist_ok=True)
					shutil.copy2(source, output)
				continue
			if business in SDK_PREFIXES and len(paths) != 1:
				raise ValueError('Expected exactly one native mobile SDK')
			for source in paths:
				if business == 'Python':
					minor = verify_python_sdk(source, target, config['nativeVersion'], repo)
					name = f'VeroScriptPythonMobile-{v}-{target}-cp{minor.replace(".", "")}'
				else:
					name = f'{SDK_PREFIXES[business]}-{v}-{target}'
				payload = work / name
				if payload.exists():
					raise ValueError('Duplicate mobile CPython SDK/ABI')
				shutil.copytree(source, payload)
				if business in MOBILE_PRODUCTS.values():
					# Optional AsmJit remains its own Modules/Jit business.
					metadata_path = payload / 'mobile-sdk.json'
					metadata = json.loads(metadata_path.read_text(encoding='utf-8-sig'))
					if metadata.get('nativeJit') is True:
						if 'Jit' not in inputs:
							raise ValueError('Publish optional Android AsmJit through the Jit business')
						metadata['nativeJit'] = False
						metadata.pop('jitBackend', None)
						metadata_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
						for backend in payload.glob('vs-asmjit-*.aar'):
							backend.unlink()
					verify_native_sdk(payload, target, config['nativeVersion'], repo)
				elif business in MOBILE_NATIVE_BUSINESSES:
					from veroscript_mobile_native import verify_native_cmake_sdk
					verify_native_cmake_sdk(payload, business, target, config['nativeVersion'], repo)
				verify_functional_payload(payload_files(payload))
				seal = {'schemaVersion': 1, 'business': business, 'target': target, 'version': v,
					'nativeVersion': config['nativeVersion'], 'targetAcceptance': 'required', 'files': payload_files(payload)}
				(payload / 'mobile-release.json').write_text(json.dumps(seal, indent=2) + '\n', encoding='utf-8')
				archive_tree(payload, stage / 'mobile' / (name + '.zip'))
	symbol_result = None
	if module_symbol_directory is not None:
		allowed_symbols = {name for business in inputs if business in MODULE_BUSINESSES
			for name in optional_artifact_names(business, target, config['businesses'][business])}
		paths = list(module_symbol_directory.iterdir())
		if not paths or any(not path.is_file() or path.name not in allowed_symbols for path in paths):
			raise ValueError('Unexpected, empty or unselected mobile module symbol input')
		for source in paths:
			output = stage / 'symbols' / source.name
			output.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(source, output)
		from veroscript_module_symbols import verify_module_symbols
		symbol_result = {}
		for source in paths:
			business = next(business for business in inputs if business in MODULE_BUSINESSES
				and source.name in optional_artifact_names(business, target, config['businesses'][business]))
			metadata = verify_module_symbols(stage / 'symbols' / source.name, business,
				config['businesses'][business], target, peer_directory=stage)
			symbol_result[business] = {'status': 'produced', 'archive': str(stage / 'symbols' / source.name),
				'libraries': len(metadata['libraries']), 'unavailable': metadata.get('unavailable', [])}
	if module_symbol_inputs is not None:
		from veroscript_module_symbols import assemble_module_symbol_inputs
		symbol_result = assemble_module_symbol_inputs(repo, stage, config['businesses'], target, module_symbol_inputs, tools=module_symbol_tools)
	items = [{'path': p.relative_to(stage).as_posix(), 'sha256': digest(p)} for p in sorted(stage.rglob('*')) if p.is_file()]
	from veroscript_business_release import artifact_business
	ids = {business: artifact_build_id([item for item in items if artifact_business(item['path']) == business]) for business in inputs}
	write_manifest(stage, config['nativeVersion'], target, artifact_build_id(items), business_versions=config['businesses'], native_version=config['nativeVersion'])
	path = stage / 'release-manifest.json'
	manifest = json.loads(path.read_text(encoding='utf-8'))
	manifest.update(selectedBusinesses=list(inputs), businessBuildIds=ids, functionalPayloadBusinesses=list(inputs),
		businessNativeVersions={business: config['nativeVersion'] for business in inputs})
	if symbol_result is not None:
		from veroscript_module_symbols import portable_symbol_status
		manifest['moduleSymbolStatus'] = portable_symbol_status(symbol_result)
	if partial_python:
		if 'Python' not in inputs:
			raise ValueError('--partial-python requires the Python business')
		manifest['partialBusinesses'] = ['Python']
	path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
	from veroscript_release import verify_release
	verify_release(stage, repo, businesses=set(inputs), partial_businesses={'Python'} if partial_python else set())


def publish_mobile_component(repo: Path, source: Path, root: Path, business: str, release_version: str, target: str) -> None:
	"""Merge already-sealed mobile artifacts, retaining the other CPython ABI."""
	from veroscript_business_release import (load_versions, business_root, collection_root,
		acquire_collection_lock, release_collection_lock, read_business, verify_business_inventory, _publish_locked,
		collection_index, verify_collection, composed_businesses, audit_source)
	from veroscript_release import verify_release
	if business not in target_businesses(target, optional=True):
		raise ValueError('Unsupported mobile business')
	config = load_versions(repo, override=release_version, businesses=[business])
	root = collection_root(repo, root)
	lock = acquire_collection_lock(repo, root, create_root=True)
	try:
		with tempfile.TemporaryDirectory(prefix='vero-mobile-component-', dir=scratch_directory(repo)) as temporary:
			stage = Path(temporary)
			leaf = business_root(root, business, target) / release_version
			old = None
			if collection_index(root, target).is_file():
				old = composed_businesses(root, verify_collection(root, target)).get(business)
			if old and old['version'] == release_version:
				for item in old['artifacts']:
					output = stage / audit_source(item, business, target)
					output.parent.mkdir(parents=True, exist_ok=True)
					shutil.copy2(Path(item['_leaf']) / item['path'], output)
			from veroscript_business_release import guard_tree
			guard_tree(source)
			files = [p for p in source.iterdir() if p.is_file()]
			allowed = set(artifact_names(business, target, release_version) + optional_artifact_names(business, target, release_version))
			if not files or any(p.name not in allowed for p in files):
				raise ValueError('Unexpected mobile component artifact')
			if business == 'CSharp' and any(p.suffix == '.nupkg' for p in files) and not any(p.suffix == '.snupkg' for p in files):
				# A replacement DLL must not retain an older PDB from the previous build.
				for name in optional_artifact_names(business, target, release_version):
					(stage / 'mobile' / name).unlink(missing_ok=True)
			if business in MODULE_BUSINESSES:
				symbol_names = set(optional_artifact_names(business, target, release_version))
				if not any(file.name in symbol_names for file in files) and any(
					file.name not in symbol_names and (stage / 'mobile' / file.name).is_file()
					and digest(stage / 'mobile' / file.name) != digest(file) for file in files):
					for name in symbol_names:
						(stage / 'symbols' / name).unlink(missing_ok=True)
			for file in files:
				is_symbol = business in MODULE_BUSINESSES and file.name in optional_artifact_names(business, target, release_version)
				output = stage / ('symbols' if is_symbol else 'mobile') / file.name
				output.parent.mkdir(parents=True, exist_ok=True)
				shutil.copy2(file, output)
			items = [{'sha256': digest(p)} for p in stage.rglob('*') if p.is_file()]
			build_id = artifact_build_id(items)
			write_manifest(stage, release_version, target, build_id, business_versions=config['businesses'], native_version=config['nativeVersion'])
			path = stage / 'release-manifest.json'
			manifest = json.loads(path.read_text(encoding='utf-8'))
			manifest.update(selectedBusinesses=[business], businessBuildIds={business: build_id}, functionalPayloadBusinesses=[business],
				businessNativeVersions={business: config['nativeVersion']})
			if business == 'Python' and len(items) != len(MOBILE_PYTHON_VERSIONS):
				manifest['partialBusinesses'] = [business]
			path.write_text(json.dumps(manifest), encoding='utf-8')
			verify_release(stage, repo)
			_publish_locked(repo, stage, root, [business])
	finally:
		release_collection_lock(lock)
