# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Independent foundation, resource and GUI native SDK releases."""
from __future__ import annotations

import hashlib
import json
import ctypes
import _ctypes
import os
from pathlib import Path
from pathlib import PurePosixPath
import re
import subprocess
import tempfile
import tarfile
import zipfile

from veroscript_products import FOUNDATION_BUSINESSES, FOUNDATION_PACKAGE_NAMES, FOUNDATION_MODULE_DIRECTORIES, clean_functional_payload, verify_module_payload
from veroscript_release import TARGETS, archive_tree, binary_prefix, core_public_abi, digest, native_identity, scratch_directory, verify_windows_runtime, version


def archive_digests(path):
	"""Hash an already validated archive in one pass, including native .so links."""
	result, links = {}, {}
	if path.suffix == '.zip':
		with zipfile.ZipFile(path) as reader:
			for item in reader.infolist():
				if not item.is_dir():
					with reader.open(item) as stream:
						result[item.filename] = hashlib.file_digest(stream, 'sha256').hexdigest()
	else:
		with tarfile.open(path, 'r|gz') as reader:
			for item in reader:
				if item.issym():
					links[item.name] = (PurePosixPath(item.name).parent / item.linkname).as_posix()
				elif item.isfile():
					with reader.extractfile(item) as stream:
						result[item.name] = hashlib.file_digest(stream, 'sha256').hexdigest()
		def resolve(name):
			if name not in result:
				result[name] = resolve(links[name])
			return result[name]
		for name in links:
			resolve(name)
	return result


def foundation_dependencies(business, settings, bindings):
	if business in {'Gui', 'RmlUi', 'Stb', 'FontBackend'}:
		return {'Gui': ['Platform', 'Graphics', 'Image', 'Font'], 'RmlUi': ['Gui'],
				'Stb': ['Image'], 'FontBackend': ['Font']}[business]
	if business == 'Graphics' and bindings:
		return ['Platform']
	if business == 'Sdl3':
		from veroscript_modules import enabled
		keys = ['VEROSCRIPT_SDL3_GPU', 'VEROSCRIPT_SDL3_OPENGL', 'VEROSCRIPT_SDL3_WEBGPU']
		if os.name == 'nt':
			keys.append('VEROSCRIPT_SDL3_D3D11')
		return ['Platform'] + (['Graphics'] if any(enabled(settings.get(key, 'OFF')) for key in keys) else []) + (
				['Audio'] if enabled(settings.get('VEROSCRIPT_SDL3_AUDIO', 'OFF')) else [])
	return []


def native_extension_contract(library, target, public_abi, implementation, package_version, business='Sdl3', *, compiled_context=None):
	"""Read a shipped native module descriptor without creating a service."""
	if compiled_context is not None and hasattr(compiled_context,'native_abi'):
		return compiled_context.native_abi(library,target,public_abi,implementation)
	class Descriptor(ctypes.Structure):
		_fields_ = [('size', ctypes.c_uint32), ('api', ctypes.c_uint32), ('abi', ctypes.c_char * 768),
					('core', ctypes.c_char * 96), ('platform', ctypes.c_char * 64),
					('id', ctypes.c_char * 64), ('version', ctypes.c_char * 64),
					('roles', ctypes.c_uint32), ('flags', ctypes.c_uint32),
					('create', ctypes.c_void_p), ('destroy', ctypes.c_void_p)]
	backend = ctypes.CDLL(str(library))
	try:
		query = backend.veroscript_extension_query
		query.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
		query.restype = ctypes.c_uint32
		descriptor = Descriptor()
		expected_id = {'Gui': 'veroscript-gui-tools', 'FontBackend': 'veroscript-font-backend'}.get(
			business, 'veroscript-' + business.lower())
		expected_roles = 8 if business == 'Gui' else 5 if business in {'Sdl3', 'FontBackend'} else 4
		if (query(0x101, ctypes.sizeof(descriptor), ctypes.byref(descriptor)) != 0x101 or
				descriptor.size != ctypes.sizeof(descriptor) or descriptor.api != 0x101 or
				descriptor.id != expected_id.encode('ascii') or descriptor.version.decode('ascii') != package_version or
				descriptor.platform.decode('ascii') != target or descriptor.roles != expected_roles or
				not descriptor.create or not descriptor.destroy or descriptor.core.decode('ascii') != implementation):
			raise ValueError(business + ' binary descriptor differs from its build/package contract')
		abi = descriptor.abi.decode('ascii')
		if not abi.startswith('native-extension:0.1.1:' + public_abi + ':'):
			raise ValueError(business + ' binary uses a stale Core public ABI')
		return abi
	finally:
		if os.name == 'nt':
			_ctypes.FreeLibrary(backend._handle)
		else:
			_ctypes.dlclose(backend._handle)


