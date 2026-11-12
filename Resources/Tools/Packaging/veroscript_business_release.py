# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Independent business versions and recoverable module Release publication."""
from __future__ import annotations
import argparse
import functools
import threading
import hashlib
from email.parser import BytesParser
import json
import os
import re
from pathlib import Path
import shutil
import sys
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET

from veroscript_paths import module_root, repository_root

if __name__ == '__main__':
	sys.modules.setdefault('veroscript_business_release', sys.modules[__name__])

from veroscript_release import TARGETS, RELEASE_TARGETS, MOBILE_TARGETS, digest, host_target, linked_path, safe_member, safe_output, scratch_directory, version

from veroscript_products import (FEATURE_MODULE_BUSINESSES, ASMJIT_PACKAGE_NAME, BYTECODE_VM_PACKAGE_NAME, MODULE_PACKAGE_NAMES, FOUNDATION_PACKAGE_NAMES, FOUNDATION_BUSINESSES, BUSINESSES, IDE_BUSINESSES, ALL_BUSINESSES, MODULE_BUSINESSES,
								 INSTALLER_BUSINESSES, IDE_PRODUCTS, check_product_inventory, business_route, installer_filename, target_businesses,
								 DISTRIBUTION_SCOPES, NATIVE_TARGETS, ARTIFACT_CATEGORIES, BUSINESS_ROUTES,
								 MOBILE_NATIVE_BUSINESSES, MOBILE_NATIVE_PREFIXES, WINDOWS_WHEEL_TAGS)
PREFIXES = {'VeroScriptRuntime': 'Runtime', 'VeroScriptSDK': 'CoreSDK', 'VeroScriptSymbols': 'CoreSDK',
			'VeroScriptInteropSDK': 'Interop', 'VeroScriptCpp': 'Cpp', 'VeroScriptCSharp': 'CSharp',
			'VeroScriptPython': 'Python', 'VeroScriptWebAssemblySDK': 'WebAssembly', ASMJIT_PACKAGE_NAME: 'Jit', BYTECODE_VM_PACKAGE_NAME: 'Vm',
			**{info['artifact']: business for business, info in IDE_PRODUCTS.items()},
			**{name: business for business, name in {**MODULE_PACKAGE_NAMES, **FOUNDATION_PACKAGE_NAMES}.items()},}
PREFIXES.update({'VeroScriptAndroidSDK': 'Android', 'VeroScriptIOSSDK': 'iOS', 'VeroScriptPythonMobile': 'Python'})
PREFIXES.update({prefix: business for business, prefix in MOBILE_NATIVE_PREFIXES.items()})


COLLECTION_SCHEMA = 1
INDEX_SCHEMA = 5
BUSINESS_SCHEMA = 3
COLLECTION_PRODUCT = 'VeroScript Release Set'
INDEX_PRODUCT = 'VeroScript Release Collection'
MANIFEST_PRODUCT = 'VeroScript Business Release'
_publication_owners = threading.local()
_verification_cache = threading.local()


def load_versions(repo: Path, path: Path | None = None, override: str | None = None,
				  businesses: list[str] | None = None) -> dict:
	path = path or module_root(repo) / 'Resources/Tools/Packaging/ReleaseVersions.json'
	config = json.loads(path.read_text(encoding='utf-8-sig'))
	if config.get('schemaVersion') != 1 or set(config.get('businesses', {})) != set(ALL_BUSINESSES):
		raise ValueError('ReleaseVersions must define every known business, without unknown keys')
	config['nativeVersion'] = version(config['nativeVersion'])
	for business, value in config['businesses'].items():
		config['businesses'][business] = version(value)
	selected = businesses or list(ALL_BUSINESSES)
	if len(selected) != len(set(selected)) or any(item not in ALL_BUSINESSES for item in selected):
		raise ValueError('Unknown or duplicate release business')
	if override:
		for business in selected:
			config['businesses'][business] = version(override)
	return config


def artifact_business(path: str) -> str:
	if path.startswith('Resources/'):
		return 'Resources'
	filename = Path(path).name
	if filename.startswith('veroscript-') and filename.endswith(('.whl', '.tar.gz')):
		return 'Python'
	if filename.startswith('VeroScript.CSharp') and filename.endswith(('.nupkg', '.snupkg')):
		return 'CSharp'
	if filename.startswith('VeroScriptDocs-'):
		return 'Resources'
	for business in BUSINESSES:
		if filename.startswith(f'VeroScript{business}Installer-'):
			return business
	# Prefer vs-font-backend over its shared vs-font prefix, independent of catalog order.
	for prefix in sorted(PREFIXES, key=len, reverse=True):
		if filename.startswith(prefix + '-'):
			return PREFIXES[prefix]
	raise ValueError(f'Unknown business artifact: {path}')


def distribution_identity(path: Path, *, entries: dict | None = None) -> tuple[str, str] | None:
	"""Read distribution versions from metadata, never from a renamed file."""
	from veroscript_release import archive_entries
	if path.name.startswith('veroscript-') and path.name.endswith(('.whl', '.tar.gz')):
		from veroscript_python_wheels import distribution_metadata
		if entries is None:
			entries = archive_entries(path, max_entry_bytes=1024 * 1024)
		message = BytesParser().parsebytes(distribution_metadata(path, entries))
		package_version = version(message['Version'])
		if message['Name'] != 'veroscript' or not path.name.startswith(f'veroscript-{package_version}-') and path.name != f'veroscript-{package_version}.tar.gz':
			raise ValueError(f'Python distribution filename/metadata mismatch: {path}')
		return 'Python', package_version
	if path.name.startswith('VeroScript.CSharp') and path.name.endswith(('.nupkg', '.snupkg')):
		entries = archive_entries(path, max_entry_bytes=1024 * 1024)
		nuspec = [data for name, (data, _) in entries.items() if name.endswith('.nuspec')]
		if len(nuspec) != 1:
			raise ValueError(f'Missing/ambiguous NuGet metadata: {path}')
		document = ET.fromstring(nuspec[0])
		values = {node.tag.split('}')[-1]: node.text for node in document.iter()}
		package_version = version(values['version'])
		if path.name != f"{values['id']}.{package_version}{path.suffix}":
			raise ValueError(f'NuGet distribution filename/metadata mismatch: {path}')
		return 'CSharp', package_version
	return None


def destination_name(source: str, business: str) -> str:
	if business == 'Resources' and not (source.startswith('Resources/VeroScriptDocs-') and source.endswith('.zip')):
		return source
	return artifact_category(source, business) + '/' + Path(source).name


def distribution_targets(path: Path, fallback: str) -> tuple[str, ...]:
	"""Route collected wheels by their platform tag, including a CI merge job."""
	if fallback not in RELEASE_TARGETS:
		raise ValueError('Distribution publication requires a native/mobile consuming target')
	if path.suffix != '.whl':
		manifest_name(fallback)
		return (fallback,)
	targets = set()
	windows = {tag: target for target, tag in WINDOWS_WHEEL_TAGS.items()}
	for tag in path.stem.rsplit('-', 1)[-1].split('.'):
		if tag in windows:
			targets.add(windows[tag])
		elif tag.startswith(('manylinux', 'musllinux', 'linux_')) and tag.endswith(('_x86_64', '_aarch64')):
			targets.add('linux-x64' if tag.endswith('_x86_64') else 'linux-arm64')
		elif tag.startswith('macosx_') and tag.endswith(('_x86_64', '_arm64', '_universal2')):
			if tag.endswith(('_x86_64', '_universal2')):
				targets.add('macos-x64')
			if tag.endswith(('_arm64', '_universal2')):
				targets.add('macos-arm64')
		elif tag == 'any':
			targets.add(fallback)
		else:
			raise ValueError(f'Unsupported distribution platform tag: {path.name}')
	for target in targets:
		manifest_name(target)
	return tuple(sorted(targets))


def manifest_name(target: str) -> str:
	from veroscript_products import DISTRIBUTION_SCOPES
	if target not in DISTRIBUTION_SCOPES:
		raise ValueError(f'Unknown target: {target}')
	return 'release-manifest.json'


