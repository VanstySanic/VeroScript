# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Keep stale stages and incompatible CPython ABIs out of desktop releases."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

TOOLS=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(TOOLS))
from veroscript_mobile_release import load_tool
from veroscript_release import digest, host_target
from veroscript_business_release import load_versions
from veroscript_desktop_cross_wheels import compiler_arguments,verify_target_python_abi,stage_extension


class DesktopCrossStagingTests(unittest.TestCase):
	def test_nonempty_work_is_rejected_without_touching_either_directory(self):
		entry=load_tool('Resources/Tools/Packaging/package-veroscript-desktop-cross.py',TOOLS.parents[2])
		with tempfile.TemporaryDirectory() as temporary:
			repo=Path(temporary)
			output=repo/'.tmp/candidate'
			work=repo/'.tmp/work'
			work.mkdir(parents=True)
			marker=work/'old-platform.dll'
			marker.write_bytes(b'preserve the previous input')
			args=SimpleNamespace(repo_root=repo,build_directory=repo/'build',target='win-arm64',output=output,work_directory=work)
			with patch.object(entry,'build_cache',return_value={'CMAKE_HOME_DIRECTORY':str(repo)}), patch.object(entry,'load_versions',return_value={'businesses':{},'nativeVersion':'0.1.1'}), patch.object(entry,'safe_output',side_effect=lambda repo,path,**kw:path):
				with self.assertRaisesRegex(ValueError,'work directory must be empty'):
					entry.DesktopCrossRelease(args)
			self.assertEqual(marker.read_bytes(),b'preserve the previous input')
			self.assertFalse(output.exists())

	def test_compiler_is_explicit_or_selected_by_the_cross_toolchain(self):
		base={'CMAKE_GENERATOR':'Ninja'}
		with self.assertRaisesRegex(ValueError,'compiler or an explicit cross toolchain'):
			compiler_arguments(base,'linux-arm64')
		self.assertIn('-DCMAKE_TOOLCHAIN_FILE=target.cmake',compiler_arguments({**base,'CMAKE_TOOLCHAIN_FILE':'target.cmake'},'linux-arm64'))
		self.assertIn('-DCMAKE_SYSTEM_PROCESSOR=ARM64',compiler_arguments({**base,'CMAKE_CXX_COMPILER':'target-cl'},'win-arm64'))

	def test_compiler_configuration_preserves_architecture_and_sysroot(self):
		visual_studio={'CMAKE_GENERATOR':'Visual Studio 18 2026','CMAKE_GENERATOR_PLATFORM':'Win32','CMAKE_GENERATOR_TOOLSET':'host=x64'}
		command=compiler_arguments(visual_studio,'win-x86')
		self.assertEqual(command[command.index('-A')+1],'Win32')
		self.assertEqual(command[command.index('-T')+1],'host=x64')
		command=compiler_arguments({'CMAKE_GENERATOR':'Visual Studio 18 2026'},'win-arm64')
		self.assertEqual(command[command.index('-A')+1],'ARM64')
		linux={'CMAKE_GENERATOR':'Ninja','CMAKE_CXX_COMPILER':'clang++','CMAKE_CXX_COMPILER_TARGET':'aarch64-linux-gnu',
			'CMAKE_SYSROOT':'/target/sysroot','CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN':'/target/toolchain'}
		command=compiler_arguments(linux,'linux-arm64')
		for key in ('CMAKE_CXX_COMPILER_TARGET','CMAKE_SYSROOT','CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN'):
			self.assertIn(f'-D{key}={linux[key]}',command)

	def test_cross_candidate_is_verified_before_recording_acceptance(self):
		entry=load_tool('Resources/Tools/Packaging/package-veroscript-desktop-cross.py',TOOLS.parents[2])
		with tempfile.TemporaryDirectory() as temporary:
			root=Path(temporary)
			candidate=entry.DesktopCrossRelease.__new__(entry.DesktopCrossRelease)
			candidate.repo=TOOLS.parents[2]
			candidate.root=root/'candidate'
			candidate.root.mkdir()
			candidate.target='win-x86'
			candidate.versions=load_versions(candidate.repo)['businesses']
			candidate.native_version='0.1.1'
			candidate.build_id='0'*64
			candidate.build_ids={}
			candidate.module_symbol_status={}
			candidate.context=SimpleNamespace(contract={})
			(root/'shared-metadata.json').write_text('{"ideDependencies":{}}',encoding='utf-8')
			manifest_file=candidate.root/'release-manifest.json'
			def audit(*args, businesses):
				manifest=json.loads(manifest_file.read_text(encoding='utf-8'))
				self.assertEqual(businesses,set(entry.DESKTOP_BUSINESSES))
				self.assertNotIn('businessQualifications',manifest)
			with patch.object(candidate,'wheels'), patch.object(entry,'verify_release',side_effect=audit) as verify:
				candidate.complete(root/'interop',root)
			verify.assert_called_once()
			checks=json.loads(manifest_file.read_text(encoding='utf-8'))['businessQualifications']['CoreSDK']['checks']
			self.assertEqual(checks['target-runtime']['status'],'not-run')


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('c++') and shutil.which('objcopy') and shutil.which('objdump'), 'ELF C++ toolchain required')
class CrossWheelSymbolTests(unittest.TestCase):
	def test_functional_extension_is_loadable_without_embedded_debug_and_keeps_original_symbols(self):
		import ctypes
		with tempfile.TemporaryDirectory(prefix='vero wheel symbols ') as temporary:
			root=Path(temporary)
			source=root/'binding.cpp'
			source.write_text('extern "C" int vero_wheel_value() { return 42; }\n')
			built=root/'_veroscript.so'
			subprocess.run(['c++','-g','-shared','-fPIC',str(source),'-o',str(built)],check=True)
			before=digest(built)
			self.assertIn('.debug_info',subprocess.check_output(['objdump','-h',built],text=True))
			staged=root/'wheel/_veroscript.so'
			staged.parent.mkdir()
			stage_extension(built,staged,{},host_target(),root/'symbols')
			self.assertEqual(digest(built),before)
			self.assertNotIn('.debug_info',subprocess.check_output(['objdump','-h',staged],text=True))
			self.assertEqual(ctypes.CDLL(str(staged)).vero_wheel_value(),42)
			debug=list((root/'symbols').rglob('*.debug'))
			self.assertEqual(len(debug),1)
			self.assertIn('.debug_info',subprocess.check_output(['objdump','-h',debug[0]],text=True))
			self.assertLess(staged.stat().st_size,built.stat().st_size)


