# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from . import (
	PythonAotError,
	PythonAotOptions,
	__version__,
	generate_python,
	generate_python_bundle,
)


def main() -> int:
	parser = argparse.ArgumentParser(
		description="Generate standalone Python from VeroScript source."
	)
	parser.add_argument("source")
	destination = parser.add_mutually_exclusive_group(required=True)
	destination.add_argument("-o", "--output")
	destination.add_argument("--output-dir")
	parser.add_argument("--bundle-name")
	parser.add_argument("-I", "--import-path", action="append", default=[])
	parser.add_argument("-D", "--define", action="append", default=[])
	parser.add_argument("--implicit-import", action="append", default=[])
	parser.add_argument("--no-circular-imports", action="store_true")
	parser.add_argument("--allow-project-wide-access", action="store_true")
	parser.add_argument(
		"--veroscript",
		help="VeroScript executable, SDK root, or SDK bin directory",
	)
	parser.add_argument("--timeout", type=float, default=120.0)
	parser.add_argument("--lang")
	parser.add_argument(
		"--version", action="version", version=f"VeroScriptPythonAot {__version__}"
	)
	arguments = parser.parse_args()
	if arguments.bundle_name and not arguments.output_dir:
		parser.error("--bundle-name requires --output-dir")
	options = PythonAotOptions(
		veroscript_executable=arguments.veroscript,
		import_paths=tuple(arguments.import_path),
		defines=tuple(arguments.define),
		implicit_imports=tuple(arguments.implicit_import),
		allow_circular_imports=not arguments.no_circular_imports,
		allow_project_wide_access=arguments.allow_project_wide_access,
		timeout_seconds=arguments.timeout,
		language=arguments.lang,
	)
	try:
		if arguments.output_dir:
			result = generate_python_bundle(
				arguments.source,
				arguments.output_dir,
				bundle_name=arguments.bundle_name,
				options=options,
			)
		else:
			result = generate_python(arguments.source, arguments.output, options=options)
	except PythonAotError as error:
		print(f"error: {error}", file=sys.stderr)
		return 1
	output = Path(result.output_path)
	print(f"Wrote Python {'source bundle' if result.is_bundle else 'source'} to {output}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
