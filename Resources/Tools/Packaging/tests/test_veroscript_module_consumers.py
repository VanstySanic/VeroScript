# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import tempfile
from contextlib import chdir
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_module_consumers import verify_consumers


class ModuleConsumerTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory()
		self.addCleanup(temporary.cleanup)
		self.work = Path(temporary.name)
		self.candidate = self.work / 'candidate'
		(self.candidate / 'modules').mkdir(parents=True)
		with zipfile.ZipFile(self.candidate / 'modules/vs-json-0.1.1-linux-x64.zip', 'w') as archive:
			archive.writestr('vs-json-0.1.1-linux-x64/lib/fixture.a', b'fixture')
		self.build = self.work / 'build'
		self.build.mkdir()
		self.sdk = self.work / 'Core SDK'
		self.settings = {'CMAKE_COMMAND': 'cmake', 'CMAKE_GENERATOR': 'Ninja'}

	def consume(self, target='linux-x64', bindings=True, builds=None):
		with patch('veroscript_module_consumers.build_cache', return_value=self.settings) as cache, \
				patch('veroscript_module_consumers.scratch_directory', return_value=self.work), \
				patch('veroscript_module_consumers.subprocess.run') as run:
			verify_consumers(self.work, self.candidate, builds or {'Json': self.build}, ['Json'], target,
							 self.sdk if bindings else None, bindings=bindings)
			cache.assert_called_once_with(self.build)
			return run.call_args_list[0].args[0]

	def test_script_consumers_use_the_installed_core_vm(self):
		for target, name in (('win-x64', 'VeroScriptVmBytecode.dll'),
							 ('linux-x64', 'libVeroScriptVmBytecode.so'),
							 ('macos-arm64', 'libVeroScriptVmBytecode.dylib')):
			with self.subTest(target=target):
				vm = self.sdk / 'bin/Vm' / name
				vm.parent.mkdir(parents=True, exist_ok=True)
				vm.write_bytes(b'backend fixture')
				arguments = self.consume(target)
				self.assertIn('-DVEROSCRIPT_TEST_VM_LIBRARY=' + str(vm.resolve()), arguments)

	def test_explicit_test_backend_is_preserved(self):
		vm = self.work / 'custom VM.so'
		vm.write_bytes(b'backend fixture')
		self.settings['VEROSCRIPT_TEST_VM_LIBRARY'] = str(vm)
		self.assertIn('-DVEROSCRIPT_TEST_VM_LIBRARY=' + str(vm.resolve()), self.consume())

	def test_relative_test_backend_uses_its_configured_source_directory(self):
		source = self.work / 'host source'
		vm = source / 'backends/VM library.so'
		vm.parent.mkdir(parents=True)
		vm.write_bytes(b'backend fixture')
		self.settings.update(CMAKE_HOME_DIRECTORY=str(source), VEROSCRIPT_TEST_VM_LIBRARY='backends/VM library.so')
		self.assertIn('-DVEROSCRIPT_TEST_VM_LIBRARY=' + str(vm.resolve()), self.consume())

	def test_relative_core_sdk_keeps_the_callers_working_directory(self):
		vm = self.sdk / 'bin/Vm/libVeroScriptVmBytecode.so'
		vm.parent.mkdir(parents=True)
		vm.write_bytes(b'backend fixture')
		self.settings['CMAKE_HOME_DIRECTORY'] = str(self.work / 'different source')
		with chdir(self.work):
			self.sdk = Path('Core SDK')
			self.assertIn('-DVEROSCRIPT_TEST_VM_LIBRARY=' + str(vm.resolve()), self.consume())

	def test_only_selected_build_supplies_consumer_settings(self):
		arguments = self.consume(bindings=False, builds={'Http': self.work / 'unrelated', 'Json': self.build})
		self.assertIn('-DVEROSCRIPT_MODULES=Json', arguments)

	def test_script_consumer_preserves_the_native_build_language_standard(self):
		vm = self.work / 'custom VM.so'
		vm.write_bytes(b'backend fixture')
		self.settings.update(VEROSCRIPT_TEST_VM_LIBRARY=str(vm), CMAKE_CXX_STANDARD='20')
		arguments = self.consume()
		self.assertIn('-DCMAKE_CXX_STANDARD=20', arguments)

	def test_script_consumers_reject_missing_backend_or_directory(self):
		for directory in (False, True):
			with self.subTest(directory=directory):
				if directory:
					(self.sdk / 'bin/Vm/libVeroScriptVmBytecode.so').mkdir(parents=True)
				with self.assertRaisesRegex(ValueError, 'matching VM backend'):
					self.consume()

	def test_native_consumers_need_neither_core_nor_vm(self):
		arguments = self.consume(bindings=False)
		self.assertIn('-DVEROSCRIPT_TEST_BINDINGS=OFF', arguments)
		self.assertFalse(any('VEROSCRIPT_TEST_VM_LIBRARY' in argument for argument in arguments))
		self.assertFalse(any(str(self.sdk) in argument for argument in arguments))


if __name__ == '__main__':
	unittest.main()