def foundation_dependency_closure(manifests, read_dependency):
	"""Validate and retain indirect pins when a backend uses a historical SDK."""
	pending = [manifest for manifest in manifests if manifest['business'] in FOUNDATION_BUSINESSES]
	found = {}
	while pending:
		manifest = pending.pop()
		key = (manifest['business'], manifest['version'], manifest['buildId'])
		if key in found:
			continue
		found[key] = manifest
		for dependency, pin in manifest.get('dependencies', {}).items():
			owned = read_dependency(dependency, pin['version'])
			if owned['business'] != dependency or owned['buildId'] != pin['buildId']:
				raise ValueError('Foundation dependency build mismatch: ' + manifest['business'] + '/' + dependency)
			if dependency in FOUNDATION_BUSINESSES:
				pending.append(owned)
			else:
				found[(dependency, owned['version'], owned['buildId'])] = owned
	return list(found.values())


def move_directory(source: Path, destination: Path) -> None:
	for attempt in range(7):
		try:
			source.rename(destination)
			return
		except PermissionError:
			if os.name != 'nt' or attempt == 6:
				raise
			time.sleep(min(0.2 * 2 ** attempt, 2))


def guard_tree(path: Path) -> None:
	for current in (path, *path.parents):
		if linked_path(current):
			# Darwin's root-owned system aliases are ancestors of ordinary
			# tempfile paths. Links within a publication tree remain forbidden.
			expected = {'/tmp': '/private/tmp', '/var': '/private/var', '/etc': '/private/etc'}
			trusted = False
			if sys.platform == 'darwin' and current != path and current.as_posix() in expected:
				try:
					trusted = current.lstat().st_uid == 0 and current.resolve(strict=True) == Path(expected[current.as_posix()])
				except OSError:
					pass
			if not trusted:
				raise ValueError(f'Linked publication path: {current}')
		if current.exists() and not current.is_dir():
			raise ValueError(f'Publication has a file ancestor: {current}')
	if path.exists():
		for current in path.rglob('*'):
			if linked_path(current):
				raise ValueError(f'Linked publication entry: {current}')


def collection_root(repo: Path, root: Path) -> Path:
	repo = repo.resolve()
	root = Path(os.path.abspath(root))
	if not root.is_relative_to(repo) or root == repo:
		raise ValueError('Business collection must be below the repository')
	allowed = (module_root(repo) / 'Release', repo / '.tmp', repo / 'dist')
	if not (root == allowed[0] or any(root != parent and root.is_relative_to(parent) for parent in allowed[1:])):
		raise ValueError(f'Business collection must be {allowed[0]} or a dedicated .tmp/dist directory')
	for parent in root.relative_to(repo).parents:
		ancestor = repo / parent
		if any((ancestor / marker).exists() for marker in ('release-index.json', 'release-manifest.json', '.publication-lock')):
			raise ValueError(f'Business collection cannot be nested inside another release output: {ancestor}')
	if (root / '.publication-lock').exists() and str(root) not in getattr(_publication_owners, 'paths', set()):
		raise FileExistsError('Another publication owns the collection lock')
	if (root / 'release-index.json').is_file():
		_read_indexes(root)
	else:
		require_business_layout(root)
	return root





def verification_operation(function):
	@functools.wraps(function)
	def invoke(*args, **kwargs):
		outer = getattr(_verification_cache, 'records', None)
		if outer is None:
			_verification_cache.records = {}
		try:
			return function(*args, **kwargs)
		finally:
			if outer is None:
				_verification_cache.records = None
	return invoke


def cache_business_verification(function):
	@functools.wraps(function)
	def invoke(root, target):
		cache = getattr(_verification_cache, 'records', None)
		if cache is None:
			return function(root, target)
		guard_tree(root)
		data = (root / manifest_name(target)).read_bytes()
		manifest = json.loads(data)
		identities = []
		for item in manifest.get('artifacts', []):
			file = root / portable_path(item['path'])
			info = file.stat()
			identities.append((item['path'], info.st_size, info.st_mtime_ns, info.st_ctime_ns, info.st_mode, info.st_ino))
		key = (str(root), target, hashlib.sha256(data).digest(), tuple(identities))
		if key not in cache:
			cache[key] = function(root, target)
		return cache[key]
	return invoke


def business_root(root: Path, business: str, target: str) -> Path:
	manifest_name(target)
	return root / business_route(business) / target


def collection_index(root: Path, target: str) -> Path:
	manifest_name(target)
	return root / 'indexes' / (target + '.json')


