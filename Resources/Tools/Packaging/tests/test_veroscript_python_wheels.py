# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import io
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tarfile
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_python_wheels import build_matrix, discover_interpreters, supported_versions, verify_matrix, retire_completed_abi

METADATA = 'Name: veroscript\nVersion: 1.0.0\nRequires-Python: <3.16,>=3.11\n'

def pe(machine=0x8664):
	data = bytearray(128)
	data[:2] = b'MZ'
	struct.pack_into('<I', data, 60, 64)
	data[64:68] = b'PE\0\0'
	struct.pack_into('<H', data, 68, machine)
	return bytes(data)


class PythonWheelMatrixTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory()
		self.addCleanup(temporary.cleanup)
		self.repo = Path(temporary.name)
		header = self.repo / 'VeroScript/Public/VeroScriptVersion.h'
		header.parent.mkdir(parents=True)
		header.write_text('// Fixture module marker\n')
		self.project = self.repo / 'Extensions/Language/Python/pyproject.toml'
		self.project.parent.mkdir(parents=True)
		self.project.write_text('[project]\nrequires-python=">=3.11,<3.16"\n[tool.cibuildwheel]\nbuild="cp31{1,2,3,4,5}-*"\n')
		(self.repo / 'LICENSE').write_text('Project license notice\n')
		self.output = self.repo / 'dist'
		self.output.mkdir()
		self.sdist = self.output / 'veroscript-1.0.0.tar.gz'
		with tarfile.open(self.sdist, 'w:gz') as archive:
			data = METADATA.encode()
			entry = tarfile.TarInfo('veroscript-1.0.0/PKG-INFO')
			entry.size = len(data)
			archive.addfile(entry, io.BytesIO(data))

	def wheel(self, minor, *, tag='win_amd64', machine=0x8664, metadata_tag=None, extension_minor=None, license_text='Project license notice\n', metadata=METADATA):
		abi = f'cp3{minor}'
		path = self.output / f'veroscript-1.0.0-{abi}-{abi}-{tag}.whl'
		with zipfile.ZipFile(path, 'w') as archive:
			archive.writestr('veroscript-1.0.0.dist-info/METADATA', metadata)
			archive.writestr('veroscript-1.0.0.dist-info/WHEEL', 'Tag: ' + (metadata_tag or f'{abi}-{abi}-{tag}') + '\n')
			archive.writestr('veroscript-1.0.0.dist-info/licenses/LICENSE', license_text)
			archive.writestr(f'veroscript/_veroscript.cp3{extension_minor or minor}-win_amd64.pyd', pe(machine))
			for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
				archive.writestr('veroscript/' + name, pe())
			archive.writestr('veroscript/licenses/MicrosoftVisualCpp/NOTICE.txt', 'Microsoft runtime notice')
		return path

	def complete(self):
		return [self.sdist, *(self.wheel(minor) for minor in range(11, 16))]

	def test_declared_support_and_ci_selection_must_agree(self):
		self.assertEqual(supported_versions(self.repo), ('3.11', '3.12', '3.13', '3.14', '3.15'))
		self.project.write_text(self.project.read_text().replace('cp31{1,2,3,4,5}', 'cp31{1,2,3,4}'))
		with self.assertRaisesRegex(ValueError, 'selection'):
			supported_versions(self.repo)

	def test_complete_matrix_verifies_without_source_checkout(self):
		paths = self.complete()
		verify_matrix(paths, self.repo, 'win-x64', '1.0.0')
		verify_matrix(paths, None, 'win-x64', '1.0.0', ('3.11', '3.12', '3.13', '3.14', '3.15'))

	def test_wheel_and_cross_reference_cannot_exclude_a_declared_minor(self):
		from veroscript_desktop_cross_wheels import reference_metadata
		paths = self.complete()
		versions = supported_versions(self.repo)
		reference_metadata(paths[-1], self.sdist, '1.0.0', versions)
		self.wheel(15, metadata=METADATA.replace('<3.16', '<3.15'))
		with self.assertRaisesRegex(ValueError, 'Requires-Python'):
			verify_matrix(paths, self.repo, 'win-x64', '1.0.0')
		with self.assertRaisesRegex(ValueError, 'Requires-Python'):
			verify_matrix(paths[-1:], self.repo, 'win-x64', '1.0.0', require_complete=False)
		with self.assertRaisesRegex(ValueError, 'Requires-Python'):
			reference_metadata(paths[-1], self.sdist, '1.0.0', versions)

	def test_macos_wheel_tag_cannot_predate_its_binary_deployment_version(self):
		library = struct.pack('<14I', 0xfeedfacf, 0x0100000c, 0, 6, 1, 24, 0, 0,
			0x32, 24, 1, 26 << 16, 26 << 16, 0)
		for major in (15, 26):
			platform = f'macosx_{major}_0_arm64'
			path = self.output / f'veroscript-1.0.0-cp315-cp315-{platform}.whl'
			with zipfile.ZipFile(path, 'w') as archive:
				archive.writestr('veroscript-1.0.0.dist-info/METADATA', METADATA)
				archive.writestr('veroscript-1.0.0.dist-info/WHEEL', f'Tag: cp315-cp315-{platform}\n')
				archive.writestr('veroscript-1.0.0.dist-info/licenses/LICENSE', 'Project license notice\n')
				archive.writestr('veroscript/_veroscript.cpython-315-darwin.so', library)
			if major == 15:
				with self.assertRaisesRegex(ValueError, 'deployment tag is older'):
					verify_matrix([path], self.repo, 'macos-arm64', '1.0.0', require_complete=False)
			else:
				verify_matrix([path], self.repo, 'macos-arm64', '1.0.0', require_complete=False)

	def test_windows_wheel_requires_the_importable_target_extension_suffix(self):
		path = self.wheel(15, tag='win_arm64', machine=0xAA64)
		with self.assertRaisesRegex(ValueError, 'extension ABI mismatch'):
			verify_matrix([path], self.repo, 'win-arm64', '1.0.0', require_complete=False)

	def test_source_distribution_rejects_android_build_caches(self):
		paths = self.complete()
		for directory in ('.cxx', '.gradle', '.externalNativeBuild', '.CXX'):
			with self.subTest(directory=directory):
				with tarfile.open(self.sdist, 'w:gz') as archive:
					for name, data in (
						('PKG-INFO', METADATA.encode()),
						('_native/Modules/Execution/VeroScriptAsmJit/Resources/Tools/Android/' + directory + '/cache.json', b'{}')):
						entry = tarfile.TarInfo('veroscript-1.0.0/' + name)
						entry.size = len(data)
						archive.addfile(entry, io.BytesIO(data))
				with self.assertRaisesRegex(ValueError, 'source distribution contains build cache'):
					verify_matrix(paths, self.repo, 'win-x64', '1.0.0')

	def test_missing_abi_and_source_distribution_are_rejected(self):
		paths = self.complete()
		for selection in (paths[:-1], paths[1:]):
			with self.assertRaisesRegex(ValueError, 'Incomplete'):
				verify_matrix(selection, self.repo, 'win-x64', '1.0.0')

	def test_source_distribution_requires_referenced_cross_build_tools(self):
		from veroscript_python_wheels import verify_source_distribution
		base = 'veroscript-1.0.0/_native/Extensions/Language/Python/'
		cross = base + 'VeroScriptPython/Resources/Tools/CMake/VeroScriptPythonDesktopCross.cmake'
		for complete in (False, True):
			with tarfile.open(self.sdist, 'w:gz') as archive:
				names = [cross]
				if complete:
					names += [base+'Resources/Tools/Desktop/'+name for name in ('desktop_sdk.py','CMakeLists.txt')]
				for name in names:
					entry = tarfile.TarInfo(name);entry.size = 1;archive.addfile(entry,io.BytesIO(b'x'))
			if complete:
				verify_source_distribution(self.sdist)
			else:
				with self.assertRaisesRegex(ValueError,'missing desktop cross-build tools'):
					verify_source_distribution(self.sdist)

	def test_wrong_platform_native_abi_metadata_and_license_are_rejected(self):
		cases = ({'tag': 'win_arm64'}, {'machine': 0xAA64}, {'extension_minor': 14},
				 {'metadata_tag': 'cp311-cp311-win_arm64'}, {'license_text': 'stale notice'})
		for options in cases:
			with self.subTest(options=options):
				paths = self.complete()
				paths[1] = self.wheel(11, **options)
				with self.assertRaises(ValueError):
					verify_matrix(paths, self.repo, 'win-x64', '1.0.0')

	def test_recorded_matrix_cannot_be_empty_or_duplicated(self):
		paths = self.complete()
		for versions in ((), ('3.11', '3.11'), ('3.11t',), ('garbage',)):
			with self.assertRaisesRegex(ValueError, 'Invalid recorded'):
				verify_matrix(paths, None, 'win-x64', '1.0.0', versions)

	def test_missing_interpreters_fail_before_build(self):
		identity = {'executable': sys.executable, 'version': '3.14', 'patch': '3.14.7',
					'implementation': 'cpython', 'system': 'Windows', 'machine': 'amd64',
					'bits': 64, 'debug': False, 'freeThreaded': False}
		with patch('veroscript_python_wheels.shutil.which', return_value=None), patch.dict('os.environ', {}, clear=True), patch('veroscript_python_wheels.interpreter_identity', return_value=identity):
			with self.assertRaisesRegex(ValueError, 'Missing CPython interpreters'):
				discover_interpreters(self.repo, 'win-x64', [])

	def test_explicit_wrong_architecture_or_free_threaded_interpreter_is_rejected(self):
		for change in ({'machine': 'arm64'}, {'freeThreaded': True}, {'debug': True}):
			identity = {'executable': 'python311', 'version': '3.11', 'implementation': 'cpython',
						'system': 'Windows', 'machine': 'amd64', 'bits': 64, 'debug': False, 'freeThreaded': False, **change}
			with self.subTest(change=change), patch('veroscript_python_wheels.shutil.which', return_value=None), patch.dict('os.environ', {}, clear=True), patch('veroscript_python_wheels.interpreter_identity', return_value=identity):
				with self.assertRaisesRegex(ValueError, 'does not match'):
					discover_interpreters(self.repo, 'win-x64', ['python311'])

	def test_broken_automatic_candidate_does_not_block_complete_requested_matrix(self):
		requested = [f'requested-python3.{minor}' for minor in range(11, 16)]
		def identity(executable):
			if executable not in requested:
				raise OSError('Stale automatic interpreter path')
			return {'executable': executable, 'version': executable.removeprefix('requested-python'),
					'implementation': 'cpython', 'system': 'Windows', 'machine': 'amd64',
					'bits': 64, 'debug': False, 'freeThreaded': False}
		with patch('veroscript_python_wheels.shutil.which', return_value=None), patch.dict('os.environ', {}, clear=True), patch('veroscript_python_wheels.interpreter_identity', side_effect=identity):
			self.assertEqual(set(discover_interpreters(self.repo, 'win-x64', requested)),
							 {'3.11', '3.12', '3.13', '3.14', '3.15'})

	def test_broken_requested_path_is_rejected_including_environment_selection(self):
		for explicit, environment in ((['broken-python'], {}), ([], {'VEROSCRIPT_PYTHON_EXECUTABLES': 'broken-python'})):
			with self.subTest(explicit=explicit), patch('veroscript_python_wheels.shutil.which', return_value=None), patch.dict('os.environ', environment, clear=True), patch('veroscript_python_wheels.interpreter_identity', side_effect=OSError('Missing executable')):
				with self.assertRaisesRegex(ValueError, 'requested CPython interpreter'):
					discover_interpreters(self.repo, 'win-x64', explicit)

	def test_generator_platform_is_only_supplied_to_visual_studio(self):
		self.project.write_text(self.project.read_text() + '\n[build-system]\nrequires=[]\n')
		for generator in ('Ninja', 'Visual Studio 18 2026'):
			with self.subTest(generator=generator), patch.dict('os.environ', {'CMAKE_GENERATOR_PLATFORM': 'ARM64'}), patch('veroscript_python_wheels.run', side_effect=RuntimeError('Stop before creating a builder')) as invocation:
				with self.assertRaisesRegex(RuntimeError, 'Stop before'):
					build_matrix(self.repo, self.output, self.repo / 'work', 'win-x64', '1.0.0',
								 {'3.11': {'executable': 'python311'}}, self.repo / 'veroscript.exe', 4, generator)
				environment = invocation.call_args.kwargs['environment']
				if generator == 'Ninja':
					self.assertNotIn('CMAKE_GENERATOR_PLATFORM', environment)
				else:
					self.assertEqual(environment['CMAKE_GENERATOR_PLATFORM'], 'x64')
					self.assertIn('/MP4', environment['CL'])

	def test_windows_native_build_avoids_nested_stage_and_cleans_success_or_failure(self):
		self.project.write_text(self.project.read_text() + '\n[build-system]\nrequires=[]\n')
		for failure in (False, True):
			with self.subTest(failure=failure):
				work = self.repo / '.tmp' / ('veroscript-release-stage-' + 'a' * 32) / 'work' / str(failure) / 'python-wheels'
				work.mkdir(parents=True)
				directories = []
				def invoke(*command, **kwargs):
					if '--wheel' not in command:
						return
					settings = command[command.index('--config-setting') + 1]
					native = Path(settings.removeprefix('build-dir='))
					directories.append(native)
					self.assertTrue(native.is_dir())
					self.assertFalse(native.is_relative_to(work))
					source = Path(command[-1])
					self.assertTrue(source.is_dir())
					self.assertFalse(source.is_relative_to(work))
					self.assertTrue(source.is_relative_to(native.parent))
					long_source = source / '_native/Modules/Execution/VeroScriptBytecodeVm/Private/VeroScriptBytecodeVmDiagnostics.cpp'
					self.assertLess(len(str(long_source)), 260)
					tracker = native / 'Extensions/Language/Python/VeroScriptPython/VeroScriptPython.dir/Release/VeroScriptPython.tlog/link-cvtres.write.1.tlog'
					self.assertLess(len(str(tracker)), 260)
					if failure:
						raise RuntimeError('Compiler failure')
					self.wheel(11)
				with patch('veroscript_python_wheels.run', side_effect=invoke), patch('veroscript_python_wheels.verify_matrix'):
					arguments = (self.repo, self.output, work, 'win-x64', '1.0.0',
								 {'3.11': {'executable': 'python311'}}, self.repo / 'veroscript.exe', 4, 'Visual Studio 18 2026')
					if failure:
						with self.assertRaisesRegex(RuntimeError, 'Compiler failure'):
							build_matrix(*arguments)
					else:
						self.assertTrue(build_matrix(*arguments)['success'])
				self.assertEqual(len(directories), 1)
				self.assertFalse(directories[0].exists())

	def test_unix_full_publication_invokes_matrix_with_running_python_and_requested_interpreters(self):
		tools = Path(__file__).resolve().parents[1]
		spec = importlib.util.spec_from_file_location('wheel_test_full_packager', tools / 'package-veroscript-all.py')
		packager = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(packager)
		release = packager.Release.__new__(packager.Release)
		release.repo, release.root, release.work = self.repo, self.output, self.repo / 'work'
		release.target, release.bin = 'linux-x64', self.repo / 'native'
		release.versions, release.settings = {'Python': '1.0.0'}, {'CMAKE_GENERATOR': 'Ninja'}
		release.args = SimpleNamespace(jobs=4, python_executable=['python311', 'python312', 'python313', 'python314'])
		with patch.object(packager, 'run') as execute:
			release.python_wheels()
		command = execute.call_args.args
		self.assertEqual(command[0], sys.executable)
		self.assertEqual(command[command.index('--native-executable') + 1], self.repo / 'native/veroscript')
		self.assertEqual(command[command.index('--output') + 1], self.output / 'distributions/python')
		self.assertEqual(command[command.index('--target') + 1], 'linux-x64')
		self.assertEqual(command[command.index('--jobs') + 1], 4)
		selected = [command[index + 1] for index, argument in enumerate(command) if argument == '--python']
		self.assertEqual(selected, release.args.python_executable)

	def test_unix_non_python_stage_is_accepted_by_the_verifier_selection_gate(self):
		tools = Path(__file__).resolve().parents[1]
		def load(name, filename):
			spec = importlib.util.spec_from_file_location(name, tools / filename)
			module = importlib.util.module_from_spec(spec)
			spec.loader.exec_module(module)
			return module
		packager = load('wheel_test_packager', 'package-veroscript-all.py')
		verifier = load('wheel_test_verifier', 'verify-veroscript-release.py')
		release = packager.Release.__new__(packager.Release)
		release.root, release.v, release.target = self.output, '1.0.0', 'linux-x64'
		release.build_id, release.build_environment = '0' * 64, {}
		release.versions = dict.fromkeys(packager.BUSINESSES, '1.0.0')
		release.ide_build_ids = {}
		release.ide_dependencies = {}
		release.module_build_ids = {}
		release.module_symbol_status = {}
		release.args = SimpleNamespace(release_versions={'nativeVersion': '1.0.0'})
		release.write_release_manifest(False)
		manifest = json.loads((self.output / 'release-manifest.json').read_text())
		# An omitted selectedBusinesses field previously failed the CLI gate
		# before its partial matrix audit could run.
		arguments = ['verify-veroscript-release.py', '--repo-root', str(self.repo),
					 '--release-directory', str(self.output), '--target', 'linux-x64']
		with patch.object(sys, 'argv', arguments), patch.object(verifier, 'verify_release', return_value=manifest) as audit:
			verifier.main()
		self.assertEqual(audit.call_args.kwargs['businesses'], set(packager.BUSINESSES))
		self.assertEqual(audit.call_args.kwargs['partial_businesses'], {'Python'})


