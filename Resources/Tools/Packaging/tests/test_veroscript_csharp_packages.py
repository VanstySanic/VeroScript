# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_csharp_packages import PACKAGE_IDS, csharp_package_inventory


class CanonicalCSharpPathsTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix='canonical CSharp paths ')
		self.addCleanup(self.temporary.cleanup)
		self.root = Path(self.temporary.name)
		for package_id in PACKAGE_IDS:
			for suffix in ('nupkg', 'snupkg'):
				with zipfile.ZipFile(self.root / f'{package_id}.0.1.1.{suffix}', 'w') as archive:
					archive.writestr(package_id + '.nuspec', f'<package><metadata><id>{package_id}</id><version>0.1.1</version></metadata></package>')
					archive.writestr('licenses/Roslyn-LICENSE.txt', 'legal notice')

	def test_complete_portable_feed_is_accepted(self):
		self.assertEqual(len(csharp_package_inventory(self.root, '0.1.1')), 12)

	def test_every_package_and_symbol_archive_rejects_noncanonical_paths(self):
		for suffix in ('nupkg', 'snupkg'):
			for bad in ('licenses//other.txt', 'licenses/./other.txt', 'licenses/other. ', 'licenses/CON.txt'):
				with self.subTest(suffix=suffix, path=bad):
					path = self.root / f'VeroScript.CSharpFrontend.0.1.1.{suffix}'
					original = path.read_bytes()
					with zipfile.ZipFile(path, 'a') as archive:
						archive.writestr(bad, 'notice')
					with self.assertRaises(ValueError):
						csharp_package_inventory(self.root, '0.1.1')
					path.write_bytes(original)

	def test_case_aliases_are_rejected_before_feed_staging(self):
		path = self.root / 'VeroScript.CSharpFrontend.0.1.1.nupkg'
		with zipfile.ZipFile(path, 'a') as archive:
			archive.writestr('LICENSES/second.txt', 'notice')
		with self.assertRaises(ValueError):
			csharp_package_inventory(self.root, '0.1.1')


if __name__ == '__main__':
	unittest.main()
