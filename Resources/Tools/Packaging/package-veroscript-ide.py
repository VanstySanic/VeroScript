# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build verified VS Code/Rider packages and optionally publish selected targets."""
import argparse
import json
from pathlib import Path
from veroscript_paths import module_root, repository_root
from veroscript_business_release import IDE_BUSINESSES, load_versions, publish_businesses
from veroscript_release import TARGETS, host_target, safe_output, write_manifest
from veroscript_ide import check_ide_sources, check_ide_prerequisites, stage_ide


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--versions-file', type=Path)
	parser.add_argument('--business', choices=IDE_BUSINESSES, action='append')
	parser.add_argument('--runtime-directory', type=Path)
	parser.add_argument('--jit-backend', type=Path)
	parser.add_argument('--out-root', type=Path, help='Dedicated empty staging directory')
	parser.add_argument('--check-sources', action='store_true', help='Check IDE version metadata before a full native build')
	parser.add_argument('--check-prerequisites', action='store_true', help='Check required IDE tools without building or packaging')
	parser.add_argument('--target', choices=tuple(TARGETS), action='append', help='Publication targets; portable plugin bytes are reused')
	parser.add_argument('--publish', action='store_true')
	parser.add_argument('--release-directory', type=Path)
	args = parser.parse_args()
	if args.target and len(args.target) != len(set(args.target)):
		parser.error('Duplicate publication targets')
	if (args.check_sources or args.check_prerequisites) and args.publish:
		parser.error('Read-only IDE preflight cannot publish artifacts')
	repo = args.repo_root.resolve()
	selected = args.business or list(IDE_BUSINESSES)
	config = load_versions(repo, args.versions_file, businesses=selected)
	check_ide_sources(repo, config['businesses'], selected)
	if args.check_prerequisites:
		check_ide_prerequisites(repo, selected)
		print('IDE packaging prerequisites verified')
	if args.check_sources or args.check_prerequisites:
		print('IDE source version metadata verified')
		return
	if not args.runtime_directory or not args.out_root:
		parser.error('--runtime-directory and --out-root are required for packaging')
	check_ide_prerequisites(repo, selected)
	output = safe_output(repo, repo / args.out_root, component=True)
	if output.exists() and any(output.iterdir()):
		raise ValueError('IDE staging directory must be empty')
	output.mkdir(parents=True, exist_ok=True)
	dependencies = {}
	identities = stage_ide(repo, output, config['businesses'], selected, args.runtime_directory, args.jit_backend,
		dependency_pins=dependencies)
	for target in args.target or [host_target()]:
		write_manifest(output, config['nativeVersion'], target, next(iter(identities.values())),
					   business_versions=config['businesses'], native_version=config['nativeVersion'])
		path = output / 'release-manifest.json'
		manifest = json.loads(path.read_text(encoding='utf-8'))
		manifest.update(selectedBusinesses=selected, businessBuildIds=identities)
		if dependencies:
			manifest['ideDependencies'] = dependencies
		path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
		from veroscript_release import verify_release
		verify_release(output, repo, businesses=set(selected))
		if args.publish:
			publish_businesses(repo, output, args.release_directory or module_root(repo) / 'Release', selected)
	print(f'IDE packages verified: {output}')


if __name__ == '__main__':
	main()
