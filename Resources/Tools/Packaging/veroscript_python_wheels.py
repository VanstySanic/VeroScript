# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build and audit the declared CPython wheel matrix for one native target."""
from __future__ import annotations

from email.parser import BytesParser
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import tomllib

from veroscript_paths import module_root
from veroscript_products import WINDOWS_WHEEL_TAGS


def supported_versions(repo: Path) -> tuple[str, ...]:
	project = module_root(repo) / 'Extensions/Language/Python/pyproject.toml'
	config = tomllib.loads(project.read_text(encoding='utf-8'))
	requirement = config['project']['requires-python']
	match = re.fullmatch(r'>=3\.(\d+),<3\.(\d+)', requirement.replace(' ', ''))
	if not match or int(match[1]) >= int(match[2]):
		raise ValueError(f'Unsupported Python matrix requirement: {requirement}')
	versions = tuple(f'3.{minor}' for minor in range(int(match[1]), int(match[2])))
	selection = config['tool']['cibuildwheel']['build']
	expected = 'cp3{' + ','.join(value.split('.')[1] for value in versions) + '}-*'
	# The compact selector shares the leading characters of the wheel tags.
	tags = tuple('cp' + value.replace('.', '') for value in versions)
	prefix = tags[0]
	for tag in tags[1:]:
		while not tag.startswith(prefix):
			prefix = prefix[:-1]
	compact = prefix + '{' + ','.join(tag[len(prefix):] for tag in tags) + '}-*'
	if selection not in (expected, compact, ' '.join(tag + '-*' for tag in tags)):
		raise ValueError('cibuildwheel selection does not match requires-python')
	return versions


def validate_versions(versions) -> tuple[str, ...]:
	if (not isinstance(versions, (tuple, list)) or not versions
			or any(not isinstance(value, str) for value in versions) or len(set(versions)) != len(versions)
			or any(not re.fullmatch(r'3\.\d+', value) for value in versions)):
		raise ValueError('Invalid recorded Python wheel matrix')
	return tuple(versions)


def distribution_metadata(path: Path, entries: dict) -> bytes:
	if path.suffix == '.whl':
		metadata = [data for name, (data, _) in entries.items() if name.endswith('.dist-info/METADATA')]
	else:
		name = path.name.removesuffix('.tar.gz') + '/PKG-INFO'
		metadata = [entries[name][0]] if name in entries else []
	if len(metadata) != 1:
		raise ValueError(f'Missing/ambiguous Python distribution metadata: {path}')
	return metadata[0]


def verify_python_requirement(metadata: bytes, versions: tuple[str, ...], subject: Path) -> None:
	minors = sorted(int(value.split('.')[1]) for value in validate_versions(versions))
	if len(minors) != minors[-1] - minors[0] + 1:
		raise ValueError('The declared CPython matrix must be contiguous')
	expected = {f'>=3.{minors[0]}', f'<3.{minors[-1] + 1}'}
	values = BytesParser().parsebytes(metadata).get_all('Requires-Python', [])
	if len(values) != 1 or set(values[0].replace(' ', '').split(',')) != expected:
		raise ValueError(f'Python Requires-Python metadata differs from the declared matrix: {subject}')


def macos_deployment_version(data: bytes) -> tuple[int, int, int]:
	from veroscript_release import native_identity
	if len(data) < 32 or native_identity(data[:4096])[0] != 'macos':
		raise ValueError('Expected a macOS wheel Mach-O library')
	order = '<' if data[:4] == b'\xcf\xfa\xed\xfe' else '>'
	count, size = struct.unpack_from(order + 'II', data, 16)
	cursor, end = 32, 32 + size
	if end > len(data) or count > size // 8:
		raise ValueError('Invalid Mach-O wheel load commands')
	minimum = []
	for _ in range(count):
		if end - cursor < 8:
			raise ValueError('Truncated Mach-O wheel load command')
		command, length = struct.unpack_from(order + 'II', data, cursor)
		if length < 8 or length % 4 or cursor + length > end:
			raise ValueError('Invalid Mach-O wheel load command')
		if command == 0x32:  # LC_BUILD_VERSION
			if length < 24 or struct.unpack_from(order + 'I', data, cursor + 8)[0] != 1:
				raise ValueError('Wheel library was built for another Apple platform')
			minimum.append(struct.unpack_from(order + 'I', data, cursor + 12)[0])
		elif command == 0x24:  # LC_VERSION_MIN_MACOSX
			if length < 16:
				raise ValueError('Invalid minimum macOS version command')
			minimum.append(struct.unpack_from(order + 'I', data, cursor + 8)[0])
		cursor += length
	if cursor != end or len(minimum) != 1 or not minimum[0] >> 16:
		raise ValueError('Missing or ambiguous macOS deployment version in wheel')
	value = minimum[0]
	return value >> 16, (value >> 8) & 255, value & 255


