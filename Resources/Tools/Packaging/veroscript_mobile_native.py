# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Self-contained, multi-slice mobile CMake SDKs; no desktop executables.

Cpp exposes the real C++ API through static libraries. Linking the hidden C++
implementation inside an Android Interop .so is not an equivalent SDK.
Optional module packages are native-data SDKs, not desktop script plugin packs.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

from veroscript_paths import module_root
from veroscript_products import (MOBILE_EMBEDDING_BUSINESSES, MOBILE_NATIVE_BUSINESSES,
	MODULE_SOURCE_DIRECTORIES, SDK_MODULE_BUSINESSES, FEATURE_MODULE_BUSINESSES,
	clean_functional_payload, verify_functional_payload)
from veroscript_mobile import checked, verify_android_elf, verify_ios_archive, strip_mobile_debug, verify_mobile_no_debug
from veroscript_cmake_exports import normalize_curl_exports, verify_curl_exports

SLICES = {
	'android': {'android-arm64-v8a': ('arm64-v8a', 'device'), 'android-x86_64': ('x86_64', 'device')},
	'ios': {'ios-arm64': ('arm64', 'device'), 'ios-simulator-arm64': ('arm64', 'simulator'),
		'ios-simulator-x64': ('x64', 'simulator')},
}
BASE_LIBRARIES = {
	'CoreSDK': ('VeroScript',),
	'Cpp': ('VeroScript', 'VeroScriptCpp', 'VeroScriptBytecodeVm'),
	'Interop': ('VeroScript', 'VeroScriptCpp', 'VeroScriptBytecodeVm', 'VeroScriptInterop'),
	'Vm': ('VeroScriptBytecodeVm',),
}
BASE_SOURCES = {'CoreSDK': 'VeroScript', 'Cpp': 'Extensions/Language/Cpp/VeroScriptCpp',
	'Interop': 'VeroScriptInterop', 'Vm': MODULE_SOURCE_DIRECTORIES['Vm']}
BASE_INCLUDES = {'CoreSDK': 'Script', 'Cpp': 'Cpp', 'Interop': 'VeroScript', 'Vm': 'Vm'}
BASE_PACKAGES = {'CoreSDK': 'VeroScript', 'Cpp': 'VeroScriptCpp', 'Interop': 'VeroScriptInterop', 'Vm': 'VeroScriptBytecodeVm'}
PRIMARY_HEADERS = {'CoreSDK': 'VeroScript.h', 'Cpp': 'VeroScriptCpp.h', 'Interop': 'VeroScriptInterop.h',
	'Vm': 'VeroScriptBytecodeVm.h', 'Platform': 'VeroScriptPlatformApi.h', 'Gui': 'VeroScriptGuiContext.h', 'RmlUi': 'VeroScriptRmlUi.h'}
CAPABILITY_OPTIONS = ('VEROSCRIPT_NETWORK_TLS', 'VEROSCRIPT_BOOST_HAS_HTTP_SERVER', 'VEROSCRIPT_BOOST_HAS_WEBSOCKET',
	'VEROSCRIPT_WEBSOCKET_COMPRESSION', 'VEROSCRIPT_SDL3_GPU', 'VEROSCRIPT_SDL3_AUDIO',
	'VEROSCRIPT_SDL3_OPENGL', 'VEROSCRIPT_SDL3_WEBGPU', 'VEROSCRIPT_SDL3_CONTROLLERS')
MODULE_NOTICES = {
	'Json': ('licenses/VeroScriptJson/jsoncons/LICENSE', 'licenses/VeroScriptJson/quickjs-regexp/LICENSE'),
	'Boost': ('licenses/VeroScriptBoost/boost/LICENSE_1_0.txt',),
	'Curl': ('licenses/VeroScriptCurl/curl/COPYING',),
	'Sdl3': ('licenses/SDL3/LICENSE.txt', 'licenses/SDL3/SDL3.version.json', 'licenses/SDL3/NOTICES.md'),
	'Stb': ('licenses/stb/LICENSE', 'licenses/stb/stb.version.json'),
	'Font': ('licenses/Fonts/LICENSE.Lato.txt',),
	'FontBackend': ('licenses/FreeType/FTL.TXT', 'licenses/FreeType/LICENSE.TXT', 'licenses/FreeType/FreeType.version.json',
		'licenses/HarfBuzz/COPYING', 'licenses/HarfBuzz/HarfBuzz.version.json',
		'licenses/SheenBidi/LICENSE.Apache-2.0.txt', 'licenses/SheenBidi/LICENSE.Unicode-3.0.txt', 'licenses/SheenBidi/NOTICE.SheenBidi.txt'),
	'RmlUi': ('licenses/RmlUi/LICENSE.txt', 'licenses/RmlUi/RmlUi.version.json', 'licenses/RmlUi/NOTICES.md',
		'licenses/RmlUi/LICENSE.Containers.txt', 'licenses/RmlUi/LICENSE.Debugger.txt'),
}


def sdk_closure(business: str, settings: dict | None = None) -> tuple[str, ...]:
	"""Bundle native service dependencies without depending on desktop releases."""
	if business not in MOBILE_NATIVE_BUSINESSES:
		raise ValueError('Unknown mobile native SDK business')
	if business in BASE_LIBRARIES:
		return {'CoreSDK': ('CoreSDK',), 'Cpp': ('CoreSDK', 'Vm', 'Cpp'),
			'Interop': ('CoreSDK', 'Vm', 'Cpp', 'Interop'), 'Vm': ('Vm',)}[business]
	settings = settings or {}
	# D3D11 is a desktop capability. The shared dependency helper otherwise
	# consults the packaging host's os.name for this one flag.
	settings = {**settings, 'VEROSCRIPT_SDL3_D3D11': 'OFF'}
	from veroscript_modules import dependency_names, backend_services
	from veroscript_foundation import foundation_dependencies
	seen = set()
	def visit(name):
		if name in seen:
			return
		seen.add(name)
		dependencies = (dependency_names(name, backend_services(name, settings)) if name in FEATURE_MODULE_BUSINESSES
			else foundation_dependencies(name, settings, False))
		for dependency in dependencies:
			visit(dependency)
	visit(business)
	return tuple(name for name in SDK_MODULE_BUSINESSES if name in seen)


