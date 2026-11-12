#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Package the independent AsmJit backend without adding it to Runtime or SDK."""
from __future__ import annotations

import argparse
import ctypes
import json
import _ctypes
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from veroscript_paths import repository_root, module_root
from veroscript_release import (TARGETS, archive_tree, binary_prefix, core_public_abi, host_target, jit_identity, native_identity,
							   safe_output, scratch_directory, verify_windows_runtime, version)
from veroscript_business_release import load_versions, publish_component
from veroscript_products import ASMJIT_PACKAGE_NAME, business_route, verify_module_payload


def build_cache(build: Path) -> dict[str, str]:
	result = {}
	for line in (build / "CMakeCache.txt").read_text(encoding="utf-8-sig").splitlines():
		if not line.startswith(("#", "//")) and ":" in line and "=" in line:
			key, value = line.split("=", 1)
			result[key.split(":", 1)[0]] = value
	return result


def cmake_enabled(settings: dict[str, str], name: str, default: bool = False) -> bool:
	"""Read the standard boolean spellings accepted by our packaging entries."""
	value = settings.get(name)
	if value is None:
		return default
	value = value.strip().upper()
	if value in {'1', 'ON', 'TRUE', 'YES', 'Y'}:
		return True
	if value in {'', '0', 'OFF', 'FALSE', 'NO', 'N', 'IGNORE', 'NOTFOUND'} or value.endswith('-NOTFOUND'):
		return False
	raise ValueError(f'Invalid CMake boolean {name}: {value}')


def _core_version_code(headers: Path) -> int:
	declarations = re.findall(
		r"\bVEROSCRIPT_VERSION\s*=\s*\{\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\}\s*;",
		(headers / "VeroScriptVersion.h").read_text(encoding="utf-8-sig"))
	if len(declarations) != 1:
		raise ValueError("Missing or ambiguous Core product version declaration")
	major, minor, patch = map(int, declarations[0])
	if major > 0xff or minor > 0xff or patch > 0xffff:
		raise ValueError("Core product version exceeds its binary descriptor encoding")
	return (major << 24) | (minor << 16) | patch


