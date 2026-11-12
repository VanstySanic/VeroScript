#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Validate explicit desktop CPython headers/ABI without executing a target interpreter."""
import argparse
import json
from pathlib import Path
import re
import struct


def validate_sdk(path, system, architecture, pointer_size):
	data = json.loads(Path(path).read_text(encoding='utf-8-sig'))
	systems = {'Windows': 'windows', 'Linux': 'linux', 'Darwin': 'macos'}
	arches = {'aarch64': 'arm64', 'ARM64': 'arm64', 'arm64': 'arm64', 'x86_64': 'x64',
		'AMD64': 'x64', 'x64': 'x64', 'x86': 'x86', 'i686': 'x86'}
	if (data.get('schemaVersion') != 1 or data.get('platform') != systems.get(system)
		or data.get('architecture') != arches.get(architecture)
		or data.get('pointerSize') != int(pointer_size)
		or int(pointer_size) != (4 if data.get('architecture') == 'x86' else 8)):
		raise ValueError('Target CPython platform, CPU or pointer size disagrees with the compiler')
	version = data.get('pythonVersion', '')
	if not re.fullmatch(r'3\.(11|12|13|14|15)\.\d+', version):
		raise ValueError('Desktop CPython must be 3.11–3.15')
	include = Path(data['includeDirectory']).resolve()
	if not (include / 'Python.h').is_file() or not (include / 'pyconfig.h').is_file():
		raise ValueError('Missing target Python.h/pyconfig.h')
	defines = (include / 'patchlevel.h').read_text(encoding='utf-8')
	for name, value in zip(('MAJOR', 'MINOR', 'MICRO'), version.split('.')):
		if not re.search(r'^#define\s+PY_' + name + r'_VERSION\s+' + value + r'\s*$', defines, re.M):
			raise ValueError('CPython headers and declared version disagree')
	minor = version.split('.')[1]
	suffix = data.get('extensionSuffix', '')
	expected = (f'.cp3{minor}-' + {'x64': 'win_amd64', 'arm64': 'win_arm64', 'x86': 'win32'}[data['architecture']] + '.pyd'
		if system == 'Windows' else f'.cpython-3{minor}-darwin.so' if system == 'Darwin'
		else f'.cpython-3{minor}-' + {'x64': 'x86_64', 'arm64': 'aarch64', 'x86': 'i386'}[data['architecture']] + '-linux-gnu.so')
	if suffix != expected:
		raise ValueError('Extension suffix does not identify the target CPython ABI')
	if system == 'Windows':
		library = Path(data.get('importLibrary', '')).resolve()
		if not library.is_file() or library.read_bytes()[:8] != b'!<arch>\n':
			raise ValueError('Missing target CPython import library')
		data['importLibrary'] = str(library)
	data['includeDirectory'] = str(include)
	return data


if __name__ == '__main__':
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--sdk', type=Path, required=True)
	parser.add_argument('--system', choices=('Windows', 'Linux', 'Darwin'), required=True)
	parser.add_argument('--architecture', required=True)
	parser.add_argument('--pointer-size', type=int, required=True)
	args = parser.parse_args()
	try:
		print(json.dumps(validate_sdk(args.sdk, args.system, args.architecture, args.pointer_size)))
	except (ValueError, KeyError, OSError) as error:
		parser.error(str(error))