def ar_objects(data: bytes):
	"""Read GNU/BSD archive members, rejecting thin archives and LLVM bitcode."""
	if data[:8] != b'!<arch>\n':
		raise ValueError('Expected a regular static archive, not a thin archive or host library')
	cursor, objects, names = 8, 0, b''
	while cursor < len(data):
		header = checked(data, cursor, 60)
		if header[58:60] != b'`\n' or not re.fullmatch(rb' *[0-9]+ *', header[48:58]):
			raise ValueError('Invalid mobile static archive member')
		size = int(header[48:58])
		member = checked(data, cursor + 60, size)
		name = header[:16].decode('ascii').strip()
		if name.startswith('#1/'):
			length = int(name[3:])
			name = checked(member, 0, length).rstrip(b'\0').decode('utf-8')
			member = member[length:]
		if name == '//':
			names = member
		elif name not in {'/', '/SYM64/'} and not name.startswith('__.SYMDEF'):
			if re.fullmatch(r'/[0-9]+', name):
				offset = int(name[1:])
				if offset >= len(names) or b'/\n' not in names[offset:]:
					raise ValueError('Invalid GNU archive long name')
			objects += 1
			yield member
		cursor += 60 + size + (size & 1)
	if cursor != len(data) or not objects:
		raise ValueError('Empty or malformed mobile static archive')


def verify_android_archive(data: bytes, abi: str) -> int:
	machine = {'arm64-v8a': 183, 'x86_64': 62}[abi]
	count = 0
	for member in ar_objects(data):
		header = checked(member, 0, 64)
		if (header[:7] != b'\x7fELF\x02\x01\x01'
				or struct.unpack_from('<HHI', header, 16) != (1, machine, 1)):
			raise ValueError('Mobile static archive contains a host, wrong-ABI or non-ELF relocatable object')
		count += 1
	return count


def selector(package: str, target: str) -> str:
	"""Relocatable package entry; a simulator is never selected from CPU alone."""
	preamble = '# Generated mobile SDK selector.\nget_filename_component(_vero_mobile_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
	if target == 'android':
		selection = '''if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
	message(FATAL_ERROR "This SDK requires an Android NDK toolchain")
endif()
set(_vero_android_abi "${ANDROID_ABI}")
if(NOT _vero_android_abi)
	set(_vero_android_abi "${CMAKE_ANDROID_ARCH_ABI}")
endif()
if(_vero_android_abi STREQUAL "arm64-v8a")
	set(_vero_mobile_slice android-arm64-v8a)
elseif(_vero_android_abi STREQUAL "x86_64")
	set(_vero_mobile_slice android-x86_64)
else()
	message(FATAL_ERROR "Supported Android ABIs: arm64-v8a, x86_64")
endif()
set(_vero_android_api "${ANDROID_PLATFORM}")
if(NOT _vero_android_api)
	set(_vero_android_api "${CMAKE_SYSTEM_VERSION}")
endif()
string(REGEX REPLACE "^android-" "" _vero_android_api "${_vero_android_api}")
if(NOT _vero_android_api MATCHES "^[0-9]+$" OR _vero_android_api VERSION_LESS 26)
	message(FATAL_ERROR "This SDK requires Android API 26 or newer")
endif()
set(_vero_android_stl "${ANDROID_STL}")
if(NOT _vero_android_stl)
	set(_vero_android_stl "${CMAKE_ANDROID_STL_TYPE}")
endif()
if(_vero_android_stl AND NOT _vero_android_stl STREQUAL "c++_shared")
	message(FATAL_ERROR "This SDK uses c++_shared; do not mix C++ runtimes")
endif()
'''
	else:
		selection = '''if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
	message(FATAL_ERROR "This SDK requires an iOS device or simulator toolchain")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 15.0)
	message(FATAL_ERROR "This SDK requires iOS 15.0 or newer")
endif()
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
	if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
		set(_vero_mobile_slice ios-simulator-arm64)
	elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
		set(_vero_mobile_slice ios-simulator-x64)
	else()
		message(FATAL_ERROR "Select one simulator architecture per CMake build: arm64 or x86_64")
	endif()
elseif(CMAKE_OSX_SYSROOT MATCHES "[Ii][Pp]hone[Oo][Ss]" AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
	set(_vero_mobile_slice ios-arm64)
else()
	message(FATAL_ERROR "Select the iPhoneOS arm64 SDK or an explicit simulator SDK")
endif()
'''
	# The included config may load another selector. Use a package-specific saved
	# prefix and avoid global slice variables after returning from that include.
	return preamble + selection + f'''list(PREPEND CMAKE_PREFIX_PATH "${{_vero_mobile_root}}/slices/${{_vero_mobile_slice}}")
include("${{_vero_mobile_root}}/slices/${{_vero_mobile_slice}}/lib/cmake/{package}/{package}Config.cmake")
'''


def base_config(business: str, native_version: str, build_id: str) -> str:
	"""Native runtime imports only; Core's desktop config also requires Tooling."""
	package = BASE_PACKAGES[business]
	text = '# Generated mobile runtime SDK.\nget_filename_component(_vero_native_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\ninclude(CMakeFindDependencyMacro)\n'
	if business == 'CoreSDK':
		text += 'find_dependency(Threads)\n'
	elif business == 'Vm':
		text += f'find_dependency(VeroScriptExecutionContract {native_version} EXACT CONFIG)\n'
	else:
		text += f'find_dependency(VeroScript {native_version} EXACT CONFIG)\n'
		if business == 'Cpp':
			text += f'find_dependency(VeroScriptBytecodeVm {native_version} EXACT CONFIG)\n'
		if business == 'Interop':
			text += f'find_dependency(VeroScriptCpp {native_version} EXACT CONFIG)\nfind_dependency(VeroScriptBytecodeVm {native_version} EXACT CONFIG)\n'
	# find_dependency() is a macro and its configs may change the working prefix.
	text += 'get_filename_component(_vero_native_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)\n'
	library = {'CoreSDK': 'VeroScript', 'Cpp': 'VeroScriptCpp', 'Interop': 'VeroScriptInterop', 'Vm': 'VeroScriptBytecodeVm'}[business]
	links = {'CoreSDK': 'Threads::Threads;${CMAKE_DL_LIBS}', 'Cpp': 'VeroScript::VeroScript',
		'Interop': 'VeroScript::VeroScriptCpp;VeroScript::VeroScriptBytecodeVm', 'Vm': 'VeroScript::VeroScriptExecutionContract'}[business]
	text += f'''if(TARGET VeroScript::{library})
  get_target_property(_vero_existing_contract VeroScript::{library} VEROSCRIPT_MOBILE_SOURCE_ID)
  if(NOT _vero_existing_contract STREQUAL "{build_id}")
    message(FATAL_ERROR "Conflicting desktop/mobile runtime SDKs or source ABIs in one application")
  endif()
endif()
if(NOT TARGET VeroScript::{library})
	add_library(VeroScript::{library} STATIC IMPORTED)
	set_target_properties(VeroScript::{library} PROPERTIES
		IMPORTED_LOCATION "${{_vero_native_prefix}}/lib/lib{library}.a"
		INTERFACE_INCLUDE_DIRECTORIES "${{_vero_native_prefix}}/include/{BASE_INCLUDES[business]}"
		INTERFACE_COMPILE_FEATURES cxx_std_17
		INTERFACE_LINK_LIBRARIES "{links}")
endif()
set({package}_VERSION "{native_version}")
set({package}_FOUND TRUE)
set({package}_BUILD_ID "{build_id}")
'''
	text += f'set_property(TARGET VeroScript::{library} PROPERTY VEROSCRIPT_MOBILE_SOURCE_ID "{build_id}")\n'
	if business == 'Interop':
		text += 'set_property(TARGET VeroScript::VeroScriptInterop APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS VS_INTEROP_STATIC=1)\n'
	if business == 'Cpp':
		text += '''set(VeroScriptCpp_Runtime_FOUND TRUE)
set(VeroScriptCpp_Tools_FOUND FALSE)
set(VeroScriptCpp_Aot_FOUND FALSE)
foreach(_vero_component IN LISTS VeroScriptCpp_FIND_COMPONENTS)
	if(NOT _vero_component STREQUAL "Runtime" AND VeroScriptCpp_FIND_REQUIRED_${_vero_component})
		set(VeroScriptCpp_FOUND FALSE)
		set(VeroScriptCpp_NOT_FOUND_MESSAGE "Mobile SDK ships Runtime; use desktop tools for source generation")
		return()
	endif()
endforeach()
'''
	return text


