#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Source icon inventory and the Runtime's installed assets/icons contract.

Consumes maintained static images only. Node.js/sharp are never packaging dependencies.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET

from veroscript_paths import module_root

PNG_SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
ICO_SIZES = PNG_SIZES[:7]
ICNS_SIZES = {'icp4': 16, 'icp5': 32, 'icp6': 64, 'ic07': 128,
			  'ic08': 256, 'ic09': 512, 'ic10': 1024}
FAMILIES = {'brand': ('icons', 'veroscript-icon'),
			'source': ('file-associations/icons', 'veroscript-source'),
			'application': ('file-associations/icons', 'veroscript-app'),
			'package': ('file-associations/icons', 'veroscript-package')}
TARGETS = ('win-x64', 'win-x86', 'win-arm64', 'linux-x64', 'linux-arm64', 'macos-x64', 'macos-arm64')
RUNTIME_ICON_ROOT = 'assets/icons'
PRODUCT_EXECUTABLES = {'veroscript.exe', 'veroscript-runtime.exe', 'veroscript-launcher.exe',
	'veroscript-lsp.exe', 'veroscript-dap.exe'}


def source_icon_files(repo: Path) -> dict[str, Path]:
	root = module_root(repo) / 'Resources/Assets'
	return {f'{directory}/{stem}{suffix}': root / directory / (stem + suffix)
			for directory, stem in FAMILIES.values()
			for suffix in ('.svg', '.ico', '.icns', *('-' + str(size) + '.png' for size in PNG_SIZES))}


def runtime_icon_files(repo: Path, target: str) -> dict[str, Path]:
	if target not in TARGETS:
		raise ValueError(f'Unsupported icon target: {target}')
	suffixes = ('.ico',) if target.startswith('win-') else ('.icns',) if target.startswith('macos-') else ('.svg', '-256.png')
	root = module_root(repo) / 'Resources/Assets'
	return {f'{RUNTIME_ICON_ROOT}/{stem}{suffix}': root / directory / (stem + suffix)
			for directory, stem in FAMILIES.values() for suffix in suffixes}


def _linked(path: Path) -> bool:
	from veroscript_release import linked_path
	return any(linked_path(current) for current in (path, *path.parents))


def _regular_file(path: Path, *, limit: int = 4 * 1024 * 1024) -> bytes:
	if _linked(path) or not path.is_file() or not 0 < path.stat().st_size <= limit:
		raise ValueError(f'Missing, linked or invalid icon file: {path}')
	return path.read_bytes()


def _ico_frames(data: bytes):
	reserved, kind, count = struct.unpack_from('<HHH', data)
	if (reserved, kind) != (0, 1) or not 0 < count <= 256:
		raise ValueError('Invalid ICO header')
	frames = []
	for index in range(count):
		*description, size, offset = struct.unpack_from('<BBBBHHII', data, 6 + index * 16)
		if not size or offset < 6 + count * 16 or offset + size > len(data):
			raise ValueError('Invalid ICO frame')
		frames.append((tuple(description), data[offset:offset + size]))
	return frames


def _windows_icon_frames(stream):
	"""Read PE32/PE32+ resources on any build host, without loading target code."""
	def read(offset, size):
		if offset < 0 or not 0 < size <= 16 * 1024 * 1024:
			raise ValueError('Invalid PE resource range')
		stream.seek(offset)
		data = stream.read(size)
		if len(data) != size:
			raise ValueError('Truncated PE resource')
		return data

	dos = read(0, 64)
	if dos[:2] != b'MZ':
		raise ValueError('Missing PE executable header')
	header_offset = struct.unpack_from('<I', dos, 60)[0]
	header = read(header_offset, 24)
	if header[:4] != b'PE\0\0':
		raise ValueError('Invalid PE signature')
	section_count = struct.unpack_from('<H', header, 6)[0]
	optional_size = struct.unpack_from('<H', header, 20)[0]
	optional = read(header_offset + 24, optional_size)
	directories = {0x10b: 96, 0x20b: 112}.get(struct.unpack_from('<H', optional)[0])
	if directories is None or len(optional) < directories + 24 or struct.unpack_from('<I', optional, directories - 4)[0] < 3:
		raise ValueError('Missing PE resource directory')
	rva, size = struct.unpack_from('<II', optional, directories + 16)
	sections = read(header_offset + 24 + optional_size, section_count * 40)
	matches = []
	for index in range(section_count):
		address, length, offset = struct.unpack_from('<III', sections, index * 40 + 12)
		if address <= rva and rva + size <= address + length:
			matches.append(offset + rva - address)
	if not rva or not size or len(matches) != 1:
		raise ValueError('Missing or invalid PE icon resources')
	resources = read(matches[0], size)

	def directory(offset):
		if offset < 0 or offset + 16 > len(resources):
			raise ValueError('Invalid PE resource directory offset')
		named, numbered = struct.unpack_from('<HH', resources, offset + 12)
		count = named + numbered
		if count > 4096 or offset + 16 + count * 8 > len(resources):
			raise ValueError('Invalid PE resource directory size')
		entries = [struct.unpack_from('<II', resources, offset + 16 + index * 8) for index in range(count)]
		if len({key for key, _ in entries}) != count:
			raise ValueError('Duplicate PE resource entry')
		return dict(entries)

	def children(entry):
		if entry is None or not entry & 0x80000000:
			raise ValueError('Missing PE icon resource directory')
		return directory(entry & 0x7fffffff)

	def payload(entry):
		if entry & 0x80000000:
			raise ValueError('Invalid PE icon resource leaf')
		address, length, _, _ = struct.unpack_from('<IIII', resources, entry)
		start = address - rva
		if start < 0 or not length or start + length > len(resources):
			raise ValueError('Invalid PE icon resource payload')
		return resources[start:start + length]

	types = directory(0)
	icons = children(types.get(3))  # RT_ICON.
	groups = children(types.get(14))  # RT_GROUP_ICON.
	group_ids = [key for key in groups if not key & 0x80000000]
	if not group_ids:
		raise ValueError('Missing PE application icon')
	variants = children(groups[min(group_ids)])
	if not variants:
		raise ValueError('Missing PE icon language')
	result = []
	for language, entry in variants.items():
		group = payload(entry)
		reserved, kind, count = struct.unpack_from('<HHH', group)
		if (reserved, kind) != (0, 1) or len(group) != 6 + count * 14:
			raise ValueError('Invalid PE icon group')
		frames = []
		for index in range(count):
			*description, length, identifier = struct.unpack_from('<BBBBHHIH', group, 6 + index * 14)
			image = payload(children(icons.get(identifier))[language])
			if len(image) != length:
				raise ValueError('Invalid PE icon frame size')
			frames.append((tuple(description), image))
		result.append(frames)
	return result


