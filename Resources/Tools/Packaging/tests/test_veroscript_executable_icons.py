# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Compile native product icons and verify the embedded PNG frames."""
import ctypes
import hashlib
import io
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[4]
CMAKE = shutil.which('cmake')
sys.path.insert(0, str(REPO / 'Resources/Tools/Packaging'))
from veroscript_icons import stage_runtime_icons, verify_runtime_icons, verify_windows_executable_icon


def windows_icon_frames(executable):
	kernel = ctypes.WinDLL('kernel32', use_last_error=True)
	kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
	kernel.LoadLibraryExW.restype = ctypes.c_void_p
	kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
	kernel.FindResourceW.restype = ctypes.c_void_p
	kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
	kernel.SizeofResource.restype = ctypes.c_uint32
	kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
	kernel.LoadResource.restype = ctypes.c_void_p
	kernel.LockResource.argtypes = [ctypes.c_void_p]
	kernel.LockResource.restype = ctypes.c_void_p
	kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
	kernel.FreeLibrary.restype = ctypes.c_int
	module = kernel.LoadLibraryExW(str(executable), None, 2)
	if not module:
		raise ctypes.WinError(ctypes.get_last_error())
	try:
		group = kernel.FindResourceW(module, 1, 14)
		if not group:
			return None
		data = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, group)),
			kernel.SizeofResource(module, group))
		reserved, kind, count = struct.unpack_from('<HHH', data)
		if (reserved, kind) != (0, 1) or len(data) != 6 + count * 14:
			raise ValueError('Invalid embedded icon group')
		frames = []
		for index in range(count):
			width, height, colors, unused, planes, bits, size, identifier = struct.unpack_from(
				'<BBBBHHIH', data, 6 + index * 14)
			image = kernel.FindResourceW(module, identifier, 3)
			if not image or kernel.SizeofResource(module, image) != size:
				raise ValueError('Missing embedded icon frame')
			pixels = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, image)), size)
			frames.append(((width, height, colors, unused, planes, bits), pixels))
		return frames
	finally:
		kernel.FreeLibrary(module)


def ico_frames(path):
	data = path.read_bytes()
	reserved, kind, count = struct.unpack_from('<HHH', data)
	if (reserved, kind) != (0, 1):
		raise ValueError('Invalid source ICO')
	frames = []
	for index in range(count):
		width, height, colors, unused, planes, bits, size, offset = struct.unpack_from(
			'<BBBBHHII', data, 6 + index * 16)
		frames.append(((width, height, colors, unused, planes, bits), data[offset:offset + size]))
	return frames


def pe_with_icon(icon, architecture='x64'):
	frames = ico_frames(icon)
	group = struct.pack('<HHH', 0, 1, len(frames)) + b''.join(
		struct.pack('<BBBBHHIH', *description, len(pixels), index + 1)
		for index, (description, pixels) in enumerate(frames))
	resources = bytearray()

	def directory(entries):
		offset = len(resources)
		resources.extend(bytes(16 + 8 * len(entries)))
		struct.pack_into('<HH', resources, offset + 12, 0, len(entries))
		for index, (identifier, value) in enumerate(sorted(entries.items())):
			if isinstance(value, dict):
				child = directory(value) | 0x80000000
			else:
				child = len(resources)
				resources.extend(bytes(16))
				struct.pack_into('<IIII', resources, child, 0x1000 + len(resources), len(value), 0, 0)
				resources.extend(value)
				resources.extend(bytes(-len(resources) % 4))
			struct.pack_into('<II', resources, offset + 16 + index * 8, identifier, child)
		return offset

	directory({3: {index + 1: {1033: pixels} for index, (_, pixels) in enumerate(frames)},
		14: {32512: {1033: group}}})
	optional_size = 224 if architecture == 'x86' else 240
	directories = 96 if architecture == 'x86' else 112
	data = bytearray(512)
	data[:2] = b'MZ'
	struct.pack_into('<I', data, 60, 128)
	data[128:132] = b'PE\0\0'
	struct.pack_into('<HH', data, 132, {'x86': 0x14c, 'x64': 0x8664, 'arm64': 0xaa64}[architecture], 1)
	struct.pack_into('<H', data, 148, optional_size)
	struct.pack_into('<H', data, 152, 0x10b if architecture == 'x86' else 0x20b)
	struct.pack_into('<I', data, 152 + directories - 4, 16)
	struct.pack_into('<II', data, 152 + directories + 16, 0x1000, len(resources))
	section = 152 + optional_size
	data[section:section + 8] = b'.rsrc\0\0\0'
	struct.pack_into('<IIII', data, section + 8, len(resources), 0x1000, len(resources), 512)
	return bytes(data + resources)