def version_config(native_version: str) -> str:
	return f'''set(PACKAGE_VERSION "{native_version}")
set(PACKAGE_VERSION_COMPATIBLE FALSE)
set(PACKAGE_VERSION_EXACT FALSE)
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
	set(PACKAGE_VERSION_COMPATIBLE TRUE)
	set(PACKAGE_VERSION_EXACT TRUE)
elseif(NOT PACKAGE_FIND_VERSION)
	set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
'''


def parse_slice_paths(values, target: str) -> dict[str, Path]:
	result = {}
	for value in values:
		name, separator, path = value.partition('=')
		if not separator or name not in SLICES[target] or name in result or not path:
			raise ValueError('Expected unique slice=directory inputs for the requested mobile platform')
		result[name] = Path(path).absolute()
	if result and set(result) != set(SLICES[target]):
		raise ValueError('Every Android ABI / iOS device and simulator slice is required')
	return result


def android_build_settings(settings: dict) -> dict:
	"""Support both NDK toolchain aliases and CMake's built-in Android keys."""
	for first, second in (('ANDROID_ABI', 'CMAKE_ANDROID_ARCH_ABI'), ('ANDROID_STL', 'CMAKE_ANDROID_STL_TYPE'),
		('ANDROID_PLATFORM', 'CMAKE_SYSTEM_VERSION')):
		left, right = settings.get(first), settings.get(second)
		if left and right and str(left).removeprefix('android-') != str(right).removeprefix('android-'):
			raise ValueError('Conflicting Android build cache fields: ' + first + '/' + second)
	return {
		'abi': settings.get('ANDROID_ABI') or settings.get('CMAKE_ANDROID_ARCH_ABI'),
		'api': str(settings.get('ANDROID_PLATFORM') or settings.get('CMAKE_SYSTEM_VERSION') or '').removeprefix('android-'),
		'stl': settings.get('ANDROID_STL') or settings.get('CMAKE_ANDROID_STL_TYPE') or '',
		'ndk': settings.get('CMAKE_ANDROID_NDK') or settings.get('ANDROID_NDK') or '',
	}


def guard_build_input(path: Path) -> Path:
	"""Developer build/toolchain trees are not shipped; normal SDK links are valid."""
	try:
		resolved = path.resolve(strict=True)
	except (OSError, RuntimeError) as error:
		raise ValueError('Missing native build/toolchain input: ' + str(path)) from error
	if not resolved.is_dir():
		raise ValueError('Native build/toolchain input must be a directory: ' + str(path))
	return resolved


def validate_ndk(path: Path) -> Path:
	ndk = guard_build_input(path)
	if not (ndk / 'build/cmake/android.toolchain.cmake').is_file():
		raise ValueError('Missing Android NDK toolchain file')
	properties = (ndk / 'source.properties').read_text(encoding='utf-8')
	if not re.search(r'^Pkg\.Revision\s*=\s*28\.2\.13676358\s*$', properties, re.MULTILINE):
		raise ValueError('Mobile native SDK requires the pinned Android NDK r28c')
	return ndk


def check_build(repo: Path, build: Path, target: str, slice_name: str) -> dict:
	from veroscript_modules import build_cache, enabled
	guard_build_input(build)
	settings = build_cache(build)
	if Path(settings.get('CMAKE_HOME_DIRECTORY', '')).resolve() != module_root(repo).resolve():
		raise ValueError('Mobile native SDK build belongs to another source tree')
	configurations = settings.get('CMAKE_CONFIGURATION_TYPES', '').split(';')
	release_configured = ('Release' in configurations if any(configurations)
		else settings.get('CMAKE_BUILD_TYPE') == 'Release')
	if (not release_configured
			or not enabled(settings.get('VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY', 'OFF'))
			or settings.get('CMAKE_INSTALL_LIBDIR', 'lib') != 'lib'):
		raise ValueError('Expected a Release embedded build with lib install layout')
	for key in ('VEROSCRIPT_BUILD_APPLICATION', 'VEROSCRIPT_BUILD_PACKAGE_MANAGER', 'VEROSCRIPT_BUILD_PACKAGE_RUNTIME',
		'VEROSCRIPT_BUILD_BUILD_SYSTEM', 'VEROSCRIPT_BUILD_MOBILE_ADAPTERS', 'VS_BUILD_VEROSCRIPT_PYTHON_ONLY',
		'VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY', 'VEROSCRIPT_BUILD_ASMJIT_BACKEND', 'VEROSCRIPT_ENABLE_ASMJIT',
		'VEROSCRIPT_JSON_BUILD_BINDINGS', 'VEROSCRIPT_NETWORK_BUILD_BINDINGS', 'VEROSCRIPT_VISUAL_SCRIPT_BINDINGS',
		'VEROSCRIPT_GUI_SCRIPTING', 'VEROSCRIPT_SDL3_NATIVE_EXTENSION', 'VS_ENABLE_IPO', 'CMAKE_INTERPROCEDURAL_OPTIMIZATION'):
		if enabled(settings.get(key, 'OFF')):
			raise ValueError('Mobile native/data SDK build must disable ' + key)
	if not enabled(settings.get('VEROSCRIPT_BUILD_VM_BACKEND', 'OFF')) or not enabled(settings.get('VEROSCRIPT_INTEROP_STATIC', 'OFF')):
		raise ValueError('Mobile C++ SDK requires the static VM and static Interop build profile')
	arch, variant = SLICES[target][slice_name]
	if target == 'android':
		android = android_build_settings(settings)
		if (settings.get('CMAKE_SYSTEM_NAME') != 'Android' or android['abi'] != arch
				or android['api'] != '26' or android['stl'] != 'c++_shared' or not android['ndk']):
			raise ValueError('Android native SDK requires matching ABI, API 26 and shared libc++')
		validate_ndk(Path(android['ndk']))
	else:
		sdk = settings.get('CMAKE_OSX_SYSROOT', '').lower()
		if (settings.get('CMAKE_SYSTEM_NAME') != 'iOS' or settings.get('CMAKE_OSX_DEPLOYMENT_TARGET') != '15.0'
				or settings.get('CMAKE_OSX_ARCHITECTURES') != ('x86_64' if arch == 'x64' else arch)
				or ('iphonesimulator' not in sdk if variant == 'simulator' else 'iphoneos' not in sdk)):
			raise ValueError('Wrong iOS device/simulator, architecture or minimum OS build')
	return settings


