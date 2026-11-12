#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import argparse
import json
from pathlib import Path
from veroscript_paths import module_root, repository_root, module_route, source_route
from veroscript_release import RELEASE_TARGETS, host_target, verify_release, version
from veroscript_products import target_businesses, DISTRIBUTION_SCOPES
from veroscript_business_release import verify_collection, require_business_layout

def main():
	parser = argparse.ArgumentParser(description="Audit native and mobile VeroScript releases without installing them.")
	parser.add_argument("--release-directory", type=Path)
	parser.add_argument('--target', choices=DISTRIBUTION_SCOPES, help='Release target or shared distribution scope')
	parser.add_argument("--repo-root", type=Path, default=repository_root())
	parser.add_argument('--require-complete', action='store_true', help='Require every business supported by the selected target, with no partial products')
	parser.add_argument('--check-source', action='store_true', help='Compare packaged public assets with the current repository')
	parser.add_argument('--version', type=version, help='Require this actual version for each selected business')
	args = parser.parse_args()
	target = args.target or host_target()
	root = (args.release_directory or module_root(args.repo_root) / 'Release').resolve()
	if (root / 'release-index.json').is_file():
		index = verify_collection(root, target, args.repo_root.resolve() if args.check_source else None,
								  require_complete=args.require_complete)
		if args.version and any(ref['version'] != args.version for entry in index['businesses'].values() for ref in entry['manifests']):
			raise ValueError('Requested version does not match an indexed business version')
		print(f"Business release verified: {len(index['businesses'])} businesses, {target}")
		return
	# A transient candidate has its own audit contract. It is never an old
	# published collection and must be outside the managed Release directory.
	if root == module_root(args.repo_root) / 'Release':
		require_business_layout(root)
		raise ValueError('Managed Release requires release-index.json')
	identity = json.loads((root / 'release-manifest.json').read_text(encoding='utf-8-sig'))
	selected = identity.get('selectedBusinesses')
	versions = identity.get('businessVersions') or {}
	values = [value for business, value in versions.items() if not selected or business in selected] or [identity.get('version')]
	if args.version and any(value != args.version for value in values):
		raise ValueError('Requested version does not match the selected candidate versions')
	if args.target and args.target not in RELEASE_TARGETS:
		raise ValueError('A candidate requires a native/mobile target')
	if args.target and RELEASE_TARGETS[args.target] != (identity.get('platform'), identity.get('architecture')):
		raise ValueError('Requested target does not match the release manifest')
	partial = set(identity.get('partialBusinesses', []))
	if args.require_complete and (not set(target_businesses(identity.get('target', target))).issubset(selected or ()) or partial):
		raise ValueError('Incomplete publication: all release businesses are required without partial products')
	if partial and (not selected or not partial.issubset(selected)):
		raise ValueError('Partial businesses must belong to the release selection')
	manifest = verify_release(root, args.repo_root.resolve() if args.check_source else None,
							  businesses=set(selected) if selected else None, partial_businesses=partial)
	print(f"Release verified: {len(manifest['artifacts'])} files, {manifest['platform']}-{manifest['architecture']}, build {manifest['buildId']}")

if __name__ == "__main__":
	main()