def foundation_selection(selected, builds, bindings):
	from veroscript_modules import build_cache
	selected = set(selected)
	pending = list(selected)
	while pending:
		business = pending.pop()
		settings = build_cache(builds[business])
		dependencies = set(foundation_dependencies(business, settings, bindings)) - selected
		selected.update(dependencies)
		pending.extend(dependencies)
	return [name for name in FOUNDATION_BUSINESSES if name in selected]


def foundation_script_targets(business):
	if business == 'Sdl3':
		return []
	if business == 'Gui':
		return ['GuiScript', 'GuiToolsModule']
	return [business + ('Module' if business in {'Stb', 'FontBackend', 'RmlUi'} else 'Bindings')]


def cpp_contract(repo, info, sdk, target, core, *, objcopy='objcopy'):
	if sdk is None:
		raise ValueError('Gui script SDK requires --cpp-sdk (the matching C++ Runtime prefix)')
	sdk = Path(sdk).resolve()
	config = sdk / 'lib/cmake/VeroScriptCpp/VeroScriptCppConfig.cmake'
	library = sdk / 'lib' / ('VeroScriptCpp.lib' if target.startswith('win-') else 'libVeroScriptCpp.a')
	if not config.is_file() or not library.is_file() or not info.get('Cpp'):
		raise ValueError('Missing matching C++ Runtime SDK/build library')
	text = config.read_text(encoding='utf-8-sig')
	if re.findall(r'set\(VeroScriptCpp_BUILD_ID "([0-9a-f]{64})"\)', text) != [core['buildId']]:
		raise ValueError('C++ Runtime SDK requires another Core build')
	if re.findall(r'set\(VeroScriptCpp_VERSION "([0-9.]+)"\)', text) != ['0.1.1']:
		raise ValueError('Gui source requires C++ Runtime SDK 0.1.1')
	for header in (repo / 'Extensions/Language/Cpp/VeroScriptCpp/Public').rglob('*'):
		if header.is_file():
			installed = sdk / 'include/Cpp' / header.relative_to(repo / 'Extensions/Language/Cpp/VeroScriptCpp/Public')
			if not installed.is_file() or installed.read_bytes() != header.read_bytes():
				raise ValueError('C++ Runtime SDK has stale public headers: ' + str(installed))
	built = Path(info['Cpp'])
	if digest(built) != digest(library):
		if not target.startswith(('linux-', 'macos-')):
			raise ValueError('C++ Runtime SDK library differs from the binding build')
		with tempfile.TemporaryDirectory(prefix='veroscript-gui-cpp-', dir=scratch_directory(repo)) as temporary:
			stripped = Path(temporary) / library.name
			if target.startswith('macos-'):
				from veroscript_native_symbols import strip_macos_debug
				import shutil
				shutil.copy2(built, stripped)
				strip_macos_debug(stripped)
			else:
				subprocess.run([objcopy, '--strip-debug', str(built), str(stripped)], check=True)
			if digest(stripped) != digest(library):
				raise ValueError('C++ Runtime SDK library differs from the binding build')
	return {'version': '0.1.1', 'buildId': core['buildId']}


def payload_id(metadata):
	# The generated CMake pin file is checked separately to avoid a digest cycle.
	return hashlib.sha256(json.dumps({key: value for key, value in metadata.items() if key != 'payloadId'},
									sort_keys=True, separators=(',', ':')).encode('utf-8')).hexdigest()


