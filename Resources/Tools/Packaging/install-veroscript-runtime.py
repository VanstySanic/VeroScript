#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Install a local native Runtime through the managed installer engine."""
import argparse
import os
from pathlib import Path
from veroscript_paths import module_root, repository_root, module_route, source_route
import re
import shutil
import subprocess
import sys
import tempfile

from veroscript_release import (TARGETS, binary_prefix, copy_tree, host_target,
							   linked_path, native_identity, stage_localization)
from veroscript_packages import stage_package_client
from veroscript_applications import stage_application_runtime
from veroscript_vm import stage_runtime_from_binaries
from veroscript_icons import stage_runtime_icons


def default_install_root(system: str, home: Path, environment: dict) -> Path:
	if system == "windows":
		local = environment.get("LOCALAPPDATA")
		if not local or not Path(local).is_absolute():
			raise ValueError("LOCALAPPDATA must identify an absolute user directory")
		return Path(local) / "Programs/VeroScript"
	if system == "macos":
		return home / "Applications/VeroScript"
	configured = environment.get("XDG_DATA_HOME", "")
	data = Path(configured) if configured and Path(configured).is_absolute() else home / ".local/share"
	return data / "VeroScript"


def runtime_resources(repo: Path, source: Path) -> dict[str, Path]:
	roots = {"stdlib": module_root(repo) / "VeroScript/Resources/Scripts/stdlib"}
	if (source / "localization").is_dir():
		roots["localization"] = source / "localization"
	return {name: source / name if (source / name).is_dir() else fallback
			for name, fallback in roots.items()}


def require_unlinked_path(path: Path) -> None:
	for current in (path, *path.parents):
		if linked_path(current):
			raise ValueError(f"Linked installation path is forbidden: {current}")


def validate_install_root(repo: Path, source: Path, root: Path, home: Path, runtime_version: str = "0.1.1") -> Path:
	repo, source, home = repo.resolve(), source.resolve(), home.resolve()
	root = Path(os.path.abspath(root))
	require_unlinked_path(root)
	if root == Path(root.anchor) or root == home or repo.is_relative_to(root):
		raise ValueError(f"Refusing a filesystem, user or repository root: {root}")
	if root.is_relative_to(source) or source.is_relative_to(root):
		raise ValueError(f"Installation cannot overlap its source: {root}")
	if root.is_relative_to(repo):
		relative = root.relative_to(repo)
		collections = (Path(".tmp"), Path("dist"), (module_root(repo) / 'Release').relative_to(repo))
		if not any(relative.is_relative_to(collection) and relative != collection for collection in collections):
			raise ValueError(f"Installation cannot modify repository sources: {root}")
	for current in (root, *root.parents):
		if (current / "release-manifest.json").is_file():
			raise ValueError(f"Installation cannot modify a complete release: {root}")
		if current.exists() and not current.is_dir():
			raise ValueError(f"Installation has a file ancestor: {current}")
	destination = root / "Runtime" / runtime_version
	require_unlinked_path(destination)
	if destination.exists() and any(destination.iterdir()) and not (destination / ".veroscript-install.json").is_file():
		raise ValueError(f"Refusing an unmanaged Runtime directory: {root}. Choose an empty directory or use the official installer.")
	return root


def run(*arguments, env=None, capture=False):
	result = subprocess.run([str(arg) for arg in arguments], check=True, env=env,
							text=True, stdout=subprocess.PIPE if capture else None)
	return result.stdout if capture else None


def main():
	if sys.version_info < (3, 11):
		raise ValueError("Local installation requires Python 3.11+")
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo-root", required=True, type=Path)
	parser.add_argument("--source-dir", required=True, type=Path)
	parser.add_argument("--install-dir", type=Path, help="Product root; executables are installed in Runtime/<version>/")
	parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
	parser.add_argument("--add-to-path", action="store_true", help="Explicitly add Runtime/<version>/ to the user PATH")
	args = parser.parse_args()
	repo, source = args.repo_root.resolve(), args.source_dir.resolve()
	target = host_target()
	system, architecture = TARGETS[target]
	home = Path.home()
	suffix = ".exe" if system == "windows" else ""
	tools = [source / (name + suffix) for name in ("veroscript", "veroscript-dap", "veroscript-lsp")]
	for tool in tools:
		if not tool.is_file() or native_identity(binary_prefix(tool)) != (system, architecture):
			raise ValueError(f"Missing or incompatible native Runtime tool: {tool}")
		if os.name != "nt" and not os.access(tool, os.X_OK):
			raise ValueError(f"Runtime tool is not executable: {tool}")
	identity = run(tools[0], "--version", capture=True)
	match = re.match(r"VeroScript ([0-9]+\.[0-9]+\.[0-9]+) ", identity)
	if not match or f"[{target};" not in identity:
		raise ValueError(f"Runtime version/host identity mismatch: {identity.strip()}")
	root = validate_install_root(repo, source,
		args.install_dir or default_install_root(system, home, os.environ), home, match[1])
	resources = runtime_resources(repo, source)
	if any(not directory.is_dir() for directory in resources.values()):
		raise ValueError("Runtime stdlib or localization is missing")
	project = module_root(repo) / "VeroScriptInstaller/Tool/VeroScriptInstaller.Tool.csproj"
	if not project.is_file() or shutil.which("dotnet") is None:
		raise ValueError("Local installation requires the installer Tool source and .NET 9 SDK (or a compatible newer SDK)")
	scratch = repo / ".tmp"
	require_unlinked_path(scratch)
	scratch.mkdir(exist_ok=True)
	env = os.environ.copy()
	env.setdefault("DOTNET_ROLL_FORWARD", "Major")
	with tempfile.TemporaryDirectory(prefix="veroscript-runtime-install-", dir=scratch) as temporary:
		stage = Path(temporary)
		if stage.resolve().parent != scratch.resolve():
			raise ValueError("Installation staging escaped its workspace")
		runtime = stage / "source"
		runtime.mkdir()
		for tool in tools:
			shutil.copy2(tool, runtime / tool.name)
		for name, directory in resources.items():
			copy_tree(directory, runtime / name)
		if "localization" not in resources:
			stage_localization(repo, runtime / "localization")
		stage_runtime_from_binaries(source, runtime)
		stage_runtime_icons(repo, runtime, ('win' if system == 'windows' else system) + '-' + architecture)
		stage_application_runtime(module_root(repo), source, runtime)
		stage_package_client(repo, runtime / tools[0].name, runtime, "runtime")
		bundle = stage / "bundle"
		command = ("dotnet", "run", "--project", project, "-c", "Release", "--")
		run(*command, "build-bundle", "--output", bundle, "--version", match[1],
			"--profile", "runtime", "--platform", system, "--architecture", architecture,
			"--source-runtime", runtime, env=env)
		install = [*command, "install", "--bundle", bundle, "--root", root, "--components", "runtime"]
		if args.add_to_path:
			install.append("--add-to-path")
		validate_install_root(repo, source, root, home, match[1])
		run(*install, env=env)
	print(f"Installed Runtime: {root / 'Runtime' / match[1]}")
	print(f"Installation record: {root / 'Runtime' / match[1] / '.veroscript-install.json'}")


if __name__ == "__main__":
	try:
		main()
	except (OSError, ValueError, subprocess.CalledProcessError) as error:
		raise SystemExit(f"veroscript-install: {error}")
