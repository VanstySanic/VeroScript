# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import ast
from importlib import metadata
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


TEST_INSTALLED = os.environ.get("VEROSCRIPT_TEST_INSTALLED_PACKAGE") == "1"
if not TEST_INSTALLED:
	sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

import veroscript_python_frontend as frontend  # noqa: E402
from veroscript_python_frontend import (  # noqa: E402
	Localizer,
	evaluate_integer_expression,
	is_veroscript_identifier,
	parse_files,
	parse_source,
	write_text_atomic,
	write_text_files_atomic,
)


class PythonFrontendTests(unittest.TestCase):
	def test_excessive_source_nesting_returns_diagnostics(self) -> None:
		terms = max(1400, sys.getrecursionlimit() + 100)
		sources = (
			"def value(x: " + " | ".join(["int"] * terms) + ") -> None:\n    pass\n",
			"value: int = " + " + ".join(["1"] * terms) + "\n",
		)
		for source in sources:
			with self.subTest(source=source[:30]):
				result = parse_source(source, "deep.py")
				self.assertFalse(result.succeeded)
				self.assertTrue(result.diagnostics)
				self.assertEqual(result.diagnostics[0].location.path, "deep.py")

	def test_excessive_constant_nesting_is_not_evaluated(self) -> None:
		expression: ast.expr = ast.Constant(value=1)
		for _ in range(sys.getrecursionlimit() + 100):
			expression = ast.UnaryOp(op=ast.UAdd(), operand=expression)
		self.assertIsNone(evaluate_integer_expression(expression))

	def test_type_checking_guard_does_not_discard_runtime_else_branch(self) -> None:
		guarded_import = "from typing import TYPE_CHECKING\nif TYPE_CHECKING:\n    import models\n"
		self.assertEqual(parse_source(guarded_import).diagnostics, ())
		module = parse_source(guarded_import + "else:\n    print('runtime side effect')\n")
		self.assertTrue(any(item.code == "PYF0005" for item in module.diagnostics))

	@unittest.skipUnless(sys.version_info >= (3, 12), "PEP 695 syntax requires Python 3.12")
	def test_generic_declarations_are_not_silently_erased(self) -> None:
		sources = (
			"def answer[T]() -> int:\n    return 42\n",
			"class Item[T]:\n    value: int\n",
			"class Item:\n    def answer[T](self) -> int:\n        return 42\n",
			"async def answer[T]() -> int:\n    return 42\n",
		)
		for source in sources:
			with self.subTest(source=source):
				module = parse_source(source, "generic.py")
				self.assertTrue(any(
					item.code == "PYF0006" and "type parameters" in item.message
					for item in module.diagnostics
				), module.diagnostics)

	def test_standalone_install_supplies_version_without_checkout_or_wheel_metadata(self) -> None:
		from veroscript_python_frontend import _version

		with tempfile.TemporaryDirectory() as temporary:
			location = Path(temporary) / "veroscript_python_frontend"
			location.mkdir()
			stamp = location / "_version.json"
			stamp.write_text(json.dumps({"version": "2.3.4"}), encoding="utf-8")
			with mock.patch.object(_version, "__file__", str(location / "_version.py")), \
					mock.patch.object(metadata, "version", return_value="9.9.9"):
				self.assertEqual(_version.discover_version(), "2.3.4")
				stamp.write_text(json.dumps({"version": "invalid"}), encoding="utf-8")
				with self.assertRaisesRegex(ValueError, "Invalid packaged"):
					_version.discover_version()

	def test_renamed_checkout_supplies_native_version_and_localization(self) -> None:
		from veroscript_python_frontend import _version, localization

		with tempfile.TemporaryDirectory() as temporary:
			module = Path(temporary) / "Engine Sources"
			header = module / "VeroScript/Public/VeroScriptVersion.h"
			header.parent.mkdir(parents=True)
			header.write_text("constexpr auto VEROSCRIPT_VERSION = { 2, 3, 4 };", encoding="utf-8")
			python_root = module / "Extensions/Language/Python"
			catalog = python_root / "Resources/Localization/zh-CN/python-tools.json"
			catalog.parent.mkdir(parents=True)
			(catalog.parent.parent / "en-US").mkdir()
			catalog.write_text(json.dumps({"python_tools.syntax_error": "translated {0}"}), encoding="utf-8")
			location = python_root / "VeroScriptPythonFrontend/python/veroscript_python_frontend"
			location.mkdir(parents=True)
			with mock.patch.object(_version, "__file__", str(location / "_version.py")):
				self.assertEqual(_version.discover_version(), "2.3.4")
			for catalog_location in (
				location,
				module / "CopiedTools/veroscript_python_frontend",
			):
				with self.subTest(location=catalog_location):
					catalog_location.mkdir(parents=True, exist_ok=True)
					with mock.patch.object(localization, "__file__", str(catalog_location / "localization.py")), \
							mock.patch.dict(os.environ, {"VERO_LOCALIZATION_DIR": ""}):
						localizer = Localizer(language="zh-CN")
						self.assertEqual(localizer.language, "zh-CN")
						self.assertEqual(localizer.message("python_tools.syntax_error", "sample"), "translated sample")

	def test_integer_expression_evaluation_is_safe_and_deterministic(self) -> None:
		values = {"READ": 1, "WRITE": 2}
		self.assertEqual(
			evaluate_integer_expression(
				ast.parse("(READ | WRITE) << 2", mode="eval").body,
				values,
			),
			12,
		)
		for expression in ("1 / 0", "unknown | 1", "1 << 64", "call()"):
			with self.subTest(expression=expression):
				self.assertIsNone(
					evaluate_integer_expression(
						ast.parse(expression, mode="eval").body,
						values,
					)
				)

	def test_veroscript_identifier_rules_match_the_ascii_lexer_and_keywords(self) -> None:
		self.assertTrue(is_veroscript_identifier("value_2"))
		for value in ("", "2value", "do", "constexpr", "int", "café"):
			with self.subTest(value=value):
				self.assertFalse(is_veroscript_identifier(value))

		lexer_path = (
			Path(__file__).resolve().parents[5]
			/ "VeroScript"
			/ "Private"
			/ "VeroScriptLexer.cpp"
		)
		lexer_source = lexer_path.read_text(encoding="utf-8")
		keyword_block = lexer_source.split(
			"static constexpr const char* Keywords[] = {", 1
		)[1].split("};", 1)[0]
		core_keywords = frozenset(re.findall(r'"([^"]+)"', keyword_block))
		self.assertEqual(frontend._VEROSCRIPT_KEYWORDS, core_keywords)

	def test_declarations_are_normalized_without_execution(self) -> None:
		source = '''
from dataclasses import dataclass
from enum import IntEnum

class Mode(IntEnum):
    OFF = 0
    ON = 1

@dataclass
class Counter:
    value: int = 0

    def add(self, amount: int) -> int:
        raise RuntimeError("must not execute")

def combine(left: list[int], right: int = 2) -> int:
    raise RuntimeError("must not execute")
'''
		result = parse_source(source, "sample.py", module_name="sample")
		self.assertTrue(result.succeeded, [item.format() for item in result.diagnostics])
		self.assertEqual([entry.name for entry in result.classes], ["Mode", "Counter"])
		field_type = result.classes[1].fields[0].type
		parameter_type = result.functions[0].parameters[0].type
		self.assertIsNotNone(field_type)
		self.assertIsNotNone(parameter_type)
		assert field_type is not None
		assert parameter_type is not None
		self.assertEqual(field_type.name, "int")
		self.assertEqual(parameter_type.name, "list")
		self.assertEqual(parameter_type.arguments[0].name, "int")
		payload = result.to_dict()
		self.assertEqual(payload["schemaVersion"], 1)
		self.assertEqual(payload["name"], "sample")

	def test_syntax_error_is_structured(self) -> None:
		result = parse_source("def broken(:\n    pass\n", "broken.py")
		self.assertFalse(result.succeeded)
		self.assertEqual(result.diagnostics[0].code, "PYF0002")
		self.assertEqual(result.diagnostics[0].location.path, "broken.py")

	def test_empty_type_subscriptions_remain_distinct_from_bare_types(self) -> None:
		for annotation in ("int[()]", "Int8[()]", "Item[()]", "tuple[()]"):
			with self.subTest(annotation=annotation):
				result = parse_source(f"def consume(value: {annotation}) -> None:\n    pass\n")
				self.assertTrue(result.succeeded, result.diagnostics)
				type_ref = result.functions[0].parameters[0].type
				assert type_ref is not None
				self.assertEqual(type_ref.name, annotation)
				self.assertEqual(type_ref.spelling, annotation)
		result = parse_source("def consume(value: list[Item[()]]) -> None:\n    pass\n")
		type_ref = result.functions[0].parameters[0].type
		assert type_ref is not None
		self.assertEqual(type_ref.arguments[0].name, "Item[()]")

	def test_unsupported_interpreter_versions_fail_closed(self) -> None:
		for version in ((3, 10, 99), (3, 16, 0)):
			with self.subTest(version=version), mock.patch.object(
				sys, "version_info", version
			):
				result = parse_source("def answer() -> int:\n    return 42\n")
			self.assertFalse(result.succeeded)
			self.assertEqual(result.diagnostics[0].code, "PYF0001")
			self.assertIn("Python 3.11-3.15", result.diagnostics[0].message)

	def test_unsupported_class_members_fail_closed(self) -> None:
		for member in ("value = 1", "class Nested:\n        pass", "print('run')"):
			with self.subTest(member=member):
				result = parse_source(f"class Example:\n    {member}\n", "class.py")
				self.assertFalse(result.succeeded)
				self.assertTrue(
					any(item.code == "PYF0006" for item in result.diagnostics)
				)

	def test_class_scope_collisions_fail_closed(self) -> None:
		sources = {
			"duplicate field": "class Example:\n    value: int\n    value: int\n",
			"duplicate method": (
				"class Example:\n"
				"    def value(self) -> int:\n        return 1\n"
				"    def value(self) -> int:\n        return 2\n"
			),
			"field and method": (
				"class Example:\n"
				"    value: int\n"
				"    def value(self) -> int:\n        return 1\n"
			),
			"duplicate enum member": (
				"class Mode(IntEnum):\n    VALUE = 1\n    VALUE = 2\n"
			),
		}
		for name, source in sources.items():
			with self.subTest(name=name):
				result = parse_source(source, f"{name}.py")
				self.assertFalse(result.succeeded)
				self.assertTrue(
					any(item.code == "PYF0003" for item in result.diagnostics),
					[item.format() for item in result.diagnostics],
				)

		property_result = parse_source(
			"class Settings:\n"
			"    @property\n"
			"    def value(self) -> int:\n        return 1\n"
			"    @value.setter\n"
			"    def value(self, new_value: int) -> None:\n        pass\n",
			"property.py",
		)
		self.assertTrue(
			property_result.succeeded,
			[item.format() for item in property_result.diagnostics],
		)

		duplicate_setter = parse_source(
			"class Settings:\n"
			"    @property\n"
			"    def value(self) -> int:\n        return 1\n"
			"    @value.setter\n"
			"    def value(self, new_value: int) -> None:\n        pass\n"
			"    @value.setter\n"
			"    def value(self, new_value: int) -> None:\n        pass\n",
			"duplicate-setter.py",
		)
		self.assertFalse(duplicate_setter.succeeded)
		self.assertTrue(
			any(item.code == "PYF0003" for item in duplicate_setter.diagnostics)
		)

	def test_localization_falls_back_to_english(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / "en-US").mkdir()
			(root / "zh-CN").mkdir()
			(root / "en-US" / "python-tools.json").write_text(
				json.dumps({"python_tools.syntax_error": "English syntax: {0}"}),
				encoding="utf-8",
			)
			(root / "zh-CN" / "python-tools.json").write_text("{}", encoding="utf-8")
			localizer = Localizer("zh-CN", root)
			self.assertEqual(localizer.message("python_tools.syntax_error", "bad"), "English syntax: bad")
			self.assertEqual(
				localizer.message("python_tools.future_message"),
				"unknown localization message: python_tools.future_message",
			)
			(root / "en-US" / "python-aot.json").write_text(
				json.dumps({"python_tools.aot_failed": "English AOT: {0}"}),
				encoding="utf-8",
			)
			(root / "zh-CN" / "python-aot.json").write_text(
				json.dumps({"python_tools.aot_failed": "本地化 AOT：{0}"}),
				encoding="utf-8",
			)
			localizer = Localizer("zh-CN", root)
			self.assertEqual(localizer.message("python_tools.syntax_error", "bad"), "English syntax: bad")
			self.assertEqual(localizer.message("python_tools.aot_failed", "bad"), "本地化 AOT：bad")

	def test_localization_preserves_scripts_and_normalizes_system_tags(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for culture in ("en-US", "zh-CN", "zh-TW", "ja-JP", "pt-BR", "pt-PT-x-pack1"):
				(root / culture).mkdir()
				(root / culture / "python-tools.json").write_text(
					json.dumps({"python_tools.syntax_error": culture + ": {0}"}), encoding="utf-8")
			for requested, expected in (
				("zh-Hant-HK", "zh-TW"), ("zh_Hans.UTF-8", "zh-CN"),
				("zh-Hans-TW", "zh-CN"), ("zh-Hant-CN", "zh-TW"),
				("zh-SG-x-Hant", "zh-CN"), ("zh-MO-x-Hans", "zh-TW"),
				("ja_JP.UTF-8@modifier", "ja-JP"), ("pt-PT", "pt-PT-x-pack1"),
				("C.UTF-8", "en-US"), ("POSIX", "en-US"),
				(" POSIX.UTF-8@modifier ", "en-US"), ("de-DE", "en-US"),
			):
				with self.subTest(requested=requested):
					localizer = Localizer(requested, root)
					self.assertEqual(expected, localizer.language)
					self.assertEqual(expected + ": bad", localizer.message("python_tools.syntax_error", "bad"))
			# Exact and progressively broader tags win before another region.
			(root / "pt-PT").mkdir()
			(root / "pt-PT/python-tools.json").write_text(
				json.dumps({"python_tools.syntax_error": "parent: {0}"}), encoding="utf-8")
			self.assertEqual("parent: bad", Localizer("pt-PT-x-extra", root).message("python_tools.syntax_error", "bad"))

	def test_localization_never_falls_back_to_opposite_chinese_script(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for culture in ("en-US", "zh-CN"):
				(root / culture).mkdir()
				(root / culture / "python-tools.json").write_text(
					json.dumps({"python_tools.syntax_error": culture + ": {0}"}), encoding="utf-8")
			localizer = Localizer("zh-Hant-HK", root)
			self.assertEqual("en-US", localizer.language)
			self.assertEqual("en-US: bad", localizer.message("python_tools.syntax_error", "bad"))

	def test_localization_preserves_catalog_english_and_format_contracts(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for culture in ("en-US", "fr-FR"):
				(root / culture).mkdir()
			english = {"python_tools.syntax_error": "Catalog English: {0}",
					   "python_tools.future_message": "Future English: {0}"}
			(root / "en-US/messages.json").write_text(json.dumps(english), encoding="utf-8")
			for template in ("lost argument", "wrong {1}", "broken {0", "named {name}",
							 "attribute {0.real}", "nested {0:{1:{2}}}", "integer {0:d}"):
				with self.subTest(template=template):
					(root / "fr-FR/messages.json").write_text(json.dumps({
						"python_tools.syntax_error": template,
						"python_tools.future_message": template,
						"python_tools.translation_only": "unowned",
					}), encoding="utf-8")
					localizer = Localizer("fr-FR", root)
					self.assertEqual("Catalog English: bad", localizer.message("python_tools.syntax_error", "bad"))
					self.assertEqual("Future English: bad", localizer.message("python_tools.future_message", "bad"))
					self.assertNotIn("python_tools.translation_only", localizer.messages)
			(root / "fr-FR/messages.json").write_text(json.dumps({
				"python_tools.future_message": "{{literal}} {0!r}",
			}), encoding="utf-8")
			self.assertEqual("{literal} 'bad'", Localizer("fr-FR", root).message("python_tools.future_message", "bad"))

	def test_localization_rejects_corrupt_cultures_and_keeps_fallbacks(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for culture in ("en-US", "fr-FR", "fr-CA"):
				(root / culture).mkdir()
			key = "python_tools.syntax_error"
			(root / "en-US/messages.json").write_text(json.dumps({key: "English: {0}"}), encoding="utf-8")
			(root / "fr-CA/messages.json").write_text(json.dumps({key: "Canadian: {0}"}), encoding="utf-8")
			malformed = ('{"python_tools.syntax_error":"first {0}","python_tools.syntax_error":"second {0}"}',
						 '{"python_tools.syntax_error":2}', '{"python_tools.syntax_error":"   "}',
						 '{"python_tools.syntax_error":"\\ud800"}', '{', '[]', b'\xff')
			for contents in malformed:
				with self.subTest(contents=contents):
					(root / "fr-FR/messages.json").write_bytes(
						contents if isinstance(contents, bytes) else contents.encode("utf-8"))
					localizer = Localizer("fr-FR", root)
					self.assertEqual("fr-CA", localizer.language)
					self.assertEqual("Canadian: bad", localizer.message(key, "bad"))
			(root / "fr-FR/messages.json").write_text(json.dumps({key: "French: {0}"}), encoding="utf-8")
			(root / "fr-FR/duplicate.json").write_text(json.dumps({key: "Duplicate: {0}"}), encoding="utf-8")
			self.assertEqual("fr-CA", Localizer("fr-FR", root).language)
			(root / "en-US/messages.json").write_text(json.dumps({key: "invalid {1}"}), encoding="utf-8")
			self.assertEqual("invalid Python syntax: bad", Localizer("en-US", root).message(key, "bad"))
			(root / "en-US/messages.json").write_text(json.dumps({key: "invalid {0:d}"}), encoding="utf-8")
			self.assertEqual("invalid Python syntax: bad", Localizer("en-US", root).message(key, "bad"))

	def test_localization_rejects_deep_catalog_without_losing_diagnostics(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			key = "python_tools.syntax_error"
			for culture in ("en-US", "fr-FR"):
				(root / culture).mkdir()
			english_path = root / "en-US/messages.json"
			english_path.write_text(json.dumps({key: "English: {0}"}), encoding="utf-8")
			# Newer CPython JSON decoders use a C-stack limit, separate from
			# sys.getrecursionlimit(); exceed either limit with a small input.
			depth = 100_000
			nested = '{"python_tools.syntax_error":' + '[' * depth + '0' + ']' * depth + '}'
			(root / "fr-FR/messages.json").write_text(nested, encoding="utf-8")
			localizer = Localizer("fr-FR", root)
			self.assertEqual("en-US", localizer.language)
			self.assertEqual("English: bad", localizer.message(key, "bad"))
			english_path.write_text(nested, encoding="utf-8")
			self.assertEqual("invalid Python syntax: bad", Localizer("en-US", root).message(key, "bad"))

	def test_localization_formats_all_six_owned_catalogs_from_source_and_deployment(self) -> None:
		python_root = Path(__file__).resolve().parents[2]
		roots = (python_root / "Resources/Localization",
				 *sorted(python_root.glob("VeroScriptPython*/Resources/Localization")))
		cultures = ("en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW")
		with tempfile.TemporaryDirectory() as directory:
			deployed = Path(directory) / "localization"
			for culture in cultures:
				expected = {}
				for root in roots:
					for path in (root / culture).rglob("*.json"):
						entries = json.loads(path.read_text(encoding="utf-8"))
						self.assertFalse(expected.keys() & entries.keys(), path)
						expected.update(entries)
						destination = deployed / culture / path.relative_to(root / culture)
						destination.parent.mkdir(parents=True, exist_ok=True)
						self.assertFalse(destination.exists(), destination)
						destination.write_bytes(path.read_bytes())
				self.assertTrue(expected)
				with mock.patch.dict(os.environ, {"VERO_LOCALIZATION_DIR": ""}):
					source_localizer = Localizer(culture)
				deployed_localizer = Localizer(culture, deployed)
				for localizer in (source_localizer, deployed_localizer):
					with self.subTest(culture=culture, deployed=localizer is deployed_localizer):
						self.assertEqual(culture, localizer.language)
						self.assertEqual(expected, dict(localizer.messages))
						for key, template in expected.items():
							# Actual diagnostics pass strings, including non-ASCII paths.
							argument = "models/错误-é.py:7"
							self.assertEqual(template.format(argument), localizer.message(key, argument), key)

	def test_localization_language_precedence_does_not_depend_on_system_locale(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for culture in ("en-US", "fr-FR", "ja-JP"):
				(root / culture).mkdir()
				(root / culture / "messages.json").write_text(json.dumps({
					"python_tools.syntax_error": culture + ": {0}",
				}), encoding="utf-8")
			with mock.patch.dict(os.environ, {"VERO_LANG": "fr-FR"}), mock.patch(
				"veroscript_python_frontend.localization.locale.getlocale",
				side_effect=ValueError("unknown locale"),
			) as system_locale:
				self.assertEqual("ja-JP", Localizer("ja-JP", root).language)
				self.assertEqual("fr-FR", Localizer(catalog_directory=root).language)
				system_locale.assert_not_called()
				with mock.patch.dict(os.environ, {"VERO_LANG": ""}):
					localizer = Localizer(catalog_directory=root)
					self.assertEqual("en-US", localizer.language)
					self.assertEqual("en-US: bad", localizer.message("python_tools.syntax_error", "bad"))

	def test_project_preserves_relative_module_layout(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "sample"
			package.mkdir()
			first = package / "models.py"
			second = package / "service.py"
			first.write_text("class User:\n    name: str\n", encoding="utf-8")
			second.write_text(
				"from .models import User\n\ndef answer() -> int:\n    return 42\n",
				encoding="utf-8",
			)
			project = parse_files((first, second), project_root=root)
			self.assertTrue(project.succeeded, [item.format() for item in project.diagnostics])
			self.assertEqual(
				[entry.module.name for entry in project.modules],
				["sample.models", "sample.service"],
			)
			self.assertEqual(
				[entry.output_name for entry in project.modules],
				["sample/models.python-ast.json", "sample/service.python-ast.json"],
			)
			self.assertEqual(project.modules[1].module.imports, (".models",))

	def test_atomic_project_writer_rejects_duplicate_targets(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			target = Path(directory) / "result.json"
			with self.assertRaises(ValueError):
				write_text_files_atomic(((target, "one"), (target.resolve(), "two")))
			self.assertFalse(target.exists())

	def test_atomic_writers_do_not_replace_symbolic_links(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			original = root / "original.txt"
			original.write_text("original", encoding="utf-8")
			link = root / "generated.txt"
			try:
				link.symlink_to(original)
			except OSError as error:
				self.skipTest(f"symbolic links are unavailable: {error}")
			with self.assertRaisesRegex(OSError, "symbolic link"):
				write_text_atomic(link, "replacement")
			with self.assertRaisesRegex(OSError, "not a file"):
				write_text_files_atomic(((link, "replacement"),))
			self.assertTrue(link.is_symlink())
			self.assertEqual(original.read_text(encoding="utf-8"), "original")

	def test_multi_file_cli_writes_manifest_and_rejects_collisions(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			package = root / "sample"
			package.mkdir()
			first = package / "models.py"
			second = package / "service.py"
			first.write_text("class User:\n    name: str\n", encoding="utf-8")
			second.write_text("def answer() -> int:\n    return 42\n", encoding="utf-8")
			output = root / "generated"
			environment = os.environ.copy()
			module_path = "" if TEST_INSTALLED else str(Path(__file__).resolve().parents[1] / "python")
			environment["PYTHONPATH"] = os.pathsep.join(
				value for value in (module_path, environment.get("PYTHONPATH", "")) if value
			)
			command = [
				sys.executable,
				"-m",
				"veroscript_python_frontend",
				str(first),
				str(second),
				"--project-root",
				str(root),
				"--output-dir",
				str(output),
			]
			completed = subprocess.run(
				command, check=False, capture_output=True, text=True, env=environment
			)
			self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
			self.assertTrue((output / "frontend.manifest.json").is_file())
			self.assertTrue((output / "sample/models.python-ast.json").is_file())

			collision = subprocess.run(
				[
					*command,
					"--manifest",
					str(output / "sample/models.python-ast.json"),
				],
				check=False,
				capture_output=True,
				text=True,
				env=environment,
			)
			self.assertNotEqual(collision.returncode, 0)
			self.assertIn("distinct", collision.stderr)


if __name__ == "__main__":
	unittest.main()