def verify_windows_executable_icon(repo: Path, stream, subject) -> None:
	try:
		source = source_icon_files(repo)['icons/veroscript-icon.ico']
		expected = _ico_frames(_regular_file(source))
		variants = _windows_icon_frames(stream)
		if any(frames != expected for frames in variants):
			raise ValueError('Embedded icon differs from the official product ICO')
	except (ValueError, KeyError, struct.error) as error:
		raise ValueError(f'Missing/stale Windows executable icon: {subject}. Rebuild with the current product icon. {error}') from error


def check_icon_sources(repo: Path, *, require_tracked: bool = False) -> dict[str, str]:
	files = source_icon_files(repo)
	data = {name: _regular_file(path) for name, path in files.items()}
	for directory, stem in FAMILIES.values():
		prefix = directory + '/' + stem
		try:
			if ET.fromstring(data[prefix + '.svg']).tag != '{http://www.w3.org/2000/svg}svg':
				raise ValueError('Missing SVG root')
			frames = {size: data[f'{prefix}-{size}.png'] for size in PNG_SIZES}
			for size, frame in frames.items():
				if (len(frame) < 45 or frame[:16] != b'\x89PNG\r\n\x1a\n\0\0\0\rIHDR'
						or struct.unpack_from('>II', frame, 16) != (size, size)
						or frame[-12:] != b'\0\0\0\0IEND\xaeB`\x82'):
					raise ValueError(f'Invalid {size}px PNG')
			ico = data[prefix + '.ico']
			if struct.unpack_from('<HHH', ico) != (0, 1, len(ICO_SIZES)):
				raise ValueError('Invalid ICO header')
			end = 6 + 16 * len(ICO_SIZES)
			for index, size in enumerate(ICO_SIZES):
				width, height, colors, reserved, planes, bits, length, offset = struct.unpack_from('<BBBBHHII', ico, 6 + index * 16)
				if ((width or 256, height or 256, colors, reserved, planes, bits) != (size, size, 0, 0, 1, 32)
						or offset != end or length != len(frames[size]) or ico[offset:offset + length] != frames[size]):
					raise ValueError(f'Stale/invalid {size}px ICO frame')
				end = offset + length
			if end != len(ico):
				raise ValueError('Trailing ICO data')
			icns = data[prefix + '.icns']
			if icns[:4] != b'icns' or struct.unpack_from('>I', icns, 4)[0] != len(icns):
				raise ValueError('Invalid ICNS header')
			position = 8
			for kind, size in ICNS_SIZES.items():
				tag, length = struct.unpack_from('>4sI', icns, position)
				if tag != kind.encode() or length != len(frames[size]) + 8 or icns[position + 8:position + length] != frames[size]:
					raise ValueError(f'Stale/invalid {size}px ICNS frame')
				position += length
			if position != len(icns):
				raise ValueError('Trailing ICNS data')
		except (ValueError, struct.error, ET.ParseError) as error:
			raise ValueError(f'Invalid official icon family {stem}: {error}') from error
	if require_tracked:
		root = repo.resolve()
		generator = module_root(repo) / 'Resources/Tools/Assets/generate-icons.cjs'
		_regular_file(generator)
		paths = [path.relative_to(root).as_posix() for path in (*files.values(), generator)]
		result = subprocess.run(['git', 'ls-files', '--error-unmatch', '--', *paths], cwd=root, capture_output=True, text=True)
		if result.returncode:
			raise ValueError('Official icons/generator must be tracked by Git: ' + result.stderr.strip())
	return {name: hashlib.sha256(value).hexdigest() for name, value in data.items()}


