# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import locale
import os
import re
import string
from pathlib import Path
from typing import Mapping


_ENGLISH: dict[str, str] = {
	"python_tools.python_version_required": "VeroScript Python tools require Python 3.11-3.15",
	"python_tools.source_read_failed": "failed to read Python source: {0}",
	"python_tools.source_write_failed": "failed to write generated output: {0}",
	"python_tools.syntax_error": "invalid Python syntax: {0}",
	"python_tools.duplicate_declaration": "duplicate declaration: {0}",
	"python_tools.unsupported_top_level": "unsupported top-level Python construct: {0}",
	"python_tools.missing_annotation": "a type annotation is required for {0}",
	"python_tools.unsupported_type": "unsupported Python type annotation: {0}",
	"python_tools.unsupported_parameter": "unsupported Python parameter form: {0}",
	"python_tools.unsupported_decorator": "unsupported Python decorator: {0}",
	"python_tools.unsupported_inheritance": "unsupported Python inheritance: {0}",
	"python_tools.unsupported_construct": "unsupported Python construct: {0}",
	"python_tools.enum_value_required": "enum member {0} must have a statically evaluable integer value",
	"python_tools.catalog_validation_failed": "generated binding catalog validation failed: {0}",
	"python_tools.validator_not_found": "VeroScript tooling requires --veroscript, VEROSCRIPT_EXECUTABLE, VEROSCRIPT_SDK, or veroscript on PATH",
	"python_tools.generated_validation_failed": "generated VeroScript validation failed: {0}",
	"python_tools.aot_failed": "Python AOT generation failed: {0}",
	"python_tools.aot_output_missing": "Python AOT reported success but produced no valid output: {0}",
	"python_tools.aot_manifest_invalid": "generated Python AOT manifest is invalid: {0}",
	"python_tools.timeout_invalid": "timeout must be a finite positive number",
}


def _canonical_language(tag: str) -> str:
	normalized = tag.strip().split(".", 1)[0].split("@", 1)[0].replace("_", "-")
	if normalized.lower() in {"c", "posix"}:
		return "en-US"
	parts = normalized.split("-")
	if not re.fullmatch(r"[A-Za-z]{2,8}", parts[0]) or any(
		not re.fullmatch(r"[A-Za-z0-9]+", part) for part in parts[1:]
	):
		return "en-US"
	canonical = [parts[0].lower()]
	for part in parts[1:]:
		canonical.append(part.title() if len(part) == 4 and part.isalpha()
						 else part.upper() if len(part) == 2 and part.isalpha()
						 else part.lower())
	return "-".join(canonical)


def _chinese_script(tag: str) -> str | None:
	inferred = None
	for part in tag.split("-")[1:]:
		if len(part) == 1:  # Extension and private-use subtags are not script/region.
			break
		if part in {"Hant", "Hans"}:
			return part  # An explicit script takes precedence over the region.
		if inferred is None:
			if part in {"TW", "HK", "MO"}:
				inferred = "Hant"
			elif part in {"CN", "SG"}:
				inferred = "Hans"
	return inferred


def _resolve_language(requested: str, available: set[str]) -> str:
	"""Match the core's exact, parent, then same-language catalog policy."""
	parent = requested
	while True:
		if parent in available:
			return parent
		if "-" not in parent:
			break
		parent = parent.rsplit("-", 1)[0]
	primary = requested.partition("-")[0]
	script = _chinese_script(requested) if primary == "zh" else None
	candidates = [tag for tag in available if tag.partition("-")[0] == primary
				  and (script is None or _chinese_script(tag) == script)]
	candidates.sort(key=lambda tag: (
		-(requested.startswith(tag) or tag.startswith(requested)), tag))
	return candidates[0] if candidates else "en-US"


def _catalog_roots(explicit: str | os.PathLike[str] | None) -> tuple[Path, ...]:
	if explicit:
		candidate = Path(explicit).expanduser().resolve()
		return (candidate,) if candidate.is_dir() else ()
	configured = os.environ.get("VERO_LOCALIZATION_DIR", "")
	if configured:
		candidate = Path(configured).expanduser().resolve()
		if candidate.is_dir():
			return (candidate,)
	for parent in Path(__file__).resolve().parents:
		candidate = parent / "localization"
		if candidate.is_dir() and (candidate / "en-US").is_dir():
			return (candidate,)
		candidate = parent / "veroscript" / "localization"
		if candidate.is_dir() and (candidate / "en-US").is_dir():
			return (candidate,)
		# In a checkout, each Python tool owns its catalogs. Only messages used
		# by several tools live in the Python area's shared resource directory.
		python_root = parent / "Extensions" / "Language" / "Python"
		if parent.name == "Python":
			python_root = parent
		shared = python_root / "Resources" / "Localization"
		if (shared / "en-US").is_dir():
			return (shared, *(
				python_root / f"VeroScriptPython{module}" / "Resources" / "Localization"
				for module in ("Frontend", "BindGen", "Transpiler", "Aot")
			))
	return ()


