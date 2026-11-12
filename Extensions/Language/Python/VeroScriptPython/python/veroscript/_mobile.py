# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Resolve an APK native library when embedded Android Python is configured."""
from __future__ import annotations

import importlib
from importlib.machinery import ExtensionFileLoader
import importlib.util
import os
from pathlib import Path
import sys
from types import ModuleType


def load_native() -> ModuleType:
	name = "veroscript._veroscript"
	cached = sys.modules.get(name)
	if cached is not None:
		return cached
	directory = os.environ.get("VEROSCRIPT_ANDROID_NATIVE_LIB_DIR")
	if sys.platform != "android" or directory is None:
		return importlib.import_module(name)
	root = Path(directory)
	if not root.is_absolute():
		raise ImportError("VEROSCRIPT_ANDROID_NATIVE_LIB_DIR must be the absolute ApplicationInfo.nativeLibraryDir")
	path = root / "libveroscript_python.so"
	if not path.is_file():
		raise ImportError(f"Missing Android VeroScript native extension: {path}")
	spec = importlib.util.spec_from_file_location(name, str(path), loader=ExtensionFileLoader(name, str(path)))
	if spec is None or spec.loader is None:
		raise ImportError(f"Cannot load Android VeroScript native extension: {path}")
	module = importlib.util.module_from_spec(spec)
	sys.modules[name] = module
	try:
		spec.loader.exec_module(module)
	except BaseException:
		sys.modules.pop(name, None)
		raise
	return module