def acquire_collection_lock(repo: Path, root: Path, *, create_root: bool = False) -> tuple[Path, Path]:
	key = hashlib.sha256(os.fsencode(os.path.normcase(str(root)))).hexdigest()
	stable = scratch_directory(repo) / f'veroscript-collection-lock-{key}'
	descriptor = os.open(stable, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
	os.close(descriptor)
	marker = root / '.publication-lock'
	try:
		if create_root:
			root.mkdir(parents=True, exist_ok=True)
		descriptor = os.open(marker, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
		os.close(descriptor)
	except BaseException:
		stable.unlink()
		raise
	_publication_owners.paths = getattr(_publication_owners, 'paths', set()) | {str(root)}
	return marker, stable


def release_collection_lock(lock: tuple[Path, Path]) -> None:
	marker, stable = lock
	_publication_owners.paths = getattr(_publication_owners, 'paths', set()) - {str(marker.parent)}
	try:
		marker.unlink(missing_ok=True)
	finally:
		stable.unlink()


def portable_path(name: str) -> str:
	canonical = safe_member(name)
	reserved = r'(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?'
	if canonical != name or any(part.endswith((' ', '.')) or re.fullmatch(reserved, part) for part in Path(name).parts):
		raise ValueError('Non-portable release path: ' + name)
	return name


def artifact_category(source: str, business: str) -> str:
	name = Path(source).name
	if business == 'Resources' and source.startswith('Resources/') and not name.startswith('VeroScriptDocs-'):
		return 'Resources'
	if source.startswith('installer/') or 'Installer-' in name:
		if business not in INSTALLER_BUSINESSES:
			raise ValueError('Modules/mobile/editor businesses do not provide standalone installers')
		return 'installers'
	if source.startswith('symbols/') or name.endswith('.snupkg'):
		return 'symbols'
	if name.endswith(('.vspkg', '.vsix', '.whl', '.nupkg', '.aar')) or business == 'Python' and name.startswith('veroscript-') or business in IDE_PRODUCTS:
		return 'packages'
	if any(name.startswith(prefix + '-') for prefix in MOBILE_NATIVE_PREFIXES.values()):
		return 'sdks'
	return 'archives' if business in ('Runtime', 'Jit', 'Vm', 'Resources') else 'sdks'


def canonical_source(source: str, business: str) -> str:
	name = Path(source).name
	if business == 'CSharp' and name.endswith(('.nupkg', '.snupkg')):
		return 'distributions/csharp/' + name
	if business == 'Python' and name.startswith('veroscript-'):
		return 'distributions/python/' + name
	return source


def audit_source(item: dict, business: str, target: str) -> str:
	if target in MOBILE_TARGETS and business == 'CSharp' and item['path'].endswith(('.nupkg', '.snupkg')):
		return 'mobile/' + Path(item['path']).name
	return item['sourcePath']


def artifact_scope(source: str, business: str, target: str) -> str:
	name = Path(source).name
	if business == 'Resources' or business in IDE_PRODUCTS or business == 'CSharp' and name.endswith(('.nupkg', '.snupkg')):
		return 'any'
	if business == 'Python' and name.startswith('veroscript-'):
		if name.endswith('.tar.gz') or name.endswith('-any.whl') or name.endswith('_universal2.whl'):
			return 'any'
	if business == 'WebAssembly' and artifact_category(source, business) == 'sdks':
		return 'wasm32'
	return target


def artifact_support(path: Path, source: str, business: str, scope: str) -> tuple[list[str], list[str]]:
	if scope not in ('any', 'wasm32'):
		return [scope], [scope]
	if scope == 'wasm32':
		return ['wasm32'], sorted(NATIVE_TARGETS)
	if business == 'Resources':
		return sorted(RELEASE_TARGETS), sorted(RELEASE_TARGETS)
	if business == 'CSharp':
		from veroscript_release import archive_entries
		entries = archive_entries(path, max_entry_bytes=4096)
		frameworks = {name.split('/')[1] for name in entries if name.startswith('lib/') and len(name.split('/')) > 2}
		# DotnetTool packages (and their symbol packages) use tools/<tfm>/<rid>/,
		# rather than the lib/<tfm>/ layout of ordinary managed libraries.
		nuspec = [data for name, (data, _) in entries.items() if name.endswith('.nuspec')]
		if len(nuspec) == 1:
			document = ET.fromstring(nuspec[0])
			package_types = {node.get('name') for node in document.iter() if node.tag.split('}')[-1] == 'packageType'}
			if package_types & {'DotnetTool', 'SymbolsPackage'}:
				frameworks.update(name.split('/')[1] for name in entries
					if name.startswith('tools/') and len(name.split('/')) > 3 and name.split('/')[2] == 'any')
		targets = set(NATIVE_TARGETS) if any('android' not in name and 'ios' not in name for name in frameworks) else set()
		for mobile in ('android', 'ios'):
			if any(mobile in framework for framework in frameworks):
				targets.add(mobile)
		if not targets:
			raise ValueError('NuGet package has no declared supported framework: ' + path.name)
		return sorted(targets), sorted(targets)
	if path.suffix == '.whl' and not path.name.endswith('-any.whl'):
		targets = list(distribution_targets(path, 'macos-x64'))
		return targets, targets
	return sorted(NATIVE_TARGETS), sorted(NATIVE_TARGETS)


def require_business_layout(root: Path) -> None:
	guard_tree(root)
	if not root.exists():
		return
	if any(root.glob('release-index-*.json')) or any((root / target).exists() for target in RELEASE_TARGETS) or (root / 'Utils').exists():
		raise ValueError('Unsupported old release format; clean owned outputs and rebuild from source')
	entry = root / 'release-index.json'
	if not entry.is_file():
		if any(path.name != '.publication-lock' for path in root.iterdir()):
			raise ValueError('unmanaged/old collection without release-index.json; rebuild from source')
		return
	metadata = json.loads(entry.read_text(encoding='utf-8'))
	if type(metadata.get('schemaVersion')) is not int or metadata['schemaVersion'] != COLLECTION_SCHEMA or metadata.get('product') != COLLECTION_PRODUCT:
		raise ValueError('Unsupported collection format; rebuild from source')


@cache_business_verification
def read_business(root: Path, target: str) -> dict:
	guard_tree(root)
	file = root / manifest_name(target)
	manifest = json.loads(file.read_text(encoding='utf-8'))
	business = manifest.get('business')
	if (type(manifest.get('schemaVersion')) is not int or manifest['schemaVersion'] != BUSINESS_SCHEMA
			or manifest.get('product') != MANIFEST_PRODUCT or business not in ALL_BUSINESSES
			or manifest.get('target') != target or root.name != version(manifest['version'])
			or tuple(root.parts[-(len(Path(business_route(business)).parts) + 2):-1]) != (*Path(business_route(business)).parts, target)):
		raise ValueError(f'Invalid/old business release identity: {file}')
	if not re.fullmatch('[0-9a-f]{64}', manifest.get('buildId', '')) or manifest.get('payloadPolicyVersion') != 1:
		raise ValueError('Invalid business build/payload identity')
	if target not in ('any', 'wasm32'):
		version(manifest['nativeVersion'])
	elif 'nativeVersion' in manifest:
		raise ValueError('A portable manifest cannot pin the publishing host native version')
	for key in ('supportedTargets', 'consumerTargets'):
		values = manifest.get(key)
		if not isinstance(values, list) or not values or values != sorted(set(values)) or not set(values).issubset(DISTRIBUTION_SCOPES):
			raise ValueError('Invalid business ' + key)
	if target not in ('any', 'wasm32') and (manifest['supportedTargets'] != [target] or manifest['consumerTargets'] != [target]
			or business not in target_businesses(target, optional=True)):
		raise ValueError('Business is not supported on its recorded target')
	if target == 'wasm32' and (business != 'WebAssembly' or manifest['supportedTargets'] != ['wasm32']):
		raise ValueError('Only the portable WebAssembly SDK belongs to wasm32')
	if 'pythonWheelVersions' in manifest:
		from veroscript_python_wheels import validate_versions
		if business != 'Python':
			raise ValueError('A wheel matrix belongs to Python only')
		validate_versions(manifest['pythonWheelVersions'])
	if target in MOBILE_TARGETS and business in MOBILE_NATIVE_BUSINESSES:
		if manifest.get('mobileNativeSdkPolicyVersion') != 1 or manifest.get('dependencies', {}) or 'modulePackagePolicyVersion' in manifest:
			raise ValueError('Mobile native SDKs require their self-contained static dependency contract')
	elif business == 'Cpp':
		pin = manifest.get('dependencies', {}).get('CoreSDK', {})
		if pin.get('buildId') != manifest['buildId']:
			raise ValueError('Invalid C++ dependency build identity')
		version(pin['version'])
	for dependency, pin in manifest.get('dependencies', {}).items():
		if dependency not in ALL_BUSINESSES or dependency == business or not re.fullmatch('[0-9a-f]{64}', pin.get('buildId', '')):
			raise ValueError('Invalid business dependency')
		version(pin['version'])
	if IDE_PRODUCTS.get(business, {}).get('base'):
		from veroscript_ide import verify_ide_dependencies
		verify_ide_dependencies({business: manifest.get('dependencies')}, {business}, {}, {})
	expected, sources, aliases, checksums = set(), set(), set(), []
	if not manifest.get('artifacts'):
		raise ValueError('Business release cannot have an empty artifact inventory')
	for item in manifest['artifacts']:
		name, source = portable_path(item['path']), portable_path(item['sourcePath'])
		category = artifact_category(source, business)
		if (name != destination_name(source, business) or item.get('role') != category
				or name in expected or name.casefold() in aliases or source in sources or artifact_business(source) != business
				or artifact_scope(source, business, target if target not in ('any', 'wasm32') else 'native') != target):
			raise ValueError('Invalid business artifact/role/scope: ' + name)
		if category == 'installers' and Path(name).name != installer_filename(business, manifest['version'], target):
			raise ValueError('Installer download filename differs from the target contract: ' + name)
		expected.add(name)
		sources.add(source)
		aliases.add(name.casefold())
		path = root / name
		if type(item.get('size')) is not int or item['size'] < 0 or not re.fullmatch('[0-9a-f]{64}', item.get('sha256', '')) or not path.is_file() or path.stat().st_size != item['size'] or digest(path) != item['sha256']:
			raise ValueError('Missing/stale business artifact: ' + str(path))
		if item.get('mode') not in (0o644, 0o755) or (os.name != 'nt' and item['mode'] != (path.stat().st_mode & 0o777)):
			raise ValueError('Business artifact permissions mismatch: ' + name)
		if business in ('CSharp', 'Python') and (name.endswith(('.whl', '.nupkg', '.snupkg')) or Path(name).name.startswith('veroscript-')):
			if distribution_identity(path) != (business, manifest['version']):
				raise ValueError('Ecosystem metadata/business version mismatch')
		if category != 'symbols' and business in (*FEATURE_MODULE_BUSINESSES, *FOUNDATION_BUSINESSES) and target not in MOBILE_TARGETS:
			from veroscript_modules import read_module_metadata
			metadata = read_module_metadata(path)
			pins = {key: pin['version'] for key, pin in manifest.get('dependencies', {}).items()}
			if metadata['dependencies'] != pins or manifest.get('modulePackagePolicyVersion') == 1 and metadata['payloadId'] != manifest['buildId']:
				raise ValueError('Module package/business dependency identity mismatch')
			if business in FOUNDATION_BUSINESSES and (metadata['payloadId'] != manifest['buildId'] or metadata['dependencyBuildIds'] != {key: pin['buildId'] for key, pin in manifest.get('dependencies', {}).items()}):
				raise ValueError('Foundation package/business build mismatch')
			if business in FEATURE_MODULE_BUSINESSES and business != 'Json' and metadata.get('networkImplementationId') != manifest.get('networkImplementationId'):
				raise ValueError('Module Network foundation identity mismatch')
		checksums.append(f"{item['sha256']}  {name}")
	actual_support, actual_consumers = set(), set()
	for item in manifest['artifacts']:
		supported, consumers = artifact_support(root / item['path'], item['sourcePath'], business, target)
		actual_support.update(supported)
		actual_consumers.update(consumers)
	if manifest['supportedTargets'] != sorted(actual_support) or manifest['consumerTargets'] != sorted(actual_consumers):
		raise ValueError('Business support declaration differs from its actual artifacts')
	if manifest.get('modulePackagePolicyVersion') == 1 and not any(name.endswith('.vspkg') for name in expected):
		raise ValueError('Installable module business is missing its package')
	if (root / 'checksums.txt').read_text(encoding='utf-8').splitlines() != checksums:
		raise ValueError('Business checksum mismatch: ' + str(root))
	if business in IDE_PRODUCTS:
		if len(expected) != 1 or manifest['artifacts'][0]['sha256'] != manifest['buildId']:
			raise ValueError('IDE business artifact/build identity mismatch')
	return manifest


def verify_business_inventory(root: Path) -> None:
	manifest = read_business(root, root.parent.name)
	owned = {item['path'] for item in manifest['artifacts']} | {'release-manifest.json', 'checksums.txt'}
	actual = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_file()}
	allowed_dirs = {parent.as_posix() for name in owned for parent in Path(name).parents if parent != Path('.')}
	actual_dirs = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_dir()}
	if actual != owned or not actual_dirs.issubset(allowed_dirs):
		raise ValueError('Business directory contains unowned files or directories: ' + str(root))


