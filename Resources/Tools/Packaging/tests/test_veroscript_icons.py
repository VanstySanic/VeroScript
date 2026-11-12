# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Icon inputs, per-platform Runtime payloads and installer manifest closure."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_icons import (TARGETS, check_icon_sources, runtime_icon_files, source_icon_files,
							 stage_runtime_icons, verify_runtime_icons, verify_runtime_icon_entries, verify_runtime_bundle)
from veroscript_release import release_asset_files, verify_release, write_manifest

REPO = Path(__file__).resolve().parents[4]


def populate_icon_sources(root):
	(root / 'VeroScript/Public').mkdir(parents=True, exist_ok=True)
	(root / 'VeroScript/Public/VeroScriptVersion.h').write_text('// Source module marker\n')
	for relative, source in source_icon_files(REPO).items():
		destination = root / 'Resources/Assets' / relative
		destination.parent.mkdir(parents=True, exist_ok=True)
		shutil.copyfile(source, destination)
	generator = root / 'Resources/Tools/Assets/generate-icons.cjs'
	generator.parent.mkdir(parents=True, exist_ok=True)
	shutil.copyfile(REPO / generator.relative_to(root), generator)
	return root


class IconPackagingTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix='vero icon packaging ')
		self.addCleanup(temporary.cleanup)
		self.root = Path(temporary.name).resolve()

	def test_all_48_official_images_are_current_and_in_resources_inventory(self):
		self.assertEqual(48, len(check_icon_sources(REPO)))
		inventory = dict(release_asset_files(REPO))
		for path in source_icon_files(REPO).values():
			self.assertEqual(path.read_bytes(), inventory[path.relative_to(REPO).as_posix()].read_bytes())

	def test_runtime_payload_contains_only_platform_icons_and_matches_source(self):
		for target in TARGETS:
			with self.subTest(target=target):
				output = self.root / target
				stage_runtime_icons(REPO, output, target)
				files = runtime_icon_files(REPO, target)
				self.assertEqual(set(files), {p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file()})
				self.assertEqual(8 if target.startswith('linux-') else 4, len(files))
				verify_runtime_icons(REPO, output, target)
				entries = {'package/' + name: ((output / name).read_bytes(), 0o644) for name in files}
				verify_runtime_icon_entries(REPO, entries, 'package/', target)
				name = next(iter(files))
				entries['package/' + name] = (b'old icon', 0o644)
				with self.assertRaisesRegex(ValueError, 'Missing/stale Runtime icon'):
					verify_runtime_icon_entries(REPO, entries, 'package/', target)
				(output / name).write_bytes(b'old icon')
				with self.assertRaisesRegex(ValueError, 'Stale Runtime icon'):
					verify_runtime_icons(REPO, output, target)

	def test_missing_and_inconsistent_container_frames_fail_before_staging(self):
		repo = populate_icon_sources(self.root / 'Renamed Source')
		file = repo / 'Resources/Assets/icons/veroscript-icon-32.png'
		old = file.read_bytes()
		file.unlink()
		with self.assertRaisesRegex(ValueError, 'Missing, linked or invalid icon'):
			stage_runtime_icons(repo, self.root / 'output', 'win-x64')
		self.assertFalse((self.root / 'output').exists())
		file.write_bytes(old)
		container = repo / 'Resources/Assets/icons/veroscript-icon.ico'
		container.write_bytes(container.read_bytes()[:-1])
		with self.assertRaisesRegex(ValueError, 'Invalid official icon family'):
			check_icon_sources(repo)

	def test_installer_bundle_must_inventory_current_icons_at_their_installed_paths(self):
		bundle = self.root / 'bundle'
		payload = bundle / 'payload/runtime'
		stage_runtime_icons(REPO, payload, 'win-x64')
		files = [{'path': name, 'size': source.stat().st_size, 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
				 for name, source in runtime_icon_files(REPO, 'win-x64').items()]
		manifest = {'platform': 'windows', 'architecture': 'x64', 'components': [
			{'id': 'runtime', 'payloadDirectory': 'payload/runtime', 'files': files}]}
		path = bundle / 'manifest.json'
		path.write_text(json.dumps(manifest))
		verify_runtime_bundle(REPO, bundle, 'win-x64')
		files.pop()
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'does not inventory its icon'):
			verify_runtime_bundle(REPO, bundle, 'win-x64')
		manifest['components'][0]['payloadDirectory'] = '../outside'
		path.write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, 'Unsafe Runtime bundle'):
			verify_runtime_bundle(REPO, bundle, 'win-x64')

	def test_staging_rejects_unowned_content_and_source_overlap(self):
		output = self.root / 'output'
		icons = output / 'assets/icons'
		icons.mkdir(parents=True)
		(icons / 'user.txt').write_text('keep')
		with self.assertRaisesRegex(ValueError, 'unexpected files'):
			stage_runtime_icons(REPO, output, 'win-x64')
		self.assertEqual('keep', (icons / 'user.txt').read_text())
		with self.assertRaisesRegex(ValueError, 'overlaps source assets'):
			stage_runtime_icons(REPO, REPO / 'Resources/Assets', 'win-x64')

	@unittest.skipUnless(sys.platform in ('linux', 'darwin') and shutil.which('bash'), 'Requires a Unix shell')
	def test_unix_installer_checks_bundle_icons_with_a_linked_temp_root(self):
		import platform
		repo = populate_icon_sources(self.root / 'checkout')
		tools = repo / 'Resources/Tools/Packaging'
		tools.mkdir(parents=True)
		for file in (REPO / 'Resources/Tools/Packaging').glob('*.py'):
			shutil.copyfile(file, tools / file.name)
		for name in ('package-veroscript-installer.sh', 'ReleaseVersions.json'):
			shutil.copyfile(REPO / 'Resources/Tools/Packaging' / name, tools / name)
		(repo / 'VeroScriptInstaller').mkdir()
		arch = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x64'
		platform_name = 'macos' if sys.platform == 'darwin' else 'linux'
		target = platform_name + '-' + arch
		runtime = repo / '.tmp/runtime'
		stage_runtime_icons(repo, runtime, target)
		version = json.loads((tools / 'ReleaseVersions.json').read_text(encoding='utf-8'))['nativeVersion']
		cli = runtime / 'veroscript'
		cli.write_text(f"#!/bin/sh\nprintf '%s\\n' 'VeroScript {version} [{target}; fixture]'\n", encoding='utf-8')
		cli.chmod(0o755)
		temporary = self.root / 'real temporary directory'
		temporary.mkdir()
		(temporary / 'keep.txt').write_text('keep', encoding='utf-8')
		alias = self.root / 'temporary alias'
		alias.symlink_to(temporary, target_is_directory=True)
		bin_root = self.root / 'bin'
		bin_root.mkdir()
		(bin_root / 'python3').symlink_to(sys.executable)
		stub = bin_root / 'dotnet.py'
		stub.write_text('''import hashlib, json, os, shutil, sys
from pathlib import Path
arguments = sys.argv[1:]
if 'build-bundle' in arguments:
    root = Path(arguments[arguments.index('--output') + 1])
    source = Path(arguments[arguments.index('--source-runtime') + 1])
    payload = root / 'payload/runtime'
    shutil.copytree(source, payload)
    files = [{'path': file.relative_to(source).as_posix(), 'size': file.stat().st_size,
              'sha256': hashlib.sha256(file.read_bytes()).hexdigest()}
             for file in source.rglob('*') if file.is_file()]
    manifest = {'platform': os.environ['ICON_TEST_PLATFORM'], 'architecture': os.environ['ICON_TEST_ARCH'],
                'components': [{'id': 'runtime', 'payloadDirectory': 'payload/runtime', 'files': files}]}
    (root / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
elif 'verify' in arguments:
    Path(os.environ['ICON_TEST_RECEIPT']).write_text(str(Path(os.environ['DOTNET_BUNDLE_EXTRACT_BASE_DIR']).parent.parent), encoding='utf-8')
    print('EXPECTED_AFTER_ICON_CHECK')
    raise SystemExit(73)
else:
    raise SystemExit(74)
''', encoding='utf-8')
		shim = bin_root / 'dotnet'
		shim.write_text('#!/bin/sh\nexec "$ICON_TEST_PYTHON" "$ICON_TEST_DOTNET" "$@"\n', encoding='utf-8')
		shim.chmod(0o755)
		receipt = self.root / 'receipt.txt'
		env = os.environ | {'PATH': str(bin_root) + os.pathsep + os.environ.get('PATH', ''), 'TMPDIR': str(alias),
			'ICON_TEST_PYTHON': sys.executable, 'ICON_TEST_DOTNET': str(stub), 'ICON_TEST_RECEIPT': str(receipt),
			'ICON_TEST_PLATFORM': platform_name, 'ICON_TEST_ARCH': arch}
		result = subprocess.run(['bash', str(tools / 'package-veroscript-installer.sh'),
			'--runtime', ('osx-' if sys.platform == 'darwin' else 'linux-') + arch,
			'--runtime-package-dir', str(runtime), '--out-root', str(repo / '.tmp/output')],
			env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
		self.assertEqual(73, result.returncode, result.stdout + result.stderr)
		self.assertIn('EXPECTED_AFTER_ICON_CHECK', result.stdout)
		self.assertEqual(str(temporary), receipt.read_text(encoding='utf-8'))
		self.assertEqual([temporary / 'keep.txt'], list(temporary.iterdir()))

	def test_source_audit_rejects_stale_runtime_icons_even_after_archive_rehash(self):
		from test_veroscript_release import elf, fixture_archive
		from installer_fixtures import installer_apphost
		repo = populate_icon_sources(self.root / 'checkout')
		candidate = repo / '.tmp/candidate'
		source = repo / '.tmp/VeroScriptRuntime-0.1.1-linux-x64'
		source.mkdir(parents=True)
		for tool in ('veroscript', 'veroscript-lsp', 'veroscript-dap'):
			(source / tool).write_bytes(elf())
		(source / 'Vm').mkdir()
		(source / 'Vm/libVeroScriptVmBytecode.so').write_bytes(elf())
		for name, data in {'lib/libVeroScriptInterop.so': elf(), 'include/VeroScript/VeroScriptInterop.h': b'header',
						   'stdlib/core.vs': b'core', 'localization/en-US/shared.json': b'{}'}.items():
			file = source / name
			file.parent.mkdir(parents=True, exist_ok=True)
			file.write_bytes(data)
		(source / 'LICENSE').write_bytes((REPO / 'LICENSE').read_bytes())
		archive = candidate / 'runtime' / (source.name + '.tar.gz')
		installer = candidate / 'installer/VeroScriptRuntimeInstaller-0.1.1-linux-x64'
		installer.parent.mkdir(parents=True)
		installer.write_bytes(installer_apphost(elf()))
		installer.chmod(0o755)
		fixture_archive(installer, installer.with_name(installer.name + '.tar.gz'))
		installer.unlink()
		def refresh():
			fixture_archive(source, archive)
			write_manifest(candidate, '0.1.1', 'linux-x64', hashlib.sha256(b'core').hexdigest(), business_versions={'Runtime': '0.1.1'})
		refresh()
		verify_release(candidate, businesses={'Runtime'})  # Historical recorded contract.
		with self.assertRaisesRegex(ValueError, 'Missing/stale Runtime icon'):
			verify_release(candidate, repo, businesses={'Runtime'})
		stage_runtime_icons(repo, source, 'linux-x64')
		refresh()
		verify_release(candidate, repo, businesses={'Runtime'})
		(source / 'assets/icons/veroscript-source.svg').write_bytes(b'old source icon')
		refresh()
		with self.assertRaisesRegex(ValueError, 'Missing/stale Runtime icon'):
			verify_release(candidate, repo, businesses={'Runtime'})

	def test_source_audit_rejects_stale_sdk_executable_icons_after_archive_rehash(self):
		from test_veroscript_executable_icons import pe_with_icon
		repo = populate_icon_sources(self.root / 'checkout')
		candidate = repo / '.tmp/candidate'
		archive = candidate / 'sdk/VeroScriptSDK-0.1.1-win-arm64.zip'
		archive.parent.mkdir(parents=True)
		stale = REPO / 'Resources/Assets/file-associations/icons/veroscript-source.ico'
		with zipfile.ZipFile(archive, 'w') as output:
			output.writestr('VeroScriptSDK-0.1.1-win-arm64/bin/veroscript.exe', pe_with_icon(stale, 'arm64'))
		write_manifest(candidate, '0.1.1', 'win-arm64', 'a' * 64, business_versions={'CoreSDK': '0.1.1'})
		with self.assertRaisesRegex(ValueError, 'Missing/stale Windows executable icon'):
			verify_release(candidate, repo, businesses={'CoreSDK'}, partial_businesses={'CoreSDK'})

	@unittest.skipUnless(sys.platform == 'win32', 'PowerShell adapter requires Windows')
	def test_powershell_adapter_stages_the_same_runtime_files(self):
		helper = str(REPO / 'Resources/Tools/Packaging/veroscript-assets.ps1').replace("'", "''")
		output = self.root / 'runtime with spaces'
		repo_arg = str(REPO).replace("'", "''")
		output_arg = str(output).replace("'", "''")
		python_arg = sys.executable.replace("'", "''")
		subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
			f"$ErrorActionPreference = 'Stop'; . '{helper}'; Invoke-VeroScriptIcons "
			f"@('stage-runtime', '--repo-root', '{repo_arg}', '--root', '{output_arg}', '--target', 'win-x64') -PythonExecutable '{python_arg}'"],
			check=True, capture_output=True)
		verify_runtime_icons(REPO, output, 'win-x64')

	@unittest.skipUnless(shutil.which('git'), 'Git is required')
	def test_ci_requires_every_icon_and_generator_to_be_tracked(self):
		repo = populate_icon_sources(self.root / 'Source Checkout')
		subprocess.run(['git', 'init', '-q', str(repo)], check=True)
		with self.assertRaisesRegex(ValueError, 'must be tracked'):
			check_icon_sources(repo, require_tracked=True)
		subprocess.run(['git', '-C', str(repo), 'add', '--', 'Resources/Assets', 'Resources/Tools/Assets'], check=True, capture_output=True)
		self.assertEqual(48, len(check_icon_sources(repo, require_tracked=True)))

	def test_sources_and_destinations_cannot_be_links(self):
		repo = populate_icon_sources(self.root / 'Source')
		file = repo / 'Resources/Assets/icons/veroscript-icon.svg'
		original = self.root / 'real.svg'
		file.rename(original)
		try:
			file.symlink_to(original)
		except OSError:
			self.skipTest('Symbolic links require permission on this host')
		with self.assertRaisesRegex(ValueError, 'linked or invalid icon'):
			check_icon_sources(repo)
		file.unlink()
		shutil.copyfile(original, file)
		output = self.root / 'output'
		output.mkdir()
		(output / 'assets').symlink_to(repo / 'Resources/Assets', target_is_directory=True)
		with self.assertRaisesRegex(ValueError, 'contains a link'):
			stage_runtime_icons(repo, output, 'win-x64')

	def test_windows_publisher_supplies_the_runtime_component_source(self):
		source = (REPO / 'Resources/Tools/Packaging/package-veroscript-installer.ps1').read_text(encoding='utf-8-sig')
		arguments = source.split('$ToolArguments = @(', 1)[1].split('\n\t)', 1)[0]
		self.assertIn('"--source-runtime", $RuntimePackageDir', arguments)

	@unittest.skipUnless(os.name == 'nt' and shutil.which('dotnet'), 'Windows and a .NET SDK are required for the installer entry test')
	def test_windows_installer_identity_guard_reaches_build_with_current_icons(self):
		# A tiny executable tests the actual entry script's version/target checks.
		# The PowerShell fixture stops before any installer build or publication.
		sdks = subprocess.run(['dotnet', '--list-sdks'], check=True, capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
		majors = [int(value) for value in re.findall(r'^(\d+)\.', sdks, re.M) if int(value) >= 8]
		if not majors:
			self.skipTest('.NET SDK 8 or newer is required')
		project = self.root / 'version cli fixture'
		project.mkdir()
		(project / 'VersionCli.csproj').write_text(
			'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType>'
			f'<TargetFramework>net{max(majors)}.0</TargetFramework>'
			f'<ApplicationIcon>{REPO.as_posix()}/Resources/Assets/icons/veroscript-icon.ico</ApplicationIcon>'
			'<AssemblyName>veroscript</AssemblyName></PropertyGroup></Project>')
		(project / 'Program.cs').write_text('System.Console.WriteLine("VeroScript 0.1.1 [win-" + '
			'System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant() + "; fixture]");')
		runtime = self.root / 'Runtime with spaces'
		subprocess.run(['dotnet', 'build', str(project / 'VersionCli.csproj'), '-c', 'Release', '-o', str(runtime), '--nologo'],
			check=True, capture_output=True, text=True, encoding='utf-8', errors='replace')
		identity = subprocess.run([str(runtime / 'veroscript.exe'), '--version'], check=True, capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
		target = re.search(r'\[(win-(?:x64|arm64|x86));', identity)[1]
		stage_runtime_icons(REPO, runtime, target)
		result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
			str(REPO / 'Resources/Tools/Packaging/tests/Test-VeroScriptInstallerIdentity.ps1'),
			'-RuntimePackageDir', str(runtime)], capture_output=True, text=True, encoding='utf-8', errors='replace')
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertIn('independent package versions accepted', result.stdout)

	def test_installer_compilation_uses_formal_icon_inputs(self):
		project = REPO / 'VeroScriptInstaller/App/VeroScriptInstaller.csproj'
		xml = ET.parse(project).getroot()
		icon = xml.find('.//ApplicationIcon').text.replace('\\', '/')
		self.assertEqual(REPO / 'Resources/Assets/icons/veroscript-icon.ico', (project.parent / icon).resolve())
		resources = [(project.parent / item.attrib['Include'].replace('\\', '/')).resolve() for item in xml.findall('.//AvaloniaResource')]
		self.assertIn(REPO / 'Resources/Assets/icons/veroscript-icon-256.png', resources)
		for name in ('package-veroscript-installer.ps1', 'package-veroscript-installer.sh',
					 'package-veroscript-sdk-installer.ps1', 'package-veroscript-product-installer.ps1'):
			source = (REPO / 'Resources/Tools/Packaging' / name).read_text(encoding='utf-8-sig')
			self.assertNotIn('--no-build', source)
			self.assertIn('installer_dotnet publish' if name.endswith('.sh') else "Invoke-VeroScriptInstallerDotNet $RepoRoot @('publish'", source)


if __name__ == '__main__':
	unittest.main()
