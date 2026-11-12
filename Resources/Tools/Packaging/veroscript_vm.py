#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Stage the explicit bytecode VM product components and installable backend."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess

from veroscript_jit import build_cache, cmake_enabled


def stage_runtime_from_binaries(binaries: Path, destination: Path) -> None:
	"""Stage the executable-relative VM independently of optional applications."""
	from veroscript_release import linked_path

	source = binaries / 'Vm'
	if not source.is_dir() or linked_path(source):
		raise ValueError('Runtime product is missing its explicit Vm component or contains links')
	paths = list(source.rglob('*'))
	if any(linked_path(path) for path in paths):
		raise ValueError('VM component inputs cannot contain links')
	libraries = [path for path in paths if path.is_file() and path.parent == source
				 and path.name in {'VeroScriptVmBytecode.dll', 'libVeroScriptVmBytecode.so',
								   'libVeroScriptVmBytecode.dylib'}]
	if len(libraries) != 1:
		raise ValueError('Runtime must contain exactly one official VM library')
	vm_destination = destination / 'Vm'
	for path in paths:
		if path.is_file():
			output = vm_destination / path.relative_to(source)
			output.parent.mkdir(parents=True, exist_ok=True)
			if path.resolve() != output.resolve():
				shutil.copy2(path, output)
	if libraries[0].suffix != '.dll':
		return

	def is_windows_runtime(path: Path) -> bool:
		name = path.name.casefold()
		return name.endswith('.dll') and name.startswith(('msvcp', 'vcruntime', 'concrt', 'vcomp'))

	# A developer bin tree may not have deployed CRT dependencies inside Vm yet.
	# Keep the CLI and its loaded backend's matched runtime closures together.
	for directory, output_directory in ((binaries, destination), (destination, vm_destination)):
		for path in directory.iterdir():
			if path.is_file() and is_windows_runtime(path):
				output = output_directory / path.name
				if not output.exists():
					if linked_path(path):
						raise ValueError('Runtime component inputs cannot contain links')
					shutil.copy2(path, output)
		notice = Path('licenses/MicrosoftVisualCpp/NOTICE.txt')
		path, output = directory / notice, output_directory / notice
		if path.is_file() and not output.exists():
			if linked_path(path):
				raise ValueError('Runtime component inputs cannot contain links')
			output.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(path, output)


def install(build: Path, destination: Path, component: str, config: str) -> None:
	settings = build_cache(build)
	if not cmake_enabled(settings, 'VEROSCRIPT_BUILD_VM_BACKEND'):
		raise ValueError('This product requires VEROSCRIPT_BUILD_VM_BACKEND=ON')
	subprocess.run([settings.get('CMAKE_COMMAND', 'cmake'), '--install', str(build),
					'--config', config, '--prefix', str(destination), '--component', component], check=True)


def stage_runtime(build: Path, destination: Path, config: str = 'Release') -> None:
	install(build, destination, 'VeroScriptRuntimeVm', config)
	libraries = [p for p in (destination / 'Vm').glob('*') if p.is_file()
				 and 'VeroScriptVmBytecode' in p.name and p.suffix in {'.dll', '.so', '.dylib'}]
	if len(libraries) != 1:
		raise ValueError('Runtime must contain exactly one official VM library')


def stage_sdk(build: Path, destination: Path, config: str = 'Release') -> None:
	install(build, destination, 'VeroScriptExecutionContract', config)
	install(build, destination, 'VeroScriptBytecodeVmSdk', config)
	stage_runtime(build, destination / 'bin', config)


