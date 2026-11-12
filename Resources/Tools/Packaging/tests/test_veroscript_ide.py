# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import io
import json
from pathlib import Path
import sys
import stat
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_ide import check_ide_sources, verify_ide_artifact
from veroscript_release import digest, verify_release, write_manifest
from veroscript_business_release import ALL_BUSINESSES, publish_businesses, publish_component, verify_collection


def vsix(path, version='0.1.1', extra=None):
	path.parent.mkdir(parents=True, exist_ok=True)
	files = {'extension/package.json': json.dumps({'name': 'veroscript', 'publisher': 'VanstySanic',
			  'version': version, 'main': './dist/extension.cjs'}),
			 'extension.vsixmanifest': f'<PackageManifest><Metadata><Identity Id="veroscript" Publisher="VanstySanic" Version="{version}"/></Metadata></PackageManifest>',
			 '[Content_Types].xml': '<Types/>', 'extension/dist/extension.cjs': 'exports.activate = () => {};',
			 'extension/readme.md': '# VeroScript\n\nInstall a matching Runtime to run and debug scripts.',
			 'extension/LICENSE.txt': 'MIT', 'extension/THIRD-PARTY-NOTICES.txt': 'Dependency notices'}
	files.update(extra or {})
	with zipfile.ZipFile(path, 'w') as archive:
		for name, value in files.items():
			archive.writestr(name, value)


class IdeReleaseTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory()
		self.addCleanup(temporary.cleanup)
		self.repo = Path(temporary.name).resolve()
		(self.repo / '.tmp').mkdir()
		self.candidate = self.repo / '.tmp/candidate'
		self.artifact = self.candidate / 'ide/VeroScriptVSCode-0.1.1.vsix'
		vsix(self.artifact)
		self.versions = {business: '0.1.1' for business in ALL_BUSINESSES}

	def manifest(self, target='win-x64'):
		write_manifest(self.candidate, '0.1.1', target, digest(self.artifact), business_versions=self.versions)
		path = self.candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest.update(selectedBusinesses=['VSCode'], businessBuildIds={'VSCode': digest(self.artifact)})
		path.write_text(json.dumps(manifest))

	def test_portable_plugin_publication_keeps_independent_identity_on_both_targets(self):
		root = self.repo / '.tmp/collection'
		original = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda path, repo=None, **kw: original(path, **kw)):
			for target in ('win-x64', 'linux-x64'):
				self.manifest(target)
				publish_businesses(self.repo, self.candidate, root)
		for target in ('win-x64', 'linux-x64'):
			index = verify_collection(root, target)
			self.assertEqual(set(index['businesses']), {'VSCode'})
			leaf = root / 'Extensions/Editor/VSCode/VSCode' / 'any' / '0.1.1'
			self.assertEqual(digest(leaf / 'packages' / self.artifact.name), digest(self.artifact))
			manifest = json.loads((leaf / 'release-manifest.json').read_text())
			self.assertEqual(manifest['buildId'], digest(self.artifact))
			self.assertNotIn('partial', manifest)

	def test_rehashed_vsix_with_mismatched_metadata_is_rejected(self):
		vsix(self.artifact, version='9.9.9')
		self.manifest()
		with self.assertRaisesRegex(ValueError, 'identity mismatch'):
			verify_release(self.candidate, businesses={'VSCode'})

	def test_default_audit_honors_declared_plugin_selection(self):
		self.manifest()
		verify_release(self.candidate)
		path = self.candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest['selectedBusinesses'] = ['VSCode', 'VSCode']
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'Invalid release business selection'):
			verify_release(self.candidate)

	def test_full_publication_audit_rejects_a_valid_plugin_only_collection(self):
		self.manifest()
		root = self.repo / '.tmp/collection'
		original = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda path, repo=None, **kw: original(path, **kw)):
			publish_businesses(self.repo, self.candidate, root)
		verify_collection(root, 'win-x64')
		with self.assertRaisesRegex(ValueError, 'Incomplete publication: missing businesses'):
			verify_collection(root, 'win-x64', require_complete=True)

	def test_full_publication_audit_rejects_partial_even_with_complete_coverage(self):
		self.manifest()
		root = self.repo / '.tmp/collection'
		original = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda path, repo=None, **kw: original(path, **kw)):
			publish_businesses(self.repo, self.candidate, root)
		leaf = root / 'Extensions/Editor/VSCode/VSCode/any/0.1.1'
		manifest_path = leaf / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text())
		manifest['partial'] = True
		manifest_path.write_text(json.dumps(manifest))
		root_entry = root / 'release-index.json'
		metadata = json.loads(root_entry.read_text())
		with patch('veroscript_business_release.target_businesses', return_value=('VSCode',)):
			from veroscript_business_release import _index_state
			for file in (root / 'indexes').glob('*.json'):
				value = json.loads(file.read_text())
				for ref in value['businesses']['VSCode']['manifests']:
					ref['sha256'] = digest(manifest_path)
				# Keep the fixture valid up to the explicit full-publication check.
				# Both inventory fields and hashes must reflect the modified manifest.
				value.update(_index_state(root, file.stem, value['businesses']))
				file.write_text(json.dumps(value))
				metadata['indexes'][file.stem]['sha256'] = digest(file)
			root_entry.write_text(json.dumps(metadata))
			with self.assertRaisesRegex(ValueError, 'Incomplete publication: partial businesses'):
				verify_collection(root, 'win-x64', require_complete=True)

	def test_native_or_private_payload_is_rejected(self):
		for entry in ('extension/Private/source.ts', 'extension/veroscript.exe', 'extension/dist/extension.cjs.map'):
			with self.subTest(entry=entry):
				vsix(self.artifact, extra={entry: 'unexpected'})
				with self.assertRaisesRegex(ValueError, 'build inputs'):
					verify_ide_artifact(self.artifact, 'VSCode', '0.1.1')

	def test_details_readme_is_functional_but_other_guides_are_rejected(self):
		verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', functional_only=True)
		for entry in ('extension/Resources/Docs/User/guide.md', 'extension/notes/README.md',
					  'extension/README.md', 'extension/CHANGELOG.md'):
			with self.subTest(entry=entry):
				vsix(self.artifact, extra={entry: 'Repository-only documentation'})
				with self.assertRaisesRegex(ValueError, 'Non-functional'):
					verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', functional_only=True)

	def test_vsix_manifest_cannot_install_under_a_different_extension_identity(self):
		for attribute, value in (('Id', 'other-extension'), ('Publisher', 'other-publisher')):
			with self.subTest(attribute=attribute):
				descriptor = '<PackageManifest><Metadata><Identity Id="veroscript" Publisher="VanstySanic" Version="0.1.1"/></Metadata></PackageManifest>'
				original = 'veroscript' if attribute == 'Id' else 'VanstySanic'
				descriptor = descriptor.replace(f'{attribute}="{original}"', f'{attribute}="{value}"')
				vsix(self.artifact, extra={'extension.vsixmanifest': descriptor})
				self.manifest()
				with self.assertRaisesRegex(ValueError, 'VSIX identity'):
					verify_release(self.candidate)

	def test_case_variant_and_versioned_native_leaks_are_rejected(self):
		for entry in ('extension/PRIVATE/secret.ts', 'extension/Native.DLL', 'extension/libnative.so.1'):
			with self.subTest(entry=entry):
				vsix(self.artifact, extra={entry: 'unexpected'})
				with self.assertRaisesRegex(ValueError, 'build inputs'):
					verify_ide_artifact(self.artifact, 'VSCode', '0.1.1')

	def jetbrains_archive(self, extra=None):
		path = self.candidate / 'ide/VeroScriptJetBrains-0.1.1.zip'
		path.parent.mkdir(parents=True, exist_ok=True)
		jar = io.BytesIO()
		with zipfile.ZipFile(jar, 'w') as archive:
			archive.writestr('META-INF/plugin.xml', '<idea-plugin><id>com.vanstysanic.veroscript</id><version>0.1.1</version><idea-version since-build="262.10315.191"/></idea-plugin>')
			archive.writestr('META-INF/LICENSE', 'MIT')
			archive.writestr('com/vanstysanic/veroscript/Plugin.class', 'compiled class')
			for name, data in extra or []:
				archive.writestr(name, data)
		with zipfile.ZipFile(path, 'w') as archive:
			archive.writestr('VeroScriptJetBrains/lib/VeroScriptJetBrains-0.1.1.jar', jar.getvalue())
		return path

	def test_nested_jar_rejects_native_payload_and_symbolic_links(self):
		linked = zipfile.ZipInfo('com/vanstysanic/veroscript/Linked.class')
		linked.create_system = 3
		linked.external_attr = (stat.S_IFLNK | 0o777) << 16
		for entry in ('native/backend.DLL', 'native/libbackend.so.2', linked):
			with self.subTest(entry=str(entry)):
				path = self.jetbrains_archive([(entry, 'unexpected')])
				with self.assertRaisesRegex(ValueError, 'build inputs|ZIP link'):
					verify_ide_artifact(path, 'JetBrains', '0.1.1')

	def test_compiled_nested_jar_is_accepted(self):
		verify_ide_artifact(self.jetbrains_archive(), 'JetBrains', '0.1.1')

	def test_empty_nested_jar_cannot_count_as_a_complete_plugin(self):
		path = self.jetbrains_archive()
		for files in ({}, {'META-INF/plugin.xml': '  '}):
			with self.subTest(files=files):
				jar = io.BytesIO()
				with zipfile.ZipFile(jar, 'w') as stream:
					for name, data in files.items():
						stream.writestr(name, data)
				with zipfile.ZipFile(path, 'w') as stream:
					stream.writestr('VeroScriptJetBrains/lib/VeroScriptJetBrains-0.1.1.jar', jar.getvalue())
				with self.assertRaisesRegex(ValueError, 'Missing/empty IDE payload'):
					verify_ide_artifact(path, 'JetBrains', '0.1.1')

	def test_missing_plugin_and_stale_build_identity_are_rejected(self):
		self.manifest()
		with self.assertRaisesRegex(ValueError, 'missing release products'):
			verify_release(self.candidate, businesses={'VSCode', 'JetBrains'})
		manifest_path = self.candidate / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text())
		manifest['businessBuildIds']['VSCode'] = '0' * 64
		manifest_path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'build identity mismatch'):
			verify_release(self.candidate, businesses={'VSCode'})

	def test_nested_jetbrains_descriptor_version_is_checked(self):
		path = self.candidate / 'ide/VeroScriptJetBrains-0.1.1.zip'
		jar = io.BytesIO()
		with zipfile.ZipFile(jar, 'w') as archive:
			archive.writestr('META-INF/plugin.xml', '<idea-plugin><id>com.vanstysanic.veroscript</id><version>9.9.9</version></idea-plugin>')
		with zipfile.ZipFile(path, 'w') as archive:
			archive.writestr('VeroScriptJetBrains/lib/VeroScriptJetBrains-0.1.1.jar', jar.getvalue())
		with self.assertRaisesRegex(ValueError, 'identity/version mismatch'):
			verify_ide_artifact(path, 'JetBrains', '0.1.1')

	def test_source_preflight_rejects_a_stale_lockfile_version(self):
		source = self.repo / 'Extensions/Editor/VSCode/VeroScriptVSCode'
		source.mkdir(parents=True)
		(source / 'package.json').write_text(json.dumps({'version': '0.1.1'}))
		lock = source / 'package-lock.json'
		lock.write_text(json.dumps({'version': '0.1.1', 'packages': {'': {'version': '0.1.0'}}}))
		with patch('veroscript_ide.module_root', return_value=self.repo):
			with self.assertRaisesRegex(ValueError, 'package-lock'):
				check_ide_sources(self.repo, self.versions, ['VSCode'])
			lock.write_text(json.dumps({'version': '0.1.1', 'packages': {'': {'version': '0.1.1'}}}))
			check_ide_sources(self.repo, self.versions, ['VSCode'])

	def test_source_audit_accepts_functional_vsix_and_checks_retained_resources(self):
		source = self.repo / 'Extensions/Editor/VSCode/VeroScriptVSCode'
		source.mkdir(parents=True)
		(self.repo / 'LICENSE').write_text('MIT')
		package = {'name': 'veroscript', 'publisher': 'VanstySanic',
				   'version': '0.1.1', 'main': './dist/extension.cjs'}
		(source / 'package.json').write_text(json.dumps(package))
		readme = source / 'README.md'
		readme.write_text('# VeroScript\n\nUse Select Runtime before running a script.')
		asset = source / 'Resources/Assets/icon.svg'
		asset.parent.mkdir(parents=True)
		asset.write_text('<svg/>')
		(asset.parent / 'CHANGELOG.zh-CN.md').write_text('Source asset history.')
		bundle = source / 'package.nls.zh-cn.json'
		bundle.write_text('{"setting": "translated"}')
		with zipfile.ZipFile(self.artifact) as archive:
			files = {item.filename: archive.read(item) for item in archive.infolist()}
		files.update({'extension/Resources/Assets/icon.svg': asset.read_bytes(),
					  'extension/package.nls.zh-cn.json': bundle.read_bytes(),
					  'extension/readme.md': readme.read_bytes()})

		def save():
			with zipfile.ZipFile(self.artifact, 'w') as archive:
				for name, data in files.items():
					archive.writestr(name, data)

		save()
		with patch('veroscript_ide.module_root', return_value=self.repo):
			verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', self.repo)
			readme.write_text('# Changed instructions')
			with self.assertRaisesRegex(ValueError, 'Stale VS Code resource'):
				verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', self.repo)
			readme.write_bytes(files['extension/readme.md'])
			original_readme = files.pop('extension/readme.md')
			save()
			with self.assertRaisesRegex(ValueError, 'Missing/empty IDE payload'):
				verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', self.repo)
			files['extension/readme.md'] = original_readme
			save()
			bundle.write_text('{"setting": "changed"}')
			with self.assertRaisesRegex(ValueError, 'Stale VS Code resource'):
				verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', self.repo)
			bundle.write_text('{"setting": "translated"}')
			del files['extension/Resources/Assets/icon.svg']
			save()
			with self.assertRaisesRegex(ValueError, 'Missing/empty IDE payload'):
				verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', self.repo)

	def test_component_publication_accepts_vsix_as_a_complete_independent_business(self):
		tools = self.repo / 'Resources/Tools/Packaging'
		tools.mkdir(parents=True)
		(tools / 'ReleaseVersions.json').write_text(json.dumps({
			'schemaVersion': 1, 'nativeVersion': '0.1.1', 'businesses': self.versions}))
		root = self.repo / '.tmp/collection'
		original = verify_release
		with patch('veroscript_business_release.module_root', return_value=self.repo), \
			 patch('veroscript_release.verify_release', side_effect=lambda path, repo=None, **kw: original(path, **kw)):
			publish_component(self.repo, self.artifact.parent, root, 'VSCode', '0.1.1', 'win-x64')
		manifest = json.loads((root / 'Extensions/Editor/VSCode/VSCode/any/0.1.1/release-manifest.json').read_text())
		self.assertEqual(manifest['buildId'], digest(self.artifact))
		self.assertNotIn('partial', manifest)
		verify_collection(root, 'win-x64')

	def test_source_audit_checks_generated_product_png_against_the_official_resource(self):
		source = self.repo / 'Extensions/Editor/VSCode/VeroScriptVSCode'
		source.mkdir(parents=True)
		(self.repo / 'LICENSE').write_text('MIT')
		official = self.repo / 'Resources/Assets/icons/veroscript-icon-256.png'
		official.parent.mkdir(parents=True)
		png = (Path(__file__).resolve().parents[4] / 'Resources/Assets/icons/veroscript-icon-256.png').read_bytes()
		official.write_bytes(png)
		from veroscript_products import IDE_PRODUCTS
		publisher = IDE_PRODUCTS['VSCode']['id'].rsplit('.', 1)[0]
		package = {'name': 'veroscript', 'publisher': publisher, 'version': '0.1.1',
			'main': './dist/extension.cjs', 'icon': './Resources/Assets/Icons/product.png'}
		(source / 'package.json').write_text(json.dumps(package))
		base = {'extension/package.json': json.dumps(package)}
		name = 'extension/Resources/Assets/Icons/product.png'
		# A clean checkout has no build-generated product.png to compare against.
		self.assertFalse((source / 'Resources/Assets/Icons/product.png').exists())
		with patch('veroscript_ide.module_root', return_value=self.repo), patch('veroscript_icons.module_root', return_value=self.repo):
			vsix(self.artifact, extra={**base, name: png})
			verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', self.repo)
			vsix(self.artifact, extra={**base, name: png.replace(b'\r\n', b'\n')})
			with self.assertRaisesRegex(ValueError, 'Stale VS Code product icon'):
				verify_ide_artifact(self.artifact, 'VSCode', '0.1.1', self.repo)
			vsix(self.artifact, extra=base)
			with self.assertRaisesRegex(ValueError, 'Missing/empty IDE payload'):
				verify_ide_artifact(self.artifact, 'VSCode', '0.1.1')


if __name__ == '__main__':
	unittest.main()
