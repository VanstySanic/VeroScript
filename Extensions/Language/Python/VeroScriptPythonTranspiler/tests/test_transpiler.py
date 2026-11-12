# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
TEST_INSTALLED = os.environ.get("VEROSCRIPT_TEST_INSTALLED_PACKAGE") == "1"
if not TEST_INSTALLED:
	sys.path.insert(0, str(ROOT / "VeroScriptPythonFrontend" / "python"))
	sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

import veroscript_python_transpiler as transpiler  # noqa: E402
from veroscript_python_frontend import parse_source  # noqa: E402
from veroscript_python_transpiler import (  # noqa: E402
	TranspileOptions,
	TranspileProjectOptions,
	transpile_files,
	transpile_source,
)


SOURCE = '''
class Counter:
    value: int = 0

    def __init__(self, initial: int) -> None:
        self.value = initial

    def increment(self, amount: int) -> int:
        self.value += amount
        return self.value

def add(left: int, right: int) -> int:
    return left + right

def main() -> int:
    counter: Counter = Counter(2)
    return 0 if add(counter.increment(3), 4) == 9 else 1

def finished() -> None:
    pass
'''


class PythonTranspilerTests(unittest.TestCase):
	def test_string_lengths_preserve_unicode_code_points_and_single_evaluation(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		source = (
			'class State:\n    text: str = "你好😀"\n    calls: int = 0\n'
			"    def read(self) -> str:\n        self.calls += 1\n        return self.text\n"
			"    def size(self) -> int:\n        return len(self.text)\n"
			'def text() -> str:\n    return "👩‍💻"\n'
			'def size(value: str) -> int:\n    return len(value)\n'
			'def main() -> None:\n    state: State = State()\n    values: list[str] = ["é", "😀"]\n'
			'    mapping: dict[int, str] = {1: "你好"}\n    empty: str = ""\n    ascii: str = "abc"\n'
			'    nul: str = "a\\0😀"\n    print(len("你好😀"))\n    print(len(empty))\n    print(len(ascii))\n'
			'    print(len(nul))\n    print(len(state.read()))\n    print(state.calls)\n    print(state.size())\n'
			'    print(len(text()))\n    print(size("你好😀"))\n    print(len(values[0]))\n'
			'    print(len(mapping[1]))\n    print(len(state.text + "x"))\n'
			'    print(len(state.text if True else ascii))\n'
		)
		with tempfile.TemporaryDirectory() as directory:
			python = subprocess.run([sys.executable, "-c", source + "\nmain()\n"], capture_output=True, text=True, timeout=30)
			self.assertEqual(python.returncode, 0, python.stderr)
			result = transpile_source(source, "unicode-length.py", TranspileOptions(veroscript_executable=executable))
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			self.assertEqual(result.source.count('import "string.vs";'), 1)
			self.assertIn("bytes.get(index)", result.source)
			path = Path(directory) / "generated.vs"
			path.write_text(result.source, encoding="utf-8", newline="\n")
			vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
			self.assertEqual(vm.returncode, 0, vm.stderr)
			self.assertEqual(vm.stdout, python.stdout)

	def test_container_and_literal_lengths_do_not_import_string_runtime(self) -> None:
		source = (
			'def main() -> None:\n    values: list[int] = [1, 2]\n    mapping: dict[int, int] = {1: 3}\n'
			'    entries: set[int] = {1, 2}\n    print(len(values))\n    print(len(mapping))\n'
			'    print(len(entries))\n    print(len("abc"))\n    print(len("你好😀"))\n'
		)
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "string.vs").write_text("export int unrelated = 7;\n", encoding="utf-8")
			result = transpile_source(source, "lengths.py", TranspileOptions(validate_generated_source=False, generated_source_path=str(root / "lengths.vs")))
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			self.assertNotIn('import "string.vs"', result.source)
			self.assertIn("len(values)", result.source)
			self.assertIn("print(3)", result.source)
			self.assertTrue(transpile_source(source, "string.py", TranspileOptions(validate_generated_source=False)).succeeded)

	def test_string_length_rejects_unknown_types_and_dependency_shadowing(self) -> None:
		source = 'def main() -> None:\n    value: str = "你好"\n    print(len(value))\n'
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for name in (("string.vs", "String.vs", "STRING.vs") if os.name == "nt" else ("string.vs",)):
				generated = root / name
				result = transpile_source(source, "text.py", TranspileOptions(validate_generated_source=False, generated_source_path=str(generated)))
				self.assertFalse(result.succeeded)
				self.assertTrue(any("shadows" in item.message for item in result.diagnostics))
			if os.name != "nt":
				self.assertTrue(transpile_source(source, "text.py", TranspileOptions(validate_generated_source=False, generated_source_path=str(root / "String.vs"))).succeeded)
			(root / "string.vs").write_text("export int unrelated = 7;\n", encoding="utf-8")
			result = transpile_source(source, "text.py", TranspileOptions(validate_generated_source=False, generated_source_path=str(root / "text.vs")))
			self.assertFalse(result.succeeded)
			self.assertTrue(any("shadows" in item.message for item in result.diagnostics))
		unknown = 'def main() -> None:\n    mapping: dict[int, str] = {1: "text"}\n    print(len(mapping.get(1)))\n'
		result = transpile_source(unknown, "unknown.py", TranspileOptions(validate_generated_source=False))
		self.assertFalse(result.succeeded)
		self.assertTrue(any("annotated local" in item.message for item in result.diagnostics))

	def test_project_string_lengths_use_imported_declarations_and_reject_shadowing(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "pkg"
			package.mkdir()
			(package / "__init__.py").write_text("", encoding="utf-8")
			model = package / "model.py"
			service = package / "service.py"
			model.write_text('class Payload:\n    text: str = "你好😀"\ndef make_text() -> str:\n    return "é"\n', encoding="utf-8")
			service.write_text('from .model import Payload, make_text\ndef main() -> None:\n    value: Payload = Payload()\n    print(len(value.text))\n    print(len(make_text()))\n', encoding="utf-8")
			result = transpile_files((model, service), TranspileProjectOptions(veroscript_executable=executable), project_root=root)
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			generated = root / "generated"
			for entry in result.files:
				path = generated / entry.output_name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_text(entry.source, encoding="utf-8", newline="\n")
			vm = subprocess.run([executable, "--backend=vm", "--allow-project-wide-access", "-I", str(generated), str(generated / "pkg/service.vs")], capture_output=True, text=True, timeout=30)
			self.assertEqual(vm.returncode, 0, vm.stderr)
			self.assertEqual(vm.stdout, "3\n2\n")
			shadow = package / ("String.py" if os.name == "nt" else "string.py")
			shadow.write_text('def unrelated() -> int:\n    return 1\n', encoding="utf-8")
			result = transpile_files((model, service, shadow), TranspileProjectOptions(validate_generated_sources=False), project_root=root)
			self.assertFalse(result.succeeded)
			self.assertTrue(any("shadows" in item.message for item in result.diagnostics))

	def test_core_builtin_collisions_are_diagnosed_without_rewriting_methods(self) -> None:
		for name, parameters, invocation in (
			("len", "value: str", 'len("abc")'),
			("abs", "value: int", "abs(-3)"),
			("min", "left: int, right: int", "min(1, 2)"),
			("max", "left: int, right: int", "max(1, 2)"),
		):
			with self.subTest(name=name):
				source = f"def {name}({parameters}) -> int:\n    return 77\ndef main() -> None:\n    print({invocation})\n"
				result = transpile_source(source, "shadow.py", TranspileOptions(validate_generated_source=False))
				self.assertFalse(result.succeeded)
				self.assertTrue(any("intercepted" in item.message for item in result.diagnostics))
		for source in (
			"def print(value: int) -> None:\n    pass\ndef main() -> None:\n    print(7)\n",
			"def main() -> None:\n    len: int = 7\n    print(len(1))\n",
			"def length(len: int) -> int:\n    return len(1)\n",
			"from typing import Any as len\ndef main() -> None:\n    print(len(1))\n",
		):
			result = transpile_source(source, "shadow.py", TranspileOptions(validate_generated_source=False))
			self.assertFalse(result.succeeded)
			self.assertTrue(any("intercepted" in item.message for item in result.diagnostics))
		source = "class Box:\n    def len(self) -> int:\n        return 77\ndef str(value: int) -> int:\n    return 77\ndef main() -> None:\n    value: Box = Box()\n    print(value.len())\n    print(str(1))\n"
		result = transpile_source(source, "methods.py", TranspileOptions(validate_generated_source=False))
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		self.assertNotIn('import "string.vs"', result.source)
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			result = transpile_source(source, "methods.py", TranspileOptions(veroscript_executable=executable))
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			with tempfile.TemporaryDirectory() as directory:
				path = Path(directory) / "methods.vs"
				path.write_text(result.source, encoding="utf-8", newline="\n")
				vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
				self.assertEqual(vm.returncode, 0, vm.stderr)
				self.assertEqual(vm.stdout, "77\n77\n")

	def test_class_bindings_cannot_be_intercepted_as_core_builtins(self) -> None:
		for name, parameters, invocation in (
			("len", "value: str", 'len("abc")'),
			("abs", "value: int", "abs(-3)"),
			("min", "left: int, right: int", "min(1, 2)"),
			("max", "left: int, right: int", "max(1, 2)"),
		):
			with self.subTest(name=name):
				source = (
					f"class {name}:\n    def __init__(self, {parameters}) -> None:\n"
					f"        print(77)\ndef main() -> None:\n    {invocation}\n"
				)
				result = transpile_source(source, "class-shadow.py", TranspileOptions(validate_generated_source=False))
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(any("intercepted" in item.message for item in result.diagnostics))

	def test_class_keywords_cannot_discard_metaclass_or_base_hook_effects(self) -> None:
		for declaration, expected in (
			(
				"class _Meta(type):\n"
				"    def __new__(cls, name, bases, namespace):\n"
				"        print(41)\n        return super().__new__(cls, name, bases, namespace)\n"
				"class Box(metaclass=_Meta):\n    value: int = 7\n",
				"41\n7\n",
			),
			(
				"class _Base:\n"
				"    def __init_subclass__(cls, flag: int) -> None:\n        print(flag)\n"
				"class Box(_Base, flag=42):\n    value: int = 7\n",
				"42\n7\n",
			),
		):
			with self.subTest(declaration=declaration):
				source = declaration + "def main() -> None:\n    box: Box = Box()\n    print(box.value)\n"
				python = subprocess.run([sys.executable, "-c", source + "\nmain()\n"], capture_output=True, text=True, timeout=30)
				self.assertEqual(python.returncode, 0, python.stderr)
				self.assertEqual(python.stdout, expected)
				result = transpile_source(source, "class-keywords.py", TranspileOptions(validate_generated_source=False))
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(any("class keyword arguments" in item.message for item in result.diagnostics))
		source = "class Box:\n    value: int = 7\ndef main() -> None:\n    box: Box = Box()\n    print(box.value)\n"
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		result = transpile_source(source, "ordinary-class.py", TranspileOptions(validate_generated_source=bool(executable), veroscript_executable=executable))
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		if executable and Path(executable).is_file():
			with tempfile.TemporaryDirectory() as directory:
				path = Path(directory) / "ordinary-class.vs"
				path.write_text(result.source, encoding="utf-8", newline="\n")
				vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
				self.assertEqual(vm.returncode, 0, vm.stderr)
				self.assertEqual(vm.stdout, "7\n")

	def test_project_str_bindings_require_a_declaration_or_import_in_the_current_module(self) -> None:
		for declaration in (
			"class str:\n    def __init__(self, value: int) -> None:\n        print(77)\n",
			"def str(value: int) -> int:\n    print(77)\n    return value\n",
		):
			with self.subTest(declaration=declaration), tempfile.TemporaryDirectory() as directory:
				root = Path(directory)
				model = root / "model.py"
				service = root / "service.py"
				model.write_text(declaration + "def helper() -> int:\n    return 3\n", encoding="utf-8", newline="\n")
				service.write_text("from model import helper\ndef main() -> None:\n    str(1)\n    print(helper())\n", encoding="utf-8", newline="\n")
				python = subprocess.run([sys.executable, "-c", "from service import main; main()"], cwd=root, capture_output=True, text=True, timeout=30)
				self.assertEqual(python.returncode, 0, python.stderr)
				self.assertEqual(python.stdout, "3\n")
				result = transpile_files((model, service), TranspileProjectOptions(validate_generated_sources=False), project_root=root)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.files, ())
				self.assertTrue(any("builtin str conversion" in item.message for item in result.diagnostics))
				service.write_text("from model import str, helper\ndef main() -> None:\n    str(1)\n    print(helper())\n", encoding="utf-8", newline="\n")
				python = subprocess.run([sys.executable, "-c", "from service import main; main()"], cwd=root, capture_output=True, text=True, timeout=30)
				self.assertEqual(python.returncode, 0, python.stderr)
				self.assertEqual(python.stdout, "77\n3\n")
				executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
				result = transpile_files((model, service), TranspileProjectOptions(validate_generated_sources=bool(executable), veroscript_executable=executable), project_root=root)
				self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
				if executable and Path(executable).is_file():
					output = root / "generated"
					output.mkdir()
					for generated in result.files:
						(output / generated.output_name).write_text(generated.source, encoding="utf-8", newline="\n")
					vm = subprocess.run([executable, "--backend=vm", "--allow-project-wide-access", "-I", str(output), str(output / "service.vs")], capture_output=True, text=True, timeout=30)
					self.assertEqual(vm.returncode, 0, vm.stderr)
					self.assertEqual(vm.stdout, python.stdout)

	def test_project_import_aliases_cannot_call_a_different_declaration(self) -> None:
		for declaration, imported_name in (
			("def second(value: int) -> int:\n    print(77)\n    return value\n", "second"),
			("class str:\n    def __init__(self, value: int) -> None:\n        print(77)\n", "str"),
		):
			for relative in (False, True):
				with self.subTest(imported_name=imported_name, relative=relative), tempfile.TemporaryDirectory() as directory:
					root = Path(directory)
					package = root / "sample"
					package.mkdir()
					(package / "__init__.py").write_text("", encoding="utf-8")
					model = package / "model.py"
					service = package / "service.py"
					model.write_text(declaration + "def first(value: int) -> int:\n    print(42)\n    return value\n", encoding="utf-8", newline="\n")
					module_name = ".model" if relative else "sample.model"
					service.write_text(f"from {module_name} import first as {imported_name}\ndef main() -> None:\n    {imported_name}(1)\n    print(3)\n", encoding="utf-8", newline="\n")
					python = subprocess.run([sys.executable, "-c", "from sample.service import main; main()"], cwd=root, capture_output=True, text=True, timeout=30)
					self.assertEqual(python.returncode, 0, python.stderr)
					self.assertEqual(python.stdout, "42\n3\n")
					result = transpile_files((model, service), TranspileProjectOptions(validate_generated_sources=False), project_root=root)
					self.assertFalse(result.succeeded)
					self.assertEqual(result.files, ())
					self.assertTrue(any("renaming project imports" in item.message for item in result.diagnostics))
		for project_import in (
			"import sample.model as renamed",
			"import typing as t, sample.model as renamed",
			"from . import model as renamed",
		):
			with self.subTest(project_import=project_import), tempfile.TemporaryDirectory() as directory:
				root = Path(directory)
				package = root / "sample"
				package.mkdir()
				(package / "__init__.py").write_text("", encoding="utf-8")
				model = package / "model.py"
				service = package / "service.py"
				model.write_text("def first(value: int) -> int:\n    return value\n", encoding="utf-8", newline="\n")
				service.write_text(f"{project_import}\ndef main() -> None:\n    pass\n", encoding="utf-8", newline="\n")
				python = subprocess.run([sys.executable, "-c", "from sample.service import main; main()"], cwd=root, capture_output=True, text=True, timeout=30)
				self.assertEqual(python.returncode, 0, python.stderr)
				result = transpile_files((model, service), TranspileProjectOptions(validate_generated_sources=False), project_root=root)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.files, ())
				self.assertTrue(any("renaming project imports" in item.message for item in result.diagnostics))

	def test_project_imports_keep_original_names_and_metadata_aliases(self) -> None:
		for project_import in (
			"from model import first",
			"from model import first as first",
			"import model\nfrom model import first",
			"import model as model\nfrom model import first",
		):
			with self.subTest(project_import=project_import), tempfile.TemporaryDirectory() as directory:
				root = Path(directory)
				model = root / "model.py"
				service = root / "service.py"
				model.write_text("def first(value: int) -> int:\n    print(42)\n    return value\n", encoding="utf-8", newline="\n")
				service.write_text(f"import typing as t\nfrom typing import List as List\n{project_import}\ndef main() -> None:\n    values: t.List[int] = [1]\n    first(values[0])\n    print(3)\n", encoding="utf-8", newline="\n")
				python = subprocess.run([sys.executable, "-c", "from service import main; main()"], cwd=root, capture_output=True, text=True, timeout=30)
				self.assertEqual(python.returncode, 0, python.stderr)
				self.assertEqual(python.stdout, "42\n3\n")
				executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
				result = transpile_files((model, service), TranspileProjectOptions(validate_generated_sources=bool(executable), veroscript_executable=executable), project_root=root)
				self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
				if executable and Path(executable).is_file():
					output = root / "generated"
					output.mkdir()
					for generated in result.files:
						(output / generated.output_name).write_text(generated.source, encoding="utf-8", newline="\n")
					vm = subprocess.run([executable, "--backend=vm", "--allow-project-wide-access", "-I", str(output), str(output / "service.vs")], capture_output=True, text=True, timeout=30)
					self.assertEqual(vm.returncode, 0, vm.stderr)
					self.assertEqual(vm.stdout, python.stdout)

	def test_safe_constant_class_defaults_keep_existing_behavior(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		source = (
			"from enum import IntEnum\nclass Mode(IntEnum):\n    A = 1\n"
			'class Box:\n    mode: Mode = Mode.A\n    text: str = "a" + "b"\n'
			"    fraction: float = 1.25 + 2.5\n"
			"def main() -> None:\n    box: Box = Box()\n    print(int(box.mode))\n"
			"    print(box.text)\n    print(1 if box.fraction == 3.75 else 0)\n"
		)
		with tempfile.TemporaryDirectory() as directory:
			python = subprocess.run([sys.executable, "-c", source + "\nmain()\n"], capture_output=True, text=True, timeout=30)
			self.assertEqual(python.returncode, 0, python.stderr)
			self.assertEqual(python.stdout, "1\nab\n1\n")
			result = transpile_source(source, "constant-defaults.py", TranspileOptions(veroscript_executable=executable))
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			path = Path(directory) / "generated.vs"
			path.write_text(result.source, encoding="utf-8", newline="\n")
			vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
			self.assertEqual(vm.returncode, 0, vm.stderr)
			self.assertEqual(vm.stdout, python.stdout)

	def test_class_defaults_reject_shared_or_repeated_evaluation(self) -> None:
		for source in (
			"class Item:\n    values: list[int] = [0]\n",
			"class Item:\n    values: dict[int, int] = {1: 0}\n",
			"class Item:\n    values: set[int] = {0}\n",
			"class Box:\n    value: int = 0\nclass Item:\n    box: Box = Box()\n",
			"def make() -> int:\n    print(42)\n    return 7\nclass Item:\n    value: int = make()\n",
			"seed: int = 7\nclass Item:\n    value: int = seed\n",
			"class Item:\n    value: int = 1 << -1\n",
			"class Item:\n    value: int = 1 % 0\n",
		):
			with self.subTest(source=source):
				result = transpile_source(source, "class-default.py", TranspileOptions(validate_generated_source=False))
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(any("immutable scalar literal" in item.message and "__init__" in item.message for item in result.diagnostics))

	def test_instance_initializers_keep_mutable_values_separate(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		source = (
			"class Box:\n    values: list[int]\n    number: int = 1 + 2\n    fraction: float = -1.5\n"
			"    def __init__(self) -> None:\n        values: list[int] = [0]\n        self.values = values\n"
			"def main() -> None:\n    first: Box = Box()\n    second: Box = Box()\n"
			"    first.values[0] = 7\n    print(first.values[0])\n    print(second.values[0])\n"
			"    print(first.number)\n"
		)
		with tempfile.TemporaryDirectory() as directory:
			python = subprocess.run([sys.executable, "-c", source + "\nmain()\n"], capture_output=True, text=True, timeout=30)
			self.assertEqual(python.returncode, 0, python.stderr)
			self.assertEqual(python.stdout, "7\n0\n3\n")
			result = transpile_source(source, "instance-defaults.py", TranspileOptions(veroscript_executable=executable))
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			path = Path(directory) / "generated.vs"
			path.write_text(result.source, encoding="utf-8", newline="\n")
			vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
			self.assertEqual(vm.returncode, 0, vm.stderr)
			self.assertEqual(vm.stdout, python.stdout)

	def test_attribute_assignment_order_matches_python(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		prefix = (
			"class Box:\n    value: int = 0\n"
			"class State:\n    events: int = 0\n    box: Box\n"
			"    def __init__(self) -> None:\n        self.box = Box()\n"
			"    def receiver(self) -> Box:\n        self.events = self.events * 10 + 1\n        return self.box\n"
			"    def value(self) -> int:\n        self.events = self.events * 10 + 2\n        return 5\n"
			"    def replace(self) -> int:\n        self.events = self.events * 10 + 2\n        self.box = Box()\n        return 7\n"
			"def main() -> None:\n    state: State = State()\n"
		)
		for statement in (
			"state.receiver().value = state.value()",
			"state.box.value = state.replace()",
			"state.receiver().value = state.replace()",
			"state.receiver().value += state.value()",
		):
			with self.subTest(statement=statement), tempfile.TemporaryDirectory() as directory:
				source = prefix + f"    {statement}\n    print(state.events)\n    print(state.box.value)\n"
				python = subprocess.run([sys.executable, "-c", source + "\nmain()\n"], capture_output=True, text=True, timeout=30)
				self.assertEqual(python.returncode, 0, python.stderr)
				result = transpile_source(source, "attribute-order.py", TranspileOptions(veroscript_executable=executable))
				self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
				path = Path(directory) / "generated.vs"
				path.write_text(result.source, encoding="utf-8", newline="\n")
				vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
				self.assertEqual(vm.returncode, 0, vm.stderr)
				self.assertEqual(vm.stdout, python.stdout)

	def test_sequence_indices_and_assignment_order_match_python(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		sources = (
			"def main() -> None:\n"
			"    values: list[int] = [10, 20, 30]\n"
			"    index: int = -2\n"
			"    print(values[-1])\n    print(values[index])\n"
			"    values[index] = 7\n    values[-1] += 4\n"
			"    print(values[1])\n    print(values[2])\n"
			"    nested: list[list[int]] = [[1, 2], [3, 4]]\n"
			"    nested[-1][-2] = 9\n    print(nested[1][0])\n",
			"def main() -> None:\n"
			"    values: dict[int, int] = {-1: 3, 1: 4}\n"
			"    index: int = -1\n    print(values[index])\n"
			"    values[index] = 7\n    values[-1] += 5\n"
			"    print(values[-1])\n    print(values[1])\n",
			"class State:\n    values: list[int]\n    events: int = 0\n"
			"    def __init__(self) -> None:\n        values: list[int] = [10, 20]\n        self.values = values\n"
			"    def base(self) -> list[int]:\n        self.events = self.events * 10 + 1\n        return self.values\n"
			"    def index(self) -> int:\n        self.events = self.events * 10 + 2\n        return -1\n"
			"    def value(self) -> int:\n        self.events = self.events * 10 + 3\n        return 7\n"
			"def main() -> None:\n    state: State = State()\n"
			"    print(state.base()[state.index()])\n    print(state.events)\n"
			"    state.events = 0\n    state.base()[state.index()] = state.value()\n"
			"    print(state.events)\n    print(state.values[-1])\n"
			"    state.events = 0\n    state.base()[state.index()] += state.value()\n"
			"    print(state.events)\n    print(state.values[-1])\n",
		)
		for index, source in enumerate(sources):
			with self.subTest(case=index), tempfile.TemporaryDirectory() as directory:
				python = subprocess.run([sys.executable, "-c", source + "\nmain()\n"], capture_output=True, text=True, timeout=30)
				self.assertEqual(python.returncode, 0, python.stderr)
				result = transpile_source(source, f"indices-{index}.py", TranspileOptions(veroscript_executable=executable))
				self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
				path = Path(directory) / "generated.vs"
				path.write_text(result.source, encoding="utf-8", newline="\n")
				vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
				self.assertEqual(vm.returncode, 0, vm.stderr)
				self.assertEqual(vm.stdout, python.stdout)

	def test_string_indexing_requires_unicode_semantics(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		for index in (0, -1):
			with self.subTest(index=index):
				source = f'def main() -> None:\n    text: str = "éa"\n    print(text[{index}])\n'
				result = transpile_source(source, "string-index.py", TranspileOptions(veroscript_executable=executable))
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(any("Python string indexing requires Unicode code point semantics" in item.message for item in result.diagnostics))

	def test_sequence_index_out_of_bounds_still_fails(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		for index in (-3, 2, -2147483648):
			with self.subTest(index=index), tempfile.TemporaryDirectory() as directory:
				source = f"def main() -> None:\n    values: list[int] = [1, 2]\n    print(values[{index}])\n"
				python = subprocess.run([sys.executable, "-c", source + "\nmain()\n"], capture_output=True, text=True, timeout=30)
				self.assertNotEqual(python.returncode, 0)
				self.assertIn("IndexError", python.stderr)
				result = transpile_source(source, "out-of-bounds.py", TranspileOptions(veroscript_executable=executable))
				self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
				path = Path(directory) / "generated.vs"
				path.write_text(result.source, encoding="utf-8", newline="\n")
				vm = subprocess.run([executable, "--backend=vm", str(path)], capture_output=True, text=True, timeout=30)
				self.assertNotEqual(vm.returncode, 0)
				self.assertEqual(vm.stdout, python.stdout)

	def test_deep_expressions_return_diagnostics_without_partial_output(self) -> None:
		terms = max(1400, sys.getrecursionlimit() + 100)
		sources = (
			"def value() -> int:\n    return " + " + ".join(["1"] * terms) + "\n",
			"from enum import IntEnum\nclass Mode(IntEnum):\n    VALUE = " + " + ".join(["1"] * terms) + "\n",
		)
		for source in sources:
			with self.subTest(source=source[:30]):
				result = transpile_source(source, "deep-expression.py", TranspileOptions(validate_generated_source=False))
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertEqual(result.source_map_json, "")
				self.assertTrue(result.diagnostics)

	def test_declared_types_shadow_numeric_marker_names(self) -> None:
		for name in ("Int8", "Float32"):
			with self.subTest(type_name=name):
				result = transpile_source(
					f"class {name}:\n    value: int = 7\n\n"
					f"def identity(value: {name}) -> {name}:\n    return value\n",
					"nominal-types.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertTrue(result.succeeded, result.diagnostics)
				self.assertIn(f"{name} identity({name} value)", result.source)

	def test_declared_classes_and_functions_shadow_builtin_casts(self) -> None:
		sources = (
			"class str:\n"
			"    value: int = 0\n"
			"    def __init__(self, value: int) -> None:\n        self.value = value\n"
			"def make() -> str:\n    return str(7)\n",
			"def str(value: int) -> int:\n    return value\n"
			"def make() -> int:\n    return str(7)\n",
		)
		for source in sources:
			with self.subTest(source=source):
				result = transpile_source(
					source,
					"shadowed-cast.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertTrue(result.succeeded, result.diagnostics)
				self.assertIn("return str(7);", result.source)
				self.assertNotIn("cast<string>", result.source)

	def test_scalar_and_nominal_type_arguments_fail_closed(self) -> None:
		for annotation in ("int[str]", "Float32[int]", "Point[int]"):
			with self.subTest(annotation=annotation):
				result = transpile_source(
					"class Point:\n    value: int = 7\n\n"
					f"def identity(value: {annotation}) -> int:\n    return 7\n",
					"invalid-type-arguments.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(any("type arguments" in item.message for item in result.diagnostics))

	def test_builtin_str_conversion_fails_without_core_validation(self) -> None:
		for expression in ("str()", "str(7)", "str(7.0)", "str(True)", "str('value')"):
			for validate in (False, True):
				with self.subTest(expression=expression, validate=validate):
					result = transpile_source(
						f"def value() -> str:\n    return {expression}\n",
						"unsupported-str.py",
						TranspileOptions(validate_generated_source=validate),
					)
					self.assertFalse(result.succeeded)
					self.assertEqual(result.source, "")
					self.assertTrue(any(
						item.code == "PYT2001" and "builtin str conversion" in item.message
						for item in result.diagnostics
					))

	def test_empty_and_nested_type_arguments_fail_closed(self) -> None:
		for annotation in ("int[()]", "Item[()]", "list[int[()]]", "list[Item[()]]"):
			with self.subTest(annotation=annotation):
				result = transpile_source(
					"class Item:\n    value: int = 7\n\n"
					f"def identity(value: {annotation}) -> int:\n    return 7\n",
					"empty-type-arguments.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(any("type" in item.message for item in result.diagnostics))

	def test_dataclass_runtime_behavior_is_not_silently_discarded(self) -> None:
		sources = (
			"from dataclasses import dataclass\n@dataclass\nclass Item:\n    value: int = 7\n",
			"import dataclasses\n@dataclasses.dataclass\nclass Item:\n    value: int = 7\n",
			"from dataclasses import dataclass\n@dataclass\nclass Item:\n"
			"    value: int = 7\n"
			"    def __init__(self, value: int) -> None:\n        self.value = value\n",
		)
		for source in sources:
			with self.subTest(source=source):
				result = transpile_source(
					source,
					"dataclass.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(any("dataclass" in item.message for item in result.diagnostics))

	def test_surrogate_string_literals_fail_before_emission(self) -> None:
		for escape in ("\\ud800", "\\udcff", "\\ud83d\\ude00"):
			for validate in (False, True):
				with self.subTest(escape=escape, validate=validate):
					result = transpile_source(
						f'def value() -> str:\n    return "{escape}"\n',
						"surrogate-literal.py",
						TranspileOptions(validate_generated_source=validate),
					)
					self.assertFalse(result.succeeded)
					self.assertEqual(result.source, "")
					self.assertTrue(any(
						item.code == "PYT2001" and "surrogate" in item.message
						for item in result.diagnostics
					))
		valid = transpile_source(
			'def value() -> str:\n    return "\\U0001f600"\n',
			"unicode-literal.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(valid.succeeded, valid.diagnostics)
		self.assertIn("\U0001f600", valid.source)
		valid.source.encode("utf-8")

	def test_local_annotations_do_not_reload_catalogs_for_each_declaration(self) -> None:
		from veroscript_python_frontend import localization

		options = TranspileOptions(validate_generated_source=False)
		with patch.object(localization, "_read_catalog", wraps=localization._read_catalog) as reads:
			small = transpile_source("def value() -> int:\n    v: list[int] = [1]\n    return v[0]\n", options=options)
			self.assertTrue(small.succeeded)
			baseline = reads.call_count
			reads.reset_mock()
			source = "def value() -> int:\n" + "".join(
				f"    v{index}: list[int] = [{index}]\n" for index in range(200)
			) + "    return v199[0]\n"
			large = transpile_source(source, options=options)
			self.assertTrue(large.succeeded)
			self.assertEqual(reads.call_count, baseline)
			self.assertIn("array<int> v199 = {199};", large.source)

	@unittest.skipUnless(sys.version_info >= (3, 12), "PEP 695 syntax requires Python 3.12")
	def test_generic_function_does_not_emit_a_non_generic_function(self) -> None:
		result = transpile_source(
			"def answer[T]() -> int:\n    return 42\n",
			"generic.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(result.succeeded)
		self.assertEqual(result.source, "")
		self.assertTrue(any(item.code == "PYF0006" for item in result.diagnostics))

	def test_invalid_validation_timeouts_fail_closed(self) -> None:
		for timeout in (0.0, -1.0, float("nan"), float("inf")):
			with self.subTest(timeout=timeout):
				result = transpile_source(
					"def value() -> int:\n    return 1\n",
					"timeout.py",
					TranspileOptions(timeout_seconds=timeout),
				)
				self.assertFalse(result.succeeded)
				self.assertTrue(
					any(item.code == "PYT2004" for item in result.diagnostics)
				)

	def test_typed_subset_is_emitted(self) -> None:
		result = transpile_source(
			SOURCE,
			"sample.py",
			TranspileOptions(validate_generated_source=False, generated_source_path="sample.vs"),
		)
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		self.assertIn("class Counter", result.source)
		self.assertIn("this.value += amount;", result.source)
		self.assertIn("int main()", result.source)
		self.assertIn("void finished()", result.source)
		self.assertIn("\t{}", result.source)
		self.assertIn('"generated": "sample.vs"', result.source_map_json)

	def test_dynamic_constructs_fail_closed(self) -> None:
		result = transpile_source(
			"def bad(value: int) -> int:\n    return eval('value')\n",
			"bad.py",
			TranspileOptions(validate_generated_source=False),
		)
		comprehension = transpile_source(
			"def bad(values: list[int]) -> list[int]:\n    return [x for x in values]\n",
			"bad.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(result.succeeded)
		self.assertFalse(comprehension.succeeded)
		self.assertTrue(any(item.code == "PYT2001" for item in comprehension.diagnostics))
		decorated = transpile_source(
			"@dataclass(frozen=True)\nclass Frozen:\n    value: int\n",
			"decorated.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(decorated.succeeded)

	def test_invalid_veroscript_identifiers_fail_before_emission(self) -> None:
		sources = {
			"new do keyword": "def do() -> int:\n    return 1\n",
			"keyword parameter": "def value(do: int) -> int:\n    return do\n",
			"keyword class": "class array:\n    value: int\n",
			"keyword field": "class Item:\n    do: int\n",
			"keyword method": (
				"class Item:\n"
				"    def do(self) -> int:\n        return 1\n"
			),
			"keyword local": (
				"def value() -> int:\n"
				"    do: int = 1\n"
				"    return do\n"
			),
			"non-ASCII identifier": "def café() -> int:\n    return 1\n",
		}
		for name, source in sources.items():
			with self.subTest(name=name):
				result = transpile_source(
					source,
					f"{name}.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(
					any(
						"valid VeroScript identifier" in item.message
						for item in result.diagnostics
					),
					[item.format() for item in result.diagnostics],
				)

	def test_statement_nesting_limit_is_enforced_without_external_validation(self) -> None:
		parser_path = (
			ROOT.parents[2]
			/ "VeroScript"
			/ "Private"
			/ "VeroScriptParser.cpp"
		)
		parser_source = parser_path.read_text(encoding="utf-8")
		match = re.search(
			r"MaxStatementParseDepth\s*=\s*(\d+)", parser_source
		)
		self.assertIsNotNone(match)
		assert match is not None
		self.assertEqual(
			transpiler._MAX_VEROSCRIPT_STATEMENT_NESTING,
			int(match.group(1)),
		)

		def nested_source(depth: int) -> str:
			return (
				"def value() -> None:\n"
				+ "".join(
					"    " * (index + 1) + "if True:\n"
					for index in range(depth)
				)
				+ "    " * (depth + 1)
				+ "print(1)\n"
			)

		boundary_depth = transpiler._MAX_VEROSCRIPT_STATEMENT_NESTING - 1
		boundary = transpile_source(
			nested_source(boundary_depth),
			"boundary.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(
			boundary.succeeded,
			[item.format() for item in boundary.diagnostics],
		)
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated_boundary = transpile_source(
				nested_source(boundary_depth),
				"validated-boundary.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated_boundary.succeeded,
				[item.format() for item in validated_boundary.diagnostics],
			)

		too_deep = transpile_source(
			nested_source(boundary_depth + 1),
			"too-deep.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(too_deep.succeeded)
		self.assertEqual(too_deep.source, "")
		self.assertTrue(
			any(
				"statement nested too deeply" in item.message
				for item in too_deep.diagnostics
			),
			[item.format() for item in too_deep.diagnostics],
		)

	def test_private_types_cannot_leak_into_public_output(self) -> None:
		source = (
			"class _Hidden:\n"
			"    value: int\n\n"
			"def expose(value: _Hidden) -> int:\n"
			"    return value.value\n"
		)
		single = transpile_source(
			source,
			"private-type.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(single.succeeded)
		self.assertEqual(single.source, "")

		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			models = root / "models.py"
			service = root / "service.py"
			models.write_text(
				"class _Hidden:\n    value: int\n",
				encoding="utf-8",
			)
			service.write_text(
				"from models import _Hidden\n\n"
				"def expose(value: _Hidden) -> int:\n"
				"    return value.value\n",
				encoding="utf-8",
			)
			project = transpile_files(
				(models, service),
				TranspileProjectOptions(validate_generated_sources=False),
				project_root=root,
			)
		self.assertFalse(project.succeeded)
		self.assertEqual(project.files, ())

		private_call = transpile_source(
			"def _helper() -> int:\n"
			"    return 1\n\n"
			"def expose() -> int:\n"
			"    return _helper()\n",
			"private-call.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(private_call.succeeded)
		self.assertEqual(private_call.source, "")

	def test_numeric_literal_boundaries_fail_closed_without_validation(self) -> None:
		invalid_sources = {
			"non-finite float": "def value() -> float:\n    return 1e309\n",
			"positive integer overflow": (
				"def value() -> int:\n    return 2147483648\n"
			),
			"negative integer overflow": (
				"def value() -> int:\n    return -2147483649\n"
			),
		}
		for name, source in invalid_sources.items():
			with self.subTest(name=name):
				result = transpile_source(
					source,
					f"{name}.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")

		minimum = transpile_source(
			"def value() -> int:\n    return -2147483648\n",
			"minimum.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(
			minimum.succeeded,
			[item.format() for item in minimum.diagnostics],
		)
		self.assertIn("return (-2147483648);", minimum.source)

		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated_minimum = transpile_source(
				"def value() -> int:\n    return -2147483648\n",
				"validated-minimum.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated_minimum.succeeded,
				[item.format() for item in validated_minimum.diagnostics],
			)

	def test_container_literals_require_typed_initializer_context(self) -> None:
		invalid_sources = (
			"def value() -> list[int]:\n    return [1, 2]\n",
			"def value() -> set[int]:\n    return {1, 2}\n",
			'def value() -> dict[str, int]:\n    return {"a": 1}\n',
			"def value(values: list[int]) -> None:\n"
			"    values = [1, 2]\n",
		)
		for source in invalid_sources:
			with self.subTest(source=source):
				result = transpile_source(
					source,
					"container-expression.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")

		typed_initializers = transpile_source(
			"def value() -> int:\n"
			"    values: list[int] = [1, 2]\n"
			"    selected: set[int] = {2, 3}\n"
			'    scores: dict[str, int] = {"answer": 4}\n'
			"    return len(values) + len(selected) + len(scores)\n",
			"typed-initializers.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(
			typed_initializers.succeeded,
			[item.format() for item in typed_initializers.diagnostics],
		)

		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated = transpile_source(
				"def value() -> int:\n"
				"    values: list[int] = [1, 2]\n"
				"    selected: set[int] = {2, 3}\n"
				'    scores: dict[str, int] = {"answer": 4}\n'
				"    return len(values) + len(selected) + len(scores)\n",
				"validated-initializers.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated.succeeded,
				[item.format() for item in validated.diagnostics],
			)

	def test_identity_comparisons_fail_closed_without_optional_types(self) -> None:
		sources = (
			"def value(text: str) -> bool:\n    return text is None\n",
			"class Item:\n"
			"    value: int\n\n"
			"def value(item: Item) -> bool:\n"
			"    return item is not None\n",
		)
		for source in sources:
			with self.subTest(source=source):
				result = transpile_source(
					source,
					"identity.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")
				self.assertTrue(
					any(
						"identity comparison" in item.message
						for item in result.diagnostics
					),
					[item.format() for item in result.diagnostics],
				)

	def test_loop_control_requires_an_enclosing_loop(self) -> None:
		for keyword in ("break", "continue"):
			with self.subTest(keyword=keyword):
				result = transpile_source(
					f"def value() -> None:\n    {keyword}\n",
					f"{keyword}.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")

		valid = transpile_source(
			"def value(limit: int) -> int:\n"
			"    current: int = 0\n"
			"    while current < limit:\n"
			"        current += 1\n"
			"        if current == 2:\n"
			"            continue\n"
			"        if current == 4:\n"
			"            break\n"
			"    return current\n",
			"loop-control.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(valid.succeeded, [item.format() for item in valid.diagnostics])

		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated = transpile_source(
				"def value(limit: int) -> int:\n"
				"    current: int = 0\n"
				"    while current < limit:\n"
				"        current += 1\n"
				"        if current == 2:\n"
				"            continue\n"
				"        if current == 4:\n"
				"            break\n"
				"    return current\n",
				"validated-loop-control.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated.succeeded,
				[item.format() for item in validated.diagnostics],
			)

	def test_enum_expressions_are_folded_to_core_literals(self) -> None:
		source = (
			"from enum import IntFlag\n"
			"class Mode(IntFlag):\n"
			"    READ = 1 << 0\n"
			"    WRITE = 1 << 1\n"
			"    ALL = READ | WRITE\n"
			"    LARGE = 1 << 40\n"
			"    NEGATIVE = -(1 << 40)\n"
			"    MINIMUM = -(1 << 63)\n"
		)
		result = transpile_source(
			source,
			"enum.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		self.assertIn("READ = 1,", result.source)
		self.assertIn("WRITE = 2,", result.source)
		self.assertIn("ALL = 3,", result.source)
		self.assertIn("LARGE = 1099511627776i64,", result.source)
		self.assertIn("NEGATIVE = -1099511627776i64,", result.source)
		self.assertIn("MINIMUM = -9223372036854775808i64", result.source)

		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated = transpile_source(
				source,
				"validated-enum.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated.succeeded,
				[item.format() for item in validated.diagnostics],
			)

		invalid = transpile_source(
			"from enum import IntEnum\n"
			"class Mode(IntEnum):\n"
			"    TOO_LARGE = 1 << 63\n",
			"invalid-enum.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(invalid.succeeded)
		self.assertEqual(invalid.source, "")

		plain_enum = transpile_source(
			"from enum import Enum\n"
			"class Mode(Enum):\n"
			"    VALUE = 1\n",
			"plain-enum.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(plain_enum.succeeded)
		self.assertEqual(plain_enum.source, "")

	def test_self_is_rewritten_only_for_an_instance_receiver(self) -> None:
		top_level = transpile_source(
			"self: int = 1\n"
			"def value(self: int) -> int:\n"
			"    return self\n",
			"self-name.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(
			top_level.succeeded,
			[item.format() for item in top_level.diagnostics],
		)
		self.assertIn("int self = 1;", top_level.source)
		self.assertIn("int value(int self)", top_level.source)
		self.assertIn("return self;", top_level.source)

		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated = transpile_source(
				"self: int = 1\n"
				"def value(self: int) -> int:\n"
				"    return self\n",
				"validated-self-name.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated.succeeded,
				[item.format() for item in validated.diagnostics],
			)

		for statement in ("self = other", "self: int = other"):
			with self.subTest(statement=statement):
				receiver_write = transpile_source(
					"class Item:\n"
					"    value: int\n"
					"    def replace(self, other: int) -> None:\n"
					f"        {statement}\n",
					"receiver-write.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(receiver_write.succeeded)
				self.assertEqual(receiver_write.source, "")

	def test_none_is_only_emitted_as_a_void_return(self) -> None:
		valid = transpile_source(
			"def finished() -> None:\n    return None\n",
			"void-none.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(valid.succeeded, [item.format() for item in valid.diagnostics])
		self.assertIn("return;", valid.source)
		self.assertNotIn("nullptr", valid.source)

		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated = transpile_source(
				"def finished() -> None:\n    return None\n",
				"validated-void-none.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated.succeeded,
				[item.format() for item in validated.diagnostics],
			)

		invalid = transpile_source(
			"def value() -> str:\n    return None\n",
			"invalid-none.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(invalid.succeeded)
		self.assertEqual(invalid.source, "")

		invalid_return_shapes = (
			"def value() -> int:\n    return\n",
			"def finished() -> None:\n    return 1\n",
			"class Item:\n"
			"    def __init__(self) -> None:\n"
			"        return 1\n",
		)
		for source in invalid_return_shapes:
			with self.subTest(source=source):
				result = transpile_source(
					source,
					"invalid-return.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")

	def test_non_void_functions_require_complete_return_paths(self) -> None:
		for source in (
			"def value() -> int:\n    pass\n",
			(
				"def value(flag: bool) -> int:\n"
				"    if flag:\n"
				"        return 1\n"
				"    pass\n"
			),
		):
			with self.subTest(source=source):
				result = transpile_source(
					source,
					"missing-return.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertTrue(
					any(item.code == "PYT2001" for item in result.diagnostics)
				)

		complete = transpile_source(
			(
				"def value(flag: bool) -> int:\n"
				"    if flag:\n"
				"        return 1\n"
				"    else:\n"
				"        return 2\n"
			),
			"complete-return.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(
			complete.succeeded,
			[item.format() for item in complete.diagnostics],
		)
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated = transpile_source(
				(
					"def value(flag: bool) -> int:\n"
					"    if flag:\n"
					"        return 1\n"
					"    else:\n"
					"        return 2\n"
				),
				"validated-complete-return.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated.succeeded,
				[item.format() for item in validated.diagnostics],
			)

	def test_local_declarations_fail_closed_before_scope_can_diverge(self) -> None:
		sources = (
			(
				"def value(flag: bool) -> int:\n"
				"    if flag:\n"
				"        result: int = 1\n"
				"    result = 2\n"
				"    return result\n"
			),
			(
				"def value() -> int:\n"
				"    result: int = 1\n"
				"    result: int = 2\n"
				"    return result\n"
			),
			(
				"def value(result: int) -> int:\n"
				"    result: int = 1\n"
				"    return result\n"
			),
		)
		for source in sources:
			with self.subTest(source=source):
				result = transpile_source(
					source,
					"local-scope.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertTrue(
					any(item.code == "PYT2001" for item in result.diagnostics)
				)

		valid = transpile_source(
			(
				"def value(flag: bool) -> int:\n"
				"    result: int = 0\n"
				"    if flag:\n"
				"        result = 1\n"
				"    return result\n"
			),
			"local-scope.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(valid.succeeded, [item.format() for item in valid.diagnostics])
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if executable and Path(executable).is_file():
			validated = transpile_source(
				(
					"def value(flag: bool) -> int:\n"
					"    result: int = 0\n"
					"    if flag:\n"
					"        result = 1\n"
					"    return result\n"
				),
				"validated-local-scope.py",
				TranspileOptions(veroscript_executable=executable),
			)
			self.assertTrue(
				validated.succeeded,
				[item.format() for item in validated.diagnostics],
			)

	def test_class_member_collisions_fail_before_emission(self) -> None:
		result = transpile_source(
			"class Example:\n"
			"    value: int\n"
			"    def value(self) -> int:\n        return 1\n",
			"collision.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(result.succeeded)
		self.assertEqual(result.source, "")
		self.assertTrue(
			any(item.code == "PYF0003" for item in result.diagnostics),
			[item.format() for item in result.diagnostics],
		)

	def test_void_is_only_allowed_as_a_callable_return(self) -> None:
		invalid_sources = {
			"parameter": "def consume(value: None) -> None:\n    pass\n",
			"field": "class Settings:\n    value: None\n",
			"container element": (
				"def consume(values: list[None]) -> None:\n    pass\n"
			),
			"constructor return": (
				"class Settings:\n"
				"    def __init__(self) -> int:\n        return 1\n"
			),
		}
		for name, source in invalid_sources.items():
			with self.subTest(name=name):
				result = transpile_source(
					source,
					f"{name}.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")

		valid_return = transpile_source(
			"def finished() -> None:\n    pass\n",
			"finished.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(
			valid_return.succeeded,
			[item.format() for item in valid_return.diagnostics],
		)
		self.assertIn("void finished()", valid_return.source)

	def test_invalid_receivers_and_decorators_fail_closed(self) -> None:
		sources = {
			"keyword-only receiver": (
				"class Service:\n"
				"    def work(*, self) -> int:\n        return 1\n"
			),
			"decorated constructor": (
				"class Service:\n"
				"    @staticmethod\n"
				"    def __init__() -> None:\n        pass\n"
			),
			"class method": (
				"class Service:\n"
				"    @classmethod\n"
				"    def work(cls) -> int:\n        return 1\n"
			),
			"top-level decorator": (
				"@staticmethod\n"
				"def work(value: int) -> int:\n    return value\n"
			),
		}
		for name, source in sources.items():
			with self.subTest(name=name):
				result = transpile_source(
					source,
					f"{name}.py",
					TranspileOptions(validate_generated_source=False),
				)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.source, "")

	def test_future_annotations_import_is_metadata_only(self) -> None:
		result = transpile_source(
			"from __future__ import annotations\n\ndef main() -> int:\n    return 0\n",
			"future.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])

	def test_python_specific_arithmetic_fails_closed(self) -> None:
		result = transpile_source(
			"def update(value: int) -> int:\n    value //= 2\n    return value\n",
			"arithmetic.py",
			TranspileOptions(validate_generated_source=False),
		)
		self.assertFalse(result.succeeded)

	def test_generated_source_passes_real_compiler(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		result = transpile_source(
			SOURCE,
			"sample.py",
			TranspileOptions(veroscript_executable=executable),
		)
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])

	def test_multi_file_project_preserves_layout_and_imports(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "sample"
			package.mkdir()
			models = package / "models.py"
			service = package / "service.py"
			models.write_text(
				"class User:\n"
				"    name: str = ''\n\n"
				"    def __init__(self, name: str) -> None:\n"
				"        self.name = name\n",
				encoding="utf-8",
			)
			service.write_text(
				"from .models import User\n\n"
				"def display(user: User) -> str:\n"
				"    return user.name\n",
				encoding="utf-8",
			)
			executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
			result = transpile_files(
				(models, service),
				TranspileProjectOptions(
					validate_generated_sources=bool(executable),
					veroscript_executable=executable,
				),
				project_root=root,
			)
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			self.assertEqual(
				[entry.output_name for entry in result.files],
				["sample/models.vs", "sample/service.vs"],
			)
			service_output = result.files[1].source
			self.assertIn('import "sample/models.vs";', service_output)
			self.assertIn("string display(User user)", service_output)
			self.assertIn("veroscript.python-transpiled-project", result.manifest_json)

	def test_project_rejects_exported_variable_symbol_collisions(self) -> None:
		invalid_pairs = (
			("shared: int = 1\n", "shared: int = 2\n"),
			("class Shared:\n    value: int\n", "Shared: int = 2\n"),
		)
		for first_source, second_source in invalid_pairs:
			with self.subTest(
				first_source=first_source,
				second_source=second_source,
			):
				with tempfile.TemporaryDirectory() as directory:
					root = Path(directory)
					first = root / "first.py"
					second = root / "second.py"
					first.write_text(first_source, encoding="utf-8")
					second.write_text(second_source, encoding="utf-8")
					result = transpile_files(
						(first, second),
						TranspileProjectOptions(
							validate_generated_sources=False
						),
						project_root=root,
					)
				self.assertFalse(result.succeeded)
				self.assertEqual(result.files, ())
				self.assertTrue(
					any(
						"project symbol" in item.message
						for item in result.diagnostics
					),
					[item.format() for item in result.diagnostics],
				)

		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			variable = root / "variable.py"
			function = root / "function.py"
			variable.write_text("shared: int = 1\n", encoding="utf-8")
			function.write_text(
				"def shared() -> int:\n    return 2\n",
				encoding="utf-8",
			)
			valid = transpile_files(
				(variable, function),
				TranspileProjectOptions(validate_generated_sources=False),
				project_root=root,
			)
		self.assertTrue(valid.succeeded, [item.format() for item in valid.diagnostics])

	def test_project_relative_import_cannot_escape_its_package(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			models = root / "models.py"
			service = root / "service.py"
			models.write_text("class User:\n    name: str = ''\n", encoding="utf-8")
			service.write_text(
				"from .models import User\n\n"
				"def display(user: User) -> str:\n"
				"    return user.name\n",
				encoding="utf-8",
			)
			result = transpile_files(
				(models, service),
				TranspileProjectOptions(validate_generated_sources=False),
				project_root=root,
			)
			self.assertFalse(result.succeeded)
			self.assertTrue(
				any(
					item.code == "PYT2001"
					and "unresolved project-relative import .models" in item.message
					for item in result.diagnostics
				),
				[item.format() for item in result.diagnostics],
			)

	def test_project_import_paths_are_string_literal_escaped(self) -> None:
		module = parse_source(
			"from generated import value\n\n"
			"def main() -> int:\n"
			"    return 0\n",
			"sample.py",
		)
		result = transpiler._Emitter(
			module,
			TranspileOptions(validate_generated_source=False),
			project_imports={"generated": ('generated/"quoted".vs',)},
		).emit()
		self.assertTrue(
			result.succeeded,
			[item.format() for item in result.diagnostics],
		)
		self.assertIn('import "generated/\\"quoted\\".vs";', result.source)

	def test_multi_file_cli_writes_valid_sources_maps_and_manifest(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "sample"
			package.mkdir()
			models = package / "models.py"
			service = package / "service.py"
			models.write_text(
				"class User:\n"
				"    name: str = ''\n\n"
				"    def __init__(self, name: str) -> None:\n"
				"        self.name = name\n",
				encoding="utf-8",
			)
			service.write_text(
				"from .models import User\n\n"
				"def display(user: User) -> str:\n"
				"    return user.name\n",
				encoding="utf-8",
			)
			output = root / "generated"
			environment = os.environ.copy()
			module_paths = [] if TEST_INSTALLED else [
				str(ROOT / "VeroScriptPythonFrontend" / "python"),
				str(Path(__file__).resolve().parents[1] / "python"),
			]
			if environment.get("PYTHONPATH"):
				module_paths.append(environment["PYTHONPATH"])
			environment["PYTHONPATH"] = os.pathsep.join(module_paths)
			command = [
				sys.executable,
				"-m",
				"veroscript_python_transpiler",
				str(models),
				str(service),
				"--project-root",
				str(root),
				"--output-dir",
				str(output),
			]
			executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
			if executable and Path(executable).is_file():
				command.extend(("--veroscript", executable))
			else:
				command.append("--no-validate")
			completed = subprocess.run(
				command, check=False, capture_output=True, text=True, env=environment
			)
			self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
			for relative in (
				"transpile.manifest.json",
				"sample/models.vs",
				"sample/models.vs.map.json",
				"sample/service.vs",
				"sample/service.vs.map.json",
			):
				self.assertTrue((output / relative).is_file(), relative)


if __name__ == "__main__":
	unittest.main()
