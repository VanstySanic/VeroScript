#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build all supported CPython wheels and test fresh installed consumers."""
import argparse
from pathlib import Path
import tempfile

from veroscript_paths import repository_root
from veroscript_release import TARGETS, binary_prefix, host_target, native_identity, safe_output, scratch_directory, version
from veroscript_business_release import load_versions
from veroscript_python_wheels import build_matrix, discover_interpreters


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--repo-root', type=Path, default=repository_root())
	parser.add_argument('--target', choices=tuple(TARGETS), default=host_target())
	parser.add_argument('--python', action='append', default=[], help='CPython executable; repeat for each supported minor version')
	parser.add_argument('--output', type=Path)
	parser.add_argument('--work-directory', type=Path)
	parser.add_argument('--native-executable', type=Path)
	parser.add_argument('--check-interpreters', action='store_true', help='Validate interpreter coverage without building')
	parser.add_argument('--version', type=version)
	parser.add_argument('--jobs', type=int, default=4)
	parser.add_argument('--cmake-generator')
	retention = parser.add_mutually_exclusive_group()
	retention.add_argument('--clean-completed-work', dest='clean_completed', action='store_true')
	retention.add_argument('--keep-completed-work', dest='clean_completed', action='store_false')
	parser.set_defaults(clean_completed=None)
	args = parser.parse_args()
	repo = args.repo_root.resolve()
	if args.target != host_target() or args.jobs < 1:
		raise ValueError('Wheel builds require a matching native host and positive jobs')
	interpreters = discover_interpreters(repo, args.target, args.python)
	if args.check_interpreters:
		print('Validated CPython interpreters: ' + ', '.join(sorted(interpreters)))
		return
	if args.output is None or args.native_executable is None or not args.native_executable.is_file():
		parser.error('--output and a tested --native-executable are required for building')
	if native_identity(binary_prefix(args.native_executable)) != TARGETS[args.target]:
		raise ValueError('The wheel test CLI does not match the selected target')
	output = safe_output(repo, args.output.resolve(), component=True)
	if output.exists() and any(output.iterdir()):
		raise ValueError('Python wheel output must be empty')
	output.mkdir(parents=True, exist_ok=True)
	package_version = args.version or load_versions(repo)['businesses']['Python']
	if args.work_directory:
		work = safe_output(repo, args.work_directory.resolve(), component=True)
		work.mkdir(parents=True, exist_ok=False)
		build_matrix(repo, output, work, args.target, package_version, interpreters, args.native_executable, args.jobs, args.cmake_generator, clean_completed=args.clean_completed)
	else:
		with tempfile.TemporaryDirectory(prefix='veroscript-python-wheels-', dir=scratch_directory(repo)) as temporary:
			build_matrix(repo, output, Path(temporary), args.target, package_version, interpreters, args.native_executable, args.jobs, args.cmake_generator, clean_completed=args.clean_completed)
	print(f'Validated CPython wheel matrix: {output}')


if __name__ == '__main__':
	main()