def core_contract(repo, info, sdk, target, *, objcopy='objcopy'):
	"""Reject headers or archives from another Core build before publication."""
	if sdk is None:
		raise ValueError('Foundation script SDKs require --core-sdk')
	sdk = Path(sdk).resolve()
	library = sdk / 'lib' / ('VeroScript.lib' if target.startswith('win-') else 'libVeroScript.a')
	core_config = sdk / 'lib/cmake/VeroScript/VeroScriptConfig.cmake'
	if not library.is_file() or not core_config.is_file() or not info.get('Core'):
		raise ValueError('Missing matching Core SDK/build library')
	actual = digest(library)
	text = core_config.read_text(encoding='utf-8-sig')
	if re.findall(r'set\(VeroScript_BUILD_ID "([0-9a-f]{64})"\)', text) != [actual]:
		raise ValueError('Core SDK build identity does not match its library')
	if re.findall(r'set\(VeroScript_VERSION "([0-9.]+)"\)', text) != ['0.1.1']:
		raise ValueError('Foundation source requires Core SDK 0.1.1')
	if info.get('CoreBuildHeader'):
		generated = sdk / 'include/Script/VeroScriptJitBuild.h'
		if (not generated.is_file() or generated.read_bytes().replace(b'\r\n', b'\n') !=
				Path(info['CoreBuildHeader']).read_bytes().replace(b'\r\n', b'\n')):
			raise ValueError('Core SDK generated ABI/implementation header differs from the binding build')
	for header in (repo / 'VeroScript/Public').rglob('*'):
		if header.is_file():
			installed = sdk / 'include/Script' / header.relative_to(repo / 'VeroScript/Public')
			if not installed.is_file() or installed.read_bytes() != header.read_bytes():
				raise ValueError('Core SDK has stale public headers: ' + str(installed))
	built = Path(info['Core'])
	if digest(built) != actual:
		if not target.startswith(('linux-', 'macos-')):
			raise ValueError('Core SDK library differs from the binding build')
		with tempfile.TemporaryDirectory(prefix='veroscript-foundation-core-', dir=scratch_directory(repo)) as temporary:
			stripped = Path(temporary) / library.name
			if target.startswith('macos-'):
				from veroscript_native_symbols import strip_macos_debug
				import shutil
				shutil.copy2(built, stripped)
				strip_macos_debug(stripped)
			else:
				subprocess.run([objcopy, '--strip-debug', str(built), str(stripped)], check=True)
			if digest(stripped) != actual:
				raise ValueError('Core SDK library differs from the binding build')
	return {'version': '0.1.1', 'buildId': actual}


def release_config(business, metadata):
	"""CMake consumers also check pinned dependencies, including same-version rebuilds."""
	base = 'VeroScript' + business
	lines = ['# SPDX-FileCopyrightText: VanstySanic', '# SPDX-License-Identifier: MIT',
			 f'set({base}_PAYLOAD_ID "{metadata["payloadId"]}")']
	for dependency, build_id in metadata['dependencyBuildIds'].items():
		if dependency in {'CoreSDK', 'Cpp'} or (business == 'Graphics' and dependency == 'Platform'):
			condition = f'"Bindings" IN_LIST {base}_FIND_COMPONENTS'
			variable = {'CoreSDK': 'VeroScript_BUILD_ID', 'Cpp': 'VeroScriptCpp_BUILD_ID',
						'Platform': 'VeroScriptPlatform_PAYLOAD_ID'}[dependency]
		else:
			condition = 'TRUE'
			variable = 'VeroScript' + dependency + '_PAYLOAD_ID'
		lines += [f'if({condition})', f'    if(NOT DEFINED {variable} OR NOT {variable} STREQUAL "{build_id}")',
				  f'        set({base}_FOUND FALSE)',
				  f'        set({base}_NOT_FOUND_MESSAGE "{dependency} version/build differs from this SDK")',
				  '        return()', '    endif()', 'endif()']
	return '\n'.join(lines) + '\n'


