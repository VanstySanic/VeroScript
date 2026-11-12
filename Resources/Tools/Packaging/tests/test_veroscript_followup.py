# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Release source coverage, independent versions and GUI plugin contracts."""
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))
from veroscript_products import ALL_BUSINESSES, IDE_PRODUCTS, check_product_inventory, business_route
from veroscript_ide import ide_build_selection, stage_ide, verify_ide_artifact, verify_jetbrains_pair
from veroscript_applications import finalize_application_sdk, verify_application_payload
from veroscript_vm import stage_backend
from test_veroscript_ide import vsix


class FollowupTests(unittest.TestCase):
	def setUp(self):
		self.temp = tempfile.TemporaryDirectory(prefix='vero-release-contract-')
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name).resolve()

	def test_inventory_rejects_an_unregistered_editor_or_native_module(self):
		for route, marker in [('Extensions/Editor/VSCode/FutureEditor', 'package.json'),
							  ('Extensions/Platform/Mobile/VeroScriptFuture', 'CMakeLists.txt'),
							  ('Modules/Future/VeroScriptExtra', 'CMakeLists.txt')]:
			with self.subTest(route=route):
				root = self.root / marker.replace('.', '-')
				file = root / route / marker
				file.parent.mkdir(parents=True)
				file.write_text('{}')
				if marker == 'CMakeLists.txt':
					(file.parent / 'Public').mkdir()
				with patch('veroscript_paths.module_root', return_value=root):
					with self.assertRaisesRegex(ValueError, 'Unregistered release product'):
						check_product_inventory(root)

	def test_real_inventory_and_gui_routes_are_complete(self):
		self.assertEqual(check_product_inventory(REPO), {'businesses': len(ALL_BUSINESSES), 'moduleSources': 18, 'editorSources': 4})
		for business in ('GuiVSCode', 'GuiJetBrains'):
			family = 'VSCode' if business == 'GuiVSCode' else 'JetBrains'
			self.assertEqual(business_route(business), 'Extensions/Editor/' + family + '/' + business)
		self.assertEqual(ide_build_selection(['GuiVSCode']), ['VSCode', 'GuiVSCode'])
		self.assertEqual(ide_build_selection(['GuiJetBrains']), ['JetBrains', 'GuiJetBrains'])

	def test_gui_vsix_requires_its_base_and_retains_editor_schemas(self):
		artifact = self.root / 'VeroScriptGuiVSCode-2.3.4.vsix'
		package = {'name': 'veroscript-gui', 'publisher': 'VanstySanic', 'version': '2.3.4',
				   'main': './dist/extension.cjs', 'extensionDependencies': ['VanstySanic.veroscript']}
		def write():
			vsix(artifact, '2.3.4', {'extension/package.json': json.dumps(package),
				'extension.vsixmanifest': '<PackageManifest><Metadata><Identity Id="veroscript-gui" Publisher="VanstySanic" Version="2.3.4"/></Metadata></PackageManifest>'})
		write()
		verify_ide_artifact(artifact, 'GuiVSCode', '2.3.4')
		package['extensionDependencies'] = []
		write()
		with self.assertRaisesRegex(ValueError, 'base extension dependency'):
			verify_ide_artifact(artifact, 'GuiVSCode', '2.3.4')

	def test_gui_jetbrains_jar_has_an_independent_identity_and_dependency(self):
		artifact = self.root / 'VeroScriptGuiJetBrains-2.3.4.zip'
		def write(depends):
			data = io.BytesIO()
			with zipfile.ZipFile(data, 'w') as jar:
				jar.writestr('META-INF/plugin.xml', '<idea-plugin><id>com.vanstysanic.veroscript.gui</id><version>2.3.4</version><idea-version since-build="262.10315.191"/>' + depends + '</idea-plugin>')
				jar.writestr('META-INF/LICENSE', 'MIT')
				jar.writestr('com/vanstysanic/veroscript/gui/jetbrains/Gui.class', 'compiled')
			with zipfile.ZipFile(artifact, 'w') as archive:
				archive.writestr('VeroScriptGuiJetBrains/lib/VeroScriptGuiJetBrains-2.3.4.jar', data.getvalue())
		write('<depends>com.vanstysanic.veroscript</depends>')
		verify_ide_artifact(artifact, 'GuiJetBrains', '2.3.4')
		write('')
		with self.assertRaisesRegex(ValueError, 'base plugin dependency'):
			verify_ide_artifact(artifact, 'GuiJetBrains', '2.3.4')

	def test_vm_uses_its_configured_product_version_and_rejects_a_stale_binary(self):
		build = self.root / 'build'
		build.mkdir()
		(build / 'CMakeCache.txt').write_text('VS_VEROSCRIPT_VM_VERSION:STRING=2.3.4\n')
		descriptor = {'id': 'vs-bytecode', 'version': '2.3.4', 'target': {'os': 'windows', 'arch': 'x64',
			'environment': 'msvc', 'coreBuild': 'a' * 64, 'core': '0.1.1', 'abis': {'vm': 'abi'}}}
		def install(build, destination, component, config):
			destination.mkdir()
			(destination / 'VeroScriptVmBytecode.dll').write_bytes(b'fixture')
		with patch('veroscript_vm.install', side_effect=install), patch('veroscript_vm.subprocess.check_output', side_effect=lambda *a, **k: json.dumps(descriptor)):
			output = self.root / 'backend'
			stage_backend(build, output, self.root / 'tool')
			self.assertEqual(json.loads((output / 'veroscript-package.json').read_text())['version'], '2.3.4')
			descriptor['version'] = '0.1.1'
			with self.assertRaisesRegex(ValueError, 'identity does not match'):
				stage_backend(build, self.root / 'stale', self.root / 'tool', package_version='2.3.4')

	def test_layout_preview_uses_registered_versions_and_does_not_publish(self):
		from veroscript_release_plan import release_plan
		config = {'businesses': {name: '0.1.1' for name in ALL_BUSINESSES}}
		config['businesses'].update(CoreSDK='2.3.4', GuiVSCode='3.4.5')
		before = set(self.root.iterdir())
		plan = release_plan(REPO, config, ['win-x64', 'linux-x64'])
		tree = plan['directories']['Release']
		self.assertTrue(plan['previewOnly'])
		self.assertIn('VeroScriptGuiVSCode-3.4.5.vsix', tree['Extensions']['Editor']['VSCode']['GuiVSCode']['any']['3.4.5']['packages'])
		self.assertIn('VeroScriptSDK-2.3.4-win-x64.zip', tree['CoreSDK']['win-x64']['2.3.4']['sdks'])
		self.assertIn('vs-bytecode-vm-0.1.1-linux-x64.vspkg', tree['Modules']['Execution']['Vm']['linux-x64']['0.1.1']['packages'])
		self.assertNotIn('Installer', json.dumps(tree['Modules']))
		self.assertEqual(set(self.root.iterdir()), before)

	def test_runtime_receipt_checks_complete_members_even_when_audit_entries_are_truncated(self):
		import hashlib
		from veroscript_release import archive_entries
		from application_fixtures import application_archive
		source = self.root / 'RuntimeFixture'
		source.mkdir()
		payload = b'large-runtime-payload' * 100000
		(source / 'veroscript-runtime').write_bytes(payload)
		(source / 'veroscript-launcher').write_bytes(b'launcher')
		(source / 'runtime-manifest.json').write_text('{"application":true}')
		for name in ('licenses/application/json/LICENSE.MIT', 'licenses/application/miniz/LICENSE', 'licenses/application/sha256/LICENSE', 'LICENSE', 'Vm/libVeroScriptVmBytecode.so', 'stdlib/core.vs'):
			file = source / name
			file.parent.mkdir(parents=True, exist_ok=True)
			file.write_bytes(b'functional fixture')
		receipt = {'format': 'veroscript-runtime', 'formatVersion': '0.1.1', 'applicationProtocol': '0.1.1',
			'target': {'os': 'linux', 'arch': 'x64', 'environment': 'gnu', 'core': '0.1.1', 'coreBuild': 'a'*64,
				'abis': {'vm':'abi'}, 'minimumVersions': {}, 'capabilities': []},
			'executable': 'veroscript-runtime', 'stdlib': ['stdlib'], 'extensions': {},
			'files': [{'path': 'veroscript-runtime', 'bytes': str(len(payload)), 'sha256': hashlib.sha256(payload).hexdigest(), 'mode': '0755'}]}
		for name in ('LICENSE', 'Vm/libVeroScriptVmBytecode.so', 'stdlib/core.vs',
			'licenses/application/json/LICENSE.MIT', 'licenses/application/miniz/LICENSE', 'licenses/application/sha256/LICENSE'):
			data = (source / name).read_bytes()
			receipt['files'].append({'path': name, 'bytes': str(len(data)), 'sha256': hashlib.sha256(data).hexdigest(), 'mode': '0644'})
		for suffix in ('.zip', '.tar.gz'):
			for stale in (False, True):
				with self.subTest(suffix=suffix, stale=stale):
					receipt['files'][0]['sha256'] = 'b'*64 if stale else hashlib.sha256(payload).hexdigest()
					(source / 'veroscript-runtime.json').write_text(json.dumps(receipt))
					archive = self.root / ('RuntimeFixture' + suffix)
					application_archive(source, archive, 'veroscript-runtime')
					entries = archive_entries(archive, max_entry_bytes=4096)
					arguments = (archive, entries, 'RuntimeFixture/', 'runtime', ('linux', 'x64'), None, 'a'*64, '0.1.1')
					if stale:
						with self.assertRaisesRegex(ValueError, 'receipt differs'):
							verify_application_payload(*arguments)
					else:
						verify_application_payload(*arguments)

	def test_readonly_package_runtime_sdk_requires_its_exports_and_exact_core_identity(self):
		prefix = 'SDK/'
		folder = 'lib/cmake/VeroScriptPackageRuntime/'
		config = folder + 'VeroScriptPackageRuntimeConfig.cmake'
		pin = folder + 'VeroScriptPackageRuntimeCoreBuild.cmake'
		entries = {prefix + name: (b'functional fixture', 0o644) for name in (
			'include/PackageManager/VeroScriptPackageManager.h',
			'lib/libVeroScriptPackageRuntime.a', 'lib/libVeroScriptPackageZip.a',
			folder + 'VeroScriptPackageRuntimeTargets.cmake', folder + 'VeroScriptPackageRuntimeConfigVersion.cmake',
			'licenses/package-runtime/json/LICENSE.MIT', 'licenses/package-runtime/miniz/LICENSE',
			'licenses/package-runtime/sha256/LICENSE')}
		entries[prefix + 'sdk-manifest.json'] = (b'{"application":false,"packageRuntime":true,"buildSystem":false}', 0o644)
		entries[prefix + config] = (b'find_dependency(VeroScript 2.3.4 EXACT CONFIG)', 0o644)
		entries[prefix + pin] = (('set(VeroScriptPackageRuntime_CORE_BUILD_ID "' + 'a'*64 + '")').encode(), 0o644)
		def verify(payload):
			verify_application_payload(self.root / 'unused.zip', payload, prefix, 'core-sdk', ('linux', 'x64'), '2.3.4', 'a'*64, '0.1.1')
		verify(entries)
		for missing in ('lib/libVeroScriptPackageZip.a', folder + 'VeroScriptPackageRuntimeTargets.cmake',
						'licenses/package-runtime/miniz/LICENSE', config):
			with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, 'payload|declared feature'):
				verify({name: value for name, value in entries.items() if name != prefix + missing})
		for name, data in ((config, b'find_dependency(VeroScript 0.1.1 EXACT CONFIG)'),
						   (pin, ('set(VeroScriptPackageRuntime_CORE_BUILD_ID "' + 'b'*64 + '")').encode())):
			with self.subTest(stale=name), self.assertRaisesRegex(ValueError, 'Core version/build identity mismatch'):
				verify({**entries, prefix + name: (data, 0o644)})

	def test_gui_only_staging_builds_the_base_but_collects_only_the_selected_addon(self):
		for addon, base in (('GuiVSCode', 'VSCode'), ('GuiJetBrains', 'JetBrains')):
			with self.subTest(addon=addon):
				repo = self.root / addon
				runtime = repo / 'runtime'
				runtime.mkdir(parents=True)
				(runtime / ('veroscript.exe' if sys.platform == 'win32' else 'veroscript')).write_bytes(b'fixture')
				versions = {base: '2.3.4', addon: '3.4.5'}
				for business in (base, addon):
					product = IDE_PRODUCTS[business]
					source = repo / product['source']
					source.mkdir(parents=True)
					if product['family'] == 'vscode':
						(source / 'package.json').write_text(json.dumps({'name': product['package'], 'version': versions[business]}))
						artifact = source / 'out' / (product['package'] + '-' + versions[business] + '.vsix')
					else:
						artifact = source / 'build/distributions' / (product['artifact'] + '-' + versions[business] + '.zip')
					artifact.parent.mkdir(parents=True)
					artifact.write_bytes(b'fixture archive')
				calls = []
				def run(command, **kwargs):
					calls.append((command, kwargs['cwd'], dict(kwargs['env'])))
					return subprocess.CompletedProcess(command, 0)
				with patch('veroscript_ide.module_root', return_value=repo), patch('veroscript_ide.check_ide_sources'), \
						patch('veroscript_ide.shutil.which', side_effect=lambda name: str(repo / name)), \
						patch('veroscript_ide.subprocess.run', side_effect=run), patch('veroscript_ide.verify_ide_artifact'), \
						patch('veroscript_ide.verify_jetbrains_pair') as pair, \
						patch.dict('os.environ', {'VEROSCRIPT_JETBRAINS_IDE_PATH': str(repo / 'rider')}):
					pins = {}
					result = stage_ide(repo, repo / 'candidate', versions, [addon], runtime, dependency_pins=pins)
					if addon == 'GuiJetBrains':
						self.assertEqual(set(pair.call_args.args[1]), {base, addon})
					else:
						self.assertFalse(pair.called)
				self.assertEqual(set(result), {addon})
				self.assertEqual(pins[addon][base]['version'], versions[base])
				self.assertEqual(pins[addon][base]['buildId'], hashlib.sha256(b'fixture archive').hexdigest())
				self.assertEqual(len(list((repo / 'candidate/ide').iterdir())), 1)
				self.assertEqual({cwd for _, cwd, _ in calls}, {repo / IDE_PRODUCTS[name]['source'] for name in (base, addon)})
				if addon == 'GuiVSCode':
					self.assertTrue(any('--folderless' in command for command, _, _ in calls))
					self.assertTrue(any('--packaged' in command and env.get('VEROSCRIPT_GUI_TEST_NATIVE') == '1'
						for command, cwd, env in calls if cwd == repo / IDE_PRODUCTS[addon]['source']))
				else:
					for command, _, _ in calls:
						self.assertIn(':buildPlugin', command)
						self.assertNotIn(':verifyPlugin', command)

	def test_joint_staging_retains_base_bytes_when_the_included_project_overwrites_its_output(self):
		products = ('JetBrains', 'GuiJetBrains')
		versions = {business: '0.1.1' for business in products}
		payloads = {business: (business + ' standalone build').encode() for business in products}
		artifacts = {}
		for business in products:
			source = self.root / IDE_PRODUCTS[business]['source']
			artifact = source / 'build/distributions' / (IDE_PRODUCTS[business]['artifact'] + '-0.1.1.zip')
			artifact.parent.mkdir(parents=True)
			artifact.write_bytes(payloads[business])
			artifacts[business] = artifact
		runtime = self.root / 'runtime'
		runtime.mkdir()
		(runtime / ('veroscript.exe' if sys.platform == 'win32' else 'veroscript')).write_bytes(b'fixture')
		def run(command, **kwargs):
			if kwargs['cwd'] == self.root / IDE_PRODUCTS['GuiJetBrains']['source']:
				artifacts['JetBrains'].write_bytes(b'included project rebuild')
			return subprocess.CompletedProcess(command, 0)
		def verify_pair(repo, selected, release_versions, env):
			for business in products:
				self.assertEqual(selected[business].read_bytes(), payloads[business])
		with patch('veroscript_ide.module_root', return_value=self.root), patch('veroscript_ide.check_ide_sources'), \
				patch('veroscript_ide.subprocess.run', side_effect=run), patch('veroscript_ide.verify_ide_artifact'), \
				patch('veroscript_ide.verify_jetbrains_pair', side_effect=verify_pair) as pair, \
				patch.dict('os.environ', {'VEROSCRIPT_JETBRAINS_IDE_PATH': str(self.root / 'rider')}):
			output = self.root / 'candidate'
			stage_ide(self.root, output, versions, products, runtime)
			pair.assert_called_once()
		for business in products:
			self.assertEqual((output / 'ide' / artifacts[business].name).read_bytes(), payloads[business])
		self.assertEqual(artifacts['JetBrains'].read_bytes(), b'included project rebuild')

	def test_joint_verifier_checks_both_results_and_the_selected_local_dependency(self):
		ide = self.root / 'rider'
		ide.mkdir()
		(ide / 'product-info.json').write_text(json.dumps({'productCode': 'RD', 'version': '2026.2.2'}))
		source = self.root / IDE_PRODUCTS['GuiJetBrains']['source']
		source.mkdir(parents=True)
		(source / 'gradle.properties').write_text('platformVersion=2026.2.2\n')
		tool = self.root / 'verifier-all.jar'
		tool.write_bytes(b'fixture')
		env = {'VEROSCRIPT_JETBRAINS_IDE_PATH': str(ide), 'VEROSCRIPT_JETBRAINS_VERIFIER_JAR': str(tool)}
		versions = {'JetBrains': '2.3.4', 'GuiJetBrains': '3.4.5'}
		artifacts = {name: self.root / (name + '.zip') for name in versions}
		for artifact in artifacts.values():
			artifact.write_bytes(b'fixture')
		for mode in ('success', 'wrong-base-version', 'incompatible', 'problem-report', 'missing-verdict'):
			def run(command, **kwargs):
				pair = Path(next(arg[1:] for arg in command if arg.startswith('@'))).read_text(encoding='utf-8')
				self.assertIn('path:' + str(artifacts['JetBrains']), pair)
				self.assertIn('path:' + str(artifacts['GuiJetBrains']), pair)
				report = Path(command[command.index('-verification-reports-dir') + 1])
				for name, version in versions.items():
					directory = report / 'RD-build/plugins' / IDE_PRODUCTS[name]['id'] / version
					directory.mkdir(parents=True)
					if mode != 'missing-verdict':
						(directory / 'verification-verdict.txt').write_text('Not compatible' if mode == 'incompatible' else 'Compatible. 1 usage of deprecated API')
					if mode == 'problem-report':
						(directory / 'compatibility-problems.txt').write_text('broken ABI')
					dependency = '2.3.0' if mode == 'wrong-base-version' else versions['JetBrains']
					(directory / 'dependencies.txt').write_text('com.vanstysanic.veroscript:' + dependency + '\n')
				return subprocess.CompletedProcess(command, 0, 'checked', '')
			with self.subTest(mode=mode), patch('veroscript_ide.module_root', return_value=self.root), patch('veroscript_ide.shutil.which', return_value='java'), patch('veroscript_ide.subprocess.run', side_effect=run):
				if mode == 'success':
					verify_jetbrains_pair(self.root, artifacts, versions, env)
				else:
					with self.assertRaisesRegex(ValueError, 'Verifier|verification|verified'):
						verify_jetbrains_pair(self.root, artifacts, versions, env)

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for VM version configuration')
	def test_vm_configures_an_independent_version_without_building_a_backend(self):
		prefix = self.root / 'contract/lib/cmake/VeroScriptExecutionContract'
		prefix.mkdir(parents=True)
		(prefix / 'VeroScriptExecutionContractConfig.cmake').write_text('add_library(VeroScript::VeroScriptExecutionContract INTERFACE IMPORTED)\n')
		(prefix / 'VeroScriptExecutionContractConfigVersion.cmake').write_text('set(PACKAGE_VERSION "0.1.1")\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\nset(PACKAGE_VERSION_EXACT TRUE)\n')
		build = self.root / 'vm-configure'
		result = subprocess.run(['cmake', '-S', str(REPO / 'Modules/Execution/VeroScriptBytecodeVm'), '-B', str(build),
			'-DCMAKE_PREFIX_PATH=' + str(self.root / 'contract'), '-DVS_VEROSCRIPT_VM_VERSION=2.3.4', '-DBUILD_TESTING=OFF'],
			capture_output=True, text=True, timeout=120)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertIn('#define VEROSCRIPT_VM_VERSION "2.3.4"', (build / 'Generated/VeroScriptVmBuild.h').read_text())
		self.assertIn('set(VeroScriptBytecodeVm_VERSION "2.3.4")', (build / 'VeroScriptBytecodeVmConfig.cmake').read_text())
		self.assertFalse(list(build.glob('**/VeroScriptVmBytecode.dll')))
		# Configure the real external SDK consumer with independently versioned
		# Core and VM exports. Imported fixture targets require no native build.
		vm = prefix.parent / 'VeroScriptBytecodeVm'
		vm.mkdir()
		for name in ('VeroScriptBytecodeVmConfig.cmake', 'VeroScriptBytecodeVmConfigVersion.cmake'):
			shutil.copy2(build / name, vm / name)
		(vm / 'VeroScriptBytecodeVmTargets.cmake').write_text('add_library(VeroScript::VeroScriptBytecodeVm INTERFACE IMPORTED)\n')
		core = prefix.parent / 'VeroScript'
		core.mkdir()
		(core / 'VeroScriptConfig.cmake').write_text('add_library(VeroScript::VeroScript INTERFACE IMPORTED)\nadd_library(VeroScript::VeroScriptTooling INTERFACE IMPORTED)\n')
		(core / 'VeroScriptConfigVersion.cmake').write_text('set(PACKAGE_VERSION "3.4.5")\nif(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\nset(PACKAGE_VERSION_EXACT TRUE)\nendif()\n')
		consumer = subprocess.run(['cmake', '-S', str(REPO / 'VeroScript/Resources/Examples/EmbedMinimal'),
			'-B', str(self.root / 'sdk-consumer'), '-DCMAKE_PREFIX_PATH=' + str(self.root / 'contract'),
			'-DVEROSCRIPT_SDK_PACKAGE_VERSION=3.4.5'], capture_output=True, text=True, timeout=120)
		self.assertEqual(consumer.returncode, 0, consumer.stdout + consumer.stderr)

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for export consumption')
	def test_application_exports_consume_an_independently_versioned_core_sdk(self):
		import hashlib
		stage = self.root / 'sdk'
		lib = stage / 'lib'
		lib.mkdir(parents=True)
		(lib / 'VeroScript.lib').write_bytes(b'published core')
		core = lib / 'cmake/VeroScript'
		core.mkdir(parents=True)
		(core / 'VeroScriptConfig.cmake').write_text('set(VeroScript_BUILD_ID "' + hashlib.sha256(b'published core').hexdigest() + '")\n')
		(core / 'VeroScriptConfigVersion.cmake').write_text('set(PACKAGE_VERSION "2.3.4")\nif(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\nset(PACKAGE_VERSION_EXACT TRUE)\nendif()\n')
		for name, owner in [('VeroScriptApplication', 'VeroScriptApplication'), ('VeroScriptPackageRuntime', 'VeroScriptPackageManager')]:
			folder = lib / 'cmake' / name
			folder.mkdir()
			source = REPO / owner / 'Resources/Tools/CMake' / (name + 'Config.cmake.in')
			(folder / (name + 'Config.cmake')).write_text(source.read_text().replace('@PACKAGE_INIT@', ''))
			(folder / (name + 'Targets.cmake')).write_text('# fixture targets')
		finalize_application_sdk(stage, '2.3.4')
		consumer = self.root / 'consumer'
		consumer.mkdir()
		(consumer / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(ExportConsumer NONE)\nmacro(check_required_components name)\nendmacro()\nfind_package(VeroScriptApplication CONFIG REQUIRED)\nfind_package(VeroScriptPackageRuntime CONFIG REQUIRED)\n')
		result = subprocess.run(['cmake', '-S', str(consumer), '-B', str(self.root / 'consumer-build'), '-DCMAKE_PREFIX_PATH=' + str(stage)], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
	unittest.main()
