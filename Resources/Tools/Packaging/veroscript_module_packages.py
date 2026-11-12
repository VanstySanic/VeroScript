# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Produce and consume installable official feature modules using the real client."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import tempfile

from veroscript_products import MODULE_PACKAGE_NAMES, MODULE_ENTRY_IDS, verify_module_payload
from veroscript_modules import dependency_names, verify_backend_identity, required_notices
from veroscript_release import TARGETS, digest, native_identity, scratch_directory, verify_windows_runtime


def package_tool(builds, supplied=None):
	if supplied:
		if hasattr(supplied,'packaging_output') and hasattr(supplied,'packaging_run'):
			return supplied
		path = Path(supplied).resolve()
		if not path.is_file():
			raise ValueError('Missing module package tool: ' + str(path))
		return path
	for build in dict.fromkeys(builds.values()):
		for directory in (build / 'bin/Release', build / 'bin'):
			for name in ('VeroScriptModulePackageTool.exe', 'VeroScriptModulePackageTool'):
				if (directory / name).is_file():
					return directory / name
	raise ValueError('Build VeroScriptModulePackageTool with VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON, '
					 'or supply --package-tool from the matching Core build')


def native_library(root, business, target):
	system = TARGETS[target][0]
	filename = ('' if system == 'windows' else 'lib') + 'VeroScript' + business + 'Module' + (
		'.dll' if system == 'windows' else '.dylib' if system == 'macos' else '.so')
	return root / 'lib/veroscript/modules' / filename


def shared_files(root, *, aliases=False):
	result = sorted(p for p in root.rglob('*') if p.is_file()
					and (p.suffix.lower() in ('.dll', '.dylib') or re.search(r'\.so(?:\.[0-9]+)*$', p.name)))
	# Materialize internal SONAME aliases as ordinary files in the archive.
	# Omitting libSDL3.so.0 while copying its versioned target breaks dlopen.
	if any(not p.resolve().is_relative_to(root.resolve()) for p in result):
		raise ValueError('Runtime library link escapes its package root')
	return result if aliases else [p for p in result if not p.is_symlink()]