def assemble_foundation(repo, builds, destination, versions, target, selected, *, bindings=True, core_sdk=None, cpp_sdk=None, package_tool=None):
	from veroscript_modules import build_cache, enabled
	identities, metadata, core_contracts = {}, {}, {}
	selected = foundation_selection(selected, builds, bindings)
	with tempfile.TemporaryDirectory(prefix='veroscript-foundation-stage-', dir=scratch_directory(repo)) as temporary:
		roots = {}
		for business in selected:
			build = builds[business]
			settings = build_cache(build)
			source = Path(settings['CMAKE_HOME_DIRECTORY']).resolve()
			if source not in {repo.resolve(), (repo / 'Modules/Multimedia/VeroScriptGui').resolve(),
							  (repo / 'Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Tools/CMake/Standalone').resolve()}:
				raise ValueError('Foundation build belongs to another source tree')
			if settings.get('CMAKE_INSTALL_LIBDIR', 'lib') != 'lib':
				raise ValueError('Official foundation SDKs require CMAKE_INSTALL_LIBDIR=lib')
			info = json.loads((build / 'veroscript-foundation-Release.json').read_text(encoding='utf-8'))
			if versions[business] != info['version'] or business not in info:
				raise ValueError('Missing foundation target or source/package version mismatch: ' + business)
			if business == 'Sdl3' and not enabled(settings.get('SDL_SHARED', 'OFF')):
				raise ValueError('Official desktop SDL3 packages require SDL_SHARED=ON for one shared runtime')
			script = bindings and business != 'Sdl3'
			if script and any(name not in info for name in foundation_script_targets(business)):
				raise ValueError('Script SDK requested from a native-only foundation build')
			name = f'{FOUNDATION_PACKAGE_NAMES[business]}-{versions[business]}-{target}'
			package = Path(temporary) / name
			cmake = settings.get('CMAKE_COMMAND', 'cmake')
			for component in [('GuiNative' if business == 'Gui' else business)] + ([business + 'Bindings'] if script else []):
				subprocess.run([cmake, '--install', str(build), '--config', 'Release', '--component',
								'VeroScript' + component, '--prefix', str(package)], check=True)
			clean_functional_payload(package)
			for config_file in package.rglob('*.cmake'):
				text = config_file.read_text(encoding='utf-8-sig').replace('\\', '/').casefold()
				if any(path.as_posix().casefold() + '/' in text for path in (repo.resolve(), build.resolve())):
					raise ValueError('Foundation CMake export retains a source/build path: ' + str(config_file))
			for config_file in package.rglob('*.pc'):
				if not re.search(r'^prefix=\$\{pcfiledir\}/', config_file.read_text(encoding='utf-8-sig'), re.MULTILINE):
					raise ValueError('Foundation pkg-config export is not relocatable; rebuild with SDL_RELOCATABLE=ON')
			# Strip only staging copies. Static linkage symbols remain available.
			if target.startswith('linux-'):
				for path in sorted(package.rglob('*')):
					if path.is_file() and not path.is_symlink():
						with path.open('rb') as stream:
							magic = stream.read(8)
						if magic == b'!<arch>\n' or magic.startswith(b'\x7fELF'):
							subprocess.run([settings.get('CMAKE_OBJCOPY','objcopy'), '--strip-debug', str(path)], check=True)
			elif target.startswith('macos-'):
				from veroscript_native_symbols import strip_macos_tree
				strip_macos_tree(package, settings.get('CMAKE_STRIP','strip'))
			dependencies = foundation_dependencies(business, settings, bindings)
			meta = {'schemaVersion': 1, 'business': business, 'name': FOUNDATION_PACKAGE_NAMES[business],
					'version': versions[business], 'target': target,
					'profile': 'native-script-sdk' if script else 'native-data-sdk',
					'dependencies': {key: metadata[key]['version'] for key in dependencies},
					'dependencyBuildIds': {key: identities[key] for key in dependencies},
					'systemDependencies': []}
			if script:
				key = (info.get('Core'), str(core_sdk), target)
				if key not in core_contracts:
					core_contracts[key] = core_contract(repo, info, core_sdk, target, objcopy=settings.get('CMAKE_OBJCOPY','objcopy'))
				core = core_contracts[key]
				meta['dependencies']['CoreSDK'] = core['version']
				meta['dependencyBuildIds']['CoreSDK'] = core['buildId']
				if business == 'Gui':
					cpp = cpp_contract(repo, info, cpp_sdk, target, core, objcopy=settings.get('CMAKE_OBJCOPY','objcopy'))
					if versions.get('Cpp') != cpp['version']:
						raise ValueError('Gui/C++ Runtime package version mismatch')
					meta['dependencies']['Cpp'] = cpp['version']
					meta['dependencyBuildIds']['Cpp'] = cpp['buildId']
			if script and business in {'Gui', 'Stb', 'FontBackend', 'RmlUi'}:
				static_option = {'Gui': 'VEROSCRIPT_GUI_STATIC_TOOLS',
								 'FontBackend': 'VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER'}.get(
									 business, 'VEROSCRIPT_' + business.upper() + '_STATIC_PROVIDER')
				if enabled(settings.get(static_option, 'OFF')):
					raise ValueError('Official desktop providers require shared modules: ' + business)
				abi = core_public_abi(repo / 'VeroScript/Public')
				header = Path(info['CoreBuildHeader']).read_bytes()
				implementation = re.findall(rb'^#define VEROSCRIPT_CORE_IMPLEMENTATION_ID "([0-9a-f]{64})"\r?$', header, re.MULTILINE)
				if len(implementation) != 1:
					raise ValueError('Missing provider Core implementation build identity')
				module = 'VeroScript' + business + ('ToolsModule' if business == 'Gui' else 'Module')
				library = package / ('bin/' + module + '.dll' if target.startswith('win-') else
									 'lib/lib' + module + '.' + ('dylib' if target.startswith('macos-') else 'so'))
				meta['nativeExtensionAbi'] = abi
				meta['nativeExtensionCoreBuild'] = implementation[0].decode('ascii')
				meta['nativeExtensionBuildAbi'] = native_extension_contract(library, target, abi,
					meta['nativeExtensionCoreBuild'], versions[business], business, compiled_context=package_tool)
			if business == 'Sdl3':
				meta['features'] = {name: enabled(settings.get(option, 'OFF')) for name, option in {
					'audio': 'VEROSCRIPT_SDL3_AUDIO', 'controllers': 'VEROSCRIPT_SDL3_CONTROLLERS',
					'gpu': 'VEROSCRIPT_SDL3_GPU', 'd3d11': 'VEROSCRIPT_SDL3_D3D11',
					'opengl': 'VEROSCRIPT_SDL3_OPENGL', 'webgpu': 'VEROSCRIPT_SDL3_WEBGPU',
					'nativeExtension': 'VEROSCRIPT_SDL3_NATIVE_EXTENSION'}.items()}
				meta['features']['d3d11'] = meta['features']['d3d11'] and target.startswith('win-')
				if meta['features']['nativeExtension']:
					abi = core_public_abi(repo / 'VeroScript/Public')
					header_path = info.get('CoreBuildHeader')
					if not header_path or not Path(header_path).is_file():
						raise ValueError('Missing SDL3 generated Core build header recorded by CMake')
					header = Path(header_path)
					if re.findall(rb'^#define VEROSCRIPT_JIT_PUBLIC_ABI "([0-9a-f]{64})"\r?$',
								  header.read_bytes(), re.MULTILINE) != [abi.encode()]:
						raise ValueError('SDL3 provider uses a stale Core public ABI')
					implementations = re.findall(rb'^#define VEROSCRIPT_CORE_IMPLEMENTATION_ID "([0-9a-f]{64})"\r?$',
												 header.read_bytes(), re.MULTILINE)
					if len(implementations) != 1:
						raise ValueError('Missing SDL3 Core implementation build identity')
					implementation = implementations[0].decode('ascii')
					library = package / ('bin/VeroScriptSdl3.dll' if target.startswith('win-') else
										 'lib/libVeroScriptSdl3.' + ('dylib' if target.startswith('macos-') else 'so'))
					meta['nativeExtensionAbi'] = abi
					meta['nativeExtensionCoreBuild'] = implementation
					meta['nativeExtensionBuildAbi'] = native_extension_contract(library, target, abi, implementation, versions[business], compiled_context=package_tool)
			pin = package / f'lib/cmake/VeroScript{business}/VeroScript{business}Release.cmake'
			meta['payloadFiles'] = {path.relative_to(package).as_posix(): digest(path)
									for path in sorted(package.rglob('*')) if path.is_file()}
			meta['payloadId'] = payload_id(meta)
			pin.write_bytes(release_config(business, meta).encode('utf-8'))
			(package / 'veroscript-module.json').write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')
			entries = {path.relative_to(package).as_posix(): (binary_prefix(path) if path.stat().st_size > 1024 * 1024
				else path.read_bytes(), path.stat().st_mode) for path in package.rglob('*') if path.is_file()}
			verify_foundation_entries(entries, business, versions[business], target, repo,
									  file_digests={path.relative_to(package).as_posix(): digest(path)
													for path in package.rglob('*') if path.is_file()})
			metadata[business], identities[business] = meta, meta['payloadId']
			roots[business] = package
			archive_tree(package, destination / 'modules' / (name + ('.zip' if target.startswith('win-') else '.tar.gz')))
		if bindings and roots:
			from veroscript_foundation_packages import create_installable_foundation
			from veroscript_module_packages import package_tool as locate_tool
			create_installable_foundation(repo, roots, destination, versions, target, locate_tool(builds, package_tool))
	return identities


