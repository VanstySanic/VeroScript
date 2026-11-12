# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Release ownership, directory routes and distribution policy."""
from pathlib import PurePosixPath

NATIVE_TARGETS = {'win-x64': ('windows', 'x64'), 'win-x86': ('windows', 'x86'),
	'win-arm64': ('windows', 'arm64'), 'linux-x64': ('linux', 'x64'),
	'linux-arm64': ('linux', 'arm64'), 'macos-x64': ('macos', 'x64'), 'macos-arm64': ('macos', 'arm64')}
MOBILE_TARGETS = {'android': ('android', 'universal'), 'ios': ('ios', 'universal')}
RELEASE_TARGETS = {**NATIVE_TARGETS, **MOBILE_TARGETS}
WINDOWS_WHEEL_TAGS = {'win-x64': 'win_amd64', 'win-x86': 'win32', 'win-arm64': 'win_arm64'}

ASMJIT_PACKAGE_NAME = 'vs-asmjit'
BYTECODE_VM_PACKAGE_NAME = 'vs-bytecode-vm'

FEATURE_MODULE_BUSINESSES = ('Json', 'Network', 'Http', 'WebSocket', 'Boost', 'Curl')
MODULE_PACKAGE_NAMES = {'Json': 'vs-json', 'Network': 'vs-network', 'Http': 'vs-http', 'WebSocket': 'vs-websocket', 'Boost': 'vs-boost', 'Curl': 'vs-curl'}
MODULE_ENTRY_IDS = {business: business.lower() for business in FEATURE_MODULE_BUSINESSES}
FOUNDATION_PACKAGE_NAMES = {'Platform': 'vs-platform', 'Graphics': 'vs-graphics',
							'Audio': 'vs-audio', 'Sdl3': 'vs-sdl3',
							'Image': 'vs-image', 'Font': 'vs-font', 'Stb': 'vs-stb',
							'FontBackend': 'vs-font-backend', 'Gui': 'vs-gui', 'RmlUi': 'vs-rmlui'}
FOUNDATION_BUSINESSES = tuple(FOUNDATION_PACKAGE_NAMES)
INSTALLABLE_PACKAGE_NAMES = {'Jit': ASMJIT_PACKAGE_NAME, 'Vm': BYTECODE_VM_PACKAGE_NAME,
							 **MODULE_PACKAGE_NAMES, **FOUNDATION_PACKAGE_NAMES}


def installable_archive(business, version, target):
	category = {'Jit': 'jit', 'Vm': 'vm'}.get(business, 'modules')
	return f'{category}/{INSTALLABLE_PACKAGE_NAMES[business]}-{version}-{target}.vspkg'


FOUNDATION_MODULE_DIRECTORIES = {'Platform': 'Modules/Multimedia/VeroScriptPlatform',
								 'Graphics': 'Modules/Multimedia/VeroScriptGraphics',
								 'Audio': 'Modules/Multimedia/VeroScriptAudio',
								 'Sdl3': 'Modules/Multimedia/Backend/VeroScriptSdl3',
								 'Image': 'Modules/Multimedia/VeroScriptImage',
								 'Font': 'Modules/Multimedia/VeroScriptFont',
								 'Stb': 'Modules/Multimedia/Backend/VeroScriptStb',
								 'FontBackend': 'Modules/Multimedia/Backend/VeroScriptFontBackend',
								 'Gui': 'Modules/Multimedia/VeroScriptGui',
								 'RmlUi': 'Modules/Multimedia/Backend/VeroScriptRmlUi'}
