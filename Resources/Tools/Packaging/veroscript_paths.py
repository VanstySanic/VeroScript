# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Locate the source module independently of its outer directory name."""
from __future__ import annotations

import json
from pathlib import Path


def is_module_catalog(path: Path) -> bool:
	# Windows also matches the Valence resources/docs spelling. Its public
	# documentation.json identifies a consumer rather than a VeroScript module.
	return path.is_file() and json.loads(path.read_text(encoding='utf-8-sig')).get('product') != 'ValenceStudio'


def module_root(repo: Path | None = None) -> Path:
	"""Use tool ownership, or discover one module in an explicitly supplied repo."""
	owned = Path(__file__).resolve().parents[3]
	if repo is None:
		return owned
	repo = repo.resolve()
	if owned.is_relative_to(repo):
		return owned
	candidates = [path for path in (repo, *repo.iterdir()) if path.is_dir()
				  and ((path / 'VeroScript/Public/VeroScriptVersion.h').is_file()
					   or is_module_catalog(path / 'Resources/Docs/documentation.json'))]
	if not candidates:
		candidates = [path for path in repo.iterdir() if path.is_dir()
					  and any((path / route).is_dir() for route in ('VeroScript/Resources', 'Resources/Assets', 'Release'))]
	if len(candidates) > 1:
		raise ValueError(f'Ambiguous VeroScript module roots: {candidates}')
	if candidates:
		return candidates[0]
	# Isolated contract fixtures may contain only staged products. Retain the
	# owning module's name for their prospective output, without a fixed name.
	return repo / owned.name


def repository_root() -> Path:
	"""Find shared build infrastructure above the module; support isolated tools."""
	root = module_root()
	for parent in (root, *root.parents):
		helper = parent / 'Resources/Tools/CMake/VeroScriptHelpers.cmake'
		if helper.is_file() and (parent / 'CMakeLists.txt').is_file():
			return parent
	return root.parent


def module_route(repo: Path, suffix: str = '') -> str:
	"""Return a repository-relative source route for catalogs and inventories."""
	root = module_root(repo).relative_to(repo.resolve())
	return (root / suffix).as_posix()


def source_route(repo: Path, route: str) -> str:
	"""Expand module catalog routes, including the previous catalog spelling."""
	for prefix in ('$module/', 'Script/'):
		if route.startswith(prefix):
			return module_route(repo, route[len(prefix):])
	return route
