# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Read-only contract checks: no archives, SDK stages, builds or publications."""
from pathlib import Path
import stat
import sys
from types import SimpleNamespace
import unittest
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))
from veroscript_mobile_release import (load_tool, mobile_native_versions, optional_artifact_names,
	portable_member, source_native_version, validate_csharp_symbol_entries, validate_zip_members)
from veroscript_products import ALL_BUSINESSES, RELEASE_TARGETS, target_businesses
from veroscript_release_plan import release_plan


class StaticMobileContracts(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.entry = load_tool('Resources/Tools/Packaging/package-veroscript-mobile-release.py', REPO)

	def options(self, **changes):
		values = dict(platform='android', csharp_package=None, csharp_symbols=None, csharp_native_root=None,
			python_sdk_directory=[], python_target_sdk=[], host_python=[], ndk=None, native_sdk=None, jit_aar=None)
		values.update(changes)
		return SimpleNamespace(**values)

	def member(self, name, mode=stat.S_IFREG):
		info = zipfile.ZipInfo(name)
		info.external_attr = mode << 16
		return info

	def test_unselected_inputs_are_rejected_before_inspection_or_builds(self):
		for field, value in [('csharp_native_root', Path('not-inspected')), ('csharp_package', Path('not-inspected')),
			('python_sdk_directory', [Path('not-inspected')]), ('python_target_sdk', [Path('not-inspected')]), ('host_python', ['not-executed'])]:
			with self.subTest(field=field), self.assertRaises(ValueError):
				self.entry.validate_input_options(self.options(**{field: value}), ['Android'])

	def test_reused_python_outputs_do_not_consume_build_options(self):
		for changes in ({'host_python': ['not-executed']}, {'ndk': Path('not-inspected')}):
			with self.assertRaisesRegex(ValueError, 'require --python-target-sdk'):
				self.entry.validate_input_options(self.options(python_sdk_directory=[Path('existing')], **changes), ['Python'])

	def test_native_lookup_and_csharp_symbol_input_contracts(self):
		self.entry.validate_input_options(self.options(native_sdk=Path('sdk')), ['Jit'])
		with self.assertRaises(ValueError):
			self.entry.validate_input_options(self.options(native_sdk=Path('unused'), jit_aar=Path('explicit.aar')), ['Jit'])
		self.entry.validate_input_options(self.options(csharp_package=Path('main.nupkg'), csharp_symbols=Path('symbols.snupkg')), ['CSharp'])
		with self.assertRaises(ValueError):
			self.entry.validate_input_options(self.options(csharp_symbols=Path('unpaired.snupkg')), ['CSharp'])

	def test_windows_aliases_and_noncanonical_archive_paths_are_rejected(self):
		for name in ('native/CON', 'native/nul.txt', 'native/COM1.dll', 'native/lpt\u00b9.txt', 'native/CON .txt',
			'native/foo.', 'native/foo ', 'native/a\x00b', 'native/a?b', './native/file', 'native//file', 'native/../file'):
			with self.subTest(name=name), self.assertRaises(ValueError):
				portable_member(name)
		self.assertEqual(portable_member('VeroScriptIOS.xcframework/ios-arm64/Headers/module.modulemap'),
			'VeroScriptIOS.xcframework/ios-arm64/Headers/module.modulemap')

	def test_archive_directory_case_and_file_directory_collisions(self):
		for names in (('Native/a', 'native/b'), ('a', 'a/b'), ('a/b', 'a'), ('a', 'a')):
			with self.subTest(names=names), self.assertRaises(ValueError):
				validate_zip_members([self.member(name) for name in names])
		validate_zip_members([self.member('native/', stat.S_IFDIR), self.member('native/ios/Info.plist')])

	def test_archive_special_entry_types_are_rejected(self):
		for mode in (stat.S_IFLNK, stat.S_IFIFO, stat.S_IFCHR, stat.S_IFBLK):
			with self.subTest(mode=mode), self.assertRaises(ValueError):
				validate_zip_members([self.member('unexpected', mode)])
		for name in ('native/hidden\x00suffix', 'native\\nonportable'):
			with self.subTest(name=name), self.assertRaises(ValueError):
				validate_zip_members([self.member(name)])

	def test_symbol_packages_are_optional_and_require_the_complete_pdb_matrix(self):
		frameworks = ['net8.0', 'net9.0', 'net10.0-android36.0', 'net10.0-ios26.0']
		entries = {f'lib/{framework}/VeroScriptCSharp.pdb': (b'BSJB', 0) for framework in frameworks}
		validate_csharp_symbol_entries(entries, frameworks)
		for wrong in ({key: value for key, value in entries.items() if 'ios' not in key},
			{**entries, 'lib/net8.0/unexpected.dll': (b'MZ..', 0)},
			{**entries, 'README.md': (b'auxi', 0)}):
			with self.assertRaises(ValueError):
				validate_csharp_symbol_entries(wrong, frameworks)
		expected = {'VeroScript.CSharp.0.1.1.snupkg'}
		for name in ('CSharpFrontend','CSharpBindGen','CSharpBindGen.Tool','CSharpTranspiler','CSharpTranspiler.Tool'):
			expected.update(f'VeroScript.{name}.0.1.1.{suffix}' for suffix in ('nupkg','snupkg'))
		self.assertEqual(set(optional_artifact_names('CSharp','ios','0.1.1')), expected)
		self.assertEqual(optional_artifact_names('iOS', 'ios', '0.1.1'), [])

	def test_unknown_targets_do_not_inherit_desktop_completeness(self):
		for target in ('', 'android-arm64', 'ios-x64', 'unknown'):
			with self.subTest(target=target), self.assertRaises(ValueError):
				target_businesses(target)
		self.assertEqual(len(target_businesses('win-x64')), 31)
		self.assertEqual(target_businesses('ios'), ('iOS', 'CoreSDK', 'Cpp', 'Interop', 'Vm', 'CSharp', 'Python'))

	def test_native_versions_are_preserved_per_business_and_legacy_defaults_work(self):
		selected = ['Android', 'CSharp', 'Python']
		values = {'Android': '0.1.1', 'CSharp': '0.1.0', 'Python': '0.1.1'}
		self.assertEqual(mobile_native_versions({'nativeVersion': '0.1.1', 'businessNativeVersions': values}, selected), values)
		self.assertEqual(mobile_native_versions({'nativeVersion': '0.1.1'}, selected), dict.fromkeys(selected, '0.1.1'))
		with self.assertRaises(ValueError):
			mobile_native_versions({'nativeVersion': '0.1.1', 'businessNativeVersions': {'Android': '0.1.1'}}, selected)

	def test_business_only_preview_has_no_unpublished_platform_indices(self):
		config = {'businesses': dict.fromkeys(ALL_BUSINESSES, '0.1.1')}
		plan = release_plan(REPO, config, list(RELEASE_TARGETS), ['Android'])
		self.assertEqual(plan['targets'], ['android'])
		self.assertEqual(plan['businessVersions'], {'Android': '0.1.1'})
		indices = sorted(plan['directories']['Release']['indexes'])
		self.assertEqual(indices, ['android.json'])
		self.assertEqual(source_native_version(REPO), '0.1.1')


if __name__ == '__main__':
	unittest.main()
