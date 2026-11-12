# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Separate native debug data without changing the native build outputs."""
from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import tempfile

from veroscript_release import digest


class LinuxNativeSymbols:
	def __init__(self, scratch: Path, objcopy='objcopy', objdump='objdump'):
		self.scratch = scratch
		self.objcopy, self.objdump = objcopy, objdump
		self.prepared = {}

	def prepare(self, source: Path):
		with source.open('rb') as stream:
			magic = stream.read(8)
		archive = magic == b'!<arch>\n'
		if not archive and not magic.startswith(b'\x7fELF'):
			return None
		key = (source.name, digest(source))
		if key in self.prepared:
			return self.prepared[key]
		sections = subprocess.run([self.objdump, '-h', str(source)], check=True,
								  capture_output=True, text=True).stdout
		if not re.search(r'\s\.(?:debug_|zdebug_)', sections):
			self.prepared[key] = None
			return None
		directory = self.scratch / key[1] / source.name
		directory.mkdir(parents=True, exist_ok=True)
		released = directory / 'release' / source.name
		released.parent.mkdir()
		symbols = directory / 'symbols' / (source.name if archive else source.name + '.debug')
		symbols.parent.mkdir()
		if archive:
			# Retain complete static objects: developers can relink with DWARF.
			# A debug-only archive cannot be linked into an embedding program.
			shutil.copy2(source, symbols)
		else:
			subprocess.run([self.objcopy, '--only-keep-debug', str(source), str(symbols)], check=True)
		subprocess.run([self.objcopy, '--strip-debug', str(source), str(released)], check=True)
		if not archive:
			subprocess.run([self.objcopy, '--remove-section=.gnu_debuglink',
							'--add-gnu-debuglink=' + str(symbols), str(released)], check=True)
		released.chmod(source.stat().st_mode)
		self.prepared[key] = (released, symbols)
		return released, symbols

	def export_symbols(self, source: Path, destination: Path):
		prepared = self.prepare(source)
		if prepared is None:
			return
		symbols = prepared[1]
		destination.mkdir(parents=True, exist_ok=True)
		target = destination / symbols.name
		if target.exists() and digest(target) != digest(symbols):
			raise ValueError(f'Conflicting native symbols: {target}')
		if not target.exists():
			shutil.copy2(symbols, target)

	def prepare_tree(self, root: Path, symbols: Path | None = None):
		for source in sorted(root.rglob('*')):
			if not source.is_file():
				continue
			prepared = self.prepare(source)
			if prepared is None:
				continue
			if symbols is not None:
				self.export_symbols(source, symbols)
			mode = source.stat().st_mode
			shutil.copy2(prepared[0], source)
			source.chmod(mode)


def macos_has_debug(data):
	"""Inspect object sections and linker debug symbols, including universal archives."""
	if data[:8] == b'!<arch>\n':
		from veroscript_mobile_native import ar_objects
		return any(macos_has_debug(member) for member in ar_objects(data))
	if data[:4] in {b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}:
		wide = data[:4] == b'\xca\xfe\xba\xbf'
		count = struct.unpack_from('>I', data, 4)[0]
		for index in range(count):
			offset, size = struct.unpack_from('>QQ' if wide else '>II', data, 8 + index * (32 if wide else 20) + 8)
			if macos_has_debug(data[offset:offset + size]):
				return True
		return False
	if data[:4] != b'\xcf\xfa\xed\xfe':
		return False
	count = struct.unpack_from('<I', data, 16)[0]
	cursor = 32
	for _ in range(count):
		command, size = struct.unpack_from('<II', data, cursor)
		if size < 8 or cursor + size > len(data):
			raise ValueError('Invalid macOS Mach-O load command')
		if command == 0x19:
			sections = struct.unpack_from('<I', data, cursor + 64)[0]
			if 72 + sections * 80 > size:
				raise ValueError('Invalid macOS Mach-O section inventory')
			for index in range(sections):
				section = cursor + 72 + index * 80
				name = data[section:section + 16].rstrip(b'\0')
				owner = data[section + 16:section + 32].rstrip(b'\0')
				if owner == b'__DWARF' or name.startswith((b'__debug_', b'__zdebug_')):
					return True
		elif command == 2:
			offset, symbols = struct.unpack_from('<II', data, cursor + 8)
			if offset + symbols * 16 > len(data):
				raise ValueError('Invalid macOS Mach-O symbol inventory')
			if any(data[offset + index * 16 + 4] & 0xe0 for index in range(symbols)):
				return True
		cursor += size
	return False


def _macos_debug_only_object(data):
	"""Only discard empty translation units, never code, data, relocations or autolinks."""
	if data[:4] != b'\xcf\xfa\xed\xfe' or struct.unpack_from('<I', data, 12)[0] != 1:
		return False
	cursor = 32
	for _ in range(struct.unpack_from('<I', data, 16)[0]):
		command, size = struct.unpack_from('<II', data, cursor)
		if size < 8 or cursor + size > len(data):
			raise ValueError('Invalid macOS Mach-O load command')
		if command == 0x19:
			count = struct.unpack_from('<I', data, cursor + 64)[0]
			if 72 + count * 80 > size:
				raise ValueError('Invalid macOS Mach-O section inventory')
			for index in range(count):
				section = cursor + 72 + index * 80
				name = data[section:section + 16].rstrip(b'\0')
				owner = data[section + 16:section + 32].rstrip(b'\0')
				if owner == b'__DWARF' or name.startswith((b'__debug_', b'__zdebug_')):
					continue
				if struct.unpack_from('<Q', data, section + 40)[0] or struct.unpack_from('<I', data, section + 60)[0]:
					return False
		elif command == 2:
			if struct.unpack_from('<I', data, cursor + 12)[0]:
				return False
		elif command not in {0x24, 0x2a, 0x32}:
			# In particular LC_LINKER_OPTION can request libraries without code.
			return False
		cursor += size
	return macos_has_debug(data)


