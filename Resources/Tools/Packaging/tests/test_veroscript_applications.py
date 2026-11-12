# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_applications import stage_application_sdk, stage_application_runtime, finalize_application_sdk
from veroscript_vm import stage_runtime_from_binaries

class ApplicationPackagingTests(unittest.TestCase):
	def test_readonly_runtime_sdk_does_not_require_manager_or_application(self):
		with tempfile.TemporaryDirectory() as tmp:
			root=Path(tmp)
			build=root/'build'
			build.mkdir()
			(build/'CMakeCache.txt').write_text('VEROSCRIPT_BUILD_APPLICATION:BOOL=OFF\nVEROSCRIPT_BUILD_PACKAGE_RUNTIME:BOOL=ON\n')
			with patch('veroscript_applications.subprocess.run') as run:
				stage_application_sdk(build, root/'sdk')
			self.assertEqual([call.args[0][-1] for call in run.call_args_list], ['VeroScriptPackageRuntime'])

	def test_runtime_component_contains_only_declared_runtime_content(self):
		with tempfile.TemporaryDirectory() as tmp:
			root=Path(tmp)
			repo=root/'repo'
			binaries=root/'bin'
			stage=root/'sdk'
			binaries.mkdir()
			stage.mkdir()
			for name in ('veroscript-runtime.exe','veroscript-launcher.exe'):
				(binaries/name).write_bytes(b'native-fixture')
			(binaries/'Vm').mkdir()
			(binaries/'Vm/VeroScriptVmBytecode.dll').write_bytes(b'vm-fixture')
			runtime_libraries = ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
								 'msvcp140_atomic_wait.dll')
			for name in runtime_libraries:
				(binaries/name).write_bytes(b'redistributable-fixture')
			notice = binaries/'licenses/MicrosoftVisualCpp/NOTICE.txt'
			notice.parent.mkdir(parents=True)
			notice.write_text('runtime redistribution notice')
			for name in ('miniz/LICENSE','json/LICENSE.MIT','sha256/LICENSE'):
				file=repo/'VeroScriptApplication/ThirdParty'/name
				file.parent.mkdir(parents=True,exist_ok=True)
				file.write_text('license')
			(stage/'assets/stdlib').mkdir(parents=True)
			(stage/'assets/stdlib/core.vs').write_text('export int value() { return 1; }')
			(stage/'lib').mkdir()
			(stage/'lib/foreign-compiler.lib').write_bytes(b'development only')
			(stage/'bin').mkdir()
			(stage/'bin/unrelated.dll').write_bytes(b'unrelated module')
			(stage/'bin/concrt140.dll').write_bytes(b'already staged runtime')
			target={'core':'0.1.1','coreBuild':'fixture','abis':{'native-extension':'fixture'}}
			with patch('veroscript_applications.subprocess.check_output', return_value=json.dumps(target)):
				self.assertTrue(stage_application_runtime(repo,binaries,stage,'bin'))
				first=(stage/'veroscript-runtime.json').read_bytes()
				self.assertTrue(stage_application_runtime(repo,stage/'bin',stage,'bin'))
				self.assertEqual(first,(stage/'veroscript-runtime.json').read_bytes())
			data=json.loads(first)
			paths=[item['path'] for item in data['files']]
			self.assertIn('bin/veroscript-runtime.exe',paths)
			self.assertIn('assets/stdlib/core.vs',paths)
			for name in runtime_libraries:
				self.assertIn('bin/' + name, paths)
				self.assertEqual((stage/'bin'/name).read_bytes(), b'redistributable-fixture')
				self.assertIn('bin/Vm/' + name, paths)
				self.assertEqual((stage/'bin/Vm'/name).read_bytes(), b'redistributable-fixture')
			self.assertIn('bin/licenses/MicrosoftVisualCpp/NOTICE.txt', paths)
			self.assertIn('bin/concrt140.dll', paths)
			self.assertIn('bin/Vm/VeroScriptVmBytecode.dll', paths)
			self.assertIn('bin/Vm/concrt140.dll', paths)
			self.assertIn('bin/Vm/licenses/MicrosoftVisualCpp/NOTICE.txt', paths)
			self.assertNotIn('bin/unrelated.dll', paths)
			self.assertNotIn('lib/foreign-compiler.lib',paths)
			self.assertNotIn('bin/veroscript-launcher.exe',paths)
			for item in data['files']:
				payload=(stage/item['path']).read_bytes()
				self.assertEqual(item['sha256'],hashlib.sha256(payload).hexdigest())
				self.assertEqual(item['bytes'],str(len(payload)))

	def test_sdk_identity_binds_to_published_core_bytes(self):
		with tempfile.TemporaryDirectory() as tmp:
			stage=Path(tmp)
			config=stage/'lib/cmake/VeroScriptApplication'
			config.mkdir(parents=True)
			(stage/'lib/VeroScript.lib').write_bytes(b'published stripped core')
			finalize_application_sdk(stage)
			self.assertIn(hashlib.sha256(b'published stripped core').hexdigest(),(config/'VeroScriptApplicationCoreBuild.cmake').read_text())

