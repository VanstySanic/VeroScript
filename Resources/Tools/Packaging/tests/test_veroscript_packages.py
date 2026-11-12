# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Package-client deployment and native SDK dependency contracts."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from subprocess import CompletedProcess

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_packages import finalize_package_manager_sdk, stage_package_client, stage_package_manager_sdk, verify_package_client_payload


class PackageClientTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix='vero package sdk ')
		self.addCleanup(temporary.cleanup)
		self.root = Path(temporary.name)

	def test_sdk_identity_uses_published_library_and_independent_core_version(self):
		for library in ('VeroScript.lib', 'libVeroScript.a'):
			stage = self.root / library
			config = stage / 'lib/cmake/VeroScriptPackageManager/VeroScriptPackageManagerConfig.cmake'
			identity = config.with_name('VeroScriptPackageManagerCoreBuild.cmake')
			config.parent.mkdir(parents=True)
			(stage / 'lib' / library).write_bytes(b'published core without debug information')
			config.write_text('find_dependency(VeroScript 0.1.1 EXACT CONFIG)\n')
			identity.write_text('set(VeroScriptPackageManager_CORE_BUILD_ID "original-unstripped-build")\n')
			finalize_package_manager_sdk(stage, '2.3.4')
			self.assertIn('find_dependency(VeroScript 2.3.4 EXACT CONFIG)', config.read_text())
			self.assertIn(hashlib.sha256(b'published core without debug information').hexdigest(), identity.read_text())
			(stage / 'lib' / library).write_bytes(b'new production library')
			finalize_package_manager_sdk(stage, '2.3.4')
			self.assertIn(hashlib.sha256(b'new production library').hexdigest(), identity.read_text())

	def test_sdk_audit_rejects_missing_dependencies_and_stale_core_identity(self):
		prefix = 'sdk/'
		build_id = 'a' * 64
		descriptor = {'format': 'veroscript-component', 'formatVersion': '0.1.1', 'role': 'core-sdk',
					  'root': '..', 'target': {'os': 'linux', 'architecture': 'x64'}}
		config = 'lib/cmake/VeroScriptPackageManager/'
		files = {prefix + name: (b'functional content', 0) for name in (
			'licenses/package-manager/json/LICENSE.MIT', 'licenses/package-manager/miniz/LICENSE',
			'licenses/package-manager/sha256/LICENSE', 'licenses/package-manager/curl/COPYING',
			'licenses/package-manager/sources.json', 'include/PackageManager/VeroScriptPackageManager.h',
			'lib/libVeroScriptPackageManager.a', 'lib/libVeroScriptPackageZip.a',
			config + 'VeroScriptPackageManagerConfigVersion.cmake', config + 'VeroScriptPackageManagerTargets.cmake')}
		files[prefix + 'bin/veroscript-component.json'] = (json.dumps(descriptor).encode(), 0)
		files[prefix + config + 'VeroScriptPackageManagerConfig.cmake'] = (b'find_dependency(VeroScript 2.3.4 EXACT CONFIG)', 0)
		identity = prefix + config + 'VeroScriptPackageManagerCoreBuild.cmake'
		files[identity] = (f'set(VeroScriptPackageManager_CORE_BUILD_ID "{build_id}")'.encode(), 0)
		files[prefix + 'sdk-manifest.json'] = (b'{"packageManager":true}', 0)
		verify_package_client_payload(files, prefix, 'core-sdk', core_version='2.3.4', build_id=build_id)
		curl_config = prefix + 'lib/cmake/CURL/CURLConfig.cmake'
		files[curl_config] = (b'set(CURL_LIBRARIES_PRIVATE "/retired/openssl/lib/libssl.a")', 0)
		with self.assertRaisesRegex(ValueError, 'Non-relocatable CURL'):
			verify_package_client_payload(files, prefix, 'core-sdk', core_version='2.3.4', build_id=build_id)
		files.pop(curl_config)
		for helper in ('bin/curl-config', 'lib/pkgconfig/libcurl.pc'):
			files[prefix + helper] = (b'prefix=/original/build/prefix', 0)
			with self.assertRaisesRegex(ValueError, 'configure-prefix curl helper'):
				verify_package_client_payload(files, prefix, 'core-sdk', core_version='2.3.4', build_id=build_id)
			files.pop(prefix + helper)
		descriptor_name = prefix + 'bin/veroscript-component.json'
		removed = files.pop(descriptor_name)
		with self.assertRaisesRegex(ValueError, 'declared package features'):
			verify_package_client_payload(files, prefix, 'core-sdk', core_version='2.3.4', build_id=build_id)
		files[descriptor_name] = removed
		for name in ('lib/libVeroScriptPackageManager.a', 'licenses/package-manager/curl/COPYING'):
			removed = files.pop(prefix + name)
			with self.assertRaisesRegex(ValueError, 'Missing package-client payload'):
				verify_package_client_payload(files, prefix, 'core-sdk', core_version='2.3.4', build_id=build_id)
			files[prefix + name] = removed
		with self.assertRaisesRegex(ValueError, 'version/build identity mismatch'):
			verify_package_client_payload(files, prefix, 'core-sdk', core_version='2.3.5', build_id=build_id)
		with self.assertRaisesRegex(ValueError, 'version/build identity mismatch'):
			verify_package_client_payload(files, prefix, 'core-sdk', core_version='2.3.4', build_id='b' * 64)

	def test_stage_removes_dependency_helpers_and_normalizes_curl_config(self):
		build = self.root / 'build'
		build.mkdir()
		(build / 'CMakeCache.txt').write_text('VEROSCRIPT_BUILD_PACKAGE_MANAGER:BOOL=ON\n')
		stage = self.root / 'stage'
		config = stage / 'lib/cmake/CURL/CURLConfig.cmake'
		config.parent.mkdir(parents=True)
		config.write_text('set(CURL_LIBRARIES CURL::libcurl)\nset(CURL_LIBRARIES_PRIVATE "/retired/openssl/lib/libssl.a")\n')
		for relative in ('bin/curl-config', 'lib/pkgconfig/libcurl.pc'):
			path = stage / relative
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text('prefix=/original/build/prefix')
		with patch('veroscript_applications.stage_application_sdk'), patch('veroscript_packages.subprocess.run'):
			stage_package_manager_sdk(build, stage)
		self.assertNotIn('/retired/', config.read_text())
		self.assertIn('get_target_property(CURL_LIBRARIES_PRIVATE CURL::libcurl INTERFACE_LINK_LIBRARIES)', config.read_text())
		self.assertFalse((stage / 'bin/curl-config').exists())
		self.assertFalse((stage / 'lib/pkgconfig/libcurl.pc').exists())

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for the installed package consumer')
	def test_finalized_package_manager_public_cmake_entry_survives_relocation_without_curl_helpers(self):
		stage = self.root / 'original SDK prefix'
		cmake = stage / 'lib/cmake'
		package = cmake / 'VeroScriptPackageManager'
		package.mkdir(parents=True)
		library = stage / 'lib/libVeroScript.a'
		library.write_bytes(b'consumer fixture core library')
		build_id = hashlib.sha256(library.read_bytes()).hexdigest()
		for name in ('VeroScript', 'VeroScriptPackageRuntime'):
			directory = cmake / name
			directory.mkdir()
			body = f'add_library({name}::{name} INTERFACE IMPORTED)\n'
			if name == 'VeroScript':
				body += f'set(VeroScript_BUILD_ID "{build_id}")\n'
			else:
				body = 'include(CMakeFindDependencyMacro)\nfind_dependency(VeroScript 0.1.1 EXACT CONFIG)\n' + body
			(directory / (name + 'Config.cmake')).write_text(body)
			(directory / (name + 'ConfigVersion.cmake')).write_text(
				'set(PACKAGE_VERSION "0.1.1")\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\nset(PACKAGE_VERSION_EXACT TRUE)\n')
		(package / 'VeroScriptPackageManagerCoreBuild.cmake').write_text('original identity\n')
		(package / 'VeroScriptPackageManagerTargets.cmake').write_text(
			'add_library(VeroScriptPackageManager::VeroScriptPackageManager INTERFACE IMPORTED)\n'
			'set_target_properties(VeroScriptPackageManager::VeroScriptPackageManager PROPERTIES INTERFACE_LINK_LIBRARIES CURL::libcurl)\n')
		repo = Path(__file__).resolve().parents[4]
		template = repo / 'VeroScriptPackageManager/Resources/Tools/CMake/VeroScriptPackageManagerConfig.cmake.in'
		prepare = self.root / 'prepare.cmake'
		prepare.write_text('include(CMakePackageConfigHelpers)\nset(VEROSCRIPT_PACKAGE_MANAGER_HAS_VM OFF)\n'
			f'configure_package_config_file("{template.as_posix()}" "{(package / "VeroScriptPackageManagerConfig.cmake").as_posix()}" INSTALL_DESTINATION lib/cmake/VeroScriptPackageManager)\n')
		subprocess.run(['cmake', '-P', str(prepare)], check=True, capture_output=True, text=True)
		curl = cmake / 'CURL'
		curl.mkdir()
		(curl / 'CURLConfigVersion.cmake').write_text('set(PACKAGE_VERSION "8.14.1")\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
		(curl / 'CURLConfig.cmake').write_text(
			'include(CMakeFindDependencyMacro)\nfind_dependency(OpenSSL)\nfind_dependency(Threads)\n'
			'add_library(CURL::libcurl INTERFACE IMPORTED)\n'
			'set_target_properties(CURL::libcurl PROPERTIES INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto;Threads::Threads")\n'
			'set(CURL_LIBRARIES CURL::libcurl)\nset(CURL_LIBRARIES_PRIVATE "/retired/openssl/lib/libssl.a;/retired/openssl/lib/libcrypto.a")\n')
		for relative in ('bin/curl-config', 'lib/pkgconfig/libcurl.pc'):
			path = stage / relative
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text('prefix=/retired/configure/prefix')
		finalize_package_manager_sdk(stage, '0.1.1')
		first = (curl / 'CURLConfig.cmake').read_bytes()
		finalize_package_manager_sdk(stage, '0.1.1')
		self.assertEqual((curl / 'CURLConfig.cmake').read_bytes(), first)
		moved = self.root / 'relocated SDK with spaces'
		stage.rename(moved)
		consumer = self.root / 'consumer'
		consumer.mkdir()
		modules = consumer / 'cmake'
		modules.mkdir()
		(modules / 'FindOpenSSL.cmake').write_text('set(OpenSSL_FOUND TRUE)\nadd_library(OpenSSL::SSL INTERFACE IMPORTED)\nadd_library(OpenSSL::Crypto INTERFACE IMPORTED)\n')
		(modules / 'FindThreads.cmake').write_text('set(Threads_FOUND TRUE)\nadd_library(Threads::Threads INTERFACE IMPORTED)\n')
		(consumer / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\nproject(RelocatedPackageManager NONE)\n'
			'list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")\nfind_package(VeroScriptPackageManager REQUIRED CONFIG)\n'
			'if(NOT CURL_LIBRARIES_PRIVATE STREQUAL "OpenSSL::SSL;OpenSSL::Crypto;Threads::Threads")\n message(FATAL_ERROR "Private CURL dependency target graph was not preserved")\nendif()\n'
			'if(EXISTS "${CMAKE_PREFIX_PATH}/bin/curl-config" OR EXISTS "${CMAKE_PREFIX_PATH}/lib/pkgconfig/libcurl.pc")\n message(FATAL_ERROR "Unexpected configure-prefix helper")\nendif()\n')
		result = subprocess.run(['cmake', '-S', str(consumer), '-B', str(self.root / 'consumer build'),
			'-G', 'Ninja', '-DCMAKE_PREFIX_PATH=' + str(moved)], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_component_descriptor_and_notices_are_staged_and_root_is_verified(self):
		for role in ('runtime', 'core-sdk'):
			root = self.root / role
			cli = root / ('bin/veroscript' if role == 'core-sdk' else 'veroscript')
			cli.parent.mkdir(parents=True)
			repo = self.root / ('repo-' + role)
			for name in ('json/LICENSE.MIT', 'miniz/LICENSE', 'sha256/LICENSE', 'curl/COPYING', 'sources.json'):
				path = repo / 'VeroScriptPackageManager/ThirdParty' / name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_text('legal notice')
			replies = [CompletedProcess([], 0, json.dumps({'target': 'linux-x64'}), ''),
					   CompletedProcess([], 0, json.dumps({'scope': 'sdk' if role == 'core-sdk' else 'runtime',
														   'root': str(root)}), '')]
			with patch('veroscript_packages.subprocess.run', side_effect=replies):
				stage_package_client(repo, cli, root, role)
			value = json.loads((cli.parent / 'veroscript-component.json').read_text())
			self.assertEqual(value['root'], '..' if role == 'core-sdk' else '.')
			self.assertTrue((root / 'licenses/package-manager/curl/COPYING').is_file())
			files = {path.relative_to(root).as_posix(): (path.read_bytes(), 0)
					 for path in root.rglob('*') if path.is_file()}
			if role == 'runtime':
				verify_package_client_payload(files, '', role)


if __name__ == '__main__':
	unittest.main()
