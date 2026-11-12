#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Preview release directories and artifact names without building or publishing."""
import argparse
import json
from pathlib import Path

from veroscript_paths import repository_root
from veroscript_products import (ALL_BUSINESSES, IDE_PRODUCTS, INSTALLABLE_PACKAGE_NAMES,
	INSTALLER_BUSINESSES, business_route, check_product_inventory, installer_filename, target_businesses, mobile_coverage)
from veroscript_business_release import load_versions, PREFIXES
from veroscript_release import PACKAGES, RELEASE_TARGETS, MOBILE_TARGETS, version


def release_plan(repo, config, targets, businesses=None):
	from veroscript_business_release import destination_name, artifact_scope, COLLECTION_SCHEMA, INDEX_SCHEMA, BUSINESS_SCHEMA
	selected = businesses or list(ALL_BUSINESSES)
	if (not selected or len(set(selected)) != len(selected) or not set(selected).issubset(ALL_BUSINESSES)
			or not targets or len(set(targets)) != len(targets) or not set(targets).issubset(RELEASE_TARGETS)):
		raise ValueError('Select known, unique release businesses and targets')
	if businesses and any(not any(b in target_businesses(t, optional=True) for t in targets) for b in selected):
		raise ValueError('Selected business is not supported on any selected target')
	root = {'release-index.json': None, 'indexes': {}}
	selections, optional, planned, required_artifacts = {}, [], set(), set()
	def add(business, target, source, optional_file=False):
		v = version(config['businesses'][business])
		scope = artifact_scope(source, business, target)
		parts = (*Path(business_route(business)).parts, scope, v)
		name = destination_name(source, business)
		if optional_file:
			optional.append('/'.join(('Release', *parts, name)))
			return
		required_artifacts.add('/'.join(('Release', *parts, name)))
		node = root
		for part in parts:
			node = node.setdefault(part, {})
		node['release-manifest.json'] = None
		node['checksums.txt'] = None
		for part in Path(name).parts[:-1]:
			node = node.setdefault(part, {})
		node[Path(name).name] = None
		root['indexes'][target + '.json'] = None
		if scope != target:
			root['indexes'][scope + '.json'] = None
		ref = {'target': scope, 'version': v, 'manifest': '/'.join((*parts, 'release-manifest.json'))}
		refs = selections.setdefault(target, {}).setdefault(business, [])
		if ref not in refs:
			refs.append(ref)
		planned.add(business)
	for target in targets:
		for business in selected:
			if business not in target_businesses(target, optional=businesses is not None):
				continue
			v = version(config['businesses'][business])
			if target in MOBILE_TARGETS:
				from veroscript_mobile_release import artifact_names, optional_artifact_names
				for filename in artifact_names(business, target, v):
					add(business, target, 'mobile/' + filename)
				for filename in optional_artifact_names(business, target, v):
					add(business, target, 'mobile/' + filename, True)
				continue
			suffix = '.zip' if target.startswith('win-') else '.tar.gz'
			for category, names in PACKAGES.items():
				for prefix in names:
					if PREFIXES[prefix] == business:
						add(business, target, f'{category}/{prefix}-{v}-{target}{suffix}')
			if business in INSTALLABLE_PACKAGE_NAMES:
				add(business, target, f'modules/{INSTALLABLE_PACKAGE_NAMES[business]}-{v}-{target}.vspkg')
			if business in IDE_PRODUCTS:
				product = IDE_PRODUCTS[business]
				add(business, target, 'ide/' + product['artifact'] + '-' + v + ('.vsix' if product['family'] == 'vscode' else '.zip'))
			if business in INSTALLER_BUSINESSES:
				add(business, target, 'installer/' + installer_filename(business, v, target))
			if business == 'WebAssembly':
				add(business, target, f'webassembly/VeroScriptWebAssemblySDK-{v}-wasm32.zip')
			if business == 'Python':
				from veroscript_python_wheels import supported_versions
				add(business, target, f'distributions/python/veroscript-{v}.tar.gz')
				for minor in supported_versions(repo):
					add(business, target, f'distributions/python/veroscript-{v}-cp{minor.replace(".", "")}-cp{minor.replace(".", "")}-<wheel-platform>.whl')
			if business == 'CSharp':
				from veroscript_csharp_packages import PACKAGE_IDS
				for package_id in PACKAGE_IDS:
					for extension in ('nupkg', 'snupkg'):
						add(business, target, f'distributions/csharp/{package_id}.{v}.{extension}')
			if business == 'Resources':
				for name in ('Assets', 'Docs', 'Scripts', 'Examples'):
					add(business, target, f'Resources/{name}/**')
	return {'format': 'VeroScript expected release layout', 'previewOnly': True,
		'collectionSchemaVersion': COLLECTION_SCHEMA, 'collectionIndexSchemaVersion': INDEX_SCHEMA, 'businessManifestSchemaVersion': BUSINESS_SCHEMA,
		'businessVersions': {b: config['businesses'][b] for b in selected if b in planned},
		'targets': list(selections), 'indexSelections': selections, 'optionalArtifacts': sorted(set(optional) - required_artifacts),
		'mobileCoverage': {target: mobile_coverage(target) for target in targets if target in MOBILE_TARGETS},
		'notes': ['null denotes a file; ** denotes a resource tree.',
			'Business classification follows the registered source categories; artifact categories are inside each version.',
			'Public any/wasm32 payloads are stored once and referenced by consuming target indexes.',
			'Optional NuGet/symbol distributions must be collected before their shared manifest is sealed.',
			'Wheel tags, target acceptance and final hashes require real artifacts; this preview does not build or certify a release.'],
		'directories': {'Release': root}}


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--versions-file', type=Path)
	parser.add_argument('--version')
	parser.add_argument('--business', choices=ALL_BUSINESSES, action='append')
	parser.add_argument('--target', choices=tuple(RELEASE_TARGETS), action='append')
	args = parser.parse_args()
	repo = args.repo_root.resolve()
	check_product_inventory(repo)
	config = load_versions(repo, args.versions_file, args.version, args.business)
	targets = args.target or list(RELEASE_TARGETS)
	print(json.dumps(release_plan(repo, config, targets, args.business), ensure_ascii=False, indent=2))


if __name__ == '__main__':
	main()