def stage_backend(repo: Path, build: Path, destination: Path, target: str, package_version: str,
				  config: str = "Release") -> None:
	settings = build_cache(build)
	if Path(settings["CMAKE_HOME_DIRECTORY"]).resolve() != repo.resolve():
		raise ValueError("JIT build cache belongs to another source tree")
	hooks = settings.get("VEROSCRIPT_ENABLE_JIT_TEST_HOOKS", "OFF").upper()
	if hooks not in {"", "OFF", "0", "FALSE", "NO", "N", "IGNORE", "NOTFOUND"} and not hooks.endswith("-NOTFOUND"):
		raise ValueError("Production JIT packages require test hooks to be disabled")
	if settings.get("VS_VEROSCRIPT_ASMJIT_VERSION", "0.1.1") != package_version:
		raise ValueError("JIT build/package versions differ; rebuild with the requested backend version")
	headers = module_root(repo) / "VeroScript/Public"
	core_version = _core_version_code(headers)
	public_abi = core_public_abi(headers).encode("ascii")
	generated = build / "VeroScript/Generated/VeroScriptJitBuild.h"
	contracts = re.findall(rb'^#define VEROSCRIPT_JIT_PUBLIC_ABI "([0-9a-f]{64})"\r?$',
						   generated.read_bytes() if generated.is_file() else b"", re.MULTILINE)
	if contracts != [public_abi]:
		raise ValueError("Missing/stale JIT public ABI build header; configure and rebuild from current sources")
	cmake = settings.get("CMAKE_COMMAND", "cmake")
	subprocess.run([cmake, "--install", str(build), "--config", config,
					"--component", "VeroScriptAsmJitBackend", "--prefix", str(destination)], check=True)
	if target.startswith('macos-'):
		from veroscript_native_symbols import strip_macos_tree
		strip_macos_tree(destination, settings.get('CMAKE_STRIP', 'strip'))
	verify_module_payload(path.relative_to(destination).as_posix() for path in destination.rglob('*') if path.is_file())
	system, architecture = TARGETS[target]
	library = destination / ("VeroScriptJitAsmJit.dll" if system == "windows" else
							 "libVeroScriptJitAsmJit." + ("dylib" if system == "macos" else "so"))
	if not library.is_file() or native_identity(binary_prefix(library)) != (system, architecture):
		raise ValueError("Packaged JIT backend is missing or has the wrong platform/architecture")
	if system == "windows":
		entries = {path.relative_to(destination).as_posix(): (binary_prefix(path), 0)
				   for path in destination.rglob("*") if path.is_file()}
		verify_windows_runtime(entries, (system, architecture))
	if target == host_target():
		class Descriptor(ctypes.Structure):
			_fields_ = [("size", ctypes.c_uint32), ("core_version", ctypes.c_uint32),
						("abi", ctypes.c_char * 512), ("id", ctypes.c_char * 64),
						("version", ctypes.c_char * 32), ("create", ctypes.c_void_p),
						("destroy", ctypes.c_void_p)]
		backend = ctypes.CDLL(str(library))
		try:
			query = backend.veroscript_jit_query
			query.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
			query.restype = ctypes.c_uint32
			descriptor = Descriptor()
			if (not query(core_version, ctypes.sizeof(descriptor), ctypes.byref(descriptor))
					or descriptor.size != ctypes.sizeof(descriptor) or descriptor.core_version != core_version
					or not descriptor.create or not descriptor.destroy
					or descriptor.id != b"asmjit" or descriptor.version.decode("ascii") != package_version):
				raise ValueError("JIT binary metadata does not match its package version")
			if not descriptor.abi.startswith(public_abi + b":"):
				raise ValueError("JIT binary public ABI differs from current sources; rebuild the backend and matching CLI")
		finally:
			if os.name == "nt":
				_ctypes.FreeLibrary(backend._handle)
			else:
				_ctypes.dlclose(backend._handle)
		cli_name = "veroscript.exe" if system == "windows" else "veroscript"
		cli = next((p for p in (build / "bin" / config / cli_name, build / "bin" / cli_name) if p.is_file()), None)
		if not cli:
			raise ValueError("Build the matching CLI to verify the standalone backend package")
		identity = subprocess.check_output([cli, "--jit-backend=" + str(library), "--version"], text=True)
		if jit_identity(identity)["backend"] != "asmjit":
			raise ValueError("Packaged library does not initialize as the AsmJit backend")


