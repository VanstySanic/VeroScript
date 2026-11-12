# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Native module SDK staging and release contracts."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import re
import tempfile
import tarfile
import zipfile

from veroscript_products import FEATURE_MODULE_BUSINESSES, FOUNDATION_BUSINESSES, SDK_MODULE_BUSINESSES, MODULE_PACKAGE_NAMES, clean_functional_payload, verify_module_payload
from veroscript_release import TARGETS, binary_prefix, digest, native_identity, verify_windows_runtime
from veroscript_cmake_exports import normalize_curl_exports, verify_curl_export_entries


def assemble_modules(repo, builds, destination, versions, target, selected, *, bindings=True, core_sdk=None, package_tool=None, cpp_sdk=None, symbol_status=None):
	from veroscript_release import archive_tree, scratch_directory
	identities = {}
	foundation = [name for name in selected if name in FOUNDATION_BUSINESSES]
	if foundation:
		from veroscript_foundation import assemble_foundation
		identities.update(assemble_foundation(repo, builds, destination, versions, target, foundation,
											 bindings=bindings, core_sdk=core_sdk, cpp_sdk=cpp_sdk, package_tool=package_tool))
	with tempfile.TemporaryDirectory(prefix='veroscript-module-stage-', dir=scratch_directory(repo)) as temporary:
		roots = {}
		for business in module_selection(selected, builds):
			if business in FOUNDATION_BUSINESSES:
				continue
			name = MODULE_PACKAGE_NAMES[business] + '-' + versions[business] + '-' + target
			package = Path(temporary) / name
			metadata = stage_module(repo, builds[business], package, business, target, versions, bindings=bindings)
			identities[business] = metadata['payloadId']
			roots[business] = package
			suffix = '.zip' if target.startswith('win-') else '.tar.gz'
			archive_tree(package, destination / 'modules' / (name + suffix))
		if bindings and roots:
			from veroscript_module_packages import create_installable_modules, package_tool as locate_tool
			create_installable_modules(repo, roots, destination, versions, target, locate_tool(builds, package_tool))
	from veroscript_module_symbols import assemble_module_symbols
	status = assemble_module_symbols(repo, builds, destination, versions, target, list(identities))
	if symbol_status is not None:
		symbol_status.update(status)
	return identities

MODULE_SOURCES = {'Json': 'Modules/Network/VeroScriptJson',
				  **{name: 'Modules/Network/VeroScript' + name for name in ('Network', 'Http', 'WebSocket')},
				  **{name: 'Modules/Network/Backend/VeroScript' + name for name in ('Boost', 'Curl')}}


def read_module_metadata(archive):
	"""Read the manifest after the normal archive/outer checksum audit."""
	archive = Path(archive)
	prefix = archive.name.removesuffix('.tar.gz').removesuffix('.zip')
	member = ('' if archive.suffix == '.vspkg' else prefix + '/') + 'veroscript-module.json'
	if archive.suffix in ('.zip', '.vspkg'):
		with zipfile.ZipFile(archive) as reader:
			return json.loads(reader.read(member))
	with tarfile.open(archive) as reader:
		return json.loads(reader.extractfile(member).read())


def module_selection(businesses, builds=None):
	"""Contract packages are independent; providers declare their actual service dependencies."""
	selected = set(businesses)
	if not selected or not selected.issubset(SDK_MODULE_BUSINESSES):
		raise ValueError('Select a registered SDK module business')
	if selected.intersection({'Http', 'WebSocket', 'Boost', 'Curl'}):
		selected.add('Network')
	if 'Curl' in selected:
		selected.add('Http')
	if 'Boost' in selected and builds:
		flags = build_cache(builds['Boost'])
		if enabled(flags.get('VEROSCRIPT_BOOST_HAS_HTTP_SERVER', 'OFF')):
			selected.add('Http')
		if enabled(flags.get('VEROSCRIPT_BOOST_HAS_WEBSOCKET', 'OFF')):
			selected.add('WebSocket')
	return [name for name in SDK_MODULE_BUSINESSES if name in selected]


def build_cache(build: Path):
	result = {}
	for line in (build / 'CMakeCache.txt').read_text(encoding='utf-8-sig').splitlines():
		if not line.startswith(('#', '//')) and ':' in line and '=' in line:
			key, value = line.split('=', 1)
			result[key.split(':', 1)[0]] = value
	return result


def enabled(value):
	return value.upper() in {'ON', '1', 'TRUE', 'YES', 'Y'}


def backend_services(business, flags=None):
	flags = flags or {}
	if business == 'Curl':
		return ['http-client']
	if business != 'Boost':
		return []
	services = ['network']
	if enabled(flags.get('VEROSCRIPT_BOOST_HAS_HTTP_SERVER', 'OFF')):
		services.append('http-server')
	if enabled(flags.get('VEROSCRIPT_BOOST_HAS_WEBSOCKET', 'OFF')):
		services += ['websocket-client', 'websocket-server']
	return services


