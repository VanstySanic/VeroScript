# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Fetch the pinned optional wgpu-native shared SDK without modifying source or existing SDKs."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import tempfile
import urllib.request
import zipfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--destination', type=Path, required=True)
	parser.add_argument('--platform', help='Manifest platform key; defaults to the current desktop platform')
	args = parser.parse_args()
	module = Path(__file__).resolve().parents[2]
	manifest = json.loads((module/'ThirdParty/WgpuNative.version.json').read_text(encoding='utf-8'))
	machine = {'amd64': 'x86_64', 'arm64': 'aarch64'}.get(platform.machine().lower(), platform.machine().lower())
	system = {'Windows': 'windows', 'Linux': 'linux', 'Darwin': 'macos'}.get(platform.system(), '')
	key = args.platform or f'{system}-{machine}' + ('-msvc' if system == 'windows' else '')
	asset = manifest['assets'].get(key)
	if not asset:
		parser.error(f'Unsupported platform {key}; available: {", ".join(manifest["assets"])}')
	destination = args.destination.resolve()
	if destination.exists():
		parser.error(f'Destination already exists: {destination}; choose a new SDK directory')
	destination.parent.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory(prefix='.wgpu-native-', dir=destination.parent) as work:
		staging = Path(work).resolve()
		archive = staging/'sdk.zip'
		digest = hashlib.sha256()
		with urllib.request.urlopen(asset['url'], timeout=90) as response, archive.open('wb') as output:
			while data := response.read(1024*1024):
				digest.update(data)
				output.write(data)
		if digest.hexdigest() != asset['sha256']:
			raise SystemExit('SDK archive does not match the pinned manifest')
		unpacked = staging/'sdk'
		with zipfile.ZipFile(archive) as source:
			for entry in source.infolist():
				if not (unpacked/entry.filename).resolve().is_relative_to(unpacked.resolve()):
					raise SystemExit('SDK archive has an invalid path')
			source.extractall(unpacked)
		stamp = unpacked/'wgpu-native-meta/wgpu-native-git-tag'
		if stamp.read_text().strip() != manifest['tag']:
			raise SystemExit('SDK tag does not match the pinned manifest')
		if staging.parent != destination.parent or destination.exists():
			raise SystemExit('SDK destination changed during extraction')
		unpacked.rename(destination)
	print(f'wgpu-native {manifest["tag"]}: {destination}')
	print(f'Configure with -DVEROSCRIPT_SDL3_WEBGPU=ON -DVEROSCRIPT_WEBGPU_ROOT={destination.as_posix()}')


if __name__ == '__main__':
	main()
