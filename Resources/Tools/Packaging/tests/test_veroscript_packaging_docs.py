# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Documentation must pass before native packaging can alter release outputs."""
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_veroscript_docs as fixture_docs
from veroscript_docs import publish_docs

TOOLS = Path(__file__).resolve().parents[1]


class DocumentationOutputTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix='vero docs output ')
		self.addCleanup(temporary.cleanup)
		self.repo = Path(temporary.name).resolve()
		self.module = self.repo / 'Engine Sources'
		self.module.mkdir()
		header = self.module / 'VeroScript/Public/VeroScriptVersion.h'
		header.parent.mkdir(parents=True)
		header.write_text('// Source module marker\n')

	def test_direct_exports_cannot_change_managed_release_inventory(self):
		for route, marker in (
				('Engine Sources/Release/win-x64', 'release-index-win-x64.json'),
				('Engine Sources/Release/Resources/linux-x64/1.0.0', 'release-manifest-linux-x64.json'),
				('.tmp/complete-release', 'release-manifest.json')):
			with self.subTest(route=route):
				root = self.repo / route
				root.mkdir(parents=True)
				identity = root / marker
				identity.write_bytes(b'{}')
				destination = root / 'UnexpectedDocs'
				with patch('veroscript_docs.publication_bundle') as bundle:
					with self.assertRaisesRegex(ValueError, 'Component output cannot|complete release collection'):
						publish_docs(self.repo, destination)
				bundle.assert_not_called()
				self.assertFalse(destination.exists())
				self.assertEqual([identity], list(root.iterdir()))
				self.assertEqual(b'{}', identity.read_bytes())

	def test_direct_exports_reject_collection_roots_and_source_trees(self):
		for route in ('.tmp', 'dist', 'Engine Sources/Release',
					  'Engine Sources/Release/linux-x64', 'Engine Sources/Release/Runtime',
					  'Engine Sources/Release/Runtime/win-x64', 'Engine Sources/Release/Extensions/Language/Python/linux-x64',
					  'Engine Sources/Release/docs-export', 'Engine Sources/Release/Modules/Execution/Jit', 'Engine Sources/Resources/Docs'):
			with self.subTest(route=route), patch('veroscript_docs.publication_bundle') as bundle:
				with self.assertRaises(ValueError):
					publish_docs(self.repo, self.repo / route)
				bundle.assert_not_called()
				self.assertFalse((self.repo / route).exists())

	def test_direct_exports_allow_dedicated_staging_and_preserve_existing_files(self):
		for route in ('.tmp/docs-export', 'dist/docs-export'):
			with self.subTest(route=route):
				destination = self.repo / route
				with patch('veroscript_docs.publication_bundle', return_value={'index.md': b'# User\n'}):
					self.assertEqual(1, publish_docs(self.repo, destination, 'User'))
				with patch('veroscript_docs.publication_bundle') as bundle:
					with self.assertRaisesRegex(ValueError, 'must be empty'):
						publish_docs(self.repo, destination, 'User')
				bundle.assert_not_called()
				self.assertEqual(b'# User\n', (destination / 'index.md').read_bytes())


class PackagingDocumentationTests(unittest.TestCase):
	def setUp(self):
		self.fixture = fixture_docs.DocumentationTests()
		self.fixture.setUp()
		self.addCleanup(self.fixture.doCleanups)
		self.fixture.prepare_collection()
		self.repo = self.fixture.repo
		source = self.fixture.root / 'User/en-US/index.md'
		source.write_text('# User\n[Missing](missing.md)\n', encoding='utf-8')
		versions = self.repo / 'Script/Resources/Tools/Packaging/ReleaseVersions.json'
		versions.parent.mkdir(parents=True)
		shutil.copy2(TOOLS / 'ReleaseVersions.json', versions)
		spec = importlib.util.spec_from_file_location('veroscript_packaging_docs_gate', TOOLS / 'package-veroscript-all.py')
		self.packager = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(self.packager)

	def test_full_packager_rejects_invalid_originals_before_build_or_publication(self):
		output = self.repo / '.tmp/Release'
		arguments = ['package-veroscript-all.py', '--repo-root', str(self.repo),
					 '--out-root', str(output), '--skip-build']
		with patch.object(sys, 'argv', arguments), \
				patch.object(self.packager, 'host_target', return_value='linux-x64'), \
				patch.object(self.packager, 'run') as run:
			with self.assertRaisesRegex(ValueError, 'missing'):
				self.packager.main()
		run.assert_not_called()
		self.assertFalse(output.exists())
		self.assertFalse((self.repo / 'Script/Resources/Docs/_generated').exists())

	def test_assembly_rechecks_originals_before_reading_native_products(self):
		release = self.packager.Release.__new__(self.packager.Release)
		release.repo = self.repo
		with patch.object(self.packager, 'native_identity') as native:
			with self.assertRaisesRegex(ValueError, 'missing'):
				release.assemble()
		native.assert_not_called()
		self.assertFalse((self.repo / 'Script/Resources/Docs/_generated').exists())
