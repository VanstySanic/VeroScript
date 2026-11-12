# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Package-client assets and verified component identity for native releases."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess


_CURL_AUXILIARY_EXPORTS = ('bin/curl-config', 'lib/pkgconfig/libcurl.pc')


def _finalize_package_manager_dependency_exports(stage: Path) -> None:
	"""Keep the declared CMake curl API portable; omit configure-prefix utilities."""
	from veroscript_cmake_exports import normalize_curl_exports
	# These optional curl utilities report their original configure prefix and
	# compiler. CoreSDK consumers use find_dependency(CURL)/CURL imported targets.
	for relative in _CURL_AUXILIARY_EXPORTS:
		(stage / relative).unlink(missing_ok=True)
	normalize_curl_exports(stage)


def stage_package_client(repo: Path, cli: Path, root: Path, role: str, *, target_context: dict | None = None) -> None:
	if role not in {"runtime", "core-sdk"}:
		raise ValueError(f"Unsupported package-client component role: {role}")
	if target_context is None:
		# A disabled optional client has no install commands and needs no descriptor.
		probe = subprocess.run([str(cli), "env", "--json", f"--path={root}"],
							   capture_output=True, text=True, encoding="utf-8")
		if probe.returncode:
			if "package management was not built" in probe.stderr:
				return
			raise ValueError(f"Cannot validate package client: {probe.stderr.strip()}")
		target = json.loads(probe.stdout)["target"]
	else:
		from veroscript_release import binary_prefix, native_identity
		if (not isinstance(target_context,dict) or not target_context.get('coreBuild')
			or not target_context.get('abis') or not target_context.get('environment')
			or (target_context.get('os'),target_context.get('arch')) != native_identity(binary_prefix(cli))):
			raise ValueError('Explicit package-client target context differs from its compiled executable')
		target = target_context
	descriptor = {"format": "veroscript-component", "formatVersion": "0.1.1",
				  "role": role, "root": ".." if role == "core-sdk" else ".", "target": target}
	(cli.parent / "veroscript-component.json").write_text(
		json.dumps(descriptor, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
	if target_context is None:
		verify = subprocess.run([str(cli), "env", "--root", "--path", str(root.resolve()), "--json"],
								capture_output=True, text=True, encoding="utf-8")
		if verify.returncode:
			raise ValueError(f"Cannot validate package component root: {verify.stderr.strip()}")
		selected = json.loads(verify.stdout)
		if selected["scope"] != ("sdk" if role == "core-sdk" else "runtime") or not Path(selected["root"]).samefile(root):
			raise ValueError("Package-client descriptor does not select its release component")
	source = repo / "VeroScriptPackageManager/ThirdParty"
	for relative in ("json/LICENSE.MIT", "miniz/LICENSE", "sha256/LICENSE", "curl/COPYING", "sources.json"):
		dest = root / "licenses/package-manager" / relative
		dest.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(source / relative, dest)


def stage_package_manager_sdk(build: Path, stage: Path, config: str = "Release") -> None:
	from veroscript_applications import stage_application_sdk
	from veroscript_jit import build_cache, cmake_enabled
	stage_application_sdk(build, stage, config)
	if not cmake_enabled(build_cache(build), 'VEROSCRIPT_BUILD_PACKAGE_MANAGER'):
		return
	subprocess.run(["cmake", "--install", str(build), "--prefix", str(stage),
					"--config", config, "--component", "VeroScriptPackageManager"], check=True)
	_finalize_package_manager_dependency_exports(stage)


def finalize_package_manager_sdk(stage: Path, core_version: str) -> None:
	"""Bind installed exports to the published Core version and library bytes."""
	from veroscript_applications import finalize_application_sdk
	finalize_application_sdk(stage, core_version)
	directory = stage / 'lib/cmake/VeroScriptPackageManager'
	config = directory / 'VeroScriptPackageManagerConfig.cmake'
	if not config.is_file():
		return
	if not re.fullmatch(r'\d+\.\d+\.\d+', core_version):
		raise ValueError('Invalid Core SDK package version')
	libraries = [path for name in ('VeroScript.lib', 'libVeroScript.a')
				 if (path := stage / 'lib' / name).is_file()]
	identity = directory / 'VeroScriptPackageManagerCoreBuild.cmake'
	if len(libraries) != 1 or not identity.is_file():
		raise ValueError('Package manager SDK requires its matching staged Core library and build identity')
	text, count = re.subn(r'find_dependency\(VeroScript\s+\d+\.\d+\.\d+\s+EXACT\s+CONFIG\)',
						 f'find_dependency(VeroScript {core_version} EXACT CONFIG)', config.read_text(encoding='utf-8-sig'))
	if count != 1:
		raise ValueError('Invalid package manager Core SDK dependency contract')
	# Windows' producer invokes this CLI after its own CMake component install;
	# perform the same finalization as the Python native/cross producers.
	_finalize_package_manager_dependency_exports(stage)
	with libraries[0].open('rb') as stream:
		build_id = hashlib.file_digest(stream, 'sha256').hexdigest()
	config.write_text(text, encoding='utf-8')
	identity.write_text(f'set(VeroScriptPackageManager_CORE_BUILD_ID "{build_id}")\n', encoding='utf-8')


def verify_package_client_payload(entries: dict, prefix: str, role: str, *,
								  core_version: str | None = None, build_id: str | None = None) -> None:
	"""Audit optional package-client descriptors and their required SDK exports."""
	if role == 'core-sdk':
		from veroscript_cmake_exports import verify_curl_export_entries
		if any(prefix + name in entries for name in _CURL_AUXILIARY_EXPORTS):
			raise ValueError('CoreSDK must omit configure-prefix curl helper exports')
		verify_curl_export_entries({name: entry for name, entry in entries.items() if name.startswith(prefix)})
	descriptor = prefix + ('bin/' if role == 'core-sdk' else '') + 'veroscript-component.json'
	metadata = prefix + ('sdk-manifest.json' if role == 'core-sdk' else 'runtime-manifest.json')
	if metadata in entries:
		declared = json.loads(entries[metadata][0]).get('packageManager')
		if declared is not None and (type(declared) is not bool or declared != (descriptor in entries)):
			raise ValueError('Package-client deployment differs from the declared package features')
	if descriptor not in entries:
		return
	value = json.loads(entries[descriptor][0])
	if (not isinstance(value, dict) or value.get('format') != 'veroscript-component' or value.get('formatVersion') != '0.1.1'
			or value.get('role') != role or value.get('root') != ('..' if role == 'core-sdk' else '.')
			or not value.get('target')):
		raise ValueError('Invalid package-client component descriptor')
	required = ['licenses/package-manager/' + name for name in
				('json/LICENSE.MIT', 'miniz/LICENSE', 'sha256/LICENSE', 'curl/COPYING', 'sources.json')]
	if role == 'core-sdk':
		cmake = 'lib/cmake/VeroScriptPackageManager/'
		required += ['include/PackageManager/VeroScriptPackageManager.h',
					 cmake + 'VeroScriptPackageManagerConfig.cmake',
					 cmake + 'VeroScriptPackageManagerConfigVersion.cmake',
					 cmake + 'VeroScriptPackageManagerTargets.cmake',
					 cmake + 'VeroScriptPackageManagerCoreBuild.cmake']
		windows = prefix + 'lib/VeroScript.lib' in entries
		required += ['lib/' + (name + '.lib' if windows else 'lib' + name + '.a')
					 for name in ('VeroScriptPackageManager', 'VeroScriptPackageZip')]
	for relative in required:
		if not entries.get(prefix + relative, (b'', 0))[0].strip():
			raise ValueError(f'Missing package-client payload: {relative}')
	if role == 'core-sdk':
		config = entries[prefix + cmake + 'VeroScriptPackageManagerConfig.cmake'][0].decode('utf-8-sig')
		dependency = re.findall(r'find_dependency\(VeroScript\s+(\d+\.\d+\.\d+)\s+EXACT\s+CONFIG\)', config)
		identity = entries[prefix + cmake + 'VeroScriptPackageManagerCoreBuild.cmake'][0].decode('utf-8-sig')
		hashes = re.findall(r'set\(VeroScriptPackageManager_CORE_BUILD_ID "([0-9a-f]{64})"\)', identity)
		if dependency != [core_version] or hashes != [build_id]:
			raise ValueError('Package manager SDK Core version/build identity mismatch')


if __name__ == '__main__':
	import argparse
	from veroscript_paths import repository_root
	from veroscript_release import safe_output
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('command', choices=('finalize-sdk',))
	parser.add_argument('--directory', type=Path, required=True)
	parser.add_argument('--version', required=True)
	args = parser.parse_args()
	finalize_package_manager_sdk(safe_output(repository_root(), args.directory, component=True), args.version)
