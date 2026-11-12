# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Pass Ninja's UTF-8 response files to the MSVC linker with a BOM."""
from codecs import BOM_UTF8
from pathlib import Path
import subprocess
import sys
import tempfile


def link(command):
	if Path(command[0]).name.lower() != 'link.exe' or not any(arg.startswith('@') for arg in command[1:]):
		return subprocess.call(command)
	# Keep the original files owned by Ninja and keep @file paths relative.
	with tempfile.TemporaryDirectory(prefix='vs-link-', dir=Path.cwd()) as directory:
		work = Path(directory).relative_to(Path.cwd())
		for index, argument in enumerate(command[1:], 1):
			if not argument.startswith('@'):
				continue
			data = Path(argument[1:]).read_bytes()
			if data.isascii() or data.startswith(BOM_UTF8) or b'\0' in data:
				continue
			try:
				data.decode('utf-8')
			except UnicodeDecodeError:
				continue
			response = work / f'{index}.rsp'
			response.write_bytes(BOM_UTF8 + data)
			command[index] = '@' + str(response)
		return subprocess.call(command)


if __name__ == '__main__':
	raise SystemExit(link(sys.argv[1:]))