def verify_source_distribution(path: Path) -> None:
	"""Reject build caches and incomplete self-contained cross-build inputs."""
	cache_directories = {'.cxx', '.gradle', '.externalnativebuild'}
	names = set()
	with tarfile.open(path) as archive:
		for entry in archive:
			names.add(entry.name)
			if any(part.casefold() in cache_directories for part in PurePosixPath(entry.name).parts):
				raise ValueError(f'Python source distribution contains build cache: {entry.name}')
	for name in names:
		marker = '/_native/Extensions/Language/Python/'
		if name.endswith(marker + 'VeroScriptPython/Resources/Tools/CMake/VeroScriptPythonDesktopCross.cmake'):
			prefix = name.split(marker, 1)[0] + marker + 'Resources/Tools/Desktop/'
			if not {prefix + 'desktop_sdk.py', prefix + 'CMakeLists.txt'}.issubset(names):
				raise ValueError('Python source distribution is missing desktop cross-build tools')


def verify_matrix(paths: list[Path], repo: Path | None, target: str, package_version: str,
				  versions: tuple[str, ...] | None = None, *, require_complete: bool = True) -> None:
	from veroscript_business_release import distribution_identity, distribution_targets
	from veroscript_release import TARGETS, archive_entries, native_identity, verify_windows_runtime
	if versions is None:
		if repo is None:
			raise ValueError('A source checkout or recorded Python wheel matrix is required')
		versions = supported_versions(repo)
	versions = validate_versions(versions)
	expected = {'cp' + value.replace('.', '') for value in versions}
	found = set()
	sdists = []
	for path in paths:
		if path.suffix != '.whl':
			if path.name == f'veroscript-{package_version}.tar.gz':
				sdists.append(path)
			continue
		match = re.fullmatch(r'veroscript-' + re.escape(package_version) + r'-(cp\d+)-(cp\d+)-(.+)\.whl', path.name)
		if not match or match[1] != match[2] or match[1] not in expected:
			raise ValueError(f'Unexpected Python wheel ABI: {path.name}')
		if target not in distribution_targets(path, target):
			raise ValueError(f'Python wheel platform mismatch: {path.name}')
		entries = archive_entries(path, max_entry_bytes=1024 * 1024)
		if distribution_identity(path, entries=entries) != ('Python', package_version):
			raise ValueError(f'Python wheel version mismatch: {path.name}')
		verify_python_requirement(distribution_metadata(path, entries), versions, path)
		wheel_metadata = [data for name, (data, _) in entries.items() if name.endswith('.dist-info/WHEEL')]
		tag = f'{match[1]}-{match[2]}-{match[3]}'
		if len(wheel_metadata) != 1 or tag not in BytesParser().parsebytes(wheel_metadata[0]).get_all('Tag', []):
			raise ValueError(f'Python wheel filename/WHEEL tag mismatch: {path.name}')
		extensions = [name for name in entries if name.startswith('veroscript/_veroscript.') and name.endswith(('.pyd', '.so'))]
		if len(extensions) != 1 or native_identity(entries[extensions[0]][0][:4096]) != TARGETS[target]:
			raise ValueError(f'Python wheel native architecture mismatch: {path.name}')
		extension = Path(extensions[0]).name
		if target in WINDOWS_WHEEL_TAGS:
			valid_abi = extension == f'_veroscript.{match[1]}-{WINDOWS_WHEEL_TAGS[target]}.pyd'
		else:
			valid_abi = extension.startswith(f'_veroscript.cpython-{match[1][2:]}-')
		if not valid_abi:
			raise ValueError(f'Python wheel extension ABI mismatch: {path.name}')
		interop = [data for name, (data, _) in entries.items() if Path(name).name.startswith(('libVeroScriptInterop.', 'VeroScriptInterop.'))]
		for data in interop:
			if native_identity(data[:4096]) != TARGETS[target]:
				raise ValueError(f'Python wheel Interop architecture mismatch: {path.name}')
		if target.startswith('macos-'):
			platforms = [re.fullmatch(r'macosx_(\d+)_(\d+)_(?:x86_64|arm64)', value) for value in match[3].split('.')]
			if not all(platforms):
				raise ValueError(f'Invalid macOS wheel deployment tag: {path.name}')
			oldest = min((int(value[1]), int(value[2]), 0) for value in platforms)
			if any(macos_deployment_version(data) > oldest for data in [entries[extensions[0]][0], *interop]):
				raise ValueError(f'Python wheel macOS deployment tag is older than its libraries: {path.name}')
		notices = [data for name, (data, _) in entries.items() if name.endswith('.dist-info/licenses/LICENSE')]
		if len(notices) != 1 or not notices[0].strip():
			raise ValueError(f'Python wheel project license mismatch: {path.name}')
		if repo and notices[0].decode('utf-8-sig').replace('\r\n', '\n').strip() != (repo / 'LICENSE').read_text(encoding='utf-8-sig').strip():
			raise ValueError(f'Python wheel project license mismatch: {path.name}')
		if target.startswith('win-'):
			verify_windows_runtime(entries, TARGETS[target])
		found.add(match[1])
	if len(sdists) > 1 or (require_complete and (found != expected or len(sdists) != 1)):
		raise ValueError(f'Incomplete Python wheel matrix: missing={sorted(expected - found)}, sdists={len(sdists)}')
	if not sdists:
		return
	sdist = sdists[0]
	entries = archive_entries(sdist, max_entry_bytes=4096)
	if distribution_identity(sdist, entries=entries) != ('Python', package_version):
		raise ValueError('Python source distribution version mismatch')
	verify_python_requirement(distribution_metadata(sdist, entries), versions, sdist)
	verify_source_distribution(sdist)