def verify_core_dependency(root: Path, target: str, expected: dict) -> dict:
	from veroscript_release import archive_member_digest
	core = read_business(root, target)
	verify_business_inventory(root)
	if expected != {'version': core['version'], 'buildId': core['buildId']}:
		raise ValueError('C++/CoreSDK version or build mismatch')
	archives = [item for item in core['artifacts'] if item['sourcePath'].startswith('sdk/')]
	if len(archives) != 1:
		raise ValueError('C++ dependency requires the published CoreSDK archive')
	archive = root / archives[0]['path']
	prefix = archive.name.removesuffix('.tar.gz').removesuffix('.zip')
	library = 'lib/VeroScript.lib' if target.startswith('win-') else 'lib/libVeroScript.a'
	if archive_member_digest(archive, prefix + '/' + library) != expected['buildId']:
		raise ValueError('C++ dependency Core library build identity mismatch')
	return core


def _read_indexes(root: Path) -> tuple[dict, dict]:
	require_business_layout(root)
	if (root / '.publication-lock').exists() and str(root) not in getattr(_publication_owners, 'paths', set()):
		raise ValueError('Publication in progress; retry collection verification')
	metadata = json.loads((root / 'release-index.json').read_text(encoding='utf-8'))
	if not re.fullmatch('[0-9a-f]{32}', metadata.get('generation', '')) or not isinstance(metadata.get('indexes'), dict):
		raise ValueError('Invalid release collection generation/index inventory')
	indexes = {}
	for target, ref in metadata['indexes'].items():
		file = collection_index(root, target)
		if ref.get('path') != file.relative_to(root).as_posix() or digest(file) != ref.get('sha256'):
			raise ValueError('Collection target index path/hash mismatch')
		index = json.loads(file.read_text(encoding='utf-8'))
		if type(index.get('schemaVersion')) is not int or index['schemaVersion'] != INDEX_SCHEMA or index.get('product') != INDEX_PRODUCT or index.get('target') != target or not isinstance(index.get('businesses'), dict):
			raise ValueError('Invalid/old release target index')
		required = list(target_businesses(target)) if target in RELEASE_TARGETS else []
		if index.get('requiredBusinesses') != required:
			raise ValueError(f'Target completeness uses a different product registry: {target}. Use a clean, dedicated release output and rebuild from source.')
		if 'qualificationStatus' in index or 'qualified' in index:
			from veroscript_qualification import target_qualification
			expected_qualification = target_qualification(index['businesses'], index.get('complete', False))
			if any(index.get(key) != value or type(index.get(key)) is not type(value) for key, value in expected_qualification.items()):
				raise ValueError('Target qualification rollup differs from its business records')
		indexes[target] = index
	actual = {file.name for file in (root / 'indexes').iterdir()}
	if actual != {target + '.json' for target in indexes}:
		raise ValueError('Unowned collection target index')
	return metadata, indexes


def manifest_references(root: Path, index: dict, overlay: dict | None = None) -> dict:
	result = {}
	target = index['target']
	for business, entry in index['businesses'].items():
		from veroscript_qualification import verify_qualification
		verify_qualification(entry.get('qualification'), entry.get('manifests', []))
		if business not in ALL_BUSINESSES or not isinstance(entry.get('manifests'), list) or not entry['manifests']:
			raise ValueError('Unknown business or empty manifest references')
		seen = set()
		for ref in entry['manifests']:
			scope, release_version = ref['target'], version(ref['version'])
			logical = business_root(root, business, scope) / release_version
			leaf = (overlay or {}).get(logical, logical)
			file = leaf / manifest_name(scope)
			if scope in seen or ref.get('manifest') != (logical / manifest_name(scope)).relative_to(root).as_posix():
				raise ValueError('Invalid/duplicate business index path')
			seen.add(scope)
			manifest = read_business(leaf, scope)
			verify_business_inventory(leaf)
			if digest(file) != ref.get('sha256') or manifest['business'] != business:
				raise ValueError('Business manifest/index hash mismatch')
			if target not in ('any', 'wasm32') and target not in manifest['consumerTargets']:
				raise ValueError('Shared manifest does not support this consuming target')
			if target in ('any', 'wasm32') and scope != target:
				raise ValueError('Shared indexes must reference their own scope')
			if target not in ('any', 'wasm32') and business not in target_businesses(target, optional=True):
				raise ValueError('Business is not part of the selected target')
			result[(business, scope, release_version)] = (leaf, manifest)
	return result


def composed_businesses(root: Path, index: dict) -> dict:
	references = manifest_references(root, index)
	result = {}
	for business, entry in index['businesses'].items():
		parts = [references[(business, ref['target'], ref['version'])] for ref in entry['manifests']]
		primary = next((manifest for leaf, manifest in parts if manifest['target'] == index['target']), parts[0][1])
		merged = {**primary, 'artifacts': [], 'partial': entry.get('partial', False) or any(m.get('partial', False) for leaf, m in parts)}
		if 'nativeVersion' in entry:
			if business != 'CSharp' or index['target'] not in RELEASE_TARGETS or primary.get('nativeVersion', entry['nativeVersion']) != entry['nativeVersion']:
				raise ValueError('Invalid consuming native-version context')
			merged['nativeVersion'] = version(entry['nativeVersion'])
		for leaf, manifest in parts:
			if manifest['version'] != entry['version']:
				raise ValueError('A composed business must retain matching ecosystem/product versions')
			merged['artifacts'].extend({**item, '_leaf': str(leaf)} for item in manifest['artifacts'])
		result[business] = merged
	return result