def find_library(build: Path, name: str) -> Path:
	# All root native targets use lib[/Release]. Never search dependencies or
	# another configuration and accidentally pick up a host-tool library.
	choices = [build / 'lib' / configuration / ('lib' + name + '.a')
		for configuration in ('Release', 'Release-iphoneos', 'Release-iphonesimulator', '')]
	paths = [path for path in choices if path.is_file()]
	if len(paths) != 1:
		raise ValueError('Expected exactly one Release static library: ' + name)
	return paths[0]


def install_base(repo: Path, build: Path, prefix: Path, business: str, native_version: str) -> None:
	from veroscript_mobile_release import load_tool
	builder = load_tool('Resources/Tools/Packaging/package-veroscript-mobile.py', repo)
	contract = builder.aot_source_contract(repo)
	header = build / 'VeroScript/Generated/VeroScriptJitBuild.h'
	if not builder.matching_aot_header(header, contract):
		raise ValueError('Mobile C++ build has stale Core headers/implementation identity')
	for name in sdk_closure(business):
		for library in BASE_LIBRARIES[name]:
			destination = prefix / 'lib' / ('lib' + library + '.a')
			destination.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(find_library(build, library), destination)
		public = repo / BASE_SOURCES[name] / 'Public'
		copy_public_headers(public, prefix / 'include' / BASE_INCLUDES[name])
		package = BASE_PACKAGES[name]
		config = prefix / 'lib/cmake' / package
		config.mkdir(parents=True, exist_ok=True)
		(config / (package + 'Config.cmake')).write_text(base_config(name, native_version, core_link_identity(contract)), encoding='utf-8')
		(config / (package + 'ConfigVersion.cmake')).write_text(version_config(native_version), encoding='utf-8')
	# The VM depends on the public execution contract but never on Core objects.
	copy_public_headers(repo / 'VeroScript/Public', prefix / 'include/Script')
	shutil.copy2(header, prefix / 'include/Script/VeroScriptJitBuild.h')
	config = prefix / 'lib/cmake/VeroScriptExecutionContract'
	config.mkdir(parents=True, exist_ok=True)
	(config / 'VeroScriptExecutionContractConfig.cmake').write_text(execution_contract_config(core_link_identity(contract)), encoding='utf-8')
	(config / 'VeroScriptExecutionContractConfigVersion.cmake').write_text(version_config(native_version), encoding='utf-8')


def copy_public_headers(public: Path, destination: Path) -> None:
	for path in public.rglob('*'):
		if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
			raise ValueError('Linked mobile SDK public-header source')
		if path.is_file() and path.suffix == '.h':
			output = destination / path.relative_to(public)
			output.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(path, output)


def core_link_identity(contract: dict) -> str:
	return hashlib.sha256(json.dumps(contract, sort_keys=True, separators=(',', ':')).encode('ascii')).hexdigest()


def execution_contract_config(build_id: str) -> str:
	return f'''get_filename_component(_vero_native_prefix "${{CMAKE_CURRENT_LIST_DIR}}/../../.." ABSOLUTE)
if(TARGET VeroScript::VeroScriptExecutionContract)
  get_target_property(_vero_existing_contract VeroScript::VeroScriptExecutionContract VEROSCRIPT_MOBILE_SOURCE_ID)
  if(NOT _vero_existing_contract STREQUAL "{build_id}")
    message(FATAL_ERROR "Conflicting mobile execution contract headers")
  endif()
else()
  add_library(VeroScript::VeroScriptExecutionContract INTERFACE IMPORTED)
  set_target_properties(VeroScript::VeroScriptExecutionContract PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${{_vero_native_prefix}}/include/Script" INTERFACE_COMPILE_FEATURES cxx_std_17
    VEROSCRIPT_MOBILE_SOURCE_ID "{build_id}")
endif()
'''


def native_libraries(prefix: Path, target: str, slice_name: str) -> dict:
	arch, variant = SLICES[target][slice_name]
	identities = {}
	for path in sorted(prefix.rglob('*')):
		if not path.is_file():
			continue
		with path.open('rb') as stream:
			magic = stream.read(8)
		name = path.relative_to(prefix).as_posix()
		if path.suffix == '.a':
			data = path.read_bytes()
			verify_mobile_no_debug(data, target)
			count = verify_android_archive(data, arch) if target == 'android' else verify_ios_archive(path, arch, variant == 'simulator')
			identities[name] = {'kind': 'static', 'objects': count}
		elif path.suffix == '.so' and target == 'android':
			data = path.read_bytes()
			verify_mobile_no_debug(data, target)
			identities[name] = {'kind': 'shared', **verify_android_elf(data, arch)}
		elif (magic.startswith((b'\x7fELF', b'MZ', b'!<arch>', b'!<thin>', b'BC\xc0\xde', b'\x00asm'))
				or magic[:4] in {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}
				or path.suffix.lower() in {'.dll', '.dylib', '.exe', '.o', '.obj', '.lib', '.bc', '.pdb'}):
			raise ValueError('Unexpected executable/object in mobile SDK: ' + name)
	return identities


def required_service_libraries(business: str, target: str) -> set[str]:
	"""Mirror the public target's real library kind, including the Android SDL bridge."""
	if business in BASE_LIBRARIES:
		return {'lib/lib' + library + '.a' for library in BASE_LIBRARIES[business]}
	suffix = '.so' if business == 'Sdl3' and target == 'android' else '.a'
	return {'lib/libVeroScript' + business + suffix}


