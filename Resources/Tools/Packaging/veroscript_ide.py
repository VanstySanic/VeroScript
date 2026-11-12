# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build and audit IDE distributions using their owned, pinned toolchains."""
from __future__ import annotations
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from veroscript_paths import module_root
from veroscript_release import archive_entries, digest, scratch_directory
from veroscript_products import IDE_PRODUCTS, check_product_inventory

IDE_ARTIFACTS = {name: (info['artifact'], '.vsix' if info['family'] == 'vscode' else '.zip')
				 for name, info in IDE_PRODUCTS.items()}
IDE_MODULES = {name: info['source'] for name, info in IDE_PRODUCTS.items()}


def ide_build_selection(businesses):
	selected = set(businesses)
	if not selected or not selected.issubset(IDE_PRODUCTS):
		raise ValueError('Select registered IDE businesses')
	for business in tuple(selected):
		if 'base' in IDE_PRODUCTS[business]:
			selected.add(IDE_PRODUCTS[business]['base'])
	return [business for business in IDE_PRODUCTS if business in selected]


def verify_ide_dependencies(dependencies, selected, versions, identities, *, allow_historical=False):
	"""Bind GUI acceptance to the exact base artifact used by its tests."""
	from veroscript_release import version
	addons = {name for name in selected if IDE_PRODUCTS.get(name, {}).get('base')}
	if not isinstance(dependencies, dict) or set(dependencies) != addons:
		raise ValueError('GUI IDE releases require their verified base dependencies')
	for addon in addons:
		base = IDE_PRODUCTS[addon]['base']
		pins = dependencies[addon]
		if not isinstance(pins, dict) or set(pins) != {base}:
			raise ValueError('Invalid GUI IDE base dependency: ' + addon)
		pin = pins[base]
		if (not isinstance(pin, dict) or set(pin) != {'version', 'buildId'}
				or not isinstance(pin.get('version'), str) or not isinstance(pin.get('buildId'), str)
				or not re.fullmatch(r'[0-9a-f]{64}', pin['buildId'])):
			raise ValueError('Invalid GUI IDE base dependency identity: ' + addon)
		version(pin['version'])
		if base in selected:
			if allow_historical and pin['version'] != versions[base]:
				continue  # Collection validation has checked the retained pinned base.
			if pin != {'version': versions[base], 'buildId': identities.get(base)}:
				raise ValueError('GUI IDE base dependency version/build mismatch: ' + addon + '/' + base)


def _verify_payload_name(name: str):
	forbidden = {'node_modules', 'private', '.gradle', '.idea', 'out', 'build', '__pycache__'}
	lowered = name.casefold()
	if (forbidden.intersection(Path(lowered).parts)
			or lowered.endswith(('.map', '.pdb', '.exe', '.dll', '.dylib', '.pyd', '.lib', '.a'))
			or re.search(r'\.so(?:\.\d+)*$', lowered)):
		raise ValueError('IDE archive contains build inputs, caches or native payloads')


def _jar_entries(data: bytes):
	"""Apply the outer archive's path/link/CRC gates to nested plugin JARs."""
	import zipfile
	from veroscript_release import safe_member
	entries, seen = {}, set()
	with zipfile.ZipFile(io.BytesIO(data)) as jar:
		for item in jar.infolist():
			name = safe_member(item.filename.rstrip('/'))
			if name in seen:
				raise ValueError('Duplicate nested plugin JAR entry')
			seen.add(name)
			if stat.S_ISLNK(item.external_attr >> 16):
				raise ValueError(f'ZIP link is not supported: {name}')
			_verify_payload_name(name)
			if not item.is_dir():
				entries[name] = jar.read(item)  # Reading to EOF validates the CRC.
	return entries