def _dependency_closure(root: Path, records: dict, overlay: dict | None = None) -> dict:
	retained, visiting = {}, set()
	def visit(leaf, manifest):
		key = (manifest['business'], manifest['target'], manifest['version'])
		if key in visiting:
			raise ValueError('Business dependency cycle')
		if key in retained:
			return
		visiting.add(key)
		for business, pin in manifest.get('dependencies', {}).items():
			scope = 'any' if business in IDE_PRODUCTS else manifest['target']
			dependency = business_root(root, business, scope) / pin['version']
			dependency = (overlay or {}).get(dependency, dependency)
			if not (dependency / manifest_name(scope)).is_file():
				label = 'C++/CoreSDK' if manifest['business'] == 'Cpp' else 'Business ' + business
				raise ValueError(label + ' dependency version/build is missing')
			actual = read_business(dependency, scope)
			verify_business_inventory(dependency)
			if actual['buildId'] != pin['buildId']:
				raise ValueError('Business dependency version/build mismatch: ' + business)
			if business == 'Network' and manifest.get('networkImplementationId') != actual.get('networkImplementationId'):
				raise ValueError('Module Network foundation implementation mismatch')
			if manifest['business'] == 'Cpp' and business == 'CoreSDK' and scope not in MOBILE_TARGETS:
				verify_core_dependency(dependency, scope, pin)
			visit(dependency, actual)
		visiting.remove(key)
		retained[key] = (leaf, manifest)
	for leaf, manifest in records.values():
		visit(leaf, manifest)
	return retained


def _collection_inventory(root: Path) -> dict:
	allowed_files = {'release-index.json', '.publication-lock'}
	allowed_dirs = {'indexes'}
	for route in BUSINESS_ROUTES.values():
		allowed_dirs.update(parent.as_posix() for parent in (Path(route), *Path(route).parents) if parent != Path('.'))
	leaves = {}
	for business in ALL_BUSINESSES:
		base = root / business_route(business)
		if not base.is_dir():
			continue
		for platform in base.iterdir():
			if not platform.is_dir() or platform.name not in DISTRIBUTION_SCOPES:
				raise ValueError('Unowned business platform entry: ' + str(platform))
			allowed_dirs.add(platform.relative_to(root).as_posix())
			for leaf in platform.iterdir():
				if not leaf.is_dir():
					raise ValueError('Unowned business version entry')
				version(leaf.name)
				manifest = read_business(leaf, platform.name)
				verify_business_inventory(leaf)
				leaves[(business, platform.name, leaf.name)] = (leaf, manifest)
				prefix = leaf.relative_to(root)
				allowed_dirs.add(prefix.as_posix())
				for item in manifest['artifacts']:
					allowed_files.add((prefix / item['path']).as_posix())
				allowed_files.update((prefix / name).as_posix() for name in ('release-manifest.json', 'checksums.txt'))
				allowed_dirs.update(path.relative_to(root).as_posix() for path in leaf.rglob('*') if path.is_dir())
	allowed_files.update(file.relative_to(root).as_posix() for file in (root / 'indexes').glob('*.json'))
	actual_files = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_file()}
	actual_dirs = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_dir()}
	if actual_files != allowed_files - ({'.publication-lock'} if not (root / '.publication-lock').exists() else set()) or not actual_dirs.issubset(allowed_dirs):
		raise ValueError('Collection contains unowned files or directories')
	identities = {}
	for leaf, manifest in leaves.values():
		for item in manifest['artifacts']:
			if item['path'].endswith(('.nupkg', '.snupkg', '.whl')) or (manifest['business'] == 'Python'
					and Path(item['path']).name.startswith('veroscript-') and item['path'].endswith('.tar.gz')):
				name = Path(item['path']).name
				if name in identities and identities[name] != item['sha256']:
					raise ValueError('Conflicting ecosystem package identity: ' + name)
				identities[name] = item['sha256']
	return leaves


def materialize_businesses(root: Path, index: dict, stage: Path) -> dict:
	from veroscript_release import write_manifest
	manifests = composed_businesses(root, index)
	for business, manifest in manifests.items():
		for item in manifest['artifacts']:
			output = stage / portable_path(audit_source(item, business, index['target']))
			if output.exists():
				raise ValueError('Duplicate composed candidate path')
			output.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(Path(item['_leaf']) / item['path'], output)
	identity = manifests.get('CoreSDK') or next(iter(manifests.values()))
	native_version = identity.get('nativeVersion', identity['version'])
	write_manifest(stage, identity['version'], index['target'], identity['buildId'],
		business_versions={business: manifest['version'] for business, manifest in manifests.items()}, native_version=native_version)
	file = stage / 'release-manifest.json'
	audit = json.loads(file.read_text(encoding='utf-8'))
	audit.update(selectedBusinesses=list(manifests), partialBusinesses=[b for b, m in manifests.items() if m.get('partial')],
		businessBuildIds={b: m['buildId'] for b, m in manifests.items()},
		functionalPayloadBusinesses=list(manifests),
		installableModuleBusinesses=[b for b, m in manifests.items() if m.get('modulePackagePolicyVersion') == 1],
		ideDependencies={b: m['dependencies'] for b, m in manifests.items() if IDE_PRODUCTS.get(b, {}).get('base')})
	if index['target'] in MOBILE_TARGETS:
		audit['businessNativeVersions'] = {b: m.get('nativeVersion', native_version) for b, m in manifests.items()}
	if manifests.get('Python', {}).get('pythonWheelVersions'):
		audit['pythonWheelVersions'] = manifests['Python']['pythonWheelVersions']
	file.write_text(json.dumps(audit, indent=2) + '\n', encoding='utf-8')
	return audit


@verification_operation
def verify_collection(root: Path, target: str, repo: Path | None = None, *, require_complete: bool = False) -> dict:
	metadata, indexes = _read_indexes(root)
	if target not in indexes:
		raise ValueError('Target has no published index: ' + target)
	index = indexes[target]
	_collection_inventory(root)
	records = {}
	for scope, value in indexes.items():
		records.update(manifest_references(root, value))
		expected_state = _index_state(root, scope, value['businesses'])
		for field in ('requiredBusinesses', 'missingBusinesses', 'partialBusinesses', 'complete'):
			if value.get(field) != expected_state[field] or type(value.get(field)) is not type(expected_state[field]):
				raise ValueError('Target completeness differs from its actual business inventory: ' + scope)
	_dependency_closure(root, records)
	manifests = composed_businesses(root, index)
	if require_complete:
		if target not in RELEASE_TARGETS or not set(target_businesses(target)).issubset(manifests):
			raise ValueError('Incomplete publication: missing businesses for the selected target')
		if any(m.get('partial') for m in manifests.values()):
			raise ValueError('Incomplete publication: partial businesses')
	if target in RELEASE_TARGETS and manifests:
		from veroscript_release import verify_release
		with tempfile.TemporaryDirectory(prefix='veroscript-business-audit-', dir=scratch_directory(repo) if repo else None) as temporary:
			stage = Path(temporary)
			materialize_businesses(root, index, stage)
			verify_release(stage, repo, businesses=set(manifests), partial_businesses={b for b, m in manifests.items() if m.get('partial')},
				allow_historical_module_dependencies=True, allow_historical_ide_dependencies=True)
	if json.loads((root / 'release-index.json').read_text(encoding='utf-8')) != metadata:
		raise ValueError('Publication generation changed; retry verification')
	return index


def obsolete_business_versions(root: Path, target: str, index: dict) -> list[Path]:
	_, indexes = _read_indexes(root)
	records = {key: record for value in indexes.values() for key, record in manifest_references(root, value).items()}
	retained = _dependency_closure(root, records)
	leaves = _collection_inventory(root)
	return sorted(leaf for key, (leaf, manifest) in leaves.items() if key not in retained)


def discard_obsolete_versions(root: Path, target: str, index: dict) -> list[Path]:
	obsolete = obsolete_business_versions(root, target, index)
	for leaf in obsolete:
		guard_tree(leaf)
		if not leaf.resolve().is_relative_to(root.resolve()) or leaf == root:
			raise ValueError('Obsolete release path escapes its collection')
		shutil.rmtree(leaf)
	return obsolete


def prune_versions(repo: Path, root: Path, target: str, *, dry_run: bool = False) -> list[Path]:
	root = collection_root(repo, root)
	require_business_layout(root)
	lock = acquire_collection_lock(repo, root)
	try:
		index = verify_collection(root, target)
		return obsolete_business_versions(root, target, index) if dry_run else discard_obsolete_versions(root, target, index)
	finally:
		release_collection_lock(lock)


