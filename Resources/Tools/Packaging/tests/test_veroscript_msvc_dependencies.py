# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_msvc_dependencies import verify_msvc_dependencies


@unittest.skipUnless(os.name == 'nt', 'MSVC Windows cache guard')
class MsvcDependencyTests(unittest.TestCase):
	def test_generated_identity_header_must_be_tracked_and_current(self):
		with tempfile.TemporaryDirectory() as directory:
			build = Path(directory)
			header = build / 'VeroScript/Generated/VeroScriptJitBuild.h'
			obj = build / 'VeroScriptPackageManager/CMakeFiles/VeroScriptPackageRuntime.dir/Private/PackageFilesystem.cpp.obj'
			for path in (header, obj):
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_bytes(b'fixture')
			os.utime(header, ns=(1000000000, 1000000000))
			os.utime(obj, ns=(2000000000, 2000000000))
			settings = {'CMAKE_GENERATOR': 'Ninja', 'CMAKE_CXX_COMPILER': 'cl.exe', 'CMAKE_MAKE_PROGRAM': 'fixture-ninja'}
			for output in ('object: #deps 0\n', 'object: #deps 1\n    unrelated.h\n'):
				with self.subTest(output=output), patch('veroscript_msvc_dependencies.subprocess.run', return_value=subprocess.CompletedProcess([], 0, output, '')):
					with self.assertRaisesRegex(ValueError, 'does not track'):
						verify_msvc_dependencies(build, settings)
			valid = 'object: #deps 1\n    VeroScript/Generated/VeroScriptJitBuild.h\n'
			with patch('veroscript_msvc_dependencies.subprocess.run', return_value=subprocess.CompletedProcess([], 0, valid, '')) as command:
				verify_msvc_dependencies(build, settings)
				self.assertEqual(command.call_args.kwargs['cwd'], build.resolve())
				os.utime(header, ns=(3000000000, 3000000000))
				with self.assertRaisesRegex(ValueError, 'does not track'):
					verify_msvc_dependencies(build, settings)


if __name__ == '__main__':
	unittest.main()
