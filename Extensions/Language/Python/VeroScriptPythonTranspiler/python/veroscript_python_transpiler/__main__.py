# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from veroscript_python_frontend import (
	Localizer,
	write_text_files_atomic,
)

from . import (
	TranspileOptions,
	TranspileProjectOptions,
	__version__,
	transpile_file,
	transpile_files,
)


def main() -> int:
	parser = argparse.ArgumentParser(description="Transpile a strict typed Python subset to VeroScript.")
	parser.add_argument("source", nargs="*")
	parser.add_argument("-o", "--output")
	parser.add_argument("--output-dir")
	parser.add_argument("--manifest")
	parser.add_argument("--project-root")
	parser.add_argument("--source-map")
	parser.add_argument("--module-name")
	parser.add_argument(
		"--veroscript",
		help="VeroScript executable, SDK root, or SDK bin directory",
	)
	parser.add_argument("--no-validate", action="store_true")
	parser.add_argument("--timeout", type=float, default=120.0)
	parser.add_argument("--lang")
	parser.add_argument("--version", action="version", version=f"VeroScriptPythonTranspiler {__version__}")
	arguments = parser.parse_args()
	if not arguments.source:
		parser.error("source is required")
	if arguments.output_dir:
		if arguments.output or arguments.source_map or arguments.module_name:
			parser.error(
				"--output-dir cannot be combined with -o, --source-map, or --module-name"
			)
		project_result = transpile_files(
			tuple(arguments.source),
			TranspileProjectOptions(
				validate_generated_sources=not arguments.no_validate,
				veroscript_executable=arguments.veroscript,
				language=arguments.lang,
				timeout_seconds=arguments.timeout,
			),
			project_root=arguments.project_root,
		)
		if not project_result.succeeded:
			for diagnostic in project_result.diagnostics:
				print(diagnostic.format(), file=sys.stderr)
			return 1
		output_directory = Path(arguments.output_dir)
		manifest_path = Path(arguments.manifest) if arguments.manifest else (
			output_directory / "transpile.manifest.json"
		)
		outputs: list[tuple[str | Path, str]] = [
			(manifest_path, project_result.manifest_json)
		]
		for generated in project_result.files:
			source_path = output_directory / generated.output_name
			outputs.append((source_path, generated.source))
			outputs.append(
				(Path(f"{source_path}.map.json"), generated.source_map_json)
			)
		localizer = Localizer(arguments.lang)
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
	if arguments.manifest or arguments.project_root:
		parser.error("--manifest and --project-root require --output-dir")
	if not arguments.output:
		parser.error("--output is required")
	options = TranspileOptions(
		validate_generated_source=not arguments.no_validate,
		veroscript_executable=arguments.veroscript,
		generated_source_path=arguments.output,
		language=arguments.lang,
		timeout_seconds=arguments.timeout,
	)
	result = transpile_file(arguments.source[0], options, module_name=arguments.module_name)
	if not result.succeeded:
		for diagnostic in result.diagnostics:
			print(diagnostic.format(), file=sys.stderr)
		return 1
	localizer = Localizer(arguments.lang)
	try:
		single_outputs: list[tuple[str | Path, str]] = [
			(Path(arguments.output), result.source)
		]
		if arguments.source_map:
			single_outputs.append((Path(arguments.source_map), result.source_map_json))
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
