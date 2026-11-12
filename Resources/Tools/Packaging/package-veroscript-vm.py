#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build the independent, installable vs-bytecode-vm distribution."""
import argparse
from pathlib import Path
import shutil
import tempfile

from veroscript_paths import repository_root, module_root
from veroscript_jit import build_cache
from veroscript_vm import stage_backend, pack_backend
from veroscript_release import archive_tree, host_target, safe_output, scratch_directory, version
from veroscript_business_release import load_versions, publish_component
from veroscript_products import BYTECODE_VM_PACKAGE_NAME


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--build-directory', type=Path, required=True)
	parser.add_argument('--target', default=host_target())
	parser.add_argument('--version', type=version)
	parser.add_argument('--config', default='Release')
	parser.add_argument('--package-tool', type=Path)
	parser.add_argument('--out-root', type=Path)
	args = parser.parse_args()
	repo = args.repo_root.resolve()
	build = (repo / args.build_directory).resolve()
	settings = build_cache(build)
	if Path(settings['CMAKE_HOME_DIRECTORY']).resolve() != repo:
		raise ValueError('VM build belongs to another source tree')
	package_version = args.version or load_versions(repo)['businesses']['Vm']
	if package_version != settings.get('VS_VEROSCRIPT_VM_VERSION', '0.1.1'):
		raise ValueError('Rebuild the VM when its product version changes')
	# A cross-built backend is not executed by this host-side descriptor tool.
	if args.target != host_target():
		raise ValueError('Package the VM using a matching target host; do not execute target libraries here')
	tool = args.package_tool
	if tool is None:
		filename = 'VeroScriptModulePackageTool' + ('.exe' if args.target.startswith('win-') else '')
		tool = next((p for p in (build / 'bin' / args.config / filename, build / 'bin' / filename) if p.is_file()), None)
	if tool is None or not tool.is_file():
		raise ValueError('Build the matching VeroScriptModulePackageTool or pass --package-tool')
	name = f'{BYTECODE_VM_PACKAGE_NAME}-{package_version}-{args.target}'
	suffix = '.zip' if args.target.startswith('win-') else '.tar.gz'
	output = safe_output(repo, repo / args.out_root, component=True) if args.out_root else None
	with tempfile.TemporaryDirectory(prefix='veroscript-vm-', dir=scratch_directory(repo)) as temporary:
		stage = Path(temporary)
		package = stage / name
		stage_backend(build, package, tool.resolve(), args.config, package_version=package_version)
		archive = stage / (name + suffix)
		archive_tree(package, archive)
		installable = stage / (name + '.vspkg')
		pack_backend(package, installable, tool.resolve())
		from veroscript_module_symbols import assemble_module_symbols
		assemble_module_symbols(repo, {'Vm': build}, stage, {'Vm': package_version}, args.target, ['Vm'], args.config)
		if output is None:
			publish_component(repo, stage, module_root(repo) / 'Release', 'Vm', package_version, args.target)
		else:
			output.mkdir(parents=True, exist_ok=True)
			if (output / name).exists() or any(p.name not in {name, archive.name, installable.name, 'symbols'} for p in output.iterdir()):
				raise ValueError('Choose an empty VM component output directory')
			shutil.copytree(package, output / name)
			shutil.copy2(archive, output / archive.name)
			shutil.copy2(installable, output / installable.name)
			if (stage / 'symbols').is_dir():
				shutil.copytree(stage / 'symbols', output / 'symbols')
		print(f'VM package: {name}')


if __name__ == '__main__':
	main()
