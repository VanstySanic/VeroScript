# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from pathlib import Path

from veroscript_python_aot import PythonAotOptions, generate_python_bundle


result = generate_python_bundle(
	"app.vs",
	"app_python",
	bundle_name="app",
	options=PythonAotOptions(veroscript_executable=Path("VeroScriptSDK")),
)
print(result.manifest_path)
