# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'nt' and shutil.which('cmake'), 'Windows CMake integration')
class ConsumerRuntimeTests(unittest.TestCase):
	def test_parallel_unicode_sdk_dll_staging(self):
		module = Path(__file__).resolve().parents[1] / 'Consumer/CopyRuntime.cmake'
		with tempfile.TemporaryDirectory(prefix='veroscript-独立 SDK-') as directory:
			root = Path(directory)
			(root / 'provider.c').write_text('__declspec(dllexport) int answer(void) { return 42; }\n')
			(root / 'consumer.c').write_text('__declspec(dllimport) int answer(void);\nint main(void) { return answer() != 42; }\n')
			(root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(UnicodeRuntimeConsumer C)
enable_testing()
include([==[MODULE]==])
add_library(provider SHARED provider.c)
set_target_properties(provider PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/独立 SDK")
foreach(i RANGE 1 8)
  add_executable(consumer${i} consumer.c)
  target_link_libraries(consumer${i} PRIVATE provider)
  set_target_properties(consumer${i} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/consumer${i}")
  veroscript_consumer_copy_runtime(consumer${i} "$<TARGET_FILE:provider>")
  add_test(NAME consumer${i} COMMAND consumer${i})
endforeach()
'''.replace('MODULE', module.as_posix()), encoding='utf-8')
			build = root / 'build'
			subprocess.run(['cmake', '-S', str(root), '-B', str(build), '-G', 'Visual Studio 18 2026', '-A', 'x64'], check=True)
			subprocess.run(['cmake', '--build', str(build), '--config', 'Release', '--parallel', '8'], check=True)
			subprocess.run(['ctest', '--test-dir', str(build), '-C', 'Release', '--output-on-failure', '--no-tests=error'], check=True)
			expected = hashlib.sha256((build / '独立 SDK/Release/provider.dll').read_bytes()).digest()
			for number in range(1, 9):
				self.assertEqual(hashlib.sha256((build / f'consumer{number}/Release/provider.dll').read_bytes()).digest(), expected)
				project = (build / f'consumer{number}.vcxproj').read_text(encoding='utf-8-sig')
				event = project.split('<PostBuildEvent>', 1)[1].split('</PostBuildEvent>', 1)[0]
				self.assertNotIn('独立 SDK', event)


if __name__ == '__main__':
	unittest.main()
