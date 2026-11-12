# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

from importlib import metadata
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import sysconfig
from types import ModuleType
import unittest

import veroscript
from veroscript import _veroscript as native
import veroscript_python_bindgen
import veroscript_python_frontend
import veroscript_python_transpiler
import veroscript_python_aot


class VeroScriptWheelTests(unittest.TestCase):
	def test_installed_python_tool_catalogs(self) -> None:
		from veroscript_python_frontend.localization import Localizer

		root = Path(veroscript.__file__).parent / "localization"
		localizer = Localizer(language="zh-CN")
		for filename, message_id in (
			("python-tools.json", "python_tools.source_write_failed"),
			("python-frontend.json", "python_tools.syntax_error"),
			("python-bindgen.json", "python_tools.missing_annotation"),
			("python-transpiler.json", "python_tools.generated_validation_failed"),
			("python-aot.json", "python_tools.aot_failed"),
		):
			with self.subTest(filename=filename):
				catalog = json.loads((root / "zh-CN" / filename).read_text(encoding="utf-8"))
				self.assertEqual(localizer.message(message_id, "example"), catalog[message_id].format("example"))
		self.assertFalse((root / "en-US" / "tooling.json").exists())
		self.assertFalse((root / "en-US" / "dap.json").exists())

	def test_installed_source_tools_reject_invalid_contracts(self) -> None:
		self.assertFalse(veroscript_python_frontend.is_veroscript_identifier("do"))
		invalid_identifier = "def do() -> int:\n    return 1\n"
		invalid_binding = veroscript_python_bindgen.generate_bindings_from_source(
			invalid_identifier,
			"invalid-identifier.py",
			module_name="sample",
		)
		self.assertFalse(invalid_binding.succeeded)
		invalid_transpile = veroscript_python_transpiler.transpile_source(
			invalid_identifier,
			"invalid-identifier.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(invalid_transpile.succeeded)

		pass_transpile = veroscript_python_transpiler.transpile_source(
			"def finished() -> None:\n    pass\n",
			"pass.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertTrue(
			pass_transpile.succeeded,
			[item.format() for item in pass_transpile.diagnostics],
		)
		self.assertIn("\t{}", pass_transpile.source)

		deep_source = "def nested() -> None:\n" + "".join(
			"    " * (index + 1) + "if True:\n" for index in range(96)
		) + "    " * 97 + "print(1)\n"
		deep_transpile = veroscript_python_transpiler.transpile_source(
			deep_source,
			"deep.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(deep_transpile.succeeded)

		private_contract = (
			"class _Hidden:\n"
			"    value: int\n\n"
			"def expose(value: _Hidden) -> int:\n"
			"    return value.value\n"
		)
		private_binding = veroscript_python_bindgen.generate_bindings_from_source(
			private_contract,
			"private-contract.py",
			module_name="private_contract",
		)
		self.assertFalse(private_binding.succeeded)
		private_transpile = veroscript_python_transpiler.transpile_source(
			private_contract,
			"private-contract.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(private_transpile.succeeded)

		for source in (
			"def value() -> float:\n    return 1e309\n",
			"def value() -> int:\n    return 2147483648\n",
			"def value() -> int:\n    return -2147483649\n",
		):
			numeric_transpile = veroscript_python_transpiler.transpile_source(
				source,
				"numeric-literal.py",
				veroscript_python_transpiler.TranspileOptions(
					validate_generated_source=False
				),
			)
			self.assertFalse(numeric_transpile.succeeded)

		minimum_transpile = veroscript_python_transpiler.transpile_source(
			"def value() -> int:\n    return -2147483648\n",
			"minimum.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertTrue(minimum_transpile.succeeded)

		for source in (
			"def value() -> list[int]:\n    return [1, 2]\n",
			"def value() -> set[int]:\n    return {1, 2}\n",
			'def value() -> dict[str, int]:\n    return {"a": 1}\n',
		):
			container_transpile = veroscript_python_transpiler.transpile_source(
				source,
				"container-expression.py",
				veroscript_python_transpiler.TranspileOptions(
					validate_generated_source=False
				),
			)
			self.assertFalse(container_transpile.succeeded)

		identity_transpile = veroscript_python_transpiler.transpile_source(
			"def value(text: str) -> bool:\n    return text is not None\n",
			"identity.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(identity_transpile.succeeded)

		for keyword in ("break", "continue"):
			loop_control = veroscript_python_transpiler.transpile_source(
				f"def value() -> None:\n    {keyword}\n",
				f"{keyword}.py",
				veroscript_python_transpiler.TranspileOptions(
					validate_generated_source=False
				),
			)
			self.assertFalse(loop_control.succeeded)

		enum_transpile = veroscript_python_transpiler.transpile_source(
			"from enum import IntFlag\n"
			"class Mode(IntFlag):\n"
			"    READ = 1 << 0\n"
			"    WRITE = 1 << 1\n"
			"    ALL = READ | WRITE\n",
			"enum.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertTrue(enum_transpile.succeeded)
		self.assertIn("ALL = 3", enum_transpile.source)

		self_name = veroscript_python_transpiler.transpile_source(
			"def value(self: int) -> int:\n    return self\n",
			"self-name.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertTrue(self_name.succeeded)
		self.assertIn("return self;", self_name.source)

		void_none = veroscript_python_transpiler.transpile_source(
			"def finished() -> None:\n    return None\n",
			"void-none.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertTrue(void_none.succeeded)
		self.assertIn("return;", void_none.source)
		self.assertNotIn("nullptr", void_none.source)

		invalid_return = veroscript_python_transpiler.transpile_source(
			"def value() -> int:\n    return\n",
			"invalid-return.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(invalid_return.succeeded)

		missing_return = veroscript_python_transpiler.transpile_source(
			"def value() -> int:\n    pass\n",
			"missing-return.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(missing_return.succeeded)

		nested_local = veroscript_python_transpiler.transpile_source(
			(
				"def value(flag: bool) -> int:\n"
				"    if flag:\n"
				"        result: int = 1\n"
				"    result = 2\n"
				"    return result\n"
			),
			"nested-local.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(nested_local.succeeded)

		invalid_property = veroscript_python_bindgen.generate_bindings_from_source(
			"class Settings:\n"
			"    @property\n"
			"    def value(self, extra: int) -> int:\n        return extra\n",
			"invalid-property.py",
		)
		self.assertFalse(invalid_property.succeeded)

		collision_source = (
			"class Example:\n"
			"    value: int\n"
			"    def value(self) -> int:\n        return 1\n"
		)
		parsed = veroscript_python_frontend.parse_source(
			collision_source, "collision.py"
		)
		self.assertFalse(parsed.succeeded)
		transpiled = veroscript_python_transpiler.transpile_source(
			collision_source,
			"collision.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(transpiled.succeeded)
		self.assertEqual(transpiled.source, "")

		invalid_void = veroscript_python_bindgen.generate_bindings_from_source(
			"class Settings:\n    value: None\n",
			"invalid-void.py",
		)
		self.assertFalse(invalid_void.succeeded)
		invalid_void_transpile = veroscript_python_transpiler.transpile_source(
			"class Settings:\n    value: None\n",
			"invalid-void.py",
			veroscript_python_transpiler.TranspileOptions(
				validate_generated_source=False
			),
		)
		self.assertFalse(invalid_void_transpile.succeeded)

		uint32_source = (
			"from __future__ import annotations\n"
			"def identity(value: UInt32) -> UInt32:\n    return value\n"
		)
		uint32_result = veroscript_python_bindgen.generate_bindings_from_source(
			uint32_source,
			"uint32.py",
			module_name="uint32_api",
		)
		self.assertTrue(
			uint32_result.succeeded,
			[item.format() for item in uint32_result.diagnostics],
		)

		class Session:
			def __init__(self) -> None:
				self.options: dict[str, object] = {}

			def register_function(
				self, name: str, callback: object, **kwargs: object
			) -> None:
				self.options = kwargs

		module = ModuleType("uint32_api")
		exec(compile(uint32_source, "uint32.py", "exec"), module.__dict__)
		session = Session()
		veroscript_python_bindgen.bind_module(session, module, uint32_result)
		self.assertEqual(session.options["return_type"], "uint32")
		self.assertEqual(session.options["parameters"], ("uint32",))

	def test_distribution_metadata_matches_runtime(self) -> None:
		distribution = metadata.distribution("veroscript")
		self.assertEqual(distribution.version, veroscript.__version__)
		self.assertEqual(
			distribution.metadata["Requires-Python"], "<3.16,>=3.11"
		)
		self.assertEqual(
			set(distribution.metadata.get_all("Requires-Dist", [])),
			{'typing_extensions>=4.16; python_version < "3.15"'},
		)
		self.assertEqual(distribution.metadata["License-Expression"], "MIT")
		self.assertEqual(
			set(distribution.metadata.get_all("License-File", [])),
			{"LICENSE"},
		)
		license_text = distribution.read_text("licenses/LICENSE")
		if license_text is None:
			self.fail("Installed wheel is missing the project license text")
		self.assertIn("MIT License", license_text)
		self.assertIn("Copyright (c) 2026 VanstySanic", license_text)
		self.assertIn("copies or substantial portions of the Software.", license_text)

	def test_runtime_payload_is_self_contained(self) -> None:
		package = Path(veroscript.__file__).resolve().parent
		native_path = Path(native.__file__).resolve()
		extension_suffix = sysconfig.get_config_var("EXT_SUFFIX")
		self.assertIsInstance(extension_suffix, str)
		self.assertEqual(native_path.parent, package)
		self.assertEqual(native_path.name, f"_veroscript{extension_suffix}")
		self.assertTrue((package / "py.typed").is_file())
		self.assertTrue((package / "_veroscript.pyi").is_file())
		self.assertTrue((package / "localization" / "en-US").is_dir())
		self.assertTrue((package / "stdlib" / "core.vs").is_file())
		self.assertTrue((package / "stdlib" / "memory.vs").is_file())
		self.assertTrue((package / "stdlib" / "bigint.vs").is_file())
		self.assertTrue((package / "stdlib" / "rational.vs").is_file())
		for language in ("en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"):
			self.assertTrue((package / "localization" / language / "memory.json").is_file())
		self.assertTrue(any(package.glob("VeroScriptInterop.dll"))
						or any(package.glob("libVeroScriptInterop.*")))
		for license_file in ("miniz/LICENSE", "json/LICENSE.MIT", "sha256/LICENSE"):
			license_path = package / "licenses" / "application" / license_file
			self.assertTrue(license_path.is_file(), license_file)
			self.assertTrue(license_path.read_text(encoding="utf-8").strip(), license_file)
		if os.name == "nt":
			libraries = ["msvcp140.dll", "vcruntime140.dll"]
			if sys.maxsize > 2 ** 32:
				libraries.append("vcruntime140_1.dll")
			for library in libraries:
				self.assertTrue((package / library).is_file(), library)
			self.assertTrue((package / "licenses/MicrosoftVisualCpp/NOTICE.txt").is_file())

		bundled_cpython = re.compile(
			r"^(?:python(?:3|\d{2,3})?(?:_d)?\.(?:exe|dll|lib)|"
			r"python(?:3|\d{2,3})?\.(?:zip|_pth)|"
			r"libpython\d(?:\.\d+)*(?:m)?\.(?:so(?:\.\d+)*|dylib)|"
			r"pyvenv\.cfg)$",
			re.IGNORECASE,
		)
		for runtime_name in (
			"python.exe",
			"python3.dll",
			"python314.lib",
			"python314.zip",
			"python314._pth",
			"libpython3.so",
			"libpython3.14.dylib",
			"pyvenv.cfg",
		):
			self.assertIsNotNone(
				bundled_cpython.fullmatch(runtime_name), runtime_name
			)
		unexpected = [
			path.relative_to(package).as_posix()
			for path in package.rglob("*")
			if path.is_file()
			and (
				bundled_cpython.fullmatch(path.name)
				or path.name.casefold() == "python.h"
				or "python.framework" in {
					part.casefold() for part in path.relative_to(package).parts
				}
			)
		]
		self.assertEqual(unexpected, [], f"bundled CPython files: {unexpected}")

	def test_installed_runtime_executes(self) -> None:
		with veroscript.Session() as session:
			loaded = session.load_source(
				"int Add(int a, int b) { return a + b; } void main() {}",
				"wheel-smoke.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("Add", 20, 22), 42)

	def test_python_source_tools_are_installed(self) -> None:
		self.assertEqual(veroscript_python_frontend.__version__, veroscript.__version__)
		self.assertEqual(veroscript_python_bindgen.__version__, veroscript.__version__)
		self.assertEqual(veroscript_python_transpiler.__version__, veroscript.__version__)
		self.assertEqual(veroscript_python_aot.__version__, veroscript.__version__)
		scripts = {
			entry.name: entry.value
			for entry in metadata.entry_points(group="console_scripts")
			if entry.dist is not None and entry.dist.name == "veroscript"
		}
		expected_scripts = {
			"veroscript-python-frontend": "veroscript_python_frontend.__main__:main",
			"veroscript-python-bindgen": "veroscript_python_bindgen.__main__:main",
			"veroscript-python-transpiler": "veroscript_python_transpiler.__main__:main",
			"veroscript-python-aot": "veroscript_python_aot.__main__:main",
		}
		for name, target in expected_scripts.items():
			self.assertEqual(scripts.get(name), target)

		for module in (
			"veroscript_python_frontend",
			"veroscript_python_bindgen",
			"veroscript_python_transpiler",
			"veroscript_python_aot",
		):
			completed = subprocess.run(
				[sys.executable, "-m", module, "--version"],
				check=False,
				capture_output=True,
				text=True,
				encoding="utf-8",
				errors="replace",
			)
			self.assertEqual(completed.returncode, 0, completed.stderr)
			self.assertIn(veroscript.__version__, completed.stdout)


if __name__ == "__main__":
	unittest.main()