def create_installable_modules(repo, roots, destination, versions, target, tool):
	"""SDK stages are retained together so protocol descriptors can load their Network dependency."""
	for business, sdk in roots.items():
		name = MODULE_PACKAGE_NAMES[business]
		with tempfile.TemporaryDirectory(prefix='veroscript-installable-module-', dir=scratch_directory(repo)) as temporary:
			stage = Path(temporary)
			library = native_library(sdk, business, target)
			for path in shared_files(sdk, aliases=True):
				output = stage / path.relative_to(sdk)
				output.parent.mkdir(parents=True, exist_ok=True)
				shutil.copy2(path, output)
			for directory in ('licenses', 'share', 'lib/veroscript/modules/licenses', 'lib/licenses', 'bin/licenses'):
				if (sdk / directory).is_dir():
					shutil.copytree(sdk / directory, stage / directory)
			shutil.copy2(sdk / 'LICENSE', stage / 'LICENSE')
			shutil.copy2(sdk / 'veroscript-module.json', stage / 'veroscript-module.json')
			catalog = stage / 'catalog/bindings.json'
			catalog.parent.mkdir()
			command = [str(tool), 'describe', str(library), name, digest(library), str(catalog)]
			if business in ('Http', 'WebSocket'):
				command.append(str(native_library(roots['Network'], 'Network', target)))
			from veroscript_tool_runner import output as tool_output, run as tool_run
			descriptor = json.loads(tool_output(tool,*command[1:]))
			context = descriptor['target']
			if ((context['os'], context['arch']) != TARGETS[target]
					or descriptor['id'] != MODULE_ENTRY_IDS[business]
					or descriptor['componentVersion'] != versions[business]
					or descriptor['abi'] != context['abis']['native-extension']
					or descriptor['coreBuild'] != context['coreBuild']):
				raise ValueError('Module descriptor and package tool do not match: ' + business)
			owned = []
			for index, original in enumerate(shared_files(sdk)):
				path = stage / original.relative_to(sdk)
				if original.resolve() == library.resolve():
					continue
				identity = 'boost-runtime' if 'VeroScriptBoostRuntime' in path.name else f'dependency-{index}'
				owned.append({'id': identity, 'path': path.relative_to(stage).as_posix(), 'linkage': 'dynamic'})
			references = [entry['id'] for entry in owned]
			native = {'id': descriptor['id'], 'path': library.relative_to(sdk).as_posix(), 'linkage': 'dynamic',
					  'roles': descriptor['roles'], 'catalog': 'catalog/bindings.json',
					  'componentVersion': descriptor['componentVersion'], 'libraries': references}
			manifest = {'format': 'veroscript-package', 'formatVersion': '0.1.1',
						'name': name, 'version': versions[business], 'license': 'MIT', 'licenseFile': 'LICENSE',
						'requires': {'core': context['core']},
						'target': {key: context[key] for key in ('os', 'arch', 'environment', 'coreBuild')},
						'entries': {'native': [native], 'libraries': owned},
						'resources': ({'localization': f'share/VeroScript{business}/Resources/Localization'}
									  if business not in {'Boost', 'Curl'} else {})}
			manifest['target']['abis'] = {'native-extension': context['abis']['native-extension']}
			metadata = json.loads((sdk / 'veroscript-module.json').read_text(encoding='utf-8'))
			manifest['dependencies'] = {MODULE_PACKAGE_NAMES[key]: {'version': value, 'usage': ['runtime', 'compile']}
										for key, value in metadata['dependencies'].items()}
			if business in {'Boost', 'Curl'}:
				backends = descriptor.get('backends', [])
				if {item['service'] for item in backends} != set(metadata.get('backendServices', [])):
					raise ValueError('Backend descriptor/service inventory mismatch')
				manifest['entries']['backends'] = backends
			(stage / 'veroscript-package.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
			verify_module_payload(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
			archive = destination / 'modules' / f'{name}-{versions[business]}-{target}.vspkg'
			tool_run(tool,'command','pack',stage,'--output='+str(archive))


def verify_installable_entries(entries, business, package_version, target):
	"""Release contract, followed by actual package client installation/analysis/execution."""
	verify_module_payload(entries)
	if any(path.startswith(('include/', 'lib/cmake/')) or Path(path).suffix.lower() in ('.a', '.lib') for path in entries):
		raise ValueError('SDK-only content in installable module')
	try:
		manifest = json.loads(entries['veroscript-package.json'][0])
		metadata = json.loads(entries['veroscript-module.json'][0])
		native = manifest['entries']['native'][0]
		catalog = json.loads(entries[native['catalog']][0])
	except (KeyError, ValueError, IndexError, TypeError) as error:
		raise ValueError('Missing/invalid installable module metadata/catalog') from error
	if (manifest.get('format') != 'veroscript-package' or manifest.get('formatVersion') != '0.1.1'
			or manifest.get('name') != MODULE_PACKAGE_NAMES[business] or manifest.get('version') != package_version
			or (manifest['target'].get('os'), manifest['target'].get('arch')) != TARGETS[target]
			or not manifest['target'].get('coreBuild') or not manifest['target'].get('environment')
			or not manifest['target'].get('abis', {}).get('native-extension')
			or native.get('id') != MODULE_ENTRY_IDS[business] or native.get('componentVersion') != package_version
			or native.get('linkage') != 'dynamic' or not native.get('roles')
			or len(manifest['entries']['native']) != 1 or catalog.get('version') != 1
			or metadata.get('schemaVersion') != 1 or metadata.get('profile') != 'native-script-sdk'
			or not re.fullmatch('[0-9a-f]{64}', metadata.get('payloadId', ''))
			or metadata.get('business') != business or metadata.get('version') != package_version
			or metadata.get('target') != target or metadata.get('name') != manifest['name']):
		raise ValueError('Installable module identity mismatch')
	if (native.get('roles') != (['bindings', 'compiler'] if business == 'Json' else ['provider'] if business in {'Boost', 'Curl'} else ['bindings'])
			or not native['path'] in entries or manifest.get('licenseFile') != 'LICENSE' or 'LICENSE' not in entries):
		raise ValueError('Installable module entry/role/license mismatch')
	verify_backend_identity(metadata, business)
	if any(path not in entries or not entries[path][0] for path in required_notices(business, metadata)):
		raise ValueError('Missing installable module third-party notices')
	dependencies = manifest.get('dependencies', {})
	expected = {MODULE_PACKAGE_NAMES[name]: value for name, value in metadata.get('dependencies', {}).items()}
	if set(metadata.get('dependencies', {})) != dependency_names(business, metadata.get('backendServices', [])):
		raise ValueError('Installable module dependency contract mismatch')
	if set(dependencies) != set(expected):
		raise ValueError('Installable module dependencies mismatch')
	for name, release_version in expected.items():
		if (dependencies[name].get('version') != release_version or
				set(dependencies[name].get('usage', [])) != {'runtime', 'compile'}):
			raise ValueError('Module dependency version/usage mismatch')
	if business in {'Boost', 'Curl'}:
		backends = manifest['entries'].get('backends', [])
		if ({item.get('service') for item in backends} != set(metadata.get('backendServices', [])) or
				len(backends) != len(metadata.get('backendServices', [])) or
				any(item.get('id') != business.lower() or item.get('module') != native['id'] for item in backends)):
			raise ValueError('Installable backend service inventory mismatch')
	elif manifest['entries'].get('backends'):
		raise ValueError('Frontend package cannot register backend services')
	for path, (data, _) in entries.items():
		if data.startswith((b'MZ', b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')):
			if native_identity(data[:4096]) != TARGETS[target]:
				raise ValueError('Installable module native target mismatch: ' + path)
	if target.startswith('win-'):
		verify_windows_runtime(entries, TARGETS[target])
	owned = manifest['entries'].get('libraries', [])
	for library in owned:
		if library.get('linkage') != 'dynamic' or library.get('path') not in entries:
			raise ValueError('Missing installable module library')
	own_ids = {library['id'] for library in owned}
	if len(own_ids) != len(owned) or any(reference not in own_ids for reference in native.get('libraries', [])):
		raise ValueError('Invalid installable module library reference')
	if business == 'Boost':
		if ('boost-runtime' not in native.get('libraries', []) or
				not any(library['id'] == 'boost-runtime' and 'VeroScriptBoostRuntime' in library['path'] for library in owned)):
			raise ValueError('Missing shared Boost runtime entry')
	elif any('VeroScriptBoostRuntime' in key for key in entries):
		raise ValueError('Only Boost may distribute its runtime')
	if any('VeroScriptNetworkRuntime' in key for key in entries):
		raise ValueError('Obsolete Network runtime payload')
	return metadata


def verify_installable_consumers(repo, candidate, builds, selected, target, supplied_tool=None):
	from veroscript_products import FOUNDATION_BUSINESSES
	foundation = [name for name in selected if name in FOUNDATION_BUSINESSES]
	if foundation:
		from veroscript_foundation_packages import verify_installable_foundation_consumers
		verify_installable_foundation_consumers(repo, candidate, builds, foundation, target, supplied_tool)
		selected = [name for name in selected if name not in FOUNDATION_BUSINESSES]
		if not selected:
			return
	tool = package_tool(builds, supplied_tool)
	with tempfile.TemporaryDirectory(prefix='veroscript-installed-packages-模块 ', dir=scratch_directory(repo)) as temporary:
		work = Path(temporary)
		archives = {}
		for business in selected:
			matches = list((candidate / 'modules').glob(MODULE_PACKAGE_NAMES[business] + '-*.vspkg'))
			if len(matches) != 1:
				raise ValueError('Missing/ambiguous installable module: ' + business)
			local = work / 'archives' / matches[0].name
			local.parent.mkdir(exist_ok=True)
			shutil.copy2(matches[0], local)
			archives[business] = local
		# Every root declares its local source, including the shared dependency.
		# Merely putting neighbouring ZIPs on disk is not an implicit registry.
		requirements = {'format': 'veroscript-project', 'formatVersion': '0.1.1',
						'name': 'installed-modules-consumer', 'version': '0.1.1', 'dependencies': {}}
		import zipfile
		for business, path in archives.items():
			with zipfile.ZipFile(path) as reader:
				manifest = json.loads(reader.read('veroscript-package.json'))
			requirements['dependencies'][manifest['name']] = {'version': manifest['version'],
				'archive': path.relative_to(work).as_posix(), 'sha256': digest(path)}
		(work / 'veroscript-project.json').write_text(json.dumps(requirements), encoding='utf-8')
		def command(*arguments):
			subprocess.run([str(tool), 'command', *map(str, arguments), '--path=' + str(work)], check=True)
		command('install', '--offline', '--cache-dir=' + str(work / '.test-cache'))
		command('verify')
		command('install', '--offline', '--locked', '--cache-dir=' + str(work / '.test-cache'))
		runtime = ['struct PackageRecord { public: int count=7; };', 'int main(){'] if 'Json' in selected else ['int main(){']
		analysis = ['int main(){']
		if 'Json' in selected:
			runtime += ['assert(json::parse("7").as_int64()==7);',
						'PackageRecord record=PackageRecord();json::value encoded=json::converter<PackageRecord>::write(ref record);',
						'PackageRecord copy=json::converter<PackageRecord>::read(encoded);assert(copy.count==7);']
			analysis += ['json::value j=json::from_int(7);']
		if 'Network' in selected and 'Boost' in selected:
			runtime += ['Network::context c=Network::create_context(false);Network::operation n=Network::resolve(c,"localhost",80,5000);',
						'n.cancel();n.wait(10000);assert(n.done());',
						'Network::context explicit_context=Network::create_context_with_backend("boost",true);explicit_context.shutdown();']
			analysis += ['Network::context c=Network::create_context(true);']
		if 'Http' in selected and 'Curl' in selected and 'Boost' in selected:
			runtime += ['http::client client=http::create_client(c);http::request request=http::create_request("GET","http://127.0.0.1/");']
			runtime += ['http::client_options selected_client=http::make_client_options();selected_client.set_backend("curl");',
						'http::client explicit_client=http::create_client_with_options(c,selected_client);']
			analysis += ['http::client client=http::create_client(c);']
		if 'WebSocket' in selected:
			runtime += ['websocket::options options=websocket::make_options();']
			analysis += ['websocket::options options=websocket::make_options();']
		if 'Network' in selected and 'Boost' in selected:
			runtime += ['c.shutdown();']
		for name, source, suffix in (('run.vs', runtime, []), ('analysis.vs', analysis, ['--analysis'])):
			path = work / name
			path.write_text('\n'.join(source + ['return 0;}']), encoding='utf-8')
			subprocess.run([str(tool), 'run', str(work), str(path), *suffix], check=True)
		defaults = {}
		for path in archives.values():
			with zipfile.ZipFile(path) as reader:
				manifest = json.loads(reader.read('veroscript-package.json'))
			for backend in manifest['entries'].get('backends', []):
				defaults[backend['service']] = {'backend': backend['id']}
		if defaults:
			requirements['services'] = defaults
			(work / 'veroscript-project.json').write_text(json.dumps(requirements), encoding='utf-8')
			command('install', '--offline', '--cache-dir=' + str(work / '.test-cache'))
			subprocess.run([str(tool), 'run', str(work), str(work / 'run.vs')], check=True)
		# Validate the installed state after both native loading and catalog-only analysis.
		command('verify')
		if 'Json' in selected:
			command('disable', 'vs-json')
			command('enable', 'vs-json')
			command('uninstall', 'vs-json')
			# Restore the explicit local source after uninstall removes the root.
			(work / 'veroscript-project.json').write_text(json.dumps(requirements), encoding='utf-8')
			command('install', '--offline', '--cache-dir=' + str(work / '.test-cache'))
			subprocess.run([str(tool), 'run', str(work), str(work / 'run.vs')], check=True)
		if set(selected).intersection(('Http', 'WebSocket')):
			result = subprocess.run([str(tool), 'command', 'disable', 'vs-network', '--path=' + str(work)],
									capture_output=True, text=True, encoding='utf-8')
			if result.returncode == 0 or 'disabled-dependency' not in result.stderr:
				raise ValueError('Disabling the shared Network dependency must fail explicitly')
			# A protocol archive alone cannot invent an offline source for Network.
			missing = work / 'missing dependency'
			missing.mkdir()
			protocol = 'Http' if 'Http' in selected else 'WebSocket'
			dependency = dict(requirements['dependencies'][MODULE_PACKAGE_NAMES[protocol]])
			dependency['archive'] = '../' + dependency['archive']
			subset = {**requirements, 'dependencies': {MODULE_PACKAGE_NAMES[protocol]: dependency}}
			(missing / 'veroscript-project.json').write_text(json.dumps(subset), encoding='utf-8')
			result = subprocess.run([str(tool), 'command', 'install', '--offline', '--path=' + str(missing),
									 '--cache-dir=' + str(missing / 'cache')], capture_output=True, text=True, encoding='utf-8')
			if result.returncode == 0:
				raise ValueError('Missing offline Network source must prevent installation')
		command('verify')
		print('Official module packages: installation, locked offline reuse, execution and analysis passed')
