# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import io
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_foundation import archive_digests, core_contract, payload_id, release_config, verify_foundation_entries, foundation_selection, native_extension_contract, assemble_foundation
from veroscript_release import core_public_abi
from veroscript_products import FOUNDATION_BUSINESSES, FOUNDATION_PACKAGE_NAMES, INSTALLER_BUSINESSES, business_route
from veroscript_business_release import artifact_business, collection_index, foundation_dependency_closure, verify_collection
from foundation_fixtures import foundation_files
import test_veroscript_business_release as business_tests
from test_veroscript_release import elf


class FoundationReleaseTests(unittest.TestCase):
	def test_public_module_and_contract_versions_match_release_versions(self):
		import re
		repo = Path(__file__).resolve().parents[4]
		versions = json.loads((repo / 'Resources/Tools/Packaging/ReleaseVersions.json').read_text(encoding='utf-8'))['businesses']
		headers = {'Platform': 'Modules/Multimedia/VeroScriptPlatform/Public/VeroScriptPlatformApi.h',
				   'Graphics': 'Modules/Multimedia/VeroScriptGraphics/Public/VeroScriptGraphics.h',
				   'Audio': 'Modules/Multimedia/VeroScriptAudio/Public/VeroScriptAudio.h',
				   'Sdl3': 'Modules/Multimedia/Backend/VeroScriptSdl3/Public/VeroScriptSdl3.h'}
		for business, path in headers.items():
			with self.subTest(business=business):
				source = (repo / path).read_text(encoding='utf-8')
				self.assertEqual(re.findall(r'\bModuleVersion\[\]\s*=\s*"([^"]+)"', source), [versions[business]])
				if business != 'Sdl3':
					self.assertRegex(source, r'\bContractVersion\s*=\s*ModuleVersion\s*;')

	def files(self, business='Sdl3'):
		return foundation_files(business, {name: '0.1.1' for name in FOUNDATION_BUSINESSES},
								'linux-x64', elf(), b'MIT notice')

	def verify(self, files, business='Sdl3'):
		return verify_foundation_entries({name: (value, 0o755) for name, value in files.items()}, business, '0.1.1', 'linux-x64')

	def fixture(self):
		fixture = business_tests.BusinessReleaseTests()
		fixture.setUp()
		self.addCleanup(fixture.doCleanups)
		return fixture

	def test_names_routes_and_no_installers(self):
		for business, name in FOUNDATION_PACKAGE_NAMES.items():
			self.assertEqual(artifact_business(name + '-0.1.1-linux-x64.tar.gz'), business)
			group = 'Backend/' if business in {'Sdl3','Stb','FontBackend','RmlUi'} else ''
			self.assertEqual(business_route(business), 'Modules/Multimedia/' + group + business)
			self.assertNotIn(business, INSTALLER_BUSINESSES)
		self.assertNotIn(artifact_business('veroscript-visual-linux-sdk-0.1.1.tar.gz'), FOUNDATION_BUSINESSES)

	def test_native_services_have_no_core_or_backend_dependency(self):
		for business in ('Platform', 'Graphics', 'Audio', 'Image', 'Font'):
			self.assertEqual(self.verify(self.files(business), business)['dependencies'], {})

	def test_font_backend_identity_and_dependency_ownership(self):
		self.assertEqual(FOUNDATION_PACKAGE_NAMES['FontBackend'], 'vs-font-backend')
		self.assertNotIn('FtHb', FOUNDATION_BUSINESSES)
		for suffix in ('linux-x64.tar.gz', 'win-x64.zip', 'macos-arm64.tar.gz'):
			for business in ('Font', 'FontBackend'):
				self.assertEqual(artifact_business(FOUNDATION_PACKAGE_NAMES[business] + '-0.1.1-' + suffix), business)
		font, backend = self.files('Font'), self.files('FontBackend')
		self.assertFalse(any(name.startswith('licenses/SheenBidi/') for name in font))
		self.assertNotIn('lib/libVeroScriptFontBackendSheenBidi.a', font)
		metadata = self.verify(backend, 'FontBackend')
		self.assertEqual(metadata['dependencies'], {'Font': '0.1.1'})
		self.assertIn('lib/libVeroScriptFontBackendSheenBidi.a', backend)

	def test_font_and_backend_archives_cannot_be_mixed(self):
		for business, library in [('Font', 'lib/libVeroScriptFontBackend.a'),
								  ('FontBackend', 'lib/libVeroScriptFont.a')]:
			with self.subTest(business=business):
				files = self.files(business)
				files[library] = b'ar'
				metadata = json.loads(files['veroscript-module.json'])
				metadata['payloadFiles'][library] = hashlib.sha256(files[library]).hexdigest()
				metadata['payloadId'] = payload_id(metadata)
				files['veroscript-module.json'] = json.dumps(metadata).encode()
				files[f'lib/cmake/VeroScript{business}/VeroScript{business}Release.cmake'] = release_config(business, metadata).encode()
				with self.assertRaisesRegex(ValueError, 'another business library'):
					self.verify(files, business)

	def test_gui_selection_adds_transitive_services_without_backends(self):
		from unittest.mock import patch
		with patch('veroscript_modules.build_cache', return_value={}):
			builds = dict.fromkeys(FOUNDATION_BUSINESSES, Path('unused'))
			self.assertEqual(foundation_selection(['RmlUi'], builds, False),
							 ['Platform', 'Graphics', 'Image', 'Font', 'Gui', 'RmlUi'])
			self.assertEqual(foundation_selection(['Gui'], builds, False),
							 ['Platform', 'Graphics', 'Image', 'Font', 'Gui'])
			self.assertEqual(foundation_selection(['Stb', 'FontBackend'], builds, False),
							 ['Image', 'Font', 'Stb', 'FontBackend'])

	def test_gui_and_resource_native_payloads_and_notices(self):
		for business in ('Image', 'Font', 'Stb', 'FontBackend', 'Gui', 'RmlUi'):
			with self.subTest(business=business):
				self.verify(self.files(business), business)
		for business, notice in [('Stb', 'licenses/stb/LICENSE'), ('FontBackend', 'licenses/HarfBuzz/COPYING'),
								 ('FontBackend', 'licenses/SheenBidi/LICENSE.Apache-2.0.txt'),
								 ('FontBackend', 'licenses/SheenBidi/LICENSE.Unicode-3.0.txt'),
								 ('FontBackend', 'licenses/SheenBidi/NOTICE.SheenBidi.txt'),
								 ('FontBackend', 'licenses/SheenBidi/SheenBidi.version.json'),
								 ('FontBackend', 'lib/libVeroScriptFontBackendSheenBidi.a'),
								 ('RmlUi', 'licenses/RmlUi/LICENSE.Containers.txt'),
								 ('Gui', 'include/VeroScriptVisual/VeroScriptGuiVersion.h')]:
			files = self.files(business)
			del files[notice]
			meta = json.loads(files['veroscript-module.json'])
			del meta['payloadFiles'][notice]
			meta['payloadId'] = payload_id(meta)
			files['veroscript-module.json'] = json.dumps(meta).encode()
			files[f'lib/cmake/VeroScript{business}/VeroScript{business}Release.cmake'] = release_config(business, meta).encode()
			with self.assertRaisesRegex(ValueError, 'Missing foundation module payload'):
				self.verify(files, business)

	def test_gui_public_version_matches_its_release_version(self):
		files = self.files('Gui')
		header = 'include/VeroScriptVisual/VeroScriptGuiVersion.h'
		files[header] = files[header].replace(b'0.1.1', b'0.1.2')
		metadata = json.loads(files['veroscript-module.json'])
		metadata['payloadFiles'][header] = hashlib.sha256(files[header]).hexdigest()
		metadata['payloadId'] = payload_id(metadata)
		files['veroscript-module.json'] = json.dumps(metadata).encode()
		files['lib/cmake/VeroScriptGui/VeroScriptGuiRelease.cmake'] = release_config('Gui', metadata).encode()
		with self.assertRaisesRegex(ValueError, 'GUI public version does not match its release version'):
			self.verify(files, 'Gui')

	def test_gui_publication_requires_transitive_dependencies(self):
		fixture = self.fixture()
		with self.assertRaisesRegex(ValueError, 'dependency version/build is missing'):
			fixture.publish(fixture.candidate('gui-missing-dependencies'), ['Gui', 'RmlUi'])
		fixture.publish(fixture.candidate('gui-complete'), ['Platform', 'Graphics', 'Image', 'Font', 'Gui', 'RmlUi'])
		verify_collection(fixture.root, 'linux-x64')

	def test_gui_script_manifest_requires_the_cpp_business(self):
		files = self.files('Gui')
		metadata = json.loads(files['veroscript-module.json'])
		metadata['profile'] = 'native-script-sdk'
		metadata['dependencies']['CoreSDK'] = '0.1.1'
		metadata['dependencyBuildIds']['CoreSDK'] = 'a' * 64
		files['veroscript-module.json'] = json.dumps(metadata).encode()
		with self.assertRaisesRegex(ValueError, 'Foundation dependency mismatch'):
			self.verify(files, 'Gui')
		metadata['dependencies']['Cpp'] = '0.1.1'
		metadata['dependencyBuildIds']['Cpp'] = 'a' * 64
		configuration = release_config('Gui', metadata)
		self.assertIn('VeroScriptCpp_BUILD_ID', configuration)
		self.assertIn('"Bindings" IN_LIST VeroScriptGui_FIND_COMPONENTS', configuration)

	def test_native_descriptor_roles_match_each_gui_and_resource_module(self):
		from types import SimpleNamespace
		from unittest.mock import patch
		abi, implementation = 'a' * 64, 'b' * 64
		for business, module_id, legacy_id, roles in [
				('Sdl3', 'veroscript-sdl3', 'veroscript.sdl3', 5),
				('Gui', 'veroscript-gui-tools', 'veroscript.gui.tools', 8),
				('RmlUi', 'veroscript-rmlui', 'veroscript.rmlui', 4),
				('Stb', 'veroscript-stb', 'veroscript.stb', 4),
				('FontBackend', 'veroscript-font-backend', 'veroscript.font-backend', 5)]:
			with self.subTest(business=business):
				descriptor_id = module_id

				def query(api, size, output):
					descriptor = output._obj
					descriptor.size, descriptor.api = size, api
					descriptor.abi = ('native-extension:0.1.1:' + abi + ':test:' + implementation + ':release').encode()
					descriptor.core, descriptor.platform = implementation.encode(), b'win-x64'
					descriptor.id, descriptor.version = descriptor_id.encode(), b'0.1.1'
					descriptor.roles, descriptor.create, descriptor.destroy = roles, 1, 1
					return api
				module = SimpleNamespace(veroscript_extension_query=query, _handle=1)
				close = '_ctypes.FreeLibrary' if os.name == 'nt' else '_ctypes.dlclose'
				with patch('veroscript_foundation.ctypes.CDLL', return_value=module), patch('veroscript_foundation.' + close):
					result = native_extension_contract(Path('provider'), 'win-x64', abi, implementation, '0.1.1', business)
					self.assertIn(implementation, result)
					with self.assertRaisesRegex(ValueError, 'descriptor differs'):
						native_extension_contract(Path('provider'), 'linux-x64', abi, implementation, '0.1.1', business)
					descriptor_id = legacy_id
					with self.assertRaisesRegex(ValueError, 'descriptor differs'):
						native_extension_contract(Path('provider'), 'win-x64', abi, implementation, '0.1.1', business)

	def test_gui_required_same_version_resource_build_is_retained(self):
		fixture = self.fixture()
		fixture.publish(fixture.candidate('gui-initial'), ['Platform', 'Graphics', 'Image', 'Font', 'Gui', 'RmlUi'])
		index = collection_index(fixture.root, 'linux-x64')
		before = index.read_bytes()
		candidate = fixture.candidate('gui-wrong-image')
		manifest_path = candidate / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text())
		manifest['businessBuildIds']['Image'] = 'f' * 64
		manifest_path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'payload identity mismatch'):
			fixture.publish(candidate, ['Image'])
		self.assertEqual(index.read_bytes(), before)

	def test_indirect_historical_core_dependency_is_preserved(self):
		core = {'business': 'CoreSDK', 'version': '0.1.1', 'buildId': 'a' * 64}
		graphics = {'business': 'Graphics', 'version': '0.1.1', 'buildId': 'b' * 64,
					'dependencies': {'CoreSDK': {'version': core['version'], 'buildId': core['buildId']}}}
		backend = {'business': 'Sdl3', 'version': '0.1.1', 'buildId': 'c' * 64,
				   'dependencies': {'Graphics': {'version': graphics['version'], 'buildId': graphics['buildId']}}}
		dependencies = {('Graphics', '0.1.1'): graphics, ('CoreSDK', '0.1.1'): core}
		result = foundation_dependency_closure([backend], lambda name, value: dependencies[(name, value)])
		self.assertEqual({item['business'] for item in result}, {'Sdl3', 'Graphics', 'CoreSDK'})
		dependencies[('CoreSDK', '0.1.1')] = {**core, 'buildId': 'd' * 64}
		with self.assertRaisesRegex(ValueError, 'dependency build mismatch'):
			foundation_dependency_closure([backend], lambda name, value: dependencies[(name, value)])

	@unittest.skipUnless(os.name != 'nt' and all(shutil.which(name) for name in ('c++', 'ar', 'objcopy')),
						 'Linux symbol processing requires native binutils')
	def test_matching_core_sdk_may_have_debug_information_removed(self):
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary)
			public = repo / 'VeroScript/Public'
			public.mkdir(parents=True)
			(public / 'VeroScriptVersion.h').write_text('// Test public header\n')
			source = repo / 'core.cpp'
			source.write_text('int core_answer() { return 42; }\n')
			subprocess.run(['c++', '-g', '-c', str(source), '-o', str(repo / 'core.o')], check=True)
			built = repo / 'libVeroScript.a'
			subprocess.run(['ar', 'rcs', str(built), str(repo / 'core.o')], check=True)
			sdk = repo / 'sdk'
			shutil.copytree(public, sdk / 'include/Script')
			config = sdk / 'lib/cmake/VeroScript'
			config.mkdir(parents=True)
			library = sdk / 'lib/libVeroScript.a'
			subprocess.run(['objcopy', '--strip-debug', str(built), str(library)], check=True)
			identity = hashlib.sha256(library.read_bytes()).hexdigest()
			(config / 'VeroScriptConfig.cmake').write_text(
				f'set(VeroScript_VERSION "0.1.1")\nset(VeroScript_BUILD_ID "{identity}")\n')
			self.assertEqual(core_contract(repo, {'Core': str(built)}, sdk, 'linux-x64')['buildId'], identity)

	def test_manifest_dependency_and_cmake_pins_must_agree(self):
		files = self.files()
		self.verify(files)
		for key, value in (('name', 'vs-visual'), ('target', 'linux-arm64'), ('dependencyBuildIds', {})):
			with self.subTest(key=key):
				changed = dict(files)
				meta = json.loads(changed['veroscript-module.json'])
				meta[key] = value
				changed['veroscript-module.json'] = json.dumps(meta).encode()
				with self.assertRaises(ValueError):
					self.verify(changed)
		changed = dict(files)
		changed['lib/cmake/VeroScriptSdl3/VeroScriptSdl3Release.cmake'] = b'wrong dependency pin'
		with self.assertRaisesRegex(ValueError, 'CMake dependency pins'):
			self.verify(changed)

	def test_sdl3_staging_uses_the_recorded_core_header(self):
		from unittest.mock import patch
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary).resolve()
			public = repo / 'VeroScript/Public'
			public.mkdir(parents=True)
			(public / 'VeroScriptVersion.h').write_text('// fixture\n', encoding='utf-8')
			abi, implementation = core_public_abi(public), 'b' * 64
			header = repo / 'embedded core/Generated ABI.h'
			header.parent.mkdir()
			header.write_text(f'#define VEROSCRIPT_JIT_PUBLIC_ABI "{abi}"\n'
							  f'#define VEROSCRIPT_CORE_IMPLEMENTATION_ID "{implementation}"\n', encoding='utf-8')
			build = repo / 'build'
			build.mkdir()
			info = {'version': '0.1.1', 'Platform': 'unused', 'Sdl3': 'unused', 'CoreBuildHeader': str(header)}
			build_info = build / 'veroscript-foundation-Release.json'
			build_info.write_text(json.dumps(info), encoding='utf-8')
			settings = {'CMAKE_HOME_DIRECTORY': str(repo), 'CMAKE_COMMAND': 'fixture-cmake',
						'SDL_SHARED': 'ON', 'VEROSCRIPT_SDL3_NATIVE_EXTENSION': 'ON'}
			versions = dict.fromkeys(FOUNDATION_BUSINESSES, '0.1.1')

			def install(command, *, check):
				if command[0] == 'objcopy':
					return
				self.assertEqual(command[:2], ['fixture-cmake', '--install'])
				business = command[command.index('--component') + 1].removeprefix('VeroScript')
				prefix = Path(command[command.index('--prefix') + 1])
				files = foundation_files(business, versions, 'linux-x64', elf(), b'MIT notice')
				for name, data in files.items():
					if name == 'veroscript-module.json' or name.endswith('Release.cmake'):
						continue
					output = prefix / name
					output.parent.mkdir(parents=True, exist_ok=True)
					output.write_bytes(data)

			descriptor_abi = f'native-extension:0.1.1:{abi}:fixture:{implementation}:release'
			with patch('veroscript_modules.build_cache', return_value=settings), \
					patch('veroscript_foundation.subprocess.run', side_effect=install), \
					patch('veroscript_foundation.native_extension_contract', return_value=descriptor_abi) as query:
				result = assemble_foundation(repo, dict.fromkeys(('Platform', 'Sdl3'), build),
											 repo / 'release', versions, 'linux-x64', ['Sdl3'], bindings=False)
				self.assertEqual(set(result), {'Platform', 'Sdl3'})
				self.assertEqual(query.call_args.args[1:], ('linux-x64', abi, implementation, '0.1.1'))
				info['CoreBuildHeader'] = str(repo / 'missing-header.h')
				build_info.write_text(json.dumps(info), encoding='utf-8')
				with self.assertRaisesRegex(ValueError, 'generated Core build header recorded by CMake'):
					assemble_foundation(repo, dict.fromkeys(('Platform', 'Sdl3'), build),
										repo / 'missing-header-release', versions, 'linux-x64', ['Sdl3'], bindings=False)

	def test_changed_payload_fails_even_if_outer_archive_is_rehashed(self):
		files = self.files()
		files['lib/libVeroScriptSdl3.so'] = elf() + b'changed implementation'
		with self.assertRaisesRegex(ValueError, 'payload checksum'):
			self.verify(files)

	def test_wrong_architecture_fails_with_valid_payload_identity(self):
		files = self.files()
		meta = json.loads(files['veroscript-module.json'])
		path = 'lib/libVeroScriptSdl3.so'
		files[path] = elf('arm64')
		meta['payloadFiles'][path] = hashlib.sha256(files[path]).hexdigest()
		meta['payloadId'] = payload_id(meta)
		files['veroscript-module.json'] = json.dumps(meta).encode()
		files['lib/cmake/VeroScriptSdl3/VeroScriptSdl3Release.cmake'] = release_config('Sdl3', meta).encode()
		with self.assertRaisesRegex(ValueError, 'native target mismatch'):
			self.verify(files)

	def test_auxiliary_content_and_symbols_are_rejected(self):
		for name in ('README.zh-CN.md', 'examples/window.vs', 'lib/VeroScriptSdl3.pdb'):
			files = self.files()
			files[name] = b'not a functional payload'
			with self.assertRaisesRegex(ValueError, 'Non-functional content'):
				self.verify(files)

	def test_tar_hashes_retain_native_library_links(self):
		with tempfile.TemporaryDirectory() as temporary:
			archive = Path(temporary) / 'sdk.tar.gz'
			with tarfile.open(archive, 'w:gz') as writer:
				item = tarfile.TarInfo('sdk/lib/libSDL3.so.0')
				item.size = len(elf())
				writer.addfile(item, io.BytesIO(elf()))
				link = tarfile.TarInfo('sdk/lib/libSDL3.so')
				link.type, link.linkname = tarfile.SYMTYPE, 'libSDL3.so.0'
				writer.addfile(link)
			result = archive_digests(archive)
			self.assertEqual(result['sdk/lib/libSDL3.so'], result['sdk/lib/libSDL3.so.0'])

	def test_backend_requires_its_matching_service_before_publication(self):
		fixture = self.fixture()
		with self.assertRaisesRegex(ValueError, 'dependency version/build is missing'):
			fixture.publish(fixture.candidate('missing-platform'), ['Sdl3'])
		self.assertFalse((fixture.root / 'Modules/Sdl3').exists())

	def test_service_version_required_by_backend_is_retained(self):
		fixture = self.fixture()
		fixture.publish(fixture.candidate('initial'), ['Platform', 'Sdl3'])
		previous = fixture.versions['Platform']
		versions = {**fixture.versions, 'Platform': '99.0.0'}
		fixture.publish(fixture.candidate('updated', versions=versions), ['Platform'])
		verify_collection(fixture.root, 'linux-x64')
		self.assertTrue((fixture.root / business_route('Platform') / 'linux-x64' / previous).is_dir())

	def test_same_version_wrong_build_fails_without_changing_index(self):
		fixture = self.fixture()
		fixture.publish(fixture.candidate('initial'), ['Platform', 'Sdl3'])
		index = collection_index(fixture.root, 'linux-x64')
		before = index.read_bytes()
		candidate = fixture.candidate('mismatch')
		path = candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest['businessBuildIds']['Platform'] = 'a' * 64
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'payload identity mismatch'):
			fixture.publish(candidate, ['Platform'])
		self.assertEqual(index.read_bytes(), before)


if __name__ == '__main__':
	unittest.main()