@unittest.skipUnless(shutil.which('cmake') and shutil.which('c++'),'A native C++ compiler and CMake are required for header ABI compilation')
class TargetPythonAbiTests(unittest.TestCase):
	def setUp(self):
		self.temporary=tempfile.TemporaryDirectory(prefix='vero sdk ABI ')
		self.addCleanup(self.temporary.cleanup)
		self.root=Path(self.temporary.name)
		self.include=self.root/'include'
		self.include.mkdir()
		(self.include/'Python.h').write_text('#include "pyconfig.h"\n#define PY_MAJOR_VERSION 3\n#define PY_MINOR_VERSION 15\n#define PY_MICRO_VERSION 0\n')
		self.sdk={'pythonVersion':'3.15.0','pointerSize':struct.calcsize('P'),'includeDirectory':str(self.include)}
		self.settings={'CMAKE_GENERATOR':'Ninja' if shutil.which('ninja') else 'Unix Makefiles','CMAKE_CXX_COMPILER':shutil.which('c++'),'CMAKE_COMMAND':shutil.which('cmake')}
		self.target=host_target()

	def check(self,name,configuration,sdk=None):
		(self.include/'pyconfig.h').write_text(configuration)
		verify_target_python_abi(self.settings,sdk or self.sdk,self.root/name,self.target)

	def test_normal_release_and_inactive_debug_definition_are_accepted(self):
		self.check('normal','#if 0\n#define Py_DEBUG 1\n#endif\n')

	def test_debug_free_threaded_and_trace_ref_headers_are_rejected(self):
		for macro in ('Py_DEBUG','Py_GIL_DISABLED','Py_TRACE_REFS'):
			with self.subTest(macro=macro),self.assertRaises(subprocess.CalledProcessError):
				self.check(macro,'#define '+macro+' 1\n')

	def test_pointer_size_is_checked_by_the_actual_compiler(self):
		with self.assertRaises(subprocess.CalledProcessError):
			self.check('wrong-pointer','',dict(self.sdk,pointerSize=4 if self.sdk['pointerSize']==8 else 8))

	def test_actual_preprocessor_version_must_match_sdk(self):
		with self.assertRaises(subprocess.CalledProcessError):
			self.check('wrong-version','',dict(self.sdk,pythonVersion='3.15.1'))


if __name__=='__main__':
	unittest.main()
