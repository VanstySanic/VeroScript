# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from foundation_fixtures import foundation_files
from test_veroscript_release import elf
from veroscript_foundation_packages import create_installable_foundation, verify_installable_foundation, ENTRY_IDS, native_path
from veroscript_module_packages import shared_files
from veroscript_products import FOUNDATION_BUSINESSES, FOUNDATION_PACKAGE_NAMES
from veroscript_release import archive_entries, archive_tree


class FoundationInstallableTests(unittest.TestCase):
	def test_sdk_and_installable_products_have_separate_layouts_and_keep_runtime_contracts(self):
		with tempfile.TemporaryDirectory(prefix='vero foundation 包 ') as temporary:
			repo = Path(temporary)
			versions = dict.fromkeys(FOUNDATION_BUSINESSES, '0.1.1')
			roots = {}
			context = {'os': 'linux', 'arch': 'x64', 'environment': 'desktop', 'core': '0.1.1',
					   'coreBuild': 'b' * 64, 'abis': {'native-extension': 'matched-extension'}}
			for business in FOUNDATION_BUSINESSES:
				sdk = repo / 'sdk' / (FOUNDATION_PACKAGE_NAMES[business] + '-0.1.1-linux-x64')
				for name, data in foundation_files(business, versions, 'linux-x64', elf(), b'license').items():
					path = sdk / name
					path.parent.mkdir(parents=True, exist_ok=True)
					path.write_bytes(data)
				if business in ENTRY_IDS:
					path = sdk / native_path(business, 'linux-x64')
					path.parent.mkdir(parents=True, exist_ok=True)
					path.write_bytes(elf())
				roots[business] = sdk
			def describe(command, **kwargs):
				if command[1] == 'target':
					return json.dumps(context)
				business = next(key for key, value in roots.items() if Path(command[2]).is_relative_to(value))
				roles = ['cli'] if business == 'Gui' else ['bindings', 'provider'] if business in {'Sdl3', 'FontBackend'} else ['provider']
				Path(command[5]).write_text('{"version":1}', encoding='utf-8')
				return json.dumps({'id': ENTRY_IDS[business], 'componentVersion': '0.1.1', 'target': context,
					'coreBuild': context['coreBuild'], 'abi': context['abis']['native-extension'], 'roles': roles,
					'backends': [] if business == 'Gui' else [{'service': business.lower(), 'id': business.lower(), 'module': ENTRY_IDS[business]}]})
			def pack(command, *, check):
				stage = Path(command[3])
				output = Path(command[4].removeprefix('--output='))
				output.parent.mkdir(parents=True, exist_ok=True)
				with zipfile.ZipFile(output, 'w') as archive:
					for path in stage.rglob('*'):
						if path.is_file():
							archive.write(path, path.relative_to(stage).as_posix())
			with patch('veroscript_foundation_packages.subprocess.check_output', side_effect=describe), \
					patch('veroscript_foundation_packages.subprocess.run', side_effect=pack):
				create_installable_foundation(repo, roots, repo / 'out', versions, 'linux-x64', repo / 'tool')
			for business, sdk in roots.items():
				output = repo / 'out/modules' / (sdk.name + '.vspkg')
				entries = archive_entries(output)
				verify_installable_foundation(entries, business, '0.1.1', 'linux-x64')
				self.assertIn('veroscript-package.json', entries)
				self.assertFalse((sdk / 'veroscript-package.json').exists())
				self.assertFalse(any(path.startswith('include/') for path in entries))
				self.assertNotIn('CoreSDK', json.loads(entries['veroscript-package.json'][0])['dependencies'])
				portable = repo / (sdk.name + '.tar.gz')
				archive_tree(sdk, portable)
				self.assertTrue(all(path.startswith(sdk.name + '/') for path in archive_entries(portable)))
				modified = dict(entries)
				modified['include/development.h'] = (b'header', 0)
				with self.assertRaisesRegex(ValueError, 'SDK-only'):
					verify_installable_foundation(modified, business, '0.1.1', 'linux-x64')

	def test_internal_soname_aliases_are_selected_and_external_library_links_are_rejected(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			sdk = root / 'sdk'
			sdk.mkdir()
			library = sdk / 'libSDL3.so.0.4.0'
			library.write_bytes(elf())
			try:
				(sdk / 'libSDL3.so.0').symlink_to(library.name)
			except OSError as error:
				self.skipTest(str(error))
			self.assertEqual(len(shared_files(sdk)), 1)
			self.assertEqual(len(shared_files(sdk, aliases=True)), 2)
			(root / 'outside.so').write_bytes(elf())
			(sdk / 'outside.so').symlink_to(root / 'outside.so')
			with self.assertRaisesRegex(ValueError, 'escapes'):
				shared_files(sdk)


if __name__ == '__main__':
	unittest.main()
