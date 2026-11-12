# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Check migrated SDK exports, generated ABI contracts and Windows JIT audits."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))
from veroscript_business_release import ALL_BUSINESSES
from veroscript_release import archive_tree, core_public_abi, verify_release, write_manifest


def pe():
	data = bytearray(128)
	data[:2] = b'MZ'
	struct.pack_into('<I', data, 60, 64)
	data[64:68] = b'PE\0\0'
	struct.pack_into('<H', data, 68, 0x8664)
	return bytes(data)


class SdkContractTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix='vero sdk contract ')
		self.addCleanup(temporary.cleanup)
		self.repo = Path(temporary.name).resolve()
		self.headers = self.repo / 'VeroScript/Public'
		self.headers.mkdir(parents=True)
		(self.headers / 'VeroScript.h').write_bytes(b'#pragma once\n')
		(self.headers / 'VeroScriptVersion.h').write_bytes(b'// Source module marker\n')
		self.vm_headers = self.repo / 'Modules/Execution/VeroScriptBytecodeVm/Public'
		self.vm_headers.mkdir(parents=True)
		(self.vm_headers / 'VeroScriptBytecodeVm.h').write_bytes(b'vm factory header')
		self.core = b'core library fixture'
		self.build_id = hashlib.sha256(self.core).hexdigest()

	def test_build_cache_preserves_utf8_paths_under_native_locale(self):
		build = self.repo / 'cache'
		build.mkdir()
		expected = '/tools/\u7f16\u8bd1\u5668 \U0001f4e6/cmake'
		(build / 'CMakeCache.txt').write_text('CMAKE_COMMAND:INTERNAL=' + expected + '\n', encoding='utf-8')
		code = (
			'import importlib.util,json,pathlib,sys;sys.path.insert(0,sys.argv[1]);'
			's=importlib.util.spec_from_file_location("release_packer",pathlib.Path(sys.argv[1])/"package-veroscript-all.py");'
			'm=importlib.util.module_from_spec(s);s.loader.exec_module(m);'
			'assert m.cache(pathlib.Path(sys.argv[2]))["CMAKE_COMMAND"] == json.loads(sys.argv[3]), "UTF-8 cache path changed"'
		)
		env = {**os.environ, 'PYTHONUTF8': '0', 'PYTHONCOERCECLOCALE': '0', 'LC_ALL': 'C', 'LANG': 'C'}
		result = subprocess.run([sys.executable, '-X', 'utf8=0', '-c', code, str(TOOLS), str(build), json.dumps(expected)],
			env=env, capture_output=True, timeout=30)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def manifest(self, release, target, business):
		write_manifest(release, '0.1.1', target, self.build_id,
					   business_versions={name: '0.1.1' for name in ALL_BUSINESSES},
					   native_version='0.1.1')
		path = release / 'release-manifest.json'
		manifest = json.loads(path.read_text())
		manifest.update(selectedBusinesses=[business], partialBusinesses=[business])
		path.write_text(json.dumps(manifest), encoding='utf-8')

	@unittest.skipUnless(shutil.which('dotnet'), 'Requires .NET SDK')
	def test_csharp_mobile_pack_preserves_native_paths_and_extensionless_notices(self):
		project = ET.parse(ROOT / 'Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj').getroot()
		item = next(node for node in project.iter('None') if node.get('Include') == '$(VeroScriptMobileNativeRoot)/**/*')
		native = self.repo / 'native-input'
		files = {'licenses/LICENSE': b'license', 'licenses/NOTICE': b'notice',
			'licenses/NOTICE.toolchain': b'toolchain', 'android/arm64-v8a/libInterop.so': b'native',
			'ios/Framework.xcframework/Info.plist': b'plist', 'mobile-sdk.json': b'{}'}
		for name, data in files.items():
			path = native / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		probe = self.repo / 'nuget-path-probe'
		probe.mkdir()
		root = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
		properties = ET.SubElement(root, 'PropertyGroup')
		for key, value in {'TargetFramework': 'net9.0', 'PackageId': 'VeroScriptPathProbe', 'Version': '0.1.1',
			'VeroScriptIncludeMobile': 'true', 'VeroScriptMobileNativeRoot': str(native)}.items():
			ET.SubElement(properties, key).text = value
		ET.SubElement(ET.SubElement(root, 'ItemGroup'), item.tag, item.attrib)
		file = probe / 'Probe.csproj'
		ET.ElementTree(root).write(file, encoding='utf-8', xml_declaration=True)
		result = subprocess.run(['dotnet', 'pack', str(file), '-c', 'Release', '-o', str(probe / 'output')],
			capture_output=True, timeout=180)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		with zipfile.ZipFile(probe / 'output/VeroScriptPathProbe.0.1.1.nupkg') as archive:
			actual = {name[7:]: archive.read(name) for name in archive.namelist() if name.startswith('native/')}
		self.assertEqual(actual, files)

	def sdk(self, target):
		release = self.repo / '.tmp' / target
		package = self.repo / '.tmp' / ('VeroScriptSDK-0.1.1-' + target)
		library = 'VeroScript.lib' if target.startswith('win-') else 'libVeroScript.a'
		files = {
			'LICENSE': b'fixture license',
			'include/Script/VeroScript.h': (self.headers / 'VeroScript.h').read_bytes(),
			'include/Script/VeroScriptVersion.h': (self.headers / 'VeroScriptVersion.h').read_bytes(),
			'include/Script/VeroScriptJitBuild.h':
				('#pragma once\n#define VEROSCRIPT_JIT_PUBLIC_ABI "' + core_public_abi(self.headers) + '"\n').encode(),
			'lib/' + library: self.core,
			'lib/libVeroScriptTooling.a': b'tooling fixture',
			'lib/cmake/VeroScript/VeroScriptConfig.cmake': self.build_id.encode(),
			'assets/stdlib/core.vs': b'core',
			'assets/localization/en-US/shared.json': b'{}',
			'lib/' + ('VeroScriptBytecodeVm.lib' if target.startswith('win-') else 'libVeroScriptBytecodeVm.a'): b'vm static',
			'include/Vm/VeroScriptBytecodeVm.h': (self.vm_headers / 'VeroScriptBytecodeVm.h').read_bytes(),
			'lib/cmake/VeroScriptBytecodeVm/VeroScriptBytecodeVmConfig.cmake': b'vm config',
			'lib/cmake/VeroScriptExecutionContract/VeroScriptExecutionContractConfig.cmake': b'contract config',
		}
		if target.startswith('win-'):
			vm = 'VeroScriptVmBytecode.dll'
			data = pe()
			for dependency in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
				files['bin/Vm/' + dependency] = data
			files['bin/Vm/licenses/MicrosoftVisualCpp/NOTICE.txt'] = b'Runtime notice'
		elif target.startswith('linux-'):
			vm = 'libVeroScriptVmBytecode.so'
			data = bytearray(64)
			data[:6] = b'\x7fELF\x02\x01'
			struct.pack_into('<H', data, 18, 62)
		else:
			vm = 'libVeroScriptVmBytecode.dylib'
			data = b'\xcf\xfa\xed\xfe' + struct.pack('<I', 0x0100000c)
		files['bin/Vm/' + vm] = data
		for name, data in files.items():
			path = package / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		archive = release / 'sdk' / (package.name + ('.zip' if target.startswith('win-') else '.tar.gz'))
		return release, package, archive

	def audit_sdk(self, release, package, archive, target):
		archive_tree(package, archive)
		self.manifest(release, target, 'CoreSDK')
		return verify_release(release, self.repo, businesses={'CoreSDK'}, partial_businesses={'CoreSDK'})

	def test_generated_abi_is_accepted_for_windows_linux_and_macos(self):
		for target in ('win-x64', 'linux-x64', 'macos-arm64'):
			with self.subTest(target=target):
				release, package, archive = self.sdk(target)
				self.audit_sdk(release, package, archive, target)

	def test_application_sdk_schema_exports_are_audited_against_their_source(self):
		target = 'linux-x64'
		release, package, archive = self.sdk(target)
		source = self.repo / 'VeroScriptApplication'
		for name, data in (('Public/VeroScriptApplication.h', b'application API'), ('Resources/Schemas/veroscript-app.schema.json', b'{"type":"object"}')):
			file = source / name
			file.parent.mkdir(parents=True, exist_ok=True)
			file.write_bytes(data)
		files = {
			'include/Application/VeroScriptApplication.h': b'application API',
			'lib/libVeroScriptApplication.a': b'static application', 'lib/libVeroScriptApplicationZip.a': b'static zip',
			'lib/cmake/VeroScriptApplication/VeroScriptApplicationConfig.cmake': b'find_dependency(VeroScript 0.1.1 EXACT CONFIG)',
			'lib/cmake/VeroScriptApplication/VeroScriptApplicationConfigVersion.cmake': b'version',
			'lib/cmake/VeroScriptApplication/VeroScriptApplicationTargets.cmake': b'targets',
			'lib/cmake/VeroScriptApplication/VeroScriptApplicationCoreBuild.cmake': ('set(VeroScriptApplication_CORE_BUILD_ID "' + self.build_id + '")').encode(),
			'share/VeroScript/application/Resources/Schemas/veroscript-app.schema.json': b'{"type":"object"}'}
		for name in ('json/LICENSE.MIT', 'miniz/LICENSE', 'sha256/LICENSE', 'sources.json'):
			files['licenses/application/' + name] = b'notice'
		for name, data in files.items():
			file = package / name
			file.parent.mkdir(parents=True, exist_ok=True)
			file.write_bytes(data)
		self.audit_sdk(release, package, archive, target)
		file = package / 'share/VeroScript/application/Resources/Schemas/veroscript-app.schema.json'
		for data in (None, b'{"type":"array"}'):
			with self.subTest(data=data):
				if data is None:
					file.unlink()
				else:
					file.write_bytes(data)
				with self.assertRaisesRegex(ValueError, 'Stale public SDK source'):
					self.audit_sdk(release, package, archive, target)

	def test_missing_stale_and_duplicate_abi_are_rejected_after_rehash(self):
		for target in ('win-x64', 'linux-x64', 'macos-arm64'):
			release, package, archive = self.sdk(target)
			header = package / 'include/Script/VeroScriptJitBuild.h'
			original = header.read_bytes()
			for data in (None, original.replace(core_public_abi(self.headers).encode(), b'0' * 64), original * 2):
				with self.subTest(target=target, data=data):
					if data is None:
						header.unlink()
					else:
						header.write_bytes(data)
					try:
						with self.assertRaisesRegex(ValueError, 'Core JIT public ABI'):
							self.audit_sdk(release, package, archive, target)
					finally:
						header.write_bytes(original)

	def test_unexpected_and_stale_public_headers_are_rejected_after_rehash(self):
		for target in ('win-x64', 'linux-x64'):
			for directory, header in (('Script', 'VeroScript.h'), ('Vm', 'VeroScriptBytecodeVm.h')):
				release, package, archive = self.sdk(target)
				extra = package / 'include' / directory / 'stale.h'
				extra.write_bytes(b'stale')
				with self.subTest(target=target, directory=directory, case='extra'), self.assertRaisesRegex(ValueError, 'Unexpected public headers'):
					self.audit_sdk(release, package, archive, target)
				extra.unlink()
				(package / 'include' / directory / header).write_bytes(b'old public API')
				with self.subTest(target=target, directory=directory, case='stale'), self.assertRaisesRegex(ValueError, 'Stale public SDK source'):
					self.audit_sdk(release, package, archive, target)

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required to compare the actual generated contract')
	def test_abi_matches_actual_cmake_generator_and_normalizes_crlf(self):
		(self.headers / 'Nested').mkdir()
		(self.headers / 'Nested/second.h').write_bytes(b'#pragma once\r\nstruct Value {};\r\n')
		(self.headers / 'lowercase.h').write_bytes(b'// Case-sensitive CMake sort\n')
		shared = self.repo / 'Shared/Private'
		shared.mkdir(parents=True)
		for name in ('VeroScriptFloatParsing.h', 'VeroScriptFloatFormatting.h', 'VeroScriptJsonString.h'):
			shutil.copy2(ROOT / 'Shared/Private' / name, shared)
		source = (ROOT / 'VeroScript/CMakeLists.txt').read_text(encoding='utf-8')
		generator = source[source.index('file(GLOB_RECURSE _vs_jit_headers'):source.index('target_include_directories(VeroScript PUBLIC')]
		script = self.headers.parent / 'CMakeLists.txt'
		script.write_text('cmake_minimum_required(VERSION 3.25)\nproject(AbiContract NONE)\n' + generator,
						  encoding='utf-8')
		build = self.repo / 'build'
		subprocess.run(['cmake', '-S', str(self.headers.parent), '-B', str(build)], check=True, capture_output=True)
		generated = (build / 'Generated/VeroScriptJitBuild.h').read_text()
		self.assertIn('"' + core_public_abi(self.headers) + '"', generated)
		expected = core_public_abi(self.headers)
		json_string = shared / 'VeroScriptJsonString.h'
		json_string.write_bytes(json_string.read_bytes() + b'\n// Shared JSON change\n')
		subprocess.run(['cmake', '-S', str(self.headers.parent), '-B', str(build)], check=True, capture_output=True)
		updated = (build / 'Generated/VeroScriptJitBuild.h').read_text()
		self.assertNotEqual(generated, updated)
		self.assertIn('"' + expected + '"', updated)
		for path in self.headers.rglob('*.h'):
			path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n'))
		self.assertEqual(core_public_abi(self.headers), expected)

	@unittest.skipUnless(os.name == 'nt', 'Requires Windows PowerShell')
	def test_powershell_jit_consumer_uses_the_staged_official_package(self):
		packager = str(TOOLS / 'package-veroscript-all.ps1').replace("'", "''")
		script = """
        $ErrorActionPreference = 'Stop'
        Set-StrictMode -Version Latest
        $tokens = $null; $errors = $null
        $ast = [System.Management.Automation.Language.Parser]::ParseFile('%s', [ref]$tokens, [ref]$errors)
        if ($errors.Count) { throw 'Packager syntax errors' }
        function Read-Assignment([string]$Name) {
            $node = $ast.Find({ param($item)
                $item -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                $item.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                $item.Left.VariablePath.UserPath -eq $Name
            }, $true)
            if (-not $node) { throw "Missing assignment: $Name" }
            return $node.Extent.Text
        }
        $consumer = $ast.Find({ param($node)
            $node -is [System.Management.Automation.Language.CommandAst] -and
            $node.GetCommandName() -eq 'Join-Path' -and $node.CommandElements.Count -eq 3 -and
            $node.CommandElements[1].Extent.Text -eq '$JitPackageDirectory'
        }, $true)
        if (-not $consumer) { throw 'Missing IDE backend path' }
        $Versions = @{ Jit = '0.1.1' }
        $JitStageRoot = Join-Path ([IO.Path]::GetTempPath()) 'JIT stage with spaces'
        foreach ($Runtime in @('win-x64', 'win-x86', 'win-arm64')) {
            Invoke-Expression (Read-Assignment 'JitPackageName')
            Invoke-Expression (Read-Assignment 'JitPackageDirectory')
            $Actual = Invoke-Expression $consumer.Extent.Text
            $Expected = Join-Path (Join-Path $JitStageRoot "vs-asmjit-0.1.1-$Runtime") 'VeroScriptJitAsmJit.dll'
            if ($Actual -cne $Expected) { throw 'Wrong staged backend path' }
        }
        """ % packager
		result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', script], capture_output=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	@unittest.skipUnless(os.name == 'nt', 'Requires Windows PowerShell')
	def test_powershell_accepts_selected_jit_without_other_businesses(self):
		release = self.repo / '.tmp/jit-release'
		package = self.repo / '.tmp/vs-asmjit-0.1.1-win-x64'
		files = {'VeroScriptJitAsmJit.dll': pe(),
				 'LICENSE': (ROOT / 'LICENSE').read_bytes(),
				 'licenses/asmjit/LICENSE.md': b'fixture license',
				 'licenses/asmjit/VERSION.txt': b'fixture version',
				 'licenses/MicrosoftVisualCpp/NOTICE.txt': b'fixture runtime notice'}
		for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
			files[name] = pe()
		for name, data in files.items():
			path = package / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		archive_tree(package, release / 'jit' / (package.name + '.zip'))
		self.manifest(release, 'win-x64', 'Jit')
		environment = os.environ.copy()
		# PowerShell 7's module path cannot load the Windows PowerShell 5 modules.
		for name in tuple(environment):
			if name.casefold() == 'psmodulepath':
				environment.pop(name)
		command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
								 str(TOOLS / 'verify-veroscript-release.ps1'), '-ReleaseDirectory', str(release),
								 '-Target', 'win-x64', '-PythonExecutable', sys.executable]
		result = subprocess.run(command, env=environment, capture_output=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		archive = release / 'jit' / (package.name + '.zip')
		archive.write_bytes(archive.read_bytes() + b'tampered')
		result = subprocess.run(command, env=environment, capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		archive_tree(package, archive)
		obsolete = release / 'installer/VeroScriptJitInstaller-0.1.1-win-x64.exe'
		obsolete.parent.mkdir()
		obsolete.write_bytes(pe())
		self.manifest(release, 'win-x64', 'Jit')
		result = subprocess.run(command, env=environment, capture_output=True)
		self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
	unittest.main()