def pack_backend(repo: Path, source: Path, archive: Path, tool: Path, target: str, package_version: str) -> None:
	"""Add an installable distribution without altering the portable product tree."""
	from veroscript_module_packages import shared_files
	from veroscript_tool_runner import output as tool_output, run as tool_run
	description = json.loads(tool_output(tool,'describe-jit',source/('VeroScriptJitAsmJit.dll' if target.startswith('win-') else
		'libVeroScriptJitAsmJit.'+('dylib' if target.startswith('macos-') else 'so'))))
	context = description['target']
	if description['id'] != 'asmjit' or (context['os'], context['arch']) != TARGETS[target]:
		raise ValueError('JIT package tool/backend target mismatch')
	with tempfile.TemporaryDirectory(prefix='veroscript-jit-installable-', dir=scratch_directory(repo)) as temporary:
		stage = Path(temporary) / 'package'
		shutil.copytree(source, stage)
		library = 'VeroScriptJitAsmJit.dll' if target.startswith('win-') else 'libVeroScriptJitAsmJit.' + (
			'dylib' if target.startswith('macos-') else 'so')
		owned = [{'id': f'runtime-{index}', 'path': p.relative_to(stage).as_posix(), 'linkage': 'dynamic'}
				 for index, p in enumerate(shared_files(stage)) if p != stage / library]
		manifest = {'format': 'veroscript-package', 'formatVersion': '0.1.1',
					'name': ASMJIT_PACKAGE_NAME, 'version': package_version,
					'description': 'Optional AsmJit execution backend.', 'licenseFile': 'LICENSE',
					'requires': {'core': context['core']},
					'target': {key: context[key] for key in ('os', 'arch', 'environment', 'coreBuild')},
					'entries': {'backends': [{'service': 'jit', 'id': 'asmjit', 'contract': 'jit',
											 'path': library, 'libraries': [p['id'] for p in owned]}],
								'libraries': owned}}
		manifest['target']['abis'] = {'jit': context['abis']['jit']}
		(stage / 'veroscript-package.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
		tool_run(tool,'command','pack',stage,'--output='+str(archive))


def verify_installable_backend(entries, package_version: str, target: str) -> None:
	from veroscript_backend_packages import verify_backend_entries
	verify_backend_entries(entries, ASMJIT_PACKAGE_NAME, 'jit', 'asmjit', package_version, target)
	for notice in ('licenses/asmjit/LICENSE.md', 'licenses/asmjit/VERSION.txt'):
		if not entries.get(notice, (b'', 0))[0]:
			raise ValueError('Missing installable JIT third-party notice: ' + notice)


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo-root", type=Path, default=repository_root())
	parser.add_argument("--build-directory", type=Path, required=True)
	parser.add_argument("--target", choices=tuple(TARGETS))
	parser.add_argument("--version", type=version)
	parser.add_argument("--out-root", type=Path)
	parser.add_argument("--package-tool", type=Path, help="Matching VeroScriptModulePackageTool (required for .vspkg)")
	parser.add_argument("--portable-only", action="store_true", help="Only produce the portable archive (no package client required)")
	args = parser.parse_args()
	repo = args.repo_root.resolve()
	build = (repo / args.build_directory).resolve()
	target = args.target or host_target()
	package_version = args.version or load_versions(repo)["businesses"]["Jit"]
	from veroscript_module_packages import package_tool
	tool = None if args.portable_only else package_tool({'Jit': build}, args.package_tool)
	name = f"{ASMJIT_PACKAGE_NAME}-{package_version}-{target}"
	suffix = ".zip" if target.startswith("win-") else ".tar.gz"
	output = safe_output(repo, repo / args.out_root, component=True) if args.out_root else None
	with tempfile.TemporaryDirectory(prefix="veroscript-jit-", dir=scratch_directory(repo)) as temporary:
		stage = Path(temporary)
		package = stage / name
		stage_backend(repo, build, package, target, package_version)
		archive = stage / (name + suffix)
		archive_tree(package, archive)
		installable = stage / (name + '.vspkg')
		if tool:
			pack_backend(repo, package, installable, tool, target, package_version)
		from veroscript_module_symbols import assemble_module_symbols
		assemble_module_symbols(repo, {'Jit': build}, stage, {'Jit': package_version}, target, ['Jit'])
		if output is None:
			publish_component(repo, stage, module_root(repo) / "Release", "Jit", package_version, target)
		else:
			output.mkdir(parents=True, exist_ok=True)
			expected = {name, name + suffix, 'symbols'}
			if tool:
				expected.add(name + '.vspkg')
			if any(p.name not in expected for p in output.iterdir()):
				raise ValueError("JIT output contains unrelated files")
			if (output / name).exists():
				raise ValueError("Choose an empty output directory for the unpacked JIT package")
			shutil.copytree(package, output / name)
			shutil.copy2(archive, output / archive.name)
			if (stage / 'symbols').is_dir():
				shutil.copytree(stage / 'symbols', output / 'symbols')
			if tool:
				shutil.copy2(installable, output / installable.name)
		print(f"JIT package: {output or module_root(repo) / 'Release' / business_route('Jit') / target / package_version}")


if __name__ == "__main__":
	main()
