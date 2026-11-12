# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""VM selection in the foundation publication build and its native-only variant."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from veroscript_release import scratch_directory

REPO = TOOLS.parents[2]
SPEC = importlib.util.spec_from_file_location('foundation_package_cli', TOOLS / 'package-veroscript-modules.py')
CLI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CLI)


class BuildObserved(Exception):
	pass


class FoundationVmBuildTests(unittest.TestCase):
	def command(self, business, extra=(), vm='ON'):
		with tempfile.TemporaryDirectory(prefix='foundation-vm-build-', dir=scratch_directory(REPO)) as temporary:
			build = Path(temporary)
			settings = {'CMAKE_HOME_DIRECTORY': ('INTERNAL', REPO.as_posix()),
						'CMAKE_COMMAND': ('INTERNAL', 'cmake'),
						'VEROSCRIPT_BUILD_VM_BACKEND': ('BOOL', vm),
						'VEROSCRIPT_VISUAL_SCRIPT_BINDINGS': ('BOOL', 'ON')}
			(build / 'CMakeCache.txt').write_text(''.join(
				f'{name}:{kind}={value}\n' for name, (kind, value) in settings.items()), encoding='utf-8')
			arguments = ['package-veroscript-modules.py', '--repo-root', str(REPO),
						 '--build-directory', str(build), '--business', business,
						 '--candidate-directory', str(build / 'candidate'), *extra]
			with (patch.object(sys, 'argv', arguments),
				  patch.object(CLI, 'host_target', return_value=CLI.host_target()),
				  patch.object(CLI.subprocess, 'run', side_effect=BuildObserved) as run,
				  patch.object(CLI, 'assemble_modules', side_effect=BuildObserved)):
				# Stop at the real build boundary. These regressions launch no compilers.
				with self.assertRaises(BuildObserved):
					CLI.main()
				return run.call_args.args[0] if run.called else None

	def test_script_publication_builds_the_explicit_test_vm(self):
		for business in ('Platform', 'Graphics', 'Audio', 'Sdl3'):
			for extra in ((), ('--package-tool', 'supplied-package-tool')):
				for vm in ('ON', '1', 'TRUE'):
					with self.subTest(business=business, supplied_tool=bool(extra), vm=vm):
						self.assertIn('VeroScriptBytecodeVmBackend', self.command(business, extra, vm))

	def test_native_and_external_vm_builds_do_not_add_the_official_vm(self):
		self.assertNotIn('VeroScriptBytecodeVmBackend', self.command('Audio', ('--data-only',)))
		self.assertNotIn('VeroScriptBytecodeVmBackend', self.command('Audio', vm='OFF'))

	def test_skip_build_does_not_build_a_vm(self):
		self.assertIsNone(self.command('Audio', ('--skip-build',)))


if __name__ == '__main__':
	unittest.main()
