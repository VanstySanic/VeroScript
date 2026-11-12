# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import ModuleType
import unittest
from typing import cast


ROOT = Path(__file__).resolve().parents[2]
TEST_INSTALLED = os.environ.get("VEROSCRIPT_TEST_INSTALLED_PACKAGE") == "1"
if not TEST_INSTALLED:
	sys.path.insert(0, str(ROOT / "VeroScriptPythonFrontend" / "python"))
	sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

from veroscript_python_bindgen import (  # noqa: E402
	bind_module,
	bind_modules,
	generate_binding_project,
	generate_bindings,
	generate_bindings_from_source,
	validate_catalog,
)
from veroscript_python_frontend import parse_source  # noqa: E402


SOURCE = '''
from __future__ import annotations
from dataclasses import dataclass
from enum import IntEnum

class Mode(IntEnum):
    OFF = 0
    READ = 1 << 0
    WRITE = 1 << 1
    ALL = READ | WRITE

@dataclass
class Counter:
    value: int

    def __init__(self, initial: int) -> None:
        self.value = initial

    def add(self, amount: int) -> int:
        return self.value + amount

    @staticmethod
    def twice(value: int) -> int:
        return value * 2

def combine(left: int, right: int) -> int:
    return left + right

def identity_uint32(value: UInt32) -> UInt32:
    return value
'''