def interpreter_identity(executable: str) -> dict:
	code = ("import json,platform,struct,sys,sysconfig; print(json.dumps({"
			"'executable':sys.executable,'version':'.'.join(map(str,sys.version_info[:2])),"
			"'patch':platform.python_version(),'implementation':sys.implementation.name,"
			"'system':platform.system(),'machine':platform.machine().lower(),'bits':struct.calcsize('P')*8,"
			"'debug':bool(sysconfig.get_config_var('Py_DEBUG')),'freeThreaded':bool(sysconfig.get_config_var('Py_GIL_DISABLED'))}))")
	result = subprocess.run([executable, '-I', '-c', code], check=True, capture_output=True, text=True, timeout=30)
	return json.loads(result.stdout)


def discover_interpreters(repo: Path, target: str, explicit: list[str]) -> dict[str, dict]:
	from veroscript_release import TARGETS
	versions = supported_versions(repo)
	requested = list(explicit)
	requested.extend(filter(None, os.environ.get('VEROSCRIPT_PYTHON_EXECUTABLES', '').split(os.pathsep)))
	candidates = list(requested)
	candidates.append(sys.executable)
	if os.name == 'nt' and shutil.which('py'):
		# Listing installed runtimes avoids an install manager fetching a missing version.
		result = subprocess.run(['py', '-0p'], capture_output=True, text=True, timeout=30)
		if result.returncode == 0:
			for line in result.stdout.splitlines():
				path = re.search(r'([A-Za-z]:[\\/].*python\.exe)\s*$', line.strip(' "'), re.IGNORECASE)
				if path and any(re.search(r'\b' + re.escape(value) + r'(?!\d)', line) for value in versions):
					candidates.append(path[1])
	for version in versions:
		executable = shutil.which('python' + version)
		if executable:
			candidates.append(executable)
	found = {}
	system, architecture = TARGETS[target]
	machine_names = {'x64': ('x86_64', 'amd64'), 'arm64': ('aarch64', 'arm64'), 'x86': ('x86', 'i386', 'i686')}
	for executable in dict.fromkeys(candidates):
		try:
			identity = interpreter_identity(executable)
		except (OSError, subprocess.SubprocessError, ValueError) as error:
			if executable in requested:
				raise ValueError(f'Cannot inspect requested CPython interpreter: {executable}') from error
			continue
		if identity['version'] not in versions:
			if executable in requested:
				raise ValueError(f'Unsupported matrix interpreter: {executable}')
			continue
		valid = (identity['implementation'] == 'cpython' and identity['system'].lower().replace('darwin', 'macos') == system
				 and identity['machine'] in machine_names[architecture] and identity['bits'] == (32 if architecture == 'x86' else 64)
				 and not identity['debug'] and not identity['freeThreaded'])
		if not valid:
			if executable in requested:
				raise ValueError(f'Interpreter does not match {target}: {identity}')
			continue
		found.setdefault(identity['version'], identity)
	missing = set(versions) - found.keys()
	if missing:
		raise ValueError(f'Missing CPython interpreters: {sorted(missing)}. Install them and pass --python or VEROSCRIPT_PYTHON_EXECUTABLES.')
	return found