MODULE_SOURCE_DIRECTORIES = {
	'Jit': 'Modules/Execution/VeroScriptAsmJit', 'Vm': 'Modules/Execution/VeroScriptBytecodeVm',
	**{name: 'Modules/Network/VeroScript' + name for name in ('Json', 'Network', 'Http', 'WebSocket')},
	**{name: 'Modules/Network/Backend/VeroScript' + name for name in ('Boost', 'Curl')},
	**FOUNDATION_MODULE_DIRECTORIES,
}
SDK_MODULE_BUSINESSES = (*FEATURE_MODULE_BUSINESSES, *FOUNDATION_BUSINESSES)
MODULE_BUSINESSES = ('Jit', 'Vm', *SDK_MODULE_BUSINESSES)
BUSINESSES = ('Runtime', 'CoreSDK', 'Interop', 'Cpp', 'CSharp', 'Python', 'WebAssembly', 'Resources', *MODULE_BUSINESSES)
IDE_PRODUCTS = {
	'VSCode': {'family': 'vscode', 'source': 'Extensions/Editor/VSCode/VeroScriptVSCode',
			   'artifact': 'VeroScriptVSCode', 'package': 'veroscript', 'id': 'VanstySanic.veroscript'},
	'JetBrains': {'family': 'jetbrains', 'source': 'Extensions/Editor/JetBrains/VeroScriptJetBrains',
				  'artifact': 'VeroScriptJetBrains', 'id': 'com.vanstysanic.veroscript',
				  'namespace': 'com/vanstysanic/veroscript/'},
	'GuiVSCode': {'family': 'vscode', 'source': 'Extensions/Editor/VSCode/VeroScriptGuiVSCode',
				  'artifact': 'VeroScriptGuiVSCode', 'package': 'veroscript-gui',
				  'id': 'VanstySanic.veroscript-gui', 'base': 'VSCode'},
	'GuiJetBrains': {'family': 'jetbrains', 'source': 'Extensions/Editor/JetBrains/VeroScriptGuiJetBrains',
					 'artifact': 'VeroScriptGuiJetBrains', 'id': 'com.vanstysanic.veroscript.gui',
					 'namespace': 'com/vanstysanic/veroscript/gui/jetbrains/', 'base': 'JetBrains'},
}
IDE_BUSINESSES = tuple(IDE_PRODUCTS)
DESKTOP_BUSINESSES = (*BUSINESSES, *IDE_BUSINESSES)
MOBILE_PRODUCTS = {'android': 'Android', 'ios': 'iOS'}
MOBILE_SOURCE_DIRECTORIES = {'Android': 'Extensions/Platform/Mobile/VeroScriptAndroid',
	'iOS': 'Extensions/Platform/Mobile/VeroScriptIOS'}
MOBILE_BUSINESSES = tuple(MOBILE_PRODUCTS.values())
ALL_BUSINESSES = (*DESKTOP_BUSINESSES, *MOBILE_BUSINESSES)
MOBILE_EMBEDDING_BUSINESSES = ('CoreSDK', 'Cpp', 'Interop', 'Vm')
# Mobile modules expose their native CMake SDKs. Desktop dynamic script/package
# formats are not valid substitutes for iOS static registration or Android ABIs.
MOBILE_NATIVE_BUSINESSES = (*MOBILE_EMBEDDING_BUSINESSES, *SDK_MODULE_BUSINESSES)
MOBILE_NATIVE_PREFIXES = {name: 'VeroScript' + name + 'MobileSDK' for name in MOBILE_NATIVE_BUSINESSES}
MOBILE_NATIVE_PREFIXES['CoreSDK'] = 'VeroScriptCoreMobileSDK'
MOBILE_EXCLUSIONS = {
	'Runtime': 'The desktop CLI/launcher is replaced by an application embedding the native platform SDK.',
	'Resources': 'Authored documentation/assets have no mobile executable ABI; use the Resources distribution.',
	'WebAssembly': 'The WebAssembly SDK targets a WebAssembly host, not the Android/iOS native ABI.',
	**{name: 'Editor plugins run in desktop VS Code/JetBrains hosts.' for name in IDE_BUSINESSES},
}
EXTENSION_BUSINESSES = ('Cpp', 'CSharp', 'Python', 'WebAssembly', *IDE_BUSINESSES, *MOBILE_BUSINESSES)
# Product packages carry functionality and legal notices; authored guides and
# examples are published separately through Resources.
BUSINESS_GROUPS = {name: ('Extensions' if name in EXTENSION_BUSINESSES else
						 'Modules' if name in MODULE_BUSINESSES else '') for name in ALL_BUSINESSES}
BUSINESS_ROUTES = {
	**{name: name for name in ('Runtime', 'CoreSDK', 'Interop', 'Resources')},
	**{name: 'Modules/Execution/' + name for name in ('Jit', 'Vm')},
	**{name: 'Modules/Network/' + name for name in ('Json', 'Network', 'Http', 'WebSocket')},
	**{name: 'Modules/Network/Backend/' + name for name in ('Boost', 'Curl')},
	**{name: 'Modules/Multimedia/' + name for name in ('Platform', 'Graphics', 'Audio', 'Image', 'Font', 'Gui')},
	**{name: 'Modules/Multimedia/Backend/' + name for name in ('Sdl3', 'Stb', 'FontBackend', 'RmlUi')},
	**{name: 'Extensions/Language/' + name for name in ('Cpp', 'CSharp', 'Python')},
	**{name: 'Extensions/Editor/VSCode/' + name for name in ('VSCode', 'GuiVSCode')},
	**{name: 'Extensions/Editor/JetBrains/' + name for name in ('JetBrains', 'GuiJetBrains')},
	**{name: 'Extensions/Platform/Mobile/' + name for name in ('Android', 'iOS')},
	'WebAssembly': 'Extensions/Platform/WebAssembly',
}
if set(BUSINESS_ROUTES) != set(ALL_BUSINESSES) or len(set(BUSINESS_ROUTES.values())) != len(ALL_BUSINESSES):
	raise ValueError('Each registered business requires one unique classified release route')
