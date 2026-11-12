# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_native_symbols import LinuxNativeSymbols, MacNativeSymbols, macos_has_debug
from veroscript_release import digest


@unittest.skipUnless(sys.platform.startswith('linux') and all(shutil.which(tool) for tool in
					 ('cc', 'ar', 'objcopy', 'objdump', 'nm')), 'GNU native tools on Linux required')
class NativeSymbolsTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix='native symbols with spaces ')
		self.addCleanup(self.temporary.cleanup)
		self.root = Path(self.temporary.name)
		self.source = self.root / 'answer.c'
		self.source.write_text('int fixture_answer(void) { return 42; }\n')
		self.split = LinuxNativeSymbols(self.root / 'cache')
		self.symbols = self.root / 'Symbols'

	def command(self, *arguments):
		return subprocess.run([str(arg) for arg in arguments], check=True, text=True,
							  capture_output=True).stdout

	def test_static_archive_retains_linkage_and_debug_original(self):
		obj = self.root / 'answer.o'
		self.command('cc', '-g', '-O2', '-c', self.source, '-o', obj)
		original = self.root / 'libanswer.a'
		self.command('ar', 'rcs', original, obj)
		original_digest = digest(original)
		production = self.root / 'Runtime'
		production.mkdir()
		library = production / original.name
		shutil.copy2(original, library)
		self.split.prepare_tree(production, self.symbols)
		self.assertEqual(digest(original), original_digest)
		self.assertEqual(digest(self.symbols / original.name), original_digest)
		self.assertNotIn('.debug_info', self.command('objdump', '-h', library))
		self.assertIn('.debug_info', self.command('objdump', '-h', self.symbols / original.name))
		self.assertEqual(self.command('nm', '-g', '--defined-only', original),
						 self.command('nm', '-g', '--defined-only', library))
		consumer = self.root / 'consumer.c'
		consumer.write_text('int fixture_answer(void); int main(void) { return fixture_answer() != 42; }\n')
		executable = self.root / 'consumer'
		self.command('cc', consumer, library, '-o', executable)
		self.command(executable)
		if shutil.which('gdb'):
			self.command('cc', consumer, self.symbols / original.name, '-o', executable)
			output = self.command('gdb', '-batch', '-ex', 'info line fixture_answer', executable)
			self.assertIn('answer.c', output)

	def test_shared_library_keeps_abi_and_matching_debuglink(self):
		original = self.root / 'libanswer.so'
		self.command('cc', '-g', '-O2', '-fPIC', '-shared', self.source, '-o', original)
		original_digest = digest(original)
		production = self.root / 'Runtime'
		production.mkdir()
		library = production / original.name
		shutil.copy2(original, library)
		original_mode = library.stat().st_mode
		self.split.prepare_tree(production, self.symbols)
		self.assertEqual(digest(original), original_digest)
		self.assertEqual(library.stat().st_mode, original_mode)
		self.assertNotIn('.debug_info', self.command('objdump', '-h', library))
		debug = self.symbols / (original.name + '.debug')
		self.assertIn('.debug_info', self.command('objdump', '-h', debug))
		self.assertEqual(self.command('nm', '-D', '--defined-only', original),
						 self.command('nm', '-D', '--defined-only', library))
		self.command(sys.executable, '-c',
					 'import ctypes,sys; assert ctypes.CDLL(sys.argv[1]).fixture_answer() == 42', library)
		link = self.root / 'debuglink.bin'
		self.command('objcopy', '--dump-section=.gnu_debuglink=' + str(link), library, self.root / 'inspect.so')
		data = link.read_bytes()
		name = data.split(b'\0', 1)[0]
		self.assertEqual(name.decode(), debug.name)
		offset = (len(name) + 1 + 3) & ~3
		self.assertEqual(struct.unpack_from('<I', data, offset)[0], zlib.crc32(debug.read_bytes()))
		released_digest = digest(library)
		self.split.prepare_tree(production, self.symbols)
		self.assertEqual(digest(library), released_digest)
		if shutil.which('gdb'):
			shutil.copy2(debug, production / debug.name)
			output = self.command('gdb', '-batch', '-ex', 'info line fixture_answer', library)
			self.assertIn('answer.c', output)

	def test_files_without_debug_and_other_assets_are_unchanged(self):
		production = self.root / 'Runtime'
		production.mkdir()
		original = production / 'libanswer.so'
		self.command('cc', '-O2', '-fPIC', '-shared', self.source, '-o', original)
		(production / 'asset.json').write_text('{"asset": true}')
		before = {p.name: digest(p) for p in production.iterdir()}
		self.split.prepare_tree(production, self.symbols)
		self.assertEqual(before, {p.name: digest(p) for p in production.iterdir()})
		self.assertFalse(self.symbols.exists())

	def test_conflicting_flat_symbol_names_are_rejected(self):
		for number in (1, 2):
			directory = self.root / str(number)
			directory.mkdir()
			self.source.write_text(f'int fixture_answer(void) {{ return {number}; }}\n')
			original = directory / 'libanswer.so'
			self.command('cc', '-g', '-O2', '-fPIC', '-shared', self.source, '-o', original)
			if number == 1:
				self.split.export_symbols(original, self.symbols)
			else:
				with self.assertRaisesRegex(ValueError, 'Conflicting native symbols'):
					self.split.export_symbols(original, self.symbols)

	def test_cached_binary_preserves_each_staged_files_permissions(self):
		original = self.root / 'libanswer.so'
		self.command('cc', '-g', '-O2', '-fPIC', '-shared', self.source, '-o', original)
		original.chmod(0o755)
		self.split.prepare(original)
		production = self.root / 'Runtime'
		production.mkdir()
		staged = production / original.name
		shutil.copy2(original, staged)
		staged.chmod(0o644)
		self.split.prepare_tree(production, self.symbols)
		self.assertEqual(staged.stat().st_mode & 0o777, 0o644)


