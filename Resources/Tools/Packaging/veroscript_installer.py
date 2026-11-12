# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Verify .NET single-file installer payloads without executing the target apphost."""
import gzip
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import tarfile
import tempfile
import zlib

from veroscript_products import NATIVE_TARGETS, installer_filename

# .NET 9 HostModel AppHost/HostWriter.cs and Bundle/{Manifest,FileEntry}.cs.
BUNDLE_SIGNATURE = bytes.fromhex('8b1202b96a612038727b930214d7a03213f5b9e6efae3318ee3b2dce24b36aae')
PART_PREFIX = 'VeroScriptInstaller.EmbeddedBundle.zip.'
BLOCK = 1024 * 1024
PART_LIMIT = 1024 * BLOCK
_VERIFIED_DOWNLOADS = set()


def _read(stream, size):
	data = stream.read(size)
	if len(data) != size:
		raise ValueError('Truncated single-file installer bundle')
	return data


def _string(stream):
	length = 0
	for shift in range(0, 35, 7):
		value = _read(stream, 1)[0]
		length |= (value & 127) << shift
		if value < 128:
			if not 0 < length <= 32768:
				raise ValueError('Invalid .NET bundle string length')
			return _read(stream, length).decode('utf-8')
	raise ValueError('Invalid .NET bundle string encoding')


def _bundle_header(stream):
	stream.seek(0)
	overlap = b''
	while chunk := stream.read(BLOCK):
		data = overlap + chunk
		marker = data.find(BUNDLE_SIGNATURE)
		if marker >= 8:
			return (struct.unpack_from('<q', data, marker - 8)[0],
				stream.tell() - len(data) + marker + len(BUNDLE_SIGNATURE))
		overlap = data[-(len(BUNDLE_SIGNATURE) + 7):]
	raise ValueError('Missing .NET single-file installer bundle')


