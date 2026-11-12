# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import os
from os import PathLike
from pathlib import Path
import shutil
import sys


def _usable_file(path: Path) -> str | None:
	try:
		if not path.is_file():
			return None
		if os.name != "nt" and not os.access(path, os.X_OK):
			return None
		return str(path.resolve())
	except (OSError, RuntimeError):
		return None


def _from_directory(directory: Path) -> str | None:
	names = ("veroscript.exe", "veroscript") if sys.platform == "win32" else (
		"veroscript",
		"veroscript.exe",
	)
	for name in names:
		for candidate in (directory / name, directory / "bin" / name):
			resolved = _usable_file(candidate)
			if resolved is not None:
				return resolved
	return None


def _resolve_configured(value: str | PathLike[str]) -> str | None:
	text = os.fspath(value).strip()
	if not text:
		return None
	try:
		path = Path(text).expanduser()
		if path.is_dir():
			return _from_directory(path)
		resolved = _usable_file(path)
		if resolved is not None:
			return resolved
	except (OSError, RuntimeError):
		return None
	discovered = shutil.which(text)
	return _usable_file(Path(discovered)) if discovered else None


def find_veroscript_executable(
	configured: str | PathLike[str] | None = None,
) -> str | None:
	"""Find a user-provided VeroScript CLI without bundling the toolchain.

    An explicit executable, SDK root, or SDK ``bin`` directory is authoritative.
    Otherwise discovery checks ``VEROSCRIPT_EXECUTABLE``, ``VEROSCRIPT_SDK``,
    and finally ``PATH`` in that order.
    """

	if configured is not None:
		return _resolve_configured(configured)
	executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
	if executable:
		return _resolve_configured(executable)
	sdk = os.environ.get("VEROSCRIPT_SDK")
	if sdk:
		try:
			return _from_directory(Path(sdk).expanduser())
		except (OSError, RuntimeError):
			return None
	discovered = shutil.which("veroscript")
	return _usable_file(Path(discovered)) if discovered else None


__all__ = ["find_veroscript_executable"]