def publish_businesses(repo: Path, candidate: Path, root: Path, selected: list[str] | None = None) -> None:
	root = collection_root(repo, root)
	lock = acquire_collection_lock(repo, root, create_root=True)
	try:
		_publish_locked(repo, candidate, root, selected)
	finally:
		release_collection_lock(lock)


def _business_metadata(identity: dict, business: str, scope: str, artifacts: list, repo: Path, root: Path, collection: Path) -> dict:
	target = identity['target']
	versions = identity['businessVersions']
	manifest = {'schemaVersion': BUSINESS_SCHEMA, 'product': MANIFEST_PRODUCT, 'business': business,
		'version': versions[business], 'target': scope, 'payloadPolicyVersion': 1,
		'buildId': identity.get('businessBuildIds', {}).get(business, identity['buildId']),
		'nativeVersion': identity.get('businessNativeVersions', {}).get(business, identity.get('nativeVersion', identity['version'])),
		'artifacts': artifacts}
	if scope in ('any', 'wasm32'):
		values = sorted(item['sha256'] for item in artifacts)
		manifest['buildId'] = values[0] if len(values) == 1 else hashlib.sha256(''.join(values).encode()).hexdigest()
		manifest.pop('nativeVersion')
	elif business in identity.get('partialBusinesses', []):
		manifest['partial'] = True
	if scope not in ('any', 'wasm32') and identity.get('buildEnvironment'):
		manifest['buildEnvironment'] = identity['buildEnvironment']
	if business in identity.get('installableModuleBusinesses', []):
		manifest['modulePackagePolicyVersion'] = 1
	if business == 'Python' and scope in NATIVE_TARGETS:
		from veroscript_python_wheels import supported_versions
		if (module_root(repo) / 'Extensions/Language/Python/pyproject.toml').is_file():
			manifest['pythonWheelVersions'] = list(supported_versions(repo))
		elif identity.get('pythonWheelVersions'):
			manifest['pythonWheelVersions'] = identity['pythonWheelVersions']
	if scope in MOBILE_TARGETS and business in MOBILE_NATIVE_BUSINESSES:
		manifest['mobileNativeSdkPolicyVersion'] = 1
		manifest['dependencies'] = {}
	elif business == 'Cpp':
		manifest['dependencies'] = {'CoreSDK': {'version': versions['CoreSDK'], 'buildId': identity['buildId']}}
	if IDE_PRODUCTS.get(business, {}).get('base'):
		manifest['dependencies'] = identity['ideDependencies'][business]
	if business in (*FEATURE_MODULE_BUSINESSES, *FOUNDATION_BUSINESSES) and scope not in MOBILE_TARGETS:
		from veroscript_modules import read_module_metadata
		metadata = read_module_metadata(root / artifacts[0]['path'])
		if business in FOUNDATION_BUSINESSES:
			manifest['buildId'] = metadata['payloadId']
			manifest['dependencies'] = {name: {'version': v, 'buildId': metadata['dependencyBuildIds'][name]} for name, v in metadata['dependencies'].items()}
		else:
			if business != 'Json':
				manifest['networkImplementationId'] = metadata['networkImplementationId']
			pins = {}
			for name, v in metadata.get('dependencies', {}).items():
				if v != versions.get(name):
					dependency_id = read_business(business_root(collection, name, scope) / v, scope)['buildId']
				else:
					dependency_id = identity.get('businessBuildIds', {}).get(name, identity['buildId'])
				pins[name] = {'version': v, 'buildId': dependency_id}
			manifest['dependencies'] = pins
	return manifest


def _index_state(root: Path, target: str, businesses: dict) -> dict:
	if target in RELEASE_TARGETS:
		required = list(target_businesses(target))
		missing = sorted(set(required) - set(businesses))
		partial = sorted(business for business, entry in businesses.items() if entry.get('partial') or any(read_business(root / Path(ref['manifest']).parent, ref['target']).get('partial') for ref in entry['manifests']))
	else:
		required, missing, partial = [], [], []
	complete = target in RELEASE_TARGETS and not missing and not partial
	from veroscript_qualification import target_qualification
	return {'schemaVersion': INDEX_SCHEMA, 'product': INDEX_PRODUCT, 'target': target, 'businesses': businesses,
		'requiredBusinesses': required, 'missingBusinesses': missing, 'partialBusinesses': partial,
		'complete': complete, **target_qualification(businesses, complete)}


