# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import json
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_veroscript_release as release_tests
import test_veroscript_business_release as business_tests
from veroscript_modules import module_selection, verify_module_entries, backend_services, module_dependencies, required_notices
from veroscript_products import FEATURE_MODULE_BUSINESSES, MODULE_PACKAGE_NAMES, MODULE_ENTRY_IDS, INSTALLER_BUSINESSES, business_route
from veroscript_release import archive_entries


class ModuleReleaseTests(unittest.TestCase):
	def setUp(self):
		self.fixture = business_tests.BusinessReleaseTests()
		self.fixture.setUp()
		self.addCleanup(self.fixture.doCleanups)

	def entries(self, business):
		root = self.fixture.repo / ('.tmp/module-fixture-' + business)
		if not root.exists():
			self.fixture.fixture.make_release(root)
		archive = root / 'modules' / f'{MODULE_PACKAGE_NAMES[business]}-1.0.0-linux-x64.tar.gz'
		prefix = archive.name.removesuffix('.tar.gz') + '/'
		return {path[len(prefix):]: value for path, value in archive_entries(archive).items()}

	def test_dependency_closure_and_distribution_policy(self):
		self.assertEqual(module_selection(['WebSocket']), ['Network', 'WebSocket'])
		self.assertEqual(module_selection(['Http', 'Json', 'Http']), ['Json', 'Network', 'Http'])
		self.assertEqual(module_selection(['Network']), ['Network'])
		self.assertEqual(module_selection(['Boost']), ['Network', 'Boost'])
		self.assertEqual(module_selection(['Curl']), ['Network', 'Http', 'Curl'])
		self.assertEqual(MODULE_PACKAGE_NAMES['Boost'], 'vs-boost')
		self.assertEqual(MODULE_PACKAGE_NAMES['Curl'], 'vs-curl')
		self.assertEqual(MODULE_PACKAGE_NAMES['Network'], 'vs-network')
		self.assertEqual(MODULE_ENTRY_IDS['Network'], 'network')
		self.assertEqual(business_route('Network'), 'Modules/Network/Network')
		self.assertTrue(set(FEATURE_MODULE_BUSINESSES).isdisjoint(INSTALLER_BUSINESSES))
		self.assertEqual(business_route('Http'), 'Modules/Network/Http')
		with self.assertRaises(ValueError):
			module_selection(['Unknown'])

	def test_contract_sdk_is_backend_free_and_boost_owns_runtime(self):
		entries = self.entries('Network')
		verify_module_entries(entries, 'Network', '1.0.0', 'linux-x64')
		self.assertFalse(any('Runtime' in path or '/boost/' in path or '/curl/' in path for path in entries))
		entries = self.entries('Boost')
		verify_module_entries(entries, 'Boost', '1.0.0', 'linux-x64')
		for path in ('lib/veroscript/modules/libVeroScriptBoostRuntime.so',
					 'licenses/VeroScriptBoost/boost/LICENSE_1_0.txt'):
			broken = dict(entries)
			del broken[path]
			with self.assertRaisesRegex(ValueError, 'Missing feature module payload'):
				verify_module_entries(broken, 'Boost', '1.0.0', 'linux-x64')

	def test_boost_dependencies_follow_built_capabilities(self):
		flags = {'VEROSCRIPT_BOOST_HAS_HTTP_SERVER': 'ON', 'VEROSCRIPT_BOOST_HAS_WEBSOCKET': 'ON'}
		with patch('veroscript_modules.build_cache', return_value=flags):
			self.assertEqual(module_selection(['Boost'], {'Boost': Path('configured')}),
							 ['Network', 'Http', 'WebSocket', 'Boost'])
		self.assertEqual(backend_services('Boost'), ['network'])
		self.assertEqual(backend_services('Curl'), ['http-client'])
	def test_curl_vendor_exports_reject_absolute_private_library_paths(self):
		entries = self.entries('Curl')
		path = 'lib/cmake/CURL/CURLConfig.cmake'
		entries[path] = (b'set(CURL_LIBRARIES_PRIVATE "/builder/libssl.so")\n', 0o644)
		with self.assertRaisesRegex(ValueError, 'Non-relocatable'):
			verify_module_entries(entries, 'Curl', '1.0.0', 'linux-x64')

	def test_backend_identity_and_service_inventory_are_required(self):
		for business, field, value in (('Network', 'networkImplementationId', ''),
									   ('Boost', 'backendServices', ['network', 'websocket-client']),
									   ('Boost', 'boostRuntimeId', ''),
									   ('Curl', 'backendServices', [])):
			entries = self.entries(business)
			metadata = json.loads(entries['veroscript-module.json'][0])
			metadata[field] = value
			entries['veroscript-module.json'] = (json.dumps(metadata).encode(), 0)
			with self.subTest(business=business, field=field), self.assertRaises(ValueError):
				verify_module_entries(entries, business, '1.0.0', 'linux-x64')

	def test_module_identity_and_target_are_checked(self):
		entries = self.entries('Network')
		with self.assertRaisesRegex(ValueError, 'identity mismatch'):
			verify_module_entries(entries, 'Network', '9.0.0', 'linux-x64')
		broken = dict(entries)
		broken['lib/libVeroScriptNetwork.a'] = (release_tests.elf('arm64'), 0)
		with self.assertRaisesRegex(ValueError, 'native target mismatch'):
			verify_module_entries(broken, 'Network', '1.0.0', 'linux-x64')

	def test_module_localization_is_required_and_parsed(self):
		entries = self.entries('Json')
		path = 'share/VeroScriptJson/Resources/Localization/ja-JP/json.json'
		for content in (None, b'[]', b'{}', b'{"key":0}', b'{"key":""}', b'{'):
			with self.subTest(content=content):
				broken = dict(entries)
				if content is None:
					del broken[path]
				else:
					broken[path] = (content, 0)
				with self.assertRaisesRegex(ValueError, 'feature module (payload|localization)'):
					verify_module_entries(broken, 'Json', '1.0.0', 'linux-x64')

	def test_protocol_cannot_bundle_a_second_runtime(self):
		entries = self.entries('Http')
		entries['lib/veroscript/modules/libVeroScriptBoostRuntime.so'] = (release_tests.elf(), 0)
		with self.assertRaisesRegex(ValueError, 'Only Boost'):
			verify_module_entries(entries, 'Http', '1.0.0', 'linux-x64')

	def test_missing_network_prevents_protocol_publication(self):
		candidate = self.fixture.candidate('without-network')
		with self.assertRaisesRegex(ValueError, 'Business Network dependency version/build is missing'):
			self.fixture.publish(candidate, ['Http'])
		self.assertFalse((self.fixture.root / business_route('Http')).exists())

	def test_protocol_build_mismatch_preserves_previous_publication(self):
		candidate = self.fixture.candidate('original')
		self.fixture.publish(candidate, ['Network', 'Http'])
		# Use the real collection index name returned by its public helper.
		from veroscript_business_release import collection_index
		index = collection_index(self.fixture.root, 'linux-x64')
		before = index.read_bytes()
		changed = self.fixture.candidate('changed')
		path = changed / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest['businessBuildIds'] = {'Network': 'a' * 64}
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'dependency version/build mismatch'):
			self.fixture.publish(changed, ['Http'])
		self.assertEqual(index.read_bytes(), before)

	def test_required_network_version_is_retained_during_update(self):
		from veroscript_business_release import obsolete_business_versions
		self.fixture.publish(self.fixture.candidate('original'), ['Network', 'Http'])
		old = self.fixture.versions['Network']
		versions = {**self.fixture.versions, 'Network': '99.0.0'}
		self.fixture.publish(self.fixture.candidate('network-update', versions=versions), ['Network'])
		from veroscript_business_release import verify_collection
		index = verify_collection(self.fixture.root, 'linux-x64')
		self.assertTrue((self.fixture.root / business_route('Network') / 'linux-x64' / old).is_dir())
		self.assertFalse(any(path.name == old and path.parent.name == 'linux-x64'
							 for path in obsolete_business_versions(self.fixture.root, 'linux-x64', index)))

	def installable(self, business):
		name = MODULE_PACKAGE_NAMES[business]
		path = f'lib/veroscript/modules/libVeroScript{business}Module.so'
		meta = {'schemaVersion': 1, 'name': name, 'business': business, 'version': '1.0.0',
				'target': 'linux-x64', 'profile': 'native-script-sdk', 'payloadId': 'a' * 64,
				'dependencies': module_dependencies(business, {name: '1.0.0' for name in MODULE_PACKAGE_NAMES})}
		if business != 'Json':
			meta['networkImplementationId'] = 'e' * 64
		if business in {'Boost', 'Curl'}:
			meta['backendServices'] = backend_services(business)
		if business == 'Boost':
			meta['boostRuntimeId'] = '0' * 64
			meta.update(nativeTls=False, websocketCompression=False)
		native = {'id': business.lower(), 'path': path, 'linkage': 'dynamic', 'componentVersion': '1.0.0',
				  'roles': ['bindings', 'compiler'] if business == 'Json' else ['provider'] if business in {'Boost', 'Curl'} else ['bindings'],
				  'catalog': 'catalog/bindings.json', 'libraries': []}
		manifest = {'format': 'veroscript-package', 'formatVersion': '0.1.1', 'name': name, 'version': '1.0.0',
					'licenseFile': 'LICENSE', 'target': {'os': 'linux', 'arch': 'x64', 'environment': 'test',
						'coreBuild': 'a' * 64, 'abis': {'native-extension': 'test'}},
					'entries': {'native': [native], 'libraries': []}}
		entries = {'LICENSE': (b'notice', 0), path: (release_tests.elf(), 0),
				   'catalog/bindings.json': (b'{"version":1}', 0)}
		entries.update({path: (b'original notice', 0) for path in required_notices(business, meta)})
		if business == 'Boost':
			runtime = 'lib/veroscript/modules/libVeroScriptBoostRuntime.so'
			manifest['entries']['libraries'] = [{'id': 'boost-runtime', 'path': runtime, 'linkage': 'dynamic'}]
			entries[runtime] = (release_tests.elf(), 0)
			native['libraries'] = ['boost-runtime']
		manifest['dependencies'] = {MODULE_PACKAGE_NAMES[key]: {'version': value, 'usage': ['runtime', 'compile']}
									for key, value in meta['dependencies'].items()}
		if business in {'Boost', 'Curl'}:
			manifest['entries']['backends'] = [{'service': service, 'id': business.lower(), 'module': business.lower()}
												for service in meta['backendServices']]
		def encode(value):
			result = dict(entries)
			result['veroscript-module.json'] = (json.dumps(meta).encode(), 0)
			result['veroscript-package.json'] = (json.dumps(value).encode(), 0)
			return result
		return manifest, encode

	def test_installable_identity_catalog_and_dependencies(self):
		from veroscript_module_packages import verify_installable_entries
		for business in FEATURE_MODULE_BUSINESSES:
			manifest, encode = self.installable(business)
			verify_installable_entries(encode(manifest), business, '1.0.0', 'linux-x64')
			mutations = [lambda m: m.update(name='old-name'),
						 lambda m: m['entries']['native'][0].update(id='VeroScript' + business),
						 lambda m: m['entries']['native'][0].update(catalog='missing.json'),
						 lambda m: m['target'].update(arch='arm64')]
			if business in ('Http', 'WebSocket', 'Boost', 'Curl'):
				mutations += [lambda m: m.update(dependencies={}),
							  lambda m: m['dependencies']['vs-network'].update(version='2.0.0'),
							  lambda m: m['dependencies']['vs-network'].update(usage=['compile'])]
			if business == 'Boost':
				mutations += [lambda m: m['entries']['native'][0].update(libraries=[])]
			if business in {'Boost', 'Curl'}:
				mutations += [lambda m: m['entries'].update(backends=[]),
							  lambda m: m['entries']['backends'][0].update(id='unknown')]
			for mutate in mutations:
				broken = copy.deepcopy(manifest)
				mutate(broken)
				with self.subTest(business=business, broken=broken), self.assertRaises(ValueError):
					verify_installable_entries(encode(broken), business, '1.0.0', 'linux-x64')

	def test_installable_rejects_sdk_and_duplicate_runtime(self):
		from veroscript_module_packages import verify_installable_entries
		manifest, encode = self.installable('Http')
		for path in ('include/Header.h', 'lib/libExample.a', 'lib/veroscript/modules/libVeroScriptBoostRuntime.so'):
			entries = encode(manifest)
			entries[path] = (release_tests.elf(), 0)
			with self.subTest(path=path), self.assertRaises(ValueError):
				verify_installable_entries(entries, 'Http', '1.0.0', 'linux-x64')

	def test_installable_profile_requires_an_archive_in_inventory(self):
		from veroscript_release import verify_release
		candidate = self.fixture.candidate('missing-installable')
		path = candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest['installableModuleBusinesses'] = ['Json']
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'Unexpected/missing release products'):
			verify_release(candidate)


if __name__ == '__main__':
	unittest.main()
