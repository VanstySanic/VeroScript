# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Installable GUI resources/providers for hosts exposing the foundation services.

The SDK archives remain complete development products. These packages deploy
runtime data and optional providers; they never embed a private Core or replace
the GUI adapter explicitly selected by the embedding host.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

from veroscript_module_packages import shared_files
from veroscript_products import FOUNDATION_PACKAGE_NAMES, verify_module_payload
from veroscript_release import TARGETS, digest, native_identity, scratch_directory, verify_windows_runtime

PROVIDERS = {'Gui': 'VeroScriptGuiToolsModule', 'Sdl3': 'VeroScriptSdl3',
			 'Stb': 'VeroScriptStbModule', 'FontBackend': 'VeroScriptFontBackendModule',
			 'RmlUi': 'VeroScriptRmlUiModule'}
ENTRY_IDS = {'Gui': 'veroscript-gui-tools', 'FontBackend': 'veroscript-font-backend',
			 **{key: 'veroscript-' + key.lower() for key in ('Sdl3', 'Stb', 'RmlUi')}}


def native_path(business, target):
	base = PROVIDERS[business]
	return 'bin/' + base + '.dll' if target.startswith('win-') else 'lib/lib' + base + (
		'.dylib' if target.startswith('macos-') else '.so')


def create_installable_foundation(repo, roots, destination, versions, target, tool):
	from veroscript_tool_runner import output as tool_output, run as tool_run
	context = json.loads(tool_output(tool,'target'))
	if (context['os'], context['arch']) != TARGETS[target]:
		raise ValueError('Foundation package tool target mismatch')
	for business, sdk in roots.items():
		metadata = json.loads((sdk / 'veroscript-module.json').read_text(encoding='utf-8'))
		with tempfile.TemporaryDirectory(prefix='veroscript-foundation-installable-', dir=scratch_directory(repo)) as temporary:
			stage = Path(temporary)
			for path in shared_files(sdk, aliases=True):
				output = stage / path.relative_to(sdk)
				output.parent.mkdir(parents=True, exist_ok=True)
				shutil.copy2(path, output)
			for directory in ('licenses', 'share', 'bin/licenses', 'lib/licenses'):
				if (sdk / directory).is_dir():
					shutil.copytree(sdk / directory, stage / directory)
			for name in ('LICENSE', 'veroscript-module.json'):
				shutil.copy2(sdk / name, stage / name)
			entries = {'libraries': []}
			native = None
			if business in PROVIDERS:
				library = native_path(business, target)
				catalog = stage / 'catalog/bindings.json'
				catalog.parent.mkdir()
				descriptor = json.loads(tool_output(tool,'describe',sdk/library,FOUNDATION_PACKAGE_NAMES[business],digest(sdk/library),catalog))
				if (descriptor['id'] != ENTRY_IDS[business] or descriptor['componentVersion'] != versions[business]
						or descriptor['target'] != context or descriptor['coreBuild'] != context['coreBuild']
						or descriptor['abi'] != context['abis']['native-extension']):
					raise ValueError('Foundation module descriptor mismatch: ' + business)
				native = {'id': descriptor['id'], 'path': library, 'linkage': 'dynamic',
						  'componentVersion': descriptor['componentVersion'], 'roles': descriptor['roles'],
						  'catalog': 'catalog/bindings.json', 'libraries': []}
				entries['native'] = [native]
				entries['backends'] = descriptor['backends']
			for index, path in enumerate(shared_files(sdk)):
				relative = path.relative_to(sdk).as_posix()
				if native and path.resolve() == (sdk / native['path']).resolve():
					continue
				entries['libraries'].append({'id': f'runtime-{index}', 'path': relative, 'linkage': 'dynamic'})
			if native:
				native['libraries'] = [item['id'] for item in entries['libraries']]
			dependencies = {FOUNDATION_PACKAGE_NAMES[key]: {'version': value, 'usage': ['runtime', 'compile']}
							for key, value in metadata['dependencies'].items() if key in FOUNDATION_PACKAGE_NAMES}
			resources = {}
			resource_root = stage / 'share' / ('VeroScript' + business) / 'Resources'
			if resource_root.is_dir():
				for group in sorted(resource_root.iterdir()):
					if group.is_dir():
						resources[group.name.lower()] = group.relative_to(stage).as_posix()
			manifest = {'format': 'veroscript-package', 'formatVersion': '0.1.1',
						'name': FOUNDATION_PACKAGE_NAMES[business], 'version': versions[business],
						'description': 'VeroScript ' + business + ' runtime resources and optional native providers.',
						'licenseFile': 'LICENSE', 'requires': {'core': context['core']},
						'target': {key: context[key] for key in ('os', 'arch', 'environment', 'coreBuild')},
						'entries': entries, 'resources': resources, 'dependencies': dependencies}
			if native:
				manifest['target']['abis'] = {'native-extension': context['abis']['native-extension']}
			(stage / 'veroscript-package.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
			verify_module_payload(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
			archive = destination / 'modules' / f'{FOUNDATION_PACKAGE_NAMES[business]}-{versions[business]}-{target}.vspkg'
			tool_run(tool,'command','pack',stage,'--output='+str(archive))


def verify_installable_foundation(entries, business, version, target):
	verify_module_payload(entries)
	if any(path.startswith(('include/', 'lib/cmake/')) or Path(path).suffix.lower() in {'.a', '.lib', '.pc'} for path in entries):
		raise ValueError('SDK-only content in installable foundation package')
	try:
		manifest = json.loads(entries['veroscript-package.json'][0])
		metadata = json.loads(entries['veroscript-module.json'][0])
		if (manifest.get('format') != 'veroscript-package' or manifest.get('formatVersion') != '0.1.1'
				or manifest.get('name') != FOUNDATION_PACKAGE_NAMES[business] or manifest.get('version') != version
				or (manifest['target'].get('os'), manifest['target'].get('arch')) != TARGETS[target]
				or not manifest['target'].get('environment') or not manifest['target'].get('coreBuild')
				or metadata.get('business') != business or metadata.get('version') != version or metadata.get('target') != target
				or metadata.get('name') != manifest['name'] or not re.fullmatch('[0-9a-f]{64}', metadata.get('payloadId', ''))
				or manifest.get('licenseFile') != 'LICENSE' or not entries['LICENSE'][0]):
			raise ValueError('Installable foundation identity mismatch')
		expected = {FOUNDATION_PACKAGE_NAMES[key]: {'version': value, 'usage': ['runtime', 'compile']}
					for key, value in metadata['dependencies'].items() if key in FOUNDATION_PACKAGE_NAMES}
		if manifest.get('dependencies') != expected:
			raise ValueError('Installable foundation dependencies mismatch')
		libraries = manifest['entries']['libraries']
		if (len({item['id'] for item in libraries}) != len(libraries)
				or any(item.get('linkage') != 'dynamic' or not entries[item['path']][0] for item in libraries)):
			raise ValueError('Invalid installable foundation runtime library')
		if business in PROVIDERS:
			native = manifest['entries']['native']
			if (len(native) != 1 or native[0].get('id') != ENTRY_IDS[business]
					or native[0].get('path') != native_path(business, target)
					or native[0].get('componentVersion') != version or native[0].get('linkage') != 'dynamic'
					or native[0].get('roles') != (['cli'] if business == 'Gui' else ['bindings', 'provider']
						if business in {'Sdl3', 'FontBackend'} else ['provider'])
					or native[0].get('libraries') != [item['id'] for item in libraries]
					or not entries[native[0]['path']][0] or not manifest['target'].get('abis', {}).get('native-extension')
					or json.loads(entries[native[0]['catalog']][0]).get('version') != 1):
				raise ValueError('Installable foundation native entry mismatch')
			backends = manifest['entries']['backends']
			if any(item.get('module') != native[0]['id'] for item in backends) or (business == 'Gui' and backends):
				raise ValueError('Invalid installable foundation provider reference')
			if business != 'Gui' and not backends:
				raise ValueError('Missing installable foundation providers')
		for path, (data, _) in entries.items():
			if data.startswith((b'MZ', b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')):
				if native_identity(data[:4096]) != TARGETS[target]:
					raise ValueError('Installable foundation native target mismatch: ' + path)
		for key in metadata.get('payloadFiles', {}):
			if key.startswith('licenses/') and not entries.get(key, (b'', 0))[0]:
				raise ValueError('Missing installable foundation license: ' + key)
		if target.startswith('win-') and (business in PROVIDERS or libraries):
			verify_windows_runtime(entries, TARGETS[target])
		return metadata
	except (KeyError, TypeError, IndexError) as error:
		raise ValueError('Missing/invalid installable foundation metadata') from error


def verify_installable_foundation_consumers(repo, candidate, builds, selected, target, supplied_tool=None):
	from veroscript_module_packages import package_tool
	from veroscript_business_release import artifact_business
	tool = package_tool(builds, supplied_tool)
	with tempfile.TemporaryDirectory(prefix='veroscript-gui-installed-空间 ', dir=scratch_directory(repo)) as temporary:
		work = Path(temporary)
		project = work / 'project'
		project.mkdir()
		requirements = {'format': 'veroscript-project', 'formatVersion': '0.1.1', 'name': 'foundation-consumer',
						'version': '0.1.1', 'dependencies': {}, 'services': {}}
		for business in selected:
			matches = [path for path in (candidate / 'modules').glob('*.vspkg') if artifact_business(path.name) == business]
			if len(matches) != 1:
				raise ValueError('Missing/ambiguous installable foundation package: ' + business)
			local = work / matches[0].name
			shutil.copy2(matches[0], local)
			with zipfile.ZipFile(local) as reader:
				manifest = json.loads(reader.read('veroscript-package.json'))
			requirements['dependencies'][manifest['name']] = {'version': manifest['version'],
				'archive': '../' + local.name, 'sha256': digest(local)}
			for backend in manifest['entries'].get('backends', []):
				requirements['services'][backend['service']] = {'backend': backend['id']}
		configuration = project / 'veroscript-project.json'
		configuration.write_text(json.dumps(requirements), encoding='utf-8')
		cache = work / 'cache'
		def command(*arguments):
			subprocess.run([str(tool), 'command', *map(str, arguments), '--path=' + str(project),
							'--cache-dir=' + str(cache)], check=True, timeout=120)
		command('install', '--offline')
		command('verify')
		# Recreate a missing environment from its exact lock and explicit local
		# archives without registry access. No process retains the old environment.
		(project / '.veroscript').rename(project / 'saved-environment')
		command('install', '--offline', '--locked')
		source = project / 'run.vs'
		source.write_text('int main(){return 0;}', encoding='utf-8')
		output = subprocess.check_output([str(tool), 'run', str(project), str(source), '--providers'],
										 text=True, encoding='utf-8', timeout=120)
		for service, selection in requirements['services'].items():
			if f'provider {service}/{selection["backend"]}' not in output:
				raise ValueError('Installed provider was not instantiated: ' + service)
		subprocess.run([str(tool), 'run', str(project), str(source), '--analysis'], check=True, timeout=120)
		if 'Gui' in selected:
			cli_name = 'veroscript.exe' if target.startswith('win-') else 'veroscript'
			cli = next((build / directory / cli_name for build in dict.fromkeys(builds.values())
						for directory in ('bin/Release', 'bin') if (build / directory / cli_name).is_file()), None)
			if not cli:
				raise ValueError('Installable GUI verification needs a matching GUI-enabled CLI')
			source.write_text('void main(){VS::Gui::Widget w=VS::Gui::Widget::Create().Gap(6);'
							  'auto read_gap=[w]()->double{return w.GetGap();};'
							  'assert(w.GetGap()==6&&read_gap()==6);'
							  'VS::Gui::ListView list=VS::Gui::ListView::Create().SelectedKey("a")'
							  '[{VS::Gui::ListItem::Create().Key("a").Text("Alpha")}];'
							  'list.Initialize();assert(list.GetActualSelectedKey()=="a");'
							  'VS::Gui::ComboBox combo=VS::Gui::ComboBox::Create().SelectedKey("a")'
							  '[{VS::Gui::ListItem::Create().Key("a").Text("Alpha")}];'
							  'combo.Initialize();assert(combo.GetText()=="Alpha");'
							  'VS::Gui::Dialog dialog=VS::Gui::Dialog::Create();'
							  'dialog.Initialize();dialog.Show();dialog.Close("done");'
							  'assert(dialog.GetModal()&&dialog.GetResult()=="done");'
							  'array<string> labels=array<string>();'
							  'for(int64 index=0;index<1000;index++)labels.push(to_string(index));'
							  'VS::Gui::ListView virtualList=VS::Gui::ListView::Create();'
							  'virtualList.SetVirtualItems(labels,28);'
							  'assert(virtualList.GetItemCount()==1000&&virtualList.GetRealizedItemCount()<32);'
							  'virtualList.Dispose();'
							  'dialog.Dispose();combo.Dispose();list.Dispose();w.Dispose();}', encoding='utf-8')
			common = [str(cli), '--path=' + str(project), '--stdlib-path', str(repo / 'VeroScript/Resources/Scripts/stdlib')]
			for arguments in ([str(source)], ['--check', str(source)]):
				subprocess.run([*common, *arguments], check=True, timeout=600)
			if 'RmlUi' in selected:
				subprocess.run([*common, '--gui-backend=rmlui', str(source)], check=True, timeout=600)
				command('disable', 'vs-rmlui')
				failed = subprocess.run([*common, '--gui-backend=rmlui', str(source)], capture_output=True, timeout=120)
				if failed.returncode == 0:
					raise ValueError('Disabled GUI provider remains selectable')
				command('enable', 'vs-rmlui')
				command('uninstall', 'vs-rmlui')
				failed = subprocess.run([*common, '--gui-backend=rmlui', str(source)], capture_output=True, timeout=120)
				if failed.returncode == 0:
					raise ValueError('Uninstalled GUI provider remains selectable')
		command('verify')
		print('Foundation .vspkg install, offline lock restore, provider creation, GUI use and analysis passed')