class VmRuntimePackagingTests(unittest.TestCase):
	def test_runtime_staging_is_independent_of_optional_application_and_idempotent(self):
		for library in ('VeroScriptVmBytecode.dll', 'libVeroScriptVmBytecode.so',
						'libVeroScriptVmBytecode.dylib'):
			with self.subTest(library=library), tempfile.TemporaryDirectory() as temporary:
				root = Path(temporary)
				binaries, destination = root / 'source', root / 'stage/bin'
				(binaries / 'Vm').mkdir(parents=True)
				(binaries / 'Vm' / library).write_bytes(b'vm fixture')
				notice = Path('licenses/MicrosoftVisualCpp/NOTICE.txt')
				if library.endswith('.dll'):
					(binaries / 'vcruntime140.dll').write_bytes(b'matched CRT')
					(binaries / notice).parent.mkdir(parents=True)
					(binaries / notice).write_bytes(b'redistribution notice')
				stage_runtime_from_binaries(binaries, destination)
				self.assertEqual((destination / 'Vm' / library).read_bytes(), b'vm fixture')
				self.assertFalse((destination / 'veroscript-runtime.exe').exists())
				if library.endswith('.dll'):
					for directory in (destination, destination / 'Vm'):
						self.assertEqual((directory / 'vcruntime140.dll').read_bytes(), b'matched CRT')
						self.assertEqual((directory / notice).read_bytes(), b'redistribution notice')
				before = {path.relative_to(destination): path.read_bytes()
						  for path in destination.rglob('*') if path.is_file()}
				stage_runtime_from_binaries(destination, destination)
				self.assertEqual(before, {path.relative_to(destination): path.read_bytes()
										  for path in destination.rglob('*') if path.is_file()})

	def test_missing_or_ambiguous_backend_is_rejected_before_staging(self):
		for libraries in (None, (), ('unrelated.dll',),
						  ('VeroScriptVmBytecode.dll', 'libVeroScriptVmBytecode.so')):
			with self.subTest(libraries=libraries), tempfile.TemporaryDirectory() as temporary:
				root = Path(temporary)
				binaries, destination = root / 'source', root / 'stage'
				binaries.mkdir()
				if libraries is not None:
					(binaries / 'Vm').mkdir()
					for library in libraries:
						(binaries / 'Vm' / library).write_bytes(b'fixture')
				with self.assertRaisesRegex(ValueError, 'Vm component|exactly one'):
					stage_runtime_from_binaries(binaries, destination)
				self.assertFalse(destination.exists())