def stage_backend(build: Path, destination: Path, package_tool: Path, config: str = 'Release', *, package_version: str | None = None) -> None:
	from veroscript_release import version
	expected_version = version(package_version or build_cache(build).get('VS_VEROSCRIPT_VM_VERSION', '0.1.1'))
	install(build, destination, 'VeroScriptBytecodeVmBackend', config)
	settings = build_cache(build)
	if any(destination.glob('*.dylib')):
		from veroscript_native_symbols import strip_macos_tree
		strip_macos_tree(destination, settings.get('CMAKE_STRIP', 'strip'))
	libraries = sorted(p for p in destination.iterdir() if 'VeroScriptVmBytecode' in p.name
					   and p.suffix in {'.dll', '.so', '.dylib'})
	if len(libraries) != 1:
		raise ValueError('Standalone VM package has no unique backend library')
	library = libraries[0]
	from veroscript_tool_runner import output as tool_output
	descriptor = json.loads(tool_output(package_tool,'describe-vm',library))
	if descriptor['id'] != 'vs-bytecode' or descriptor['version'] != expected_version:
		raise ValueError('VM backend identity does not match the product')
	context = descriptor['target']
	target = {key: context[key] for key in ('os', 'arch', 'environment', 'coreBuild')}
	target['abis'] = {'vm': context['abis']['vm']}
	libraries = [{'id': f'runtime-{index}', 'path': path.name, 'linkage': 'dynamic'}
				 for index, path in enumerate(sorted(destination.iterdir()))
				 if path.is_file() and path != library and path.suffix in {'.dll', '.so', '.dylib'}]
	manifest = {
		'format': 'veroscript-package', 'formatVersion': '0.1.1',
		'name': 'vs-bytecode-vm', 'version': descriptor['version'],
		'description': 'Official VeroScript bytecode interpreter backend.',
		'licenseFile': 'LICENSE',
		'requires': {'core': context['core']},
		'target': target,
		'entries': {'backends': [{'service': 'vm', 'id': 'vs-bytecode', 'contract': 'vm',
								  'path': library.name, 'libraries': [item['id'] for item in libraries]}],
					'libraries': libraries},
	}
	(destination / 'veroscript-package.json').write_text(
		json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def pack_backend(source: Path, archive: Path, package_tool: Path) -> None:
	"""Use the public pack command: installable archives have a root manifest."""
	from veroscript_tool_runner import run as tool_run
	tool_run(package_tool,'command','pack',source,'--output='+str(archive))


def verify_installable_backend(entries, package_version: str, target: str) -> None:
	from veroscript_release import TARGETS, native_identity, verify_windows_runtime
	from veroscript_products import verify_module_payload
	verify_module_payload(entries)
	try:
		manifest = json.loads(entries['veroscript-package.json'][0])
		platform = TARGETS[target]
		library = ('VeroScriptVmBytecode.dll' if platform[0] == 'windows' else
				   'libVeroScriptVmBytecode.' + ('so' if platform[0] == 'linux' else 'dylib'))
		libraries = manifest['entries'].get('libraries', [])
		expected_entry = {'service': 'vm', 'id': 'vs-bytecode', 'contract': 'vm', 'path': library,
						  'libraries': [item['id'] for item in libraries]}
		if (manifest.get('format') != 'veroscript-package' or manifest.get('formatVersion') != '0.1.1'
				or manifest.get('name') != 'vs-bytecode-vm' or manifest.get('version') != package_version
				or (manifest['target'].get('os'), manifest['target'].get('arch')) != platform
				or not manifest['target'].get('coreBuild') or not manifest['target'].get('environment')
				or not manifest['target'].get('abis', {}).get('vm')
				or manifest.get('entries') != {'backends': [expected_entry], 'libraries': libraries}
				or any(item.get('linkage') != 'dynamic' or not entries[item['path']][0] for item in libraries)
				or manifest.get('licenseFile') != 'LICENSE' or not entries['LICENSE'][0]
				or native_identity(entries[library][0][:4096]) != platform):
			raise ValueError('Installable VM identity/entry mismatch')
		if platform[0] == 'windows':
			verify_windows_runtime(entries, platform)
	except (KeyError, TypeError, IndexError) as error:
		raise ValueError('Missing/invalid installable VM metadata') from error


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--build-directory', required=True, type=Path)
	parser.add_argument('--destination', required=True, type=Path)
	parser.add_argument('--kind', choices=('runtime', 'sdk', 'backend'), required=True)
	parser.add_argument('--package-tool', type=Path)
	parser.add_argument('--config', default='Release')
	parser.add_argument('--version', help='Expected VM product version; defaults to the configured build version')
	args = parser.parse_args()
	build, destination = args.build_directory.resolve(), args.destination.resolve()
	if args.kind == 'backend':
		if not args.package_tool:
			parser.error('--kind backend requires a matching host --package-tool')
		stage_backend(build, destination, args.package_tool.resolve(), args.config, package_version=args.version)
	else:
		(stage_runtime if args.kind == 'runtime' else stage_sdk)(build, destination, args.config)


if __name__ == '__main__':
	main()