@unittest.skipUnless(sys.platform == 'darwin' and all(shutil.which(tool) for tool in
					 ('clang', 'ar', 'strip', 'dsymutil', 'codesign')), 'Apple native tools required')
class MacNativeSymbolsTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix='mac native symbols with spaces ')
		self.addCleanup(self.temporary.cleanup)
		self.root = Path(self.temporary.name)
		self.source = self.root / 'answer.c'
		self.source.write_text('int fixture_answer(void) { return 42; }\n')
		self.split = MacNativeSymbols(self.root / 'cache')
		self.symbols = self.root / 'Symbols'

	def command(self, *arguments):
		return subprocess.run([str(arg) for arg in arguments], check=True, text=True,
							  capture_output=True).stdout

	def test_archive_preserves_linkage_debug_original_and_determinism(self):
		obj = self.root / 'answer.o'
		self.command('clang', '-g', '-O2', '-c', self.source, '-o', obj)
		self.assertTrue(macos_has_debug(obj.read_bytes()))
		original = self.root / 'libanswer.a'
		self.command('ar', 'rcs', original, obj)
		before = digest(original)
		released, symbols = self.split.prepare(original)
		self.assertFalse(macos_has_debug(released.read_bytes()))
		self.assertTrue(macos_has_debug(symbols.read_bytes()))
		self.assertEqual(before, digest(original))
		other, _ = MacNativeSymbols(self.root / 'other').prepare(original)
		self.assertEqual(digest(released), digest(other))
		self.assertEqual(self.command('nm', '-g', original), self.command('nm', '-g', released))
		consumer = self.root / 'consumer.c'
		consumer.write_text('int fixture_answer(void); int main(void) { return fixture_answer() != 42; }\n')
		executable = self.root / 'consumer'
		self.command('clang', consumer, released, '-o', executable)
		self.command(executable)

	def test_shared_library_is_signed_and_loadable_after_stripping(self):
		original = self.root / 'libanswer.dylib'
		obj = self.root / 'answer.o'
		self.command('clang', '-g', '-O2', '-c', self.source, '-o', obj)
		self.command('clang', '-dynamiclib', obj, '-o', original)
		before = digest(original)
		production = self.root / 'Runtime'
		production.mkdir()
		library = production / original.name
		shutil.copy2(original, library)
		self.split.prepare_tree(production, self.symbols)
		self.assertEqual(before, digest(original))
		self.assertFalse(macos_has_debug(library.read_bytes()))
		self.assertTrue((self.symbols / (original.name + '.dSYM')).is_dir())
		self.assertTrue(macos_has_debug((self.symbols / (original.name + '.dSYM') / 'Contents/Resources/DWARF' / original.name).read_bytes()))
		self.command('codesign', '--verify', '--strict', library)
		self.command(sys.executable, '-c', 'import ctypes,sys; assert ctypes.CDLL(sys.argv[1]).fixture_answer() == 42', library)
		after = digest(library)
		self.split.prepare_tree(production, self.symbols)
		self.assertEqual(after, digest(library))

	def test_debug_only_member_removal_preserves_duplicate_object_names_and_linkage(self):
		objects = []
		for name, value in (('left', 20), ('right', 22)):
			directory = self.root / name
			directory.mkdir()
			source = directory / 'part.cpp'
			source.write_text(f'extern "C" int fixture_{name}(void) {{ return {value}; }}\n')
			obj = directory / 'same.o'
			self.command('clang++', '-g', '-O2', '-c', source, '-o', obj)
			objects.append(obj)
		empty = self.root / 'empty.cpp'
		empty.write_text('namespace Fixture { struct OnlyDebug { int field; }; extern OnlyDebug declared; }\n')
		empty_object = self.root / 'empty.o'
		self.command('clang++', '-g', '-fno-eliminate-unused-debug-types', '-c', empty, '-o', empty_object)
		self.assertTrue(macos_has_debug(empty_object.read_bytes()))
		original = self.root / 'libduplicates.a'
		self.command('ar', 'qcs', original, *objects, empty_object)
		before = digest(original)
		released, symbols = self.split.prepare(original)
		self.assertEqual(before, digest(original))
		self.assertEqual(before, digest(symbols))
		members = self.command('ar', '-t', released).splitlines()
		self.assertEqual([name for name in members if not name.startswith('__.SYMDEF')], ['same.o', 'same.o'])
		self.assertFalse(macos_has_debug(released.read_bytes()))
		external = lambda output: [line for line in output.splitlines() if line and not line.endswith(':')]
		self.assertEqual(external(self.command('nm', '-g', '-j', original)), external(self.command('nm', '-g', '-j', released)))
		consumer = self.root / 'consumer.cpp'
		consumer.write_text('extern "C" int fixture_left(void); extern "C" int fixture_right(void); int main() { return fixture_left() + fixture_right() != 42; }\n')
		executable = self.root / 'consumer'
		self.command('clang++', consumer, released, '-o', executable)
		self.command(executable)
		other, _ = MacNativeSymbols(self.root / 'other-cache').prepare(original)
		self.assertEqual(digest(released), digest(other))

	def test_empty_object_with_linker_options_is_not_discarded(self):
		empty = self.root / 'autolink.cpp'
		empty.write_text('namespace Fixture { struct OnlyDebug { int field; }; extern OnlyDebug declared; }\n'
			'__asm__(".linker_option \\\"-framework\\\", \\\"Foundation\\\"");\n')
		obj = self.root / 'autolink.o'
		self.command('clang++', '-g', '-fno-eliminate-unused-debug-types', '-c', empty, '-o', obj)
		self.assertTrue(macos_has_debug(obj.read_bytes()))
		original = self.root / 'libautolink.a'
		self.command('ar', 'qcs', original, obj)
		before = digest(original)
		with self.assertRaisesRegex(ValueError, 'nonempty or ambiguous'):
			self.split.prepare(original)
		self.assertEqual(before, digest(original))
		from veroscript_native_symbols import strip_macos_debug
		with self.assertRaisesRegex(ValueError, 'nonempty or ambiguous'):
			strip_macos_debug(original)
		self.assertEqual(before, digest(original), 'Failed staging transforms must preserve the input')
		self.assertEqual(list(self.root.glob('.veroscript-strip-*')), [])

	def test_install_time_ranlib_metadata_preserves_one_original_symbol_archive(self):
		obj = self.root / 'answer.o'
		self.command('clang', '-g', '-O2', '-c', self.source, '-o', obj)
		original = self.root / 'libanswer.a'
		self.command('ar', 'rcs', original, obj)
		self.split.export_symbols(original, self.symbols)
		original_sha = digest(original)
		installed = self.root / 'installed' / original.name
		installed.parent.mkdir()
		shutil.copy2(original, installed)
		self.command('ranlib', installed)
		# Make the ranlib-managed index timestamp deliberately different even
		# if both commands completed within the same second.
		data = bytearray(installed.read_bytes())
		data[24:36] = b'1234567890  '
		installed.write_bytes(data)
		self.assertNotEqual(original_sha, digest(installed))
		self.split.export_symbols(installed, self.symbols)
		self.assertEqual(digest(self.symbols / original.name), original_sha)
		self.source.write_text('int fixture_answer(void) { return 17; }\n')
		self.command('clang', '-g', '-O2', '-c', self.source, '-o', obj)
		installed.unlink()
		self.command('ar', 'rcs', installed, obj)
		with self.assertRaisesRegex(ValueError, 'Conflicting native symbols'):
			self.split.export_symbols(installed, self.symbols)

	def test_archive_equivalence_preserves_member_names_and_order(self):
		from veroscript_native_symbols import _macos_archive_objects_equal
		obj = self.root / 'answer.o'
		self.command('clang', '-g', '-O2', '-c', self.source, '-o', obj)
		other = self.root / 'renamed.o'
		shutil.copy2(obj, other)
		first, renamed, ordered, reversed_order = (self.root / name for name in
			('first.a', 'renamed.a', 'ordered.a', 'reversed.a'))
		self.command('ar', 'qcs', first, obj)
		self.command('ar', 'qcs', renamed, other)
		self.command('ar', 'qcs', ordered, obj, other)
		self.command('ar', 'qcs', reversed_order, other, obj)
		self.assertFalse(_macos_archive_objects_equal(first, renamed))
		self.assertFalse(_macos_archive_objects_equal(ordered, reversed_order))


if __name__ == '__main__':
	unittest.main()