def verify_foundation_entries(entries, business, package_version, target, repo=None, *, file_digests=None):
	verify_module_payload(entries)
	try:
		meta = json.loads(entries['veroscript-module.json'][0])
	except (KeyError, ValueError) as error:
		raise ValueError('Missing/invalid foundation module manifest') from error
	if (not isinstance(meta, dict) or meta.get('schemaVersion') != 1 or meta.get('business') != business or
			meta.get('name') != FOUNDATION_PACKAGE_NAMES[business] or meta.get('version') != package_version or
			meta.get('target') != target or meta.get('profile') not in {'native-data-sdk', 'native-script-sdk'} or
			not re.fullmatch('[0-9a-f]{64}', meta.get('payloadId', ''))):
		raise ValueError('Foundation module identity mismatch')
	dependencies, pins = meta.get('dependencies'), meta.get('dependencyBuildIds')
	script = meta['profile'] == 'native-script-sdk'
	expected = {'CoreSDK'} if script else set()
	expected.update(foundation_dependencies(business, {}, script))
	if business == 'Gui' and script:
		expected.add('Cpp')
	if business == 'Graphics' and script:
		expected.add('Platform')
	if business == 'Sdl3':
		features = meta.get('features')
		if (not isinstance(features, dict) or set(features) != {'audio', 'controllers', 'gpu', 'd3d11', 'opengl', 'webgpu', 'nativeExtension'}
				or any(type(value) is not bool for value in features.values()) or script):
			raise ValueError('Invalid SDL3 provider features/profile')
		expected = {'Platform'} | ({'Audio'} if features['audio'] else set()) | (
			{'Graphics'} if any(features[key] for key in ('gpu', 'd3d11', 'opengl', 'webgpu')) else set())
		if features['nativeExtension'] and not re.fullmatch('[0-9a-f]{64}', meta.get('nativeExtensionAbi', '')):
			raise ValueError('Missing SDL3 native-extension ABI/build identity')
		if features['nativeExtension'] and ('nativeExtensionCoreBuild' in meta or 'nativeExtensionBuildAbi' in meta):
			if (not re.fullmatch('[0-9a-f]{64}', meta.get('nativeExtensionCoreBuild', '')) or
					not meta.get('nativeExtensionBuildAbi', '').startswith('native-extension:0.1.1:' + meta['nativeExtensionAbi'] + ':') or
					':' + meta['nativeExtensionCoreBuild'] + ':' not in meta['nativeExtensionBuildAbi']):
				raise ValueError('Invalid SDL3 native-extension build identity')
		if repo and features['nativeExtension'] and meta['nativeExtensionAbi'] != core_public_abi(repo / 'VeroScript/Public'):
			raise ValueError('Stale SDL3 native-extension ABI')
	if not isinstance(dependencies, dict) or set(dependencies) != expected or not isinstance(pins, dict) or set(pins) != expected:
		raise ValueError('Foundation dependency mismatch')
	for key, value in dependencies.items():
		version(value)
		if not re.fullmatch('[0-9a-f]{64}', pins[key]):
			raise ValueError('Invalid foundation dependency build identity')
	base = 'VeroScript' + business
	system, architecture = TARGETS[target]
	cmake = 'lib/cmake/' + base + '/'
	files = meta.get('payloadFiles')
	pin = cmake + base + 'Release.cmake'
	if (not isinstance(files, dict) or set(files) != set(entries) - {'veroscript-module.json', pin}
			or any(not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value) for value in files.values())
			or meta['payloadId'] != payload_id(meta)):
		raise ValueError('Foundation payload inventory/identity mismatch')
	for path, expected_digest in files.items():
		actual = file_digests[path] if file_digests is not None else hashlib.sha256(entries[path][0]).hexdigest()
		if actual != expected_digest:
			raise ValueError('Foundation payload checksum mismatch: ' + path)
	exports = ('lib/cmake/VeroScriptVisual/VeroScriptVisual' + business if business in {'Platform', 'Graphics', 'Audio', 'Sdl3'}
			   else cmake + base)
	headers = {'Platform': ['VeroScriptPlatformApi.h'], 'Graphics': ['VeroScriptGraphics.h', 'VeroScriptGraphics2D.h'],
			   'Audio': ['VeroScriptAudio.h'], 'Sdl3': ['VeroScriptSdl3.h', 'VeroScriptSdl3Module.h'],
			   'Image': ['VeroScriptImage.h'], 'Font': ['VeroScriptFont.h', 'VeroScriptFontLayout.h'],
			   'Stb': ['VeroScriptStb.h'], 'FontBackend': ['VeroScriptFontBackend.h'],
			   'Gui': ['VeroScriptGuiVersion.h', 'VeroScriptGuiWidget.h', 'VeroScriptGuiContext.h', 'VeroScriptGuiApplication.h'],
			   'RmlUi': ['VeroScriptRmlUi.h']}[business]
	required = ['LICENSE', cmake + base + 'Config.cmake', cmake + base + 'ConfigVersion.cmake',
				cmake + base + 'Release.cmake', exports + 'Targets.cmake']
	required += ['include/VeroScriptVisual/' + header for header in headers]
	if business != 'Sdl3':
		required.append('lib/' + (base + '.lib' if system == 'windows' else 'lib' + base + '.a'))
		if script and business not in {'Gui', 'Stb', 'FontBackend', 'RmlUi'}:
			required += ['lib/' + (base + 'Bindings.lib' if system == 'windows' else 'lib' + base + 'Bindings.a'),
						 'include/VeroScriptVisual/' + base + 'Bindings.h', exports + 'BindingsTargets.cmake']
		if script and business == 'Gui':
			required += ['lib/' + ('VeroScriptGuiScript.lib' if system == 'windows' else 'libVeroScriptGuiScript.a'),
						 'include/VeroScriptVisual/VeroScriptGuiScript.h', exports + 'BindingsTargets.cmake']
		if script and business in {'Gui', 'Stb', 'FontBackend', 'RmlUi'}:
			module = base + ('ToolsModule' if business == 'Gui' else 'Module')
			required += ['bin/' + module + '.dll' if system == 'windows' else
						 'lib/lib' + module + ('.dylib' if system == 'macos' else '.so')]
		if script and business in {'Stb', 'FontBackend', 'RmlUi'}:
			required += [exports + 'ModuleTargets.cmake']
		if script and business in {'Gui', 'Stb', 'FontBackend', 'RmlUi'}:
			if (not re.fullmatch('[0-9a-f]{64}', meta.get('nativeExtensionAbi', '')) or
					not re.fullmatch('[0-9a-f]{64}', meta.get('nativeExtensionCoreBuild', '')) or
					not meta.get('nativeExtensionBuildAbi', '').startswith('native-extension:0.1.1:' + meta['nativeExtensionAbi'] + ':') or
					':' + meta['nativeExtensionCoreBuild'] + ':' not in meta['nativeExtensionBuildAbi']):
				raise ValueError('Invalid provider native-extension build identity')
			if repo and meta['nativeExtensionAbi'] != core_public_abi(repo / 'VeroScript/Public'):
				raise ValueError('Stale provider native-extension ABI')
	else:
		required += ['bin/VeroScriptSdl3.dll' if system == 'windows' else 'lib/libVeroScriptSdl3.' + ('dylib' if system == 'macos' else 'so'),
					 'bin/SDL3.dll' if system == 'windows' else 'lib/libSDL3.' + ('dylib' if system == 'macos' else 'so'),
					 'licenses/SDL3/LICENSE.txt', 'licenses/SDL3/SDL3.version.json', 'licenses/SDL3/NOTICES.md',
					 'include/SDL3/SDL.h', ('cmake/SDL3Config.cmake' if system == 'windows' else 'lib/cmake/SDL3/SDL3Config.cmake')]
		if meta['features']['webgpu']:
			required += ['licenses/WgpuNative/WGPU-NOTICES.txt', 'licenses/WgpuNative/WgpuNative.version.json',
						 'bin/wgpu_native.dll' if system == 'windows' else 'lib/libwgpu_native.' + ('dylib' if system == 'macos' else 'so')]
	if business == 'Stb':
		required += ['licenses/stb/LICENSE', 'licenses/stb/stb.version.json']
	if business == 'Font':
		required += ['licenses/Fonts/LICENSE.Lato.txt']
	if business == 'FontBackend':
		required += ['licenses/FreeType/FTL.TXT', 'licenses/FreeType/LICENSE.TXT', 'licenses/FreeType/FreeType.version.json',
					 'licenses/HarfBuzz/COPYING', 'licenses/HarfBuzz/HarfBuzz.version.json',
					 cmake + base + 'DependenciesTargets.cmake', 'include/freetype2/ft2build.h',
					 'lib/' + ('freetype.lib' if system == 'windows' else 'libfreetype.a'),
					 'lib/' + ('VeroScriptFontBackendHarfBuzz.lib' if system == 'windows' else 'libVeroScriptFontBackendHarfBuzz.a'),
					 'lib/' + ('VeroScriptFontBackendSheenBidi.lib' if system == 'windows' else 'libVeroScriptFontBackendSheenBidi.a'),
					 'licenses/SheenBidi/LICENSE.Apache-2.0.txt', 'licenses/SheenBidi/LICENSE.Unicode-3.0.txt',
					 'licenses/SheenBidi/NOTICE.SheenBidi.txt', 'licenses/SheenBidi/SheenBidi.version.json']
	if business == 'RmlUi':
		required += ['licenses/RmlUi/LICENSE.txt', 'licenses/RmlUi/RmlUi.version.json', 'licenses/RmlUi/NOTICES.md',
					 'licenses/RmlUi/LICENSE.Containers.txt', 'licenses/RmlUi/LICENSE.Debugger.txt',
					 'lib/cmake/RmlUi/RmlUiConfig.cmake', 'lib/cmake/RmlUi/RmlUiTargets.cmake',
					 'lib/' + ('rmlui.lib' if system == 'windows' else 'librmlui.a')]
	for path in required:
		if not entries.get(path, (b'', 0))[0]:
			raise ValueError('Missing foundation module payload: ' + path)
	if business == 'Gui':
		public_version = re.findall(rb'\binline\s+constexpr\s+char\s+Version\[\]\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"',
									entries['include/VeroScriptVisual/VeroScriptGuiVersion.h'][0])
		if public_version != [package_version.encode('ascii')]:
			raise ValueError('GUI public version does not match its release version')
	if entries[cmake + base + 'Release.cmake'][0].decode('utf-8') != release_config(business, meta):
		raise ValueError('Foundation CMake dependency pins differ from the manifest')
	for path, (data, _) in entries.items():
		if data.startswith((b'MZ', b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')) and native_identity(data[:4096]) != (system, architecture):
			raise ValueError('Foundation native target mismatch: ' + path)
		if path.startswith('lib/'):
			library = re.fullmatch(r'(?:lib)?VeroScript(Platform|Graphics|Audio|Image|Font|Gui|RmlUi|Stb|FontBackend)(?:Bindings|Script)?\.(?:a|lib)',
								   PurePosixPath(path).name)
			if library and library.group(1) != business:
				raise ValueError('Foundation package contains another business library: ' + path)
	if system == 'windows':
		verify_windows_runtime(entries, (system, architecture))
	if repo:
		public = repo / FOUNDATION_MODULE_DIRECTORIES[business] / 'Public'
		for header in public.glob('*.h'):
			if header.name.endswith('Bindings.h') and not script:
				continue
			if business == 'Gui' and not script and header.name in {
					'VeroScriptGuiScript.h', 'VeroScriptGuiSchema.h', 'VeroScriptGuiCompilation.h',
					'VeroScriptGuiLocalization.h', 'VeroScriptGuiTools.h', 'VeroScriptGuiServices.h',
					'VeroScriptGuiCommandLine.h', 'VeroScriptGuiModule.h'}:
				continue
			relative = 'include/VeroScriptVisual/' + header.name
			if entries.get(relative, (b'', 0))[0] != header.read_bytes():
				raise ValueError('Stale foundation public header: ' + relative)
	return meta