def source_header_map(repo: Path, prefix: Path, businesses) -> dict:
	"""Compare installed project-owned headers with the current public sources."""
	sources = {}
	for business in businesses:
		public = repo / (BASE_SOURCES.get(business) or MODULE_SOURCE_DIRECTORIES[business]) / 'Public'
		for path in public.rglob('*.h'):
			sources.setdefault(path.name, []).append(path)
	# Core headers are the VM execution contract as well.
	if any(b in MOBILE_EMBEDDING_BUSINESSES for b in businesses):
		for path in (repo / 'VeroScript/Public').rglob('*.h'):
			sources.setdefault(path.name, []).append(path)
	result = {}
	for path in (prefix / 'include').rglob('*.h'):
		if path.name not in sources:
			continue  # Third-party/generated headers have the complete payload inventory.
		matches = [source for source in sources[path.name] if source.read_bytes() == path.read_bytes()]
		if not matches:
			raise ValueError('Stale mobile SDK public header: ' + path.name)
		result[path.relative_to(prefix).as_posix()] = matches[0].relative_to(repo).as_posix()
	return result


def sdk_capabilities(bundled, settings: dict) -> dict:
	"""Describe services in this SDK, rather than unrelated targets in its build."""
	from veroscript_modules import enabled
	active = set()
	if 'Boost' in bundled:
		active.update(CAPABILITY_OPTIONS[:4])
	if 'Sdl3' in bundled:
		active.update(CAPABILITY_OPTIONS[4:])
	capabilities = {key: key in active and enabled(settings.get(key, 'OFF')) for key in CAPABILITY_OPTIONS}
	capabilities['VEROSCRIPT_WEBSOCKET_COMPRESSION'] &= capabilities['VEROSCRIPT_BOOST_HAS_WEBSOCKET']
	return capabilities


def embedding_resources(repo: Path, business: str) -> dict[str, bytes]:
	if business not in {'CoreSDK', 'Cpp', 'Interop'}:
		return {}
	files = {'stdlib/' + path.relative_to(repo / 'VeroScript/Resources/Scripts/stdlib').as_posix(): path.read_bytes()
		for path in (repo / 'VeroScript/Resources/Scripts/stdlib').rglob('*') if path.is_file()}
	for component in sdk_closure(business):
		for path in (repo / BASE_SOURCES[component] / 'Resources/Localization').rglob('*.json'):
			name = 'localization/' + path.relative_to(repo / BASE_SOURCES[component] / 'Resources/Localization').as_posix()
			if name in files:
				raise ValueError('Duplicate mobile embedding localization file')
			files[name] = path.read_bytes()
	return files


