# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Deployment contract and independently published IDE dependency regressions."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_applications import verify_application_payload
from veroscript_business_release import ALL_BUSINESSES, business_root, publish_businesses, verify_collection
from veroscript_packages import stage_package_manager_sdk
from veroscript_ide import check_ide_prerequisites
from veroscript_products import IDE_PRODUCTS
from veroscript_release import archive_entries, digest, verify_release, write_manifest
from veroscript_vm import install
from veroscript_csharp_packages import PACKAGE_IDS, stage_csharp_packages
from application_fixtures import application_archive
from test_veroscript_ide import vsix


class PackagingReviewTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix='vero packaging review ')
		self.addCleanup(temporary.cleanup)
		self.root = Path(temporary.name).resolve()
		(self.root / '.tmp').mkdir()

	def test_windows_interop_producer_stages_runtime_before_consumption(self):
		powershell = shutil.which('pwsh') or shutil.which('powershell')
		if not powershell:
			self.skipTest('PowerShell is required to execute the Windows producer segment')
		producer = Path(__file__).resolve().parents[1] / 'package-veroscript-all.ps1'
		script = self.root / 'interop-producer.ps1'
		script.write_text(r'''
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

param([string]$Producer, [string]$Stage)
$ErrorActionPreference = 'Stop'
$BuildRoot = Join-Path $Stage 'build'
$ReleaseRoot = Join-Path $Stage 'release'
$RepoRoot = $Stage
$ModuleRelative = '.'
$Config = 'Release'
$Runtime = 'win-x64'
$Versions = [pscustomobject]@{ Interop = '0.1.1' }
$CMakeVersionWriter = Join-Path $Stage 'version-writer.cmake'
$Calls = [Collections.Generic.List[string]]::new()
function Invoke-Checked([string]$Command, [string[]]$Arguments)
{
	if ($Arguments -contains '--install')
	{
		$Prefix = $Arguments[[Array]::IndexOf($Arguments, '--prefix') + 1]
		$Directory = Join-Path $Prefix 'lib/cmake/VeroScriptInterop'
		New-Item -ItemType Directory -Path $Directory -Force | Out-Null
		[IO.File]::WriteAllText((Join-Path $Directory 'VeroScriptInteropConfig.cmake'), 'set(VeroScriptInterop_VERSION "0.1.1")')
	}
}
function Copy-VeroScriptWindowsRuntime([string]$Build, [string]$Destination, [string]$Configuration)
{
	if ($Build -ne $BuildRoot -or $Configuration -ne 'Release')
	{
		throw 'Wrong runtime staging arguments'
	}
	$Notice = Join-Path $Destination 'licenses/MicrosoftVisualCpp'
	New-Item -ItemType Directory -Path $Notice -Force | Out-Null
	foreach ($Name in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'licenses/MicrosoftVisualCpp/NOTICE.txt'))
	{
		[IO.File]::WriteAllText((Join-Path $Destination $Name), 'runtime fixture')
	}
	$Calls.Add('runtime')
}
function Copy-LicenseFiles([string]$Directory) {}
function ConvertTo-VeroScriptPortableAssetTree([string]$Directory, [string]$SourceRoot) {}
function Test-SdkConsumer([string]$Directory, [string]$Sample, [string]$Name)
{
	foreach ($File in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'licenses/MicrosoftVisualCpp/NOTICE.txt'))
	{
		if (-not (Test-Path -LiteralPath (Join-Path $Directory "bin/$File") -PathType Leaf))
		{
			throw "Interop SDK deployment dependency missing before consumption: $File"
		}
	}
	$Calls.Add('consumer')
}
function New-PackageZip([string]$Directory, [string]$Archive)
{
	$Calls.Add('archive')
}
# Execute the production segment while replacing external tools and inputs.
$Source = [IO.File]::ReadAllText($Producer)
$Start = $Source.IndexOf('# Stable C ABI SDK.')
$End = $Source.IndexOf('# WebAssembly SDK:', $Start)
if ($Start -lt 0 -or $End -lt $Start)
{
	throw 'Interop producer segment is absent'
}
& ([ScriptBlock]::Create($Source.Substring($Start, $End - $Start)))
if (($Calls -join ',') -cne 'runtime,consumer,archive')
{
	throw 'Runtime staging must precede consumer execution and archive creation'
}
''', encoding='utf-8', newline='\n')
		result = subprocess.run([powershell, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(script),
			'-Producer', str(producer), '-Stage', str(self.root)], capture_output=True, encoding='utf-8', errors='replace')
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_archiving_releases_only_the_private_root_copy(self):
		path = Path(__file__).resolve().parents[1] / 'package-veroscript-all.py'
		spec = importlib.util.spec_from_file_location('veroscript_archive_storage_review', path)
		producer = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(producer)
		for scenario in ('copied', 'original-root', 'failed'):
			with self.subTest(scenario=scenario):
				work = self.root / scenario
				work.mkdir()
				release = producer.Release.__new__(producer.Release)
				release.work, release.root = work, work / 'release'
				release.versions, release.target = {'CoreSDK': '0.1.1'}, 'linux-x64'
				release.licenses = lambda root: None
				(release.root / 'sdk').mkdir(parents=True)
				portable = work / 'VeroScriptSDK-0.1.1-linux-x64'
				source = portable if scenario == 'original-root' else work / 'input'
				source.mkdir()
				(source / 'LICENSE').write_text('MIT\n')
				(source / 'library.bin').write_bytes(b'functional payload')
				if scenario == 'failed':
					with patch.object(producer, 'archive_tree', side_effect=OSError('archive write failed')):
						with self.assertRaisesRegex(OSError, 'archive write failed'):
							release.archive('sdk', 'VeroScriptSDK', source)
				else:
					release.archive('sdk', 'VeroScriptSDK', source)
					archive = release.root / 'sdk/VeroScriptSDK-0.1.1-linux-x64.tar.gz'
					entries = archive_entries(archive)
					self.assertEqual(entries['VeroScriptSDK-0.1.1-linux-x64/library.bin'][0], b'functional payload')
				self.assertEqual((source / 'library.bin').read_bytes(), b'functional payload')
				self.assertEqual(portable.exists(), scenario == 'original-root')

	def test_installer_releases_expanded_payload_only_after_successful_pack(self):
		class PublishBoundary(Exception):
			pass
		tools = Path(__file__).resolve().parents[1]
		for filename, class_name in (('package-veroscript-all.py', 'Release'),
				('package-veroscript-desktop-cross.py', 'DesktopCrossRelease')):
			for fail_pack in (False, True):
				with self.subTest(producer=filename, fail_pack=fail_pack):
					spec = importlib.util.spec_from_file_location('installer_storage_review', tools / filename)
					producer = importlib.util.module_from_spec(spec)
					spec.loader.exec_module(producer)
					work = self.root / (class_name + str(fail_pack))
					work.mkdir()
					component = work / 'original-sdk'
					component.mkdir()
					(component / 'library').write_bytes(b'preserved source payload')
					release = getattr(producer, class_name).__new__(getattr(producer, class_name))
					release.repo, release.work = tools.parents[2], work
					release.versions = {'CoreSDK': '0.1.1'}
					release.target, release.platform, release.arch = 'macos-x64', 'macos', 'x64'
					bundle = work / ('core-sdk-bundle' if class_name == 'Release' else 'CoreSDK-bundle')
					def run(*arguments, **kwargs):
						command = list(map(str, arguments))
						if 'build-bundle' in command:
							bundle.mkdir()
							(bundle / 'copied-library').write_bytes((component / 'library').read_bytes())
						elif 'pack-bundle' in command:
							self.assertTrue(bundle.is_dir())
							if fail_pack:
								raise OSError('pack failed')
							Path(command[command.index('--output') + 1]).write_bytes(b'verified ZIP')
						elif 'publish' in command:
							self.assertFalse(bundle.exists(), 'Publish must not retain a second expanded payload')
							self.assertEqual((component / 'library').read_bytes(), b'preserved source payload')
							raise PublishBoundary()
					with patch.object(producer, 'run', side_effect=run):
						with self.assertRaisesRegex(OSError, 'pack failed') if fail_pack else self.assertRaises(PublishBoundary):
							if class_name == 'Release':
								release.installer('core-sdk', 'CoreSDK', {'sdk': component})
							else:
								release.installer('CoreSDK', 'core-sdk', {'sdk': component})
					self.assertEqual(bundle.exists(), fail_pack)
					self.assertTrue(component.is_dir())

	def test_optional_sdk_components_follow_standard_cmake_boolean_spellings(self):
		build = self.root / 'build'
		build.mkdir()
		for enabled in ('ON', 'TRUE', '1', 'YES', 'Y', 'yes'):
			with self.subTest(enabled=enabled):
				(build / 'CMakeCache.txt').write_text(''.join(f'VEROSCRIPT_BUILD_{name}:BOOL={enabled}\n'
					for name in ('APPLICATION', 'PACKAGE_RUNTIME', 'PACKAGE_MANAGER', 'BUILD_SYSTEM', 'VM_BACKEND')))
				with patch('subprocess.run') as run:
					stage_package_manager_sdk(build, self.root / 'stage')
					self.assertEqual([call.args[0][-1] for call in run.call_args_list], ['VeroScriptApplication',
						'VeroScriptPackageRuntime', 'VeroScriptApplicationPackages', 'VeroScriptBuild', 'VeroScriptPackageManager'])
				with patch('subprocess.run') as run:
					install(build, self.root / 'vm', 'VeroScriptRuntimeVm', 'Release')
					self.assertEqual(run.call_count, 1)
		for disabled in ('OFF', 'FALSE', '0', 'NO', 'N'):
			with self.subTest(disabled=disabled):
				(build / 'CMakeCache.txt').write_text(''.join(f'VEROSCRIPT_BUILD_{name}:BOOL={disabled}\n'
					for name in ('APPLICATION', 'PACKAGE_RUNTIME', 'PACKAGE_MANAGER', 'BUILD_SYSTEM')))
				with patch('subprocess.run') as run:
					stage_package_manager_sdk(build, self.root / 'stage')
					self.assertFalse(run.called)

	def test_csharp_distributions_and_offline_feeds_share_one_complete_inventory(self):
		canonical = self.root / 'canonical'
		canonical.mkdir()
		for package_id in PACKAGE_IDS:
			for suffix in ('nupkg', 'snupkg'):
				with zipfile.ZipFile(canonical / f'{package_id}.0.1.1.{suffix}', 'w') as archive:
					archive.writestr(package_id + '.nuspec', '<package><metadata><id>' + package_id
						+ '</id><version>0.1.1</version></metadata></package>')
					archive.writestr('content.bin', (package_id + suffix).encode())
		runtime, tools, output = (self.root / name for name in ('runtime', 'tools', 'packages'))
		records = stage_csharp_packages(self.root / 'unused-build', runtime, tools, output, '0.1.1', canonical)
		self.assertEqual(len(records), 12)
		for name, checksum in records.items():
			feed = runtime if name.startswith('VeroScript.CSharp.0.') else tools
			self.assertEqual(digest(output / name), checksum)
			self.assertEqual(digest(feed / 'share/VeroScript/nuget' / name), checksum)
		missing = canonical / 'VeroScript.CSharpBindGen.Tool.0.1.1.snupkg'
		missing.unlink()
		untouched = self.root / 'must-stay-absent'
		with self.assertRaisesRegex(ValueError, 'Missing canonical C# package'):
			stage_csharp_packages(self.root, untouched / 'runtime', untouched / 'tools', untouched / 'out', '0.1.1', canonical)
		self.assertFalse(untouched.exists())

	def runtime(self):
		source = self.root / 'RuntimeFixture'
		source.mkdir()
		files = ('veroscript-runtime', 'veroscript-launcher', 'LICENSE', 'Vm/libVeroScriptVmBytecode.so',
			'stdlib/core.vs', 'stdlib/string.vs', 'localization/en-US/shared.json',
			'licenses/application/json/LICENSE.MIT', 'licenses/application/miniz/LICENSE', 'licenses/application/sha256/LICENSE')
		for name in files:
			file = source / name
			file.parent.mkdir(parents=True, exist_ok=True)
			file.write_bytes(b'functional fixture')
		(source / 'runtime-manifest.json').write_text('{"application":true}')
		receipt = {'format': 'veroscript-runtime', 'formatVersion': '0.1.1', 'applicationProtocol': '0.1.1',
			'target': {'os': 'linux', 'arch': 'x64', 'environment': 'gnu', 'core': '0.1.1', 'coreBuild': 'a'*64,
				'abis': {'vm': 'abi'}, 'minimumVersions': {}, 'capabilities': []},
			'executable': 'veroscript-runtime', 'stdlib': ['stdlib'], 'extensions': {}, 'files': []}
		for name in files:
			if name == 'veroscript-launcher':
				continue
			data = (source / name).read_bytes()
			receipt['files'].append({'path': name, 'bytes': str(len(data)), 'sha256': hashlib.sha256(data).hexdigest(),
				'mode': '0755' if name == 'veroscript-runtime' else '0644'})
		return source, receipt

	def test_application_and_package_runtime_require_their_joint_sdk_adapter(self):
		prefix = 'SDK/'
		entries = {prefix + 'sdk-manifest.json': (b'{"packageRuntime":true,"buildSystem":true}', 0o644)}
		for feature, name, header, libraries in (
			('application', 'VeroScriptApplication', 'Application/VeroScriptApplication.h', ('VeroScriptApplication', 'VeroScriptApplicationZip')),
			('package-runtime', 'VeroScriptPackageRuntime', 'PackageManager/VeroScriptPackageManager.h', ('VeroScriptPackageRuntime', 'VeroScriptPackageZip')),
			('build', 'VeroScriptBuild', 'Build/VeroScriptBuild.h', ('VeroScriptBuild',))):
			entries[prefix + 'include/' + header] = (b'public API', 0o644)
			for library in libraries:
				entries[prefix + 'lib/lib' + library + '.a'] = (b'static library', 0o644)
			folder = 'lib/cmake/' + name + '/' + name
			for suffix in ('Config.cmake', 'ConfigVersion.cmake', 'Targets.cmake'):
				entries[prefix + folder + suffix] = (b'find_dependency(VeroScript 2.3.4 EXACT CONFIG)', 0o644)
			if feature != 'build':
				entries[prefix + folder + 'CoreBuild.cmake'] = (('set(' + name + '_CORE_BUILD_ID "' + 'a'*64 + '")').encode(), 0o644)
			for notice in (('json/LICENSE.MIT', 'sources.json') if feature == 'build' else
				('json/LICENSE.MIT', 'miniz/LICENSE', 'sha256/LICENSE', 'sources.json')):
				entries[prefix + 'licenses/' + feature + '/' + notice] = (b'legal notice', 0o644)
		adapter = 'lib/cmake/VeroScriptApplicationPackages/VeroScriptApplicationPackages'
		required = ('include/Application/VeroScriptApplicationPackages.h', 'lib/libVeroScriptApplicationPackages.a',
			adapter + 'Config.cmake', adapter + 'ConfigVersion.cmake', adapter + 'Targets.cmake')
		for name in required:
			entries[prefix + name] = (b'adapter export', 0o644)
		def verify(payload):
			verify_application_payload(self.root / 'unused.zip', payload, prefix, 'core-sdk', ('linux', 'x64'), '2.3.4', 'a'*64, '0.1.1')
		verify(entries)
		for missing in required:
			with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, 'Missing application deployment payload'):
				verify({name: value for name, value in entries.items() if name != prefix + missing})

	def test_runtime_receipts_require_native_file_contract_and_complete_resources(self):
		source, original = self.runtime()
		cases = ['valid', 'missing-mode', 'invalid-mode', 'non-executable', 'missing-extensions',
			'omitted-stdlib', 'omitted-localization', 'omitted-notice', 'case-collision', 'missing-target-fields']
		for suffix in ('.zip', '.tar.gz'):
			for case in cases:
				with self.subTest(suffix=suffix, case=case):
					receipt = copy.deepcopy(original)
					if case == 'missing-mode':
						receipt['files'][0].pop('mode')
					elif case == 'invalid-mode':
						receipt['files'][0]['mode'] = '0777'
					elif case == 'non-executable':
						receipt['files'][0]['mode'] = '0644'
					elif case == 'missing-extensions':
						receipt.pop('extensions')
					elif case == 'missing-target-fields':
						receipt['target'].pop('capabilities')
					elif case.startswith('omitted-'):
						missing = {'omitted-stdlib': 'stdlib/string.vs', 'omitted-localization': 'localization/en-US/shared.json',
							'omitted-notice': 'licenses/application/miniz/LICENSE'}[case]
						receipt['files'] = [item for item in receipt['files'] if item['path'] != missing]
					elif case == 'case-collision':
						receipt['files'].append({**receipt['files'][0], 'path': 'VEROSCRIPT-RUNTIME'})
					(source / 'veroscript-runtime.json').write_text(json.dumps(receipt))
					archive = self.root / ('RuntimeFixture' + suffix)
					application_archive(source, archive, 'veroscript-runtime')
					arguments = (archive, archive_entries(archive), 'RuntimeFixture/', 'runtime', ('linux', 'x64'), None, 'a'*64, '0.1.1')
					if case == 'valid':
						verify_application_payload(*arguments)
					else:
						with self.assertRaisesRegex(ValueError, 'receipt|runtime target'):
							verify_application_payload(*arguments)

	def test_ide_tool_preflight_does_not_build_or_invoke_the_verifier(self):
		ide = self.root / 'rider'
		ide.mkdir()
		(ide / 'product-info.json').write_text('{"productCode":"RD","version":"2026.2.2"}')
		source = self.root / IDE_PRODUCTS['GuiJetBrains']['source']
		source.mkdir(parents=True)
		(source / 'gradle.properties').write_text('platformVersion=2026.2.2\n')
		jar = self.root / 'verifier-all.jar'
		jar.write_bytes(b'fixture')
		java = self.root / 'jdk/bin' / ('java.exe' if sys.platform == 'win32' else 'java')
		java.parent.mkdir(parents=True)
		java.write_bytes(b'fixture')
		env = {'VEROSCRIPT_JETBRAINS_IDE_PATH': str(ide), 'VEROSCRIPT_JETBRAINS_VERIFIER_JAR': str(jar),
			'JAVA_HOME': str(java.parent.parent)}
		with patch('veroscript_ide.module_root', return_value=self.root), patch('veroscript_ide.shutil.which', return_value=None), patch('subprocess.run') as run:
			check_ide_prerequisites(self.root, ['Runtime'], env)
			check_ide_prerequisites(self.root, ['GuiJetBrains'], env)
			with self.assertRaisesRegex(ValueError, 'Node.js and npm'):
				check_ide_prerequisites(self.root, ['GuiVSCode'], env)
			(ide / 'product-info.json').write_text('{"productCode":"RD","version":"2025.1"}')
			with self.assertRaisesRegex(ValueError, 'matching Rider'):
				check_ide_prerequisites(self.root, ['GuiJetBrains'], env)
			self.assertFalse(run.called)

	def candidate(self, base_version, gui_version=None, *, base=True, pin=None, extra=None):
		candidate = Path(tempfile.mkdtemp(dir=self.root / '.tmp'))
		versions = {name: '0.1.1' for name in ALL_BUSINESSES}
		versions.update(VSCode=base_version, GuiVSCode=gui_version or '2.0.0')
		identities = {}
		selected = []
		if base:
			file = candidate / 'ide' / f'VeroScriptVSCode-{base_version}.vsix'
			vsix(file, base_version, extra)
			identities['VSCode'] = digest(file)
			selected.append('VSCode')
		if gui_version:
			file = candidate / 'ide' / f'VeroScriptGuiVSCode-{gui_version}.vsix'
			vsix(file, gui_version, {'extension/package.json': json.dumps({'name': 'veroscript-gui',
				'publisher': 'VanstySanic', 'version': gui_version, 'main': './dist/extension.cjs',
				'extensionDependencies': ['VanstySanic.veroscript']}), 'extension.vsixmanifest':
				f'<PackageManifest><Metadata><Identity Id="veroscript-gui" Publisher="VanstySanic" Version="{gui_version}"/></Metadata></PackageManifest>'})
			identities['GuiVSCode'] = digest(file)
			selected.append('GuiVSCode')
		write_manifest(candidate, '0.1.1', 'win-x64', next(iter(identities.values())), business_versions=versions)
		path = candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest.update(selectedBusinesses=selected, businessBuildIds=identities)
		if gui_version:
			manifest['ideDependencies'] = {'GuiVSCode': {'VSCode': pin or {'version': base_version, 'buildId': identities['VSCode']}}}
		path.write_text(json.dumps(manifest))
		return candidate, identities

	def test_independent_gui_publication_retains_then_prunes_its_verified_base(self):
		collection = self.root / '.tmp/collection'
		original = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda path, repo=None, **kwargs: original(path, **kwargs)):
			candidate, identities = self.candidate('1.0.0', '2.0.0')
			publish_businesses(self.root, candidate, collection)
			index = collection / 'indexes/win-x64.json'
			before = index.read_bytes()
			invalid, _ = self.candidate('1.0.0', '2.1.0', base=False, pin={'version': '1.0.0', 'buildId': 'f'*64})
			with self.assertRaisesRegex(ValueError, 'GUI IDE base dependency|Business dependency version/build mismatch|Shared artifact identity differs'):
				publish_businesses(self.root, invalid, collection)
			self.assertEqual(index.read_bytes(), before)
			replacement, _ = self.candidate('1.0.0', extra={'extension/dist/extension.cjs': 'changed base'})
			with self.assertRaisesRegex(ValueError, 'GUI IDE base dependency|Business dependency version/build mismatch|Shared artifact identity differs'):
				publish_businesses(self.root, replacement, collection)
			self.assertEqual(index.read_bytes(), before)
			new_base, new_identities = self.candidate('1.1.0')
			publish_businesses(self.root, new_base, collection)
			self.assertTrue((business_root(collection, 'VSCode', 'any') / '1.0.0').is_dir())
			verify_collection(collection, 'win-x64')
			new_gui, _ = self.candidate('1.1.0', '2.1.0', base=False, pin={'version': '1.1.0', 'buildId': new_identities['VSCode']})
			publish_businesses(self.root, new_gui, collection)
			self.assertFalse((business_root(collection, 'VSCode', 'any') / '1.0.0').exists())
			self.assertEqual(set(verify_collection(collection, 'win-x64')['businesses']), {'VSCode', 'GuiVSCode'})

	def test_powershell_digest_spelling_is_canonicalized_without_accepting_wrong_bytes(self):
		collection = self.root / '.tmp/hash-collection'
		candidate, _ = self.candidate('1.0.0')
		file = candidate / 'release-manifest.json'
		manifest = json.loads(file.read_text())
		for item in manifest['artifacts']:
			item['sha256'] = item['sha256'].upper()
		file.write_text(json.dumps(manifest))
		(candidate / 'checksums.txt').write_text(''.join(f"{item['sha256']}  {item['path']}\n" for item in manifest['artifacts']))
		original = verify_release
		with patch('veroscript_release.verify_release', side_effect=lambda path, repo=None, **kwargs: original(path, **kwargs)):
			publish_businesses(self.root, candidate, collection)
			verify_collection(collection, 'win-x64')
			published = json.loads((business_root(collection, 'VSCode', 'any') / '1.0.0/release-manifest.json').read_text())
			self.assertTrue(all(item['sha256'] == item['sha256'].lower() for item in published['artifacts']))
			index = collection / 'release-index.json'
			before = index.read_bytes()
			artifact = candidate / manifest['artifacts'][0]['path']
			artifact.write_bytes(artifact.read_bytes() + b'changed after receipt')
			with self.assertRaises(ValueError):
				publish_businesses(self.root, candidate, collection)
			self.assertEqual(index.read_bytes(), before)

	def test_common_dotnet_tool_and_symbols_declare_their_native_hosts(self):
		from veroscript_business_release import artifact_support
		from veroscript_products import NATIVE_TARGETS
		for extension, package_type, member in (
			('.nupkg', 'DotnetTool', 'DotnetToolSettings.xml'),
			('.snupkg', 'SymbolsPackage', 'Tool.pdb'),
			('.nupkg', 'Dependency', 'Tool.dll')):
			with self.subTest(extension=extension, package_type=package_type):
				path = self.root / ('VeroScript.CSharpBindGen.Tool.0.1.1' + extension)
				with zipfile.ZipFile(path, 'w') as archive:
					archive.writestr('Tool.nuspec', '<package><metadata><packageTypes><packageType name="' + package_type + '"/></packageTypes></metadata></package>')
					archive.writestr('tools/net8.0/any/' + member, 'tool payload')
				if package_type == 'Dependency':
					with self.assertRaisesRegex(ValueError, 'no declared supported framework'):
						artifact_support(path, path.name, 'CSharp', 'any')
				else:
					self.assertEqual(artifact_support(path, path.name, 'CSharp', 'any'), (sorted(NATIVE_TARGETS), sorted(NATIVE_TARGETS)))

	def test_full_csharp_plan_lists_the_complete_shared_inventory_once(self):
		from veroscript_release_plan import release_plan
		config = {'businesses': {name: '0.1.1' for name in ALL_BUSINESSES}, 'nativeVersion': '0.1.1'}
		plan = release_plan(self.root, config, ['win-x64', 'linux-x64', 'android', 'ios'], ['CSharp'])
		common = plan['directories']['Release']['Extensions']['Language']['CSharp']['any']['0.1.1']
		self.assertEqual(len(common['packages']), 6)
		self.assertEqual(len(common['symbols']), 6)
		self.assertFalse(plan['optionalArtifacts'])

	def test_module_descriptors_are_not_conflicting_python_distributions(self):
		from veroscript_business_release import _collection_inventory
		collection = self.root / '.tmp/descriptors'
		collection.mkdir()
		(collection / 'release-index.json').write_text('{}')
		manifests = {}
		for business in ('Jit', 'Vm'):
			leaf = business_root(collection, business, 'linux-x64') / '0.1.1'
			leaf.mkdir(parents=True)
			for filename in ('release-manifest.json', 'checksums.txt', 'veroscript-package.json'):
				(leaf / filename).write_text(business)
			manifests[leaf] = {'business': business, 'artifacts': [{'path': 'veroscript-package.json', 'sha256': business}]}
		with patch('veroscript_business_release.read_business', side_effect=lambda leaf, target: manifests[leaf]), patch('veroscript_business_release.verify_business_inventory'):
			self.assertEqual(len(_collection_inventory(collection)), 2)

	def test_gui_candidate_rejects_missing_or_mismatched_base_acceptance(self):
		candidate, _ = self.candidate('1.0.0', '2.0.0')
		file = candidate / 'release-manifest.json'
		original = json.loads(file.read_text())
		for field in ('missing', 'version', 'buildId'):
			with self.subTest(field=field):
				manifest = copy.deepcopy(original)
				if field == 'missing':
					manifest.pop('ideDependencies')
				else:
					manifest['ideDependencies']['GuiVSCode']['VSCode'][field] = '9.9.9' if field == 'version' else 'f'*64
				file.write_text(json.dumps(manifest))
				with self.assertRaisesRegex(ValueError, 'GUI IDE'):
					verify_release(candidate)


if __name__ == '__main__':
	unittest.main()