@verification_operation
def _publish_locked(repo: Path, candidate: Path, root: Path, selected: list[str] | None) -> None:
	from veroscript_release import verify_release
	identity = json.loads((candidate / 'release-manifest.json').read_text(encoding='utf-8'))
	selection = identity.get('selectedBusinesses')
	identity = verify_release(candidate, repo, businesses=set(selection) if selection else None, partial_businesses=set(identity.get('partialBusinesses', [])))
	selected = selected or selection or list(BUSINESSES)
	target = identity['target']
	if not selected or len(selected) != len(set(selected)) or not set(selected).issubset(target_businesses(target, optional=True)) or selection is not None and not set(selected).issubset(selection):
		raise ValueError('Unknown/duplicate businesses or selection outside the candidate target')
	identity = {**identity, 'businessVersions': identity.get('businessVersions') or {b: identity['version'] for b in ALL_BUSINESSES}}
	require_business_layout(root)
	if (root / 'release-index.json').exists():
		metadata, indexes = _read_indexes(root)
		_collection_inventory(root)
		for existing in indexes:
			verify_collection(root, existing)
	else:
		indexes = {}
	with tempfile.TemporaryDirectory(prefix='veroscript-business-stage-', dir=scratch_directory(repo)) as temporary:
		stage = Path(temporary)
		groups, replacements = {}, []
		for item in identity['artifacts']:
			business = artifact_business(item['path'])
			if business in selected:
				scope = artifact_scope(item['path'], business, target)
				groups.setdefault((business, scope), []).append(item)
		if set(selected) != {business for business, scope in groups}:
			raise ValueError('No candidate artifacts for selected business')
		refs_by_business = {}
		for (business, scope), items in sorted(groups.items()):
			v = version(identity['businessVersions'][business])
			prepared = business_root(stage, business, scope) / v
			prepared.mkdir(parents=True)
			artifacts, support, consumers = [], set(), set()
			for item in sorted(items, key=lambda value: value['path']):
				source = canonical_source(item['path'], business)
				name = destination_name(source, business)
				output = prepared / name
				if output.exists():
					raise ValueError('Duplicate classified business artifact')
				output.parent.mkdir(parents=True, exist_ok=True)
				shutil.copy2(candidate / item['path'], output)
				actual, hosts = artifact_support(output, source, business, scope)
				support.update(actual)
				consumers.update(hosts)
				mode = 0o755 if source.startswith('Resources/') and output.suffix == '.sh' else 0o644
				if os.name != 'nt':
					output.chmod(mode)
				# Candidates accept either hexadecimal spelling (PowerShell hashes
				# are uppercase). Business manifests use canonical lowercase while
				# retaining the already verified expected digest for the copy audit.
				artifacts.append({**item, 'sha256': item['sha256'].lower(), 'path': name,
					'sourcePath': source, 'role': artifact_category(source, business), 'mode': mode})
			manifest = _business_metadata(identity, business, scope, artifacts, repo, prepared, root)
			manifest.update(supportedTargets=sorted(support), consumerTargets=sorted(consumers))
			file = prepared / manifest_name(scope)
			file.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
			(prepared / 'checksums.txt').write_text(''.join(f"{item['sha256']}  {item['path']}\n" for item in artifacts), encoding='utf-8')
			read_business(prepared, scope)
			destination = business_root(root, business, scope) / v
			if destination.exists():
				verify_business_inventory(destination)
				if scope in ('any', 'wasm32') and (destination / manifest_name(scope)).read_bytes() != file.read_bytes():
					raise ValueError('Shared artifact identity differs; use a new business version')
			ref = {'target': scope, 'version': v, 'manifest': (destination / manifest_name(scope)).relative_to(root).as_posix(), 'sha256': digest(file)}
			refs_by_business.setdefault(business, []).append(ref)
			replacements.append((prepared, destination))
		updated = json.loads(json.dumps(indexes))
		updated.setdefault(target, {'businesses': {}})
		for business, refs in refs_by_business.items():
			previous_entry = updated[target]['businesses'].get(business, {})
			updated[target]['businesses'][business] = {'version': identity['businessVersions'][business], 'manifests': refs,
				'partial': business in identity.get('partialBusinesses', [])}
			from veroscript_qualification import bind_qualification
			qualification = bind_qualification(identity.get('businessQualifications', {}).get(business), refs)
			if qualification is not None:
				updated[target]['businesses'][business]['qualification'] = qualification
			if business == 'CSharp':
				updated[target]['businesses'][business]['nativeVersion'] = identity.get('businessNativeVersions', {}).get(business,
					identity.get('nativeVersion', identity['version']))
			entry = updated[target]['businesses'][business]
			if qualification is None and previous_entry.get('qualification') is not None and all(
				previous_entry.get(field, False if field == 'partial' else None) == entry.get(field, False if field == 'partial' else None)
				for field in ('version', 'manifests', 'partial', 'nativeVersion')):
				entry['qualification'] = previous_entry['qualification']
			for ref in refs:
				if ref['target'] in ('any', 'wasm32'):
					shared = updated.setdefault(ref['target'], {'businesses': {}})
					previous_shared = shared['businesses'].get(business, {})
					shared_entry = {'version': ref['version'], 'manifests': [ref]}
					if previous_shared.get('version') == ref['version'] and previous_shared.get('manifests') == [ref] and previous_shared.get('qualification') is not None:
						shared_entry['qualification'] = previous_shared['qualification']
					shared['businesses'][business] = shared_entry
		# Resolve every prospective reference and dependency before replacing files.
		overlay = {destination: prepared for prepared, destination in replacements}
		prospective = {}
		for scope, index in updated.items():
			index['target'] = scope
			prospective.update(manifest_references(root, index, overlay))
		_dependency_closure(root, prospective, overlay)
		backup = scratch_directory(repo) / ('veroscript-business-backup-' + uuid.uuid4().hex)
		backup.mkdir()
		moved, files_before = [], {}
		try:
			for prepared, destination in replacements:
				guard_tree(destination)
				destination.parent.mkdir(parents=True, exist_ok=True)
				saved = backup / destination.relative_to(root)
				saved.parent.mkdir(parents=True, exist_ok=True)
				if destination.exists():
					move_directory(destination, saved)
				moved.append((destination, saved))
				move_directory(prepared, destination)
			for scope, index in updated.items():
				file = collection_index(root, scope)
				files_before[file] = file.read_bytes() if file.exists() else None
				file.parent.mkdir(parents=True, exist_ok=True)
				updated[scope] = _index_state(root, scope, index['businesses'])
				prepared_file = stage / (scope + '.json')
				prepared_file.write_text(json.dumps(updated[scope], indent=2) + '\n', encoding='utf-8')
				os.replace(prepared_file, file)
			file = root / 'release-index.json'
			files_before[file] = file.read_bytes() if file.exists() else None
			metadata = {'schemaVersion': COLLECTION_SCHEMA, 'product': COLLECTION_PRODUCT, 'generation': uuid.uuid4().hex,
				'indexes': {scope: {'path': 'indexes/' + scope + '.json', 'sha256': digest(collection_index(root, scope))} for scope in sorted(updated)}}
			prepared_file = stage / 'root-index.json'
			prepared_file.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
			os.replace(prepared_file, file)
			for scope in updated:
				verify_collection(root, scope)
		except BaseException:
			for destination, saved in reversed(moved):
				if destination.exists():
					failed = backup / 'failed' / destination.relative_to(root)
					failed.parent.mkdir(parents=True, exist_ok=True)
					move_directory(destination, failed)
				if saved.exists():
					move_directory(saved, destination)
			for file, data in files_before.items():
				if data is None:
					file.unlink(missing_ok=True)
				else:
					file.write_bytes(data)
			for directory in sorted((path for path in root.rglob('*') if path.is_dir()), key=lambda path: len(path.parts), reverse=True):
				if not any(directory.iterdir()):
					directory.rmdir()
			raise
		try:
			discard_obsolete_versions(root, target, updated[target])
			guard_tree(backup)
			if not backup.resolve().is_relative_to(scratch_directory(repo).resolve()):
				raise ValueError('Publication backup escapes its scratch directory')
			shutil.rmtree(backup)
		except (OSError, ValueError) as error:
			raise RuntimeError('Publication succeeded, but obsolete release cleanup failed: ' + str(error)) from error