DISTRIBUTION_SCOPES = (*RELEASE_TARGETS, 'any', 'wasm32')
ARTIFACT_CATEGORIES = ('archives', 'sdks', 'packages', 'installers', 'symbols')

# New Modules are archive/package distributions and never installer products.
INSTALLER_BUSINESSES = tuple(name for name in BUSINESSES
							if name != 'Resources' and BUSINESS_GROUPS[name] != 'Modules')


def installer_filename(business: str, version: str, target: str) -> str:
	if business not in INSTALLER_BUSINESSES or target not in NATIVE_TARGETS:
		raise ValueError('No installer for the requested business and target')
	suffix = {'windows': '.exe', 'linux': '.tar.gz', 'macos': '.dmg'}[NATIVE_TARGETS[target][0]]
	return f'VeroScript{business}Installer-{version}-{target}{suffix}'


def target_businesses(target: str, *, optional: bool = False) -> tuple[str, ...]:
	"""Universal mobile SDKs have a different completeness contract to desktop."""
	if target in MOBILE_PRODUCTS:
		return (MOBILE_PRODUCTS[target], *MOBILE_EMBEDDING_BUSINESSES, 'CSharp', 'Python',
			*(SDK_MODULE_BUSINESSES if optional else ()), *(('Jit',) if optional and target == 'android' else ()))
	if target in NATIVE_TARGETS:
		return DESKTOP_BUSINESSES
	raise ValueError(f'Unknown release target: {target}')


def mobile_coverage(target: str) -> dict:
	"""Account for every product, so adding a business cannot silently omit mobile."""
	if target not in MOBILE_PRODUCTS:
		raise ValueError('Expected a mobile release target')
	default, supported = set(target_businesses(target)), set(target_businesses(target, optional=True))
	excluded = {**MOBILE_EXCLUSIONS, MOBILE_PRODUCTS['ios' if target == 'android' else 'android']:
		'Platform adapter belongs to the other mobile operating system.'}
	if target == 'ios':
		excluded['Jit'] = 'Native AsmJit machine-code backend is unavailable on iOS; use VM/static AOT.'
	if set(ALL_BUSINESSES) != supported | set(excluded) or supported & set(excluded):
		raise ValueError('Every release product must have an explicit mobile distribution policy')
	return {name: {'status': 'default' if name in default else 'optional-native-sdk' if name in supported else 'excluded',
		**({'reason': excluded[name]} if name in excluded else {})} for name in ALL_BUSINESSES}


def business_route(business: str) -> str:
	if business not in BUSINESS_GROUPS:
		raise ValueError(f'Unknown release business: {business}')
	return BUSINESS_ROUTES[business]