def verify_backend_identity(metadata, business):
	if business != 'Json' and (not isinstance(metadata.get('networkImplementationId'), str) or
			not re.fullmatch('[0-9a-f]{64}', metadata['networkImplementationId'])):
		raise ValueError('Missing/invalid Network foundation implementation identity')
	services = metadata.get('backendServices', [])
	if not isinstance(services, list) or not all(isinstance(service, str) for service in services):
		raise ValueError('Invalid backend services')
	if business == 'Boost':
		if (not isinstance(metadata.get('nativeTls'), bool) or not isinstance(metadata.get('websocketCompression'), bool)
				or (metadata['websocketCompression'] and 'websocket-client' not in services)):
			raise ValueError('Invalid Boost build capabilities')
		if (len(services) != len(set(services)) or 'network' not in services
				or not set(services) <= {'network', 'http-server', 'websocket-client', 'websocket-server'}
				or (('websocket-client' in services) != ('websocket-server' in services))
				or not isinstance(metadata.get('boostRuntimeId'), str)
				or not re.fullmatch('[0-9a-f]{64}', metadata['boostRuntimeId'])):
			raise ValueError('Invalid Boost runtime/services identity')
	elif business == 'Curl':
		if services != ['http-client']:
			raise ValueError('Invalid curl service identity')
	elif services:
		raise ValueError('Frontend modules cannot advertise backend services')
	if business != 'Boost' and 'boostRuntimeId' in metadata:
		raise ValueError('Only Boost may declare its runtime identity')
	if 'networkRuntimeId' in metadata:
		raise ValueError('Obsolete Network runtime identity')


def required_notices(business, metadata):
	if business == 'Json':
		return ['licenses/VeroScriptJson/jsoncons/LICENSE', 'licenses/VeroScriptJson/quickjs-regexp/LICENSE']
	if business == 'Curl':
		return ['licenses/VeroScriptCurl/curl/COPYING']
	if business == 'Boost':
		return ['licenses/VeroScriptBoost/boost/LICENSE_1_0.txt'] + ([
			'licenses/VeroScriptBoost/openssl/OpenSSL-LICENSE.txt',
			'licenses/VeroScriptBoost/openssl/openssl-source.json'] if metadata.get('nativeTls') else [])
	return []


def dependency_names(business, services=()):
	dependencies = {'Network'} if business in {'Http', 'WebSocket', 'Boost', 'Curl'} else set()
	if business == 'Curl' or 'http-server' in services:
		dependencies.add('Http')
	if any(service.startswith('websocket-') for service in services):
		dependencies.add('WebSocket')
	return dependencies


def module_dependencies(business, versions, flags=None):
	return {name: versions[name] for name in sorted(dependency_names(business, backend_services(business, flags)))}