class PythonBindGenTests(unittest.TestCase):
	def test_deep_enum_expression_returns_diagnostics(self) -> None:
		terms = max(1400, sys.getrecursionlimit() + 100)
		source = "from enum import IntEnum\nclass Mode(IntEnum):\n    VALUE = " + " + ".join(["1"] * terms) + "\n"
		result = generate_bindings_from_source(source, "deep_enum.py")
		self.assertFalse(result.succeeded)
		self.assertIsNone(result.catalog)
		self.assertTrue(result.diagnostics)

	def test_nested_optional_container_types_fail_with_diagnostics(self) -> None:
		for annotation in (
			"list[int | None]", "set[Optional[str]]", "dict[str, int | None]",
			"dict[Optional[int], str]", "list[dict[str, Optional[int]]]",
			"list[Item | None]", "list[Mode | None]",
		):
			for subject in ("parameter", "return", "field"):
				with self.subTest(annotation=annotation, subject=subject):
					source = "from typing import Optional\nfrom enum import IntEnum\n"
					source += "class Item:\n    value: int\nclass Mode(IntEnum):\n    ONE = 1\n"
					if subject == "parameter":
						source += f"def process(value: {annotation}) -> None:\n    pass\n"
					elif subject == "return":
						source += f"def process() -> {annotation}:\n    pass\n"
					else:
						source += f"class Holder:\n    value: {annotation}\n"
					result = generate_bindings_from_source(source, "optional.py")
					self.assertFalse(result.succeeded)
					self.assertIsNone(result.catalog)
					self.assertEqual(result.catalog_json, "")
					self.assertTrue(any(item.code == "PYB1002" for item in result.diagnostics))

	def test_project_containers_resolve_qualified_types_with_duplicate_leaf_names(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			sources = {
				"one.py": "from enum import IntEnum\nclass Item:\n    value: int\nclass Mode(IntEnum):\n    ONE = 1\n",
				"two.py": "from enum import IntEnum\nclass Item:\n    value: int\nclass Mode(IntEnum):\n    TWO = 2\n",
				"api.py": "import one\nimport two\ndef items(left: list[one.Item], right: list[two.Item]) -> None:\n    pass\ndef modes(left: list[one.Mode], right: list[two.Mode]) -> None:\n    pass\n",
			}
			for name, source in sources.items():
				(root / name).write_text(source, encoding="utf-8")
			result = generate_binding_project([root / name for name in sources], project_root=root)
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			functions = result.catalog["functions"]
			self.assertEqual(functions[0]["paramElementTypeNames"], ["one::Item", "two::Item"])
			self.assertEqual(functions[1]["paramElementTypeNames"], ["one::Mode", "two::Mode"])

	@unittest.skipUnless(sys.version_info >= (3, 12), "PEP 695 syntax requires Python 3.12")
	def test_generic_function_does_not_generate_a_non_generic_binding(self) -> None:
		result = generate_bindings_from_source(
			"def answer[T]() -> int:\n    return 42\n", "generic.py"
		)
		self.assertFalse(result.succeeded)
		self.assertIsNone(result.catalog)
		self.assertEqual(result.catalog_json, "")
		self.assertTrue(any(item.code == "PYF0006" for item in result.diagnostics))

	def test_project_catalog_binds_explicit_modules_in_two_phases(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "sample"
			package.mkdir()
			models_path = package / "models.py"
			service_path = package / "service.py"
			models_source = (
				"class User:\n"
				"    name: str\n\n"
				"    def __init__(self, name: str) -> None:\n"
				"        self.name = name\n"
			)
			service_source = (
				"def answer(value: int) -> int:\n"
				"    return value\n"
			)
			models_path.write_text(models_source, encoding="utf-8")
			service_path.write_text(service_source, encoding="utf-8")
			result = generate_binding_project(
				(models_path, service_path), project_root=root
			)
			self.assertTrue(
				result.succeeded, [item.format() for item in result.diagnostics]
			)

		models = ModuleType("sample.models")
		service = ModuleType("sample.service")
		exec(compile(models_source, "models.py", "exec"), models.__dict__)
		exec(compile(service_source, "service.py", "exec"), service.__dict__)
		calls: list[tuple[object, ...]] = []

		class Binding:
			def constructor(self, **kwargs: object) -> "Binding":
				calls.append(("constructor", kwargs))
				return self

			def field(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("field", name, kwargs))
				return self

			def method(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("method", name, kwargs))
				return self

			def static_method(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("static", name, kwargs))
				return self

			def property(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("property", name, kwargs))
				return self

		class Session:
			def register_class(
				self, name: str, python_type: type, **kwargs: object
			) -> Binding:
				calls.append(("class", name, python_type, kwargs))
				return Binding()

			def register_function(
				self, name: str, callback: object, **kwargs: object
			) -> None:
				calls.append(("function", name, callback, kwargs))

		bind_modules(Session(), (models, service), result)
		class_index = next(
			index for index, call in enumerate(calls) if call[0] == "class"
		)
		member_index = next(
			index for index, call in enumerate(calls) if call[0] == "constructor"
		)
		self.assertLess(class_index, member_index)
		self.assertTrue(
			any(
				call[:2] == ("function", "sample.service.answer")
				for call in calls
			)
		)

	def test_project_binding_requires_every_module_before_mutation(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			first = root / "first.py"
			second = root / "second.py"
			first.write_text("class First:\n    value: int\n", encoding="utf-8")
			second.write_text("def second() -> int:\n    return 2\n", encoding="utf-8")
			result = generate_binding_project((first, second), project_root=root)
			self.assertTrue(result.succeeded)

		first_module = ModuleType("first")
		exec(
			compile("class First:\n    value: int\n", "first.py", "exec"),
			first_module.__dict__,
		)

		class Session:
			def __getattr__(self, name: str) -> object:
				raise AssertionError(f"session mutated through {name}")

		with self.assertRaisesRegex(ValueError, "second"):
			bind_modules(Session(), (first_module,), result)

	def test_runtime_shape_mismatch_fails_before_session_mutation(self) -> None:
		class Session:
			def __getattr__(self, name: str) -> object:
				raise AssertionError(f"session mutated through {name}")

		method_result = generate_bindings_from_source(
			"class Service:\n"
			"    def work(self, value: int) -> int:\n"
			"        return value\n",
			"service.py",
			module_name="service",
		)
		self.assertTrue(method_result.succeeded)
		stale_method_module = ModuleType("service")
		exec("class Service:\n    work = 1\n", stale_method_module.__dict__)
		with self.assertRaisesRegex(TypeError, "work is not callable"):
			bind_module(Session(), stale_method_module, method_result)

		stale_method_signature = ModuleType("service")
		exec(
			"class Service:\n"
			"    def work(self, value, extra):\n        return value + extra\n",
			stale_method_signature.__dict__,
		)
		with self.assertRaisesRegex(TypeError, "work does not match"):
			bind_module(Session(), stale_method_signature, method_result)

		property_result = generate_bindings_from_source(
			"class Settings:\n"
			"    @property\n"
			"    def value(self) -> int:\n"
			"        return 1\n\n"
			"    @value.setter\n"
			"    def value(self, new_value: int) -> None:\n"
			"        pass\n",
			"settings.py",
			module_name="settings",
		)
		self.assertTrue(property_result.succeeded)
		stale_property_module = ModuleType("settings")
		exec(
			"class Settings:\n"
			"    @property\n"
			"    def value(self):\n"
			"        return 1\n",
			stale_property_module.__dict__,
		)
		with self.assertRaisesRegex(TypeError, "value is not a writable property"):
			bind_module(Session(), stale_property_module, property_result)

		stale_getter_module = ModuleType("settings")
		exec(
			"class Settings:\n"
			"    @property\n"
			"    def value(self, extra):\n"
			"        return extra\n\n"
			"    @value.setter\n"
			"    def value(self, new_value):\n"
			"        pass\n",
			stale_getter_module.__dict__,
		)
		with self.assertRaisesRegex(TypeError, "getter must accept exactly self"):
			bind_module(Session(), stale_getter_module, property_result)

		stale_setter_module = ModuleType("settings")
		exec(
			"class Settings:\n"
			"    @property\n"
			"    def value(self):\n"
			"        return 1\n\n"
			"    @value.setter\n"
			"    def value(self, first, second):\n"
			"        pass\n",
			stale_setter_module.__dict__,
		)
		with self.assertRaisesRegex(
			TypeError, "setter must accept exactly self and value"
		):
			bind_module(Session(), stale_setter_module, property_result)

		matching_property_module = ModuleType("settings")
		exec(
			"class Settings:\n"
			"    @property\n"
			"    def value(self):\n"
			"        return 1\n\n"
			"    @value.setter\n"
			"    def value(self, new_value):\n"
			"        pass\n",
			matching_property_module.__dict__,
		)
		malformed_catalog = json.loads(property_result.catalog_json)
		setter = malformed_catalog["types"][0]["members"][1]
		setter["paramTypes"] = ["string"]
		with self.assertRaisesRegex(ValueError, "setter contract does not match"):
			bind_module(Session(), matching_property_module, malformed_catalog)

		static_result = generate_bindings_from_source(
			"class Service:\n"
			"    @staticmethod\n"
			"    def work(value: int) -> int:\n"
			"        return value\n",
			"service.py",
			module_name="service",
		)
		self.assertTrue(static_result.succeeded)
		stale_static_module = ModuleType("service")
		exec(
			"class Service:\n"
			"    def work(self, value):\n"
			"        return value\n",
			stale_static_module.__dict__,
		)
		with self.assertRaisesRegex(TypeError, "work must be a static or class method"):
			bind_module(Session(), stale_static_module, static_result)

	@unittest.skipUnless(sys.version_info >= (3, 14), "lazy annotations require Python 3.14+")
	def test_runtime_shape_checks_do_not_evaluate_lazy_annotations(self) -> None:
		source = (
			"class Counter:\n"
			"    def __init__(self, value: Int64) -> None:\n        self.value = value\n"
			"    def add(self, value: Int64) -> Int64:\n        return self.value + value\n"
			"    @staticmethod\n"
			"    def twice(value: Int64) -> Int64:\n        return value * 2\n"
			"    @property\n"
			"    def current(self) -> Int64:\n        return self.value\n"
			"    @current.setter\n"
			"    def current(self, value: Int64) -> None:\n        self.value = value\n"
			"def twice(value: Int64) -> Int64:\n    return value * 2\n"
		)
		result = generate_bindings_from_source(source, "lazy.py", module_name="lazy")
		self.assertTrue(result.succeeded, result.diagnostics)
		module = ModuleType("lazy")
		# The test module's future import must not turn this into string annotations.
		exec(compile(source, "lazy.py", "exec", dont_inherit=True), module.__dict__)
		calls: list[str] = []

		class Binding:
			def constructor(self, **kwargs: object) -> None:
				calls.append("constructor")

			def method(self, name: str, **kwargs: object) -> None:
				calls.append(name)

			def static_method(self, name: str, **kwargs: object) -> None:
				calls.append("static_" + name)

			def property(self, name: str, **kwargs: object) -> None:
				calls.append(name)

		class Session:
			def register_class(self, name: str, python_type: type, **kwargs: object) -> Binding:
				calls.append(name)
				return Binding()

			def register_function(self, name: str, callback: object, **kwargs: object) -> None:
				calls.append(name)

		bind_module(Session(), module, result)
		self.assertEqual(calls, ["lazy.Counter", "constructor", "add", "static_twice", "current", "lazy.twice"])
		self.assertEqual(module.twice(3), 6)
		self.assertNotIn("Int64", module.__dict__)
		with self.assertRaises(NameError):
			getattr(module.twice, "__annotations__")

	@unittest.skipUnless(sys.version_info >= (3, 14), "lazy annotations require Python 3.14+")
	def test_lazy_annotations_do_not_hide_runtime_shape_mismatches(self) -> None:
		result = generate_bindings_from_source(
			"def twice(value: Int64) -> Int64:\n    return value * 2\n",
			"lazy.py", module_name="lazy",
		)
		self.assertTrue(result.succeeded, result.diagnostics)

		class Session:
			def __getattr__(self, name: str) -> object:
				raise AssertionError(f"session mutated through {name}")

		for parameters in ("value: Int64, extra: Int64", "value: Int64 = 0", "*, value: Int64", "*value: Int64"):
			with self.subTest(parameters=parameters):
				module = ModuleType("lazy")
				source = f"def twice({parameters}) -> Int64:\n    return 0\n"
				exec(compile(source, "lazy.py", "exec", dont_inherit=True), module.__dict__)
				with self.assertRaisesRegex(TypeError, "twice does not match"):
					bind_module(Session(), module, result)


	def test_enum_runtime_contract_is_exact_and_supports_intflag(self) -> None:
		result = generate_bindings_from_source(
			"from enum import IntFlag\n"
			"class Permission(IntFlag):\n"
			"    READ = 1\n"
			"    WRITE = 2\n",
			"permissions.py",
			module_name="permissions",
		)
		self.assertTrue(result.succeeded)
		module = ModuleType("permissions")
		exec(
			"from enum import IntFlag\n"
			"class Permission(IntFlag):\n"
			"    READ = 1\n"
			"    WRITE = 2\n",
			module.__dict__,
		)
		calls: list[tuple[object, ...]] = []

		class Session:
			def register_enum(self, name: str, enum_type: type, **kwargs: object) -> None:
				calls.append((name, enum_type, kwargs))

		bind_module(Session(), module, result)
		self.assertEqual(calls[0][0], "permissions.Permission")

		stale = ModuleType("permissions")
		exec(
			"from enum import IntEnum\n"
			"class Permission(IntEnum):\n"
			"    READ = 9\n"
			"    WRITE = 2\n",
			stale.__dict__,
		)
		with self.assertRaisesRegex(ValueError, "members do not match"):
			bind_module(Session(), stale, result)

		plain_enum = generate_bindings_from_source(
			"from enum import Enum\n"
			"class Invalid(Enum):\n"
			"    VALUE = 1\n",
			"invalid.py",
			module_name="invalid",
		)
		self.assertFalse(plain_enum.succeeded)

	def test_generated_catalog_can_register_an_explicit_module(self) -> None:
		result = generate_bindings_from_source(SOURCE, "sample.py", module_name="sample")
		self.assertTrue(result.succeeded)
		module = ModuleType("sample")
		sys.modules[module.__name__] = module
		try:
			exec(compile(SOURCE, "sample.py", "exec"), module.__dict__)
		finally:
			sys.modules.pop(module.__name__, None)
		calls: list[tuple[object, ...]] = []

		class Binding:
			def constructor(self, **kwargs: object) -> "Binding":
				calls.append(("constructor", kwargs))
				return self

			def field(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("field", name, kwargs))
				return self

			def method(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("method", name, kwargs))
				return self

			def static_method(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("static", name, kwargs))
				return self

			def property(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("property", name, kwargs))
				return self

		class Session:
			def register_enum(self, name: str, enum_type: type, **kwargs: object) -> None:
				calls.append(("enum", name, enum_type, kwargs))

			def register_class(self, name: str, python_type: type, **kwargs: object) -> Binding:
				calls.append(("class", name, python_type, kwargs))
				return Binding()

			def register_function(
				self, name: str, callback: object, **kwargs: object
			) -> None:
				calls.append(("function", name, callback, kwargs))

		bind_module(Session(), module, result)
		self.assertTrue(any(call[:2] == ("enum", "sample.Mode") for call in calls))
		self.assertTrue(any(call[:2] == ("class", "sample.Counter") for call in calls))
		self.assertTrue(any(call[:2] == ("function", "sample.combine") for call in calls))
		self.assertTrue(any(call[:2] == ("method", "add") for call in calls))
		self.assertTrue(any(call[:2] == ("static", "twice") for call in calls))

	def test_generated_catalog_executes_with_core_qualified_binding_names(self) -> None:
		try:
			import veroscript
		except ModuleNotFoundError as error:
			if error.name not in {"veroscript", "veroscript._veroscript"}:
				raise
			self.skipTest("native VeroScript binding is not available")

		source = SOURCE + (
			"\ndef identity_counter(value: Counter) -> Counter:\n    return value\n"
			"def identity_items(values: list[Counter]) -> list[Counter]:\n    return values\n"
			"def identity_table(values: dict[str, Counter]) -> dict[str, Counter]:\n    return values\n"
			"def identity_modes(values: list[Mode]) -> list[Mode]:\n    return values\n"
		)
		result = generate_bindings_from_source(source, "sample_api.py", module_name="sample.api")
		self.assertTrue(result.succeeded, result.diagnostics)
		module = ModuleType("sample.api")
		sys.modules[module.__name__] = module
		try:
			exec(compile(source, "sample_api.py", "exec", dont_inherit=True), module.__dict__)
		finally:
			sys.modules.pop(module.__name__, None)
		catalog_before = result.catalog_json
		self.assertIn("sample::api::Counter", catalog_before)
		self.assertIn("sample::api::identity_items", catalog_before)
		with veroscript.Session(backend=veroscript.Backend.VM) as session:
			bind_module(session, module, result)
			loaded = session.load_source(
				"int read(){sample::api::Counter item=sample::api::Counter(4);item.value=5;"
				"sample::api::Counter same=sample::api::identity_counter(item);"
				"array<sample::api::Counter> values={same};"
				"array<sample::api::Counter> echoed=sample::api::identity_items(values);"
				"map<string,sample::api::Counter> table={\"item\":item};"
				"map<string,sample::api::Counter> mapped=sample::api::identity_table(table);"
				"array<sample::api::Mode> modes={sample::api::Mode::READ};"
				"array<sample::api::Mode> returned=sample::api::identity_modes(modes);"
				"return sample::api::combine(item.add(3),sample::api::Counter::twice(2))"
				"+echoed[0].value+mapped[\"item\"].value+cast<int>(returned[0]);}void main(){}",
				"bindgen_qualified_runtime.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertTrue(session.run(veroscript.RunMode.EVAL).success)
			self.assertEqual(session.call("read"), 23)
		self.assertEqual(result.catalog_json, catalog_before)

	def test_catalog_contains_typed_python_api(self) -> None:
		result = generate_bindings_from_source(SOURCE, "sample.py", module_name="sample")
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		assert result.catalog is not None
		self.assertEqual(result.catalog["version"], 1)
		entries = cast(list[dict[str, object]], result.catalog["types"])
		types = {str(entry["name"]): entry for entry in entries}
		mode_members = cast(list[dict[str, object]], types["sample::Mode"]["members"])
		counter_entries = cast(
			list[dict[str, object]], types["sample::Counter"]["members"]
		)
		self.assertEqual(types["sample::Counter"]["kind"], "class")
		self.assertEqual(mode_members[-1]["enumValue"], 3)
		value_members = [
			entry for entry in counter_entries if entry["name"] == "value"
		]
		self.assertEqual(
			[entry["kind"] for entry in value_members],
			["field", "propertySetter"],
		)
		counter_members = {str(entry["name"]): entry for entry in counter_entries}
		self.assertEqual(counter_members["add"]["paramTypes"], ["int"])
		self.assertEqual(counter_members["twice"]["kind"], "staticMethod")
		self.assertIn("sample::combine", result.api_reference)

	def test_untyped_and_dynamic_signatures_fail_closed(self) -> None:
		result = generate_bindings_from_source("def bad(value):\n    return value\n", "bad.py")
		self.assertFalse(result.succeeded)
		self.assertTrue(any(item.code == "PYB1001" for item in result.diagnostics))
		decorated = generate_bindings_from_source(
			"@cache\ndef value() -> int:\n    return 1\n", "decorated.py"
		)
		self.assertFalse(decorated.succeeded)
		self.assertTrue(any(item.code == "PYB1004" for item in decorated.diagnostics))

	def test_invalid_veroscript_identifiers_fail_before_catalog_generation(self) -> None:
		cases = {
			"new do keyword": (
				"def do() -> int:\n    return 1\n",
				"sample",
			),
			"new constexpr keyword": (
				"def constexpr() -> int:\n    return 1\n",
				"sample",
			),
			"keyword module": (
				"def value() -> int:\n    return 1\n",
				"do.module",
			),
			"keyword parameter": (
				"def value(do: int) -> int:\n    return do\n",
				"sample",
			),
			"keyword field": (
				"class Item:\n    do: int\n",
				"sample",
			),
			"keyword method": (
				"class Item:\n"
				"    def do(self) -> int:\n        return 1\n",
				"sample",
			),
			"keyword enum member": (
				"from enum import IntEnum\n"
				"class Mode(IntEnum):\n    do = 1\n",
				"sample",
			),
			"non-ASCII identifier": (
				"def café() -> int:\n    return 1\n",
				"sample",
			),
		}
		for name, (source, module_name) in cases.items():
			with self.subTest(name=name):
				result = generate_bindings_from_source(
					source,
					f"{name}.py",
					module_name=module_name,
				)
				self.assertFalse(result.succeeded)
				self.assertIsNone(result.catalog)
				self.assertEqual(result.catalog_json, "")
				self.assertTrue(
					any(
						item.code == "PYB1007"
						and "valid VeroScript identifier" in item.message
						for item in result.diagnostics
					),
					[item.format() for item in result.diagnostics],
				)

	def test_private_types_cannot_leak_into_public_contracts(self) -> None:
		source = (
			"class _Hidden:\n"
			"    value: int\n\n"
			"def expose(value: _Hidden) -> int:\n"
			"    return value.value\n"
		)
		single = generate_bindings_from_source(
			source,
			"private-type.py",
			module_name="private_type",
		)
		self.assertFalse(single.succeeded)
		self.assertIsNone(single.catalog)
		self.assertEqual(single.catalog_json, "")

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
			project = generate_binding_project(
				(models, service),
				project_root=root,
			)
		self.assertFalse(project.succeeded)
		self.assertIsNone(project.catalog)
		self.assertEqual(project.catalog_json, "")

	def test_invalid_method_receivers_and_decorators_fail_closed(self) -> None:
		sources = {
			"keyword-only instance receiver": (
				"class Service:\n"
				"    def work(*, self) -> int:\n        return 1\n"
			),
			"keyword-only class receiver": (
				"class Service:\n"
				"    @classmethod\n"
				"    def work(*, cls) -> int:\n        return 1\n"
			),
			"decorated constructor": (
				"class Service:\n"
				"    @staticmethod\n"
				"    def __init__() -> None:\n        pass\n"
			),
			"combined method decorators": (
				"class Service:\n"
				"    @staticmethod\n"
				"    @classmethod\n"
				"    def work(cls) -> int:\n        return 1\n"
			),
		}
		for name, source in sources.items():
			with self.subTest(name=name):
				result = generate_bindings_from_source(source, f"{name}.py")
				self.assertFalse(result.succeeded)
				self.assertTrue(result.diagnostics)

	def test_invalid_property_contracts_fail_closed(self) -> None:
		sources = {
			"getter parameter": (
				"class Settings:\n"
				"    @property\n"
				"    def value(self, extra: int) -> int:\n        return extra\n"
			),
			"getter without value": (
				"class Settings:\n"
				"    @property\n"
				"    def value(self) -> None:\n        pass\n"
			),
			"dynamic property decorator": (
				"class Settings:\n"
				"    @decorators.property\n"
				"    def value(self) -> int:\n        return 1\n"
			),
			"setter without value": (
				"class Settings:\n"
				"    @property\n"
				"    def value(self) -> int:\n        return 1\n"
				"    @value.setter\n"
				"    def value(self) -> None:\n        pass\n"
			),
			"setter with two values": (
				"class Settings:\n"
				"    @property\n"
				"    def value(self) -> int:\n        return 1\n"
				"    @value.setter\n"
				"    def value(self, first: int, second: int) -> None:\n        pass\n"
			),
			"setter type mismatch": (
				"class Settings:\n"
				"    @property\n"
				"    def value(self) -> int:\n        return 1\n"
				"    @value.setter\n"
				"    def value(self, new_value: str) -> None:\n        pass\n"
			),
			"setter returns value": (
				"class Settings:\n"
				"    @property\n"
				"    def value(self) -> int:\n        return 1\n"
				"    @value.setter\n"
				"    def value(self, new_value: int) -> int:\n        return new_value\n"
			),
			"setter without getter": (
				"class Settings:\n"
				"    @value.setter\n"
				"    def value(self, new_value: int) -> None:\n        pass\n"
			),
			"setter name mismatch": (
				"class Settings:\n"
				"    @property\n"
				"    def value(self) -> int:\n        return 1\n"
				"    @value.setter\n"
				"    def other(self, new_value: int) -> None:\n        pass\n"
			),
		}
		for name, source in sources.items():
			with self.subTest(name=name):
				result = generate_bindings_from_source(source, f"{name}.py")
				self.assertFalse(result.succeeded)
				self.assertTrue(
					any(item.code in {"PYB1003", "PYB1007"} for item in result.diagnostics),
					[item.format() for item in result.diagnostics],
				)

	def test_class_member_collisions_propagate_from_frontend(self) -> None:
		result = generate_bindings_from_source(
			"class Example:\n"
			"    value: int\n"
			"    def value(self) -> int:\n        return 1\n",
			"collision.py",
		)
		self.assertFalse(result.succeeded)
		self.assertIsNone(result.catalog)
		self.assertTrue(
			any(item.code == "PYF0003" for item in result.diagnostics),
			[item.format() for item in result.diagnostics],
		)

	def test_valid_property_contract_is_preserved(self) -> None:
		source = (
			"class Settings:\n"
			"    @property\n"
			"    def value(self) -> int:\n        return 1\n"
			"    @value.setter\n"
			"    def value(self, new_value: int) -> None:\n        pass\n"
		)
		result = generate_bindings_from_source(
			source,
			"settings.py",
			module_name="settings",
		)
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		assert result.catalog is not None
		types = cast(list[dict[str, object]], result.catalog["types"])
		members = cast(list[dict[str, object]], types[0]["members"])
		self.assertEqual(
			[(member["kind"], member["name"]) for member in members],
			[("propertyGetter", "value"), ("propertySetter", "value")],
		)
		self.assertEqual(members[0]["returnType"], "int")
		self.assertEqual(members[1]["paramTypes"], ["int"])
		self.assertEqual(members[1]["returnType"], "void")

		calls: list[tuple[str, str, object]] = []

		class Binding:
			def property(self, name: str, **kwargs: object) -> "Binding":
				calls.append(("property", name, kwargs))
				return self

		class Session:
			def register_class(
				self, name: str, python_type: type, **kwargs: object
			) -> Binding:
				calls.append(("class", name, python_type))
				return Binding()

		module = ModuleType("settings")
		exec(compile(source, "settings.py", "exec"), module.__dict__)
		bind_module(Session(), module, result)
		self.assertTrue(any(call[:2] == ("property", "value") for call in calls))

	def test_emitted_scalar_types_all_bind_to_runtime_types(self) -> None:
		aliases = {
			"bool": "bool",
			"int": "int",
			"float": "double",
			"str": "string",
			"Int8": "int8",
			"UInt8": "uint8",
			"Int16": "int16",
			"UInt16": "uint16",
			"Int32": "int",
			"UInt32": "uint32",
			"Int64": "int64",
			"UInt64": "uint64",
			"Float32": "float",
			"Float64": "double",
		}
		for alias, expected in aliases.items():
			with self.subTest(alias=alias):
				source = (
					"from __future__ import annotations\n"
					"class Settings:\n"
					f"    value: {alias}\n"
				)
				result = generate_bindings_from_source(
					source,
					"settings.py",
					module_name="settings",
				)
				self.assertTrue(
					result.succeeded,
					[item.format() for item in result.diagnostics],
				)
				calls: list[tuple[str, str, object]] = []

				class Binding:
					def field(self, name: str, **kwargs: object) -> "Binding":
						calls.append(("field", name, kwargs))
						return self

				class Session:
					def register_class(
						self, name: str, python_type: type, **kwargs: object
					) -> Binding:
						calls.append(("class", name, python_type))
						return Binding()

				module = ModuleType("settings")
				exec(compile(source, "settings.py", "exec"), module.__dict__)
				bind_module(Session(), module, result)
				field_call = next(
					call for call in calls if call[:2] == ("field", "value")
				)
				field_options = cast(dict[str, object], field_call[2])
				self.assertEqual(field_options["type_name"], expected)
				self.assertTrue(field_options["writable"])

	def test_declared_types_shadow_scalar_aliases(self) -> None:
		source = (
			"from enum import IntEnum\n"
			"class Int8:\n    value: int\n"
			"class Float32(IntEnum):\n    ITEM = 1\n"
			"def echo(value: Int8, kind: Float32) -> Int8:\n    return value\n"
			"def gather(values: list[Int8], kinds: list[Float32]) -> None:\n    pass\n"
		)
		result = generate_bindings_from_source(source, "shadow.py")
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		catalog = cast(dict[str, object], result.catalog)
		functions = cast(list[dict[str, object]], catalog["functions"])
		echo, gather = functions
		self.assertEqual(echo["paramTypes"], ["object", "enum"])
		self.assertEqual(echo["paramHostTypeNames"], ["shadow::Int8", "shadow::Float32"])
		self.assertEqual(echo["returnHostTypeName"], "shadow::Int8")
		self.assertEqual(gather["paramElementTypeNames"], ["shadow::Int8", "shadow::Float32"])

	def test_project_qualified_types_precede_other_leaf_type_kinds(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			sources = {
				"records.py": "class Item:\n    value: int\nclass Int8:\n    value: int\n",
				"enums.py": "from enum import IntEnum\nclass Item(IntEnum):\n    ONE = 1\nclass Int8(IntEnum):\n    ONE = 1\n",
				"api.py": (
					"import records\nimport enums\n"
					"def echo(item: enums.Item, record: records.Int8, code: enums.Int8) -> None:\n    pass\n"
					"def gather(items: list[enums.Item], records: list[records.Int8]) -> None:\n    pass\n"
				),
				# A local enum remains usable despite ambiguous project leaves.
				"local.py": "from enum import IntEnum\nclass Item(IntEnum):\n    TWO = 2\ndef echo(item: Item) -> Item:\n    return item\n",
			}
			for name, source in sources.items():
				(root / name).write_text(source, encoding="utf-8")
			result = generate_binding_project([root / name for name in sources], project_root=root)
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			catalog = cast(dict[str, object], result.catalog)
			functions = {str(entry["name"]): entry for entry in cast(list[dict[str, object]], catalog["functions"])}
			echo = functions["api::echo"]
			self.assertEqual(echo["paramTypes"], ["enum", "object", "enum"])
			self.assertEqual(echo["paramHostTypeNames"], ["enums::Item", "records::Int8", "enums::Int8"])
			self.assertEqual(functions["api::gather"]["paramElementTypeNames"], ["enums::Item", "records::Int8"])
			self.assertEqual(functions["local::echo"]["returnType"], "enum")
			self.assertEqual(functions["local::echo"]["returnHostTypeName"], "local::Item")

	def test_local_types_override_opposite_kind_from_known_types(self) -> None:
		module = parse_source(
			"from enum import IntEnum\nclass Item(IntEnum):\n    ONE = 1\n"
			"def echo(value: Item) -> Item:\n    return value\n",
			"local.py",
		)
		result = generate_bindings(module, known_records={"Item": "foreign::Item"})
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		functions = cast(list[dict[str, object]], cast(dict[str, object], result.catalog)["functions"])
		self.assertEqual(functions[0]["paramTypes"], ["enum"])
		self.assertEqual(functions[0]["returnHostTypeName"], "local::Item")

	def test_ambiguous_project_types_do_not_fall_back_to_scalar_aliases(self) -> None:
		for annotation in ("Int8", "list[Int8]", "dict[str, Int8]"):
			with self.subTest(annotation=annotation), tempfile.TemporaryDirectory() as directory:
				root = Path(directory)
				sources = {
					"records.py": "class Int8:\n    value: int\n",
					"enums.py": "from enum import IntEnum\nclass Int8(IntEnum):\n    ONE = 1\n",
					"api.py": "from records import Int8\n"
							  f"def echo(value: {annotation}) -> None:\n    pass\n",
				}
				for name, source in sources.items():
					(root / name).write_text(source, encoding="utf-8")
				result = generate_binding_project([root / name for name in sources], project_root=root)
				self.assertFalse(result.succeeded)
				self.assertIsNone(result.catalog)
				self.assertTrue(any(item.code == "PYB1002" and "ambiguous" in item.message
									for item in result.diagnostics))

	def test_scalar_and_nongeneric_record_type_arguments_fail_closed(self) -> None:
		for annotation in ("int[str]", "Int8[str]", "list[int[str]]", "list[Item[int]]",
						   "int[()]", "Int8[()]", "Item[()]", "list[Item[()]]"):
			with self.subTest(annotation=annotation):
				source = "class Item:\n    value: int\n"
				source += f"def consume(value: {annotation}) -> None:\n    pass\n"
				result = generate_bindings_from_source(source, "invalid_type_arguments.py")
				self.assertFalse(result.succeeded)
				self.assertIsNone(result.catalog)
				self.assertTrue(any(item.code == "PYB1002" for item in result.diagnostics))

	def test_ambiguous_project_types_do_not_affect_ignored_receiver_annotations(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			sources = {
				"first.py": "class Self:\n    value: int\n",
				"second.py": "class Self:\n    value: int\n",
				"api.py": (
					"from typing import Self\nclass Holder:\n"
					"    def echo(self: Self, value: int) -> int:\n        return value\n"
					"    @classmethod\n"
					"    def zero(cls: Self) -> int:\n        return 0\n"
				),
			}
			for name, source in sources.items():
				(root / name).write_text(source, encoding="utf-8")
			result = generate_binding_project([root / name for name in sources], project_root=root)
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])

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
			"constructor decorator": (
				"class Settings:\n"
				"    @staticmethod\n"
				"    def __init__() -> None:\n        pass\n"
			),
		}
		for name, source in invalid_sources.items():
			with self.subTest(name=name):
				result = generate_bindings_from_source(source, f"{name}.py")
				self.assertFalse(result.succeeded)
				self.assertTrue(result.diagnostics)

		valid_return = generate_bindings_from_source(
			"def finished() -> None:\n    pass\n",
			"finished.py",
		)
		self.assertTrue(
			valid_return.succeeded,
			[item.format() for item in valid_return.diagnostics],
		)

	def test_generated_catalog_is_accepted_by_veroscript(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable or not Path(executable).is_file():
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		result = generate_bindings_from_source(SOURCE, "sample.py", module_name="sample")
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		with tempfile.TemporaryDirectory() as directory:
			catalog = Path(directory) / "sample.bindings.json"
			catalog.write_text(result.catalog_json, encoding="utf-8")
			completed = subprocess.run(
				[executable, "bindings", "validate", str(catalog)],
				check=False,
				capture_output=True,
				text=True,
			)
			self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

	def test_validator_launch_failure_is_reported(self) -> None:
		succeeded, output = validate_catalog(
			"missing.bindings.json",
			Path("missing-veroscript-executable"),
		)
		self.assertFalse(succeeded)
		self.assertTrue(output)

	def test_validator_rejects_invalid_timeouts(self) -> None:
		for timeout in (0.0, -1.0, float("nan"), float("inf")):
			with self.subTest(timeout=timeout):
				succeeded, output = validate_catalog(
					"missing.bindings.json",
					sys.executable,
					timeout_seconds=timeout,
				)
				self.assertFalse(succeeded)
				self.assertIn("finite positive number", output)

	def test_multi_file_project_merges_catalog_and_references(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "sample"
			package.mkdir()
			models = package / "models.py"
			service = package / "service.py"
			models.write_text(
				"class User:\n    name: str\n",
				encoding="utf-8",
			)
			service.write_text(
				"from sample.models import User\n\n"
				"def get_name(user: User) -> str:\n    return user.name\n",
				encoding="utf-8",
			)
			result = generate_binding_project(
				(models, service),
				project_root=root,
			)
			self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
			assert result.catalog is not None
			types = cast(list[dict[str, object]], result.catalog["types"])
			functions = cast(list[dict[str, object]], result.catalog["functions"])
			self.assertEqual(types[0]["name"], "sample::models::User")
			self.assertEqual(functions[0]["name"], "sample::service::get_name")
			self.assertEqual(
				functions[0]["paramHostTypeNames"],
				["sample::models::User"],
			)
			self.assertEqual(
				[entry.output_name for entry in result.api_references],
				["sample/models.vs.txt", "sample/service.vs.txt"],
			)
			self.assertIn("veroscript.python-binding-project", result.manifest_json)

	def test_multi_file_cli_writes_complete_output_set(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "sample"
			package.mkdir()
			models = package / "models.py"
			service = package / "service.py"
			models.write_text("class User:\n    name: str\n", encoding="utf-8")
			service.write_text(
				"from sample.models import User\n\n"
				"def get_name(user: User) -> str:\n    return user.name\n",
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
				"veroscript_python_bindgen",
				str(models),
				str(service),
				"--project-root",
				str(root),
				"--output-dir",
				str(output),
			]
			executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
			if executable and Path(executable).is_file():
				command.extend(("--validate", "--veroscript", executable))
			completed = subprocess.run(
				command, check=False, capture_output=True, text=True, env=environment
			)
			self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
			for relative in (
				"bindings.json",
				"bindgen.manifest.json",
				"sample/models.vs.txt",
				"sample/service.vs.txt",
			):
				self.assertTrue((output / relative).is_file(), relative)


if __name__ == "__main__":
	unittest.main()
