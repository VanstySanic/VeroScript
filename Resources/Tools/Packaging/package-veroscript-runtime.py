#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Stage a standalone native Unix Runtime without modifying a full release."""
from pathlib import Path
from veroscript_paths import module_root, repository_root, module_route, source_route
import argparse
import json
import shutil
import subprocess
import tempfile
import uuid

from veroscript_release import archive_entries, archive_tree, binary_prefix, copy_tree, host_target, jit_identity, native_identity, safe_output, scratch_directory, stage_localization, version
from veroscript_business_release import load_versions, publish_component
from veroscript_packages import stage_package_client
from veroscript_applications import stage_application_runtime
from veroscript_vm import stage_runtime_from_binaries
from veroscript_products import clean_functional_payload
from veroscript_icons import stage_runtime_icons


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo-root", type=Path, required=True)
	parser.add_argument("--bin-directory", type=Path, required=True)
	parser.add_argument("--interop-library", type=Path, required=True)
	parser.add_argument("--version", type=version, required=True)
	parser.add_argument("--out-root", type=Path)
	parser.add_argument("--no-examples", action="store_true")
	args = parser.parse_args()
	repo = args.repo_root.resolve()
	target = host_target()
	if target.startswith("win-"):
		raise ValueError("Use the PowerShell Runtime entry point on Windows")
	output = safe_output(repo, repo / args.out_root, component=True) if args.out_root else module_root(repo) / 'Release' / 'Runtime' / target / args.version
	name = f"VeroScriptRuntime-{args.version}-{target}"
	if args.out_root and output.exists() and any(p.name not in {name, name + ".tar.gz", name + ".zip"} for p in output.iterdir()):
		raise ValueError("Runtime output contains unrelated files")
	identity = tuple(target.split("-"))
	for binary in [args.bin_directory / n for n in ("veroscript", "veroscript-lsp", "veroscript-dap")] + [args.interop_library]:
		if native_identity(binary_prefix(binary)) != identity:
			raise ValueError(f"Runtime binary is not for the native host: {binary}")
	actual = subprocess.check_output([args.bin_directory / "veroscript", "--version"], text=True)
	if not actual.startswith(f"VeroScript {load_versions(repo)['nativeVersion']} "):
		raise ValueError("Runtime version mismatch")
	jit = jit_identity(actual)
	scratch = scratch_directory(repo)
	with tempfile.TemporaryDirectory(prefix="veroscript-runtime-stage-", dir=scratch) as temporary:
		stage = Path(temporary) / "output"
		package = stage / name
		package.mkdir(parents=True)
		for tool in ("veroscript", "veroscript-lsp", "veroscript-dap"):
			shutil.copy2(args.bin_directory / tool, package / tool)
		(package / "lib").mkdir()
		shutil.copy2(args.interop_library, package / "lib" / args.interop_library.name)
		copy_tree(module_root(repo) / "VeroScriptInterop/Public", package / "include/VeroScript")
		for tree in ("stdlib",):
			copy_tree(module_root(repo) / "VeroScript/Resources/Scripts" / tree, package / tree)
		stage_localization(repo, package / "localization")
		stage_runtime_icons(repo, package, target)
		shutil.copy2(repo / "LICENSE", package / "LICENSE")
		if jit["backend"] == "asmjit":
			(package / "licenses/asmjit").mkdir(parents=True)
			shutil.copy2(module_root(repo) / "Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/VERSION.txt", package / "licenses/asmjit/VERSION.txt")
			shutil.copy2(module_root(repo) / "Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md", package / "licenses/asmjit/LICENSE.md")
		(package / "runtime-manifest.json").write_text(json.dumps({"schemaVersion": 1, "product": "VeroScript Runtime", "version": args.version, "platform": identity[0], "architecture": identity[1], "includesExamples": False, "jit": jit}, indent=2) + "\n", encoding="utf-8")
		stage_runtime_from_binaries(args.bin_directory, package)
		stage_package_client(repo, package / "veroscript", package, "runtime")
		stage_application_runtime(module_root(repo), args.bin_directory, package)
		archive = stage / (name + ".tar.gz")
		clean_functional_payload(package)
		archive_tree(package, archive)
		archive_entries(archive, max_entry_bytes=1024 * 1024)
		if not args.out_root:
			publish_component(repo, stage, module_root(repo) / 'Release', 'Runtime', args.version, target)
			print(f'Runtime archive: {output / archive.name}')
			return
		output.parent.mkdir(parents=True, exist_ok=True)
		backup = scratch / ("veroscript-runtime-backup-" + uuid.uuid4().hex)
		moved = output.exists()
		if moved:
			output.rename(backup)
		try:
			stage.rename(output)
		except BaseException:
			if moved:
				backup.rename(output)
			raise
		if moved:
			try:
				shutil.rmtree(backup)
			except OSError as error:
				raise RuntimeError(f"Runtime package published, but obsolete output cleanup failed: {error}") from error
	print(f"Runtime directory: {output / name}")
	print(f"Runtime archive: {output / (name + '.tar.gz')}")


if __name__ == "__main__":
	main()
