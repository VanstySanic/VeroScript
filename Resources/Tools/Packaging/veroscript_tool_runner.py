# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Invoke a native packaging helper or an explicit cross-build metadata adapter."""
import subprocess


def output(tool,*arguments):
	if hasattr(tool,'packaging_output'):
		return tool.packaging_output(*map(str,arguments))
	return subprocess.check_output([str(tool),*map(str,arguments)],text=True,encoding='utf-8')


def run(tool,*arguments):
	if hasattr(tool,'packaging_run'):
		return tool.packaging_run(*map(str,arguments))
	return subprocess.run([str(tool),*map(str,arguments)],check=True)