def run(*command, environment: dict, cwd: Path | None = None) -> None:
	print('+ ' + ' '.join(map(str, command)), flush=True)
	subprocess.run(list(map(str, command)), env=environment, cwd=cwd, check=True)


def retire_completed_abi(work: Path, minor: str) -> None:
	"""Retire only owned successful ABI work, retaining any native symbol outputs."""
	from veroscript_release import linked_path
	if not re.fullmatch(r'3\.[0-9]+', minor):
		raise ValueError('Invalid completed Python ABI')
	work = work.resolve()
	for prefix in ('source-', 'consumer-', 'build-env-'):
		directory = work / (prefix + minor)
		if not directory.exists():
			continue
		if linked_path(directory) or directory.resolve().parent != work:
			raise ValueError('Completed Python work escapes its owned directory')
		symbols = sorted((path for path in directory.rglob('*') if path.suffix in {'.pdb', '.debug', '.dSYM'}),
						 key=lambda path: len(path.parts))
		for source in symbols:
			if not source.exists():
				continue
			if linked_path(source):
				raise ValueError('Linked Python symbol output is not owned completed work')
			destination = work / 'retained-symbols' / minor / directory.name / source.relative_to(directory)
			destination.parent.mkdir(parents=True, exist_ok=True)
			shutil.move(source, destination)
		shutil.rmtree(directory)