def publish_component(repo: Path, source: Path, root: Path, business: str, release_version: str, target: str) -> None:
	"""Merge a verified standalone archive/installer into its owning business."""
	if target not in RELEASE_TARGETS:
		raise ValueError('Component publication requires a native/mobile consuming target')
	if target in MOBILE_TARGETS:
		from veroscript_mobile_release import publish_mobile_component
		publish_mobile_component(repo, source, root, business, release_version, target)
		return
	from veroscript_release import archive_entries, archive_member_digest, write_manifest
	from veroscript_ide import IDE_ARTIFACTS
	release_version = version(release_version)
	config = load_versions(repo, override=release_version, businesses=[business])
	root = collection_root(repo, root)
	require_business_layout(root)
	directory = root
	guard_tree(source)
	lock = acquire_collection_lock(repo, root, create_root=True)
	try:
		with tempfile.TemporaryDirectory(prefix='veroscript-component-', dir=scratch_directory(repo)) as temporary:
			candidate = Path(temporary)
			leaf = business_root(root, business, target) / release_version
			old = None
			if collection_index(root, target).is_file():
				index = verify_collection(root, target)
				old = composed_businesses(root, index).get(business)
				if old and old['version'] != release_version:
					old = None
			if old and old['version'] == release_version:
				for item in old['artifacts']:
					destination = candidate / audit_source(item, business, target)
					destination.parent.mkdir(parents=True, exist_ok=True)
					shutil.copy2(Path(item['_leaf']) / item['path'], destination)
			categories = {'Runtime': 'runtime', 'CoreSDK': 'sdk', 'Interop': 'interop', 'Cpp': 'languages',
						  'CSharp': 'languages', 'Python': 'languages', 'Jit': 'jit', 'Vm': 'vm', 'WebAssembly': 'webassembly',
						  'Resources': 'Resources', **dict.fromkeys(IDE_BUSINESSES, 'ide'),
						  'Json': 'modules', 'Network': 'modules', 'Http': 'modules', 'WebSocket': 'modules', 'Boost': 'modules', 'Curl': 'modules',
						  **{business: 'modules' for business in FOUNDATION_BUSINESSES}}
			retained_sources = {item['path']: item['sourcePath'] for item in old['artifacts']} if old else {}
			found = 0
			component_files = [*source.iterdir(), *((source / 'symbols').iterdir() if (source / 'symbols').is_dir() else [])]
			for file in component_files:
				if not file.is_file() or not (file.name.endswith(('.zip', '.vspkg', '.vsix', '.tar.gz', '.exe', '.whl', '.nupkg', '.snupkg')) or 'Installer-' in file.name):
					continue
				distribution = distribution_identity(file)
				if distribution and target not in distribution_targets(file, target):
					raise ValueError(f'Distribution target mismatch: {file.name} is not for {target}')
				if business in IDE_BUSINESSES:
					prefix, suffix = IDE_ARTIFACTS[business]
					wrong_version = file.name != f'{prefix}-{release_version}{suffix}'
				else:
					wrong_version = (distribution != (business, release_version) if distribution else
									 f'-{release_version}-' not in file.name and file.name != f'VeroScriptDocs-{release_version}.zip')
				if artifact_business(file.name) != business or wrong_version:
					raise ValueError(f'Wrong standalone artifact business/version: {file}')
				from veroscript_module_symbols import MODULES, symbol_archive_name
				module_symbol = business in MODULES and file.name == symbol_archive_name(business, release_version, target)
				category = 'installer' if 'Installer-' in file.name else ('symbols' if module_symbol or file.name.startswith('VeroScriptSymbols-') else categories[business])
				source_path = f'{category}/{file.name}'
				# Retain the original staging route when replacing an owned file.
				# Matrix distributions use distributions/python, not languages.
				destination = candidate / retained_sources.get(destination_name(source_path, business), source_path)
				destination.parent.mkdir(parents=True, exist_ok=True)
				shutil.copy2(file, destination)
				if file.name.endswith(('.zip', '.vspkg', '.vsix', '.tar.gz', '.whl', '.nupkg', '.snupkg')):
					archive_entries(file, max_entry_bytes=4096)
				found += 1
			if not found:
				raise ValueError('No standalone distributable archives/installers were generated')
			index_file = collection_index(root, target)
			index = verify_collection(root, target) if index_file.is_file() else {'businesses': {}}
			core_entry = index['businesses'].get('CoreSDK')
			core = read_business(business_root(root, 'CoreSDK', target) / core_entry['version'], target) if core_entry else None
			if business == 'Cpp' and old:
				# Updating an installer must not retarget the retained C++ SDK.
				dependency = old['dependencies']['CoreSDK']
				core = verify_core_dependency(business_root(root, 'CoreSDK', target) / dependency['version'], target, dependency)
			if core:
				config['businesses']['CoreSDK'] = core['version']
			build_id = old['buildId'] if old else (core['buildId'] if core else digest(next(p for p in candidate.rglob('*') if p.is_file())))
			if business == 'CoreSDK':
				for file in (candidate / 'sdk').glob('*') if (candidate / 'sdk').exists() else []:
					library = 'lib/VeroScript.lib' if TARGETS[target][0] == 'windows' else 'lib/libVeroScript.a'
					prefix = file.name.removesuffix('.tar.gz').removesuffix('.zip')
					build_id = archive_member_digest(file, prefix + '/' + library)
			if business == 'Cpp' and not core:
				raise ValueError('Publish the matching CoreSDK before the standalone C++ package')
			write_manifest(candidate, config['businesses']['CoreSDK'], target, build_id,
						   business_versions=config['businesses'], native_version=config['nativeVersion'])
			path = candidate / 'release-manifest.json'
			manifest = json.loads(path.read_text(encoding='utf-8'))
			manifest['selectedBusinesses'] = [business]
			category = {'Jit': 'jit', 'Vm': 'vm'}.get(business, 'modules')
			if business in MODULE_BUSINESSES and any((candidate / category).glob('*.vspkg')):
				manifest['installableModuleBusinesses'] = [business]
				if business not in {'Jit', 'Vm'}:
					from veroscript_modules import read_module_metadata
					metadata = read_module_metadata(next((candidate / category).glob('*.vspkg')))
					manifest['businessBuildIds'] = {business: metadata['payloadId']}
			if business in IDE_BUSINESSES:
				prefix, suffix = IDE_ARTIFACTS[business]
				artifact = candidate / 'ide' / f'{prefix}-{release_version}{suffix}'
				manifest['businessBuildIds'] = {business: digest(artifact)}
			if business == 'Python' and old and not old.get('partial') and old.get('pythonWheelVersions'):
				# Updating one wheel must pass the recorded complete matrix gate.
				manifest['pythonWheelVersions'] = old['pythonWheelVersions']
			elif (not old or old.get('partial')) and business not in IDE_BUSINESSES and business not in manifest.get('installableModuleBusinesses', []):
				manifest['partialBusinesses'] = [business]
			path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
			_publish_locked(repo, candidate, root, [business])
	finally:
		release_collection_lock(lock)



def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('command', choices=('versions', 'configuration', 'publish', 'publish-component', 'publish-distributions', 'verify', 'guard', 'materialize', 'prune'))
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--versions-file', type=Path)
	parser.add_argument('--version')
	parser.add_argument('--business', choices=ALL_BUSINESSES, action='append')
	parser.add_argument('--candidate', type=Path)
	parser.add_argument('--release-directory', type=Path)
	parser.add_argument('--target', choices=DISTRIBUTION_SCOPES, default=host_target())
	parser.add_argument('--dry-run', action='store_true')
	parser.add_argument('--require-complete', action='store_true')
	parser.add_argument('--check-source', action='store_true')
	args = parser.parse_args()
	if args.dry_run and args.command != 'prune':
		parser.error('--dry-run requires prune')
	if (args.require_complete or args.check_source) and args.command != 'verify':
		parser.error('--require-complete/--check-source require verify')
	repo = args.repo_root.resolve()
	root = args.release_directory or module_root(repo) / 'Release'
	if args.command in ('versions', 'configuration'):
		config = load_versions(repo, args.versions_file, args.version, args.business)
		if args.command == 'configuration':
			config.update(sourceInventory=check_product_inventory(repo),
				businessPaths={name: business_route(name) for name in ALL_BUSINESSES},
				installerBusinesses=list(INSTALLER_BUSINESSES), ideBusinesses=list(IDE_BUSINESSES), nativeBusinesses=list(BUSINESSES),
				targetBusinesses={target: list(target_businesses(target, optional=True)) for target in RELEASE_TARGETS},
				distributionScopes=list(DISTRIBUTION_SCOPES), artifactCategories=list(ARTIFACT_CATEGORIES),
				collectionSchemaVersion=COLLECTION_SCHEMA, targetIndexSchemaVersion=INDEX_SCHEMA, businessManifestSchemaVersion=BUSINESS_SCHEMA)
		print(json.dumps(config))
	elif args.command == 'guard':
		print(collection_root(repo, root))
	elif args.command == 'prune':
		paths = prune_versions(repo, root, args.target, dry_run=args.dry_run)
		print(json.dumps({'dryRun': args.dry_run, 'obsoleteVersions': [str(path) for path in paths]}))
	elif args.command == 'publish':
		if not args.candidate:
			parser.error('--candidate is required')
		publish_businesses(repo, args.candidate, root, args.business)
	elif args.command == 'publish-component':
		if not args.candidate or not args.business or len(args.business) != 1 or not args.version or args.target not in RELEASE_TARGETS:
			parser.error('publish-component requires --candidate, one --business, --version and a native/mobile consumer --target')
		publish_component(repo, args.candidate, root, args.business[0], args.version, args.target)
	elif args.command == 'publish-distributions':
		if not args.candidate or args.target not in RELEASE_TARGETS:
			parser.error('publish-distributions requires --candidate and a native/mobile consuming --target')
		groups = {}
		guard_tree(args.candidate)
		for file in args.candidate.rglob('*'):
			if file.is_file():
				identity = distribution_identity(file)
				if identity:
					for target in distribution_targets(file, args.target):
						groups.setdefault((*identity, target), []).append(file)
		if not groups:
			raise ValueError('No Python/NuGet distributions found')
		for (business, v, target), files in groups.items():
			with tempfile.TemporaryDirectory(prefix='veroscript-distributions-', dir=scratch_directory(repo)) as temporary:
				stage = Path(temporary)
				for file in files:
					output = stage / file.name
					if output.exists() and digest(output) != digest(file):
						raise ValueError('Conflicting distribution filename')
					shutil.copy2(file, output)
				publish_component(repo, stage, root, business, v, target)
	else:
		index = verify_collection(root, args.target, repo if args.check_source else None, require_complete=args.require_complete)
		if args.command == 'materialize':
			if not args.candidate or args.target not in RELEASE_TARGETS:
				parser.error('materialize requires --candidate and a native/mobile --target')
			stage = safe_output(repo, args.candidate, component=True)
			if stage.exists() and any(stage.iterdir()):
				raise ValueError('Audit staging directory must be empty')
			stage.mkdir(parents=True, exist_ok=True)
			materialize_businesses(root, index, stage)
		print(f"Business releases verified: {len(index['businesses'])} businesses, {args.target}")


if __name__ == '__main__':
	main()