def check_ide_sources(repo: Path, versions: dict, businesses):
	check_product_inventory(repo)
	for business in ide_build_selection(businesses) if businesses else ():
		product = IDE_PRODUCTS[business]
		source = module_root(repo) / IDE_MODULES[business]
		if product['family'] == 'vscode':
			package = json.loads((source / 'package.json').read_text(encoding='utf-8'))
			lock = json.loads((source / 'package-lock.json').read_text(encoding='utf-8'))
			if any(value != versions[business] for value in
				   (package['version'], lock.get('version'), lock.get('packages', {}).get('', {}).get('version'))):
				raise ValueError('Align VS Code package.json/package-lock.json with ReleaseVersions before packaging')
			if product.get('base') and IDE_PRODUCTS[product['base']]['id'] not in package.get('extensionDependencies', []):
				raise ValueError('GUI VS Code package must declare its base extension dependency')
		else:
			properties = (source / 'gradle.properties').read_text(encoding='utf-8')
			if not re.search(r'^pluginVersion=' + re.escape(versions[business]) + r'\s*$', properties, re.MULTILINE):
				raise ValueError('Align JetBrains gradle.properties with ReleaseVersions before packaging')
			if product.get('base'):
				base_source = module_root(repo) / IDE_MODULES[product['base']]
				base_properties = (base_source / 'gradle.properties').read_text(encoding='utf-8')
				platform = re.findall(r'^platformVersion=(\S+)\s*$', properties, re.MULTILINE)
				if not platform or platform != re.findall(r'^platformVersion=(\S+)\s*$', base_properties, re.MULTILINE):
					raise ValueError('Base and GUI JetBrains plugins require the same Rider SDK')
				descriptor = ET.parse(source / 'Resources/META-INF/plugin.xml').getroot()
				if IDE_PRODUCTS[product['base']]['id'] not in [node.text for node in descriptor.findall('depends')]:
					raise ValueError('GUI JetBrains source must declare its base plugin dependency')


