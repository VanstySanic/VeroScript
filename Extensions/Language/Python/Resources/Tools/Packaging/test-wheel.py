# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Run the installed-wheel consumer tests without a fixed source folder name."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

from veroscript_python_metadata import source_module


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--project', required=True, type=Path)
	args = parser.parse_args()
	root = source_module(args.project.resolve()) / 'Extensions/Language/Python'
	environment = os.environ.copy()
	environment.pop('PYTHONPATH', None)
	environment['VEROSCRIPT_TEST_INSTALLED_PACKAGE'] = '1'
	suites = [('VeroScriptPython', 'test_veroscript.py'), ('VeroScriptPython', 'test_variant.py'),
			  ('VeroScriptPython', 'test_numerics.py'),
			  ('VeroScriptPython', 'test_wheel.py'),
			  ('VeroScriptPython', 'test_distribution_validator.py'), ('VeroScriptPythonFrontend', 'test_frontend.py'),
			  ('VeroScriptPython', 'test_mobile.py'),
			  ('VeroScriptPythonBindGen', 'test_bindgen.py'), ('VeroScriptPythonTranspiler', 'test_transpiler.py'),
			  ('VeroScriptPythonAot', 'test_aot.py')]
	for module, suite in suites:
		subprocess.run([sys.executable, '-I', '-X', 'dev', str(root / module / 'tests' / suite), '-v'],
					   check=True, env=environment)


if __name__ == '__main__':
	main()