def verify_module_entries(entries, business, package_version, target, repo=None, *, file_digests=None):
	"""Validate independent package contents, including native target and notices."""
	if business in FOUNDATION_BUSINESSES:
		from veroscript_foundation import verify_foundation_entries
		return verify_foundation_entries(entries, business, package_version, target, repo, file_digests=file_digests)
	if business not in FEATURE_MODULE_BUSINESSES:
		raise ValueError('Unknown feature module')
	verify_curl_export_entries(entries)
	verify_module_payload(entries)
	try:
		meta = json.loads(entries['veroscript-module.json'][0])
	except (KeyError, ValueError) as error:
		raise ValueError('Missing/invalid feature module manifest') from error
	if (meta.get('schemaVersion') != 1 or meta.get('business') != business
			or meta.get('version') != package_version or meta.get('target') != target
			or meta.get('profile') not in {'native-data-sdk', 'native-script-sdk'}):
		raise ValueError('Feature module identity mismatch')
	verify_backend_identity(meta, business)
	dependencies = meta.get('dependencies')
	if not isinstance(dependencies, dict) or set(dependencies) != dependency_names(business, meta.get('backendServices', [])):
		raise ValueError('Feature module dependency mismatch')
	from veroscript_release import version
	for dependency_version in dependencies.values():
		version(dependency_version)
	base = 'VeroScript' + business
	system, architecture = TARGETS[target]
	static = 'lib/' + (base + '.lib' if system == 'windows' else 'lib' + base + '.a')
	required = ['LICENSE', static, 'include/' + base + '/' + base + '.h',
				'lib/cmake/' + base + '/' + base + 'Config.cmake',
				'lib/cmake/' + base + '/' + base + 'ConfigVersion.cmake']
	catalogs = [f'share/{base}/Resources/Localization/{locale}/{business.lower()}.json'
				for locale in ('en-US', 'fr-FR', 'ja-JP', 'ko-KR', 'zh-CN', 'zh-TW')] if business not in {'Boost', 'Curl'} else []
	required += catalogs
	if business == 'Boost':
		required += ['lib/veroscript/modules/' + ('' if system == 'windows' else 'lib')
					 + 'VeroScriptBoostRuntime' + ('.dll' if system == 'windows' else '.dylib' if system == 'macos' else '.so')]
	required += required_notices(business, meta)
	if meta['profile'] == 'native-script-sdk':
		required += ['include/' + base + '/' + base + 'Module.h',
					 'lib/cmake/' + base + '/' + base + 'BindingsTargets.cmake']
		dynamic = ('lib/veroscript/modules/' + ('' if system == 'windows' else 'lib')
				   + base + 'Module' + ('.dll' if system == 'windows' else '.dylib' if system == 'macos' else '.so'))
		required.append(dynamic)
	for path in required:
		if path not in entries or not entries[path][0]:
			raise ValueError('Missing feature module payload: ' + path)
	for path in catalogs:
		try:
			catalog = json.loads(entries[path][0])
		except (ValueError, UnicodeError) as error:
			raise ValueError('Invalid feature module localization: ' + path) from error
		if not isinstance(catalog, dict) or not catalog or not all(isinstance(text, str) and text for text in catalog.values()):
			raise ValueError('Invalid feature module localization: ' + path)
	if business != 'Boost' and any('VeroScriptBoostRuntime' in path for path in entries):
		raise ValueError('Only Boost may distribute its runtime')
	if any('VeroScriptNetworkRuntime' in path for path in entries):
		raise ValueError('Obsolete Network runtime payload')
	for path, (data, _) in entries.items():
		if data.startswith((b'MZ', b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')):
			if native_identity(data[:4096]) != (system, architecture):
				raise ValueError('Feature module native target mismatch: ' + path)
	if system == 'windows':
		verify_windows_runtime(entries, (system, architecture))
	if repo is not None:
		source_root = repo / MODULE_SOURCES[business] / 'Resources/Localization'
		for source in source_root.rglob('*.json'):
			path = 'share/' + base + '/Resources/Localization/' + source.relative_to(source_root).as_posix()
			if path not in entries or entries[path][0] != source.read_bytes():
				raise ValueError('Stale feature module localization: ' + path)
		for header in (repo / MODULE_SOURCES[business] / 'Public').rglob('*'):
			if not header.is_file():
				continue
			path = 'include/' + base + '/' + header.relative_to(repo / MODULE_SOURCES[business] / 'Public').as_posix()
			# The data-only profile intentionally omits the registration header.
			if path.endswith(('Module.h', 'Providers.h', 'ProviderHost.h')) and meta['profile'] == 'native-data-sdk':
				continue
			if path not in entries or entries[path][0] != header.read_bytes():
				raise ValueError('Stale feature module public header: ' + path)
		hooks = {'Network': ('NetworkProvider.h', 'NetworkStream.h', 'NetworkDiagnostics.h', 'NetworkUpgrade.h'),
				 'Http': ('HttpClientHistory.h',), 'WebSocket': ('WebSocketProtocols.h',)}
		for filename in hooks.get(business, ()):
			source = repo / MODULE_SOURCES[business] / 'Internal' / filename
			path = 'include/' + base + '/Backend/' + filename
			if path not in entries or entries[path][0] != source.read_bytes():
				raise ValueError('Stale feature module provider contract: ' + path)
	return meta


def stage_module(repo, build, destination, business, target, versions, *, bindings=True, config='Release'):
	"""Install only the selected module component; never install an entire root build."""
	repo, build, destination = Path(repo).resolve(), Path(build).resolve(), Path(destination).resolve()
	if destination.exists() and any(destination.iterdir()):
		raise ValueError('Feature module staging requires an empty directory')
	settings = build_cache(build)
	expected_source = repo / ('Modules/Network/VeroScriptJson' if business == 'Json' else MODULE_SOURCES[business] if business in {'Boost', 'Curl'} else 'Modules/Network')
	allowed_sources = {repo, expected_source, repo / 'Modules/Network'}
	if business in {'Network', 'Http', 'WebSocket'}:
		allowed_sources.update(repo / MODULE_SOURCES[name] for name in ('Boost', 'Curl'))
	if Path(settings['CMAKE_HOME_DIRECTORY']).resolve() not in allowed_sources:
		raise ValueError('Feature module build belongs to another source tree')
	if versions[business] != '0.1.1':
		raise ValueError('Feature module source version is 0.1.1; rebuild after updating the module version')
	binding_option = 'VEROSCRIPT_JSON_BUILD_BINDINGS' if business == 'Json' else 'VEROSCRIPT_NETWORK_BUILD_BINDINGS'
	if bindings and not enabled(settings.get(binding_option, 'OFF')):
		raise ValueError('Script SDK requested from a data-only build')
	if business in {'Http', 'WebSocket'} and not enabled(settings.get('VEROSCRIPT_BUILD_' + business.upper(), 'OFF')):
		raise ValueError('Requested protocol is disabled in the build')
	cmake = settings.get('CMAKE_COMMAND', 'cmake')
	destination.mkdir(parents=True, exist_ok=True)
	for component in ['VeroScript' + business] + (['VeroScript' + business + 'Bindings'] if bindings else []):
		subprocess.run([cmake, '--install', str(build), '--config', config, '--component', component,
						'--prefix', str(destination)], check=True)
	# curl's own install/export is self-contained. Run only its subproject's
	# installer, which also works when curl was shared with the package manager.
	bundled_curl = enabled(settings.get('VEROSCRIPT_CURL_BUNDLED', 'OFF'))
	if business == 'Curl' and bundled_curl:
		curl_build = Path(settings.get('CURL_BINARY_DIR', ''))
		if not curl_build.is_absolute() or not (curl_build / 'cmake_install.cmake').is_file():
			raise ValueError('Bundled curl install tree is missing')
		subprocess.run([cmake, '--install', str(curl_build), '--config', config,
						'--prefix', str(destination)], check=True)
		# These curl helper files embed the original configure-time prefix.
		# The module SDK consumes curl through its relocatable CMake exports.
		for helper in ('bin/curl-config', 'lib/pkgconfig/libcurl.pc'):
			(destination / helper).unlink(missing_ok=True)
	normalize_curl_exports(destination)
	shutil.copy2(repo / 'LICENSE', destination / 'LICENSE')
	clean_functional_payload(destination)
	if target.startswith('linux-'):
		for path in sorted(destination.rglob('*')):
			if path.is_file() and not path.is_symlink():
				with path.open('rb') as stream:
					magic = stream.read(8)
				if magic == b'!<arch>\n' or magic.startswith(b'\x7fELF'):
					subprocess.run([settings.get('CMAKE_OBJCOPY', 'objcopy'), '--strip-debug', str(path)], check=True)
	elif target.startswith('macos-'):
		from veroscript_native_symbols import strip_macos_tree
		strip_macos_tree(destination, settings.get('CMAKE_STRIP', 'strip'))
	metadata = {'schemaVersion': 1, 'business': business, 'name': MODULE_PACKAGE_NAMES[business],
				'version': versions[business], 'target': target,
				'profile': 'native-script-sdk' if bindings else 'native-data-sdk',
				'dependencies': module_dependencies(business, versions, settings),
				'systemDependencies': ['CURL >= 8.13'] if business == 'Curl' and
					not bundled_curl else []}
	if business != 'Json':
		identity = build / ('veroscript-network-identity-' + config + '.txt')
		metadata['networkImplementationId'] = digest(identity)
	if business in {'Boost', 'Curl'}:
		metadata['backendServices'] = backend_services(business, settings)
	if business == 'Boost':
		metadata['nativeTls'] = enabled(settings.get('VEROSCRIPT_NETWORK_TLS', 'OFF'))
		metadata['websocketCompression'] = enabled(settings.get('VEROSCRIPT_BOOST_HAS_WEBSOCKET', 'OFF')) and enabled(settings.get('VEROSCRIPT_WEBSOCKET_COMPRESSION', 'OFF'))
		runtime = Path((build / ('veroscript-boost-runtime-' + config + '.txt')).read_text(encoding='utf-8').strip())
		metadata['boostRuntimeId'] = digest(runtime)
	static_ssl = enabled(settings.get('OPENSSL_USE_STATIC_LIBS', 'ON' if target.startswith('win-') else 'OFF'))
	if ((business == 'Boost' and enabled(settings.get('VEROSCRIPT_NETWORK_TLS', 'OFF')) and not static_ssl)
			or (business == 'Curl' and bundled_curl and not target.startswith('win-'))):
		metadata['systemDependencies'].append('OpenSSL >= 3.0')
	# This is an identity of the actual package payload, independent of the
	# compiler/build directory. The enclosing release also hashes the archive.
	metadata['payloadId'] = hashlib.sha256('\n'.join(
		path.relative_to(destination).as_posix() + ':' + digest(path)
		for path in sorted(destination.rglob('*')) if path.is_file()).encode('utf-8')).hexdigest()
	(destination / 'veroscript-module.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
	entries = {path.relative_to(destination).as_posix(): (binary_prefix(path) if path.stat().st_size > 1024 * 1024
				else path.read_bytes(), path.stat().st_mode) for path in destination.rglob('*') if path.is_file()}
	verify_module_entries(entries, business, versions[business], target, repo)
	return metadata