def verify_ide_artifact(path: Path, business: str, release_version: str, repo: Path | None = None, *, functional_only=False):
	prefix, suffix = IDE_ARTIFACTS[business]
	if path.name != f'{prefix}-{release_version}{suffix}':
		raise ValueError('IDE artifact filename/version mismatch')
	entries = archive_entries(path)
	if functional_only or repo:
		from veroscript_products import verify_functional_payload
		# VS Code renders this exact root README as its extension Details page.
		verify_functional_payload(name for name in entries
			if not (IDE_PRODUCTS[business]['family'] == 'vscode' and name == 'extension/readme.md'))
	for name in entries:
		_verify_payload_name(name)
	source = module_root(repo) / IDE_MODULES[business] if repo else None
	license_text = (repo / 'LICENSE').read_bytes() if repo else None

	def require(name):
		if name not in entries or not entries[name][0].strip():
			raise ValueError(f'Missing/empty IDE payload: {name}')
		return entries[name][0]

	def same(left, right):
		return left.replace(b'\r\n', b'\n').strip() == right.replace(b'\r\n', b'\n').strip()

	product = IDE_PRODUCTS[business]
	if product['family'] == 'vscode':
		for name in ('extension.vsixmanifest', '[Content_Types].xml', 'extension/dist/extension.cjs',
					 'extension/readme.md', 'extension/LICENSE.txt', 'extension/THIRD-PARTY-NOTICES.txt'):
			require(name)
		package = json.loads(require('extension/package.json'))
		if (package.get('version') != release_version or package.get('name') != product['package']
				or f"{package.get('publisher')}.{package.get('name')}" != product['id']
				or package.get('main') != './dist/extension.cjs'):
			raise ValueError('VS Code package identity mismatch')
		if product.get('base') and IDE_PRODUCTS[product['base']]['id'] not in package.get('extensionDependencies', []):
			raise ValueError('GUI VS Code package is missing its base extension dependency')
		if package.get('icon'):
			from veroscript_release import safe_member
			icon = require('extension/' + safe_member(package['icon']))
			if source:
				from veroscript_icons import source_icon_files
				official = source_icon_files(repo)['icons/veroscript-icon-256.png']
				if icon != official.read_bytes():
					raise ValueError('Stale VS Code product icon')
		manifest = ET.fromstring(require('extension.vsixmanifest'))
		identities = [node for node in manifest.iter() if node.tag.split('}')[-1] == 'Identity']
		if (len(identities) != 1 or identities[0].get('Version') != release_version
				or identities[0].get('Id') != package['name']
				or identities[0].get('Publisher') != package['publisher'] or identities[0].get('TargetPlatform')):
			raise ValueError('VSIX identity/version/platform mismatch')
		if license_text and not same(require('extension/LICENSE.txt'), license_text):
			raise ValueError('Stale IDE project license')
		if source:
			# Compare the shipped declarative contract and resources with current sources.
			if package != json.loads((source / 'package.json').read_text(encoding='utf-8')):
				raise ValueError('Stale VS Code package contract')
			files = [file for file in [source / 'README.md'] if file.is_file()] + list(source.glob('package.nls*.json'))
			for directory in ('Resources/Assets', 'Resources/Language', 'Resources/Syntaxes', 'Resources/Snippets', 'Resources/Localization', 'Resources/Schemas'):
				files += [file for file in (source / directory).rglob('*') if file.is_file()]
			for file in files:
				relative = file.relative_to(source).as_posix()
				from veroscript_products import auxiliary_content
				if relative != 'README.md' and auxiliary_content(relative):
					continue
				name = 'readme.md' if relative == 'README.md' else relative
				shipped, original = require('extension/' + name), file.read_bytes()
				equal = shipped == original if file.suffix.casefold() in {'.png', '.ico', '.icns'} else same(shipped, original)
				if not equal:
					raise ValueError(f'Stale VS Code resource: {file}')
	else:
		plugin = product['artifact']
		main_jar = f'{plugin}/lib/{plugin}-{release_version}.jar'
		search_jar = f'{plugin}/lib/{plugin}-{release_version}-searchableOptions.jar'
		if main_jar not in entries or not set(entries).issubset({main_jar, search_jar}):
			raise ValueError('Unexpected JetBrains plugin distribution layout')
		if search_jar in entries:
			search = _jar_entries(require(search_jar))
			if any(name != 'META-INF/MANIFEST.MF' and not name.endswith(('.xml', '.json')) for name in search):
				raise ValueError('Unexpected searchable options JAR entry')
		jar = _jar_entries(require(main_jar))
		if functional_only or repo:
			from veroscript_products import verify_functional_payload
			verify_functional_payload(jar)
		if not jar.get('META-INF/plugin.xml', b'').strip():
			raise ValueError('Missing/empty IDE payload: META-INF/plugin.xml')
		document = ET.fromstring(jar['META-INF/plugin.xml'])
		if document.findtext('id') != product['id'] or document.findtext('version') != release_version:
			raise ValueError('JetBrains plugin identity/version mismatch')
		if product.get('base') and IDE_PRODUCTS[product['base']]['id'] not in [n.text for n in document.findall('depends')]:
			raise ValueError('GUI JetBrains package is missing its base plugin dependency')
		idea = document.find('idea-version')
		if idea is None or idea.get('since-build') != '262.10315.191':
			raise ValueError('JetBrains minimum Rider build mismatch')
		if not any(name.endswith('.class') and name.startswith(product['namespace']) for name in jar):
			raise ValueError('Missing compiled JetBrains plugin')
		if not jar.get('META-INF/LICENSE', b'').strip():
			raise ValueError('Missing/empty IDE payload: META-INF/LICENSE')
		if license_text and not same(jar['META-INF/LICENSE'], license_text):
			raise ValueError('Stale IDE project license')
		if source:
			if (source / 'Resources/Catalog').is_dir():
				for file in (source / 'Resources/Catalog').rglob('*'):
					if file.is_file() and jar.get(file.relative_to(source / 'Resources').as_posix()) != file.read_bytes():
						raise ValueError(f'Stale JetBrains catalog: {file}')
			for file in (source / 'Resources/Localization').rglob('*'):
				if file.is_file() and not same(jar.get(file.relative_to(source / 'Resources').as_posix(), b''), file.read_bytes()):
					raise ValueError(f'Stale JetBrains resource: {file}')
			for file in (source / 'Resources/Assets/Icons').rglob('*'):
				if file.is_file():
					name = ('META-INF/' + file.name if file.name.startswith('pluginIcon') else
							'Icons/' + file.relative_to(source / 'Resources/Assets/Icons').as_posix())
					if jar.get(name) != file.read_bytes():
						raise ValueError('Stale JetBrains icon: ' + name)
			original = ET.parse(source / 'Resources/META-INF/plugin.xml').getroot()
			for tag in ('id', 'name', 'vendor', 'description', 'resource-bundle', 'extensions', 'actions', 'depends'):
				# Patching changes whitespace, so compare element content recursively.
				def tree(node):
					return (node.tag, node.attrib, (node.text or '').strip(), [tree(child) for child in node])
				if [tree(n) for n in original.findall(tag)] != [tree(n) for n in document.findall(tag)]:
					raise ValueError(f'Stale JetBrains plugin contract: {tag}')