def check_product_inventory(repo):
	"""Catch new module/editor products that are missing from release coverage."""
	from veroscript_paths import module_root
	root = module_root(repo)
	found = set()
	for family, marker in (('VSCode', 'package.json'), ('JetBrains', 'gradle.properties')):
		found.update(path.parent.relative_to(root).as_posix()
					 for path in (root / 'Extensions/Editor' / family).glob('*/' + marker))
	modules = root / 'Modules'
	if modules.is_dir():
		for group in modules.iterdir():
			if not group.is_dir():
				continue
			for directory in group.iterdir():
				candidates = list(directory.iterdir()) if directory.is_dir() and directory.name == 'Backend' else [directory]
				for candidate in candidates:
					if (candidate / 'Public').is_dir() and (candidate / 'CMakeLists.txt').is_file():
						found.add(candidate.relative_to(root).as_posix())
	registered = set(MODULE_SOURCE_DIRECTORIES.values()) | {info['source'] for info in IDE_PRODUCTS.values()}
	mobile = root / 'Extensions/Platform/Mobile'
	for candidate in mobile.iterdir() if mobile.is_dir() else ():
		if candidate.name != 'VeroScriptMobile' and (candidate / 'CMakeLists.txt').is_file():
			found.add(candidate.relative_to(root).as_posix())
	registered.update(MOBILE_SOURCE_DIRECTORIES.values())
	# The language SDK family is also a product source, even when only its
	# runtime participates in an embedded build (the original Cpp coverage gap).
	languages = root / 'Extensions/Language'
	found.update(path.relative_to(root).as_posix() for path in (languages.iterdir() if languages.is_dir() else ()) if path.is_dir())
	registered.update('Extensions/Language/' + name for name in ('Cpp', 'CSharp', 'Python'))
	if found - registered:
		raise ValueError('Unregistered release product sources: ' + ', '.join(sorted(found - registered)))
	classified_sources = {**MODULE_SOURCE_DIRECTORIES, **MOBILE_SOURCE_DIRECTORIES,
		**{name: product['source'] for name, product in IDE_PRODUCTS.items()}}
	for business, source in classified_sources.items():
		expected = str(PurePosixPath(source).parent / business)
		if business_route(business) != expected:
			raise ValueError('Release classification diverges from its source category: ' + business)
	for target in MOBILE_PRODUCTS:
		mobile_coverage(target)
	return {'businesses': len(ALL_BUSINESSES), 'moduleSources': len(MODULE_SOURCE_DIRECTORIES),
			'editorSources': len(IDE_PRODUCTS)}


def auxiliary_content(name: str) -> bool:
	"""Classify optional authored content, preserving licenses and runtime data."""
	directories = {'doc', 'docs', 'documentation', 'tutorial', 'tutorials', 'example', 'examples',
				   'sample', 'samples', 'test', 'tests', 'benchmark', 'benchmarks', 'history', 'standards'}
	documents = {'readme', 'changelog', 'changes', 'contributing'}
	parts = PurePosixPath(name).parts
	if not parts:
		return False
	filename = parts[-1].casefold()
	return (any(part.casefold() in directories for part in parts[:-1])
			or any(filename == word or filename.startswith(tuple(word + suffix for suffix in ('.', '-', '_')))
				   for word in documents))


def verify_functional_payload(paths) -> None:
	for name in paths:
		if auxiliary_content(name):
			raise ValueError(f'Non-functional content in product package: {name}')


def clean_functional_payload(root) -> None:
	"""Clean a dedicated packaging stage after consumer tests, never source inputs."""
	from pathlib import Path
	root = Path(root).resolve()
	if not root.is_dir() or (root / 'CMakeCache.txt').exists() or (root / '.git').exists():
		raise ValueError(f'Not a packaging stage: {root}')
	paths = list(root.rglob('*'))
	if any(getattr(path, 'is_junction', lambda: False)() or (path.is_symlink() and path.is_dir()) for path in paths):
		raise ValueError('Linked directory in packaging stage')
	if any(path.is_symlink() for path in paths):
		# Native .so links are functional and are retained; never follow them.
		if any((path.is_symlink() or getattr(path, 'is_junction', lambda: False)())
			   and auxiliary_content(path.relative_to(root).as_posix()) for path in paths):
			raise ValueError('Linked optional content in packaging stage')
	for path in paths:
		if path.is_file() and not path.is_symlink() and auxiliary_content(path.relative_to(root).as_posix()):
			if not path.resolve().is_relative_to(root):
				raise ValueError('Packaging cleanup escapes its staging directory')
			path.unlink()
	for path in sorted(paths, key=lambda item: len(item.parts), reverse=True):
		if path.is_dir() and not path.is_symlink() and not any(path.iterdir()):
			path.rmdir()


def verify_module_payload(paths) -> None:
	"""Modules also exclude debug symbols; those have their own distribution."""
	for name in paths:
		parts = PurePosixPath(name).parts
		if (auxiliary_content(name)
				or any(part.casefold() == 'symbols' or part.casefold().endswith('.dsym') for part in parts[:-1])
				or PurePosixPath(parts[-1]).suffix.casefold() in {'.pdb', '.debug', '.dsym'}):
			raise ValueError(f'Non-functional content in module package: {name}')


if __name__ == '__main__':
	import argparse
	from pathlib import Path
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('command', choices=('clean',))
	parser.add_argument('--directory', type=Path, required=True)
	args = parser.parse_args()
	from veroscript_paths import repository_root
	from veroscript_release import safe_output
	clean_functional_payload(safe_output(repository_root(), args.directory, component=True))