def _bundle_entries(stream):
	from veroscript_release import safe_member
	header, host_end = _bundle_header(stream)
	file_size = stream.seek(0, os.SEEK_END)
	if not host_end <= header <= file_size - 12:
		raise ValueError('Invalid .NET bundle header offset')
	stream.seek(header)
	major, minor, count = struct.unpack('<IIi', _read(stream, 12))
	if (major, minor) != (6, 0) or not 1 <= count <= min(65536, (file_size - header) // 26):
		raise ValueError('Unsupported .NET installer bundle format')
	_string(stream)  # Extraction ID.
	_read(stream, 40)  # Deps/runtime configuration locations and flags.
	entries, aliases, ranges = {}, set(), []
	for _ in range(count):
		offset, size, compressed, kind = struct.unpack('<qqqB', _read(stream, 25))
		name = _string(stream)
		end = offset + (compressed or size)
		if (safe_member(name) != name or name.casefold() in aliases or not host_end <= offset <= end <= header
				or size < 0 or not 0 <= compressed <= size or kind > 5):
			raise ValueError(f'Invalid .NET installer bundle entry: {name}')
		entries[name] = (offset, size, compressed)
		aliases.add(name.casefold())
		if end > offset:
			ranges.append((offset, end))
	ranges.sort()
	if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
		raise ValueError('Overlapping .NET installer bundle entries')
	return entries


def _entry_chunks(stream, entry):
	offset, size, compressed = entry
	stream.seek(offset)
	remaining, expanded = compressed or size, 0
	inflater = zlib.decompressobj(-15) if compressed else None
	while remaining:
		chunk = _read(stream, min(BLOCK, remaining))
		remaining -= len(chunk)
		if inflater is None:
			expanded += len(chunk)
			yield chunk
			continue
		while chunk:
			data = inflater.decompress(chunk, min(BLOCK, size - expanded + 1))
			expanded += len(data)
			if expanded > size or inflater.unused_data:
				raise ValueError('Invalid compressed installer content')
			yield data
			chunk = inflater.unconsumed_tail
	if expanded != size or (inflater is not None and not inflater.eof):
		raise ValueError('Truncated installer content')


def _assembly_metadata(assembly, key):
	# ECMA-335 attribute prolog, two length-prefixed UTF-8 strings, no named arguments.
	encoded = key.encode('ascii')
	prefix = b'\x01\x00' + bytes([len(encoded)]) + encoded
	positions = [match.end() for match in re.finditer(re.escape(prefix), assembly)]
	if len(positions) != 1 or positions[0] >= len(assembly):
		raise ValueError(f'Missing/ambiguous installer assembly metadata: {key}')
	start = positions[0] + 1
	length = assembly[start - 1]
	end = start + length
	if not 0 < length < 128 or assembly[end:end + 2] != b'\x00\x00':
		raise ValueError(f'Invalid installer assembly metadata: {key}')
	return assembly[start:end].decode('ascii')


def _verify_apphost(stream, target, expected_payload):
	from veroscript_release import native_identity
	stream.seek(0)
	if native_identity(stream.read(4096)) != NATIVE_TARGETS[target]:
		raise ValueError('Installer architecture mismatch')
	entries = _bundle_entries(stream)
	names = sorted(name for name in entries if name.casefold().startswith(PART_PREFIX.casefold()))
	if (not 1 <= len(names) <= 32 or names != [f'{PART_PREFIX}{index:04d}' for index in range(len(names))]
			or any(not 0 < entries[name][1] <= PART_LIMIT for name in names)):
		raise ValueError('Invalid installer bundle content parts')
	entry = entries.get('VeroScriptInstaller.dll')
	if not entry or not 0 < entry[1] <= 64 * BLOCK:
		raise ValueError('Missing/invalid VeroScript installer assembly')
	assembly = b''.join(_entry_chunks(stream, entry))
	if _assembly_metadata(assembly, 'VeroScriptInstaller.BundlePartCount') != str(len(names)):
		raise ValueError('Installer assembly content part count mismatch')
	declared = _assembly_metadata(assembly, 'VeroScriptInstaller.BundleSha256').lower()
	checksum = hashlib.sha256()
	for name in names:
		for data in _entry_chunks(stream, entries[name]):
			checksum.update(data)
	actual = checksum.hexdigest()
	if actual != declared or (expected_payload is not None and actual != expected_payload):
		raise ValueError('Embedded installer payload differs from its verified bundle SHA-256')
	return actual


def verify_apphost(path, target, expected_payload=None):
	with Path(path).open('rb') as stream:
		return _verify_apphost(stream, target, expected_payload)


def verify_linux_download(path, target, expected_payload=None, *, scratch=None):
	executable = path.name.removesuffix('.tar.gz')
	with gzip.open(path, 'rb') as compressed, tarfile.open(fileobj=compressed, mode='r|') as archive:
		member = archive.next()
		if member is None or not member.isfile() or member.name != executable or not member.mode & 0o111:
			raise ValueError('Installer download archive identity/permissions mismatch')
		with tempfile.TemporaryFile(dir=scratch) as program, archive.extractfile(member) as source:
			shutil.copyfileobj(source, program, BLOCK)
			if archive.next() is not None:
				raise ValueError('Installer download must contain exactly one executable')
			# Consume tar padding and the gzip trailer after the last member.
			while chunk := archive.fileobj.read(BLOCK):
				if chunk.strip(b'\x00'):
					raise ValueError('Unexpected data after the installer download archive')
			return _verify_apphost(program, target, expected_payload)


def verify_installer_download(path, business, version, target, checksum, repo=None):
	if path.name != installer_filename(business, version, target):
		raise ValueError('Installer download filename differs from the target contract')
	if target.startswith('macos-'):
		from veroscript_macos_installer import verify_disk_image
		verify_disk_image(path, business, version, target, repo)
		return
	if repo is not None and target.startswith('win-'):
		from veroscript_icons import verify_windows_executable_icon
		with path.open('rb') as stream:
			verify_windows_executable_icon(repo, stream, path)
	# The candidate verifier has already hashed the outer artifact. Reuse the
	# payload audit when publication materializes the exact same artifact again.
	key = (checksum.lower(), target, path.name)
	if key not in _VERIFIED_DOWNLOADS:
		if target.startswith('linux-'):
			from veroscript_release import scratch_directory
			verify_linux_download(path, target, scratch=scratch_directory(repo) if repo is not None else None)
		else:
			verify_apphost(path, target)
		_VERIFIED_DOWNLOADS.add(key)


def package_unix_installer(repo, program, output, business, version, target, expected_payload):
	filename = installer_filename(business, version, target)
	output.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory(prefix='veroscript-installer-', dir=output) as temporary:
		stage = Path(temporary) / filename
		if target.startswith('macos-'):
			from veroscript_macos_installer import build_disk_image
			build_disk_image(repo, program, stage, business, version, target, expected_payload=expected_payload)
		elif target.startswith('linux-'):
			with tarfile.open(stage, 'w:gz') as archive, program.open('rb') as source:
				entry = archive.gettarinfo(program, arcname=filename.removesuffix('.tar.gz'))
				entry.mode = 0o755
				archive.addfile(entry, source)
			verify_linux_download(stage, target, expected_payload, scratch=stage.parent)
		else:
			raise ValueError('A Unix installer download requires a Linux or macOS target')
		os.replace(stage, output / filename)
	return output / filename
