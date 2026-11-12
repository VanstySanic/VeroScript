# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""New-format ownership, shared references and failure recovery; no product builds."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import os
import subprocess
import sys
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_veroscript_business_release as fixtures
import veroscript_business_release as release
from veroscript_products import ALL_BUSINESSES, BUSINESS_ROUTES, business_route, check_product_inventory
from veroscript_release import archive_tree, digest, verify_release, safe_output
from veroscript_release_plan import release_plan


class ClassifiedLayoutTests(unittest.TestCase):
	def setUp(self):
		self.fixture = fixtures.BusinessReleaseTests()
		self.fixture.setUp()
		self.addCleanup(self.fixture.doCleanups)
		self.root = self.fixture.root
		self.repo = self.fixture.repo

	def publish(self, name='candidate', selected=None, arch='x64'):
		self.fixture.publish(self.fixture.candidate(name, arch=arch), selected or ['Runtime', 'Resources'])

	def reseal(self, target):
		path = self.root / 'release-index.json'
		metadata = json.loads(path.read_text())
		metadata['indexes'][target]['sha256'] = digest(release.collection_index(self.root, target))
		path.write_text(json.dumps(metadata))

	def test_all_businesses_have_classified_unique_routes(self):
		self.assertEqual(set(BUSINESS_ROUTES), set(ALL_BUSINESSES))
		self.assertEqual(len(set(BUSINESS_ROUTES.values())), len(ALL_BUSINESSES))
		for business, route in {'Cpp':'Extensions/Language/Cpp', 'VSCode':'Extensions/Editor/VSCode/VSCode',
			'GuiJetBrains':'Extensions/Editor/JetBrains/GuiJetBrains', 'Android':'Extensions/Platform/Mobile/Android',
			'WebAssembly':'Extensions/Platform/WebAssembly', 'Jit':'Modules/Execution/Jit',
			'Json':'Modules/Network/Json', 'Boost':'Modules/Network/Backend/Boost',
			'Gui':'Modules/Multimedia/Gui', 'RmlUi':'Modules/Multimedia/Backend/RmlUi'}.items():
			self.assertEqual(business_route(business), route)

	def test_shared_resources_are_stored_once_and_referenced_with_exact_scope(self):
		self.publish('x64')
		self.publish('arm64', arch='arm64')
		root = json.loads((self.root / 'release-index.json').read_text())
		self.assertEqual(set(root['indexes']), {'any', 'linux-x64', 'linux-arm64'})
		refs = [release.verify_collection(self.root, target)['businesses']['Resources']['manifests']
			for target in ('linux-x64', 'linux-arm64')]
		self.assertEqual(refs[0], refs[1])
		self.assertEqual(refs[0][0]['target'], 'any')
		self.assertEqual(len(list((self.root / 'Resources/any').iterdir())), 1)

	def test_source_category_changes_cannot_silently_leave_an_old_release_route(self):
		with patch.dict(BUSINESS_ROUTES, {'Gui': 'Modules/Execution/Gui'}):
			with self.assertRaisesRegex(ValueError, 'classification diverges'):
				check_product_inventory(Path(__file__).resolve().parents[4])

	def test_identical_nuget_can_be_shared_by_independent_native_versions(self):
		source = self.repo / '.tmp/nuget'
		source.mkdir()
		name = 'VeroScript.CSharp.' + self.fixture.versions['CSharp'] + '.nupkg'
		with zipfile.ZipFile(source / name, 'w') as archive:
			archive.writestr('VeroScript.CSharp.nuspec', '<package><metadata><id>VeroScript.CSharp</id><version>' + self.fixture.versions['CSharp'] + '</version></metadata></package>')
			archive.writestr('lib/net8.0/VeroScriptCSharp.dll', b'managed fixture')
		release.publish_component(self.repo, source, self.root, 'CSharp', self.fixture.versions['CSharp'], 'linux-x64')
		versions_file = self.repo / 'Script/Resources/Tools/Packaging/ReleaseVersions.json'
		config = json.loads(versions_file.read_text())
		config['nativeVersion'] = '2.0.0'
		versions_file.write_text(json.dumps(config))
		release.publish_component(self.repo, source, self.root, 'CSharp', self.fixture.versions['CSharp'], 'linux-arm64')
		first = release.verify_collection(self.root, 'linux-x64')['businesses']['CSharp']
		second = release.verify_collection(self.root, 'linux-arm64')['businesses']['CSharp']
		self.assertEqual(first['manifests'], second['manifests'])
		self.assertEqual(first['nativeVersion'], '1.0.0')
		self.assertEqual(second['nativeVersion'], '2.0.0')
		common = release.read_business(release.business_root(self.root, 'CSharp', 'any') / self.fixture.versions['CSharp'], 'any')
		self.assertNotIn('nativeVersion', common)

	def test_classification_and_single_unix_installer_download(self):
		self.publish(selected=['Runtime', 'CoreSDK'])
		leaf = self.root / 'Runtime/linux-x64/1.0.0'
		self.assertEqual({p.name for p in leaf.iterdir()}, {'archives', 'installers', 'release-manifest.json', 'checksums.txt'})
		self.assertEqual(len(list((leaf / 'installers').iterdir())), 1)
		self.assertTrue(next((leaf / 'installers').iterdir()).name.endswith('.tar.gz'))
		core = self.root / 'CoreSDK/linux-x64/2.0.0'
		self.assertTrue((core / 'sdks').is_dir())
		self.assertTrue((core / 'symbols').is_dir())
		self.assertFalse((core / 'sdk').exists())

	def test_index_tampering_is_detected_by_the_root_digest(self):
		self.publish()
		index = release.collection_index(self.root, 'linux-x64')
		index.write_bytes(index.read_bytes() + b'\n')
		with self.assertRaisesRegex(ValueError, 'index path/hash'):
			release.verify_collection(self.root, 'linux-x64')

	def test_rehashed_reference_cannot_escape_its_business_scope(self):
		self.publish()
		path = release.collection_index(self.root, 'linux-x64')
		index = json.loads(path.read_text())
		index['businesses']['Resources']['manifests'][0]['manifest'] = '../outside/release-manifest.json'
		path.write_text(json.dumps(index))
		self.reseal('linux-x64')
		with self.assertRaisesRegex(ValueError, 'business index path'):
			release.verify_collection(self.root, 'linux-x64')

	def test_unowned_empty_directory_prevents_cleanup(self):
		self.publish()
		unknown = self.root / 'Resources/any' / self.fixture.versions['Resources'] / 'keep-user-data'
		unknown.mkdir()
		with self.assertRaisesRegex(ValueError, 'unowned'):
			release.prune_versions(self.repo, self.root, 'linux-x64')
		self.assertTrue(unknown.is_dir())


	def test_shared_manifest_does_not_accept_a_host_native_version(self):
		self.publish(selected=['Resources'])
		leaf = self.root / 'Resources/any' / self.fixture.versions['Resources']
		file = leaf / 'release-manifest.json'
		metadata = json.loads(file.read_text())
		metadata['nativeVersion'] = '1.0.0'
		file.write_text(json.dumps(metadata))
		with self.assertRaisesRegex(ValueError, 'portable manifest cannot pin'):
			release.read_business(leaf, 'any')

	def test_consuming_context_cannot_override_a_native_runtime_identity(self):
		self.publish(selected=['Runtime'])
		file = release.collection_index(self.root, 'linux-x64')
		metadata = json.loads(file.read_text())
		metadata['businesses']['Runtime']['nativeVersion'] = '9.9.9'
		file.write_text(json.dumps(metadata))
		self.reseal('linux-x64')
		with self.assertRaisesRegex(ValueError, 'consuming native-version context'):
			release.verify_collection(self.root, 'linux-x64')

	def test_reader_in_another_thread_cannot_read_during_publication(self):
		self.publish()
		lock = release.acquire_collection_lock(self.repo, self.root)
		try:
			with ThreadPoolExecutor(max_workers=1) as worker:
				with self.assertRaisesRegex(ValueError, 'Publication in progress'):
					worker.submit(release.verify_collection, self.root, 'linux-x64').result()
		finally:
			release.release_collection_lock(lock)

	def test_failure_after_root_commit_restores_all_index_and_payload_bytes(self):
		self.publish()
		before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
		candidate = self.fixture.candidate('updated', versions={**self.fixture.versions, 'Runtime':'1.0.1'})
		original = release.verify_collection
		def fail(root, target, *args, **kwargs):
			if target == 'linux-x64':
				index = json.loads(release.collection_index(root, target).read_text())
				if index['businesses']['Runtime']['version'] == '1.0.1':
					raise OSError('injected failure after root commit')
			return original(root, target, *args, **kwargs)
		with patch('veroscript_business_release.verify_collection', side_effect=fail), self.assertRaisesRegex(OSError, 'after root commit'):
			self.fixture.publish(candidate, ['Runtime'])
		after = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
		self.assertEqual(before, after)

	def test_old_formats_are_rejected_without_a_migration_command(self):
		self.root.mkdir(parents=True)
		(self.root / 'release-index-linux-x64.json').write_text('{"schemaVersion":4}')
		with self.assertRaisesRegex(ValueError, 'Unsupported old release format'):
			release.require_business_layout(self.root)
		result = subprocess.run([sys.executable, str(Path(release.__file__)), 'migrate-layout'], capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn(b'invalid choice', result.stderr)

	def test_old_flat_release_identity_is_not_a_current_candidate(self):
		candidate = self.fixture.candidate('flat')
		file = candidate / 'release-manifest.json'
		metadata = json.loads(file.read_text())
		metadata.update(schemaVersion=1, product='VeroScript')
		file.write_text(json.dumps(metadata))
		with self.assertRaisesRegex(ValueError, 'Invalid release identity'):
			verify_release(candidate)

	def test_preview_groups_source_categories_and_deduplicates_shared_paths(self):
		config = release.load_versions(self.repo)
		plan = release_plan(Path(__file__).resolve().parents[4], config, ['win-x64', 'linux-arm64', 'android', 'ios'])
		tree = plan['directories']['Release']
		self.assertIn('Backend', tree['Modules']['Network'])
		self.assertIn('any', tree['Resources'])
		self.assertEqual(set(tree['Extensions']['Editor']['VSCode']['VSCode']), {'any'})
		self.assertEqual(set(tree['Extensions']['Platform']['WebAssembly']), {'wasm32', 'win-x64', 'linux-arm64'})
		vm_version = config['businesses']['Vm']
		self.assertIn('VeroScriptVmMobileSDK-' + vm_version + '-android.zip',
			tree['Modules']['Execution']['Vm']['android'][vm_version]['sdks'])
		self.assertEqual(plan['collectionIndexSchemaVersion'], 5)
		self.assertFalse(self.root.exists())

	def test_zip_bytes_do_not_depend_on_file_modification_times(self):
		source = self.repo / '.tmp/portable'
		source.mkdir()
		file = source / 'module.wasm'
		file.write_bytes(b'wasm fixture')
		first, second = self.repo / '.tmp/first.zip', self.repo / '.tmp/second.zip'
		archive_tree(source, first)
		os.utime(file, (1_000_000_000, 1_000_000_000))
		archive_tree(source, second)
		self.assertEqual(first.read_bytes(), second.read_bytes())
		with zipfile.ZipFile(first) as archive:
			self.assertEqual(archive.read('portable/module.wasm'), b'wasm fixture')

	def test_managed_collection_in_staging_cannot_be_used_as_direct_output(self):
		self.publish()
		import shutil
		copied = self.repo / '.tmp/managed-collection'
		shutil.copytree(self.root, copied)
		with self.assertRaisesRegex(ValueError, 'managed release collection'):
			safe_output(self.repo, copied / 'new-stage', component=True)
		entry = Path(release.__file__)
		result = subprocess.run([sys.executable, str(entry), 'materialize', '--repo-root', str(self.repo),
			'--release-directory', str(self.root), '--target', 'linux-x64', '--candidate', str(copied / 'new-stage')], capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertFalse((copied / 'new-stage').exists())


if __name__ == '__main__':
	unittest.main()