def _jetbrains_verifier_tools(repo, env):
	ide = Path(env.get('VEROSCRIPT_JETBRAINS_IDE_PATH', '')).resolve()
	if not env.get('VEROSCRIPT_JETBRAINS_IDE_PATH') or not ide.is_dir():
		raise ValueError('GUI JetBrains verification requires VEROSCRIPT_JETBRAINS_IDE_PATH')
	if (ide / 'Contents/product-info.json').is_file():
		ide /= 'Contents'
	if not (ide / 'product-info.json').is_file():
		raise ValueError('The selected Rider SDK is missing product-info.json')
	info = json.loads((ide / 'product-info.json').read_text(encoding='utf-8'))
	properties = (module_root(repo) / IDE_MODULES['GuiJetBrains'] / 'gradle.properties').read_text(encoding='utf-8')
	expected = re.search(r'^platformVersion=(\S+)\s*$', properties, re.MULTILINE)
	if not expected or info.get('productCode') != 'RD' or info.get('version') != expected[1]:
		raise ValueError('Joint plugin verification requires the configured matching Rider SDK')
	jar = env.get('VEROSCRIPT_JETBRAINS_VERIFIER_JAR')
	if not jar:
		home = Path(env.get('GRADLE_USER_HOME', Path.home() / '.gradle'))
		candidates = list((home / 'caches/modules-2/files-2.1/org.jetbrains.intellij.plugins/verifier-cli').glob('*/*/*-all.jar'))
		if candidates:
			jar = max(candidates, key=lambda path: path.stat().st_mtime_ns)
	if not jar or not Path(jar).is_file():
		raise ValueError('Set VEROSCRIPT_JETBRAINS_VERIFIER_JAR to the official verifier CLI, or use its populated GRADLE_USER_HOME')
	jar = Path(jar).resolve()
	java = _java_executable(env)
	if not java:
		raise ValueError('Java is required for joint JetBrains verification')
	return ide, jar, java


def _java_executable(env):
	if env.get('JAVA_HOME'):
		java = Path(env['JAVA_HOME']) / 'bin' / ('java.exe' if os.name == 'nt' else 'java')
		if not java.is_file():
			raise ValueError('JAVA_HOME does not contain a Java executable')
		return str(java)
	return shutil.which('java')


