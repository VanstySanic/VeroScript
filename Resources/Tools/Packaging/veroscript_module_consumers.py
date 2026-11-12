# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Consume unpacked module SDKs after relocation, using their public exports."""
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile

from veroscript_modules import build_cache
from veroscript_release import scratch_directory


def verify_consumers(repo, candidate, builds, selected, target, core_sdk=None, *, bindings=True, jobs=2, cpp_sdk=None):
	from veroscript_products import FOUNDATION_BUSINESSES
	foundation = [name for name in selected if name in FOUNDATION_BUSINESSES]
	if foundation:
		from veroscript_foundation_consumers import verify_foundation_consumers
		verify_foundation_consumers(repo, candidate, builds, foundation, target, core_sdk, bindings=bindings, jobs=jobs, cpp_sdk=cpp_sdk)
		selected = [name for name in selected if name not in FOUNDATION_BUSINESSES]
		if not selected:
			return
	with tempfile.TemporaryDirectory(prefix='veroscript-installed-模块 SDK-', dir=scratch_directory(repo)) as temporary:
		work = Path(temporary)
		prefixes = []
		for archive in sorted((candidate / 'modules').iterdir()):
			if archive.suffix == '.vspkg':
				continue
			from veroscript_business_release import artifact_business
			if artifact_business(archive.name) not in selected:
				continue
			if archive.suffix == '.zip':
				with zipfile.ZipFile(archive) as reader:
					reader.extractall(work)
				root_name = archive.name.removesuffix('.zip')
			else:
				with tarfile.open(archive) as reader:
					reader.extractall(work, filter='data')
				root_name = archive.name.removesuffix('.tar.gz')
			prefixes.append(work / root_name)
		settings = build_cache(builds[selected[0]])
		if bindings:
			if core_sdk is None:
				# A standalone binding build records the exact installed Core
				# package used to compile it. Root builds require an explicit SDK.
				core_config = settings.get('VeroScript_DIR')
				if not core_config:
					raise ValueError('Provide --core-sdk for installed script SDK consumption')
				core_sdk = Path(core_config).resolve().parents[2]
			core_sdk = Path(core_sdk).resolve()
			prefixes.append(core_sdk)
			vm_name = ('VeroScriptVmBytecode.dll' if target.startswith('win-') else
					   'libVeroScriptVmBytecode.dylib' if target.startswith('macos-') else
					   'libVeroScriptVmBytecode.so')
			vm_library = Path(settings.get('VEROSCRIPT_TEST_VM_LIBRARY') or
							  core_sdk / 'bin/Vm' / vm_name)
			if not vm_library.is_absolute():
				vm_library = Path(settings.get('CMAKE_HOME_DIRECTORY', repo)) / vm_library
			vm_library = vm_library.resolve()
			if not vm_library.is_file():
				raise ValueError('Script SDK consumers require a matching VM backend in --core-sdk/bin/Vm '
								 'or VEROSCRIPT_TEST_VM_LIBRARY')
		cmake = settings.get('CMAKE_COMMAND', 'cmake')
		ctest = str(Path(cmake).with_name('ctest.exe' if target.startswith('win-') else 'ctest'))
		build = work / 'consumer build'
		arguments = [cmake, '-S', str(repo / 'Resources/Tools/Packaging/Consumer/Modules'), '-B', str(build),
					 '-G', settings['CMAKE_GENERATOR'], '-DCMAKE_BUILD_TYPE=Release',
					 '-DCMAKE_PREFIX_PATH=' + ';'.join(str(prefix) for prefix in prefixes),
					 '-DVEROSCRIPT_MODULES=' + ';'.join(selected),
					 '-DVEROSCRIPT_TEST_BINDINGS=' + ('ON' if bindings else 'OFF'),
					 '-DVEROSCRIPT_JSON_CONSUMER_SOURCE=' + str(repo / 'Modules/Network/VeroScriptJson/Private/Tests/Consumer/main.cpp'),
					 '-DVEROSCRIPT_NETWORK_CONSUMER_SOURCE=' + str(repo / 'Modules/Network/VeroScriptNetwork/Resources/Tests/Integration/Consumer/main.cpp')]
		if bindings:
			arguments.append('-DVEROSCRIPT_TEST_VM_LIBRARY=' + str(vm_library))
		if settings.get('CMAKE_CXX_STANDARD'):
			arguments.append('-DCMAKE_CXX_STANDARD=' + settings['CMAKE_CXX_STANDARD'])
		if settings.get('CMAKE_GENERATOR_PLATFORM'):
			arguments += ['-A', settings['CMAKE_GENERATOR_PLATFORM']]
		subprocess.run(arguments, check=True)
		subprocess.run([cmake, '--build', str(build), '--config', 'Release', '--parallel', str(jobs)], check=True)
		subprocess.run([ctest, '--test-dir', str(build), '-C', 'Release', '--output-on-failure', '--no-tests=error'], check=True)