class CompletedPythonWorkTests(unittest.TestCase):
	def test_completed_work_retains_artifacts_symbols_and_other_abis(self):
		with tempfile.TemporaryDirectory(prefix='completed Python ABI ') as temporary:
			root = Path(temporary)
			for name in ('source-3.11', 'consumer-3.11', 'build-env-3.11', 'source-3.12'):
				(root / name).mkdir()
				(root / name / 'generated.o').write_bytes(b'object')
			(root / 'source-3.11/native.pdb').write_bytes(b'linkable debug information')
			bundle = root / 'source-3.11/module.dSYM/Contents/Resources/DWARF'
			bundle.mkdir(parents=True)
			(bundle / 'module').write_bytes(b'DWARF')
			(root / 'result.whl').write_bytes(b'verified wheel')
			(root / 'python-wheel-matrix.partial.json').write_text('{}')
			retire_completed_abi(root, '3.11')
			self.assertFalse((root / 'source-3.11').exists())
			self.assertFalse((root / 'consumer-3.11').exists())
			self.assertFalse((root / 'build-env-3.11').exists())
			self.assertEqual((root / 'retained-symbols/3.11/source-3.11/native.pdb').read_bytes(), b'linkable debug information')
			self.assertEqual((root / 'retained-symbols/3.11/source-3.11/module.dSYM/Contents/Resources/DWARF/module').read_bytes(), b'DWARF')
			self.assertTrue((root / 'source-3.12/generated.o').is_file())
			self.assertEqual((root / 'result.whl').read_bytes(), b'verified wheel')
			self.assertTrue((root / 'python-wheel-matrix.partial.json').is_file())

	def test_cleanup_rejects_path_like_abi(self):
		with tempfile.TemporaryDirectory() as temporary:
			with self.assertRaises(ValueError):
				retire_completed_abi(Path(temporary), '../3.11')


if __name__ == '__main__':
	unittest.main()