def check_ide_prerequisites(repo, businesses, env=None):
	"""Fail before a native build when required IDE tools are unavailable."""
	selected = [name for name in businesses if name in IDE_PRODUCTS]
	if not selected:
		return
	selected = ide_build_selection(selected)
	env = os.environ if env is None else env
	if any(IDE_PRODUCTS[name]['family'] == 'vscode' for name in selected):
		node = shutil.which('node')
		if not node or not ((Path(node).parent / 'node_modules/npm/bin/npm-cli.js').is_file() or shutil.which('npm')):
			raise ValueError('Node.js and npm are required for IDE packaging')
	if any(IDE_PRODUCTS[name]['family'] == 'jetbrains' for name in selected) and not _java_executable(env):
		raise ValueError('Java is required for JetBrains packaging')
	if 'GuiJetBrains' in selected:
		_jetbrains_verifier_tools(repo, env)


def verify_jetbrains_pair(repo, artifacts, versions, env):
	"""Verify local base/add-on ZIPs together, including their dependency resolution."""
	ide, jar, java = _jetbrains_verifier_tools(repo, env)
	work = Path(tempfile.mkdtemp(prefix='veroscript-ide-verification-', dir=scratch_directory(repo)))
	plugins = work / 'plugins.txt'
	paths = [str(artifacts[business].resolve()) for business in ('JetBrains', 'GuiJetBrains')]
	if any('\n' in path or '\r' in path for path in paths):
		raise ValueError('Plugin Verifier paths cannot contain newlines')
	plugins.write_text(''.join('path:' + path + '\n' for path in paths), encoding='utf-8')
	reports = work / 'reports'
	command = [java, '-jar', str(jar), 'check-plugin', '@' + str(plugins), str(ide),
			   '-verification-reports-dir', str(reports), '-verification-reports-formats', 'plain']
	result = subprocess.run(command, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
	(work / 'verifier.log').write_text(result.stdout + result.stderr, encoding='utf-8')
	if result.returncode:
		raise ValueError('Joint JetBrains verification failed; reports: ' + str(work))
	for business in ('JetBrains', 'GuiJetBrains'):
		matches = [path for path in reports.rglob('verification-verdict.txt')
				   if path.parent.name == versions[business] and path.parent.parent.name == IDE_PRODUCTS[business]['id']]
		if len(matches) != 1 or not re.match(r'^Compatible(?:\.|\s|$)', matches[0].read_text(encoding='utf-8').strip()):
			raise ValueError('Plugin Verifier did not confirm compatibility for ' + business + '; reports: ' + str(work))
		for name in ('compatibility-problems.txt', 'invalid-plugin.txt', 'missing-dependencies.txt'):
			path = matches[0].with_name(name)
			if path.is_file() and path.read_text(encoding='utf-8').strip():
				raise ValueError('Plugin Verifier reported ' + name + '; reports: ' + str(work))
		if business == 'GuiJetBrains':
			dependency_file = matches[0].with_name('dependencies.txt')
			if not dependency_file.is_file():
				raise ValueError('Plugin Verifier did not report GUI dependencies; reports: ' + str(work))
			dependencies = dependency_file.read_text(encoding='utf-8')
			if not re.search(re.escape(IDE_PRODUCTS['JetBrains']['id'] + ':' + versions['JetBrains']) + r'(?:\s|$)', dependencies):
				raise ValueError('GUI plugin was not verified with the selected base plugin; reports: ' + str(work))
	print('Joint JetBrains verification reports:', work, flush=True)


def stage_ide(repo: Path, output: Path, versions: dict, businesses, runtime: Path, jit_backend: Path | None = None,
			  *, dependency_pins: dict | None = None):
	selected = [business for business in IDE_ARTIFACTS if business in businesses]
	if not selected:
		return {}
	check_ide_sources(repo, versions, selected)
	build_selection = ide_build_selection(selected)
	runtime = runtime.resolve()
	executable = runtime / ('veroscript.exe' if os.name == 'nt' else 'veroscript')
	if not executable.is_file():
		raise ValueError('IDE acceptance requires an extracted Runtime directory')
	env = os.environ.copy()
	env['VEROSCRIPT_TEST_RUNTIME'] = str(runtime)
	if jit_backend:
		env['VEROSCRIPT_TEST_JIT_BACKEND'] = str(jit_backend.resolve())
	if 'GuiJetBrains' in selected and not env.get('VEROSCRIPT_JETBRAINS_IDE_PATH'):
		raise ValueError('Set VEROSCRIPT_JETBRAINS_IDE_PATH for local base/GUI plugin verification')
	with tempfile.TemporaryDirectory(prefix='veroscript-ide-artifacts-', dir=scratch_directory(repo)) as temporary:
		identities = {}
		artifacts = {}
		# Base products are prepared first, including when only their add-on is published.
		for business in build_selection:
			source = module_root(repo) / IDE_MODULES[business]
			def run(*args):
				print('+', ' '.join(map(str, args)), flush=True)
				subprocess.run(list(map(str, args)), cwd=source, env=env, check=True)
			if IDE_PRODUCTS[business]['family'] == 'vscode':
				package = json.loads((source / 'package.json').read_text(encoding='utf-8'))
				# Resolve npm through Node on Windows, avoiding cmd shell quoting and npm.ps1 policy.
				node = shutil.which('node')
				if not node:
					raise ValueError('Node.js and npm are required for IDE packaging')
				npm = Path(node).parent / 'node_modules/npm/bin/npm-cli.js'
				npm_command = [node, npm] if npm.is_file() else [shutil.which('npm') or 'npm']
				run(*npm_command, 'ci', '--no-audit', '--no-fund', '--engine-strict')
				run(*npm_command, 'run', 'test:unit')
				run(*npm_command, 'run', 'package')
				if business == 'GuiVSCode':
					env['VEROSCRIPT_GUI_TEST_NATIVE'] = '1'
				run(node, 'Resources/Tools/test.mjs', '--packaged', '--skip-build')
				if business == 'VSCode':
					run(node, 'Resources/Tools/test.mjs', '--packaged', '--folderless', '--skip-build')
				artifact = source / 'out' / f"{package['name']}-{package['version']}.vsix"
			else:
				wrapper = source / ('gradlew.bat' if os.name == 'nt' else 'gradlew')
				command = [wrapper] if os.name == 'nt' else ['bash', wrapper]
				local_ide = env.get('VEROSCRIPT_JETBRAINS_IDE_PATH')
				if local_ide:
					command.append('-PlocalIdePath=' + local_ide)
				# The GUI Gradle project includes the base as a subproject. Select root
				# tasks explicitly; the base has already completed its own acceptance.
				tasks = [':test', ':verifyPluginProjectConfiguration', ':verifyPluginStructure', ':buildPlugin']
				if 'GuiJetBrains' not in build_selection:
					tasks.append(':verifyPlugin')
				run(*command, '--no-daemon', *tasks)
				artifact = source / 'build/distributions' / f'{IDE_PRODUCTS[business]["artifact"]}-{versions[business]}.zip'
			# The GUI's included base project shares build outputs with the standalone
			# project. Retain verified bytes before another product can replace them.
			prefix, suffix = IDE_ARTIFACTS[business]
			snapshot = Path(temporary) / business / f'{prefix}-{versions[business]}{suffix}'
			snapshot.parent.mkdir()
			shutil.copy2(artifact, snapshot)
			verify_ide_artifact(snapshot, business, versions[business], repo)
			artifacts[business] = snapshot
		if 'GuiJetBrains' in selected:
			verify_jetbrains_pair(repo, artifacts, versions, env)
		if dependency_pins is not None:
			for business in selected:
				base = IDE_PRODUCTS[business].get('base')
				if base:
					dependency_pins[business] = {base: {'version': versions[base], 'buildId': digest(artifacts[base])}}
		for business in selected:
			artifact = artifacts[business]
			prefix, suffix = IDE_ARTIFACTS[business]
			destination = output / 'ide' / f'{prefix}-{versions[business]}{suffix}'
			destination.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(artifact, destination)
			verify_ide_artifact(destination, business, versions[business], repo)
			identities[business] = digest(destination)
		return identities