def stage_native_sdk(repo: Path, output: Path, business: str, target: str, builds: dict, native_version: str) -> None:
	from veroscript_mobile_release import payload_files
	from veroscript_modules import build_cache, enabled
	if business not in MOBILE_NATIVE_BUSINESSES or set(builds) != set(SLICES[target]):
		raise ValueError('Mobile SDK requires a supported business and every platform slice')
	if output.exists():
		raise ValueError('Choose a new mobile native SDK directory')
	closures, headers, records, packages, capabilities = None, {}, {}, None, None
	for name, build in builds.items():
		settings = check_build(repo, build, target, name)
		closure = sdk_closure(business, settings)
		if 'Curl' in closure and not enabled(settings.get('VEROSCRIPT_CURL_BUNDLED', 'OFF')):
			raise ValueError('Mobile Curl SDKs require the target-built, bundled curl SDK/export')
		if 'Sdl3' in closure and (enabled(settings.get('SDL_SHARED', 'OFF')) or not enabled(settings.get('SDL_STATIC', 'OFF'))):
			raise ValueError('Mobile SDL SDKs require SDL_SHARED=OFF and SDL_STATIC=ON')
		if closures is not None and closure != closures:
			raise ValueError('Mobile SDK slices have different service dependencies')
		closures = closure
		flags = sdk_capabilities(closure, settings)
		if capabilities is not None and capabilities != flags:
			raise ValueError('Mobile slices have different native SDK capabilities')
		capabilities = flags
		prefix = output / 'slices' / name
		prefix.mkdir(parents=True)
		if business in MOBILE_EMBEDDING_BUSINESSES:
			install_base(repo, build, prefix, business, native_version)
		else:
			for component in closure:
				subprocess.run([settings.get('CMAKE_COMMAND', 'cmake'), '--install', str(build), '--config', 'Release',
					'--component', 'VeroScript' + ('GuiNative' if component == 'Gui' else component), '--prefix', str(prefix)], check=True)
			if 'Curl' in closure and enabled(settings.get('VEROSCRIPT_CURL_BUNDLED', 'OFF')):
				curl_build = Path(settings['CURL_BINARY_DIR'])
				guard_build_input(curl_build)
				subprocess.run([settings.get('CMAKE_COMMAND', 'cmake'), '--install', str(curl_build), '--config', 'Release', '--prefix', str(prefix)], check=True)
				for helper in ('bin/curl-config', 'lib/pkgconfig/libcurl.pc'):
					(prefix / helper).unlink(missing_ok=True)
			clean_functional_payload(prefix)
		normalize_curl_exports(prefix)
		if target == 'android':
			# Static libraries use the application's single shared C++ runtime.
			# NDK r28c supplies the matching, 16-KB-aligned deployment library.
			ndk = Path(android_build_settings(settings)['ndk'])
			triple = {'arm64-v8a': 'aarch64-linux-android', 'x86_64': 'x86_64-linux-android'}[SLICES[target][name][0]]
			choices = list(ndk.glob('toolchains/llvm/prebuilt/*/sysroot/usr/lib/' + triple + '/libc++_shared.so'))
			if len(choices) != 1:
				raise ValueError('Missing unambiguous NDK shared C++ runtime')
			shutil.copy2(choices[0], prefix / 'lib/libc++_shared.so')
			for filename in ('NOTICE', 'NOTICE.toolchain'):
				shutil.copy2(ndk / filename, prefix / filename)
		for library in sorted((prefix / 'lib').rglob('*')):
			if library.is_file() and library.suffix in {'.a', '.so'}:
				strip_mobile_debug(library, target, settings.get('CMAKE_STRIP') if target == 'android' else None)
		headers[name] = source_header_map(repo, prefix, closure)
		records[name] = native_libraries(prefix, target, name)
		found = {path.parent.name for path in (prefix / 'lib/cmake').glob('*/*Config.cmake')}
		if packages is not None and packages != found:
			raise ValueError('Mobile slices expose different CMake packages')
		packages = found
		for path in prefix.rglob('*.cmake'):
			text = path.read_text(encoding='utf-8-sig').replace('\\', '/').casefold()
			if any(directory.resolve().as_posix().casefold() + '/' in text for directory in (repo, build, prefix)):
				raise ValueError('Mobile CMake export retains a source/build/staging path')
	for package in sorted(packages or ()):
		config = output / 'lib/cmake' / package
		config.mkdir(parents=True)
		(config / (package + 'Config.cmake')).write_text(selector(package, target), encoding='utf-8')
		# Keep dependency versions from the actual source-built package.
		shutil.copy2(output / 'slices' / next(iter(SLICES[target])) / 'lib/cmake' / package / (package + 'ConfigVersion.cmake'), config)
	shutil.copy2(repo / 'LICENSE', output / 'LICENSE')
	for route, data in embedding_resources(repo, business).items():
		path = output / 'assets' / route
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)
	metadata = {'schemaVersion': 1, 'product': 'VeroScript Mobile Native SDK', 'business': business,
		'target': target, 'nativeVersion': native_version, 'profile': 'embedding-static-sdk' if business in MOBILE_EMBEDDING_BUSINESSES else 'native-data-sdk',
		'bundledBusinesses': list(closures), 'packages': sorted(packages or ()), 'slices': records, 'sourceHeaders': headers,
		'capabilities': capabilities, 'bundledNativeVersions': dict.fromkeys(closures, native_version),
		'externalDependencies': ['OpenSSL >= 3.0 target SDK/runtime'] if 'Curl' in closures or 'Boost' in closures and capabilities['VEROSCRIPT_NETWORK_TLS'] else [],
		'minimumApi': 26 if target == 'android' else None, 'minimumOS': '15.0' if target == 'ios' else None,
		'toolchain': 'android-ndk-28.2.13676358' if target == 'android' else 'xcode-ios',
		'targetAcceptance': 'required', 'files': payload_files(output)}
	if business in MOBILE_EMBEDDING_BUSINESSES:
		from veroscript_mobile_release import load_tool
		metadata['coreContract'] = load_tool('Resources/Tools/Packaging/package-veroscript-mobile.py', repo).aot_source_contract(repo)
	(output / 'mobile-native-sdk.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
	verify_native_cmake_sdk(output, business, target, native_version, repo, require_consumer=False)


def verify_native_cmake_sdk(root: Path, business: str, target: str, native_version: str, repo: Path | None = None, *, require_consumer: bool = True) -> None:
	from veroscript_mobile_release import verify_inventory, payload_files, portable_member
	if business not in MOBILE_NATIVE_BUSINESSES or target not in SLICES:
		raise ValueError('Unsupported mobile native SDK business/target')
	metadata = verify_inventory(root, 'mobile-native-sdk.json')
	allowed_roots = {'mobile-native-sdk.json', 'LICENSE', 'slices', 'lib'} | ({'assets'} if business in {'CoreSDK', 'Cpp', 'Interop'} else set())
	if {path.name for path in root.iterdir()} != allowed_roots or not (root / 'LICENSE').read_bytes():
		raise ValueError('Unexpected or incomplete mobile native SDK payload')
	expected = {'product': 'VeroScript Mobile Native SDK', 'business': business, 'target': target, 'nativeVersion': native_version,
		'profile': 'embedding-static-sdk' if business in MOBILE_EMBEDDING_BUSINESSES else 'native-data-sdk',
		'minimumApi': 26 if target == 'android' else None, 'minimumOS': '15.0' if target == 'ios' else None,
		'toolchain': 'android-ndk-28.2.13676358' if target == 'android' else 'xcode-ios', 'targetAcceptance': 'required'}
	if any(metadata.get(key) != value for key, value in expected.items()):
		raise ValueError('Mobile native SDK identity/profile/toolchain mismatch')
	if require_consumer:
		verify_consumer_validation(metadata.get('consumerValidation'), target, repo)
	if set(metadata.get('slices', {})) != set(SLICES[target]) or set(metadata.get('sourceHeaders', {})) != set(SLICES[target]):
		raise ValueError('Incomplete mobile native SDK ABI/device/simulator matrix')
	if {path.name for path in (root / 'slices').iterdir()} != set(SLICES[target]):
		raise ValueError('Unexpected mobile SDK slice directory')
	bundled = metadata.get('bundledBusinesses')
	if (not isinstance(bundled, list) or len(bundled) != len(set(bundled)) or business not in bundled
			or any(name not in MOBILE_NATIVE_BUSINESSES for name in bundled)
			or business in MOBILE_EMBEDDING_BUSINESSES and tuple(bundled) != sdk_closure(business)):
		raise ValueError('Invalid mobile native SDK dependency closure')
	if metadata.get('bundledNativeVersions') != dict.fromkeys(bundled, native_version):
		raise ValueError('Mobile SDK bundled native component versions disagree')
	flags = metadata.get('capabilities')
	if not isinstance(flags, dict) or set(flags) != set(CAPABILITY_OPTIONS) or any(type(value) is not bool for value in flags.values()):
		raise ValueError('Missing or invalid mobile SDK build capabilities')
	if flags != sdk_capabilities(bundled, {key: 'ON' if value else 'OFF' for key, value in flags.items()}):
		raise ValueError('Mobile SDK advertises capabilities from an unbundled provider')
	if tuple(bundled) != sdk_closure(business, {key: 'ON' if value else 'OFF' for key, value in flags.items()}):
		raise ValueError('Mobile SDK is missing native service dependencies')
	external = ['OpenSSL >= 3.0 target SDK/runtime'] if 'Curl' in bundled or 'Boost' in bundled and flags['VEROSCRIPT_NETWORK_TLS'] else []
	if metadata.get('externalDependencies') != external:
		raise ValueError('Mobile SDK external dependency contract mismatch')
	contract = metadata.get('coreContract', {})
	if business in MOBILE_EMBEDDING_BUSINESSES:
		if (set(contract) != {'VEROSCRIPT_JIT_PUBLIC_ABI', 'VEROSCRIPT_CORE_IMPLEMENTATION_ID'}
				or any(not re.fullmatch('[0-9a-f]{64}', str(value)) for value in contract.values())):
			raise ValueError('Missing mobile embedding Core ABI/implementation identity')
		if repo:
			from veroscript_mobile_release import load_tool
			if contract != load_tool('Resources/Tools/Packaging/package-veroscript-mobile.py', repo).aot_source_contract(module_root(repo)):
				raise ValueError('Stale mobile embedding Core ABI/implementation identity')
	packages = metadata.get('packages')
	if not isinstance(packages, list) or not packages or len(packages) != len(set(packages)) or any(not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', p) for p in packages):
		raise ValueError('Missing or invalid mobile CMake package inventory')
	if {path.name for path in (root / 'lib/cmake').iterdir()} != set(packages):
		raise ValueError('Unexpected mobile SDK CMake selector')
	for package in packages:
		if (root / 'lib/cmake' / package / (package + 'Config.cmake')).read_text(encoding='utf-8') != selector(package, target):
			raise ValueError('Invalid/non-relocatable mobile CMake selector')
	for name, declared in metadata['slices'].items():
		prefix = root / 'slices' / name
		verify_curl_exports(prefix)
		if native_libraries(prefix, target, name) != declared:
			raise ValueError('Mobile native SDK binary identity/inventory mismatch')
		for component in bundled:
			if not required_service_libraries(component, target).issubset(declared):
				raise ValueError('Missing bundled mobile SDK native service library: ' + component)
		for component, required in {'FontBackend': ('lib/libfreetype.a', 'lib/libVeroScriptFontBackendHarfBuzz.a', 'lib/libVeroScriptFontBackendSheenBidi.a'),
			'RmlUi': ('lib/librmlui.a',), 'Curl': ('lib/libcurl.a',),
			'Boost': (('lib/veroscript/modules/libVeroScriptBoostRuntime.so' if target == 'android'
				else 'lib/libVeroScriptBoostRuntime.a'),)}.items():
			if component in bundled and any(path not in declared for path in required):
				raise ValueError('Missing bundled mobile provider dependency library: ' + component)
		if 'Sdl3' in bundled and not {'lib/libSDL3.a', 'lib/libSDL3-static.a'}.intersection(declared):
			raise ValueError('Missing bundled mobile SDL static runtime')
		if target == 'android' and 'lib/libc++_shared.so' not in declared:
			raise ValueError('Missing mobile SDK shared C++ runtime')
		if any(not path.startswith('lib/') for path in declared):
			raise ValueError('Native mobile SDK libraries must stay under their slice lib directory')
		for component in bundled:
			for notice in MODULE_NOTICES.get(component, ()):
				if not (prefix / notice).is_file() or not (prefix / notice).read_bytes():
					raise ValueError('Missing mobile native SDK redistribution notice: ' + notice)
		if 'Boost' in bundled and flags['VEROSCRIPT_NETWORK_TLS'] and not (prefix / 'licenses/VeroScriptBoost/openssl/OpenSSL-LICENSE.txt').is_file():
			raise ValueError('Missing mobile Boost OpenSSL notice')
		if target == 'android' and any(not (prefix / filename).is_file() or not (prefix / filename).read_bytes() for filename in ('NOTICE', 'NOTICE.toolchain')):
			raise ValueError('Missing Android native SDK toolchain notices')
		actual_packages = {path.parent.name for path in (prefix / 'lib/cmake').glob('*/*Config.cmake')}
		if actual_packages != set(packages):
			raise ValueError('Incomplete mobile CMake package slice')
		mapping = metadata['sourceHeaders'][name]
		if not isinstance(mapping, dict) or not mapping:
			raise ValueError('Missing current public-header inventory')
		for component in bundled:
			header = PRIMARY_HEADERS.get(component, 'VeroScript' + component + '.h')
			if not any(Path(path).name == header for path in mapping):
				raise ValueError('Missing mobile native SDK primary header: ' + header)
		for path, source in mapping.items():
			if portable_member(path) != path or portable_member(source) != source or not path.startswith('include/'):
				raise ValueError('Invalid mobile native SDK source/header path')
			if not (prefix / path).is_file() or repo and (prefix / path).read_bytes() != (module_root(repo) / source).read_bytes():
				raise ValueError('Stale mobile native SDK public headers')
		if repo and mapping != source_header_map(module_root(repo), prefix, bundled):
			raise ValueError('Mobile native SDK public-header provenance mismatch')
		if business in MOBILE_EMBEDDING_BUSINESSES:
			header = (prefix / 'include/Script/VeroScriptJitBuild.h').read_bytes()
			if (prefix / 'lib/cmake/VeroScriptExecutionContract/VeroScriptExecutionContractConfig.cmake').read_text(encoding='utf-8') != execution_contract_config(core_link_identity(contract)):
				raise ValueError('Missing or changed mobile VM execution-contract export')
			for key, value in contract.items():
				if re.findall(rb'^#define ' + key.encode() + rb' "([0-9a-f]{64})"\r?$', header, re.MULTILINE) != [value.encode()]:
					raise ValueError('Mobile SDK generated Core identity/header mismatch')
			for component in bundled:
				package = BASE_PACKAGES[component]
				path = prefix / 'lib/cmake' / package / (package + 'Config.cmake')
				if path.read_text(encoding='utf-8') != base_config(component, native_version, core_link_identity(contract)):
					raise ValueError('Mobile embedding CMake dependency/link contract mismatch')
				if path.with_name(package + 'ConfigVersion.cmake').read_text(encoding='utf-8') != version_config(native_version):
					raise ValueError('Mobile embedding package/native version mismatch')
				if repo:
					public = module_root(repo) / BASE_SOURCES[component] / 'Public'
					for source in public.rglob('*.h'):
						installed = prefix / 'include' / BASE_INCLUDES[component] / source.relative_to(public)
						if not installed.is_file() or installed.read_bytes() != source.read_bytes():
							raise ValueError('Incomplete mobile embedding public-header tree')
		for package in packages:
			version_path = Path('lib/cmake') / package / (package + 'ConfigVersion.cmake')
			if (root / version_path).read_bytes() != (prefix / version_path).read_bytes():
				raise ValueError('Mobile package-version selectors disagree across slices')
	if repo and (root / 'LICENSE').read_bytes() != (module_root(repo) / 'LICENSE').read_bytes():
		raise ValueError('Stale mobile native SDK project license')
	if repo and business in {'CoreSDK', 'Cpp', 'Interop'}:
		actual = {name: (root / 'assets' / name).read_bytes() for name in payload_files(root / 'assets')}
		if actual != embedding_resources(module_root(repo), business):
			raise ValueError('Incomplete or stale mobile embedding resources')
	if business in {'CoreSDK', 'Cpp', 'Interop'} and not (root / 'assets/stdlib/core.vs').is_file():
		raise ValueError('Missing mobile embedding standard library')
	# Extra binaries cannot be authorized by adding them to the file inventory.
	for path in root.rglob('*'):
		if path.is_file() and not path.relative_to(root).as_posix().startswith('slices/'):
			with path.open('rb') as stream:
				magic = stream.read(8)
			if magic.startswith((b'MZ', b'\x7fELF', b'!<arch>', b'!<thin>', b'\xcf\xfa\xed\xfe', b'BC\xc0\xde')):
				raise ValueError('Unexpected native binary outside mobile SDK slices')
	verify_functional_payload(payload_files(root))


def verify_consumer_validation(validation, target: str, repo: Path | None = None) -> None:
	if (not isinstance(validation, dict) or validation.get('profile') != 'whole-archive-cross-link'
			or validation.get('executed') is not False or not isinstance(validation.get('slices'), dict)
			or set(validation['slices']) != set(SLICES[target])
			or any(not re.fullmatch('[0-9a-f]{64}', str(value)) for value in validation['slices'].values())
			or not re.fullmatch('[0-9a-f]{64}', str(validation.get('templateSha256')))):
		raise ValueError('Mobile SDK requires successful cross-link qualification for every target slice')
	if repo and validation['templateSha256'] != hashlib.sha256((module_root(repo) / 'Resources/Tools/Packaging/MobileNativeConsumer/CMakeLists.txt').read_bytes()).hexdigest():
		raise ValueError('Stale mobile SDK cross-link consumer template')


def consumer_executable(build: Path, target: str, slice_name: str) -> Path:
	value = (build / 'consumer-Release.txt').read_text(encoding='utf-8').strip()
	base = build.resolve()
	binary = Path(value)
	if not binary.is_absolute() or not binary.resolve().is_relative_to(base):
		raise ValueError('Mobile SDK consumer executable is outside its build directory: ' + str(binary))
	parts = binary.resolve().relative_to(base).parts
	if target == 'ios':
		variant = SLICES['ios'][slice_name][1]
		suffix = '-iphoneos' if variant == 'device' else '-iphonesimulator'
		# Xcode leaves its platform build setting in TARGET_FILE generator output.
		parts = tuple('Release' + suffix if part in ('Release${EFFECTIVE_PLATFORM_NAME}',
			'Release$(EFFECTIVE_PLATFORM_NAME)') else part for part in parts)
	binary = base.joinpath(*parts)
	if not binary.resolve().is_relative_to(base):
		raise ValueError('Mobile SDK consumer executable is outside its build directory: ' + str(binary))
	if not binary.is_file():
		raise ValueError('Mobile SDK consumer did not produce its target executable: ' + str(binary))
	return binary


def qualify_sdk(repo: Path, root: Path, business: str, target: str, builds: dict, work: Path) -> dict:
	"""Cross-compile/link a consumer of every SDK slice; never execute target code."""
	from veroscript_modules import build_cache
	package = BASE_PACKAGES.get(business, 'VeroScript' + business)
	target_name = 'VeroScript::' + ({'CoreSDK': 'VeroScript', 'Cpp': 'VeroScriptCpp',
		'Interop': 'VeroScriptInterop', 'Vm': 'VeroScriptBytecodeVm'}.get(business, business))
	header = PRIMARY_HEADERS.get(business, 'VeroScript' + business + '.h')
	metadata = json.loads((root / 'mobile-native-sdk.json').read_text(encoding='utf-8'))
	results = {}
	for name in SLICES[target]:
		settings = build_cache(builds[name])
		prefixes = [root.resolve().as_posix(), settings.get('CMAKE_PREFIX_PATH', '')]
		build = work / (business + '-' + name)
		arguments = ['-S', str(repo / 'Resources/Tools/Packaging/MobileNativeConsumer'), '-B', str(build),
			'-G', 'Ninja' if target == 'android' else 'Xcode', '-DCMAKE_BUILD_TYPE=Release',
			'-DCMAKE_PREFIX_PATH=' + ';'.join(prefix for prefix in prefixes if prefix),
			'-DVERO_SDK_PACKAGE=' + package, '-DVERO_SDK_TARGET=' + target_name,
			'-DVERO_SDK_HEADER=' + header, '-DVERO_SDK_VERSION=' + metadata['nativeVersion']]
		if business in {'Cpp', 'Interop'}:
			arguments += ['-DVERO_SDK_VM=VeroScript::VeroScriptBytecodeVm']
		if metadata['externalDependencies']:
			arguments += ['-DVERO_SDK_OPENSSL=ON']
			# Reused native builds own these target dependencies. A desktop
			# OPENSSL_ROOT_DIR in the publishing shell must not redirect discovery.
			for key in ('OPENSSL_ROOT_DIR', 'OPENSSL_INCLUDE_DIR', 'OPENSSL_SSL_LIBRARY', 'OPENSSL_CRYPTO_LIBRARY'):
				if settings.get(key) and not settings[key].endswith('-NOTFOUND'):
					arguments += ['-D' + key + ':PATH=' + settings[key]]
		arguments += ['-DCMAKE_FIND_ROOT_PATH=' + ';'.join([root.resolve().as_posix(), settings.get('CMAKE_FIND_ROOT_PATH', '')]),
			'-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY']
		for key in ('CMAKE_SYSTEM_NAME', 'CMAKE_TOOLCHAIN_FILE', 'CMAKE_OSX_SYSROOT', 'CMAKE_OSX_ARCHITECTURES',
			'CMAKE_OSX_DEPLOYMENT_TARGET', 'ANDROID_ABI', 'ANDROID_PLATFORM', 'ANDROID_STL'):
			if settings.get(key):
				arguments += ['-D' + key + '=' + settings[key]]
		if target == 'android':
			android = android_build_settings(settings)
			arguments += ['-DCMAKE_ANDROID_NDK=' + android['ndk'], '-DANDROID_ABI=' + android['abi'],
				'-DANDROID_PLATFORM=android-' + android['api'], '-DCMAKE_SYSTEM_VERSION=' + android['api'],
				'-DANDROID_STL=' + android['stl'], '-DCMAKE_ANDROID_STL_TYPE=' + android['stl']]
			if not settings.get('CMAKE_TOOLCHAIN_FILE'):
				arguments += ['-DCMAKE_TOOLCHAIN_FILE=' + str(Path(android['ndk']) / 'build/cmake/android.toolchain.cmake')]
		if target == 'ios':
			arguments += ['-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO', '-DCMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE=NO']
		cmake = settings.get('CMAKE_COMMAND', 'cmake')
		environment = os.environ.copy()
		environment.pop('OPENSSL_ROOT_DIR', None)
		subprocess.run([cmake, *arguments], check=True, env=environment)
		subprocess.run([cmake, '--build', str(build), '--config', 'Release', '--target', 'VeroScriptMobileSdkConsumer'], check=True, env=environment)
		binary = consumer_executable(build, target, name)
		results[name] = hashlib.sha256(binary.read_bytes()).hexdigest()
	return {'profile': 'whole-archive-cross-link', 'executed': False, 'slices': results,
		'templateSha256': hashlib.sha256((repo / 'Resources/Tools/Packaging/MobileNativeConsumer/CMakeLists.txt').read_bytes()).hexdigest()}