@unittest.skipUnless(os.name == 'nt', 'Requires Windows PowerShell 5.1')
class PackageClientPowerShellTests(unittest.TestCase):
	def test_sdk_installs_the_optional_client_for_a_true_cache_value(self):
		with tempfile.TemporaryDirectory(prefix='vero SDK cache boolean ') as temporary:
			root = Path(temporary)
			(root / 'CMakeCache.txt').write_text('VEROSCRIPT_BUILD_PACKAGE_MANAGER:BOOL=TRUE\n')
			helper = Path(__file__).resolve().parents[1] / 'veroscript-packages.ps1'
			quote = lambda value: "'" + str(value).replace("'", "''") + "'"
			script = ("$ErrorActionPreference='Stop'\n$script:Calls=@()\n. " + quote(helper) + '\n' +
				"function python { $script:Calls += ,(@('python') + $args); $global:LASTEXITCODE=0 }\n" +
				"function cmake { $script:Calls += ,(@('cmake') + $args); $global:LASTEXITCODE=0 }\n" +
				'Copy-VeroScriptPackageManagerSdk ' + ' '.join(map(quote, (root, root / 'stage', 'Release', '2.3.4'))) + '\n' +
				'ConvertTo-Json -InputObject $script:Calls -Depth 8\n')
			powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
			result = subprocess.run([str(powershell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script], capture_output=True, text=True)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			calls = json.loads(result.stdout)
			self.assertTrue(any(call[0] == 'cmake' and call[-1] == 'VeroScriptPackageManager' for call in calls))
			self.assertTrue(any(call[0] == 'python' and str(call[1]).endswith('veroscript_packages.py') for call in calls))

	def check_native_failure(self, message, exit_code, expected_skip):
		with tempfile.TemporaryDirectory(prefix='vero package client ') as temporary:
			root = Path(temporary)
			cli = root / 'client.cmd'
			cli.write_text('@echo off\necho ' + message + ' 1>&2\nexit /b ' + str(exit_code) + '\n')
			helper = Path(__file__).resolve().parents[1] / 'veroscript-packages.ps1'
			quote = lambda value: "'" + str(value).replace("'", "''") + "'"
			invoke = 'Copy-VeroScriptPackageClient ' + ' '.join(map(quote, (root, cli, root, 'runtime')))
			script = "$ErrorActionPreference='Stop'\n. " + quote(helper) + '\n'
			if expected_skip:
				script += invoke + '\n'
			else:
				script += ("$ExpectedFailure=$false\ntry { " + invoke + " } catch {\n"
						   "if ($_.Exception.Message -notlike '*unexpected fixture failure*') { throw }\n"
						   "$ExpectedFailure=$true\n}\n"
						   "if (-not $ExpectedFailure) { throw 'Unexpected native error was ignored' }\n")
			script += ("if ($ErrorActionPreference -ne 'Stop') { throw 'Error preference was changed' }\n"
					   "Write-Output 'Expected package client outcome'\n")
			powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
			result = subprocess.run([str(powershell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script],
									capture_output=True, text=True, errors='replace')
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertIn('Expected package client outcome', result.stdout)

	def test_optional_package_manager_failure_is_captured_without_terminating(self):
		self.check_native_failure('VeroScript: package management was not built into this executable', 2, True)

	def test_unexpected_native_failure_still_throws_and_preserves_preference(self):
		self.check_native_failure('VeroScript: unexpected fixture failure', 3, False)

@unittest.skipUnless(os.name == 'nt', 'Requires Windows PowerShell 5.1')
class ApplicationRuntimePowerShellTests(unittest.TestCase):
	def check_executable_directory(self, executable_directory):
		with tempfile.TemporaryDirectory(prefix='vero application argv ') as temporary:
			root = Path(temporary)
			binaries = root / 'binaries'
			binaries.mkdir()
			helper = Path(__file__).resolve().parents[1] / 'veroscript-packages.ps1'
			quote = lambda value: "'" + str(value).replace("'", "''") + "'"
			arguments = [root, binaries, root / 'stage']
			if executable_directory is not None:
				arguments.append(executable_directory)
			script = ("$ErrorActionPreference='Stop'\n. " + quote(helper) + '\n' +
					  'Copy-VeroScriptApplicationRuntime ' + ' '.join(map(quote, arguments)) + '\n' +
					  "Write-Output 'Application Runtime arguments accepted'\n")
			environment = dict(os.environ)
			environment['PATH'] = str(Path(sys.executable).parent) + os.pathsep + environment.get('PATH', '')
			environment['PYTHONDONTWRITEBYTECODE'] = '1'
			powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
			result = subprocess.run([str(powershell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script],
									env=environment, capture_output=True, text=True, errors='replace')
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertIn('Application Runtime arguments accepted', result.stdout)

	def test_default_empty_executable_directory_reaches_python_entry_point(self):
		self.check_executable_directory(None)

	def test_explicit_bin_directory_reaches_python_entry_point(self):
		self.check_executable_directory('bin')

if __name__=='__main__':
	unittest.main()
