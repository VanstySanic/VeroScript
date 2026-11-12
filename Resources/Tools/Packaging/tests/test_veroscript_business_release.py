# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import json
import hashlib
import io
import shutil
from pathlib import Path
import sys
import subprocess
import tarfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_veroscript_release as release_tests
CORE_BUILD_ID = release_tests.CORE_BUILD_ID
from veroscript_release import digest, verify_release, write_manifest, safe_output
from veroscript_products import business_route, INSTALLER_BUSINESSES, MODULE_BUSINESSES, verify_module_payload, clean_functional_payload
from veroscript_business_release import BUSINESSES, ALL_BUSINESSES, artifact_business, collection_root, distribution_identity, distribution_targets, load_versions, publish_businesses, publish_component, verify_collection, read_business, prune_versions, guard_tree


class BusinessReleaseTests(unittest.TestCase):
	@unittest.skipUnless(sys.platform == 'darwin', 'Darwin system aliases require macOS')
	def test_system_temporary_ancestor_preserves_publication_link_guards(self):
		import tempfile
		with tempfile.TemporaryDirectory(dir='/tmp') as temporary:
			root = Path(temporary)
			guard_tree(root)
			target = root / 'target'
			target.mkdir()
			link = root / 'linked'
			link.symlink_to(target, target_is_directory=True)
			with self.assertRaisesRegex(ValueError, 'Linked publication path'):
				guard_tree(link / 'candidate')
			with self.assertRaisesRegex(ValueError, 'Linked publication entry'):
				guard_tree(root)
		with self.assertRaisesRegex(ValueError, 'Linked publication path'):
			guard_tree(Path('/tmp'))

	def setUp(self):
		self.fixture = release_tests.ReleaseContractTests()
		self.fixture.setUp()
		self.addCleanup(self.fixture.doCleanups)
		self.repo = self.fixture.repo
		tools = self.repo / 'Script/Resources/Tools/Packaging'
		tools.mkdir(parents=True)
		header = self.repo / 'Script/VeroScript/Public/VeroScriptVersion.h'
		header.parent.mkdir(parents=True)
		header.write_text('// Source module marker\n')
		self.versions = {business: f'{index + 1}.0.0' for index, business in enumerate(ALL_BUSINESSES)}
		(tools / 'ReleaseVersions.json').write_text(json.dumps({
			'schemaVersion': 1, 'nativeVersion': '1.0.0', 'businesses': self.versions}))
		self.root = self.repo / 'Script/Release'

	def candidate(self, name, arch='x64', versions=None):
		versions = versions or self.versions
		root = self.fixture.make_release(self.repo / '.tmp' / name, arch, csharp_version=versions['CSharp'])
		for file in list(root.rglob('*')):
			if not file.is_file() or file.name in {'release-manifest.json', 'checksums.txt'} or file.parts[-2] == 'Resources' or 'Assets' in file.parts:
				continue
			business = artifact_business(file.relative_to(root).as_posix())
			new_name = file.name.replace('-1.0.0', '-' + versions[business])
			destination = file.with_name(new_name)
			if file.name.endswith('.tar.gz'):
				with tarfile.open(file, 'r:gz') as archive:
					members = [(item, archive.extractfile(item).read() if item.isfile() else None) for item in archive]
				output = file.with_name(file.name + '.new')
				with tarfile.open(output, 'w:gz') as archive:
					import io
					for item, data in members:
						item.name = item.name.replace('-1.0.0', '-' + versions[business])
						if data is not None and item.name.endswith('/veroscript-module.json'):
							metadata = json.loads(data)
							metadata['version'] = versions[business]
							metadata['dependencies'] = {key: versions[key] for key in metadata.get('dependencies', {})}
							data = json.dumps(metadata).encode()
							item.size = len(data)
						item.pax_headers.pop('path', None)
						archive.addfile(item, io.BytesIO(data) if data is not None else None)
				file.unlink()
				output.rename(destination)
			elif file.suffix == '.zip':
				with zipfile.ZipFile(file) as archive:
					entries = [(item, archive.read(item)) for item in archive.infolist()]
				output = file.with_name(file.name + '.new')
				with zipfile.ZipFile(output, 'w') as archive:
					for item, data in entries:
						item.filename = item.filename.replace('VeroScriptWebAssemblySDK-1.0.0-wasm32', destination.stem)
						archive.writestr(item, data)
				file.unlink()
				output.rename(destination)
			elif destination != file:
				file.rename(destination)
		from veroscript_products import FOUNDATION_PACKAGE_NAMES
		from foundation_fixtures import foundation_files
		foundation_ids = {}
		for business, package in FOUNDATION_PACKAGE_NAMES.items():
			files = foundation_files(business, versions, 'linux-' + arch, release_tests.elf(arch), (Path(__file__).resolve().parents[4] / 'LICENSE').read_bytes())
			archive = root / 'modules' / f'{package}-{versions[business]}-linux-{arch}.tar.gz'
			with tarfile.open(archive, 'w:gz') as writer:
				for name, data in files.items():
					item = tarfile.TarInfo(archive.name.removesuffix('.tar.gz') + '/' + name)
					item.size, item.mode = len(data), 0o755
					writer.addfile(item, io.BytesIO(data))
			foundation_ids[business] = json.loads(files['veroscript-module.json'])['payloadId']
		write_manifest(root, versions['CoreSDK'], 'linux-' + arch, CORE_BUILD_ID,
					   business_versions=versions, native_version='1.0.0')
		manifest_path = root / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text())
		manifest['businessBuildIds'] = foundation_ids
		manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
		verify_release(root)
		return root

	def publish(self, candidate, businesses=None):
		# The synthetic fixture has only a module marker, without the repository API/doc contract.
		audit = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda root, repo=None, **kw: audit(root, **kw)):
			publish_businesses(self.repo, candidate, self.root, businesses)

	def test_full_release_uses_independent_versions_and_owned_symbols(self):
		self.publish(self.candidate('full'))
		index = verify_collection(self.root, 'linux-x64')
		self.assertEqual(set(index['businesses']), set(BUSINESSES))
		for business in BUSINESSES:
			self.assertEqual(index['businesses'][business]['version'], self.versions[business])
		self.assertTrue((self.root / 'CoreSDK/linux-x64/2.0.0/symbols/VeroScriptSymbols-2.0.0-linux-x64.tar.gz').is_file())
		self.assertFalse((self.root / 'linux-x64/symbols').exists())
		self.assertTrue((self.root / 'Resources/any' / self.versions['Resources'] / 'Resources/README.md').is_file())

	def test_runtime_can_be_the_first_business_without_a_core_sdk(self):
		self.publish(self.candidate('runtime-only'), ['Runtime'])
		index = verify_collection(self.root, 'linux-x64')
		self.assertEqual(set(index['businesses']), {'Runtime'})
		self.assertEqual(index['businesses']['Runtime']['version'], '1.0.0')
		self.assertFalse((self.root / 'CoreSDK').exists())

	def rewrite_index(self, target, change):
		file = self.root / 'indexes' / (target + '.json')
		index = json.loads(file.read_text());change(index)
		file.write_text(json.dumps(index))
		root_file = self.root / 'release-index.json'
		metadata = json.loads(root_file.read_text());metadata['indexes'][target]['sha256'] = digest(file)
		root_file.write_text(json.dumps(metadata))

	def test_completeness_is_recomputed_from_actual_inventory(self):
		self.publish(self.candidate('runtime-only-state'), ['Runtime'])
		self.rewrite_index('linux-x64', lambda index: index.update(
			requiredBusinesses=[], missingBusinesses=[], partialBusinesses=[], complete=True))
		with self.assertRaisesRegex(ValueError, 'completeness'):
			verify_collection(self.root, 'linux-x64')
		with self.assertRaisesRegex(ValueError, 'different product registry'):
			collection_root(self.repo, self.root)

	def test_complete_requires_a_boolean_even_if_inventory_is_complete(self):
		self.publish(self.candidate('full-state-type'))
		self.rewrite_index('linux-x64', lambda index: index.update(complete=1))
		with self.assertRaisesRegex(ValueError, 'completeness'):
			verify_collection(self.root, 'linux-x64')

	def test_shared_publication_preserves_matching_qualification_only(self):
		from veroscript_qualification import record_qualifications
		candidate = self.candidate('shared-qualified')
		self.publish(candidate, ['Resources'])
		record = {'Resources': {'checks': {
			'build': {'status':'passed','reason':'Resource build verified'},
			'target-runtime': {'status':'not-applicable','reason':'Static resources'}}}}
		record_qualifications(self.repo, self.root, 'any', record)
		file = self.root / 'indexes/any.json'
		before = json.loads(file.read_text())['businesses']['Resources']['qualification']
		self.publish(candidate, ['Resources'])
		self.assertEqual(json.loads(file.read_text())['businesses']['Resources']['qualification'], before)
		versions = {**self.versions, 'Resources':'99.0.0'}
		self.publish(self.candidate('changed-shared-version', versions=versions), ['Resources'])
		self.assertNotIn('qualification', json.loads(file.read_text())['businesses']['Resources'])

	def test_native_no_op_preserves_qualification_and_explicit_checks_replace_it(self):
		from veroscript_qualification import record_qualifications
		candidate = self.candidate('native-qualified')
		self.publish(candidate, ['Runtime'])
		record = {'Runtime': {'checks': {
			'build': {'status':'passed','reason':'Real build verified'},
			'target-runtime': {'status':'passed','reason':'Native consumer passed'}}}}
		record_qualifications(self.repo, self.root, 'linux-x64', record)
		file = self.root / 'indexes/linux-x64.json'
		before = json.loads(file.read_text())['businesses']['Runtime']['qualification']
		self.publish(candidate, ['Runtime'])
		self.assertEqual(json.loads(file.read_text())['businesses']['Runtime']['qualification'], before)
		record['Runtime']['checks']['target-runtime'] = {'status':'failed','reason':'Explicit new runtime failure'}
		manifest_file = candidate / 'release-manifest.json';manifest = json.loads(manifest_file.read_text())
		manifest['businessQualifications'] = record;manifest_file.write_text(json.dumps(manifest))
		self.publish(candidate, ['Runtime'])
		self.assertEqual(json.loads(file.read_text())['businesses']['Runtime']['qualification']['status'], 'failed')

	def test_standalone_runtime_can_start_a_collection_without_a_core_sdk(self):
		source = self.candidate('runtime-component') / 'runtime'
		audit = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda root, repo=None, **kw: audit(root, **kw)):
			publish_component(self.repo, source, self.root, 'Runtime', '1.0.0', 'linux-x64')
		index = verify_collection(self.root, 'linux-x64')
		self.assertEqual(set(index['businesses']), {'Runtime'})
		self.assertTrue(read_business(self.root / 'Runtime/linux-x64/1.0.0', 'linux-x64')['partial'])

	def test_selected_business_prunes_superseded_version_and_preserves_other_businesses(self):
		self.publish(self.candidate('initial'))
		original = (self.root / 'Runtime/linux-x64/1.0.0/release-manifest.json').read_bytes()
		versions = {**self.versions, 'Interop': '3.1.0'}
		self.publish(self.candidate('updated', versions=versions), ['Interop'])
		index = verify_collection(self.root, 'linux-x64')
		self.assertEqual(index['businesses']['Interop']['version'], '3.1.0')
		self.assertFalse((self.root / 'Interop/linux-x64/3.0.0').exists())
		self.assertEqual((self.root / 'Runtime/linux-x64/1.0.0/release-manifest.json').read_bytes(), original)

	def test_downgrade_keeps_indexed_version_and_other_platform(self):
		self.publish(self.candidate('initial'))
		self.publish(self.candidate('arm', arch='arm64'))
		versions = {**self.versions, 'Interop': '2.9.0'}
		self.publish(self.candidate('downgrade', versions=versions), ['Interop'])
		self.assertTrue((self.root / 'Interop/linux-x64/2.9.0').is_dir())
		self.assertFalse((self.root / 'Interop/linux-x64/3.0.0').exists())
		self.assertTrue((self.root / 'Interop/linux-arm64/3.0.0').is_dir())
		self.assertFalse(list((self.repo / '.tmp').glob('veroscript-business-backup-*')))

	def test_prune_preflights_all_owned_versions_and_supports_dry_run(self):
		self.publish(self.candidate('initial'))
		with patch('veroscript_business_release.discard_obsolete_versions'):
			self.publish(self.candidate('updated', versions={**self.versions, 'Interop': '3.1.0', 'Python': '6.1.0'}), ['Interop', 'Python'])
		expected = [self.root / 'Interop/linux-x64/3.0.0', self.root / 'Extensions/Language/Python/linux-x64/6.0.0']
		self.assertEqual(set(prune_versions(self.repo, self.root, 'linux-x64', dry_run=True)), set(expected))
		self.assertTrue(all(path.is_dir() for path in expected))
		unknown = expected[1] / 'user-owned.txt'
		unknown.write_text('preserve this file')
		with self.assertRaisesRegex(ValueError, 'unowned'):
			prune_versions(self.repo, self.root, 'linux-x64')
		self.assertTrue(all(path.is_dir() for path in expected))
		self.assertFalse((self.root / '.publication-lock').exists())
		unknown.unlink()
		prune_versions(self.repo, self.root, 'linux-x64')
		self.assertFalse(any(path.exists() for path in expected))
		verify_collection(self.root, 'linux-x64')

	def test_cleanup_failure_reports_committed_publication_without_rollback(self):
		self.publish(self.candidate('initial'))
		updated = self.candidate('updated', versions={**self.versions, 'Interop': '3.1.0'})
		with patch('veroscript_business_release.discard_obsolete_versions', side_effect=PermissionError('locked old version')):
			with self.assertRaisesRegex(RuntimeError, 'Publication succeeded'):
				self.publish(updated, ['Interop'])
		self.assertEqual(verify_collection(self.root, 'linux-x64')['businesses']['Interop']['version'], '3.1.0')
		self.assertTrue((self.root / 'Interop/linux-x64/3.0.0').is_dir())
		self.assertFalse((self.root / '.publication-lock').exists())

	def test_dry_run_cannot_be_silently_ignored_by_publication(self):
		entry = Path(__file__).resolve().parents[1] / 'veroscript_business_release.py'
		result = subprocess.run([sys.executable, str(entry), 'publish', '--dry-run',
								 '--repo-root', str(self.repo)], capture_output=True, text=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn('--dry-run requires prune', result.stderr)
		self.assertFalse(self.root.exists())

	def test_failed_upgrade_keeps_previous_versions_until_commit(self):
		self.publish(self.candidate('initial'))
		versions = {**self.versions, 'Runtime': '1.1.0', 'Interop': '3.1.0'}
		updated = self.candidate('updated', versions=versions)
		before = {p.relative_to(self.root): digest(p) for p in self.root.rglob('*') if p.is_file()}
		rename = Path.rename
		def fail(source, destination):
			if 'veroscript-business-stage-' in str(source) and Path(destination).parents[1].name == 'Interop':
				raise PermissionError('injected upgrade failure')
			return rename(source, destination)
		with patch.object(Path, 'rename', fail), self.assertRaises(PermissionError):
			self.publish(updated, ['Runtime', 'Interop'])
		self.assertEqual({p.relative_to(self.root): digest(p) for p in self.root.rglob('*') if p.is_file()}, before)
		self.assertFalse((self.root / 'Runtime/linux-x64/1.1.0').exists())

	def test_platforms_reuse_resources_and_protect_other_targets_references(self):
		self.publish(self.candidate('x64'))
		arm = self.candidate('arm', arch='arm64')
		self.publish(arm)
		self.assertEqual(len(list((self.root / 'Resources/any').iterdir())), 1)
		(arm / 'Resources/README.md').write_text('different shared resource')
		write_manifest(arm, '2.0.0', 'linux-arm64', CORE_BUILD_ID, business_versions=self.versions, native_version='1.0.0')
		with self.assertRaisesRegex(ValueError, 'Shared artifact identity differs'):
			self.publish(arm, ['Resources'])
		write_manifest(arm, '2.0.0', 'linux-arm64', CORE_BUILD_ID, business_versions={**self.versions, 'Resources':'8.1.0'}, native_version='1.0.0')
		self.publish(arm, ['Resources'])
		self.assertTrue((self.root / 'Resources/any' / self.versions['Resources']).is_dir())
		self.assertTrue((self.root / 'Resources/any/8.1.0').is_dir())
		verify_collection(self.root, 'linux-x64')
		verify_collection(self.root, 'linux-arm64')



	def reseal_index(self, path):
		entry = self.root / 'release-index.json'
		metadata = json.loads(entry.read_text())
		metadata['indexes'][path.stem]['sha256'] = digest(path)
		entry.write_text(json.dumps(metadata))

	def materialize(self, name):
		stage = self.repo / '.tmp' / name
		entry = Path(__file__).resolve().parents[1] / 'veroscript_business_release.py'
		result = subprocess.run([sys.executable, str(entry), 'materialize', '--repo-root', str(self.repo),
								 '--candidate', str(stage), '--target', 'linux-x64'], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		return stage

	def test_materialize_preserves_historical_cpp_build_identity(self):
		self.publish(self.candidate('initial'))
		candidate = self.candidate('core-upgrade', versions={**self.versions, 'CoreSDK': '2.1.0'})
		library = b'updated core library'
		build_id = hashlib.sha256(library).hexdigest()
		archive_path = candidate / 'sdk/VeroScriptSDK-2.1.0-linux-x64.tar.gz'
		with tarfile.open(archive_path, 'r:gz') as archive:
			members = [(item, archive.extractfile(item).read() if item.isfile() else None) for item in archive]
		with tarfile.open(archive_path, 'w:gz') as archive:
			for item, data in members:
				if item.name.endswith('/lib/libVeroScript.a'):
					data = library
				elif item.name.endswith('/VeroScriptConfig.cmake'):
					data = data.replace(CORE_BUILD_ID.encode(), build_id.encode())
				if data is not None:
					item.size = len(data)
				archive.addfile(item, io.BytesIO(data) if data is not None else None)
		write_manifest(candidate, '2.1.0', 'linux-x64', build_id,
					   business_versions={**self.versions, 'CoreSDK': '2.1.0'}, native_version='1.0.0')
		path = candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest['businessBuildIds'] = {'Cpp': CORE_BUILD_ID}
		path.write_text(json.dumps(manifest))
		self.publish(candidate, ['CoreSDK'])
		stage = self.materialize('audit')
		manifest = json.loads((stage / 'release-manifest.json').read_text())
		self.assertEqual(manifest['buildId'], build_id)
		self.assertEqual(manifest['businessBuildIds']['Cpp'], CORE_BUILD_ID)
		verify_release(stage, businesses=set(manifest['selectedBusinesses']),
					   partial_businesses=set(manifest['partialBusinesses']))

	def test_materialized_partial_distribution_passes_verifier_cli(self):
		source = self.repo / '.tmp/wheel-only'
		source.mkdir()
		with zipfile.ZipFile(source / 'veroscript-6.0.0-cp311-cp311-manylinux_2_28_x86_64.whl', 'w') as archive:
			archive.writestr('veroscript-6.0.0.dist-info/METADATA', 'Name: veroscript\nVersion: 6.0.0\n')
		publish_component(self.repo, source, self.root, 'Python', '6.0.0', 'linux-x64')
		stage = self.materialize('partial-audit')
		manifest = json.loads((stage / 'release-manifest.json').read_text())
		self.assertEqual(manifest['selectedBusinesses'], ['Python'])
		self.assertEqual(manifest['partialBusinesses'], ['Python'])
		entry = Path(__file__).resolve().parents[1] / 'verify-veroscript-release.py'
		result = subprocess.run([sys.executable, str(entry), '--release-directory', str(stage),
								 '--repo-root', str(self.repo), '--target', 'linux-x64'], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_collection_verifies_after_copying_outside_script_and_renaming(self):
		self.publish(self.candidate('initial'))
		relocated = self.repo / 'download/ProductPackages'
		shutil.copytree(self.root, relocated)
		self.assertEqual(verify_collection(relocated, 'linux-x64'), verify_collection(self.root, 'linux-x64'))

	def test_dependency_mismatch_is_rejected_before_publication(self):
		self.publish(self.candidate('initial'))
		updated = self.candidate('new', versions={**self.versions, 'CoreSDK': '2.1.0'})
		before = (self.root / 'indexes/linux-x64.json').read_bytes()
		with self.assertRaisesRegex(ValueError, r'C\+\+/CoreSDK'):
			self.publish(updated, ['Cpp'])
		self.assertEqual((self.root / 'indexes/linux-x64.json').read_bytes(), before)

	def test_core_upgrade_preserves_cpp_dependency_on_historical_core(self):
		self.publish(self.candidate('initial'))
		updated = self.candidate('new', versions={**self.versions, 'CoreSDK': '2.1.0'})
		self.publish(updated, ['CoreSDK'])
		index = verify_collection(self.root, 'linux-x64')
		self.assertEqual(index['businesses']['CoreSDK']['version'], '2.1.0')
		cpp = json.loads((self.root / 'Extensions/Language/Cpp/linux-x64/4.0.0/release-manifest.json').read_text())
		self.assertEqual(cpp['dependencies']['CoreSDK']['version'], '2.0.0')

	def test_standalone_cpp_update_preserves_historical_core_dependency(self):
		import shutil
		self.publish(self.candidate('initial'))
		self.publish(self.candidate('new', versions={**self.versions, 'CoreSDK': '2.1.0'}), ['CoreSDK'])
		source = self.repo / '.tmp/cpp-installer'
		source.mkdir()
		leaf = self.root / 'Extensions/Language/Cpp/linux-x64/4.0.0'
		before = json.loads((leaf / 'release-manifest.json').read_text())
		for file in (leaf / 'installers').glob('VeroScriptCppInstaller-*'):
			shutil.copy2(file, source / file.name)
		audit = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda root, repo=None, **kw: audit(root, **kw)):
			publish_component(self.repo, source, self.root, 'Cpp', '4.0.0', 'linux-x64')
		after = json.loads((leaf / 'release-manifest.json').read_text())
		self.assertEqual(after['dependencies'], before['dependencies'])
		verify_collection(self.root, 'linux-x64')

	def test_historical_core_dependency_checks_actual_library_after_rehash(self):
		import io
		self.publish(self.candidate('initial'))
		self.publish(self.candidate('new', versions={**self.versions, 'CoreSDK': '2.1.0'}), ['CoreSDK'])
		leaf = self.root / 'CoreSDK/linux-x64/2.0.0'
		file = leaf / 'sdks/VeroScriptSDK-2.0.0-linux-x64.tar.gz'
		with tarfile.open(file, 'r:gz') as archive:
			members = [(item, archive.extractfile(item).read() if item.isfile() else None) for item in archive]
		with tarfile.open(file, 'w:gz') as archive:
			for item, data in members:
				if item.name.endswith('/lib/libVeroScript.a'):
					data = b'different native library'
					item.size = len(data)
				archive.addfile(item, io.BytesIO(data) if data is not None else None)
		path = leaf / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		for item in manifest['artifacts']:
			if item['path'] == file.relative_to(leaf).as_posix():
				item.update(sha256=digest(file), size=file.stat().st_size)
		path.write_text(json.dumps(manifest))
		(leaf / 'checksums.txt').write_text(''.join(f"{a['sha256']}  {a['path']}\n" for a in manifest['artifacts']))
		with self.assertRaisesRegex(ValueError, 'Core library build identity'):
			verify_collection(self.root, 'linux-x64')

	def test_multi_business_failure_restores_previous_publication(self):
		self.publish(self.candidate('initial'))
		updated = self.candidate('updated')
		before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
		rename = Path.rename
		def fail(source, destination):
			if 'veroscript-business-stage-' in str(source) and Path(destination).parents[1].name == 'Interop':
				raise PermissionError('injected publication failure')
			return rename(source, destination)
		with patch.object(Path, 'rename', fail), self.assertRaises(PermissionError):
			self.publish(updated, ['Runtime', 'Interop'])
		after = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
		self.assertEqual(after, before)

	def test_standalone_archive_merges_into_existing_business(self):
		self.publish(self.candidate('initial'))
		source = self.repo / '.tmp/standalone'
		source.mkdir()
		archive = self.root / 'Runtime/linux-x64/1.0.0/archives/VeroScriptRuntime-1.0.0-linux-x64.tar.gz'
		import shutil
		shutil.copy2(archive, source / archive.name)
		audit = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda root, repo=None, **kw: audit(root, **kw)):
			publish_component(self.repo, source, self.root, 'Runtime', '1.0.0', 'linux-x64')
		index = verify_collection(self.root, 'linux-x64')
		self.assertTrue((self.root / 'Runtime/linux-x64/1.0.0/installers/VeroScriptRuntimeInstaller-1.0.0-linux-x64.tar.gz').is_file())
		self.assertEqual(index['businesses']['Interop']['version'], '3.0.0')

	def test_unowned_files_and_paths_are_rejected(self):
		self.publish(self.candidate('initial'))
		(self.root / 'Runtime/linux-x64/1.0.0/unknown.txt').write_text('unowned')
		with self.assertRaisesRegex(ValueError, 'unowned'):
			verify_collection(self.root, 'linux-x64')
		for path in (self.repo, self.repo / 'Script', self.repo / 'Script/VeroScript', self.root / 'Runtime/linux-x64/1.0.0'):
			with self.assertRaises(ValueError):
				collection_root(self.repo, path)

	def test_collection_cannot_nest_inside_a_release_output(self):
		self.root = self.repo / '.tmp/alternate-release'
		candidate = self.candidate('nested-output-candidate')
		self.publish(candidate, ['Runtime'])
		previous_index = verify_collection(self.root, 'linux-x64')
		locked = self.repo / '.tmp/pending-release'
		locked.mkdir()
		(locked / '.publication-lock').touch()
		for parent in (self.root, candidate, locked):
			with self.subTest(parent=parent):
				nested = parent / 'nested-release'
				with self.assertRaisesRegex(ValueError, 'cannot be nested'):
					publish_businesses(self.repo, candidate, nested, ['Runtime'])
				self.assertFalse(nested.exists())
		self.assertEqual(previous_index, verify_collection(self.root, 'linux-x64'))

	def test_config_override_only_changes_selected_business(self):
		config = load_versions(self.repo, override='1.2.3', businesses=['Cpp'])
		self.assertEqual(config['businesses']['Cpp'], '1.2.3')
		self.assertEqual(config['businesses']['CoreSDK'], '2.0.0')
		self.assertEqual(config['nativeVersion'], '1.0.0')

	def test_python_distribution_uses_metadata_and_merges_with_native_sdk(self):
		self.publish(self.candidate('initial'))
		source = self.repo / '.tmp/distributions'
		source.mkdir()
		wheel = source / 'veroscript-6.0.0-cp311-cp311-manylinux_2_28_x86_64.whl'
		with zipfile.ZipFile(wheel, 'w') as archive:
			archive.writestr('veroscript-6.0.0.dist-info/METADATA', 'Metadata-Version: 2.1\nName: veroscript\nVersion: 6.0.0\n')
			archive.writestr('veroscript/__init__.py', '')
		self.assertEqual(distribution_identity(wheel), ('Python', '6.0.0'))
		audit = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda root, repo=None, **kw: audit(root, **kw)):
			publish_component(self.repo, source, self.root, 'Python', '6.0.0', 'linux-x64')
		verify_collection(self.root, 'linux-x64')
		self.assertTrue((self.root / 'Extensions/Language/Python/linux-x64/6.0.0/packages' / wheel.name).is_file())
		self.assertTrue((self.root / 'Extensions/Language/Python/linux-x64/6.0.0/sdks/VeroScriptPython-6.0.0-linux-x64.tar.gz').is_file())
		renamed = wheel.with_name(wheel.name.replace('6.0.0', '6.1.0'))
		wheel.rename(renamed)
		with self.assertRaisesRegex(ValueError, 'metadata mismatch'):
			distribution_identity(renamed)

	def matrix_wheel(self, directory, minor, *, arch='x64', marker='initial'):
		path = directory / f'veroscript-6.0.0-cp3{minor}-cp3{minor}-linux_x86_64.whl'
		with zipfile.ZipFile(path, 'w') as archive:
			archive.writestr('veroscript-6.0.0.dist-info/METADATA', 'Name: veroscript\nVersion: 6.0.0\nRequires-Python: >=3.11,<3.16\n')
			archive.writestr('veroscript-6.0.0.dist-info/WHEEL', f'Tag: cp3{minor}-cp3{minor}-linux_x86_64\n')
			archive.writestr('veroscript-6.0.0.dist-info/licenses/LICENSE', 'Fixture license\n')
			archive.writestr(f'veroscript/_veroscript.cpython-3{minor}-x86_64-linux-gnu.so', release_tests.elf(arch))
			archive.writestr('veroscript/build-marker.txt', marker)
		return path

	def publish_complete_python_matrix(self):
		project = self.repo / 'Script/Extensions/Language/Python/pyproject.toml'
		project.parent.mkdir(parents=True)
		project.write_text('[project]\nrequires-python=">=3.11,<3.16"\n[tool.cibuildwheel]\nbuild="cp31{1,2,3,4,5}-*"\n')
		candidate = self.candidate('complete-matrix')
		distributions = candidate / 'distributions/python'
		distributions.mkdir(parents=True)
		for minor in range(11, 16):
			self.matrix_wheel(distributions, minor)
		with tarfile.open(distributions / 'veroscript-6.0.0.tar.gz', 'w:gz') as archive:
			data = b'Name: veroscript\nVersion: 6.0.0\nRequires-Python: >=3.11,<3.16\n'
			entry = tarfile.TarInfo('veroscript-6.0.0/PKG-INFO')
			entry.size = len(data)
			archive.addfile(entry, io.BytesIO(data))
		write_manifest(candidate, self.versions['CoreSDK'], 'linux-x64', CORE_BUILD_ID,
					   business_versions=self.versions, native_version='1.0.0')
		self.publish(candidate)

	def test_python_component_preserves_complete_matrix_and_source_paths(self):
		self.publish_complete_python_matrix()
		leaf = self.root / 'Extensions/Language/Python/linux-x64/6.0.0'
		before = read_business(leaf, 'linux-x64')
		source = self.repo / '.tmp/one-wheel'
		source.mkdir()
		wheel = self.matrix_wheel(source, 11, marker='updated')
		audit = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda root, repo=None, **kw: audit(root, **kw)):
			publish_component(self.repo, source, self.root, 'Python', '6.0.0', 'linux-x64')
		after = read_business(leaf, 'linux-x64')
		self.assertFalse(after.get('partial', False))
		self.assertEqual(after['pythonWheelVersions'], ['3.11', '3.12', '3.13', '3.14', '3.15'])
		self.assertEqual({item['path']: item['sourcePath'] for item in before['artifacts']},
						 {item['path']: item['sourcePath'] for item in after['artifacts']})
		self.assertEqual(digest(leaf / 'packages' / wheel.name), digest(wheel))
		verify_collection(self.root, 'linux-x64')

	def test_invalid_python_component_cannot_bypass_complete_matrix(self):
		self.publish_complete_python_matrix()
		inventory = {file.relative_to(self.root).as_posix(): digest(file)
					 for file in self.root.rglob('*') if file.is_file()}
		source = self.repo / '.tmp/wrong-architecture-wheel'
		source.mkdir()
		self.matrix_wheel(source, 11, arch='arm64')
		audit = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda root, repo=None, **kw: audit(root, **kw)):
			with self.assertRaisesRegex(ValueError, 'native architecture mismatch'):
				publish_component(self.repo, source, self.root, 'Python', '6.0.0', 'linux-x64')
		self.assertEqual(inventory, {file.relative_to(self.root).as_posix(): digest(file)
									 for file in self.root.rglob('*') if file.is_file()})
		self.assertFalse((self.root / '.publication-lock').exists())

	def test_unmanaged_business_and_existing_lock_are_preserved(self):
		candidate = self.candidate('initial')
		leaf = self.root / 'Runtime/linux-x64/1.0.0'
		leaf.mkdir(parents=True)
		marker = leaf / 'private.txt'
		marker.write_text('preserve')
		with self.assertRaisesRegex(ValueError, 'unmanaged'):
			self.publish(candidate)
		self.assertEqual(marker.read_text(), 'preserve')
		lock = self.root / '.publication-lock'
		lock.write_text('other publisher')
		with self.assertRaises(FileExistsError):
			self.publish(candidate)
		self.assertEqual(lock.read_text(), 'other publisher')
		self.assertFalse(list((self.repo / '.tmp').glob('veroscript-collection-lock-*')))












	def test_modules_never_require_installers_and_reject_documentation(self):
		self.assertTrue(set(MODULE_BUSINESSES).isdisjoint(INSTALLER_BUSINESSES))
		verify_module_payload(['veroscript-package.json', 'backend.dll', 'licenses/asmjit/LICENSE.md', 'VERSION.txt'])
		for name in ('README.md', 'README_zh-CN.md', 'Docs/guide.html', 'Examples/main.vs',
					 'Standards/en-US/GENERAL.md', 'resources/STANDARDS/zh-CN/CPP.md',
					 'Private/Tests/test.cpp', 'backend.pdb', 'backend.dSYM/Contents/Resources/DWARF/backend'):
			with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Non-functional'):
				verify_module_payload([name])

	def test_functional_cleanup_preserves_legal_and_operational_resources(self):
		root = self.repo / '.tmp/functional-stage'
		retained = ('LICENSE', 'licenses/vendor/LICENSE.md', 'package.json',
					'include/Script/api.h', 'lib/Api.xml', 'localization/en-US.json', 'Resources/Schemas/package.json')
		removed = ('README.md', 'Docs/guide.md', 'Examples/main.vs', 'History/notes.md', 'Resources/Docs/manual.html',
				   'Standards/en-US/GENERAL.md', 'resources/STANDARDS/zh-CN/CPP.md')
		for name in (*retained, *removed):
			file = root / name
			file.parent.mkdir(parents=True, exist_ok=True)
			file.write_text(name)
		clean_functional_payload(root)
		self.assertEqual({file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_file()}, set(retained))

	def test_complete_publication_requires_current_payload_policy(self):
		self.publish(self.candidate('initial'))
		path = self.root / 'Runtime/linux-x64/1.0.0/release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest.pop('payloadPolicyVersion')
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'payload identity'):
			verify_collection(self.root, 'linux-x64')

	def test_business_manifest_cannot_be_placed_under_a_different_platform(self):
		self.publish(self.candidate('initial'))
		leaf = self.root / 'Runtime/linux-x64/1.0.0'
		wrong = self.root / 'Runtime/linux-arm64/1.0.0'
		shutil.copytree(leaf, wrong)
		with self.assertRaisesRegex(ValueError, 'identity'):
			read_business(wrong, 'linux-x64')

	def test_module_manifest_requires_its_modules_group(self):
		self.publish(self.candidate('initial'))
		source = self.root / 'Modules/Execution/Jit/linux-x64' / self.versions['Jit']
		for route in (f"Jit/linux-x64/{self.versions['Jit']}", f"Extensions/Jit/linux-x64/{self.versions['Jit']}"):
			wrong = self.root / route
			shutil.copytree(source, wrong)
			with self.subTest(route=route), self.assertRaisesRegex(ValueError, 'identity'):
				read_business(wrong, 'linux-x64')

	def test_distribution_platforms_are_inferred_for_a_merged_wheel_matrix(self):
		names = {'win_amd64': ('win-x64',), 'manylinux_2_28_x86_64': ('linux-x64',),
				 'manylinux2014_aarch64': ('linux-arm64',), 'macosx_11_0_arm64': ('macos-arm64',),
				 'macosx_11_0_universal2': ('macos-arm64', 'macos-x64')}
		for tag, targets in names.items():
			self.assertEqual(distribution_targets(Path(f'veroscript-6.0.0-cp311-cp311-{tag}.whl'), 'linux-x64'), targets)
		self.assertEqual(distribution_targets(Path('veroscript-6.0.0.tar.gz'), 'win-x64'), ('win-x64',))
		with self.assertRaisesRegex(ValueError, 'Unsupported'):
			distribution_targets(Path('veroscript-6.0.0-cp311-cp311-unknown.whl'), 'linux-x64')

	def test_merged_distribution_cli_publishes_each_wheel_under_its_actual_platform(self):
		source = self.repo / '.tmp/wheels'
		source.mkdir()
		for target, tag in (('win-x64', 'win_amd64'), ('linux-x64', 'manylinux_2_28_x86_64')):
			directory = source / target / 'Extensions/Language/Python/6.0.0'
			directory.mkdir(parents=True)
			with zipfile.ZipFile(directory / f'veroscript-6.0.0-cp311-cp311-{tag}.whl', 'w') as archive:
				archive.writestr('veroscript-6.0.0.dist-info/METADATA', 'Name: veroscript\nVersion: 6.0.0\n')
		entry = Path(__file__).resolve().parents[1] / 'veroscript_business_release.py'
		result = subprocess.run([sys.executable, str(entry), 'publish-distributions', '--repo-root', str(self.repo),
								 '--candidate', str(source), '--target', 'linux-x64'], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		for target in ('win-x64', 'linux-x64'):
			verify_collection(self.root, target)
			self.assertEqual(len(list((self.root / 'Extensions/Language/Python' / target / '6.0.0/packages').glob('*.whl'))), 1)

	def test_platform_collection_is_protected_from_standalone_output(self):
		self.publish(self.candidate('initial'))
		for output in (self.root / 'linux-x64', self.root / 'linux-x64/new-stage', self.root / 'macos-arm64',
					   self.root / 'Runtime', self.root / 'Runtime/macos-arm64',
					   self.root / 'Extensions', self.root / 'Extensions/Language/Python',
					   self.root / 'Extensions/Language/Python/win-x64', self.root / 'Modules/Execution/Jit/linux-x64'):
			with self.assertRaisesRegex(ValueError, 'collection'):
				safe_output(self.repo, output, component=True)



	@unittest.skipUnless(sys.platform == 'win32', 'Windows standalone packaging entry')
	def test_powershell_default_entry_publishes_only_business_files(self):
		script = self.repo / '.tmp/component.ps1'
		script.write_text('''param([string]$Version, [string]$OutRoot)
$ErrorActionPreference = 'Stop'
$Package = Join-Path $OutRoot ("VeroScriptDocs-" + $Version)
New-Item -ItemType Directory -Path (Join-Path $Package 'docs') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $Package 'docs/index.md'), '# Documentation')
Compress-Archive -LiteralPath $Package -DestinationPath (Join-Path $OutRoot ("VeroScriptDocs-" + $Version + '.zip'))
''')
		helper = Path(__file__).resolve().parents[1] / 'veroscript-output.ps1'
		quote = lambda path: "'" + str(path).replace("'", "''") + "'"
		command = f"$ErrorActionPreference='Stop'; . {quote(helper)}; if (-not (Invoke-VeroScriptDefaultBusinessPackage {quote(script)} @{{}} {quote(self.repo)} 'Resources' '8.0.0' 'win-x64')) {{ throw 'default publisher was not invoked' }}"
		result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'RemoteSigned', '-Command', command], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		verify_collection(self.root, 'win-x64')
		files = {file.name for file in (self.root / 'Resources/any/8.0.0').iterdir()}
		self.assertEqual(files, {'archives', 'release-manifest.json', 'checksums.txt'})
		verifier = Path(__file__).resolve().parents[1] / 'verify-veroscript-release.py'
		result = subprocess.run([sys.executable, str(verifier), '--release-directory', str(self.root)], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
	unittest.main()
