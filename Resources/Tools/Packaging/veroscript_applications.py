# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Stage optional local application/build services and relocatable runtime components."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tarfile
import zipfile
from veroscript_vm import stage_runtime_from_binaries
from veroscript_jit import build_cache, cmake_enabled


def stage_application_sdk(build: Path, stage: Path, config: str = "Release") -> None:
	cache = build_cache(build)
	application = cmake_enabled(cache, 'VEROSCRIPT_BUILD_APPLICATION')
	runtime = (cmake_enabled(cache, 'VEROSCRIPT_BUILD_PACKAGE_RUNTIME') or
			   cmake_enabled(cache, 'VEROSCRIPT_BUILD_PACKAGE_MANAGER'))
	components = ["VeroScriptApplication"] if application else []
	if runtime:
		components.append("VeroScriptPackageRuntime")
		if application:
			components.append("VeroScriptApplicationPackages")
	if cmake_enabled(cache, 'VEROSCRIPT_BUILD_BUILD_SYSTEM'):
		components.append("VeroScriptBuild")
	for component in components:
		subprocess.run(["cmake", "--install", str(build), "--prefix", str(stage),
						"--config", config, "--component", component], check=True)


def finalize_application_sdk(stage: Path, core_version: str | None = None) -> None:
	if core_version is not None and not re.fullmatch(r'\d+\.\d+\.\d+', core_version):
		raise ValueError('Invalid Core SDK package version')
	libraries = [stage / "lib" / name for name in ("VeroScript.lib", "libVeroScript.a")
				 if (stage / "lib" / name).is_file()]
	directories = [stage / "lib/cmake" / name for name in
				   ("VeroScriptApplication", "VeroScriptPackageRuntime")]
	if not any(directory.is_dir() for directory in directories):
		return
	if len(libraries) != 1:
		raise ValueError("Application SDK requires exactly one matching Core static library")
	with libraries[0].open("rb") as stream:
		digest = hashlib.file_digest(stream, "sha256").hexdigest()
	for directory in directories:
		if directory.is_dir():
			name = directory.name
			if core_version is not None:
				config = directory / f'{name}Config.cmake'
				text, count = re.subn(r'find_dependency\(VeroScript\s+\d+\.\d+\.\d+\s+EXACT\s+CONFIG\)',
					f'find_dependency(VeroScript {core_version} EXACT CONFIG)', config.read_text(encoding='utf-8-sig'))
				if count != 1:
					raise ValueError(f'Invalid {name} Core SDK dependency contract')
				config.write_text(text, encoding='utf-8')
			(directory / f"{name}CoreBuild.cmake").write_text(
				f'set({name}_CORE_BUILD_ID "{digest}")\n', encoding="utf-8")


