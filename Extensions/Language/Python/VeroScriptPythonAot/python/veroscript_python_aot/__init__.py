# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Python control API for the VeroScript-to-Python AOT generator."""

from __future__ import annotations

from dataclasses import dataclass
import json
import math
import os
from os import PathLike
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import tempfile
from uuid import uuid4
from typing import Any

from veroscript_python_frontend import (
	Localizer,
	__version__ as __version__,
	find_veroscript_executable,
)


PathValue = str | PathLike[str]


@dataclass(frozen=True, slots=True)
class PythonAotOptions:
	"""External toolchain and compiler options for Python AOT generation."""

	veroscript_executable: PathValue | None = None
	import_paths: tuple[PathValue, ...] = ()
	defines: tuple[str, ...] = ()
	implicit_imports: tuple[PathValue, ...] = ()
	allow_circular_imports: bool = True
	allow_project_wide_access: bool = False
	timeout_seconds: float = 120.0
	language: str | None = None
	catalog_directory: PathValue | None = None


@dataclass(frozen=True, slots=True)
class PythonAotResult:
	source_path: str
	output_path: str
	is_bundle: bool
	command: tuple[str, ...]
	returncode: int
	stdout: str
	stderr: str
	files: tuple[str, ...] = ()
	manifest_path: str | None = None

	@property
	def succeeded(self) -> bool:
		return self.returncode == 0 and bool(self.files)


class PythonAotError(RuntimeError):
	"""A launch, compiler, or generated-output validation failure."""

	def __init__(self, message: str, result: PythonAotResult) -> None:
		super().__init__(message)
		self.result = result


def _options(value: PythonAotOptions | None) -> PythonAotOptions:
	return value if value is not None else PythonAotOptions()


def _localizer(options: PythonAotOptions) -> Localizer:
	return Localizer(options.language, options.catalog_directory)


def _failure_message(result: PythonAotResult, localizer: Localizer) -> str:
	detail = result.stderr.strip() or result.stdout.strip() or (
		f"VeroScript exited with status {result.returncode}"
	)
	return localizer.message("python_tools.aot_failed", detail)


def _finish(
	result: PythonAotResult,
	*,
	check: bool,
	localizer: Localizer,
) -> PythonAotResult:
	if check and not result.succeeded:
		raise PythonAotError(_failure_message(result, localizer), result)
	return result


def _command(
	source_path: PathValue,
	*,
	output_path: PathValue | None,
	output_directory: PathValue | None,
	bundle_name: str | None,
	options: PythonAotOptions,
) -> tuple[str, ...]:
	executable = find_veroscript_executable(options.veroscript_executable)
	if executable is None:
		raise FileNotFoundError(
			_localizer(options).message("python_tools.validator_not_found")
		)
	if (output_path is None) == (output_directory is None):
		raise ValueError("exactly one of output_path or output_directory is required")
	if bundle_name is not None and output_directory is None:
		raise ValueError("bundle_name requires output_directory")
	if not math.isfinite(options.timeout_seconds) or options.timeout_seconds <= 0:
		raise ValueError(
			_localizer(options).message("python_tools.timeout_invalid")
		)

	command = [executable, "aot", "--target=python"]
	for path in options.import_paths:
		command.extend(("-I", os.fspath(path)))
	for define in options.defines:
		if not define:
			raise ValueError("defines must not contain an empty value")
		command.extend(("-D", define))
	for path in options.implicit_imports:
		command.extend(("--implicit-import", os.fspath(path)))
	if not options.allow_circular_imports:
		command.append("--no-circular-imports")
	if options.allow_project_wide_access:
		command.append("--allow-project-wide-access")
	command.append(os.fspath(source_path))
	if output_path is not None:
		command.extend(("-o", os.fspath(output_path)))
	else:
		assert output_directory is not None
		command.extend(("--output-dir", os.fspath(output_directory)))
		if bundle_name is not None:
			command.extend(("--bundle-name", bundle_name))
	return tuple(command)


def build_python_aot_command(
	source_path: PathValue,
	*,
	output_path: PathValue | None = None,
	output_directory: PathValue | None = None,
	bundle_name: str | None = None,
	options: PythonAotOptions | None = None,
) -> tuple[str, ...]:
	"""Build the exact argument vector used for an external AOT invocation."""

	return _command(
		source_path,
		output_path=output_path,
		output_directory=output_directory,
		bundle_name=bundle_name,
		options=_options(options),
	)


def _invalid_output(
	result: PythonAotResult,
	message: str,
) -> PythonAotResult:
	detail = f"{result.stderr.rstrip()}\n{message}".strip()
	return PythonAotResult(
		result.source_path,
		result.output_path,
		result.is_bundle,
		result.command,
		1,
		result.stdout,
		detail,
	)


