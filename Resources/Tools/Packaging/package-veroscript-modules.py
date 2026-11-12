#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build, verify and package independent native module SDKs."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from veroscript_paths import repository_root
from veroscript_products import FEATURE_MODULE_BUSINESSES, FOUNDATION_BUSINESSES, SDK_MODULE_BUSINESSES
from veroscript_business_release import collection_root, load_versions, publish_businesses, verify_collection
from veroscript_release import host_target, safe_output, scratch_directory, verify_release, write_manifest
from veroscript_modules import assemble_modules, build_cache, enabled, module_selection


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--build-directory', type=Path, help='A combined root build containing all selected modules')
	parser.add_argument('--json-build-directory', type=Path)
	parser.add_argument('--network-build-directory', type=Path)
	parser.add_argument('--foundation-build-directory', type=Path)
	parser.add_argument('--boost-build-directory', type=Path)
	parser.add_argument('--curl-build-directory', type=Path)
	parser.add_argument('--business', action='append', choices=SDK_MODULE_BUSINESSES)
	parser.add_argument('--versions-file', type=Path)
	parser.add_argument('--version')
	parser.add_argument('--target')
	outputs = parser.add_mutually_exclusive_group()
	outputs.add_argument('--out-root', type=Path, help='Transactional release collection (default: Release)')
	outputs.add_argument('--candidate-directory', type=Path, help='New directory for a verified candidate, without publishing a collection')
	parser.add_argument('--skip-build', action='store_true', help='Reuse binaries; installed package consumers still run')
	parser.add_argument('--data-only', action='store_true', help='Package data SDKs without script registration')
	parser.add_argument('--core-sdk', type=Path, help='Matching installed Core SDK for registration consumers')
	parser.add_argument('--cpp-sdk', type=Path, help='Matching C++ Runtime SDK prefix for Gui script consumers')
	parser.add_argument('--package-tool', type=Path, help='VeroScriptModulePackageTool from a matching Core build')
	parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 8))
	args = parser.parse_args()
	repo = args.repo_root.resolve()
	target = args.target or host_target()
	if target != host_target():
		raise ValueError('Module publication requires its native host to run package consumers')
	if args.jobs < 1:
		raise ValueError('--jobs must be positive')
	selected = module_selection(args.business or FEATURE_MODULE_BUSINESSES)
	builds = {}
	for business in set(selected) | (set(FOUNDATION_BUSINESSES) if set(selected).intersection(FOUNDATION_BUSINESSES) else set()) | (set(("Network", "Http", "WebSocket")) if "Boost" in selected else set()):
		build = (args.foundation_build_directory if business in FOUNDATION_BUSINESSES else
				 args.json_build_directory if business == 'Json' else
				 (args.boost_build_directory or args.network_build_directory) if business == 'Boost' else
				 (args.curl_build_directory or args.network_build_directory) if business == 'Curl' else (args.network_build_directory or args.boost_build_directory or args.curl_build_directory))
		build = build or args.build_directory
		if build is None:
			raise ValueError('Provide --build-directory or the corresponding module build directory')
		builds[business] = (repo / build).resolve()
	selected = module_selection(selected, builds)
	foundation = [name for name in selected if name in FOUNDATION_BUSINESSES]
	if foundation:
		from veroscript_foundation import foundation_selection
		selected = module_selection([*[name for name in selected if name not in FOUNDATION_BUSINESSES],
			*foundation_selection(foundation, builds, not args.data_only)])
	versions = load_versions(repo, args.versions_file, args.version, selected)
	output = (safe_output(repo, repo / args.candidate_directory, component=True) if args.candidate_directory else
			  collection_root(repo, (repo / args.out_root if args.out_root else repo / 'Release')))
	if args.candidate_directory and output.exists():
		raise ValueError('Candidate output must be a new directory')
	builds = {name: builds[name] for name in selected}
	for build in dict.fromkeys(builds.values()):
		settings = build_cache(build)
		cmake = settings.get('CMAKE_COMMAND', 'cmake')
		if not args.skip_build:
			targets = []
			for business in selected:
				if builds[business] != build:
					continue
				if business in FOUNDATION_BUSINESSES:
					targets += ['VeroScript' + business]
					if not args.data_only:
						from veroscript_foundation import foundation_script_targets
						targets += ['VeroScript' + name for name in foundation_script_targets(business)]
				else:
					targets += ['VeroScript' + business + ('' if args.data_only else 'Module')]
			if not args.data_only and not args.package_tool:
				if Path(settings['CMAKE_HOME_DIRECTORY']).resolve() == repo:
					targets += ['VeroScriptModulePackageTool']
			# Installed Gui packages are consumed through the desktop Runtime.
			# Building its dependencies alone leaves an existing CLI embedding
			# the previous script library, even when the SDK itself is current.
			if (not args.data_only and 'Gui' in selected and builds['Gui'] == build and
					Path(settings['CMAKE_HOME_DIRECTORY']).resolve() == repo):
				if any(enabled(settings.get(mode, 'OFF')) for mode in (
						'VS_BUILD_VEROSCRIPT_EMBEDDED_ONLY', 'VS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY',
						'VS_BUILD_VEROSCRIPT_PYTHON_ONLY')):
					raise ValueError('Installable Gui verification requires a desktop Runtime build')
				targets += ['VeroScriptCli']
			# Foundation SDK execution regressions load an explicit VM module, even when a
			# separately supplied package tool already embeds its own matching interpreter.
			if (not args.data_only and enabled(settings.get('VEROSCRIPT_BUILD_VM_BACKEND', 'OFF')) and
					Path(settings['CMAKE_HOME_DIRECTORY']).resolve() == repo and
					any(business in ('Platform', 'Graphics', 'Audio', 'Sdl3') and builds[business] == build
						for business in selected)):
				targets += ['VeroScriptBytecodeVmBackend']
			subprocess.run([cmake, '--build', str(build), '--config', 'Release', '--parallel', str(args.jobs),
							'--target', *targets], check=True)
	with tempfile.TemporaryDirectory(prefix='veroscript-modules-release-', dir=scratch_directory(repo)) as temporary:
		candidate = Path(temporary) / 'release'
		symbol_status = {}
		identities = assemble_modules(repo, builds, candidate, versions['businesses'], target, selected,
									  bindings=not args.data_only, core_sdk=args.core_sdk, package_tool=args.package_tool, cpp_sdk=args.cpp_sdk, symbol_status=symbol_status)
		write_manifest(candidate, versions['nativeVersion'], target, next(iter(identities.values())),
					   business_versions=versions['businesses'], native_version=versions['nativeVersion'])
		manifest_path = candidate / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
		manifest.update(selectedBusinesses=selected, businessBuildIds=identities, functionalPayloadBusinesses=selected)
		manifest['installableModuleBusinesses'] = selected if not args.data_only else []
		from veroscript_module_symbols import portable_symbol_status
		manifest['moduleSymbolStatus'] = portable_symbol_status(symbol_status)
		manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
		verify_release(candidate, repo, businesses=set(selected))
		# Consumers configure against the unpacked archives, in a directory with
		# spaces and Unicode, without borrowing headers from the source build.
		from veroscript_module_consumers import verify_consumers
		verify_consumers(repo, candidate, builds, selected, target, args.core_sdk, bindings=not args.data_only, jobs=args.jobs, cpp_sdk=args.cpp_sdk)
		if manifest['installableModuleBusinesses']:
			from veroscript_module_packages import verify_installable_consumers
			verify_installable_consumers(repo, candidate, builds, manifest['installableModuleBusinesses'], target, args.package_tool)
		if args.candidate_directory:
			output.parent.mkdir(parents=True, exist_ok=True)
			candidate.rename(output)
			print('Verified VeroScript module candidate: ' + str(output))
			return
		publish_businesses(repo, candidate, output, selected)
	verify_collection(output, target)
	print('VeroScript module releases: ' + str(output))


if __name__ == '__main__':
	main()