def _remove_macos_debug_only_members(path, environment):
	from veroscript_mobile_native import ar_objects
	objects = list(ar_objects(path.read_bytes()))
	names = subprocess.run(['xcrun', 'ar', '-t', str(path)], check=True, capture_output=True,
						   text=True).stdout.splitlines()
	names = [name for name in names if not name.startswith('__.SYMDEF') and name not in {'/', '//'}]
	if len(names) != len(objects):
		raise ValueError('Cannot match macOS archive member identities')
	remove = []
	for name, data in zip(names, objects):
		if not macos_has_debug(data):
			continue
		if not _macos_debug_only_object(data) or names.count(name) != 1 or name.startswith('-'):
			raise ValueError('macOS strip retained debug in a nonempty or ambiguous archive object: ' + name)
		remove.append(name)
	if remove:
		# Apple strip skips objects without LC_SYMTAB, even when they contain
		# DWARF. Such proven-empty objects have no effect on static linkage.
		subprocess.run(['xcrun', 'ar', '-d', str(path), *remove], check=True, env=environment)


def strip_macos_debug(path, strip='strip'):
	# Deterministic archive member timestamps also make SDK/build comparisons stable.
	environment = dict(os.environ, ZERO_AR_DATE='1')
	# Preserve the staging input if stripping, debug validation or signing
	# fails. Apple's archive processing may still require internal /tmp space.
	with tempfile.TemporaryDirectory(prefix='.veroscript-strip-', dir=path.parent) as temporary:
		output = Path(temporary) / path.name
		subprocess.run([strip, '-S', '-o', str(output), str(path)], check=True, env=environment)
		data = output.read_bytes()
		if data[:8] == b'!<arch>\n' and macos_has_debug(data):
			_remove_macos_debug_only_members(output, environment)
			data = output.read_bytes()
		if macos_has_debug(data):
			raise ValueError('macOS functional library retains debug data: ' + str(path))
		if data[:4] == b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I', data, 12)[0] != 1:
			# Stripping invalidates linker signatures, including ARM64 signatures.
			subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(output)], check=True)
			subprocess.run(['codesign', '--verify', '--strict', str(output)], check=True)
		shutil.copystat(path, output)
		os.replace(output, path)


def strip_macos_tree(root, strip='strip'):
	"""Trim staging-only module payloads; their original build objects remain intact."""
	for path in sorted(root.rglob('*')):
		if not path.is_file() or path.is_symlink():
			continue
		with path.open('rb') as stream:
			magic = stream.read(8)
		if magic == b'!<arch>\n' or magic[:4] in {b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}:
			if macos_has_debug(path.read_bytes()):
				strip_macos_debug(path, strip)


def _macos_archive_objects_equal(left, right):
	"""Ignore only archive index/header metadata regenerated by install-time ranlib."""
	from veroscript_mobile_native import ar_objects
	first, second = left.read_bytes(), right.read_bytes()
	if first[:8] != b'!<arch>\n' or second[:8] != b'!<arch>\n':
		return False
	if list(ar_objects(first)) != list(ar_objects(second)):
		return False
	def members(path):
		names = subprocess.run(['xcrun', 'ar', '-t', str(path)], check=True,
			capture_output=True, text=True).stdout.splitlines()
		return [name for name in names if not name.startswith('__.SYMDEF') and name not in {'/', '//'}]
	return members(left) == members(right)


class MacNativeSymbols(LinuxNativeSymbols):
	def __init__(self, scratch, strip='strip'):
		self.scratch, self.strip, self.prepared = scratch, strip, {}

	def prepare(self, source):
		if source.is_symlink():
			return None
		with source.open('rb') as stream:
			magic = stream.read(8)
		archive = magic == b'!<arch>\n'
		if not archive and magic[:4] not in {b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}:
			return None
		key = (source.name, digest(source))
		if key in self.prepared:
			return self.prepared[key]
		if not macos_has_debug(source.read_bytes()):
			self.prepared[key] = None
			return None
		directory = self.scratch / key[1] / source.name
		released = directory / 'release' / source.name
		released.parent.mkdir(parents=True, exist_ok=True)
		symbols = directory / 'symbols' / (source.name if archive else source.name + '.dSYM')
		symbols.parent.mkdir(parents=True, exist_ok=True)
		if archive:
			shutil.copy2(source, symbols)
		else:
			subprocess.run(['dsymutil', str(source), '-o', str(symbols)], check=True)
		shutil.copy2(source, released)
		strip_macos_debug(released, self.strip)
		self.prepared[key] = (released, symbols)
		return released, symbols

	def export_symbols(self, source, destination):
		prepared = self.prepare(source)
		if prepared is None:
			return
		symbols = prepared[1]
		destination.mkdir(parents=True, exist_ok=True)
		target = destination / symbols.name
		if symbols.is_dir():
			if target.exists():
				left = {p.relative_to(symbols): digest(p) for p in symbols.rglob('*') if p.is_file()}
				right = {p.relative_to(target): digest(p) for p in target.rglob('*') if p.is_file()}
				if left != right:
					raise ValueError('Conflicting native symbols: ' + str(target))
			else:
				shutil.copytree(symbols, target)
		elif target.exists() and digest(target) != digest(symbols) and not _macos_archive_objects_equal(target, symbols):
			raise ValueError('Conflicting native symbols: ' + str(target))
		elif not target.exists():
			shutil.copy2(symbols, target)
