# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

from importlib import metadata
import json
from pathlib import Path
import re


def discover_version() -> str:
	pattern = re.compile(
		r"VEROSCRIPT_VERSION\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}"
	)
	for parent in Path(__file__).resolve().parents:
		header = parent / "VeroScript" / "Public" / "VeroScriptVersion.h"
		if not header.is_file():
			continue
		try:
			text = header.read_text(encoding="utf-8")
		except (OSError, UnicodeError):
			break
		match = pattern.search(text)
		if match is not None:
			return ".".join(match.groups())
	try:
		stamp = json.loads(Path(__file__).with_name("_version.json").read_text(encoding="utf-8"))
	except FileNotFoundError:
		pass
	else:
		packaged = stamp.get("version") if isinstance(stamp, dict) else None
		if not isinstance(packaged, str) or not re.fullmatch(r"\d+\.\d+\.\d+", packaged):
			raise ValueError("Invalid packaged VeroScript Python tool version")
		return packaged
	try:
		return metadata.version("veroscript")
	except metadata.PackageNotFoundError:
		pass
	return "0+unknown"


__all__ = ["discover_version"]