def _read_catalog(path: Path) -> dict[str, str] | None:
	def unique_entries(pairs: list[tuple[str, object]]) -> dict[str, object]:
		entries: dict[str, object] = {}
		for key, value in pairs:
			if key in entries:
				raise ValueError(f"Duplicate localization message: {key}")
			entries[key] = value
		return entries

	try:
		value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_entries)
		if not isinstance(value, dict) or any(
			not isinstance(message, str) or not message.strip() for message in value.values()
		):
			return None
		for key, message in value.items():
			key.encode("utf-8")
			message.encode("utf-8")
		return value
	except (OSError, UnicodeError, ValueError, RecursionError):
		return None


def _read_culture(roots: tuple[Path, ...], language: str) -> dict[str, str] | None:
	messages: dict[str, str] = {}
	found = False
	for root in roots:
		for path in sorted((root / language).rglob("*.json")):
			entries = _read_catalog(path)
			if entries is None:
				return None
			found = True
			for key, value in entries.items():
				if key.startswith("python_tools."):
					if key in messages:
						return None
					messages[key] = value
	return messages if found else None


def _format_arguments(template: str, depth: int = 0) -> frozenset[int] | None:
	"""Validate positional fields without formatting caller-provided objects."""
	arguments: set[int] = set()
	if depth > 1:
		return None
	try:
		for _, field, specification, conversion in string.Formatter().parse(template):
			if field is None:
				continue
			if not re.fullmatch(r"[0-9]+", field) or conversion not in (None, "s", "r", "a"):
				return None
			arguments.add(int(field))
			if specification:
				nested = _format_arguments(specification, depth + 1)
				if nested is None:
					return None
				arguments.update(nested)
	except ValueError:
		return None
	return frozenset(arguments)


class Localizer:
	"""Small catalog reader with authoritative English fallback."""

	def __init__(
		self,
		language: str | None = None,
		catalog_directory: str | os.PathLike[str] | None = None,
	) -> None:
		requested_language = language or os.environ.get("VERO_LANG", "")
		if not requested_language:
			try:
				requested_language = locale.getlocale()[0] or "en-US"
			except ValueError:
				requested_language = "en-US"
		requested = _canonical_language(requested_language)
		self.language = "en-US"
		self._english: dict[str, str] = dict(_ENGLISH)
		self._messages: dict[str, str] = dict(self._english)
		roots = _catalog_roots(catalog_directory)
		if not roots:
			return
		english = _read_culture(roots, "en-US") or {}
		self._english.update({key: value for key, value in english.items()
							  if _format_arguments(value) is not None
							  and (key not in _ENGLISH
								   or _format_arguments(value) == _format_arguments(_ENGLISH[key]))})
		self._messages = dict(self._english)
		candidates = {path.name for root in roots if root.is_dir()
					  for path in root.iterdir() if path.is_dir()
					  and "-" in path.name and _canonical_language(path.name) == path.name}
		# Read only candidates needed for this request; corrupt cultures must
		# not become selected merely because a JSON filename is present.
		while candidates:
			selected = _resolve_language(requested, candidates)
			if selected == "en-US":
				break  # The validated English snapshot is already active.
			entries = _read_culture(roots, selected)
			if entries is None:
				candidates.discard(selected)
				continue
			self.language = selected
			self._messages.update({key: value for key, value in entries.items()
								   if key in self._english
								   and _format_arguments(value) is not None
								   and _format_arguments(value) == _format_arguments(self._english[key])})
			break

	def message(self, message_id: str, *arguments: object) -> str:
		english = self._english.get(
			message_id, f"unknown localization message: {message_id}"
		)
		templates = (self._messages.get(message_id, english), english,
					 _ENGLISH.get(message_id, english))
		for template in dict.fromkeys(templates):
			try:
				return template.format(*arguments)
			except (IndexError, KeyError, ValueError, TypeError):
				continue
		return english

	@property
	def messages(self) -> Mapping[str, str]:
		return self._messages


__all__ = ["Localizer"]
