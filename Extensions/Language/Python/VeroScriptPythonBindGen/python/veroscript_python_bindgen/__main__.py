# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile

from veroscript_python_frontend import (
	find_veroscript_executable,
	Localizer,
	write_text_files_atomic,
)

from . import (
	__version__,
	generate_binding_project,
	generate_bindings_from_file,
	validate_catalog,
)


def main() -> int:
	parser = argparse.ArgumentParser(description="Generate a VeroScript Binding Catalog from typed Python source.")
	parser.add_argument("source", nargs="*")
	parser.add_argument("--catalog", required=False)
	parser.add_argument("--api-reference")
	parser.add_argument("--output-dir")
	parser.add_argument("--manifest")
	parser.add_argument("--project-root")
	parser.add_argument("--module-name")
	parser.add_argument("--validate", action="store_true")
	parser.add_argument("--timeout", type=float, default=120.0)
	parser.add_argument(
		"--veroscript",
		help="VeroScript executable, SDK root, or SDK bin directory",
	)
	parser.add_argument("--lang")
	parser.add_argument("--version", action="version", version=f"VeroScriptPythonBindGen {__version__}")
	arguments = parser.parse_args()
	if not arguments.source:
		parser.error("source is required")
	if arguments.output_dir:
		if arguments.catalog or arguments.api_reference or arguments.module_name:
			parser.error(
				"--output-dir cannot be combined with --catalog, --api-reference, or --module-name"
			)
		project_result = generate_binding_project(
			arguments.source,
			project_root=arguments.project_root,
			language=arguments.lang,
		)
		if not project_result.succeeded:
			for diagnostic in project_result.diagnostics:
				print(diagnostic.format(), file=sys.stderr)
			return 1
		output_directory = Path(arguments.output_dir)
		catalog_path = output_directory / "bindings.json"
		manifest_path = Path(arguments.manifest) if arguments.manifest else (
			output_directory / "bindgen.manifest.json"
		)
		localizer = Localizer(arguments.lang)
		if arguments.validate:
			executable = find_veroscript_executable(arguments.veroscript)
			if not executable:
				print(
					f"error: {localizer.message('python_tools.validator_not_found')}",
					file=sys.stderr,
				)
				return 1
			with tempfile.TemporaryDirectory() as directory:
				validation_path = Path(directory) / "generated.bindings.json"
				validation_path.write_text(
					project_result.catalog_json, encoding="utf-8", newline="\n"
				)
				succeeded, output = validate_catalog(
					validation_path, executable, timeout_seconds=arguments.timeout
				)
			if not succeeded:
				print(
					localizer.message(
						"python_tools.catalog_validation_failed",
						output or "unknown validator error",
					),
					file=sys.stderr,
				)
				return 1
		outputs: list[tuple[str | Path, str]] = [
			(catalog_path, project_result.catalog_json),
			(manifest_path, project_result.manifest_json),
		]
		for reference in project_result.api_references:
			outputs.append(
				(output_directory / reference.output_name, reference.api_reference)
			)
		try:
			write_text_files_atomic(outputs)
		except (OSError, ValueError) as error:
			print(
				f"error: {localizer.message('python_tools.source_write_failed', error)}",
				file=sys.stderr,
			)
			return 1
		return 0
	if len(arguments.source) != 1:
		parser.error("multiple sources require --output-dir")
	if arguments.project_root or arguments.manifest:
		parser.error("--project-root and --manifest require --output-dir")
	if not arguments.catalog:
		parser.error("--catalog is required")
	result = generate_bindings_from_file(
		arguments.source[0],
		module_name=arguments.module_name,
		language=arguments.lang,
	)
	if not result.succeeded:
		for diagnostic in result.diagnostics:
			print(diagnostic.format(), file=sys.stderr)
		return 1
	localizer = Localizer(arguments.lang)
	if arguments.validate:
		executable = find_veroscript_executable(arguments.veroscript)
		if not executable:
			print(f"error: {localizer.message('python_tools.validator_not_found')}", file=sys.stderr)
			return 1
		with tempfile.TemporaryDirectory() as directory:
			validation_path = Path(directory) / "generated.bindings.json"
			validation_path.write_text(
				result.catalog_json,
				encoding="utf-8",
				newline="\n",
			)
			succeeded, output = validate_catalog(
				validation_path, executable, timeout_seconds=arguments.timeout
			)
		if not succeeded:
			print(
				localizer.message(
					"python_tools.catalog_validation_failed",
					output or "unknown validator error",
				),
				file=sys.stderr,
			)
			return 1
	try:
		single_outputs: list[tuple[str | Path, str]] = [
			(Path(arguments.catalog), result.catalog_json)
		]
		if arguments.api_reference:
			single_outputs.append((Path(arguments.api_reference), result.api_reference))
		write_text_files_atomic(single_outputs)
	except (OSError, ValueError) as error:
		print(
			f"error: {localizer.message('python_tools.source_write_failed', error)}",
			file=sys.stderr,
		)
		return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
