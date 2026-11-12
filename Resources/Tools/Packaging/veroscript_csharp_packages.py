# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Stage one canonical C# package inventory and the SDK's offline feeds."""
from pathlib import Path
import argparse
import os
import shutil
import xml.etree.ElementTree as ET
import zipfile

from veroscript_release import digest, version

PACKAGE_IDS = ('VeroScript.CSharp', 'VeroScript.CSharpFrontend', 'VeroScript.CSharpBindGen',
	'VeroScript.CSharpBindGen.Tool', 'VeroScript.CSharpTranspiler', 'VeroScript.CSharpTranspiler.Tool')


def csharp_package_inventory(source: Path, package_version: str) -> list[tuple[str, Path, str]]:
	package_version = version(package_version)
	source = Path(source).resolve()
	packages = []
	for package_id in PACKAGE_IDS:
		for suffix in ('nupkg', 'snupkg'):
			path = source / f'{package_id}.{package_version}.{suffix}'
			if not path.is_file() or path.is_symlink():
				raise ValueError('Missing canonical C# package: ' + str(path))
			with zipfile.ZipFile(path) as archive:
				# A canonical feed is shared by Windows and mobile SDKs too. Reject
				# aliases and non-portable names before copying it into any product.
				from veroscript_mobile_release import validate_zip_members
				validate_zip_members(archive.infolist())
				nuspecs = [name for name in archive.namelist() if name.endswith('.nuspec')]
				if len(nuspecs) != 1 or archive.testzip() is not None:
					raise ValueError('Invalid canonical C# package: ' + str(path))
				metadata = ET.fromstring(archive.read(nuspecs[0]))
				values = {node.tag.rsplit('}', 1)[-1]: node.text for node in metadata.iter()}
				if values.get('id') != package_id or values.get('version') != package_version:
					raise ValueError('Canonical C# package identity mismatch: ' + str(path))
			packages.append((package_id, path, digest(path)))
	return packages


def stage_csharp_packages(build: Path, runtime: Path, tools: Path, output: Path,
						 package_version: str, canonical: Path | None = None) -> dict[str, str]:
	configured = os.environ.get('VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY')
	packages = csharp_package_inventory(Path(canonical or configured or build / 'nuget'), package_version)
	# Validate the complete inventory before changing either offline feed.
	records = {}
	for package_id, path, checksum in packages:
		feed = (runtime if package_id == 'VeroScript.CSharp' else tools) / 'share/VeroScript/nuget'
		for destination in (output / path.name, feed / path.name):
			destination.parent.mkdir(parents=True, exist_ok=True)
			if destination.resolve() != path:
				shutil.copy2(path, destination)
			if digest(destination) != checksum:
				raise ValueError('Canonical C# package copy differs: ' + str(destination))
		records[path.name] = checksum
	return records


if __name__ == '__main__':
	parser = argparse.ArgumentParser(description=__doc__)
	for name in ('build', 'runtime', 'tools', 'output'):
		parser.add_argument('--' + name, type=Path, required=True)
	parser.add_argument('--version', type=version, required=True)
	parser.add_argument('--canonical', type=Path)
	args = parser.parse_args()
	stage_csharp_packages(args.build, args.runtime, args.tools, args.output, args.version, args.canonical)
