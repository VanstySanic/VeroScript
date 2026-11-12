# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Documentation source, localization and publication contracts."""
from pathlib import Path
import json
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_docs import (anchors, archive_docs, build_docs, catalog, check_links, links,
							 check_tree, portable_bytes, portable_tree, rewrite_links, user_bundle, verify_bundle,
							 collection_bundle, verified_collection, check_collection, sync_standards, INTEGRATION_MARKER,
							 publication_bundle, publish_docs, verify_publication, verify_standards_reference,
							 select_user_source, user_projection, sync_standards_reference, check_documentation_dates,
							 documentation_entrypoints, check_localized_prose, validate_collection_metadata, module_entrypoints,
							 load_settings)
from veroscript_paths import module_root, source_route
from veroscript_release import copy_tree, portable_asset_bytes, sdk_asset_tree, release_asset_files
from veroscript_paths import module_root as source_module_root


class DocumentationTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory()
		self.addCleanup(self.temporary.cleanup)
		self.repo = Path(self.temporary.name).resolve()
		self.root = self.repo / 'Script/Core/Resources/Docs'
		self.locales = ['en-US', 'zh-CN']
		settings = {'schemaVersion': 1, 'locales': self.locales,
					'modules': ['Script/Core/Resources/Docs/manifest.json'],
					'resources': [{'source': 'Script/Resources/Assets', 'output': '_resources/assets'}]}
		self.write('Script/Resources/Docs/documentation.json', json.dumps(settings))
		self.write('Script/Core/Resources/Docs/manifest.json', json.dumps(
			{'schemaVersion': 1, 'module': 'Core', 'developer': True,
			 'user': [{'source': 'User/{locale}', 'output': '{locale}'}]}))
		for locale in self.locales:
			self.write(f'Script/Core/Resources/Docs/User/{locale}/index.md', '# Overview\n\n[page](page.md#api-value)\n')
			self.write(f'Script/Core/Resources/Docs/User/{locale}/page.md', '# API `Value`\n\n[home](index.md)\n')
			self.write(f'Script/Core/Resources/Docs/Developer/{locale}/index.md', '# Implementation\n')

	def write(self, relative, text):
		path = self.repo / relative
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(text.encode('utf-8'))
		return path

	def test_localized_prose_rejects_untranslated_paragraphs_with_different_wrapping(self):
		paragraph = ('The selected application environment keeps its resource providers alive '
					 'until the final callback has returned. Configure the environment before '
					 'starting the application and preserve the same owner throughout execution.')
		english = self.write('Docs/en-US/page.md', '# Contract\n\n' + paragraph + '\n')
		translated = self.write('Docs/zh-CN/page.md', '# 契约\n\n' + paragraph.replace('. ', '.\n') + '\n')
		errors = check_localized_prose([english, translated], self.locales)
		self.assertEqual(1, len(errors))
		self.assertIn('untranslated English prose', errors[0])
		translated.write_text('# 契约\n\n应用环境持有资源提供者，直到最后一个回调返回。启动前完成配置，并保持同一所有者。\n', encoding='utf-8')
		self.assertEqual([], check_localized_prose([english, translated], self.locales))

	def test_localized_prose_preserves_code_identifiers(self):
		code = ('# API\n\n```cpp\n// This original sample deliberately uses English comments and '
				'identifiers that are identical in every locale because they describe the same '
				'public programming contract and compiled program.\n```\n\n'
				'- `FModuleEnvironment::Create FNativeExtensionRegistry::Add '
				'FI18nServices::PublishResources FI18nContext::CaptureSnapshot`\n')
		english = self.write('Docs/en-US/page.md', code)
		translated = self.write('Docs/zh-CN/page.md', code)
		self.assertEqual([], check_localized_prose([english, translated], self.locales))

	def test_publication_rejects_copied_english_developer_prose_for_both_audiences(self):
		self.prepare_collection()
		paragraph = ('The module registration owns each callback until every consumer has released '
					 'its environment. The host must retain all providers and services across '
					 'execution, cancellation and asynchronous cleanup of the application.')
		for locale in self.locales:
			self.write(f'Script/Core/Resources/Docs/Developer/{locale}/index.md', '# Contract\n\n' + paragraph + '\n')
		for audience in ['All', 'User']:
			with self.subTest(audience=audience), self.assertRaisesRegex(ValueError, 'untranslated English prose'):
				publish_docs(self.repo, self.repo / '.tmp/published', audience)

	def test_localized_prose_preserves_stable_module_navigation_but_checks_prose(self):
		navigation = '\n'.join(f'- [VeroScriptFeature{index}](feature-{index}.md)' for index in range(20))
		pages = [self.write(f'Docs/{locale}/index.md', navigation) for locale in self.locales]
		self.assertEqual([], check_localized_prose(pages, self.locales))
		paragraph = ('The application must preserve its selected resource environment until all callbacks '
					 'have returned and every owned worker has finished. Configure the resource provider '
					 'before starting the application and keep its implementation loaded during execution.')
		for page in pages:
			page.write_text(navigation + '\n\n' + paragraph, encoding='utf-8')
		self.assertEqual(1, len(check_localized_prose(pages, self.locales)))

	def test_calendar_dates_rejected_but_versions_remain_valid(self):
		version = self.write('versions.md', '# Compatibility\nRider 2026.2.2; ABI 0x00000101; product 0.1.1; NuGet 2.1.25547.20250602.\n')
		self.assertEqual(check_documentation_dates([version]), [])
		dialect = self.write('dialects.md', '# JSON Schema\nDraft 2020-12; draft 2019-09; https://json-schema.org/draft/2020-12/schema.\n')
		self.assertEqual(check_documentation_dates([dialect]), [])
		for date in ('2026-10-11', '2026/10/11', 'review-20261011', '2026年10月11日',
					 '2026년 10월 11일', '2026년10월', 'October 11, 2026', '11 October 2026',
					 'October 11th, 2026', '11th Oct. 2026', '11 octobre 2026', '1er février 2026',
					 '11 oct. 2026', '11/10/2026', '11-10-2026', 'review_11_Oct_2026',
					 '2026-07', '2026/07', 'October 2026', 'octobre 2026', 'Draft 2020-12-01'):
			with self.subTest(date=date):
				page = self.write('dated.md', f'# Review\n{date}\n')
				self.assertEqual(len(check_documentation_dates([page])), 1)
		filename = self.write('review_2026-10-11.md', '# Review\n')
		self.assertEqual(len(check_documentation_dates([filename])), 1)

	def test_publication_enforces_date_free_documentation_policy(self):
		self.prepare_collection()
		config = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(config.read_text())
		settings['dateFreeDocumentation'] = True
		config.write_text(json.dumps(settings))
		self.write('Script/Core/Resources/Docs/Developer/en-US/index.md', '# Record\n2026-10-11\n')
		with self.assertRaisesRegex(ValueError, 'calendar date'):
			publish_docs(self.repo, self.repo / '.tmp/published', 'User')

	def test_entrypoint_globs_ignore_generated_source_snapshots(self):
		source = self.write('Script/Core/Resources/Examples/README.md', '# Source\n')
		for folder in ('.tmp', 'build', 'Release', 'dist', 'node_modules',
					   'staging/.TMP', 'staging/Build', 'staging/ReLeAsE', 'staging/DIST',
					   'staging/Node_Modules', 'staging/__PyCache__'):
			self.write(f'{folder}/snapshot/Core/Resources/Examples/README.md', '[stale](missing.md)\n')
		settings = {'entrypoints': ['**/Resources/Examples/**/README.md']}
		original_iterdir = Path.iterdir

		def source_only_iterdir(path):
			if any(part.casefold() in {'.tmp', 'build', 'release', 'dist', 'node_modules', '__pycache__'}
				   for part in path.relative_to(self.repo).parts):
				raise AssertionError(f'Generated directory was traversed: {path}')
			return original_iterdir(path)

		with patch.object(Path, 'iterdir', source_only_iterdir):
			self.assertEqual(documentation_entrypoints(self.repo, settings), [source])

	def test_entrypoint_globs_keep_nested_matches_and_deduplicate(self):
		direct = self.write('Script/Core/Resources/Examples/README.md', '# Examples\n')
		nested = self.write('Script/Core/Resources/Examples/Embed/README.md', '# Embed\n')
		settings = {'entrypoints': ['Script/*/Resources/Examples/**/README.md',
									'Script/Core/Resources/Examples/README.md',
									'missing/**/README.md', 'Release/**/README.md']}
		self.write('Release/Resources/Examples/README.md', '# Generated\n')
		self.assertEqual(documentation_entrypoints(self.repo, settings), [direct, nested])

	def prepare_collection(self):
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		settings.update(repositoryBaseUrl='https://example.test/repo', repositoryRef='main')
		path.write_text(json.dumps(settings))
		self.write('Script/Core/README.md', '[Documentation](Resources/Docs/README.md)\n')
		self.write('Script/Core/Resources/Docs/README.md', ''.join(
			f'[User](User/{locale}/index.md)\n[Developer](Developer/{locale}/index.md)\n'
			for locale in self.locales))
		self.write('Script/Resources/Docs/README.md', '[Overview](Overview/en-US/index.md)\n')
		self.write('Script/Core/code.py', '# implementation')
		self.write('Script/Resources/Assets/developer.svg', '<svg/>')
		for locale in self.locales:
			self.write(f'Script/Resources/Docs/Overview/{locale}/index.md',
					   f'# Integrated overview\n[User](../../../../Core/Resources/Docs/User/{locale}/index.md)\n'
					   f'[Developer](../../../../Core/Resources/Docs/Developer/{locale}/index.md)\n')
			self.write(f'Script/Core/Resources/Docs/Developer/{locale}/index.md',
					   f'# Implementation\n[user](../../User/{locale}/index.md#overview)\n'
					   '[code](../../../../code.py)\n'
					   '![internal](../../../../../Resources/Assets/developer.svg)\n'
					   '```md\n[example](../../../../code.py)\n```\n')
		return self.repo / '.tmp/collection'

	def publish_collection(self):
		destination = self.repo / '.tmp/collection'
		if destination.exists():
			shutil.rmtree(destination)
		return publish_docs(self.repo, destination)

	def test_absent_optional_component_and_damaged_present_component(self):
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		route = 'Script/Modules/Execution/Backend/Resources/Docs/manifest.json'
		settings['modules'].append(route)
		settings['optionalModules'] = [route]
		path.write_text(json.dumps(settings))
		self.assertEqual(len(catalog(self.repo)[3]), 1)
		(self.repo / 'Script/Modules/Execution/Backend').mkdir(parents=True)
		with self.assertRaises(FileNotFoundError):
			catalog(self.repo)

	def test_temporary_collection_cleanup_and_source_only_navigation(self):
		self.prepare_collection()
		originals = {p.relative_to(self.repo): p.read_bytes() for p in (self.repo / 'Script').rglob('*') if p.is_file()}
		for _ in range(2):
			check_collection(self.repo)
			self.assertFalse((self.repo / 'Script/Resources/Docs/_generated').exists())
			self.assertEqual([], list((self.repo / '.tmp').glob('docs-collection-*')))
			self.assertEqual([], check_links(list((self.repo / 'Script').rglob('*.md'))))
		self.assertEqual(originals, {p.relative_to(self.repo): p.read_bytes() for p in (self.repo / 'Script').rglob('*') if p.is_file()})

	def test_temporary_collection_failure_cleans_stage_and_preserves_sources(self):
		self.prepare_collection()
		import veroscript_docs as docs
		original = docs.check_tree
		def checking(root, **kwargs):
			if Path(root).name == 'Docs' and Path(root).parent.name.startswith('docs-collection-'):
				raise OSError('Injected collection validation failure')
			return original(root, **kwargs)
		before = (self.root / 'User/en-US/page.md').read_bytes()
		with patch.object(docs, 'check_tree', side_effect=checking):
			with self.assertRaisesRegex(OSError, 'Injected'):
				publish_docs(self.repo, self.repo / '.tmp/rejected')
		self.assertEqual(before, (self.root / 'User/en-US/page.md').read_bytes())
		self.assertFalse((self.repo / '.tmp/rejected').exists())
		self.assertEqual([], list((self.repo / '.tmp').glob('docs-collection-*')))

	def test_obsolete_persistent_collection_configuration_is_rejected(self):
		self.prepare_collection()
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		settings['integrationRoot'] = 'Script/Resources/Docs/_generated'
		path.write_text(json.dumps(settings))
		with self.assertRaisesRegex(ValueError, 'obsolete'):
			check_collection(self.repo)
		self.assertFalse((self.repo / '.tmp').exists())

	def test_temporary_collection_rejects_linked_scratch_root(self):
		self.prepare_collection()
		outside = self.repo / 'outside'
		outside.mkdir()
		try:
			(self.repo / '.tmp').symlink_to(outside, target_is_directory=True)
		except OSError:
			self.skipTest('Creating symlinks requires Windows Developer Mode or privilege')
		with self.assertRaisesRegex(ValueError, 'Linked'):
			check_collection(self.repo)
		self.assertEqual([], list(outside.iterdir()))

	def test_custom_all_export_rejects_edited_resources_and_unknown_markers(self):
		output = self.prepare_collection()
		self.publish_collection()
		copied = self.repo / '.tmp/copied'
		shutil.copytree(output, copied)
		asset = output / '_resources/assets/developer.svg'
		original = asset.read_bytes()
		asset.write_bytes(b'<svg>edited</svg>')
		with self.assertRaisesRegex(ValueError, 'edited'):
			portable_tree(self.repo, output, copied)
		asset.write_bytes(original)
		marker = output / INTEGRATION_MARKER
		metadata = json.loads(marker.read_bytes())
		metadata['product'] = 'OtherDocumentation'
		marker.write_text(json.dumps(metadata))
		with self.assertRaisesRegex(ValueError, 'Unrecognized'):
			portable_tree(self.repo, output, copied)

	def test_custom_all_export_checks_the_copied_tree_before_archiving(self):
		output = self.prepare_collection()
		self.publish_collection()
		copied = self.repo / '.tmp/copied'
		shutil.copytree(output, copied)
		(copied / '_resources/assets/developer.svg').write_bytes(b'<svg>changed while copying</svg>')
		with self.assertRaisesRegex(ValueError, 'copied|edited'):
			portable_tree(self.repo, output, copied)

	def test_overview_only_resources_are_included_with_transitive_dependencies(self):
		self.prepare_collection()
		self.write('Script/Resources/Assets/overview/guide.md', '# Illustration\n![image](picture.svg)\n')
		self.write('Script/Resources/Assets/overview/picture.svg', '<svg/>')
		for locale in self.locales:
			source = self.repo / f'Script/Resources/Docs/Overview/{locale}/index.md'
			source.write_bytes(source.read_bytes() + b'\n[Illustration](../../../Assets/overview/guide.md#illustration)\n')
		bundle = verified_collection(self.repo)
		self.assertIn('_resources/assets/overview/guide.md', bundle)
		self.assertIn('_resources/assets/overview/picture.svg', bundle)
		self.assertIn(b'(../../_resources/assets/overview/guide.md#illustration)', bundle['Overview/en-US/index.md'])
		self.assertNotIn('_resources/assets/overview/guide.md', publication_bundle(self.repo, 'User'))

	def test_portable_mapping_follows_manifest_changes_in_the_same_process(self):
		self.prepare_collection()
		source = self.write('Script/Resources/Assets/README.md', '[API](../../Core/Resources/Docs/User/en-US/page.md#api-value)\n')
		self.assertEqual(b'[API](../Docs/en-US/page.md#api-value)\n', portable_bytes(self.repo, source))
		manifest = self.root / 'manifest.json'
		definition = json.loads(manifest.read_bytes())
		definition['user'] = [
			{'source': 'User/{locale}/index.md', 'output': '{locale}/index.md'},
			{'source': 'User/{locale}/page.md', 'output': '{locale}/reference/page.md'},
		]
		manifest.write_text(json.dumps(definition), encoding='utf-8')
		self.assertEqual(b'[API](../Docs/en-US/reference/page.md#api-value)\n', portable_bytes(self.repo, source))

	def test_flat_checkout_asset_links_resolve_from_its_own_root(self):
		self.prepare_collection()
		self.write('Script/Resources/Assets/README.md', '[API](../../Core/Resources/Docs/User/en-US/page.md#api-value)\n')
		self.write('Script/Bindings/README.md', '[code](../Core/code.py)\n')
		flat = self.repo / '.tmp/flat checkout'
		shutil.copytree(self.repo / 'Script', flat)
		(flat / 'CMakeLists.txt').write_text('project(VeroScript)\n')
		self.assertEqual(b'[API](../Docs/en-US/page.md#api-value)\n',
						 portable_asset_bytes(flat / 'Resources/Assets/README.md'))
		self.assertEqual(b'[code](https://example.test/repo/blob/main/Core/code.py)\n',
						 portable_asset_bytes(flat / 'Bindings/README.md'))

	def test_staged_user_docs_keep_offline_links_without_source_rescans(self):
		self.prepare_collection()
		flat = self.repo / '.tmp/flat checkout'
		shutil.copytree(self.repo / 'Script', flat)
		(flat / 'CMakeLists.txt').write_text('project(VeroScript)\n')
		published = flat / '.tmp/published'
		publish_docs(flat, published, 'User')
		copied = flat / '.tmp/sdk/Docs'
		shutil.copytree(published, copied)
		expected = {path.relative_to(published): path.read_bytes()
					for path in check_tree(published)}
		with patch('veroscript_docs.portable_mapping', side_effect=AssertionError('Published docs must not rescan source mappings')):
			portable_tree(flat, published, copied)
			sdk_asset_tree(copied)
		self.assertEqual(expected, {path.relative_to(copied): path.read_bytes()
									for path in check_tree(copied)})

	def test_module_entrypoints_resolve_the_owning_guides(self):
		self.prepare_collection()
		settings, _, _, manifests = catalog(self.repo)
		self.assertEqual([self.repo / 'Script/Core/README.md', self.root / 'README.md'],
			module_entrypoints(self.repo, settings, manifests))

	def test_publication_requires_a_module_landing_page_for_both_audiences(self):
		self.prepare_collection()
		(self.repo / 'Script/Core/README.md').unlink()
		for audience in ('All', 'User'):
			with self.subTest(audience=audience), self.assertRaisesRegex(ValueError, 'module README'):
				publish_docs(self.repo, self.repo / '.tmp/missing-entry', audience)
		self.assertFalse((self.repo / '.tmp/missing-entry').exists())

	def test_module_selector_requires_every_language_and_audience(self):
		self.prepare_collection()
		hub = self.root / 'README.md'
		complete = hub.read_text()
		for role in ('User', 'Developer'):
			for locale in self.locales:
				text = complete.replace(f'[{role}]({role}/{locale}/index.md)\n', '')
				hub.write_text(text, encoding='utf-8')
				for audience in ('All', 'User'):
					with self.subTest(role=role, locale=locale, audience=audience), self.assertRaisesRegex(ValueError, f'Missing {role}/{locale}'):
						publication_bundle(self.repo, audience)
		hub.write_text(complete, encoding='utf-8')

	def test_module_landing_page_cannot_select_another_modules_documentation(self):
		self.prepare_collection()
		other = self.write('Script/Other/Resources/Docs/README.md', '# Another collection\n')
		(self.repo / 'Script/Core/README.md').write_text('[Documentation](../Other/Resources/Docs/README.md)\n', encoding='utf-8')
		self.assertTrue(other.is_file())
		with self.assertRaisesRegex(ValueError, 'Missing documentation index link'):
			publication_bundle(self.repo)

	def test_module_landing_accepts_only_its_canonical_online_documentation_index(self):
		self.prepare_collection()
		landing = self.repo / 'Script/Core/README.md'
		hub = 'https://example.test/repo/blob/main/Script/Core/Resources/Docs/README.md'
		landing.write_text(f'[Documentation]({hub})\n', encoding='utf-8')
		publication_bundle(self.repo)
		for wrong in (hub.replace('/Core/', '/Other/'), hub.replace('/main/', '/old/')):
			with self.subTest(url=wrong):
				landing.write_text(f'[Documentation]({wrong})\n', encoding='utf-8')
				with self.assertRaisesRegex(ValueError, 'Missing documentation index link'):
					publication_bundle(self.repo)

	def test_landing_change_during_publication_is_not_accepted(self):
		self.prepare_collection()
		original = collection_bundle
		def change_landing(repo):
			bundle = original(repo)
			landing = self.repo / 'Script/Core/README.md'
			landing.write_text(landing.read_text() + '\nChanged source overview.\n', encoding='utf-8')
			return bundle
		with patch('veroscript_docs.collection_bundle', side_effect=change_landing):
			with self.assertRaisesRegex(ValueError, 'Authored documentation changed'):
				publication_bundle(self.repo)

	def test_history_module_trees_are_rejected_for_both_audiences(self):
		self.prepare_collection()
		for folder in ('History', 'history', 'hIsToRy'):
			history = self.write(f'Script/Core/Resources/Docs/Developer/en-US/{folder}/record.md', '# Old record\n')
			try:
				for audience in ('All', 'User'):
					with self.subTest(folder=folder, audience=audience), self.assertRaisesRegex(ValueError, 'History documentation'):
						publish_docs(self.repo, self.repo / '.tmp/rejected', audience)
					self.assertFalse((self.repo / '.tmp/rejected').exists())
			finally:
				history.unlink()
				history.parent.rmdir()

	def test_shared_standards_and_tutorial_history_are_not_publication_inputs(self):
		self.prepare_standards_tutorials()
		for section in ('Standards', 'Tutorials'):
			history = self.write(f'Script/Resources/Docs/{section}/History/record.md', '# Old record\n')
			try:
				for audience in ('All', 'User'):
					with self.subTest(section=section, audience=audience), self.assertRaisesRegex(ValueError, 'History documentation'):
						publication_bundle(self.repo, audience)
			finally:
				history.unlink()
				history.parent.rmdir()

	def test_history_cannot_enter_through_a_registered_resource(self):
		self.prepare_collection()
		self.write('Script/Resources/Assets/History/record.md', '# Old resource record\n')
		page = self.root / 'User/en-US/index.md'
		page.write_text(page.read_text() + '\n[asset](../../../../../Resources/Assets/History/record.md)\n', encoding='utf-8')
		for audience in ('All', 'User'):
			with self.subTest(audience=audience), self.assertRaisesRegex(ValueError, 'History documentation'):
				publication_bundle(self.repo, audience)

	def test_custom_history_trees_are_rejected_before_archive_creation(self):
		history = self.write('custom/History/current.md', '# Apparently current title\n')
		archive = self.repo / 'rejected.zip'
		for source in (history.parent, history.parent.parent):
			with self.subTest(source=source), self.assertRaisesRegex(ValueError, 'History documentation'):
				check_tree(source, validate_links=False)
			with self.subTest(archive=source), self.assertRaisesRegex(ValueError, 'History documentation'):
				archive_docs(source, archive)
			self.assertFalse(archive.exists())

	def test_collection_hashes_cannot_authorize_history_payloads(self):
		self.prepare_collection()
		bundle = collection_bundle(self.repo)
		name = 'Developer/en-US/Core/History/record.md'
		bundle[name] = b'# Old record\n'
		marker = json.loads(bundle[INTEGRATION_MARKER])
		marker['files'][name] = hashlib.sha256(bundle[name]).hexdigest()
		bundle[INTEGRATION_MARKER] = json.dumps(marker).encode()
		with self.assertRaisesRegex(ValueError, 'History documentation'):
			validate_collection_metadata(bundle)

	def test_history_feature_pages_remain_valid_current_documentation(self):
		self.prepare_collection()
		for locale in self.locales:
			self.write(f'Script/Core/Resources/Docs/User/{locale}/http-history.md', '# Request history\n\n`FRequest::History`\n')
		bundle = publication_bundle(self.repo, 'All')
		self.assertIn('User/en-US/http-history.md', bundle)
		self.assertIn(b'FRequest::History', bundle['User/en-US/http-history.md'])

	def test_integrated_user_developer_hubs_and_portable_source_links(self):
		output = self.prepare_collection()
		original = (self.root / 'Developer/en-US/index.md').read_bytes()
		count = self.publish_collection()
		self.assertEqual(count, check_collection(self.repo))
		repository_link = b'(https://example.test/repo)'
		for relative in ('README.md', 'User/README.md', 'Developer/README.md'):
			self.assertIn(repository_link, (output / relative).read_bytes())
		for locale in self.locales:
			self.assertIn(repository_link, (output / f'Developer/{locale}/index.md').read_bytes())
		projected = user_projection({p.relative_to(output).as_posix(): p.read_bytes()
									for p in output.rglob('*') if p.is_file()})
		self.assertIn(repository_link, projected['README.md'])
		self.assertIn(b'(en-US/index.md)', projected['README.md'])
		for name, data in user_bundle(self.repo).items():
			self.assertEqual(data, (output / 'User' / name).read_bytes())
		verify_bundle(self.repo, output / 'User')
		self.assertTrue((output / '_resources/assets/developer.svg').is_file())
		self.assertFalse((output / 'User/_resources/assets/developer.svg').exists())
		developer = (output / 'Developer/en-US/Core/index.md').read_text(encoding='utf-8')
		self.assertIn('(../../../User/en-US/index.md#overview)', developer)
		self.assertIn('(https://example.test/repo/blob/main/Script/Core/code.py)', developer)
		self.assertIn('[example](../../../../code.py)', developer)
		self.assertEqual(original, (self.root / 'Developer/en-US/index.md').read_bytes())
		self.assertFalse(any('history' == part.casefold() for p in output.rglob('*') for part in p.relative_to(output).parts))
		self.assertIn('(Core/index.md)', (output / 'Developer/en-US/index.md').read_text(encoding='utf-8'))
		self.assertEqual([], check_links(list(output.rglob('*.md')), boundary=output))
		archive = self.repo / 'integrated.zip'
		archive_docs(output, archive)
		with zipfile.ZipFile(archive) as package:
			package.extractall(self.repo / 'export')
		check_tree(self.repo / 'export/collection')

	def prepare_standards_tutorials(self):
		output = self.prepare_collection()
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		settings.update(standardsRoot='Script/Resources/Docs/Standards',
						standardsAudience='Developer',
						tutorialsRoot='Script/Resources/Docs/Tutorials')
		settings['resources'].append({'source': settings['tutorialsRoot'], 'output': 'Tutorials'})
		path.write_text(json.dumps(settings))
		self.write(settings['standardsRoot'] + '/README.md', '[Home](../README.md)\n[English](en-US/README.md)\n')
		self.write(settings['tutorialsRoot'] + '/README.md', '[English](en-US/index.md)\n')
		self.write('IDE/Resources/docs/documentation.json', json.dumps({'schemaVersion': 1, 'product': 'ValenceStudio'}))
		self.write('IDE/Resources/docs/README.md', '# IDE docs\n[Rules](standards/README.md)\n')
		for locale in self.locales:
			self.write(settings['standardsRoot'] + f'/{locale}/README.md', '[General](GENERAL.md)\n')
			self.write(settings['standardsRoot'] + f'/{locale}/GENERAL.md', f'# Rules {locale}\n')
			self.write(settings['tutorialsRoot'] + f'/{locale}/index.md', '[Lesson](lesson.md)\n')
			self.write(settings['tutorialsRoot'] + f'/{locale}/lesson.md',
					   f'# Lesson {locale}\n[API](../../../../Core/Resources/Docs/User/{locale}/page.md?x=1&amp;y=2#api-value)\n')
			# Deliberately unlinked: complete extraction must still include it.
			self.write(settings['tutorialsRoot'] + f'/{locale}/extra.md', '# Extra lesson\n')
		return output

	def test_standards_audience_cannot_be_changed_to_user_or_all(self):
		self.prepare_standards_tutorials()
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		for audience in ('User', 'All'):
			with self.subTest(audience=audience):
				settings['standardsAudience'] = audience
				path.write_text(json.dumps(settings))
				with self.assertRaisesRegex(ValueError, 'standardsAudience must be Developer'):
					load_settings(self.repo)

	def test_user_and_tutorial_links_cannot_include_standards_as_resources(self):
		self.prepare_standards_tutorials()
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		settings['resources'].append({'source': settings['standardsRoot'], 'output': '_resources/rules'})
		path.write_text(json.dumps(settings))
		standards = self.repo / settings['standardsRoot'] / 'en-US/GENERAL.md'
		for source in (self.root / 'User/en-US/index.md',
					   self.repo / settings['tutorialsRoot'] / 'en-US/lesson.md'):
			with self.subTest(source=source):
				original = source.read_bytes()
				url = os.path.relpath(standards, source.parent).replace(os.sep, '/')
				source.write_text(f'# Guide\n\n[Project standards]({url})\n', encoding='utf-8')
				with self.assertRaisesRegex(ValueError, 'User link targets project development and contribution standards'):
					user_bundle(self.repo)
				source.write_bytes(original)

	def test_standards_reference_follows_renamed_valence_consumer(self):
		self.prepare_standards_tutorials()
		renamed = self.repo / 'Editor Workspace'
		(self.repo / 'IDE').rename(renamed)
		sync_standards(self.repo)
		verify_standards_reference(self.repo)
		self.assertFalse((self.repo / 'IDE').exists())
		self.assertTrue((renamed / 'Resources/docs/standards/.standards.json').is_file())

	def test_module_discovery_excludes_valence_documentation_catalog(self):
		# Also exercise the collision on case-sensitive hosts with Docs spelling.
		self.write('Another Editor/Resources/Docs/documentation.json',
				   json.dumps({'schemaVersion': 2, 'product': 'ValenceStudio'}))
		self.assertEqual(self.repo / 'Script', module_root(self.repo))

	def test_documentation_cli_accepts_relative_repository_path(self):
		self.prepare_standards_tutorials()
		sync_standards(self.repo)
		script = Path(__file__).resolve().parents[1] / 'veroscript_docs.py'
		for command in ('check', 'check-collection'):
			result = subprocess.run(
				[sys.executable, str(script), command, '--repo', self.repo.name],
				cwd=self.repo.parent, capture_output=True, text=True)
			self.assertEqual(0, result.returncode, result.stdout + result.stderr)

	def test_external_valence_navigation_follows_the_catalog_identity(self):
		self.prepare_standards_tutorials()
		catalog = self.repo / 'Script/Resources/Docs/documentation.json'
		config = json.loads(catalog.read_text())
		config['entrypoints'] = ['$valence/README.md', '$module/README.md']
		catalog.write_text(json.dumps(config))
		renamed = self.repo / 'Editor Space 中文'
		(self.repo / 'IDE').rename(renamed)
		from veroscript_docs import load_settings
		self.assertEqual(['Editor Space 中文/README.md', 'Script/README.md'],
						 load_settings(self.repo)['entrypoints'])

	def test_external_valence_navigation_is_optional_for_standalone_script(self):
		catalog = self.repo / 'Script/Resources/Docs/documentation.json'
		config = json.loads(catalog.read_text())
		config['entrypoints'] = ['$valence/README.md', '$module/README.md']
		catalog.write_text(json.dumps(config))
		from veroscript_docs import load_settings
		self.assertEqual(['Script/README.md'], load_settings(self.repo)['entrypoints'])

	def test_standards_reference_rejects_ambiguous_consumers(self):
		self.prepare_standards_tutorials()
		self.write('AnotherEditor/Resources/docs/documentation.json', json.dumps({'product': 'ValenceStudio'}))
		with self.assertRaisesRegex(ValueError, 'Ambiguous Valence'):
			sync_standards_reference(self.repo, {})
		self.assertFalse((self.repo / 'AnotherEditor/Resources/docs/standards').exists())

	def test_complete_tutorials_and_authored_standards_publish_without_duplicate_lessons(self):
		output = self.prepare_standards_tutorials()
		sync_standards(self.repo)
		verify_standards_reference(self.repo)
		all_files = publication_bundle(self.repo, 'All')
		user = publication_bundle(self.repo, 'User')
		self.assertIn('Standards/en-US/GENERAL.md', all_files)
		self.assertNotIn('Standards/History/record.md', all_files)
		self.assertIn('Tutorials/zh-CN/extra.md', all_files)
		self.assertIn('Tutorials/zh-CN/extra.md', user)
		self.assertFalse(any(name.startswith(('Standards/', 'Developer/', 'User/Tutorials/', '_resources/tutorials/')) for name in user))
		self.assertFalse(any(name.startswith('User/Tutorials/') for name in all_files))
		self.assertEqual(user_bundle(self.repo), user)
		self.assertIn(b'(../../User/en-US/page.md?x=1&y=2#api-value)', all_files['Tutorials/en-US/lesson.md'])
		self.assertIn(b'(../../en-US/page.md?x=1&y=2#api-value)', user['Tutorials/en-US/lesson.md'])
		for audience in ('All', 'User'):
			destination = self.repo / '.tmp' / audience
			publish_docs(self.repo, destination, audience)
			check_tree(destination)
			verify_publication(self.repo, destination, audience)
		mirror = self.repo / 'IDE/Resources/docs/standards'
		self.assertIn(b'(../README.md)', (mirror / 'README.md').read_bytes())
		self.assertEqual([], check_links(list(mirror.rglob('*.md'))))
		standards = self.repo / 'Script/Resources/Docs/Standards/en-US/GENERAL.md'
		edited = standards.read_bytes() + b'\nAdditional maintained rule.\n'
		standards.write_bytes(edited)
		self.assertEqual(edited, publication_bundle(self.repo, 'All')['Standards/en-US/GENERAL.md'])
		with self.assertRaisesRegex(ValueError, 'stale'):
			verify_standards_reference(self.repo)
		sync_standards(self.repo)
		self.assertEqual(edited, standards.read_bytes())
		self.assertEqual(edited, (mirror / 'en-US/GENERAL.md').read_bytes())
		verify_standards_reference(self.repo)
		(mirror / 'en-US/GENERAL.md').write_bytes(b'# local mirror edit')
		with self.assertRaisesRegex(ValueError, 'stale'):
			verify_standards_reference(self.repo)
		with self.assertRaisesRegex(ValueError, 'local edits'):
			sync_standards(self.repo)

	def test_new_localized_sections_reject_missing_locale_pages(self):
		output = self.prepare_standards_tutorials()
		extra = self.repo / 'Script/Resources/Docs/Tutorials/zh-CN/extra.md'
		extra.unlink()
		with self.assertRaisesRegex(ValueError, 'Tutorials locale page parity'):
			collection_bundle(self.repo)
		extra.write_text('# Extra lesson')
		(self.repo / 'Script/Resources/Docs/Standards/zh-CN/GENERAL.md').unlink()
		with self.assertRaisesRegex(ValueError, 'Standards locale page parity'):
			collection_bundle(self.repo)

	def test_authored_standards_modified_during_assembly_are_preserved(self):
		output = self.prepare_standards_tutorials()
		import veroscript_docs as docs
		original = docs.collection_bundle
		standards = self.repo / 'Script/Resources/Docs/Standards/en-US/GENERAL.md'
		edited = b'# Concurrent saved rule\n'
		def changing(repo):
			bundle = original(repo)
			standards.write_bytes(edited)
			return bundle
		with patch.object(docs, 'collection_bundle', side_effect=changing):
			with self.assertRaisesRegex(ValueError, 'changed during'):
				self.publish_collection()
		self.assertEqual(edited, standards.read_bytes())

	def test_module_sources_modified_during_assembly_are_preserved(self):
		self.prepare_collection()
		import veroscript_docs as docs
		original = docs.collection_bundle
		source = self.root / 'User/en-US/page.md'
		edited = source.read_bytes() + b'\nConcurrent source save\n'
		def changing(repo):
			bundle = original(repo)
			source.write_bytes(edited)
			return bundle
		with patch.object(docs, 'collection_bundle', side_effect=changing):
			with self.assertRaisesRegex(ValueError, 'changed during'):
				publish_docs(self.repo, self.repo / '.tmp/rejected')
		self.assertEqual(edited, source.read_bytes())
		self.assertFalse((self.repo / '.tmp/rejected').exists())
		self.assertEqual([], list((self.repo / '.tmp').glob('docs-collection-*')))

	def test_standards_reference_uses_canonical_links_instead_of_rendered_collection(self):
		output = self.prepare_standards_tutorials()
		portal = self.repo / 'Script/Resources/Docs/Standards/README.md'
		portal.write_bytes(portal.read_bytes() + b'\n[Query](en-US/README.md?x=1&amp;y=2)\n')
		sync_standards(self.repo)
		verify_standards_reference(self.repo)
		self.assertIn(b'?x=1&amp;y=2', (self.repo / 'IDE/Resources/docs/standards/README.md').read_bytes())
		self.assertIn(b'?x=1&y=2', publication_bundle(self.repo)['Standards/README.md'])



	def test_standards_reference_preserves_edits_at_rename_and_windows_hold(self):
		output = self.prepare_standards_tutorials()
		sync_standards(self.repo)
		mirror = self.repo / 'IDE/Resources/docs/standards'
		files = {p.relative_to(self.repo / 'Script/Resources/Docs/Standards').as_posix(): p.read_bytes()
				 for p in check_tree(self.repo / 'Script/Resources/Docs/Standards', validate_links=False)}
		files['en-US/GENERAL.md'] += b'\nUpdated rule\n'
		replace = os.replace
		for held in (False, True):
			with self.subTest(held=held):
				page = mirror / 'en-US/GENERAL.md'
				original = page.read_bytes()
				edited = original + b'\nConcurrent reference edit\n'
				def moving(start, end):
					if Path(start) == mirror:
						page.write_bytes(edited)
						if held:
							error = PermissionError('Injected Windows root handle')
							error.winerror = 32
							raise error
					return replace(start, end)
				with patch('veroscript_docs.os.replace', side_effect=moving), patch('veroscript_docs.time.sleep'):
					with self.assertRaisesRegex(ValueError, 'changed during'):
						sync_standards_reference(self.repo, files)
				self.assertEqual(edited, page.read_bytes())
				page.write_bytes(original)

	def test_windows_held_standards_reference_uses_marker_last_and_retains_failed_rollback(self):
		output = self.prepare_standards_tutorials()
		sync_standards(self.repo)
		mirror = self.repo / 'IDE/Resources/docs/standards'
		files = {p.relative_to(self.repo / 'Script/Resources/Docs/Standards').as_posix(): p.read_bytes()
				 for p in check_tree(self.repo / 'Script/Resources/Docs/Standards', validate_links=False)}
		files['en-US/GENERAL.md'] += b'\nUpdated rule\n'
		before = {p.relative_to(mirror).as_posix(): p.read_bytes()
				  for p in check_tree(mirror, validate_links=False)}
		replace = os.replace
		writes = []
		def held(start, end):
			if Path(start) == mirror:
				error = PermissionError('Injected Windows root handle')
				error.winerror = 5
				raise error
			if Path(end).is_relative_to(mirror):
				writes.append(Path(end).relative_to(mirror).as_posix())
			return replace(start, end)
		with patch('veroscript_docs.os.replace', side_effect=held), patch('veroscript_docs.time.sleep'):
			sync_standards_reference(self.repo, files)
		self.assertEqual('.standards.json', writes[-1])
		self.assertEqual(files['en-US/GENERAL.md'], (mirror / 'en-US/GENERAL.md').read_bytes())
		# Return to the original owned tree before each injected update failure.
		for permanent in (False, True):
			for name, data in before.items():
				(mirror / name).write_bytes(data)
			failures = []
			def failing(start, end):
				if (Path(end) == mirror / '.standards.json' and not failures
						or permanent and failures and Path(end) == mirror / 'en-US/GENERAL.md'):
					failures.append(True)
					raise OSError('Injected reference file failure')
				return held(start, end)
			with self.subTest(permanent=permanent), \
					patch('veroscript_docs.os.replace', side_effect=failing), patch('veroscript_docs.time.sleep'):
				with self.assertRaisesRegex(RuntimeError if permanent else OSError,
											'recover reference' if permanent else 'Injected reference'):
					sync_standards_reference(self.repo, files)
			if permanent:
				backups = list((self.repo / '.tmp').glob('standards-reference-*/previous'))
				self.assertEqual(1, len(backups))
				self.assertEqual(before, {p.relative_to(backups[0]).as_posix(): p.read_bytes()
										  for p in check_tree(backups[0], validate_links=False)})
			else:
				self.assertEqual(before, {p.relative_to(mirror).as_posix(): p.read_bytes()
										  for p in check_tree(mirror, validate_links=False)})
				self.assertEqual([], list((self.repo / '.tmp').glob('standards-reference-*')))

	def test_user_projection_rejects_collisions_conflicts_and_encoded_root_links(self):
		for bundle, error in (({'User/Tutorials/index.md': b'', 'Tutorials/index.md': b''}, 'Duplicate'),
							  ({'User/Tutorials': b'', 'Tutorials/index.md': b''}, 'conflict'),
							  ({'User/index.md': b'[Root](&#47;outside.md)\n'}, 'Root-relative')):
			with self.subTest(error=error):
				with self.assertRaisesRegex(ValueError, error):
					user_projection(bundle)

	def test_installers_select_user_from_readonly_ancestor_alias(self):
		output = self.prepare_standards_tutorials()
		self.publish_collection()
		package = self.repo / '.tmp/real/docs'
		publish_docs(self.repo, package, 'All')
		alias = self.repo / '.tmp/alias'
		try:
			alias.symlink_to(package.parent, target_is_directory=True)
		except OSError:
			self.skipTest('Creating symlinks requires Windows Developer Mode or privilege')
		selected = select_user_source(self.repo, alias / 'docs')
		self.assertEqual(publication_bundle(self.repo, 'User'),
						 {p.relative_to(selected).as_posix(): p.read_bytes() for p in check_tree(selected)})

	def test_published_standards_keep_locale_structure_and_shared_code_examples(self):
		repo = Path(__file__).resolve().parents[4]
		root = source_module_root(repo) / 'Resources/Docs/Standards'
		settings = json.loads((source_module_root(repo) / 'Resources/Docs/documentation.json').read_text())
		source = root / 'zh-CN'
		expected = {p.name for p in source.glob('*.md')}
		code = re.findall(r'```[^\n]*\n.*?```', (source / 'CPP.md').read_text(encoding='utf-8'), re.S)
		self.assertEqual(8, len(code))
		for locale in settings['locales']:
			self.assertEqual(expected, {p.name for p in (root / locale).glob('*.md')})
			self.assertEqual(code, re.findall(r'```[^\n]*\n.*?```', (root / locale / 'CPP.md').read_text(encoding='utf-8'), re.S))

	def test_editable_overview_and_module_changes_are_published_immediately(self):
		self.prepare_collection()
		overview = self.repo / 'Script/Resources/Docs/Overview/en-US/index.md'
		edited = overview.read_bytes() + b'\n## Reading path\n[API](../../../../Core/Resources/Docs/User/en-US/page.md#api-value)\n'
		overview.write_bytes(edited)
		for audience in ('All', 'User'):
			destination = self.repo / '.tmp' / audience
			expected = publication_bundle(self.repo, audience)
			self.assertEqual(len(expected), publish_docs(self.repo, destination, audience))
			verify_publication(self.repo, destination, audience)
			self.assertEqual(expected, {p.relative_to(destination).as_posix(): p.read_bytes() for p in check_tree(destination)})
		self.assertEqual(edited, overview.read_bytes())
		self.assertIn('Overview/en-US/index.md', publication_bundle(self.repo))
		source = self.root / 'User/en-US/page.md'
		source.write_bytes(source.read_bytes() + b'\nSource changed\n')
		self.assertIn(b'Source changed', publication_bundle(self.repo, 'User')['en-US/page.md'])
		with self.assertRaisesRegex(ValueError, 'stale'):
			verify_publication(self.repo, self.repo / '.tmp/User', 'User')
		self.assertFalse((self.repo / 'Script/Resources/Docs/_generated').exists())

	def test_overview_pages_can_be_added_and_invalid_edits_remain_recoverable(self):
		self.prepare_collection()
		for locale in self.locales:
			self.write(f'Script/Resources/Docs/Overview/{locale}/tasks.md', '# Tasks\n[Home](index.md)\n')
		self.assertIn('Overview/en-US/tasks.md', verified_collection(self.repo))
		overview = self.repo / 'Script/Resources/Docs/Overview/en-US/index.md'
		overview.write_bytes(overview.read_bytes() + b'\n[Missing](../../../../Core/Resources/Docs/User/en-US/missing.md)\n')
		original = overview.read_bytes()
		with self.assertRaisesRegex(ValueError, 'missing'):
			publish_docs(self.repo, self.repo / '.tmp/rejected')
		self.assertEqual(original, overview.read_bytes())
		self.assertFalse((self.repo / '.tmp/rejected').exists())

	def test_overview_inventory_requires_locale_parity(self):
		output = self.prepare_collection()
		self.write('Script/Resources/Docs/Overview/en-US/tasks.md', '# Tasks\n')
		with self.assertRaisesRegex(ValueError, 'parity'):
			self.publish_collection()
		(self.repo / 'Script/Resources/Docs/Overview/en-US/tasks.md').unlink()
		(self.repo / 'Script/Resources/Docs/Overview/en-US/index.md').unlink()
		with self.assertRaisesRegex(ValueError, 'locale index'):
			self.publish_collection()

	@unittest.skipUnless(sys.platform == 'win32', 'PowerShell installer documentation selection')
	def test_installers_select_complete_offline_user_view_from_all_and_standalone(self):
		output = self.prepare_standards_tutorials()
		self.publish_collection()
		package = self.repo / '.tmp/package'
		publish_docs(self.repo, package / 'docs', 'All')
		standalone = self.repo / '.tmp/standalone'
		publish_docs(self.repo, standalone, 'User')
		helper = Path(__file__).resolve().parents[1] / 'veroscript-assets.ps1'
		quote = lambda path: "'" + str(path).replace("'", "''") + "'"
		command = (f"$ErrorActionPreference='Stop'; . {quote(helper)}; "
				   f"@((Resolve-VeroScriptUserDocsRoot {quote(package)}), "
				   f"(Resolve-VeroScriptUserDocsRoot {quote(output)}), "
				   f"(Resolve-VeroScriptUserDocsRoot {quote(standalone)})) | ConvertTo-Json -Compress")
		result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command],
								check=True, capture_output=True, text=True)
		selected = [Path(path) for path in json.loads(result.stdout)]
		self.assertEqual(standalone, selected[-1])
		expected = publication_bundle(self.repo, 'User')
		for source in selected:
			self.assertEqual(expected, {p.relative_to(source).as_posix(): p.read_bytes() for p in check_tree(source)})

	def test_integrated_custom_package_preserves_already_portable_links(self):
		output = self.prepare_collection()
		self.publish_collection()
		expected = collection_bundle(self.repo)
		copied = self.repo / '.tmp/custom-docs'
		shutil.copytree(output, copied)
		portable_tree(self.repo, output, copied)
		actual = {p.relative_to(copied).as_posix(): p.read_bytes() for p in check_tree(copied)}
		self.assertEqual(expected, actual)
		if sys.platform == 'win32':
			packager = Path(__file__).resolve().parents[1] / 'package-veroscript-docs.ps1'
			packaged = self.repo / '.tmp/package-output'
			subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(packager),
							'-Version', '1.0.0', '-SourceRoot', str(output), '-OutRoot', str(packaged)],
						   check=True, capture_output=True, text=True)
			with zipfile.ZipFile(packaged / 'VeroScriptDocs-1.0.0.zip') as archive:
				actual = {name.removeprefix('VeroScriptDocs-1.0.0/docs/'): archive.read(name)
						  for name in archive.namelist()}
			self.assertEqual(expected, actual)

	def test_module_relative_catalog_preserves_product_identity_and_portable_copies(self):
		output = self.prepare_standards_tutorials()
		config = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(config.read_text())
		settings['modules'] = ['$module/' + route.removeprefix('Script/') for route in settings['modules']]
		for item in settings['resources']:
			item['source'] = '$module/' + item['source'].removeprefix('Script/')
		for key in ('standardsRoot', 'tutorialsRoot'):
			settings[key] = '$module/' + settings[key].removeprefix('Script/')
		config.write_text(json.dumps(settings))
		renamed = self.repo / 'Language Sources'
		(self.repo / 'Script').rename(renamed)
		self.assertEqual(renamed, module_root(self.repo))
		self.assertEqual('Language Sources/VeroScript/Resources/Docs',
						 source_route(self.repo, '$module/VeroScript/Resources/Docs'))
		sync_standards(self.repo)
		self.publish_collection()
		check_collection(self.repo)
		verify_standards_reference(self.repo)
		output = self.repo / '.tmp/collection'
		portal = (output / 'README.md').read_text(encoding='utf-8')
		self.assertIn('`Language Sources/Resources/Docs`', portal)
		self.assertIn('python "Language Sources/Resources/Tools/Packaging/veroscript_docs.py" check', portal)
		self.assertIn('& "./Language Sources/Resources/Tools/Packaging/package-veroscript-docs.ps1"', portal)
		for page in output.rglob('*.md'):
			self.assertEqual(page.read_bytes(), portable_bytes(self.repo, page), page)
		for audience in ('All', 'User'):
			destination = self.repo / '.tmp' / audience
			publish_docs(self.repo, destination, audience)
			verify_publication(self.repo, destination, audience)

	def test_integrated_collection_rejects_missing_empty_or_directory_hubs(self):
		output = self.prepare_collection()
		original = {p.relative_to(output): p.read_bytes() for p in output.rglob('*') if p.is_file()}
		hub = self.root / 'README.md'
		hub.unlink()
		with self.assertRaisesRegex(ValueError, 'entrypoint'):
			self.publish_collection()
		hub.write_text(' \n', encoding='utf-8')
		with self.assertRaisesRegex(ValueError, 'entrypoint'):
			self.publish_collection()
		hub.unlink()
		hub.mkdir()
		with self.assertRaisesRegex(ValueError, 'entrypoint'):
			self.publish_collection()
		self.assertEqual(original, {p.relative_to(output): p.read_bytes() for p in output.rglob('*') if p.is_file()})

	def test_integrated_collection_rejects_linked_module_hubs(self):
		output = self.prepare_collection()
		original = {p.relative_to(output): p.read_bytes() for p in output.rglob('*') if p.is_file()}
		hub = self.root / 'README.md'
		hub.unlink()
		external = self.write('external.md', '# Outside module\n')
		try:
			hub.symlink_to(external)
		except OSError:
			self.skipTest('Creating symlinks requires Windows Developer Mode or privilege')
		with self.assertRaisesRegex(ValueError, 'Linked'):
			self.publish_collection()
		self.assertEqual(original, {p.relative_to(output): p.read_bytes() for p in output.rglob('*') if p.is_file()})

	def test_integrated_generation_is_identical_for_lf_and_crlf_sources(self):
		output = self.prepare_collection()
		for locale in self.locales:
			self.write(f'Script/Core/Resources/Docs/User/{locale}/index.md',
					   '# Overview\n[page](page.md#api-value)\n'
					   '[sample](../../../../../Resources/Assets/sample.vs)\n')
		self.write('Script/Resources/Assets/sample.vs', 'output "sample";\n')
		binary = self.repo / 'Script/Resources/Assets/developer.svg'
		binary.write_bytes(b'<svg>\n</svg>\n')
		expected = collection_bundle(self.repo)
		for source in list(self.root.rglob('*.md')) + [binary, binary.with_name('sample.vs')]:
			source.write_bytes(source.read_bytes().replace(b'\n', b'\r\n'))
		self.assertEqual(expected, collection_bundle(self.repo))
		sample = binary.with_name('sample.vs')
		sample.write_bytes(sample.read_bytes().replace(b'\r\n', b'\r'))
		self.assertEqual(expected, collection_bundle(self.repo))
		self.publish_collection()
		check_collection(self.repo)
		self.assertEqual(b'output "sample";\n', (output / 'User/_resources/assets/sample.vs').read_bytes())
		self.assertEqual(b'<svg>\n</svg>\n', (output / '_resources/assets/developer.svg').read_bytes())
		self.assertEqual(b'<svg>\r\n</svg>\r\n', binary.read_bytes())

















	def test_real_repository_documented_c_abi_names_and_localization_paths_exist(self):
		repo = Path(__file__).resolve().parents[4]
		_, user, developer, _ = catalog(repo)
		public = '\n'.join(p.read_text(encoding='utf-8-sig')
						   for p in (source_module_root(repo) / 'VeroScriptInterop/Public').rglob('*.h'))
		for path in list(user) + developer:
			if path.suffix.lower() != '.md' or 'History' in path.parts:
				continue
			content = path.read_text(encoding='utf-8-sig')
			for name in re.findall(r'\bvs_interop_[A-Za-z0-9_]+', content):
				if not name.endswith('_'):  # Wildcard families are not individual exports.
					self.assertIn(name, public, path)
		for path in (source_module_root(repo) / 'VeroScript/Resources/Docs/User').glob('*/localization.md'):
			for route in re.findall(r'^  (\S*Resources/Localization/<culture>/\S*)',
									path.read_text(encoding='utf-8-sig'), re.M):
				self.assertTrue((source_module_root(repo) / route.replace('<culture>', 'en-US')).exists(),
								f'{path}: {route}')
		# Source diagrams and inline paths are outside Markdown link validation.
		for module in ('VeroScriptInstaller',):
			module_root = source_module_root(repo) / module
			for path in (module_root / 'Resources/Docs/Developer').glob('*/index.md'):
				routes = re.findall(r'`(App/[^`]*Localization/[^`]*)`',
									path.read_text(encoding='utf-8-sig'))
				self.assertTrue(routes, f'{path}: missing UI catalog ownership path')
				for route in routes:
					resolved = route.replace('<culture>', 'en-US').replace('<BCP47>', 'en-US')
					self.assertTrue((module_root / resolved).is_dir(), f'{path}: {route}')



	def test_markdown_links_preserve_code_titles_and_encoded_targets(self):
		text = ('```VeroScript\n[factor](int value)\n```\n'
				'`[fake](missing.md)`\n\n'
				'[API `Value`](<a b.md#api-value> "title")\n'
				'[example](a(b).md?q=1#part)\n'
				'![image](image.svg)\n'
				'[ref]: target.md "title"\n'
				'<a href="other.md#section">other</a>\n')
		self.assertEqual(['a b.md#api-value', 'a(b).md?q=1#part', 'image.svg', 'target.md', 'other.md#section'],
						 [url for _, _, url in links(text)])
		rewritten = rewrite_links(text, lambda url: 'new/' + url)
		self.assertIn('[factor](int value)', rewritten)
		self.assertIn('`[fake](missing.md)`', rewritten)
		self.assertIn('"title"', rewritten)
		self.assertIn('<new/a b.md#api-value>', rewritten)
		self.assertEqual({'api-value', 'api-value-1', '中文标题', 'explicit'},
						 anchors('# API `Value`\n# API `Value`\n## 中文标题\n<a id="explicit"></a>\n```\n# Hidden\n```\n'))

	def test_source_and_offline_links_locales_and_resource_closure(self):
		source = self.root / 'User/en-US/index.md'
		source.write_text('# Overview\n\n[page](page.md#api-value)\n![image](../../../../../Resources/Assets/a%20b.svg)\n', encoding='utf-8')
		self.write('Script/Resources/Assets/a b.svg', '<svg/>')
		self.assertEqual([], check_links(list(self.root.rglob('*.md'))))
		output = self.repo / '.tmp/docs'
		self.assertEqual(6, build_docs(self.repo, output))
		self.assertEqual([], check_links(list(output.rglob('*.md'))))
		verify_bundle(self.repo, output)
		self.assertIn(b'../_resources/assets/a%20b.svg', (output / 'en-US/index.md').read_bytes())
		self.assertFalse(any('Developer' in str(p) for p in output.rglob('*')))
		(output / 'en-US/page.md').write_text('# changed', encoding='utf-8')
		with self.assertRaisesRegex(ValueError, 'stale'):
			verify_bundle(self.repo, output)

	def test_list_prose_comments_titles_and_escaped_destinations(self):
		text = ('- parent\n    - [child](child.md "title [fake](wrong.md)")\n\n'
				'> ```md\n> [code](ignored.md)\n> ```\n\n'
				'    [indented code](ignored.md)\n\n'
				'<!-- [comment](ignored.md) -->\n'
				'[escaped](file\\(name\\).md)\n'
				'[reference]: target.md "[fake](wrong.md)"\n')
		self.assertEqual(['child.md', r'file\(name\).md', 'target.md'],
						 [url for _, _, url in links(text)])
		rewritten = rewrite_links(text, lambda url: 'new/' + url)
		self.assertIn('title [fake](wrong.md)', rewritten)
		self.assertIn('<!-- [comment](ignored.md) -->', rewritten)
		file = self.write('custom/file(name).md', '# target')
		source = self.write('custom/index.md', r'[escaped](file\(name\).md)')
		self.assertEqual([], check_links([source]))

	def test_setext_heading_collision_entities_and_literal_code(self):
		self.assertEqual({'one', 'one-1', 'one-2', 'two--three', 'type', 'explicit'},
						 anchors('# One\n# One-1\n# One\nTwo &amp; Three\n---\n'
								 '## `<Type>`\n<a id="explicit"></a>\n'
								 '<!-- <a id="ignored"></a> -->\n'))

	def test_support_anchor_is_checked_before_creating_output(self):
		self.write('Script/Resources/Assets/guide.md', '# Resource')
		source = self.root / 'User/en-US/index.md'
		source.write_text('[resource](../../../../../Resources/Assets/guide.md#missing)')
		output = self.repo / '.tmp/bad-resource'
		with self.assertRaisesRegex(ValueError, 'anchor'):
			build_docs(self.repo, output)
		self.assertFalse(output.exists())

	def test_custom_offline_tree_requires_contained_existing_links(self):
		source = self.write('custom/index.md', '[bad](missing.md)')
		with self.assertRaisesRegex(ValueError, 'missing'):
			check_tree(source.parent)
		self.write('outside.md', '# Outside')
		source.write_text('[outside](../outside.md)')
		with self.assertRaisesRegex(ValueError, 'escapes'):
			check_tree(source.parent)
		source.write_text('[root](/outside.md)')
		with self.assertRaisesRegex(ValueError, 'root-relative'):
			check_tree(source.parent)
		source.write_text('# Home\n[home](#home)')
		self.assertEqual([source], check_tree(source.parent))

	def test_offline_linked_entry_is_rejected_even_with_matching_bytes(self):
		output = self.repo / '.tmp/linked-output'
		build_docs(self.repo, output)
		page = output / 'en-US/page.md'
		external = self.write('same.md', page.read_text())
		original = page.read_bytes()
		page.unlink()
		try:
			page.symlink_to(external)
		except OSError:
			page.write_bytes(original)
			self.skipTest('Creating symlinks requires Windows Developer Mode or privilege')
		with self.assertRaisesRegex(ValueError, 'Linked'):
			verify_bundle(self.repo, output)

	def test_readonly_offline_tree_allows_ancestor_alias_but_rejects_linked_root(self):
		output = self.repo / '.tmp/real/docs'
		build_docs(self.repo, output)
		alias = self.repo / '.tmp/alias'
		try:
			alias.symlink_to(output.parent, target_is_directory=True)
		except OSError:
			self.skipTest('Creating symlinks requires Windows Developer Mode or privilege')
		verify_bundle(self.repo, alias / 'docs')
		with self.assertRaisesRegex(ValueError, 'Linked'):
			check_tree(alias)
		with self.assertRaisesRegex(ValueError, 'Linked'):
			build_docs(self.repo, alias / 'new-docs')

	def test_empty_custom_tree_is_rejected(self):
		root = self.repo / 'empty'
		root.mkdir()
		with self.assertRaisesRegex(ValueError, 'Empty'):
			check_tree(root)

	def test_archive_preserves_empty_link_targets_after_extraction(self):
		source = self.write('package/docs/index.md', '# Home\n[empty](empty/)\n[nested](nested/leaf/)\n').parent
		(source / 'empty').mkdir()
		(source / 'nested/leaf').mkdir(parents=True)
		archive = self.repo / 'package.zip'
		self.assertEqual(1, archive_docs(source.parent, archive))
		with zipfile.ZipFile(archive) as bundle:
			self.assertTrue(bundle.getinfo('package/docs/empty/').is_dir())
			self.assertTrue(bundle.getinfo('package/docs/nested/leaf/').is_dir())
			bundle.extractall(self.repo / 'extracted')
		check_tree(self.repo / 'extracted/package/docs')

	@unittest.skipUnless(hasattr(os, 'mkfifo'), 'Unix special filesystem entries')
	def test_special_documentation_entry_is_rejected(self):
		root = self.write('special/index.md', '# Home').parent
		os.mkfifo(root / 'stream')
		with self.assertRaisesRegex(ValueError, 'Unsupported'):
			check_tree(root)

	def test_published_locale_entrypoint_and_file_directory_conflict(self):
		path = self.root / 'manifest.json'
		manifest = json.loads(path.read_text())
		manifest['user'][0]['output'] = 'other/{locale}'
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'entrypoint'):
			user_bundle(self.repo)
		manifest['user'] = [{'source': 'User/{locale}/index.md', 'output': '{locale}/index.md'},
							{'source': 'User/{locale}/page.md', 'output': '{locale}/index.md/page.md'}]
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'file/directory conflict'):
			user_bundle(self.repo)
		manifest['user'][1]['output'] = 'README.md/{locale}/page.md'
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'file/directory conflict'):
			user_bundle(self.repo)

	def test_localized_binary_assets_are_copied_without_text_decoding(self):
		for locale in self.locales:
			path = self.root / 'User' / locale / 'diagram.png'
			path.write_bytes(b'\x89PNG\r\n\x1a\n\xff')
			page = self.root / 'User' / locale / 'index.md'
			page.write_text('# Overview\n\n![diagram](diagram.png)\n', encoding='utf-8')
		output = self.repo / '.tmp/docs-with-images'
		self.assertEqual(7, build_docs(self.repo, output))
		verify_bundle(self.repo, output)
		self.assertEqual(b'\x89PNG\r\n\x1a\n\xff', (output / 'en-US/diagram.png').read_bytes())

	def test_missing_locale_and_duplicate_destination_are_rejected(self):
		(self.root / 'User/zh-CN/page.md').unlink()
		with self.assertRaisesRegex(ValueError, 'parity'):
			catalog(self.repo)
		self.write('Script/Core/Resources/Docs/User/zh-CN/page.md', '# API `Value`\n')
		path = self.root / 'manifest.json'
		manifest = json.loads(path.read_text())
		manifest['user'].append({'source': 'User/{locale}/index.md', 'output': '{locale}/INDEX.md'})
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'Duplicate'):
			catalog(self.repo)

	def test_missing_anchor_source_only_links_and_unsafe_outputs_are_rejected(self):
		source = self.root / 'User/en-US/index.md'
		source.write_text('[bad](page.md#missing)\n', encoding='utf-8')
		with self.assertRaisesRegex(ValueError, 'anchor'):
			build_docs(self.repo, self.repo / '.tmp/docs')
		source.write_text('[private](../../Developer/en-US/index.md)\n', encoding='utf-8')
		with self.assertRaisesRegex(ValueError, 'source-only'):
			user_bundle(self.repo)
		with self.assertRaisesRegex(ValueError, 'Release output must be under'):
			build_docs(self.repo, self.root)
		source.write_text('# Overview\n', encoding='utf-8')
		occupied = self.write('.tmp/occupied/keep.md', 'keep').parent
		with self.assertRaisesRegex(ValueError, 'empty'):
			build_docs(self.repo, occupied)
		self.assertEqual('keep', (occupied / 'keep.md').read_text())

	def test_portable_source_backlinks_use_manifest_and_preserve_original(self):
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		settings['resources'].append({'source': 'Script/Resources/Docs/Tutorials', 'output': 'Tutorials'})
		path.write_text(json.dumps(settings))
		source = self.write('Script/Resources/Docs/Tutorials/en-US/lesson.md',
							'[guide](../../../../Core/Resources/Docs/User/en-US/page.md#api-value)\r\n')
		page = self.root / 'User/en-US/index.md'
		page.write_bytes(page.read_bytes() + b'[lesson](../../../../../Resources/Docs/Tutorials/en-US/lesson.md)\n')
		expected = b'[guide](../../en-US/page.md#api-value)\r\n'
		self.assertEqual(expected, portable_bytes(self.repo, source))
		self.write('Script/Resources/Docs/private.md', '# private')
		asset = self.write('Script/Resources/Assets/icons/README.md',
						   '[guide](../../../Core/Resources/Docs/User/en-US/page.md#api-value)\n')
		self.write('Script/Resources/Assets/icons/keep.svg', '<svg/>')
		destination = self.repo / '.tmp/release/Resources'
		self.write('.tmp/release/Resources/README.md', 'existing')
		destination.mkdir(parents=True, exist_ok=True)
		assets_destination = self.repo / '.tmp/Resources/Assets'
		copy_tree(self.repo / 'Script/Resources/Assets', assets_destination)
		build_docs(self.repo, destination / 'Docs')
		self.assertEqual(expected.replace(b'\r\n', b'\n'), (destination / 'Docs/Tutorials/en-US/lesson.md').read_bytes())
		self.assertFalse((destination / 'tutorials').exists())
		self.assertIn(b'Core/Resources', source.read_bytes())
		self.assertFalse((destination / 'Resources/Docs').exists())
		self.assertFalse((destination / 'Resources/Tutorials').exists())
		self.assertFalse((destination / 'assets').exists())
		self.assertTrue((assets_destination / 'icons/keep.svg').is_file())
		self.assertEqual(b'[guide](../../Docs/en-US/page.md#api-value)\n',
						 (assets_destination / 'icons/README.md').read_bytes())
		self.assertIn(b'Core/Resources/Docs', asset.read_bytes())
		self.assertEqual('existing', (destination / 'README.md').read_text())
		self.assertEqual({'Resources/Assets/icons/keep.svg',
						  'Resources/Assets/icons/README.md'},
						 {name for name, _ in release_asset_files(self.repo)})

	def test_gui_resource_examples_keep_companions_and_exclude_caches(self):
		root = 'Script/Modules/Multimedia/VeroScriptGui/Resources/Examples'
		expected = {}
		for name, content in (('Showcase/main.vs', 'import "page.vswml";\n'),
							  ('Showcase/page.vswml', '<Widget />\n'),
							  ('Showcase/page.vswcss', 'Widget {}\n'),
							  ('Showcase/State.vs', 'class State {}\n'),
							  ('Showcase/image.svg', '<svg />\n')):
			expected['Resources/Examples/Gui/' + name] = self.write(root + '/' + name, content)
		self.write(root + '/Showcase/.verocache/cache.vsbc', 'generated')
		self.write(root + '/Showcase/__pycache__/cache.pyc', 'generated')
		self.assertEqual(expected, dict(release_asset_files(self.repo)))

	def test_linked_tutorial_is_published_only_under_docs_with_offline_dependencies(self):
		settings_path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(settings_path.read_text())
		settings['resources'].append({'source': 'Script/Resources/Docs/Tutorials',
									  'output': 'Tutorials'})
		settings_path.write_text(json.dumps(settings))
		source = self.write('Script/Resources/Docs/Tutorials/en-US/lesson.md',
							'# Lesson\n[guide](../../../../Core/Resources/Docs/User/en-US/page.md?mode=offline#api-value)\n'
							'[next](next.md#next)\n')
		self.write('Script/Resources/Docs/Tutorials/en-US/next.md',
				   '# Next\n[lesson](lesson.md#lesson)\n')
		page = self.root / 'User/en-US/index.md'
		page.write_bytes(page.read_bytes() +
						 b'[lesson](../../../../../Resources/Docs/Tutorials/en-US/lesson.md#lesson)\n')
		bundle = user_bundle(self.repo)
		self.assertIn('Tutorials/en-US/lesson.md', bundle)
		self.assertIn('Tutorials/en-US/next.md', bundle)
		expected = (b'# Lesson\n[guide](../../en-US/page.md?mode=offline#api-value)\n'
					b'[next](next.md#next)\n')
		self.assertEqual(expected, portable_bytes(self.repo, source))
		destination = self.repo / '.tmp/portable/Resources'
		destination.mkdir(parents=True, exist_ok=True)
		build_docs(self.repo, destination / 'Docs')
		self.assertEqual(expected, (destination / 'Docs/Tutorials/en-US/lesson.md').read_bytes())
		self.assertFalse((destination / 'tutorials').exists())
		self.assertFalse((destination / 'Resources/Tutorials').exists())
		self.assertEqual([], check_links(list(destination.rglob('*.md')), boundary=destination))

	def test_document_dependency_and_loose_script_copy_have_distinct_link_origins(self):
		settings_path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(settings_path.read_text())
		settings['resources'].append({'source': 'Script/VeroScript/Resources/Scripts/examples', 'output': '_resources/examples'})
		settings_path.write_text(json.dumps(settings))
		source = self.write('Script/VeroScript/Resources/Scripts/examples/package/README.md',
							'[guide](../../../../../Core/Resources/Docs/User/en-US/page.md#api-value)\n')
		page = self.root / 'User/en-US/index.md'
		page.write_bytes(page.read_bytes() + b'[example](../../../../../VeroScript/Resources/Scripts/examples/package/README.md)\n')
		self.assertEqual(b'[guide](../../../en-US/page.md#api-value)\n', user_bundle(self.repo)['_resources/examples/package/README.md'])
		self.assertEqual(b'[guide](../../../Docs/en-US/page.md#api-value)\n', portable_bytes(self.repo, source))

	def test_standalone_sdk_readme_keeps_local_links_and_uses_online_docs(self):
		path = self.repo / 'Script/Resources/Docs/documentation.json'
		settings = json.loads(path.read_text())
		settings.update(repositoryBaseUrl='https://example.test/repo', repositoryRef='main')
		path.write_text(json.dumps(settings))
		self.write('Script/Core/example.cpp', '// example')
		source = self.write('Script/Core/README.md', '[user](Resources/Docs/User/en-US/index.md)\n[local](example.cpp)\n')
		result = portable_bytes(self.repo, source)
		self.assertIn(b'https://example.test/repo/blob/main/Script/Core/Resources/Docs/User/en-US/index.md', result)
		self.assertIn(b'[local](example.cpp)', result)

	def test_unlisted_user_page_and_wrong_audience_are_rejected(self):
		manifest = self.root / 'manifest.json'
		definition = json.loads(manifest.read_text())
		definition['user'] = [{'source': 'User/{locale}/index.md', 'output': '{locale}/index.md'}]
		manifest.write_text(json.dumps(definition))
		with self.assertRaisesRegex(ValueError, 'Unlisted'):
			catalog(self.repo)
		definition['user'] = [{'source': 'Developer/{locale}/index.md', 'output': '{locale}/index.md'}]
		manifest.write_text(json.dumps(definition))
		with self.assertRaisesRegex(ValueError, 'owning locale'):
			catalog(self.repo)

	def test_real_repository_source_and_zip_ready_layout(self):
		repo = Path(__file__).resolve().parents[4]
		_, user, developer, _ = catalog(repo)
		self.assertEqual([], check_links(list(user) + developer))
		bundle = user_bundle(repo)
		self.assertTrue(all('Developer/' not in name for name in bundle))
		self.assertIn('en-US/cpp/index.md', bundle)
		self.assertIn('zh-CN/toolchain/cli.md', bundle)
		self.assertIn('zh-TW/installation-and-distribution.md', bundle)
		with tempfile.TemporaryDirectory() as temporary:
			output = Path(temporary) / 'docs'
			for name, data in bundle.items():
				path = output / name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_bytes(data)
			archive = shutil.make_archive(str(Path(temporary) / 'package'), 'zip', temporary, 'docs')
			unpacked = Path(temporary) / 'unpacked'
			shutil.unpack_archive(archive, unpacked)
			verify_bundle(repo, unpacked / 'docs')

	@unittest.skipUnless(sys.platform == 'win32', 'Windows documentation packaging')
	def test_custom_package_rejects_bad_links_and_rolls_back_replacement(self):
		packager = Path(__file__).resolve().parents[1] / 'package-veroscript-docs.ps1'
		source = self.write('package-source/index.md', '# Home\n[home](#home)\n[empty](empty/)')
		self.write('package-source/.hidden.txt', 'hidden supporting file')
		(source.parent / 'empty').mkdir()
		output = self.repo / 'package-output'
		args = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(packager),
				'-Version', '1.0.0', '-SourceRoot', str(source.parent), '-OutRoot', str(output)]
		subprocess.run(args, check=True, capture_output=True, text=True)
		archive = output / 'VeroScriptDocs-1.0.0.zip'
		page = output / 'VeroScriptDocs-1.0.0/docs/index.md'
		previous_zip, previous_page = archive.read_bytes(), page.read_bytes()
		with zipfile.ZipFile(archive) as bundle:
			self.assertEqual(b'hidden supporting file', bundle.read('VeroScriptDocs-1.0.0/docs/.hidden.txt'))
			self.assertTrue(bundle.getinfo('VeroScriptDocs-1.0.0/docs/empty/').is_dir())
			bundle.extractall(self.repo / 'extracted-custom')
		check_tree(self.repo / 'extracted-custom/VeroScriptDocs-1.0.0/docs')
		for overlapping in (source.parent, source.parent / 'output', self.repo):
			rejected = subprocess.run(args[:-1] + [str(overlapping)], capture_output=True, text=True)
			self.assertNotEqual(0, rejected.returncode, rejected.stdout)
			self.assertIn('must not overlap', rejected.stderr)
			self.assertEqual(previous_zip, archive.read_bytes())
			self.assertEqual(previous_page, page.read_bytes())
		self.assertFalse((source.parent / 'output').exists())
		for bad in ('[bad](missing.md)', '[escape](../outside.md)'):
			source.write_text(bad)
			rejected = subprocess.run(args, capture_output=True, text=True)
			self.assertNotEqual(0, rejected.returncode, rejected.stdout)
			self.assertEqual(previous_zip, archive.read_bytes())
			self.assertEqual(previous_page, page.read_bytes())
		source.write_text('# Changed')
		quote = lambda path: "'" + str(path).replace("'", "''") + "'"
		command = ("$ErrorActionPreference='Stop'; $script:Injected=$false; "
				   "function Move-Item { param($LiteralPath, $Destination); "
				   "if ($LiteralPath.Replace('\\', '/') -match '/docs-package-[^/]+/VeroScriptDocs-1\\.0\\.0\\.zip$' "
				   "-and -not $script:Injected) { $script:Injected=$true; throw 'Injected zip move failure' }; "
				   "Microsoft.PowerShell.Management\\Move-Item -LiteralPath $LiteralPath -Destination $Destination }; "
				   f"& {quote(packager)} -Version 1.0.0 -SourceRoot {quote(source.parent)} -OutRoot {quote(output)}")
		rejected = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command],
								  capture_output=True, text=True)
		self.assertNotEqual(0, rejected.returncode, rejected.stdout)
		self.assertIn('Injected zip move failure', rejected.stderr)
		self.assertEqual(previous_zip, archive.read_bytes())
		self.assertEqual(previous_page, page.read_bytes())
		self.assertEqual([], list(output.glob('.docs-backup-*')))


if __name__ == '__main__':
	unittest.main()
