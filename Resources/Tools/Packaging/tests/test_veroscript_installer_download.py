# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import hashlib
import sys
from pathlib import Path
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from installer_fixtures import installer_apphost
from test_veroscript_release import elf
from veroscript_business_release import load_versions
from veroscript_installer import package_unix_installer, verify_apphost, verify_linux_download, verify_installer_download
from veroscript_release import verify_release, write_manifest, digest
from veroscript_release_plan import release_plan

REPO = Path(__file__).resolve().parents[4]


class InstallerDownloadTests(unittest.TestCase):
	def setUp(self):
		work = tempfile.TemporaryDirectory()
		self.addCleanup(work.cleanup)
		self.root = Path(work.name).resolve()
		self.program = self.root / 'VeroScriptInstaller'
		self.payload = b'content' * (1024 * 384)
		self.checksum = hashlib.sha256(self.payload).hexdigest()

	def test_portable_audit_reads_multiple_compressed_parts(self):
		self.program.write_bytes(installer_apphost(elf('arm64'), self.payload, parts=2, compress=True))
		self.assertEqual(verify_apphost(self.program, 'linux-arm64', self.checksum), self.checksum)
		with self.assertRaisesRegex(ValueError, 'architecture mismatch'):
			verify_apphost(self.program, 'linux-x64', self.checksum)
		with self.assertRaisesRegex(ValueError, 'verified bundle SHA-256'):
			verify_apphost(self.program, 'linux-arm64', 'b' * 64)

	def test_tampered_content_does_not_match_assembly_metadata(self):
		self.program.write_bytes(installer_apphost(elf(), b'changed payload', checksum=self.checksum))
		with self.assertRaisesRegex(ValueError, 'verified bundle SHA-256'):
			verify_apphost(self.program, 'linux-x64')

	def test_source_audit_checks_the_windows_icon_even_after_a_cached_payload_audit(self):
		from test_veroscript_executable_icons import pe_with_icon
		program = self.root / 'VeroScriptRuntimeInstaller-0.1.1-win-arm64.exe'
		official = REPO / 'Resources/Assets/icons/veroscript-icon.ico'
		stale = REPO / 'Resources/Assets/file-associations/icons/veroscript-source.ico'
		program.write_bytes(installer_apphost(pe_with_icon(official, 'arm64'), b'payload'))
		verify_installer_download(program, 'Runtime', '0.1.1', 'win-arm64', digest(program), REPO)
		program.write_bytes(installer_apphost(pe_with_icon(stale, 'arm64'), b'payload'))
		verify_installer_download(program, 'Runtime', '0.1.1', 'win-arm64', digest(program))
		with self.assertRaisesRegex(ValueError, 'Missing/stale Windows executable icon'):
			verify_installer_download(program, 'Runtime', '0.1.1', 'win-arm64', digest(program), REPO)

	def test_linux_wrap_preserves_permission_and_does_not_publish_a_failed_replacement(self):
		self.program.write_bytes(installer_apphost(elf(), self.payload))
		output = self.root / 'downloads'
		arguments = (REPO, self.program, output, 'Runtime', '0.1.1', 'linux-x64')
		archive = package_unix_installer(*arguments, self.checksum)
		self.assertEqual(list(output.iterdir()), [archive])
		self.assertEqual(verify_linux_download(archive, 'linux-x64'), self.checksum)
		previous = archive.read_bytes()
		self.program.write_bytes(installer_apphost(elf(), b'changed payload'))
		with self.assertRaisesRegex(ValueError, 'verified bundle SHA-256'):
			package_unix_installer(*arguments, self.checksum)
		self.assertEqual(list(output.iterdir()), [archive])
		self.assertEqual(archive.read_bytes(), previous)

	def test_macos_plan_and_verifier_require_dmg_on_both_architectures(self):
		targets = ['macos-x64', 'macos-arm64']
		plan = release_plan(REPO, load_versions(REPO), targets, ['Runtime'])['directories']['Release']['Runtime']
		for target in targets:
			with self.subTest(target=target):
				name = f'VeroScriptRuntimeInstaller-0.1.1-{target}'
				self.assertEqual(list(plan[target]['0.1.1']['installers']), [name + '.dmg'])
				candidate = self.root / target
				(candidate / 'installer').mkdir(parents=True)
				(candidate / 'installer' / (name + '.tar.gz')).write_bytes(b'wrong container')
				write_manifest(candidate, '0.1.1', target, '0' * 64)
				with self.assertRaisesRegex(ValueError, 'Unexpected/missing release products'):
					verify_release(candidate, businesses={'Runtime'}, partial_businesses={'Runtime'})