def stage_runtime_icons(repo: Path, runtime: Path, target: str) -> None:
	check_icon_sources(repo)
	files = runtime_icon_files(repo, target)
	assets = (module_root(repo) / 'Resources/Assets').resolve()
	if runtime.resolve().is_relative_to(assets) or assets.is_relative_to(runtime.resolve()):
		raise ValueError('Runtime icon destination overlaps source assets')
	icon_root = runtime / RUNTIME_ICON_ROOT
	if _linked(icon_root):
		raise ValueError('Runtime icon destination contains a link')
	expected = {Path(name).name for name in files}
	if icon_root.exists() and (not icon_root.is_dir() or {path.name for path in icon_root.iterdir()} - expected):
		raise ValueError('Runtime icon destination contains unexpected files')
	for relative, source in files.items():
		destination = runtime / relative
		if _linked(destination):
			raise ValueError('Runtime icon destination contains a link')
		destination.parent.mkdir(parents=True, exist_ok=True)
		shutil.copyfile(source, destination)
	verify_runtime_icons(repo, runtime, target)


def verify_runtime_icons(repo: Path, runtime: Path, target: str) -> None:
	check_icon_sources(repo)
	for relative, source in runtime_icon_files(repo, target).items():
		if _regular_file(runtime / relative) != source.read_bytes():
			raise ValueError(f'Stale Runtime icon: {relative}')
	root = runtime / RUNTIME_ICON_ROOT
	if {p.name for p in root.iterdir()} != {Path(name).name for name in runtime_icon_files(repo, target)}:
		raise ValueError('Unexpected Runtime icon files')
	verify_product_executable_icons(repo, runtime, target)


def verify_product_executable_icons(repo: Path, root: Path, target: str) -> None:
	if _linked(root) or not root.is_dir():
		raise ValueError(f'Missing or linked executable icon input: {root}')
	if target.startswith('win-'):
		for file in root.rglob('*'):
			if file.name.casefold() in PRODUCT_EXECUTABLES and file.is_file():
				if _linked(file):
					raise ValueError(f'Linked product executable: {file}')
				with file.open('rb') as stream:
					verify_windows_executable_icon(repo, stream, file)


def verify_runtime_icon_entries(repo: Path, entries: dict, prefix: str, target: str) -> None:
	check_icon_sources(repo)
	files = runtime_icon_files(repo, target)
	for relative, source in files.items():
		if entries.get(prefix + relative, (b'', 0))[0] != source.read_bytes():
			raise ValueError(f'Missing/stale Runtime icon in archive: {relative}')
	if {name for name in entries if name.startswith(prefix + RUNTIME_ICON_ROOT + '/')} != {prefix + name for name in files}:
		raise ValueError('Unexpected Runtime icon files in archive')


def verify_runtime_bundle(repo: Path, bundle: Path, target: str) -> None:
	manifest = json.loads(_regular_file(bundle / 'manifest.json', limit=128 * 1024 * 1024).decode('utf-8-sig'))
	components = [component for component in manifest['components'] if component['id'] == 'runtime']
	if len(components) != 1 or manifest['platform'] != ('windows' if target.startswith('win-') else target.split('-')[0]) or manifest['architecture'] != target.split('-')[1]:
		raise ValueError('Runtime bundle target/component mismatch')
	component = components[0]
	relative = PurePosixPath(component['payloadDirectory'])
	if relative.is_absolute() or '..' in relative.parts or '\\' in str(relative) or ':' in str(relative):
		raise ValueError('Unsafe Runtime bundle payload path')
	verify_runtime_icons(repo, bundle / relative, target)
	recorded = {file['path']: file for file in component['files']}
	for name, source in runtime_icon_files(repo, target).items():
		file = recorded.get(name, {})
		if file.get('size') != source.stat().st_size or file.get('sha256', '').lower() != hashlib.sha256(source.read_bytes()).hexdigest():
			raise ValueError(f'Runtime bundle does not inventory its icon: {name}')


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('command', choices=('check', 'stage-runtime', 'verify-runtime', 'verify-bundle', 'verify-executables'))
	parser.add_argument('--repo-root', type=Path, default=module_root())
	parser.add_argument('--root', type=Path)
	parser.add_argument('--target', choices=TARGETS)
	parser.add_argument('--require-tracked', action='store_true')
	args = parser.parse_args()
	if args.command == 'check':
		print(json.dumps(check_icon_sources(args.repo_root, require_tracked=args.require_tracked), indent=2))
	else:
		if args.root is None or args.target is None or args.require_tracked:
			parser.error('Runtime operations require --root and --target; --require-tracked belongs to check')
		{'stage-runtime': stage_runtime_icons, 'verify-runtime': verify_runtime_icons,
		 'verify-bundle': verify_runtime_bundle,
		 'verify-executables': verify_product_executable_icons}[args.command](args.repo_root, args.root, args.target)
		print(f'Verified Runtime icons: {args.target} / {args.root}')


if __name__ == '__main__':
	main()
