# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from . import (
	Localizer,
	__version__,
	parse_file,
	parse_files,
	write_text_atomic,
	write_text_files_atomic,
)


def main() -> int:
	parser = argparse.ArgumentParser(description="Parse typed Python source into the VeroScript Python frontend AST.")
	parser.add_argument("source", nargs="*")
	parser.add_argument("-o", "--output")
	parser.add_argument("--output-dir")
	parser.add_argument("--manifest")
	parser.add_argument("--project-root")
	parser.add_argument("--module-name")
	parser.add_argument("--lang")
	parser.add_argument("--version", action="version", version=f"VeroScriptPythonFrontend {__version__}")
	arguments = parser.parse_args()
	if not arguments.source:
		parser.error("source is required")
	if arguments.output_dir:
		if arguments.output or arguments.module_name:
			parser.error("--output-dir cannot be combined with -o or --module-name")
		project = parse_files(
			arguments.source,
			project_root=arguments.project_root,
			language=arguments.lang,
		)
		if not project.succeeded:
			for diagnostic in project.diagnostics:
				print(diagnostic.format(), file=sys.stderr)
			return 1
		output_directory = Path(arguments.output_dir)
		manifest_path = Path(arguments.manifest) if arguments.manifest else (
			output_directory / "frontend.manifest.json"
		)
		outputs: list[tuple[str | Path, str]] = []
		manifest_files: list[dict[str, str]] = []
		for entry in project.modules:
			output_path = output_directory / entry.output_name
			outputs.append((
				output_path,
				json.dumps(entry.module.to_dict(), ensure_ascii=False, indent=2) + "\n",
			))
			manifest_files.append(
				{
					"input": entry.input_path,
					"module": entry.module.name,
					"ast": entry.output_name,
				}
			)
		outputs.append((manifest_path, json.dumps(
			{
				"version": 1,
				"kind": "veroscript.python-frontend-project",
				"root": project.root,
				"files": manifest_files,
			},
			ensure_ascii=False,
			indent=2,
		) + "\n"))
		try:
			write_text_files_atomic(outputs)
		except (OSError, ValueError) as error:
			localizer = Localizer(arguments.lang)
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
	result = parse_file(arguments.source[0], module_name=arguments.module_name, language=arguments.lang)
	if not result.succeeded:
		for diagnostic in result.diagnostics:
			print(diagnostic.format(), file=sys.stderr)
		return 1
	rendered = json.dumps(result.to_dict(), ensure_ascii=False, indent=2) + "\n"
	if arguments.output:
		try:
			write_text_atomic(Path(arguments.output), rendered)
		except OSError as error:
			localizer = Localizer(arguments.lang)
			print(
				f"error: {localizer.message('python_tools.source_write_failed', error)}",
				file=sys.stderr,
			)
			return 1
	else:
		sys.stdout.write(rendered)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
