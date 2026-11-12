# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest

TOOLS = Path(__file__).resolve().parents[1]
REPO = Path(__file__).resolve().parents[4]
MODULE = TOOLS.parents[2]
sys.path.insert(0, str(TOOLS))


class ToolLocationTests(unittest.TestCase):
	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for SDK configuration tests')
	def test_packager_generates_consumable_configs_from_module_owned_templates(self):
		spec = importlib.util.spec_from_file_location('veroscript_packager', TOOLS / 'package-veroscript-all.py')
		packager = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(packager)
		release = packager.Release.__new__(packager.Release)
		release.repo = REPO
		release.v = '0.1.1'
		release.versions = {'CoreSDK': release.v, 'Cpp': release.v}
		release.build_id = 'matching-build'
		release.build_environment = {'asmjit': 'OFF'}
		release.settings = {'VEROSCRIPT_BUILD_APPLICATION': 'OFF'}
		release.arch = 'x64'
		with tempfile.TemporaryDirectory(prefix='vero SDK source layout ') as temporary:
			root = Path(temporary).resolve()
			core, cpp = root / 'Core SDK', root / 'Cpp/Runtime'
			release.config('core', core)
			release.config('cpp', cpp)
			header = cpp / 'include/Cpp/VeroScriptCpp.h'
			header.parent.mkdir(parents=True)
			header.write_text('// SDK fixture\n')
			library = cpp / 'lib/libVeroScriptCpp.a'
			library.touch()
			source = root / 'consumer'
			source.mkdir()
			(source / 'CMakeLists.txt').write_text(
				'cmake_minimum_required(VERSION 3.25)\nproject(SdkConsumer LANGUAGES CXX)\n'
				'find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)\n'
				'if(NOT TARGET VeroScript::VeroScript OR NOT TARGET VeroScript::VeroScriptCpp)\n'
				'  message(FATAL_ERROR "Missing imported SDK targets")\nendif()\n'
				'if(NOT VeroScriptCpp_BUILD_ID STREQUAL "matching-build")\n'
				'  message(FATAL_ERROR "SDK build identity was lost")\nendif()\n')
			result = subprocess.run(['cmake', '-S', str(source), '-B', str(root / 'build'),
									 '-DCMAKE_PREFIX_PATH=' + core.as_posix() + ';' + cpp.as_posix()],
									text=True, capture_output=True, timeout=180)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_python_distribution_validator_discovers_tools_from_another_working_directory(self):
		with tempfile.TemporaryDirectory(prefix='vero relocated validator ') as temporary:
			root = Path(temporary).resolve()
			tools = root / 'Renamed Checkout/Resources/Tools/Packaging'
			tools.mkdir(parents=True)
			shutil.copy2(TOOLS / 'veroscript_products.py', tools)
			owner = root / 'Renamed Checkout/Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging'
			owner.mkdir(parents=True)
			validator = MODULE / 'Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging/validate_distribution.py'
			shutil.copy2(validator, owner)
			result = subprocess.run([sys.executable, str(owner / validator.name), '--help'],
									cwd=root, text=True, capture_output=True)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertIn('--expected-version', result.stdout)

	@unittest.skipUnless(os.name == 'nt', 'PowerShell component output guards')
	def test_powershell_protects_business_platform_containers_before_first_publication(self):
		with tempfile.TemporaryDirectory(prefix='vero collection guards ') as temporary:
			repo = Path(temporary).resolve()
			module = repo / 'Engine Sources'
			marker = module / 'VeroScript/Public/VeroScriptVersion.h'
			marker.parent.mkdir(parents=True)
			marker.write_text('// module marker\n')
			quote = lambda value: "'" + str(value).replace("'", "''") + "'"
			paths = ('Runtime', 'CoreSDK/win-x64', 'Extensions', 'Extensions/Language/Python',
					 'Extensions/Language/Python/linux-x64', 'Modules/Execution/Jit', 'Modules/Execution/Jit/macos-arm64')
			array = '@(' + ','.join(quote(module / 'Release' / path) for path in paths) + ')'
			command = (f"$ErrorActionPreference='Stop'; . {quote(TOOLS / 'veroscript-output.ps1')}; "
					   f"foreach ($Path in {array}) {{ $Rejected=$false; "
					   f"try {{ Assert-VeroScriptComponentOutputPath $Path {quote(repo)} }} "
					   "catch { if ($_.Exception.Message -notlike '*managed Release*') { throw }; $Rejected=$true }; "
					   "if (-not $Rejected) { throw \"Unprotected collection container: $Path\" } }; "
					   f"Assert-VeroScriptComponentOutputPath {quote(repo / '.tmp/package-stage')} {quote(repo)}")
			result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command],
									capture_output=True, text=True)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	@unittest.skipUnless(os.name == 'nt', 'Windows native stderr handling')
	def test_windows_native_commands_use_exit_codes_with_redirected_stderr(self):
		shells = {path for name in ('powershell.exe', 'pwsh.exe')
				  if (path := shutil.which(name))}
		self.assertTrue(shells, 'PowerShell is required on Windows')
		probe = r'''
param([string]$PackagingRoot)
$ErrorActionPreference = 'Stop'
foreach ($Name in @('package-veroscript-all.ps1', 'verify-veroscript-webassembly.ps1')) {
    $ParseErrors = $null
    $Ast = [System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PackagingRoot $Name), [ref]$null, [ref]$ParseErrors)
    if ($ParseErrors.Count) { throw "Cannot parse $Name" }
    $Function = $Ast.Find({ param($Node)
        $Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $Node.Name -eq 'Invoke-Checked'
    }, $true)
    if ($null -eq $Function) { throw "Missing helper: $Name" }
    . ([scriptblock]::Create($Function.Extent.Text))
    $Log = Join-Path $PSScriptRoot 'native.log'
    Invoke-Checked $env:ComSpec @('/d', '/c', 'echo normal diagnostic 1>&2 & exit /b 0') *> $Log
    if ((Get-Content -LiteralPath $Log -Raw) -notmatch 'normal diagnostic') {
        throw "Missing diagnostic: $Name"
    }
    if ($ErrorActionPreference -ne 'Stop') { throw 'Caller preference changed' }
    $Rejected = $false
    try {
        Invoke-Checked $env:ComSpec @('/d', '/c', 'echo real failure 1>&2 & exit /b 73') *> $Log
    } catch {
        if ($_.Exception.Message -notlike 'Command failed (73):*') { throw }
        $Rejected = $true
    }
    if (-not $Rejected) { throw "Failed command accepted: $Name" }
    $Rejected = $false
    try { Invoke-Checked 'veroscript-missing-command-for-test.exe' @() *> $Log }
    catch { $Rejected = $true }
    if (-not $Rejected) { throw "Missing command accepted: $Name" }
}
'''
		with tempfile.TemporaryDirectory(prefix='vero native commands ') as temporary:
			script = Path(temporary) / 'probe.ps1'
			script.write_text(probe, encoding='utf-8')
			for shell in sorted(shells):
				with self.subTest(shell=shell):
					result = subprocess.run([shell, '-NoProfile', '-ExecutionPolicy', 'Bypass',
											 '-File', str(script), str(TOOLS)],
											capture_output=True, text=True)
					self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	def test_repository_discovery_uses_relocated_shared_helper(self):
		with tempfile.TemporaryDirectory(prefix='vero repository root ') as temporary:
			workspace = Path(temporary).resolve()
			# An enclosing project must not capture this module through the retired marker.
			(workspace / 'CMakeLists.txt').write_text('# Enclosing project\n')
			(workspace / 'cmake').mkdir()
			repo = workspace / 'Repository Sources'
			tools = repo / 'Engine Sources/Resources/Tools/Packaging'
			tools.mkdir(parents=True)
			shutil.copy2(TOOLS / 'veroscript_paths.py', tools)
			(repo / 'CMakeLists.txt').write_text('# Repository entry\n')
			helper = repo / 'Resources/Tools/CMake/VeroScriptHelpers.cmake'
			helper.parent.mkdir(parents=True)
			shutil.copy2(REPO / 'Resources/Tools/CMake/VeroScriptHelpers.cmake', helper)
			probe = ('import sys; sys.path.insert(0, sys.argv[1]); '
					 'from veroscript_paths import repository_root; print(repository_root())')
			result = subprocess.run([sys.executable, '-c', probe, str(tools)],
									cwd=workspace, check=True, text=True, capture_output=True)
			self.assertEqual(Path(result.stdout.strip()), repo)

	def test_isolated_tools_ignore_empty_shared_directory(self):
		with tempfile.TemporaryDirectory(prefix='vero isolated tools ') as temporary:
			workspace = Path(temporary).resolve()
			(workspace / 'CMakeLists.txt').write_text('# Incomplete enclosing project\n')
			(workspace / 'Resources/Tools/CMake').mkdir(parents=True)
			repo = workspace / 'Isolated Sources'
			tools = repo / 'Engine Sources/Resources/Tools/Packaging'
			tools.mkdir(parents=True)
			shutil.copy2(TOOLS / 'veroscript_paths.py', tools)
			probe = ('import sys; sys.path.insert(0, sys.argv[1]); '
					 'from veroscript_paths import repository_root; print(repository_root())')
			result = subprocess.run([sys.executable, '-c', probe, str(tools)],
									cwd=workspace, check=True, text=True, capture_output=True)
			self.assertEqual(Path(result.stdout.strip()), repo)

	def test_docs_catalog_alone_identifies_a_renamed_module(self):
		sys.path.insert(0, str(TOOLS))
		from veroscript_paths import module_root
		with tempfile.TemporaryDirectory(prefix='vero docs catalog ') as temporary:
			repo = Path(temporary).resolve()
			module = repo / 'Language Sources'
			catalog = module / 'Resources/Docs/documentation.json'
			catalog.parent.mkdir(parents=True)
			catalog.write_text('{}', encoding='utf-8')
			consumer = repo / 'Editor Space/Resources/Docs/documentation.json'
			consumer.parent.mkdir(parents=True)
			consumer.write_text(json.dumps({'product': 'ValenceStudio'}), encoding='utf-8')
			self.assertEqual(module, module_root(repo))
			if os.name == 'nt':
				quote = lambda value: "'" + str(value).replace("'", "''") + "'"
				command = (f"$ErrorActionPreference='Stop'; . {quote(TOOLS / 'veroscript-output.ps1')}; "
						   f"Get-VeroScriptModuleRoot {quote(repo)}")
				result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command],
										check=True, capture_output=True, text=True)
				self.assertEqual(module, Path(result.stdout.strip()))

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for module discovery tests')
	def test_cmake_discovery_ignores_other_products_version_files(self):
		with tempfile.TemporaryDirectory(prefix='vero module root ') as temporary:
			repo = Path(temporary)
			module = repo / 'Engine Sources'
			(module / 'VeroScript').mkdir(parents=True)
			tools = module / 'Resources/Tools/Packaging'
			tools.mkdir(parents=True)
			(tools / 'ReleaseVersions.json').write_text('{}')
			(repo / 'Another Product').mkdir()
			(repo / 'Another Product/ReleaseVersions.json').write_text('{}')
			script = repo / 'resolve.cmake'
			script.write_text(f'include("{(MODULE / "Resources/Tools/CMake/VeroScriptPaths.cmake").as_posix()}")\n'
							  f'vs_resolve_veroscript_root(root "{repo.as_posix()}")\n'
							  f'file(WRITE "{(repo / "root.txt").as_posix()}" "${{root}}")\n')
			result = subprocess.run(['cmake', '-P', str(script)], text=True, capture_output=True)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertEqual(Path((repo / 'root.txt').read_text()), module)
			result = subprocess.run(['cmake', '-DVEROSCRIPT_MODULE_ROOT=Another Product', '-P', str(script)],
									text=True, capture_output=True)
			self.assertNotEqual(result.returncode, 0)
			self.assertIn('Invalid VeroScript module root', result.stderr)

	def test_source_header_identifies_module_without_release_configuration(self):
		sys.path.insert(0, str(TOOLS))
		from veroscript_paths import module_root
		with tempfile.TemporaryDirectory(prefix='vero source module ') as temporary:
			repo = Path(temporary).resolve()
			module = repo / 'Engine Sources'
			header = module / 'VeroScript/Public/VeroScriptVersion.h'
			header.parent.mkdir(parents=True)
			header.write_text('// Source module marker\n')
			unrelated = repo / 'Other Product/ReleaseVersions.json'
			unrelated.parent.mkdir()
			unrelated.write_text('{}')
			self.assertEqual(module, module_root(repo))
			self.assertEqual(module, module_root(module))
			if os.name == 'nt':
				quote = lambda value: "'" + str(value).replace("'", "''") + "'"
				command = (f"$ErrorActionPreference='Stop'; . {quote(TOOLS / 'veroscript-output.ps1')}; "
						   f"Get-VeroScriptModuleRoot {quote(repo)}")
				result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command],
										check=True, capture_output=True, text=True)
				self.assertEqual(module, Path(result.stdout.strip()))

	def test_python_metadata_uses_source_header_in_renamed_module(self):
		with tempfile.TemporaryDirectory(prefix='vero Python metadata ') as temporary:
			repo = Path(temporary).resolve()
			module = repo / 'Engine Sources'
			header = module / 'VeroScript/Public/VeroScriptVersion.h'
			header.parent.mkdir(parents=True)
			header.write_text('constexpr Version VEROSCRIPT_VERSION = {2, 3, 4};\n')
			readme = module / 'Extensions/Language/Python/VeroScriptPython/README.md'
			readme.parent.mkdir(parents=True)
			readme.write_text('# SDK\n')
			unrelated = repo / 'Other Product/ReleaseVersions.json'
			unrelated.parent.mkdir()
			unrelated.write_text('{}')
			provider = REPO / 'Extensions/Language/Python/Resources/Tools/Packaging'
			probe = ('import sys, json; sys.path.insert(0, sys.argv[1]); '
					 'from veroscript_python_metadata import dynamic_metadata; '
					 'print(json.dumps(dynamic_metadata(None, None)))')
			result = subprocess.run([sys.executable, '-c', probe, str(provider)], cwd=repo,
									check=True, capture_output=True, text=True)
			metadata = json.loads(result.stdout)
			self.assertEqual('2.3.4', metadata['version'])
			self.assertNotIn('readme', metadata)

	def test_python_metadata_supports_module_project_and_independent_sdist(self):
		provider = REPO / 'Extensions/Language/Python/Resources/Tools/Packaging'
		probe = ('import sys, json; sys.path.insert(0, sys.argv[1]); '
				 'from veroscript_python_metadata import dynamic_metadata; '
				 'print(json.dumps(dynamic_metadata(None, None)))')
		with tempfile.TemporaryDirectory(prefix='vero Python package ') as temporary:
			workspace = Path(temporary).resolve()
			checkout = workspace / 'Renamed Checkout'
			package = checkout / 'Extensions/Language/Python'
			sdist = workspace / 'Source Archive'
			for source, project in ((checkout, package), (sdist / '_native', sdist)):
				with self.subTest(project=project):
					project.mkdir(parents=True, exist_ok=True)
					header = source / 'VeroScript/Public/VeroScriptVersion.h'
					header.parent.mkdir(parents=True)
					header.write_text('constexpr Version VEROSCRIPT_VERSION = {3, 4, 5};\n')
					readme = source / 'Extensions/Language/Python/VeroScriptPython/README.md'
					readme.parent.mkdir(parents=True)
					readme.write_text('# Module package\n')
					result = subprocess.run([sys.executable, '-c', probe, str(provider)], cwd=project,
											check=True, capture_output=True, text=True)
					metadata = json.loads(result.stdout)
					self.assertEqual('3.4.5', metadata['version'])
					self.assertNotIn('readme', metadata)

	def test_python_package_license_matches_repository_notice(self):
		self.assertEqual((REPO / 'LICENSE').read_bytes(),
						 (REPO / 'Extensions/Language/Python/LICENSE').read_bytes())

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for Python source-package configuration')
	def test_python_tools_configure_and_install_without_authored_content(self):
		with tempfile.TemporaryDirectory(prefix='vero functional source ') as temporary:
			root = Path(temporary)
			header = root / 'VeroScript/Public/VeroScriptVersion.h'
			header.parent.mkdir(parents=True)
			shutil.copy2(MODULE / 'VeroScript/Public/VeroScriptVersion.h', header)
			names = ('VeroScriptPythonFrontend', 'VeroScriptPythonBindGen', 'VeroScriptPythonTranspiler')
			for name in names:
				shutil.copytree(MODULE / 'Extensions/Language/Python' / name, root / name,
								ignore=shutil.ignore_patterns('README*', 'Examples', 'tests', '__pycache__', '.veroscript'))
			(root / 'CMakeLists.txt').write_text(
				'cmake_minimum_required(VERSION 3.25)\nproject(PythonSourcePackage NONE)\n'
				'set(VEROSCRIPT_MODULE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")\n'
				'function(vs_set_solution_folder)\nendfunction()\n'
				'function(vs_localization_roots output)\nset(${output} "" PARENT_SCOPE)\nendfunction()\n'
				+ ''.join(f'add_subdirectory({name})\n' for name in names))
			build = root / 'build'
			install = root / 'install'
			for command in (['cmake', '-S', str(root), '-B', str(build)],
							['cmake', '--install', str(build), '--component', 'VeroScriptPythonTools', '--prefix', str(install)]):
				result = subprocess.run(command, text=True, capture_output=True)
				self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			from veroscript_products import verify_functional_payload
			files = [file.relative_to(install).as_posix() for file in install.rglob('*') if file.is_file()]
			verify_functional_payload(files)
			for name in ('frontend', 'bindgen', 'transpiler'):
				self.assertIn(f'share/veroscript/python-tools/veroscript_python_{name}/__main__.py', files)

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for build entry tests')
	def test_module_owned_cmake_entry_supports_renaming_and_build_modes(self):
		with tempfile.TemporaryDirectory(prefix='vero build root ') as temporary:
			repo = Path(temporary).resolve()
			module = repo / 'Engine Sources'
			shutil.copytree(MODULE / 'Resources/Tools/CMake', module / 'Resources/Tools/CMake')
			(repo / 'Resources/Tools/CMake').mkdir(parents=True)
			shutil.copy2(REPO / 'Resources/Tools/CMake/VeroScriptHelpers.cmake', repo / 'Resources/Tools/CMake')
			fixture_modules = {
				'Cpp/VSCppNative': 'VSCppNative',
				'Unreal/VSCppUnreal': 'VSCppUnreal',
				'Cpp/VSCppNativeCli': 'VSCppNativeCli',
				'Engine Sources/VeroScript': 'VeroScript',
				'Engine Sources/Extensions/Language/Cpp/VeroScriptCpp': 'VeroScriptCpp',
				'Engine Sources/VeroScriptInterop': 'VeroScriptInterop',
				'Engine Sources/Extensions/Language/Python/VeroScriptPython': 'VeroScriptPython',
				'Engine Sources/Extensions/Language/Python/VeroScriptPythonFrontend': 'VeroScriptPythonFrontend',
				'Engine Sources/Extensions/Language/Python/VeroScriptPythonAot': 'VeroScriptPythonAot',
				'Engine Sources/Extensions/Platform/WebAssembly/VeroScriptWebAssembly': 'VeroScriptWebAssembly',
				'Engine Sources/VeroScriptTooling': 'VeroScriptTooling',
				'Engine Sources/VeroScriptCli': 'VeroScriptCli',
				'Engine Sources/Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/Embed': 'VeroScriptEmbedExample',
			}
			for route, target in fixture_modules.items():
				directory = repo / route
				directory.mkdir(parents=True, exist_ok=True)
				(directory / 'CMakeLists.txt').write_text(
					f'add_custom_target({target})\n'
					f'set_property(GLOBAL APPEND PROPERTY fixture_modules {target})\n')
			(repo / 'CMakeLists.txt').write_text(
				'cmake_minimum_required(VERSION 3.25)\nproject(Fixture NONE)\n'
				'include("${CMAKE_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptHelpers.cmake")\n'
				'vs_add_root_subdirectories()\nvs_add_aggregate_build_targets()\n'
				'get_property(modules GLOBAL PROPERTY fixture_modules)\n'
				'if(NOT TARGET build_native)\nmessage(FATAL_ERROR "Missing aggregate target")\nendif()\n'
				'file(WRITE "${CMAKE_BINARY_DIR}/modules.txt" "${modules}")\n'
				'file(WRITE "${CMAKE_BINARY_DIR}/root.txt" "${VEROSCRIPT_MODULE_ROOT}")\n')
			modes = {
				'full': ([], {'VeroScriptCli', 'VeroScriptEmbedExample'}, {'VSCppNative', 'VSCppUnreal', 'VSCppNativeCli'}),
				'python': (['-DVS_BUILD_VEROSCRIPT_PYTHON_ONLY=ON'],
						   {'VeroScriptPython', 'VeroScriptPythonAot', 'VeroScriptInterop'},
						   {'VSCppNative', 'VeroScriptWebAssembly', 'VeroScriptCli'}),
				'web': (['-DVS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY=ON'],
						{'VeroScript', 'VeroScriptCpp', 'VeroScriptWebAssembly'},
						{'VSCppNative', 'VeroScriptPython', 'VeroScriptInterop'}),
			}
			for mode, (arguments, required, absent) in modes.items():
				with self.subTest(mode=mode):
					build = repo / ('build-' + mode)
					result = subprocess.run(['cmake', '-S', str(repo), '-B', str(build), *arguments],
											text=True, capture_output=True)
					self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
					modules = (build / 'modules.txt').read_text().split(';')
					self.assertTrue(required <= set(modules), modules)
					self.assertFalse(absent & set(modules), modules)
					self.assertLess(modules.index('VeroScript'), modules.index('VeroScriptCpp'))
					self.assertEqual(Path((build / 'root.txt').read_text()), module)

			# A second build entry must be rejected unless the caller selects a module.
			other = repo / 'Other Sources/Resources/Tools/CMake'
			shutil.copytree(module / 'Resources/Tools/CMake', other)
			result = subprocess.run(['cmake', '-S', str(repo), '-B', str(repo / 'build-ambiguous')],
									text=True, capture_output=True)
			self.assertNotEqual(result.returncode, 0)
			self.assertIn('Expected one VeroScript build module', result.stderr)
			result = subprocess.run(['cmake', '-S', str(repo), '-B', str(repo / 'build-explicit'),
									 '-DVEROSCRIPT_MODULE_ROOT=Engine Sources'], text=True, capture_output=True)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

	@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for build entry tests')
	def test_standalone_build_requires_its_own_source_module(self):
		with tempfile.TemporaryDirectory(prefix='missing source module ') as temporary:
			repo = Path(temporary)
			(repo / 'Resources/Tools/CMake').mkdir(parents=True)
			shutil.copy2(REPO / 'Resources/Tools/CMake/VeroScriptHelpers.cmake', repo / 'Resources/Tools/CMake')
			(repo / 'CMakeLists.txt').write_text(
				'cmake_minimum_required(VERSION 3.25)\nproject(Fixture NONE)\n'
				'include("${CMAKE_SOURCE_DIR}/Resources/Tools/CMake/VeroScriptHelpers.cmake")\n'
				'vs_add_root_subdirectories()\n')
			result = subprocess.run(['cmake', '-S', str(repo), '-B', str(repo / 'build')],
									text=True, capture_output=True)
			self.assertNotEqual(result.returncode, 0)
			self.assertIn('Expected one VeroScript build module', result.stderr)

	def test_cibuildwheel_test_command_preserves_a_project_path_with_spaces(self):
		config = tomllib.loads((REPO / 'Extensions/Language/Python/pyproject.toml').read_text(encoding='utf-8'))
		command = config['tool']['cibuildwheel']['test-command'][0]
		with tempfile.TemporaryDirectory(prefix='vero wheel project ') as temporary:
			root = Path(temporary)
			package = root / 'Extensions/Language/Python'
			runner = package / 'Resources/Tools/Packaging/test-wheel.py'
			runner.parent.mkdir(parents=True)
			runner.write_text('import json, sys\nprint(json.dumps(sys.argv[1:]))\n')
			command = command.format(project=root.as_posix(), package=package.as_posix()).replace('python ', f'"{sys.executable}" ', 1)
			result = subprocess.run(command, shell=True, text=True, capture_output=True)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertEqual(json.loads(result.stdout), ['--project', package.as_posix()])

	def test_shell_sources_keep_lf_line_endings(self):
		for path in TOOLS.rglob('*.sh'):
			with self.subTest(path=path):
				self.assertNotIn(b'\r', path.read_bytes(),
								 f'{path}: Bash scripts must use LF on every host')

	def test_non_ascii_powershell_sources_keep_utf8_bom_for_windows_powershell(self):
		for path in TOOLS.rglob('*.ps1'):
			data = path.read_bytes()
			text = data.decode('utf-8-sig')
			if any(ord(character) > 127 for character in text):
				self.assertTrue(data.startswith(b'\xef\xbb\xbf'),
								f'{path}: Windows PowerShell 5.1 needs a BOM for UTF-8 non-ASCII text')

	def test_default_version_configuration_is_independent_of_working_directory(self):
		expected = json.loads((TOOLS / 'ReleaseVersions.json').read_text(encoding='utf-8-sig'))
		with tempfile.TemporaryDirectory() as temporary:
			result = subprocess.run([sys.executable, str(TOOLS / 'veroscript_business_release.py'), 'versions'],
									cwd=temporary, text=True, capture_output=True)
		self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
		self.assertEqual(json.loads(result.stdout), expected)
		if os.name == 'nt':
			quote = lambda value: "'" + str(value).replace("'", "''") + "'"
			command = (f"$ErrorActionPreference='Stop'; . {quote(TOOLS / 'veroscript-output.ps1')}; "
					   f"Get-VeroScriptReleaseConfiguration {quote(REPO)} | ConvertTo-Json -Depth 4")
			with tempfile.TemporaryDirectory() as temporary:
				result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', command],
										cwd=temporary, check=True, text=True, capture_output=True)
			actual = json.loads(result.stdout)
			self.assertEqual({key: actual[key] for key in expected}, expected)
			self.assertEqual(actual['businessPaths']['Jit'], 'Modules/Execution/Jit')
			self.assertEqual(actual['businessPaths']['Python'], 'Extensions/Language/Python')
			self.assertEqual(actual['businessPaths']['VSCode'], 'Extensions/Editor/VSCode/VSCode')
			self.assertNotIn('Jit', actual['installerBusinesses'])

	def renamed_tools(self, root):
		module = root / 'Engine Sources'
		tools = module / 'Resources/Tools/Packaging'
		shutil.copytree(TOOLS, tools, ignore=shutil.ignore_patterns('tests', '__pycache__'))
		self.assertFalse((module / 'ReleaseVersions.json').exists())
		return module, tools

	def test_renamed_module_publishes_and_verifies_a_distribution_from_another_cwd(self):
		import zipfile
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			module, tools = self.renamed_tools(root)
			incoming = root / 'incoming'
			incoming.mkdir()
			version = json.loads((tools / 'ReleaseVersions.json').read_text(encoding='utf-8-sig'))['businesses']['Python']
			with zipfile.ZipFile(incoming / f'veroscript-{version}-cp311-cp311-win_amd64.whl', 'w') as archive:
				archive.writestr(f'veroscript-{version}.dist-info/METADATA', f'Name: veroscript\nVersion: {version}\n')
			for arguments in ([str(tools / 'veroscript_business_release.py'), 'versions'],
							  [str(tools / 'veroscript_business_release.py'), 'publish-distributions', '--candidate', str(incoming)],
							  [str(tools / 'verify-veroscript-release.py'), '--target', 'win-x64']):
				result = subprocess.run([sys.executable, *arguments], cwd=incoming, text=True, capture_output=True)
				self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertTrue((module / f'Release/Extensions/Language/Python/win-x64/{version}').is_dir())
			self.assertFalse((root / 'Script').exists())
			if os.name == 'nt':
				command = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
						   str(tools / 'verify-veroscript-release.ps1'), '-Target', 'win-x64',
						   '-PythonExecutable', sys.executable]
				for arguments in ([], ['-Version', version]):
					result = subprocess.run(command + arguments, cwd=incoming, text=True, capture_output=True)
					self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
				result = subprocess.run(command + ['-Version', '9.9.9'],
										cwd=incoming, text=True, capture_output=True)
				self.assertNotEqual(result.returncode, 0)
				self.assertIn('Requested version', result.stderr)

	def test_renamed_module_documentation_check_and_publication(self):
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary).resolve()
			module, tools = self.renamed_tools(repo)
			settings = {'schemaVersion': 1, 'locales': ['en-US'], 'resources': [],
						'modules': ['$module/Core/Resources/Docs/manifest.json'],
						'repositoryBaseUrl': 'https://example.test/repo', 'repositoryRef': 'main'}
			documents = {
				'Resources/Docs/documentation.json': json.dumps(settings),
				'Resources/Docs/README.md': '# Source documentation\n',
				'Resources/Docs/Overview/en-US/index.md': '# Overview\n',
				'Core/Resources/Docs/manifest.json': json.dumps({'schemaVersion': 1, 'module': 'Core',
					'developer': True, 'user': [{'source': 'User/{locale}', 'output': '{locale}'}]}),
				'Core/README.md': '# Core\n\n[Documentation](Resources/Docs/README.md)\n',
				'Core/Resources/Docs/README.md': '[User](User/en-US/index.md)\n\n[Developer](Developer/en-US/index.md)\n',
				'Core/Resources/Docs/User/en-US/index.md': '# User\n',
				'Core/Resources/Docs/Developer/en-US/index.md': '# Developer\n',
			}
			for route, content in documents.items():
				path = module / route
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_text(content, encoding='utf-8')
			commands = [['check'], ['check-collection']]
			for audience in ('All', 'User'):
				output = repo / '.tmp' / audience
				commands.extend([[command, '--audience', audience, '--output', str(output)]
								 for command in ('publish', 'verify-publication')])
			for arguments in commands:
				result = subprocess.run([sys.executable, str(tools / 'veroscript_docs.py'), *arguments],
										cwd=temporary, text=True, capture_output=True)
				self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertFalse((module / 'Resources/Docs/_generated').exists())
			self.assertEqual([], list((repo / '.tmp').glob('docs-collection-*')))

	def test_full_unix_packager_loads_the_current_documentation_contract(self):
		with tempfile.TemporaryDirectory() as temporary:
			_, tools = self.renamed_tools(Path(temporary))
			result = subprocess.run([sys.executable, str(tools / 'package-veroscript-all.py'), '--help'],
									cwd=temporary, text=True, capture_output=True)
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertIn('--skip-build', result.stdout)

	@unittest.skipIf(os.name == 'nt', 'Native Bash installer entry requires a Unix host')
	def test_renamed_bash_installer_reads_versions_before_host_validation(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			_, tools = self.renamed_tools(root)
			result = subprocess.run(['bash', str(tools / 'package-veroscript-installer.sh'), '--runtime', 'win-x64'],
									cwd=root, text=True, capture_output=True)
			self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
			self.assertIn('Native VeroScript tools cannot be relabeled', result.stderr)

	@unittest.skipIf(os.name == 'nt', 'Native Bash build entry requires a Unix host')
	def test_webassembly_build_uses_repository_root_from_arbitrary_directory(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			fakebin = root / 'fake-bin'
			fakebin.mkdir()
			command = fakebin / 'emcmake'
			command.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\nexit 73\n')
			command.chmod(0o755)
			env = dict(os.environ, PATH=str(fakebin) + os.pathsep + os.environ['PATH'],
					   VEROSCRIPT_WEBASSEMBLY_BUILD_DIR=str(root / 'output'))
			result = subprocess.run([shutil.which('bash'), str(TOOLS / 'build-veroscript-webassembly.sh')],
									cwd=root, env=env, text=True, capture_output=True)
		self.assertEqual(result.returncode, 73, result.stdout + result.stderr)
		arguments = result.stdout.splitlines()
		self.assertEqual(Path(arguments[arguments.index('-S') + 1]), REPO)

	@unittest.skipUnless(os.name == 'nt', 'PowerShell build entry requires Windows')
	def test_powershell_webassembly_build_uses_renamed_checkout_from_another_cwd(self):
		with tempfile.TemporaryDirectory(prefix='vero web build ') as temporary:
			workspace = Path(temporary).resolve()
			repo = workspace / 'Renamed Checkout'
			tools = repo / 'Resources/Tools/Packaging'
			tools.mkdir(parents=True)
			shutil.copy2(TOOLS / 'build-veroscript-webassembly.ps1', tools)
			fakebin = workspace / 'fake-bin'
			fakebin.mkdir()
			captured = workspace / 'arguments.json'
			recorder = fakebin / 'record.py'
			recorder.write_text('import json, sys\nfrom pathlib import Path\n'
								f'Path({str(captured)!r}).write_text(json.dumps(sys.argv[1:]))\n',
								encoding='utf-8')
			(fakebin / 'emcmake.bat').write_text(
				f'@"{sys.executable}" "{recorder}" %*\n@exit /b 73\n', encoding='utf-8')
			# The wrapper only passes these paths to emcmake before the stop code.
			for name in ('cmake.exe', 'ninja.exe'):
				(fakebin / name).touch()
			env = dict(os.environ, PATH=str(fakebin) + os.pathsep + os.environ['PATH'])
			for build_directory in (None, '.tmp/custom web output'):
				with self.subTest(build_directory=build_directory):
					command = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
							   '-File', str(tools / 'build-veroscript-webassembly.ps1')]
					if build_directory is not None:
						command.extend(['-BuildDirectory', build_directory])
					result = subprocess.run(command, cwd=workspace, env=env,
											text=True, capture_output=True)
					self.assertNotEqual(result.returncode, 0)
					self.assertIn('exit code 73', result.stderr)
					arguments = json.loads(captured.read_text(encoding='utf-8'))
					self.assertEqual(repo, Path(arguments[arguments.index('-S') + 1]))
					self.assertEqual(repo / (build_directory or 'build/webassembly'),
									 Path(arguments[arguments.index('-B') + 1]))
					self.assertIn(f'-DVEROSCRIPT_MODULE_ROOT={repo}', arguments)


if __name__ == '__main__':
	unittest.main()