class PortableExecutableIconTests(unittest.TestCase):
	def test_all_windows_architectures_match_and_stale_or_truncated_resources_fail(self):
		official = REPO / 'Resources/Assets/icons/veroscript-icon.ico'
		stale = REPO / 'Resources/Assets/file-associations/icons/veroscript-source.ico'
		for architecture in ('x86', 'x64', 'arm64'):
			with self.subTest(architecture=architecture):
				current = pe_with_icon(official, architecture)
				verify_windows_executable_icon(REPO, io.BytesIO(current + b'signed overlay'), architecture)
				for data in (pe_with_icon(stale, architecture), current[:-1], b'MZ' + bytes(64)):
					with self.assertRaisesRegex(ValueError, 'Missing/stale Windows executable icon'):
						verify_windows_executable_icon(REPO, io.BytesIO(data), architecture)

	def test_runtime_audit_rejects_an_old_embedded_icon_with_current_external_icons(self):
		with tempfile.TemporaryDirectory() as temporary:
			runtime = Path(temporary).resolve()
			stage_runtime_icons(REPO, runtime, 'win-arm64')
			stale = REPO / 'Resources/Assets/file-associations/icons/veroscript-source.ico'
			(runtime / 'veroscript.exe').write_bytes(pe_with_icon(stale, 'arm64'))
			with self.assertRaisesRegex(ValueError, 'Missing/stale Windows executable icon'):
				verify_runtime_icons(REPO, runtime, 'win-arm64')

	def test_sdk_installer_input_cli_checks_nested_executables(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			program = root / 'bin/veroscript.exe'
			program.parent.mkdir()
			official = REPO / 'Resources/Assets/icons/veroscript-icon.ico'
			stale = REPO / 'Resources/Assets/file-associations/icons/veroscript-source.ico'
			command = [sys.executable, '-B', str(REPO / 'Resources/Tools/Packaging/veroscript_icons.py'),
				'verify-executables', '--root', str(root), '--target', 'win-x64']
			program.write_bytes(pe_with_icon(official))
			subprocess.run(command, check=True, capture_output=True)
			program.write_bytes(pe_with_icon(stale))
			result = subprocess.run(command, capture_output=True)
			self.assertNotEqual(0, result.returncode)
			self.assertIn(b'Missing/stale Windows executable icon', result.stderr)


@unittest.skipUnless(CMAKE, 'CMake is required for native executable icon checks')
class ExecutableIconTests(unittest.TestCase):
	def run_cmake(self, *arguments):
		result = subprocess.run([CMAKE, *map(str, arguments)], capture_output=True,
			text=True, encoding='utf-8', errors='replace', timeout=180)
		self.assertEqual(0, result.returncode, result.stdout + result.stderr)

	def test_product_icon_is_opt_in_relocatable_and_rebuilt_when_ico_changes(self):
		with tempfile.TemporaryDirectory(prefix='vero executable icons ') as temporary:
			root = Path(temporary).resolve()
			source = root / 'source with spaces 中文'
			build = root / 'native build'
			helpers = source / 'Resources/Tools/CMake'
			helpers.mkdir(parents=True)
			for name in ('VeroScriptHelpers.cmake', 'VeroScriptProductIcon.rc.in'):
				shutil.copyfile(REPO / 'Resources/Tools/CMake' / name, helpers / name)
			icon = source / 'Resources/Assets/icons/veroscript-icon.ico'
			icon.parent.mkdir(parents=True)
			shutil.copyfile(REPO / 'Resources/Assets/icons/veroscript-icon.ico', icon)
			(source / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
			(source / 'CMakeLists.txt').write_text(
				'cmake_minimum_required(VERSION 3.25)\n'
				'project(ExecutableIcons LANGUAGES CXX)\n'
				'if(WIN32)\n  enable_language(RC)\nendif()\n'
				'include(Resources/Tools/CMake/VeroScriptHelpers.cmake)\n'
				'vs_add_cli_executable(product PRODUCT_ICON SOURCES main.cpp)\n'
				'vs_add_cli_executable(control SOURCES main.cpp)\n'
				'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/targets-$<CONFIG>.txt"\n'
				'  CONTENT "$<TARGET_FILE:product>\n$<TARGET_FILE:control>\n")\n',
				encoding='utf-8')
			self.run_cmake('-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release')
			self.run_cmake('--build', build, '--config', 'Release', '--parallel', '2')
			product, control = map(Path, (build / 'targets-Release.txt').read_text(encoding='utf-8').splitlines())
			for program in (product, control):
				self.assertEqual(0, subprocess.run([str(program)], timeout=30).returncode)
			if sys.platform == 'win32':
				self.assertEqual(ico_frames(icon), windows_icon_frames(product))
				self.assertIsNone(windows_icon_frames(control))
				with product.open('rb') as stream:
					verify_windows_executable_icon(REPO, stream, product)
				with control.open('rb') as stream, self.assertRaisesRegex(ValueError, 'Missing/stale Windows executable icon'):
					verify_windows_executable_icon(REPO, stream, control)
			else:
				self.assertFalse((build / 'product-icon.rc').exists())
			product_hash = hashlib.sha256(product.read_bytes()).hexdigest()
			control_hash = hashlib.sha256(control.read_bytes()).hexdigest()
			shutil.copyfile(REPO / 'Resources/Assets/file-associations/icons/veroscript-source.ico', icon)
			self.run_cmake('--build', build, '--config', 'Release', '--parallel', '2')
			self.assertEqual(control_hash, hashlib.sha256(control.read_bytes()).hexdigest())
			if sys.platform == 'win32':
				self.assertNotEqual(product_hash, hashlib.sha256(product.read_bytes()).hexdigest())
				self.assertEqual(ico_frames(icon), windows_icon_frames(product))
				with product.open('rb') as stream, self.assertRaisesRegex(ValueError, 'Missing/stale Windows executable icon'):
					verify_windows_executable_icon(REPO, stream, product)
			else:
				self.assertEqual(product_hash, hashlib.sha256(product.read_bytes()).hexdigest())


if __name__ == '__main__':
	unittest.main()