def stage_application_runtime(repo: Path, binaries: Path, root: Path,
							  executable_directory: str = "", *, target_context: dict | None = None) -> bool:
	suffix = ".exe" if (binaries / "veroscript-runtime.exe").is_file() else ""
	executable = binaries / f"veroscript-runtime{suffix}"
	if not executable.is_file():
		return False
	relative = Path(executable_directory) / executable.name
	destination = root / relative
	destination.parent.mkdir(parents=True, exist_ok=True)
	if executable.resolve() != destination.resolve():
		shutil.copy2(executable, destination)
	launcher = binaries / f"veroscript-launcher{suffix}"
	if launcher.is_file():
		target = destination.with_name(launcher.name)
		if launcher.resolve() != target.resolve():
			shutil.copy2(launcher, target)
	def is_windows_runtime(file: Path) -> bool:
		name = file.name.casefold()
		return name.endswith('.dll') and name.startswith(('msvcp', 'vcruntime', 'concrt', 'vcomp'))
	stage_runtime_from_binaries(binaries, destination.parent)
	if target_context is None:
		target = json.loads(subprocess.check_output([str(executable), "--component-info"], text=True, encoding="utf-8"))
	else:
		from veroscript_release import binary_prefix,native_identity
		if (not isinstance(target_context,dict) or
			(target_context.get('os'),target_context.get('arch'))!=native_identity(binary_prefix(executable))):
			raise ValueError('Explicit application target differs from its compiled executable')
		target = target_context
	if target.get("core") != "0.1.1" or not target.get("coreBuild") or not target.get("abis"):
		raise ValueError("Runtime does not expose a complete application target identity")
	vendor = repo / "VeroScriptApplication/ThirdParty"
	for source, folder in (("miniz/LICENSE", "miniz"), ("json/LICENSE.MIT", "json"), ("sha256/LICENSE", "sha256")):
		output = root / "licenses/application" / folder / Path(source).name
		output.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(vendor / source, output)
	selected = [relative]

	vm_destination = destination.parent / 'Vm'
	selected.extend(file.relative_to(root) for file in vm_destination.rglob('*') if file.is_file())
	if suffix:
		selected.extend(file.relative_to(root) for file in destination.parent.iterdir()
						if file.is_file() and is_windows_runtime(file))
		notice = destination.parent / 'licenses/MicrosoftVisualCpp/NOTICE.txt'
		if notice.is_file():
			selected.append(notice.relative_to(root))
	for prefix in ("stdlib", "assets/stdlib", "localization", "assets/localization", "licenses/application", "licenses/package-runtime"):
		if (root / prefix).is_dir():
			selected.extend(file.relative_to(root) for file in (root / prefix).rglob("*") if file.is_file())
	if (root / "LICENSE").is_file():
		selected.append(Path("LICENSE"))
	records = []
	for relative_file in sorted(set(selected), key=lambda value: value.as_posix()):
		file = root / relative_file
		if file.is_symlink():
			raise ValueError("Runtime component inputs cannot contain links")
		with file.open("rb") as stream:
			digest = hashlib.file_digest(stream, "sha256").hexdigest()
		records.append({"path": relative_file.as_posix(), "bytes": str(file.stat().st_size),
						"sha256": digest, "mode": "0755" if relative_file == relative or bool(file.stat().st_mode & 0o100) else "0644"})
	stdlib = [name for name in ("stdlib", "assets/stdlib") if (root / name).is_dir()]
	descriptor = {"format": "veroscript-runtime", "formatVersion": "0.1.1", "applicationProtocol": "0.1.1",
				  "target": target, "executable": relative.as_posix(), "stdlib": stdlib,
				  "files": records, "extensions": {}}
	(root / "veroscript-runtime.json").write_text(json.dumps(descriptor, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
	return True


def verify_application_payload(archive, entries, prefix, role, identity, core_version, build_id, native_version):
	"""Audit optional SDK exports and the runtime receipt against shipped bytes."""
	from veroscript_release import safe_member
	metadata_name = 'sdk-manifest.json' if role == 'core-sdk' else 'runtime-manifest.json'
	metadata = json.loads(entries[prefix + metadata_name][0]) if prefix + metadata_name in entries else {}
	if not isinstance(metadata, dict):
		raise ValueError('Invalid application capability manifest')
	for flag in ('application', 'packageRuntime', 'buildSystem'):
		if flag in metadata and type(metadata[flag]) is not bool:
			raise ValueError('Invalid application SDK feature declaration: ' + flag)
	def require(name):
		if not entries.get(prefix + name, (b'', 0))[0]:
			raise ValueError('Missing application deployment payload: ' + name)
	if role == 'core-sdk':
		library = lambda name: 'lib/' + (name + '.lib' if identity[0] == 'windows' else 'lib' + name + '.a')
		for feature, name, header, libraries in (
			('application', 'VeroScriptApplication', 'include/Application/VeroScriptApplication.h', ('VeroScriptApplication', 'VeroScriptApplicationZip')),
			('packageRuntime', 'VeroScriptPackageRuntime', 'include/PackageManager/VeroScriptPackageManager.h', ('VeroScriptPackageRuntime', 'VeroScriptPackageZip')),
			('buildSystem', 'VeroScriptBuild', 'include/Build/VeroScriptBuild.h', ('VeroScriptBuild',))):
			directory = 'lib/cmake/' + name + '/'
			config = directory + name + 'Config.cmake'
			present = prefix + config in entries
			if feature in metadata and metadata[feature] != present:
				raise ValueError('SDK deployment differs from the declared feature: ' + feature)
			if not present:
				continue
			for required in (header, config, directory + name + 'Targets.cmake',
							 directory + name + 'ConfigVersion.cmake', *(library(lib) for lib in libraries)):
				require(required)
			notices = ({'application': ['licenses/application/' + name for name in
				('json/LICENSE.MIT', 'miniz/LICENSE', 'sha256/LICENSE', 'sources.json')],
				'packageRuntime': ['licenses/package-runtime/' + name for name in
				('json/LICENSE.MIT', 'miniz/LICENSE', 'sha256/LICENSE')],
				'buildSystem': ['licenses/build/json/LICENSE.MIT', 'licenses/build/sources.json']}[feature])
			for required in notices:
				require(required)
			if feature != 'buildSystem':
				text = entries[prefix + config][0].decode('utf-8-sig')
				pin = directory + name + 'CoreBuild.cmake'
				require(pin)
				if (re.findall(r'find_dependency\(VeroScript\s+(\d+\.\d+\.\d+)\s+EXACT\s+CONFIG\)', text) != [core_version]
						or re.findall(r'CORE_BUILD_ID "([0-9a-f]{64})"', entries[prefix + pin][0].decode('utf-8-sig')) != [build_id]):
					raise ValueError('Application SDK Core version/build identity mismatch: ' + name)
			elif prefix + 'lib/cmake/VeroScriptApplication/VeroScriptApplicationConfig.cmake' not in entries:
				raise ValueError('Build SDK requires the Application SDK')
		application = prefix + 'lib/cmake/VeroScriptApplication/VeroScriptApplicationConfig.cmake' in entries
		packages = prefix + 'lib/cmake/VeroScriptPackageRuntime/VeroScriptPackageRuntimeConfig.cmake' in entries
		if application and packages:
			adapter = 'lib/cmake/VeroScriptApplicationPackages/VeroScriptApplicationPackages'
			for name in ('include/Application/VeroScriptApplicationPackages.h', library('VeroScriptApplicationPackages'),
						 adapter + 'Config.cmake', adapter + 'ConfigVersion.cmake', adapter + 'Targets.cmake'):
				require(name)
	receipt_name = prefix + 'veroscript-runtime.json'
	if metadata.get('application') is False and receipt_name in entries:
		raise ValueError('Application runtime differs from the declared feature')
	if metadata.get('application'):
		require('veroscript-runtime.json')
		base = 'bin/' if role == 'core-sdk' else ''
		suffix = '.exe' if identity[0] == 'windows' else ''
		for name in ('veroscript-runtime', 'veroscript-launcher'):
			require(base + name + suffix)
		for name in ('json/LICENSE.MIT', 'miniz/LICENSE', 'sha256/LICENSE'):
			require('licenses/application/' + name)
	if receipt_name not in entries:
		return
	receipt = json.loads(entries[receipt_name][0])
	if (not isinstance(receipt, dict) or set(receipt) !=
			{'format', 'formatVersion', 'applicationProtocol', 'target', 'executable', 'stdlib', 'files', 'extensions'}
			or not isinstance(receipt['files'], list) or not isinstance(receipt['extensions'], dict)):
		raise ValueError('Invalid application runtime receipt contract')
	target = receipt.get('target', {})
	if (not isinstance(target, dict) or set(target) !=
			{'core', 'coreBuild', 'os', 'arch', 'environment', 'abis', 'minimumVersions', 'capabilities'}
			or any(not isinstance(target.get(name), str) or not target[name]
				for name in ('core', 'coreBuild', 'os', 'arch', 'environment'))
			or any(not isinstance(target.get(name), dict) or any(not isinstance(value, str) or not value
				for value in target[name].values()) for name in ('abis', 'minimumVersions'))
			or not isinstance(target.get('capabilities'), list)
			or any(not isinstance(value, str) or not value for value in target['capabilities'])
			or len(set(target['capabilities'])) != len(target['capabilities'])):
		raise ValueError('Invalid application runtime target contract')
	if (not isinstance(target, dict) or receipt.get('format') != 'veroscript-runtime' or receipt.get('formatVersion') != '0.1.1'
			or receipt.get('applicationProtocol') != '0.1.1' or target.get('core') != native_version
			or (target.get('os'), target.get('arch')) != identity or not target.get('environment')
			or not isinstance(target.get('coreBuild'), str) or not re.fullmatch('[0-9a-f]{64}', target['coreBuild'])
			or not isinstance(target.get('abis'), dict) or not isinstance(target['abis'].get('vm'), str) or not target['abis']['vm']):
		raise ValueError('Invalid application runtime target/ABI receipt')
	wanted = {}
	folded = set()
	for item in receipt.get('files', []):
		if (not isinstance(item, dict) or set(item) != {'path', 'bytes', 'sha256', 'mode'}
				or not isinstance(item.get('path'), str) or not isinstance(item.get('bytes'), str)
				or not re.fullmatch(r'(0|[1-9][0-9]{0,19})', item['bytes'])
				or not isinstance(item.get('sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', item['sha256'])
				or item.get('mode') not in {'0644', '0755'}):
			raise ValueError('Invalid application runtime receipt file contract')
		name = safe_member(item['path'])
		if (name != item['path'] or len(name.encode('utf-8')) > 511 or name.casefold() in folded
				or prefix + name == receipt_name or prefix + name not in entries):
			raise ValueError('Invalid/duplicate application runtime receipt path')
		wanted[name] = item
		folded.add(name.casefold())
	if not wanted or receipt.get('executable') not in wanted:
		raise ValueError('Application runtime receipt omits its executable')
	base = 'bin/' if role == 'core-sdk' else ''
	vm = 'VeroScriptVmBytecode.dll' if identity[0] == 'windows' else 'libVeroScriptVmBytecode.' + ('dylib' if identity[0] == 'macos' else 'so')
	executable = base + 'veroscript-runtime' + ('.exe' if identity[0] == 'windows' else '')
	stdlib = 'assets/stdlib' if role == 'core-sdk' else 'stdlib'
	if (receipt.get('executable') != executable or base + 'Vm/' + vm not in wanted
			or receipt.get('stdlib') != [stdlib] or stdlib + '/core.vs' not in wanted
			or metadata.get('application') and 'LICENSE' not in wanted):
		raise ValueError('Application runtime receipt omits its required runtime closure')
	closure_roots = (stdlib, base + 'Vm', 'assets/localization' if role == 'core-sdk' else 'localization',
		'licenses/application', 'licenses/package-runtime')
	closure = {name[len(prefix):] for name in entries if name.startswith(prefix)
		and any(name[len(prefix):].startswith(directory + '/') for directory in closure_roots)}
	if identity[0] == 'windows':
		for name in entries:
			if not name.startswith(prefix + base):
				continue
			relative = name[len(prefix + base):]
			if ('/' not in relative and relative.casefold().endswith('.dll')
					and relative.casefold().startswith(('msvcp', 'vcruntime', 'concrt', 'vcomp'))
					or relative == 'licenses/MicrosoftVisualCpp/NOTICE.txt'):
				closure.add(base + relative)
	if not closure.issubset(wanted):
		raise ValueError('Application runtime receipt omits shipped runtime resources')
	if wanted[executable]['mode'] != '0755':
		raise ValueError('Application runtime receipt executable is not executable')
	if identity[0] != 'windows':
		for name, item in wanted.items():
			if bool(entries[prefix + name][1] & 0o111) != (item['mode'] == '0755'):
				raise ValueError('Application runtime receipt permission mismatch: ' + name)
	def check(name, size, stream):
		item = wanted.pop(name)
		if str(size) != item.get('bytes') or hashlib.file_digest(stream, 'sha256').hexdigest() != item.get('sha256'):
			raise ValueError('Application runtime receipt differs from shipped bytes: ' + name)
	if archive.name.endswith('.tar.gz'):
		with tarfile.open(archive, 'r:gz') as reader:
			for member in reader:
				name = member.name.removeprefix(prefix)
				if member.name.startswith(prefix) and name in wanted and member.isfile():
					with reader.extractfile(member) as stream:
						check(name, member.size, stream)
				if not wanted:
					break
	else:
		with zipfile.ZipFile(archive) as reader:
			for name in list(wanted):
				member = reader.getinfo(prefix + name)
				with reader.open(member) as stream:
					check(name, member.file_size, stream)
	if wanted:
		raise ValueError('Application runtime receipt lists missing payload files')


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("command", choices=("stage-sdk", "finalize-sdk", "stage-runtime"))
	parser.add_argument("--repo", type=Path)
	parser.add_argument("--build", type=Path)
	parser.add_argument("--config", default="Release")
	parser.add_argument('--core-version', help='Published Core SDK version for finalized CMake dependencies')
	parser.add_argument("--binaries", type=Path)
	parser.add_argument("--directory", required=True, type=Path)
	parser.add_argument("--executable-directory", default="")
	args = parser.parse_args()
	if args.command == "stage-sdk":
		if not args.build:
			parser.error("--build is required")
		stage_application_sdk(args.build.resolve(), args.directory.resolve(), args.config)
	elif args.command == "finalize-sdk":
		if not args.core_version:
			parser.error('finalize-sdk requires --core-version')
		finalize_application_sdk(args.directory.resolve(), args.core_version)
	else:
		if not args.repo or not args.binaries:
			parser.error("--repo and --binaries are required")
		stage_application_runtime(args.repo.resolve(), args.binaries.resolve(), args.directory.resolve(), args.executable_directory)