def _is_link(path: Path) -> bool:
	if path.is_symlink():
		return True
	if os.name != "nt":
		return False
	try:
		attributes = getattr(path.lstat(), "st_file_attributes", 0)
	except FileNotFoundError:
		return False
	# Junctions are reparse points rather than symbolic links on Windows.
	return bool(attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def _bundle_payload(
	root: Path,
	result: PythonAotResult,
	localizer: Localizer,
) -> PythonAotResult:
	manifest = root / "python-aot.manifest.json"
	try:
		if _is_link(root) or not root.is_dir():
			raise ValueError("output is not a regular directory")
		if _is_link(manifest) or not manifest.is_file():
			raise ValueError("python-aot.manifest.json is missing")
		payload: Any = json.loads(manifest.read_text(encoding="utf-8"))
		if (
			not isinstance(payload, dict)
			or payload.get("version") != 1
			or payload.get("kind") != "veroscript.python-aot-source-bundle"
			or payload.get("entry") != "__main__.py"
			or not isinstance(payload.get("name"), str)
			or not payload.get("name")
			or not isinstance(payload.get("files"), list)
		):
			raise ValueError("unexpected manifest contract")
		raw_files = payload["files"]
		if not raw_files or not all(isinstance(item, str) for item in raw_files):
			raise ValueError("manifest files must be a non-empty string list")
		expected_without_runtime = [
			"__main__.py",
			"__init__.py",
			"_veroscript_program.py",
		]
		expected_with_runtime = [
			"__main__.py",
			"__init__.py",
			"_veroscript_runtime.py",
			"_veroscript_program.py",
		]
		if raw_files not in (expected_without_runtime, expected_with_runtime):
			raise ValueError("manifest contains an unexpected generated file set")
		root_resolved = root.resolve()
		files: list[str] = []
		for item in raw_files:
			relative = PurePosixPath(item)
			if (
				relative.is_absolute()
				or "\\" in item
				or any(part in {"", ".", ".."} for part in relative.parts)
			):
				raise ValueError(f"unsafe generated path {item!r}")
			candidate = root.joinpath(*relative.parts)
			if _is_link(candidate) or not candidate.is_file():
				raise ValueError(f"missing generated file {item!r}")
			resolved = candidate.resolve()
			if not resolved.is_relative_to(root_resolved):
				raise ValueError(f"generated file escapes output directory: {item!r}")
			files.append(str(resolved))
		expected_entries = {"python-aot.manifest.json", *raw_files}
		actual_entries: set[str] = set()
		for candidate in root.rglob("*"):
			relative_parts = candidate.relative_to(root).parts
			relative_path = PurePosixPath(*relative_parts).as_posix()
			if _is_link(candidate):
				raise ValueError(f"unexpected generated directory or link {relative_path!r}")
			if candidate.is_dir():
				if relative_parts == ("__pycache__",):
					continue
				raise ValueError(f"unexpected generated directory or link {relative_path!r}")
			if not candidate.is_file():
				raise ValueError(f"unexpected generated entry {relative_path!r}")
			if len(relative_parts) == 2 and relative_parts[0] == "__pycache__":
				cache_name = relative_parts[1]
				known_stems = (
					"__main__.cpython-",
					"__init__.cpython-",
					"_veroscript_runtime.cpython-",
					"_veroscript_program.cpython-",
				)
				if (
					not cache_name.endswith(".pyc")
					or not cache_name.startswith(known_stems)
				):
					raise ValueError(f"unexpected generated cache entry {relative_path!r}")
				continue
			actual_entries.add(relative_path)
		if actual_entries != expected_entries:
			raise ValueError("output directory contains unexpected generated files")
	except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
		return _invalid_output(
			result,
			localizer.message("python_tools.aot_manifest_invalid", error),
		)
	return PythonAotResult(
		result.source_path,
		result.output_path,
		True,
		result.command,
		0,
		result.stdout,
		result.stderr,
		tuple(files),
		str(manifest.resolve()),
	)


def _run(
	source_path: PathValue,
	*,
	output_path: PathValue | None,
	output_directory: PathValue | None,
	bundle_name: str | None,
	options: PythonAotOptions,
	check: bool,
) -> PythonAotResult:
	source = Path(source_path).expanduser()
	selected_output = output_path if output_path is not None else output_directory
	if selected_output is None:
		raise ValueError("an output path is required")
	output = Path(selected_output).expanduser()
	localizer = _localizer(options)
	if output.name in {"", ".", ".."}:
		result = PythonAotResult(
			str(source), str(output), output_directory is not None, (), 2, "",
			"output must name a file or directory",
		)
		return _finish(result, check=check, localizer=localizer)
	try:
		output.parent.mkdir(parents=True, exist_ok=True)
		# Preserve the final path component so links cannot bypass validation.
		output = output.parent.resolve(strict=False) / output.name
	except (OSError, ValueError) as error:
		result = PythonAotResult(
			str(source), str(output), output_directory is not None, (),
			2 if isinstance(error, ValueError) else 1, "", str(error)
		)
		return _finish(result, check=check, localizer=localizer)
	try:
		staging_directory = Path(tempfile.mkdtemp(
			prefix=f".{output.name}.veroscript-python-aot-", dir=output.parent
		))
	except (OSError, ValueError) as error:
		result = PythonAotResult(
			str(source), str(output), output_directory is not None, (),
			2 if isinstance(error, ValueError) else 1, "", str(error)
		)
		return _finish(result, check=check, localizer=localizer)
	staged_output = staging_directory / output.name if output_path is not None else staging_directory
	try:
		command = _command(
			source,
			output_path=staged_output if output_path is not None else None,
			output_directory=staged_output if output_directory is not None else None,
			bundle_name=bundle_name,
			options=options,
		)
	except ValueError as error:
		shutil.rmtree(staging_directory, ignore_errors=True)
		result = PythonAotResult(
			str(source), str(output), output_directory is not None, (), 2, "", str(error)
		)
		return _finish(result, check=check, localizer=localizer)
	except (FileNotFoundError, OSError) as error:
		shutil.rmtree(staging_directory, ignore_errors=True)
		result = PythonAotResult(
			str(source), str(output), output_directory is not None, (), 127, "", str(error)
		)
		return _finish(result, check=check, localizer=localizer)
	environment = None
	if options.language:
		environment = os.environ.copy()
		environment["VERO_LANG"] = options.language
	try:
		completed = subprocess.run(
			command,
			check=False,
			capture_output=True,
			text=True,
			encoding="utf-8",
			errors="replace",
			timeout=options.timeout_seconds,
			env=environment,
		)
		result = PythonAotResult(
			str(source),
			str(output),
			output_directory is not None,
			command,
			completed.returncode,
			completed.stdout,
			completed.stderr,
		)
	except (OSError, subprocess.TimeoutExpired, ValueError) as error:
		result = PythonAotResult(
			str(source),
			str(output),
			output_directory is not None,
			command,
			124 if isinstance(error, subprocess.TimeoutExpired) else (
				2 if isinstance(error, ValueError) else 127
			),
			"",
			str(error),
		)
	try:
		if result.returncode != 0:
			return _finish(result, check=check, localizer=localizer)
		if output_directory is not None:
			result = _bundle_payload(staged_output, result, localizer)
			if result.returncode == 0 and (output.exists() or _is_link(output)):
				is_empty_directory = (
					not _is_link(output)
					and output.is_dir()
					and next(output.iterdir(), None) is None
				)
				if not is_empty_directory:
					previous = _bundle_payload(output, result, localizer)
					if previous.returncode != 0:
						result = _invalid_output(
							result, "refusing to replace a non-bundle output directory"
						)
			if result.returncode == 0:
				backup: Path | None = None
				try:
					if output.exists() or _is_link(output):
						if _is_link(output) or not output.is_dir():
							raise OSError("output is not a regular directory")
						backup = output.with_name(
							f".{output.name}.previous-{uuid4().hex}"
						)
						os.replace(output, backup)
					os.replace(staged_output, output)
					if backup is not None:
						shutil.rmtree(backup, ignore_errors=True)
				except OSError as error:
					if backup is not None and backup.exists() and not output.exists():
						try:
							os.replace(backup, output)
						except OSError:
							pass
					result = _invalid_output(result, str(error))
				else:
					result = PythonAotResult(
						result.source_path, result.output_path, True, result.command,
						0, result.stdout, result.stderr,
						tuple(str(output / Path(item).name) for item in result.files),
						str((output / "python-aot.manifest.json").resolve()),
					)
		elif _is_link(staged_output) or not staged_output.is_file():
			result = _invalid_output(
				result,
				localizer.message("python_tools.aot_output_missing", output),
			)
		elif _is_link(output) or (output.exists() and not output.is_file()):
			result = _invalid_output(result, "refusing to replace a non-file output")
		else:
			try:
				os.replace(staged_output, output)
			except OSError as error:
				result = _invalid_output(result, str(error))
			else:
				result = PythonAotResult(
					result.source_path, result.output_path, False, result.command,
					0, result.stdout, result.stderr, (str(output.resolve()),),
				)
		return _finish(result, check=check, localizer=localizer)
	except OSError as error:
		return _finish(
			_invalid_output(result, str(error)), check=check, localizer=localizer
		)
	finally:
		shutil.rmtree(staging_directory, ignore_errors=True)


def generate_python(
	source_path: PathValue,
	output_path: PathValue,
	*,
	options: PythonAotOptions | None = None,
	check: bool = True,
) -> PythonAotResult:
	"""Generate one standalone Python file from a VeroScript source file."""

	return _run(
		source_path,
		output_path=output_path,
		output_directory=None,
		bundle_name=None,
		options=_options(options),
		check=check,
	)


def generate_python_bundle(
	source_path: PathValue,
	output_directory: PathValue,
	*,
	bundle_name: str | None = None,
	options: PythonAotOptions | None = None,
	check: bool = True,
) -> PythonAotResult:
	"""Generate an executable multi-file Python package and verified manifest."""

	return _run(
		source_path,
		output_path=None,
		output_directory=output_directory,
		bundle_name=bundle_name,
		options=_options(options),
		check=check,
	)


__all__ = [
	"__version__",
	"PythonAotError",
	"PythonAotOptions",
	"PythonAotResult",
	"build_python_aot_command",
	"generate_python",
	"generate_python_bundle",
]