def build_matrix(repo: Path, output: Path, work: Path, target: str, package_version: str,
				 interpreters: dict[str, dict], native_executable: Path, jobs: int,
				 generator: str | None = None, *, clean_completed: bool | None = None) -> dict:
	from veroscript_release import digest, scratch_directory
	project = module_root(repo) / 'Extensions/Language/Python'
	if clean_completed is None:
		# Native Apple batches share a small VM volume. Other hosts keep their
		# existing work-retention behavior unless the caller explicitly opts in.
		clean_completed = target.startswith('macos-')
	config = tomllib.loads((project / 'pyproject.toml').read_text(encoding='utf-8'))
	environment = os.environ.copy()
	for name in ('PYTHONPATH', 'PYTHONHOME', 'VERO_PATH', 'VERO_STDLIB_PATH'):
		environment.pop(name, None)
	environment.update(PYTHONDONTWRITEBYTECODE='1', CMAKE_BUILD_PARALLEL_LEVEL=str(jobs),
					   VEROSCRIPT_EXECUTABLE=str(native_executable.resolve()))
	if generator:
		environment['CMAKE_GENERATOR'] = generator
		environment.pop('CMAKE_GENERATOR_PLATFORM', None)
		if target.startswith('win-') and generator.startswith('Visual Studio'):
			environment['CMAKE_GENERATOR_PLATFORM'] = {'win-x64': 'x64', 'win-arm64': 'ARM64', 'win-x86': 'Win32'}[target]
			# MSBuild's project parallelism alone does not parallelize CL.
			environment['CL'] = (environment.get('CL', '') + f' /MP{jobs}').strip()
	builders = {}
	for version, identity in sorted(interpreters.items()):
		directory = work / ('build-env-' + version)
		run(identity['executable'], '-m', 'venv', directory, environment=environment)
		python = directory / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
		run(python, '-m', 'pip', 'install', 'build', *config['build-system']['requires'], environment=environment)
		builders[version] = python
	coordinator = builders[max(builders, key=lambda value: tuple(map(int, value.split('.'))))]
	run(coordinator, '-m', 'build', '--sdist', '--no-isolation', '--outdir', output, project, environment=environment)
	sdists = list(output.glob('*.tar.gz'))
	if len(sdists) != 1 or sdists[0].name != f'veroscript-{package_version}.tar.gz':
		raise ValueError('Built Python source version differs from the selected Python business version')
	verify_source_distribution(sdists[0])
	records = []
	for version, python in builders.items():
		source_directory = work / ('source-' + version)
		source_directory.mkdir()
		with tarfile.open(sdists[0]) as archive:
			archive.extractall(source_directory, filter='data')
		source = source_directory / f'veroscript-{package_version}'
		if target.startswith('win-'):
			# MSBuild file tracking still has MAX_PATH limits even when Windows
			# long paths are enabled. Both source and build paths must stay
			# outside the nested release stage, with one clean tree per ABI.
			with tempfile.TemporaryDirectory(prefix='py-native-', dir=scratch_directory(repo)) as native:
				native = Path(native)
				with tarfile.open(sdists[0]) as archive:
					archive.extractall(native / 's', filter='data')
				native_source = native / 's' / f'veroscript-{package_version}'
				native_build = native / 'b'
				native_build.mkdir()
				run(python, '-m', 'build', '--wheel', '--no-isolation', '--outdir', output,
					'--config-setting', 'build-dir=' + str(native_build), native_source, environment=environment)
		else:
			run(python, '-m', 'build', '--wheel', '--no-isolation', '--outdir', output, source, environment=environment)
		tag = 'cp' + version.replace('.', '')
		wheels = list(output.glob(f'veroscript-{package_version}-{tag}-{tag}-*.whl'))
		if len(wheels) != 1:
			raise ValueError(f'Expected one {tag} wheel')
		consumer_directory = work / ('consumer-' + version)
		run(interpreters[version]['executable'], '-m', 'venv', consumer_directory, environment=environment)
		consumer = consumer_directory / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
		run(consumer, '-m', 'pip', 'install', wheels[0], environment=environment)
		run(consumer, '-m', 'pip', 'check', environment=environment)
		run(consumer, project / 'Resources/Tools/Packaging/test-wheel.py', '--project', project, environment=environment)
		records.append({'python': interpreters[version], 'wheel': wheels[0].name, 'sha256': digest(wheels[0]),
						'installedTestsPassed': True})
		(work / 'python-wheel-matrix.partial.json').write_text(json.dumps({'target': target,
			'version': package_version, 'sdistSha256': digest(sdists[0]), 'wheels': records}, indent=2) + '\n', encoding='utf-8')
		if clean_completed:
			retire_completed_abi(work, version)
	verify_matrix(list(output.iterdir()), repo, target, package_version)
	report = {'success': True, 'target': target, 'version': package_version,
			  'sdist': {'name': sdists[0].name, 'sha256': digest(sdists[0])}, 'wheels': records}
	(work / 'python-wheel-matrix.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
	return report
