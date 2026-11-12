# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise mobile publication using synthetic binaries, without SDK builds."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))
from veroscript_business_release import (business_root, collection_index, load_versions,
	publish_businesses, publish_component, read_business, verify_collection)
from veroscript_mobile_release import (stage_mobile_candidate, verify_native_sdk,
	verify_csharp_package, verify_python_sdk, MOBILE_PYTHON_VERSIONS)
from veroscript_mobile_release import host_interpreters, load_tool
from veroscript_products import target_businesses, INSTALLER_BUSINESSES
from veroscript_release import safe_output, verify_release, write_manifest, digest, scratch_directory
from veroscript_release_plan import release_plan
from mobile_release_fixtures import create_inputs, write_zip
from test_veroscript_mobile import ar, macho, elf


class MobileReleaseTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.fixtures = tempfile.TemporaryDirectory(prefix='vero-mobile-release-fixtures-')
		try:
			cls.inputs = create_inputs(Path(cls.fixtures.name))
		except BaseException:
			cls.fixtures.cleanup()
			raise

	@classmethod
	def tearDownClass(cls):
		cls.fixtures.cleanup()

	def setUp(self):
		self.temp = tempfile.TemporaryDirectory(prefix='vero-mobile-release-test-', dir=scratch_directory(REPO))
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name)
		self.config = load_versions(REPO)
		self.output = self.root / 'Release 移动端'

	def stage(self, target='android', inputs=None, config=None, partial=False, name='candidate'):
		stage = self.root / name
		stage_mobile_candidate(REPO, stage, config or self.config, target, inputs or self.inputs[target], partial_python=partial)
		return stage

	def rewrite_zip(self, path, change):
		with zipfile.ZipFile(path) as archive:
			entries = {info.filename: archive.read(info) for info in archive.infolist() if not info.is_dir()}
		change(entries)
		write_zip(path, entries)
	def test_optional_module_symbols_keep_the_functional_sdk_required(self):
		from veroscript_module_symbols import symbol_archive_name
		directory = self.root / 'module-symbols'
		directory.mkdir()
		name = symbol_archive_name('Vm', '0.1.1', 'android')
		(directory / name).write_bytes(b'sealed symbol archive fixture; native verifier is tested separately')
		stage = self.root / 'with-module-symbols'
		with patch('veroscript_module_symbols.verify_module_symbols', return_value={'libraries': [{}]}) as verify:
			stage_mobile_candidate(REPO, stage, self.config, 'android', {'Vm': self.inputs['android']['Vm']}, module_symbol_directory=directory)
			self.assertGreaterEqual(verify.call_count, 2)
		manifest = json.loads((stage / 'release-manifest.json').read_text())
		self.assertEqual({item['path'] for item in manifest['artifacts']},
			{'mobile/VeroScriptVmMobileSDK-0.1.1-android.zip', 'symbols/' + name})
		self.assertEqual(manifest['moduleSymbolStatus']['Vm']['archive'], 'symbols/' + name)
		with self.assertRaisesRegex(ValueError, 'exactly one native mobile SDK'):
			stage_mobile_candidate(REPO, self.root / 'missing-functional-sdk', self.config, 'android', {'Vm': []}, module_symbol_directory=directory)

	def test_module_symbols_reject_foreign_owners_and_unselected_original_inputs(self):
		from veroscript_module_symbols import symbol_archive_name
		directory = self.root / 'foreign-module-symbols'
		directory.mkdir()
		(directory / symbol_archive_name('Json', '0.1.1', 'android')).write_bytes(b'foreign symbols')
		with self.assertRaisesRegex(ValueError, 'unselected mobile module symbol'):
			stage_mobile_candidate(REPO, self.root / 'foreign-symbols', self.config, 'android', {'Vm': self.inputs['android']['Vm']}, module_symbol_directory=directory)
		stage = self.root / 'unselected-originals'
		with self.assertRaisesRegex(ValueError, 'selected mobile module businesses'):
			stage_mobile_candidate(REPO, stage, self.config, 'android', {'Vm': self.inputs['android']['Vm']}, module_symbol_inputs=[{'business': 'Json'}])
		self.assertFalse(stage.exists())

	def test_module_symbol_options_reject_unused_or_wrong_platform_tools(self):
		entry = TOOLS / 'package-veroscript-mobile-release.py'
		for arguments, message in (
			(['android', '--business', 'Android', '--module-symbol-inputs', 'mapping.json'], 'require selecting'),
			(['android', '--business', 'Vm', '--module-symbol-strip', 'strip'], 'require --module-symbol-inputs'),
			(['ios', '--business', 'Vm', '--module-symbol-inputs', 'mapping.json', '--module-symbol-objcopy', 'objcopy'], 'belong to Android')):
			with self.subTest(arguments=arguments):
				result = subprocess.run([sys.executable, str(entry), *arguments, '--plan'], capture_output=True, text=True)
				self.assertNotEqual(result.returncode, 0)
				self.assertIn(message, result.stderr)

	def test_mobile_hosts_reuse_the_complete_canonical_csharp_inventory(self):
		from veroscript_csharp_packages import PACKAGE_IDS
		feed = self.root / 'canonical-csharp'
		feed.mkdir()
		main = self.inputs['android']['CSharp'][0]
		shutil.copy2(main, feed / main.name)
		frameworks = ('net8.0', 'net9.0', 'net10.0-android36.0', 'net10.0-ios26.0')
		for package_id in PACKAGE_IDS:
			for suffix in ('nupkg', 'snupkg'):
				path = feed / f'{package_id}.0.1.1.{suffix}'
				if path.exists():
					continue
				symbols = suffix == 'snupkg'
				kind = 'SymbolsPackage' if symbols else 'DotnetTool' if package_id.endswith('.Tool') else 'Dependency'
				entries = {package_id + '.nuspec': ('<package><metadata><id>' + package_id + '</id><version>0.1.1</version><packageTypes><packageType name="' + kind + '"/></packageTypes></metadata></package>').encode()}
				if package_id == 'VeroScript.CSharp':
					entries.update({f'lib/{tfm}/VeroScriptCSharp.pdb': b'BSJB portable PDB fixture' for tfm in frameworks})
				else:
					folder = 'tools/net8.0/any/' if package_id.endswith('.Tool') else 'lib/net8.0/'
					entries[folder + 'Tool.' + ('pdb' if symbols else 'dll')] = b'BSJB managed fixture'
				write_zip(path, entries)
		with patch.dict(os.environ, {'VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY': str(feed)}):
			for target in ('android', 'ios'):
				candidate = self.stage(target, inputs={'CSharp': [main]}, name='canonical-' + target)
				self.assertEqual(len(json.loads((candidate / 'release-manifest.json').read_text())['artifacts']), 12)
				publish_businesses(REPO, candidate, self.output)
				verify_collection(self.output, target, REPO)
			wrong = self.root / main.name
			wrong.write_bytes(main.read_bytes() + b'changed input')
			with self.assertRaisesRegex(ValueError, 'differs from the canonical common feed'):
				self.stage(inputs={'CSharp': [wrong]}, name='must-stay-absent')
			self.assertFalse((self.root / 'must-stay-absent').exists())

	def test_complete_android_and_ios_use_platform_specific_completeness(self):
		for target in ('android', 'ios'):
			with self.subTest(target=target):
				candidate = self.stage(target, name='candidate-' + target)
				publish_businesses(REPO, candidate, self.output)
				index = verify_collection(self.output, target, REPO, require_complete=True)
				self.assertEqual(set(index['businesses']), set(target_businesses(target)))
				for business, item in index['businesses'].items():
					self.assertEqual(item['version'], '0.1.1')
					scope = item['manifests'][0]['target']
					leaf = business_root(self.output, business, scope) / item['version']
					manifest = read_business(leaf, scope)
					self.assertNotIn('pythonWheelVersions', manifest)
					self.assertNotIn('partial', manifest)
					self.assertTrue((leaf / 'checksums.txt').is_file())
					self.assertTrue(all('Installer' not in a['path'] for a in manifest['artifacts']))
		self.assertTrue(collection_index(self.output, 'android').is_file())
		self.assertTrue(collection_index(self.output, 'ios').is_file())

	def test_public_cli_collects_inputs_and_verifies_a_copied_collection(self):
		inputs = self.inputs['ios']
		result = subprocess.run([sys.executable, '-B', str(TOOLS / 'package-veroscript-mobile-release.py'), 'ios',
			'--native-sdk', str(inputs['iOS'][0]), '--csharp-package', str(inputs['CSharp'][0]),
			'--native-cmake-sdk', str(inputs['Cpp'][0].parent),
			'--python-sdk-directory', str(inputs['Python'][0]), '--python-sdk-directory', str(inputs['Python'][1]),
			'--out-root', str(self.output)], capture_output=True, text=True, encoding='utf-8', errors='replace')
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		copied = self.root / 'copied and renamed'
		shutil.copytree(self.output, copied)
		result = subprocess.run([sys.executable, '-B', str(TOOLS / 'verify-veroscript-release.py'),
			'--release-directory', str(copied), '--target', 'ios', '--require-complete'],
			capture_output=True, text=True, encoding='utf-8', errors='replace')
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_python_missing_minor_is_rejected_unless_explicitly_partial_then_merges(self):
		inputs = {**self.inputs['android'], 'Python': [self.inputs['android']['Python'][0]]}
		with self.assertRaisesRegex(ValueError, 'Incomplete.*Python'):
			self.stage(inputs=inputs)
		candidate = self.stage(inputs=inputs, partial=True, name='partial')
		publish_businesses(REPO, candidate, self.output)
		with self.assertRaisesRegex(ValueError, 'partial'):
			verify_collection(self.output, 'android', require_complete=True)
		complete = self.stage(name='complete')
		new_abi = self.root / 'new-abi'
		new_abi.mkdir()
		file = next((complete / 'mobile').glob('*cp314.zip'))
		shutil.copy2(file, new_abi / file.name)
		publish_component(REPO, new_abi, self.output, 'Python', '0.1.1', 'android')
		index = verify_collection(self.output, 'android', REPO, require_complete=True)
		leaf = business_root(self.output, 'Python', 'android') / index['businesses']['Python']['version']
		self.assertEqual(len(read_business(leaf, 'android')['artifacts']), 2)

	def test_independent_business_versions_and_platforms_and_obsolete_pruning(self):
		publish_businesses(REPO, self.stage(), self.output)
		publish_businesses(REPO, self.stage('ios', name='ios-candidate'), self.output)
		config = copy.deepcopy(self.config)
		config['businesses']['Android'] = '0.1.2'
		candidate = self.stage(inputs={'Android': self.inputs['android']['Android']}, config=config, name='new-android')
		publish_businesses(REPO, candidate, self.output)
		index = verify_collection(self.output, 'android', require_complete=True)
		self.assertEqual(index['businesses']['Android']['version'], '0.1.2')
		self.assertEqual(index['businesses']['Python']['version'], '0.1.1')
		self.assertFalse((business_root(self.output, 'Android', 'android') / '0.1.1').exists())
		self.assertEqual(verify_collection(self.output, 'ios', require_complete=True)['businesses']['Python']['version'], '0.1.1')

	def test_failed_commit_restores_the_previous_index_and_payloads(self):
		publish_businesses(REPO, self.stage(), self.output)
		before = {p.relative_to(self.output).as_posix(): p.read_bytes() for p in self.output.rglob('*') if p.is_file()}
		config = copy.deepcopy(self.config)
		config['businesses']['Android'] = '0.1.2'
		candidate = self.stage(inputs={'Android': self.inputs['android']['Android']}, config=config, name='new')
		verify = verify_collection
		def fail_after_swap(root, target, *args, **kwargs):
			index = json.loads(collection_index(root, target).read_text())
			if index['businesses'].get('Android', {}).get('version') == '0.1.2':
				raise OSError('simulated verification interruption')
			return verify(root, target, *args, **kwargs)
		with patch('veroscript_business_release.verify_collection', side_effect=fail_after_swap), \
				patch('veroscript_business_release.scratch_directory', return_value=self.root):
			with self.assertRaisesRegex(OSError, 'interruption'):
				publish_businesses(REPO, candidate, self.output)
		after = {p.relative_to(self.output).as_posix(): p.read_bytes() for p in self.output.rglob('*') if p.is_file()}
		self.assertEqual(before, after)
		verify_collection(self.output, 'android', require_complete=True)

	def test_android_rejects_a_missing_abi_and_stale_source_assets(self):
		native = self.root / 'native'
		shutil.copytree(self.inputs['android']['Android'][0], native)
		aar = next(native.glob('*.aar'))
		self.rewrite_zip(aar, lambda entries: entries.pop('jni/x86_64/libVeroScriptInterop.so'))
		with self.assertRaisesRegex(ValueError, 'ABI dependency'):
			verify_native_sdk(native, 'android', '0.1.1')
		shutil.copy2(next(self.inputs['android']['Android'][0].glob('*.aar')), aar)
		self.rewrite_zip(aar, lambda entries: entries.update({'assets/veroscript/stdlib/core.vs': b'stale'}))
		verify_native_sdk(native, 'android', '0.1.1')
		with self.assertRaisesRegex(ValueError, 'stale Android standard'):
			verify_native_sdk(native, 'android', '0.1.1', REPO)

	def test_ios_rejects_a_macos_archive_even_for_arm64(self):
		native = self.root / 'native'
		shutil.copytree(self.inputs['ios']['iOS'][0], native)
		(native / 'VeroScriptIOS.xcframework/ios-arm64/mobile.a').write_bytes(ar(macho(platform=1)))
		with self.assertRaisesRegex(ValueError, 'Apple platform'):
			verify_native_sdk(native, 'ios', '0.1.1')

	def test_csharp_rejects_desktop_only_nuget_and_stale_build_integration(self):
		package = self.root / self.inputs['android']['CSharp'][0].name
		shutil.copy2(self.inputs['android']['CSharp'][0], package)
		self.rewrite_zip(package, lambda entries: entries.pop('lib/net10.0-ios26.0/VeroScriptCSharp.dll'))
		with self.assertRaisesRegex(ValueError, 'managed assemblies'):
			verify_csharp_package(package, '0.1.1')
		shutil.copy2(self.inputs['android']['CSharp'][0], package)
		self.rewrite_zip(package, lambda entries: entries.update({'buildTransitive/VeroScript.CSharp.targets': b'<Project/>'}))
		verify_csharp_package(package, '0.1.1')
		with self.assertRaisesRegex(ValueError, 'Stale C#'):
			verify_csharp_package(package, '0.1.1', REPO)

	def test_csharp_rejects_native_dll_masquerading_as_managed(self):
		package = self.root / self.inputs['android']['CSharp'][0].name
		shutil.copy2(self.inputs['android']['CSharp'][0], package)
		self.rewrite_zip(package, lambda entries: entries.update({'lib/net10.0-ios26.0/VeroScriptCSharp.dll': b'MZ' + bytes(1000)}))
		with self.assertRaisesRegex(ValueError, 'managed assemblies'):
			verify_csharp_package(package, '0.1.1')

	def test_python_platform_matrix_and_duplicate_minor_are_rejected(self):
		with self.assertRaisesRegex(ValueError, 'platform/ABI matrix'):
			verify_python_sdk(self.inputs['ios']['Python'][0], 'android', '0.1.1')
		with self.assertRaisesRegex(ValueError, 'Duplicate mobile CPython'):
			self.stage(inputs={'Python': [self.inputs['android']['Python'][0]] * 2})

	def test_checksum_index_and_unowned_file_tampering_are_rejected(self):
		publish_businesses(REPO, self.stage(), self.output)
		index = verify_collection(self.output, 'android')
		leaf = business_root(self.output, 'Android', 'android') / '0.1.1'
		(leaf / 'unowned.txt').write_text('keep user data')
		with self.assertRaisesRegex(ValueError, 'unowned'):
			verify_collection(self.output, 'android')
		(leaf / 'unowned.txt').unlink()
		file = leaf / read_business(leaf, 'android')['artifacts'][0]['path']
		with file.open('ab') as stream:
			stream.write(b'tamper')
		with self.assertRaisesRegex(ValueError, 'Missing/stale'):
			verify_collection(self.output, 'android')

	def test_archive_paths_and_auxiliary_content_are_rejected_before_extraction(self):
		candidate = self.stage(inputs={'Android': self.inputs['android']['Android']})
		archive = next((candidate / 'mobile').glob('*.zip'))
		self.rewrite_zip(archive, lambda entries: entries.update({'../escape.txt': b'bad'}))
		write_manifest(candidate, '0.1.1', 'android', 'a' * 64, business_versions=self.config['businesses'], native_version='0.1.1')
		path = candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest.update(selectedBusinesses=['Android'], businessBuildIds={'Android': digest(archive)})
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'Unsafe|Invalid'):
			verify_release(candidate)
		self.assertFalse((self.root / 'escape.txt').exists())

	def test_preview_is_read_only_and_mobile_routes_have_no_desktop_artifacts(self):
		before = set(self.root.iterdir())
		plan = release_plan(REPO, self.config, ['android', 'ios'])
		tree = plan['directories']['Release']['Extensions']
		self.assertIn('VeroScriptAndroidSDK-0.1.1-android.zip', tree['Platform']['Mobile']['Android']['android']['0.1.1']['sdks'])
		self.assertIn('VeroScriptIOSSDK-0.1.1-ios.zip', tree['Platform']['Mobile']['iOS']['ios']['0.1.1']['sdks'])
		text = json.dumps(plan)
		self.assertNotIn('Installer-', text)
		self.assertNotIn('.whl', text)
		self.assertNotIn('Runtime', tree)
		self.assertNotIn('Android', INSTALLER_BUSINESSES)
		self.assertEqual(set(self.root.iterdir()), before)
		result = subprocess.run([sys.executable, '-B', str(TOOLS / 'package-veroscript-mobile-release.py'), 'ios', '--plan'], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stderr)
		self.assertTrue(json.loads(result.stdout)['previewOnly'])

	def test_invalid_business_or_missing_inputs_fail_before_creating_output(self):
		for arguments in (['ios', '--business', 'Jit'], ['android'], ['android', '--business', 'Runtime']):
			result = subprocess.run([sys.executable, '-B', str(TOOLS / 'package-veroscript-mobile-release.py'), *arguments,
				'--out-root', str(self.output)], capture_output=True, text=True)
			self.assertNotEqual(result.returncode, 0)
			self.assertFalse(self.output.exists())

	def test_managed_mobile_release_paths_and_linked_inputs_are_protected(self):
		for business in ('Android', 'iOS', 'Cpp', 'CSharp', 'Python'):
			with self.assertRaises(ValueError):
				safe_output(REPO, REPO / 'Release/Extensions' / business / 'android', component=True)
		with self.assertRaisesRegex(ValueError, 'not supported'):
			release_plan(REPO, self.config, ['ios'], ['Android'])

	def test_cpp_mobile_business_has_its_own_sdk_without_a_desktop_core_dependency(self):
		for target in ('android', 'ios'):
			candidate = self.stage(target, inputs={'Cpp': self.inputs[target]['Cpp']}, name='cpp-' + target)
			publish_businesses(REPO, candidate, self.output)
			index = verify_collection(self.output, target, REPO)
			self.assertEqual(set(index['businesses']), {'Cpp'})
			leaf = business_root(self.output, 'Cpp', target) / '0.1.1'
			manifest = read_business(leaf, target)
			self.assertEqual(manifest['dependencies'], {})
			self.assertEqual(manifest['mobileNativeSdkPolicyVersion'], 1)
			self.assertNotIn('modulePackagePolicyVersion', manifest)

	def test_candidate_output_is_source_relative_from_another_working_directory(self):
		candidate = self.root / 'candidate from elsewhere'
		result = subprocess.run([sys.executable, '-B', str(TOOLS / 'package-veroscript-mobile-release.py'), 'android',
			'--business', 'Android', '--native-sdk', str(self.inputs['android']['Android'][0]),
			'--candidate-directory', str(candidate.relative_to(REPO))], cwd=self.root,
			capture_output=True, text=True, encoding='utf-8', errors='replace')
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertEqual(verify_release(candidate)['selectedBusinesses'], ['Android'])
		self.assertFalse(self.output.exists())

	def test_existing_nuget_distribution_publisher_routes_mobile_components(self):
		source = self.root / 'nuget'
		source.mkdir()
		file = self.inputs['android']['CSharp'][0]
		shutil.copy2(file, source / file.name)
		result = subprocess.run([sys.executable, '-B', str(TOOLS / 'veroscript_business_release.py'), 'publish-distributions',
			'--candidate', str(source), '--release-directory', str(self.output), '--target', 'android'],
			capture_output=True, text=True, encoding='utf-8', errors='replace')
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		index = verify_collection(self.output, 'android', REPO)
		self.assertEqual(set(index['businesses']), {'CSharp'})
		with self.assertRaisesRegex(ValueError, 'missing.*businesses'):
			verify_collection(self.output, 'android', require_complete=True)

	def test_collection_materialization_retains_the_mobile_contract(self):
		publish_businesses(REPO, self.stage(), self.output)
		candidate = self.root / 'materialized'
		result = subprocess.run([sys.executable, '-B', str(TOOLS / 'veroscript_business_release.py'), 'materialize',
			'--candidate', str(candidate), '--release-directory', str(self.output), '--target', 'android'],
			capture_output=True, text=True, encoding='utf-8', errors='replace')
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertEqual(set(verify_release(candidate)['selectedBusinesses']), set(target_businesses('android')))

	def test_optional_android_backend_is_a_module_and_ios_rejects_it(self):
		aar = self.root / 'vs-asmjit-0.1.1-android.aar'
		entries = {name: b'license' for name in ('AndroidManifest.xml', 'assets/veroscript/licenses/asmjit/LICENSE',
			'assets/veroscript/licenses/asmjit/LICENSE.md', 'assets/veroscript/licenses/asmjit/VERSION.txt')}
		for name, source in {'LICENSE': 'LICENSE', 'LICENSE.md': 'Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md',
			'VERSION.txt': 'Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/VERSION.txt'}.items():
			entries['assets/veroscript/licenses/asmjit/' + name] = (REPO / source).read_bytes()
		for abi, machine in (('arm64-v8a', 183), ('x86_64', 62)):
			entries[f'jni/{abi}/libVeroScriptJitAsmJit.so'] = elf(machine=machine)
		write_zip(aar, entries)
		candidate = self.stage(inputs={'Jit': [aar]})
		publish_businesses(REPO, candidate, self.output)
		leaf = self.output / 'Modules/Execution/Jit/android/0.1.1'
		self.assertTrue((leaf / 'packages' / aar.name).is_file())
		self.assertFalse(any('Installer' in p.name for p in leaf.iterdir()))
		with self.assertRaisesRegex(ValueError, 'Invalid mobile platform'):
			self.stage('ios', inputs={'Jit': [aar]}, name='bad-ios')

	def test_aot_source_comparison_is_additional_to_historical_validation(self):
		native = self.root / 'old source SDK'
		shutil.copytree(self.inputs['android']['Android'][0], native)
		path = native / 'mobile-sdk.json'
		metadata = json.loads(path.read_text())
		old_abi = metadata['aotHeaders']['publicAbi']
		metadata['aotHeaders']['publicAbi'] = '0' * 64
		path.write_text(json.dumps(metadata))
		header = native / 'aot/include/VeroScriptJitBuild.h'
		header.write_bytes(header.read_bytes().replace(old_abi.encode(), b'0' * 64))
		verify_native_sdk(native, 'android', '0.1.1')
		with self.assertRaisesRegex(ValueError, 'current Core sources'):
			verify_native_sdk(native, 'android', '0.1.1', REPO)

	def test_renamed_python_archive_cannot_change_its_cpython_abi(self):
		candidate = self.stage(inputs={'Python': [self.inputs['android']['Python'][0]]}, partial=True)
		archive = next((candidate / 'mobile').iterdir())
		replacement = archive.with_name(archive.name.replace('cp313', 'cp314'))
		with zipfile.ZipFile(archive) as package:
			entries = {name.replace(archive.stem + '/', replacement.stem + '/'): package.read(name) for name in package.namelist()}
		write_zip(replacement, entries)
		archive.unlink()
		write_manifest(candidate, '0.1.1', 'android', 'a' * 64, business_versions=self.config['businesses'], native_version='0.1.1')
		path = candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest.update(selectedBusinesses=['Python'], partialBusinesses=['Python'], businessBuildIds={'Python': digest(replacement)})
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'filename/ABI mismatch'):
			verify_release(candidate)

	def test_mobile_linked_input_is_rejected(self):
		link = self.root / 'linked SDK'
		try:
			link.symlink_to(self.inputs['android']['Android'][0], target_is_directory=True)
		except OSError:
			self.skipTest('Creating symlinks requires Windows Developer Mode or privilege')
		with self.assertRaisesRegex(ValueError, 'Linked|linked'):
			self.stage(inputs={'Android': [link]})


	def test_host_interpreters_are_selected_separately_for_each_target_minor(self):
		def identity(executable):
			minor = '3.13' if executable == 'host-3.13' else '3.14'
			return {'version': minor, 'executable': executable, 'implementation': 'cpython', 'debug': False, 'freeThreaded': False}
		with patch('veroscript_python_wheels.interpreter_identity', side_effect=identity), patch('veroscript_mobile_release.shutil.which', return_value=None):
			self.assertEqual(host_interpreters(MOBILE_PYTHON_VERSIONS, ['host-3.13', 'host-3.14']),
				{'3.13': 'host-3.13', '3.14': 'host-3.14'})
			with self.assertRaisesRegex(ValueError, 'Missing matching HOST CPython'):
				host_interpreters(MOBILE_PYTHON_VERSIONS, ['host-3.14'])

	def test_free_threaded_host_interpreter_is_rejected_before_building(self):
		identity = {'version': '3.14', 'executable': 'free-threaded', 'implementation': 'cpython', 'debug': False, 'freeThreaded': True}
		with patch('veroscript_python_wheels.interpreter_identity', return_value=identity):
			with self.assertRaisesRegex(ValueError, 'non-free-threaded'):
				host_interpreters(['3.14'], ['free-threaded'])

	def test_python_build_dispatch_uses_the_matching_host_for_each_minor(self):
		entry = load_tool('Resources/Tools/Packaging/package-veroscript-mobile-release.py', REPO)
		records = {}
		arguments = ['mobile-release', 'android', '--business', 'Python']
		for minor in MOBILE_PYTHON_VERSIONS:
			for architecture in ('arm64-v8a', 'x86_64'):
				descriptor = self.root / (minor + '-' + architecture + '.json')
				descriptor.write_text('{}')
				records[descriptor] = {'pythonVersion': minor + '.7', 'platform': 'android', 'architecture': architecture, 'variant': 'device'}
				arguments += ['--python-target-sdk', str(descriptor)]
			arguments += ['--host-python', 'host-' + minor]
		ndk = self.root / 'ndk'
		toolchain = ndk / 'build/cmake/android.toolchain.cmake'
		toolchain.parent.mkdir(parents=True)
		toolchain.write_text('# fixture')
		candidate = self.root / 'built-candidate'
		arguments += ['--ndk', str(ndk), '--candidate-directory', str(candidate)]
		def identity(executable):
			return {'version': '3.13' if executable == 'host-3.13' else '3.14', 'executable': executable,
				'implementation': 'cpython', 'debug': False, 'freeThreaded': False}
		seen = []
		def producer(*command):
			command = list(map(str, command))
			descriptor = Path(command[command.index('--sdk') + 1])
			minor = '.'.join(records[descriptor]['pythonVersion'].split('.')[:2])
			self.assertEqual(command[command.index('--host-python') + 1], 'host-' + minor)
			output = Path(command[command.index('--out-root') + 1])
			shutil.copytree(self.inputs['android']['Python'][MOBILE_PYTHON_VERSIONS.index(minor)], output)
			seen.append(minor)
		with patch.object(sys, 'argv', arguments), patch.object(entry, 'load_tool', return_value=SimpleNamespace(inspect_sdk=records.__getitem__)), \
				patch.object(entry, 'command', side_effect=producer), patch('veroscript_python_wheels.interpreter_identity', side_effect=identity), \
				patch('veroscript_mobile_release.shutil.which', return_value=None), \
				patch.object(entry, 'collection_root', side_effect=AssertionError('Candidate-only assembly must not open Release')):
			entry.main()
		self.assertEqual(seen, list(MOBILE_PYTHON_VERSIONS))
		self.assertEqual(verify_release(candidate)['selectedBusinesses'], ['Python'])

	def test_python_stale_acceptance_tool_is_rejected_even_after_rehash(self):
		deployment = self.root / 'python'
		shutil.copytree(self.inputs['android']['Python'][0], deployment)
		(deployment / 'device_smoke.py').write_text('# stale acceptance protocol\n')
		path = deployment / 'python-mobile.json'
		metadata = json.loads(path.read_text())
		metadata['files']['device_smoke.py'] = digest(deployment / 'device_smoke.py')
		path.write_text(json.dumps(metadata))
		verify_python_sdk(deployment, 'android', '0.1.1')
		with self.assertRaisesRegex(ValueError, 'Stale Python mobile'):
			verify_python_sdk(deployment, 'android', '0.1.1', REPO)

	def test_shared_csharp_symbols_are_immutable_and_a_new_version_drops_old_symbols(self):
		from mobile_release_fixtures import write_zip
		symbols = self.root / 'VeroScript.CSharp.0.1.1.snupkg'
		frameworks = ['net8.0', 'net9.0', 'net10.0-android36.0', 'net10.0-ios26.0']
		entries = {f'lib/{framework}/VeroScriptCSharp.pdb': b'BSJB portable PDB fixture' for framework in frameworks}
		entries['VeroScript.CSharp.nuspec'] = b'<package><metadata><id>VeroScript.CSharp</id><version>0.1.1</version><packageTypes><packageType name="SymbolsPackage"/></packageTypes></metadata></package>'
		write_zip(symbols, entries)
		inputs = {'CSharp': [self.inputs['android']['CSharp'][0], symbols]}
		publish_businesses(REPO, self.stage(inputs=inputs), self.output)
		leaf = business_root(self.output, 'CSharp', 'any') / '0.1.1'
		self.assertTrue((leaf / 'symbols' / symbols.name).is_file())
		source = self.root / 'replacement'
		source.mkdir()
		package = source / inputs['CSharp'][0].name
		shutil.copy2(inputs['CSharp'][0], package)
		with self.assertRaisesRegex(ValueError, 'Shared artifact identity differs'):
			publish_component(REPO, source, self.output, 'CSharp', '0.1.1', 'android')
		self.rewrite_zip(package, lambda files: files.update({'VeroScript.CSharp.nuspec': files['VeroScript.CSharp.nuspec'].replace(b'0.1.1', b'0.1.2')}))
		package.rename(source / 'VeroScript.CSharp.0.1.2.nupkg')
		publish_component(REPO, source, self.output, 'CSharp', '0.1.2', 'android')
		current = business_root(self.output, 'CSharp', 'any') / '0.1.2'
		self.assertFalse((current / 'symbols').exists())
		verify_collection(self.output, 'android', REPO)

	def test_native_ios_sdk_rejects_a_missing_adapter_header(self):
		native = self.root / 'incomplete ios'
		shutil.copytree(self.inputs['ios']['iOS'][0], native)
		(native / 'VeroScriptIOS.xcframework/ios-arm64/Headers/VeroScriptIOS.h').unlink()
		with self.assertRaisesRegex(ValueError, 'adapter header/module map'):
			verify_native_sdk(native, 'ios', '0.1.1')


if __name__ == '__main__':
	unittest.main()
