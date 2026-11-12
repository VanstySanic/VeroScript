# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build and run consumers of independently unpacked foundation SDKs."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import tarfile
import zipfile

from veroscript_modules import build_cache
from veroscript_release import scratch_directory
from veroscript_business_release import artifact_business
from veroscript_products import FOUNDATION_MODULE_DIRECTORIES
from veroscript_foundation import foundation_dependencies


def verify_foundation_consumers(repo, candidate, builds, selected, target, core_sdk=None, *, bindings=True, jobs=2, cpp_sdk=None):
	with tempfile.TemporaryDirectory(prefix='veroscript-foundation-独立 SDK-', dir=scratch_directory(repo)) as temporary:
		work = Path(temporary)
		prefixes, package_prefixes = [], {}
		for archive in sorted((candidate / 'modules').iterdir()):
			if archive.suffix == '.vspkg':
				continue
			if artifact_business(archive.name) not in selected:
				continue
			if archive.suffix == '.zip':
				with zipfile.ZipFile(archive) as reader:
					reader.extractall(work)
			else:
				with tarfile.open(archive) as reader:
					reader.extractall(work, filter='data')
			prefix = work / archive.name.removesuffix('.tar.gz').removesuffix('.zip')
			prefixes.append(prefix)
			package_prefixes[artifact_business(archive.name)] = prefix
		if bindings and any(name != 'Sdl3' for name in selected):
			if core_sdk is None:
				raise ValueError('Foundation script SDK consumption requires --core-sdk')
			prefixes.append(Path(core_sdk).resolve())
		if bindings and 'Gui' in selected:
			if cpp_sdk is None:
				raise ValueError('Gui script SDK consumption requires --cpp-sdk')
			prefixes.append(Path(cpp_sdk).resolve())
		settings = build_cache(builds[selected[0]])
		cmake = settings.get('CMAKE_COMMAND', 'cmake')
		ctest = str(Path(cmake).with_name('ctest.exe' if target.startswith('win-') else 'ctest'))
		def configure_command(label, modules, search, script, *, framework_only=False):
			command = [cmake, '-S', str(repo / 'Resources/Tools/Packaging/Consumer/Foundation'), '-B', str(work / label),
					   '-G', settings['CMAKE_GENERATOR'], '-DCMAKE_BUILD_TYPE=Release',
					   '-DCMAKE_PREFIX_PATH=' + ';'.join(str(path) for path in search),
					   '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
					   '-DVEROSCRIPT_FOUNDATION_MODULES=' + ';'.join(modules),
					   '-DVEROSCRIPT_TEST_BINDINGS=' + ('ON' if script else 'OFF'),
					   '-DVEROSCRIPT_PACKAGING_PYTHON=' + sys.executable]
			if framework_only:
				command += ['-DVEROSCRIPT_TEST_GUI_FRAMEWORK_ONLY=ON']
			if core_sdk is not None:
				command += ['-DVEROSCRIPT_GUI_TEST_STDLIB=' + str(Path(core_sdk).resolve() / 'assets/stdlib')]
			if script and set(modules).difference(('Image', 'Font', 'Stb', 'FontBackend')):
				info = json.loads((builds[selected[0]] / 'veroscript-foundation-Release.json').read_text(encoding='utf-8'))
				vm = info.get('TestVmBackend') or settings.get('VEROSCRIPT_TEST_VM_LIBRARY')
				if not vm or not Path(vm).is_file():
					raise ValueError('Foundation script consumers require an explicit matching test VM backend; rebuild with a VM backend or set VEROSCRIPT_TEST_VM_LIBRARY')
				command += ['-DVEROSCRIPT_MODULE_ROOT=' + str(repo.resolve()),
							'-DVEROSCRIPT_TEST_VM_LIBRARY=' + Path(vm).resolve().as_posix()]
			command += ['-DVEROSCRIPT_FOUNDATION_SOURCE_' + name.upper() + '=' + str(repo / directory)
						for name, directory in FOUNDATION_MODULE_DIRECTORIES.items()]
			if settings.get('CMAKE_GENERATOR_PLATFORM'):
				command += ['-A', settings['CMAKE_GENERATOR_PLATFORM']]
			return command

		def consume(label, modules, search, script):
			subprocess.run(configure_command(label, modules, search, script), check=True)
			build = work / label
			subprocess.run([cmake, '--build', str(build), '--config', 'Release', '--parallel', str(jobs)], check=True)
			subprocess.run([ctest, '--test-dir', str(build), '-C', 'Release', '--output-on-failure', '--no-tests=error'], check=True)

		if len(selected) > 1 or bindings:
			for business in selected:
				if business != 'Sdl3':
					needed, pending = {business}, [business]
					while pending:
						item = pending.pop()
						dependencies = set(foundation_dependencies(item, build_cache(builds[item]), False)) - needed
						needed.update(dependencies)
						pending.extend(dependencies)
					consume('isolated ' + business, [business], [package_prefixes[name] for name in sorted(needed)], False)
		consume('consumer build', selected, prefixes, bindings)
		if 'Gui' in selected and 'RmlUi' in selected:
			subprocess.run(configure_command('legacy GUI discovery', ['Gui', 'RmlUi'], prefixes, bindings,
											framework_only=True), check=True)
			if bindings:
				pin = package_prefixes['RmlUi'] / 'lib/cmake/VeroScriptRmlUi/VeroScriptRmlUiRelease.cmake'
				original = pin.read_bytes()
				try:
					metadata = json.loads((package_prefixes['RmlUi'] / 'veroscript-module.json').read_text(encoding='utf-8'))
					identity = metadata['dependencyBuildIds']['CoreSDK'].encode('ascii')
					replacement = b'0' * 64 if identity != b'0' * 64 else b'1' * 64
					pin.write_bytes(original.replace(identity, replacement))
					probe = subprocess.run(configure_command('mismatched legacy GUI dependency', ['Gui', 'RmlUi'],
						prefixes, True, framework_only=True), capture_output=True, text=True, encoding='utf-8', errors='replace')
					if probe.returncode == 0 or 'version/build differs' not in probe.stdout + probe.stderr:
						raise ValueError('Legacy GUI discovery failed to check the RmlUi Core build')
				finally:
					pin.write_bytes(original)
		if 'Gui' in selected:
			pin = package_prefixes['Image'] / 'lib/cmake/VeroScriptImage/VeroScriptImageRelease.cmake'
			original = pin.read_bytes()
			try:
				pin.write_bytes(original + b'\nset(VeroScriptImage_PAYLOAD_ID "wrong-build")\n')
				probe = subprocess.run(configure_command('mismatched GUI dependency', ['Gui'], prefixes, False),
									   capture_output=True, text=True, encoding='utf-8', errors='replace')
				if probe.returncode == 0 or 'version/build differs' not in probe.stdout + probe.stderr:
					raise ValueError('Installed Gui SDK failed to reject a mismatched Image build')
			finally:
				pin.write_bytes(original)
		if 'Sdl3' in selected:
			pin = package_prefixes['Platform'] / 'lib/cmake/VeroScriptPlatform/VeroScriptPlatformRelease.cmake'
			original = pin.read_bytes()
			try:
				pin.write_bytes(original + b'\nset(VeroScriptPlatform_PAYLOAD_ID "wrong-build")\n')
				probe = subprocess.run(configure_command('mismatched dependency', ['Sdl3'], prefixes, False),
									   capture_output=True, text=True, encoding='utf-8', errors='replace')
				if probe.returncode == 0 or 'version/build differs' not in probe.stdout + probe.stderr:
					raise ValueError('Installed SDL3 SDK failed to reject a mismatched Platform build')
			finally:
				pin.write_bytes(original)
