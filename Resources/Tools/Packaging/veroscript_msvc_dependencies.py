# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Reject MSVC Ninja caches that silently omitted generated Core ABI headers."""
import os
from pathlib import Path
import re
import subprocess


def verify_msvc_dependencies(build: Path, settings: dict) -> None:
	if (os.name != 'nt' or not settings.get('CMAKE_GENERATOR', '').startswith('Ninja')
			or Path(settings.get('CMAKE_CXX_COMPILER', '')).name.lower() not in {'cl.exe', 'clang-cl.exe'}):
		return
	build = build.resolve()
	object_file = build / 'VeroScriptPackageManager/CMakeFiles/VeroScriptPackageRuntime.dir/Private/PackageFilesystem.cpp.obj'
	header = build / 'VeroScript/Generated/VeroScriptJitBuild.h'
	problem = ('MSVC Ninja cache does not track the current generated Core identity header. '
		'Use matching compiler language resources for configure and build, recreate the CMake cache, '
		'and rebuild the affected native objects before packaging.')
	if not object_file.is_file() or not header.is_file():
		raise ValueError(problem)
	result = subprocess.run([settings.get('CMAKE_MAKE_PROGRAM', 'ninja'), '-t', 'deps',
		object_file.relative_to(build).as_posix()], cwd=build, check=True,
		capture_output=True, text=True, encoding='utf-8')
	dependencies = {(build / line.strip()).resolve() for line in result.stdout.splitlines() if line.startswith('    ')}
	if (not re.search(r'#deps [1-9][0-9]*\b', result.stdout) or header not in dependencies
			or object_file.stat().st_mtime_ns < header.stat().st_mtime_ns):
		raise ValueError(problem)
