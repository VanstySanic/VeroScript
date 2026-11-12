# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from typing import cast
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TEST_INSTALLED = os.environ.get("VEROSCRIPT_TEST_INSTALLED_PACKAGE") == "1"
if not TEST_INSTALLED:
	sys.path.insert(0, str(ROOT / "VeroScriptPythonFrontend" / "python"))
	sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

from veroscript_python_aot import (  # noqa: E402
	PythonAotError,
	PythonAotOptions,
	build_python_aot_command,
	generate_python,
	generate_python_bundle,
)
from veroscript_python_frontend import find_veroscript_executable  # noqa: E402


SOURCE = "void main() { print(42); }\n"


def _write_bundle(root: Path, content: str) -> None:
	files = ["__main__.py", "__init__.py", "_veroscript_program.py"]
	for name in files:
		(root / name).write_text(content, encoding="utf-8")
	(root / "python-aot.manifest.json").write_text(
		json.dumps({
			"version": 1,
			"kind": "veroscript.python-aot-source-bundle",
			"name": "application",
			"entry": "__main__.py",
			"files": files,
		}),
		encoding="utf-8",
	)


class PythonAotApiTests(unittest.TestCase):
	def test_invalid_process_paths_and_arguments_are_structured(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			cases = (
				("bad\0.vs", root / "output.py", ()),
				("input.vs", root / "output.py", ("FEATURE=bad\0value",)),
				("input.vs", root / "bad\0.py", ()),
				("input.vs", root / "bad\0" / "output.py", ()),
			)
			for bundle in (False, True):
				generate_output = generate_python_bundle if bundle else generate_python
				for source, output, defines in cases:
					with self.subTest(bundle=bundle, source=source, output=output, defines=defines):
						options = PythonAotOptions(
							veroscript_executable=sys.executable, defines=defines,
						)
						result = generate_output(source, output, options=options, check=False)
						self.assertFalse(result.succeeded)
						self.assertEqual(result.returncode, 2)
						self.assertEqual(result.files, ())
						self.assertTrue(result.stderr)
						with self.assertRaises(PythonAotError) as raised:
							generate_output(source, output, options=options)
						self.assertEqual(raised.exception.result.returncode, 2)
						self.assertEqual(list(root.iterdir()), [])

	def _assert_link_output_preserved(self, *, bundle: bool, junction: bool = False) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			target = root / ("target" if bundle else "target.py")
			output = root / ("output" if bundle else "output.py")
			if bundle:
				target.mkdir()
				_write_bundle(target, "# original\n")
			else:
				target.write_text("# original\n", encoding="utf-8")
			if junction:
				import _winapi

				_winapi.CreateJunction(str(target), str(output))
			else:
				try:
					output.symlink_to(target, target_is_directory=bundle)
				except OSError as error:
					self.skipTest(f"symbolic links are unavailable: {error}")

			def generate(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
				command = cast(tuple[str, ...], args[0])
				staged = Path(command[command.index("--output-dir" if bundle else "-o") + 1])
				if bundle:
					_write_bundle(staged, "# generated\n")
				else:
					staged.write_text("# generated\n", encoding="utf-8")
				return subprocess.CompletedProcess((), 0, "ok", "")

			with mock.patch("veroscript_python_aot.subprocess.run", side_effect=generate):
				generate_output = generate_python_bundle if bundle else generate_python
				result = generate_output(
					"input.vs", output,
					options=PythonAotOptions(veroscript_executable=sys.executable),
					check=False,
				)
			self.assertFalse(result.succeeded, result)
			original = target / "_veroscript_program.py" if bundle else target
			self.assertEqual(original.read_text(encoding="utf-8"), "# original\n")
			self.assertEqual(output.resolve(), target.resolve())
			self.assertFalse(any(root.glob(".*.veroscript-python-aot-*")))

	def test_file_output_symlink_preserves_target(self) -> None:
		self._assert_link_output_preserved(bundle=False)

	def test_bundle_output_symlink_preserves_target(self) -> None:
		self._assert_link_output_preserved(bundle=True)

	@unittest.skipUnless(sys.platform == "win32", "Windows junctions only")
	def test_bundle_output_junction_preserves_target(self) -> None:
		self._assert_link_output_preserved(bundle=True, junction=True)

	@unittest.skipUnless(sys.platform == "win32", "Windows junctions only")
	def test_real_cli_rejects_bundle_output_junction(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable:
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		import _winapi

		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory).resolve()
			source = root / "app.vs"
			source.write_text(SOURCE, encoding="utf-8")
			for destination_kind in ("empty", "owned", "dangling"):
				with self.subTest(destination_kind=destination_kind):
					target = root / f"{destination_kind}-target"
					output = root / f"{destination_kind}-output"
					target.mkdir()
					options = PythonAotOptions(veroscript_executable=executable)
					if destination_kind == "owned":
						generate_python_bundle(source, target, bundle_name="app", options=options)
					original_files = {path.name: path.read_bytes() for path in target.iterdir()}
					_winapi.CreateJunction(str(target), str(output))
					if destination_kind == "dangling":
						target.rmdir()
					completed = subprocess.run(
						build_python_aot_command(
							source, output_directory=output, bundle_name="app", options=options,
						),
						check=False, capture_output=True, text=True, encoding="utf-8", errors="replace",
						timeout=options.timeout_seconds,
					)
					self.assertNotEqual(completed.returncode, 0, completed.stdout)
					self.assertEqual(output.resolve(), target.resolve())
					if destination_kind == "dangling":
						self.assertFalse(target.exists())
					else:
						self.assertEqual(
							{path.name: path.read_bytes() for path in target.iterdir()}, original_files,
						)

	@unittest.skipUnless(sys.platform == "win32", "Windows reparse-point inspection only")
	def test_output_metadata_failure_is_structured(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory).resolve()
			output = root / "output.py"
			output.write_text("# original\n", encoding="utf-8")
			original_lstat = Path.lstat

			def inspect_path(path: Path) -> os.stat_result:
				if path == output:
					raise PermissionError("cannot inspect output metadata")
				return original_lstat(path)

			def generate(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
				command = cast(tuple[str, ...], args[0])
				Path(command[command.index("-o") + 1]).write_text("# generated\n", encoding="utf-8")
				return subprocess.CompletedProcess((), 0, "ok", "")

			with mock.patch.object(Path, "lstat", inspect_path), mock.patch(
				"veroscript_python_aot.subprocess.run", side_effect=generate
			):
				result = generate_python(
					"input.vs", output,
					options=PythonAotOptions(veroscript_executable=sys.executable),
					check=False,
				)
				self.assertFalse(result.succeeded)
				self.assertIn("cannot inspect output metadata", result.stderr)
				with self.assertRaises(PythonAotError) as raised:
					generate_python(
						"input.vs", output,
						options=PythonAotOptions(veroscript_executable=sys.executable),
					)
				self.assertFalse(raised.exception.result.succeeded)
			self.assertEqual(output.read_text(encoding="utf-8"), "# original\n")
			self.assertFalse(any(root.glob(".*.veroscript-python-aot-*")))

	def test_bundle_commit_failure_restores_previous_output(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			output = root / "bundle"
			output.mkdir()
			_write_bundle(output, "# original\n")
			original_replace = os.replace

			def replace(source: Path, destination: Path) -> None:
				if source.name.startswith(".bundle.veroscript-python-aot-"):
					raise PermissionError("cannot commit generated bundle")
				original_replace(source, destination)

			def generate(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
				command = cast(tuple[str, ...], args[0])
				_write_bundle(Path(command[command.index("--output-dir") + 1]), "# generated\n")
				return subprocess.CompletedProcess((), 0, "ok", "")

			with mock.patch("veroscript_python_aot.os.replace", side_effect=replace), mock.patch(
				"veroscript_python_aot.subprocess.run", side_effect=generate
			):
				result = generate_python_bundle(
					"input.vs", output,
					options=PythonAotOptions(veroscript_executable=sys.executable),
					check=False,
				)
			self.assertFalse(result.succeeded)
			self.assertIn("cannot commit generated bundle", result.stderr)
			self.assertEqual((output / "_veroscript_program.py").read_text(encoding="utf-8"), "# original\n")
			self.assertEqual(set(root.iterdir()), {output})

	def test_links_in_output_parent_remain_supported(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			target = root / "target"
			target.mkdir()
			parent = root / "parent"
			if sys.platform == "win32":
				import _winapi

				_winapi.CreateJunction(str(target), str(parent))
			else:
				parent.symlink_to(target, target_is_directory=True)

			def generate(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
				command = cast(tuple[str, ...], args[0])
				if "--output-dir" in command:
					_write_bundle(Path(command[command.index("--output-dir") + 1]), "# generated\n")
				else:
					Path(command[command.index("-o") + 1]).write_text("# generated\n", encoding="utf-8")
				return subprocess.CompletedProcess((), 0, "ok", "")

			with mock.patch("veroscript_python_aot.subprocess.run", side_effect=generate):
				for bundle in (False, True):
					with self.subTest(bundle=bundle):
						output = parent / ("bundle" if bundle else "output.py")
						generate_output = generate_python_bundle if bundle else generate_python
						result = generate_output(
							"input.vs", output,
							options=PythonAotOptions(veroscript_executable=sys.executable),
						)
						self.assertTrue(result.succeeded)
						self.assertEqual(Path(result.output_path), target.resolve() / output.name)
						for filename in result.files:
							self.assertTrue(Path(filename).is_file())

	def test_external_sdk_root_is_discovered(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			sdk = Path(directory)
			executable = sdk / "bin" / (
				"veroscript.exe" if sys.platform == "win32" else "veroscript"
			)
			executable.parent.mkdir()
			executable.write_bytes(b"")
			executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
			self.assertEqual(
				find_veroscript_executable(sdk), str(executable.resolve())
			)
			with mock.patch.dict(
				os.environ,
				{"VEROSCRIPT_SDK": str(sdk)},
				clear=True,
			):
				self.assertEqual(
					find_veroscript_executable(), str(executable.resolve())
				)

	def test_command_preserves_all_compiler_options(self) -> None:
		command = build_python_aot_command(
			"app.vs",
			output_directory="generated",
			bundle_name="application",
			options=PythonAotOptions(
				veroscript_executable=sys.executable,
				import_paths=("modules",),
				defines=("FEATURE=1",),
				implicit_imports=("prelude.vs",),
				allow_circular_imports=False,
				allow_project_wide_access=True,
			),
		)
		self.assertEqual(command[:3], (str(Path(sys.executable).resolve()), "aot", "--target=python"))
		self.assertIn(("-I", "modules"), tuple(zip(command, command[1:])))
		self.assertIn(("-D", "FEATURE=1"), tuple(zip(command, command[1:])))
		self.assertIn(
			("--implicit-import", "prelude.vs"), tuple(zip(command, command[1:]))
		)
		self.assertIn("--no-circular-imports", command)
		self.assertIn("--allow-project-wide-access", command)
		self.assertEqual(command[-4:], ("--output-dir", "generated", "--bundle-name", "application"))

	def test_missing_external_toolchain_is_structured(self) -> None:
		with tempfile.TemporaryDirectory() as directory, mock.patch.dict(os.environ, {}, clear=True), mock.patch(
			"veroscript_python_frontend._toolchain.shutil.which", return_value=None
		):
			output = Path(directory) / "output.py"
			result = generate_python(
				"input.vs", output, check=False
			)
			self.assertFalse(result.succeeded)
			self.assertEqual(result.returncode, 127)
			self.assertIn("VEROSCRIPT_SDK", result.stderr)
			with self.assertRaises(PythonAotError):
				generate_python("input.vs", output)

	def test_invalid_timeouts_are_structured(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for timeout in (0.0, -1.0, float("nan"), float("inf")):
				with self.subTest(timeout=timeout):
					result = generate_python(
						"input.vs",
						root / "output.py",
						options=PythonAotOptions(
							veroscript_executable=sys.executable,
							timeout_seconds=timeout,
						),
						check=False,
					)
					self.assertFalse(result.succeeded)
					self.assertEqual(result.returncode, 2)
					self.assertEqual(result.command, ())
					self.assertIn("finite positive number", result.stderr)

			environment = os.environ.copy()
			module_paths = [] if TEST_INSTALLED else [
				str(ROOT / "VeroScriptPythonFrontend" / "python"),
				str(Path(__file__).resolve().parents[1] / "python"),
			]
			if environment.get("PYTHONPATH"):
				module_paths.append(environment["PYTHONPATH"])
			environment["PYTHONPATH"] = os.pathsep.join(module_paths)
			completed = subprocess.run(
				[
					sys.executable,
					"-m",
					"veroscript_python_aot",
					"input.vs",
					"-o",
					str(root / "cli.py"),
					"--timeout",
					"nan",
					"--veroscript",
					sys.executable,
				],
				check=False,
				capture_output=True,
				text=True,
				encoding="utf-8",
				errors="replace",
				env=environment,
			)
			self.assertEqual(completed.returncode, 1)
			self.assertIn("finite positive number", completed.stderr)
			self.assertNotIn("Traceback", completed.stderr)

	def test_success_without_output_fails_closed(self) -> None:
		completed = subprocess.CompletedProcess[str]((), 0, "ok", "")
		with tempfile.TemporaryDirectory() as directory, mock.patch(
			"veroscript_python_aot.subprocess.run", return_value=completed
		):
			result = generate_python(
				"input.vs",
				Path(directory) / "missing.py",
				options=PythonAotOptions(veroscript_executable=sys.executable),
				check=False,
			)
		self.assertFalse(result.succeeded)
		self.assertIn("produced no valid output", result.stderr)

	def test_bundle_manifest_is_verified(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			output = Path(directory) / "bundle"

			def generate(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
				command = cast(tuple[str, ...], args[0])
				staged = Path(command[command.index("--output-dir") + 1])
				for name in ("__main__.py", "__init__.py", "_veroscript_program.py"):
					(staged / name).write_text("# generated\n", encoding="utf-8")
				(staged / "python-aot.manifest.json").write_text(
					json.dumps(
						{
							"version": 1,
							"kind": "veroscript.python-aot-source-bundle",
							"name": "application",
							"entry": "__main__.py",
							"files": [
								"__main__.py",
								"__init__.py",
								"_veroscript_program.py",
							],
						}
					),
					encoding="utf-8",
				)
				return subprocess.CompletedProcess((), 0, "ok", "")

			with mock.patch("veroscript_python_aot.subprocess.run", side_effect=generate):
				result = generate_python_bundle(
					"input.vs",
					output,
					options=PythonAotOptions(veroscript_executable=sys.executable),
				)
			self.assertTrue(result.succeeded)
			self.assertEqual(len(result.files), 3)
			self.assertEqual(
				result.manifest_path,
				str((output / "python-aot.manifest.json").resolve()),
			)
			cache = output / "__pycache__"
			cache.mkdir()
			(cache / "__main__.cpython-314.pyc").write_bytes(b"cache")
			with mock.patch("veroscript_python_aot.subprocess.run", side_effect=generate):
				replacement = generate_python_bundle(
					"input.vs",
					output,
					options=PythonAotOptions(veroscript_executable=sys.executable),
				)
			self.assertTrue(replacement.succeeded)
			self.assertFalse(cache.exists())

	def test_stale_single_file_is_not_accepted_as_new_output(self) -> None:
		completed = subprocess.CompletedProcess[str]((), 0, "ok", "")
		with tempfile.TemporaryDirectory() as directory, mock.patch(
			"veroscript_python_aot.subprocess.run", return_value=completed
		):
			output = Path(directory) / "old.py"
			output.write_text("# old\n", encoding="utf-8")
			result = generate_python(
				"input.vs", output,
				options=PythonAotOptions(veroscript_executable=sys.executable),
				check=False,
			)
			self.assertFalse(result.succeeded)
			self.assertEqual(output.read_text(encoding="utf-8"), "# old\n")

	def test_empty_bundle_output_directory_is_supported(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			output = Path(directory) / "bundle"
			output.mkdir()

			def generate(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
				command = cast(tuple[str, ...], args[0])
				staged = Path(command[command.index("--output-dir") + 1])
				_write_bundle(staged, "# generated\n")
				return subprocess.CompletedProcess((), 0, "ok", "")

			with mock.patch("veroscript_python_aot.subprocess.run", side_effect=generate):
				result = generate_python_bundle(
					"input.vs", output,
					options=PythonAotOptions(veroscript_executable=sys.executable),
					check=False,
				)
			self.assertTrue(result.succeeded, result.stderr)
			self.assertEqual(len(result.files), 3)
			self.assertEqual((output / "__main__.py").read_text(encoding="utf-8"), "# generated\n")
			self.assertEqual(list(output.parent.iterdir()), [output])

	def test_bundle_with_unexpected_previous_files_is_not_replaced(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			output = Path(directory) / "bundle"
			output.mkdir()
			(output / "unexpected.py").write_text("# keep\n", encoding="utf-8")

			def generate(*args: object, **kwargs: object) -> subprocess.CompletedProcess[str]:
				command = cast(tuple[str, ...], args[0])
				staged = Path(command[command.index("--output-dir") + 1])
				for name in ("__main__.py", "__init__.py", "_veroscript_program.py"):
					(staged / name).write_text("# generated\n", encoding="utf-8")
				(staged / "python-aot.manifest.json").write_text(
					json.dumps({
						"version": 1,
						"kind": "veroscript.python-aot-source-bundle",
						"name": "application",
						"entry": "__main__.py",
						"files": ["__main__.py", "__init__.py", "_veroscript_program.py"],
					}),
					encoding="utf-8",
				)
				return subprocess.CompletedProcess((), 0, "ok", "")

			with mock.patch("veroscript_python_aot.subprocess.run", side_effect=generate):
				result = generate_python_bundle(
					"input.vs", output,
					options=PythonAotOptions(veroscript_executable=sys.executable),
					check=False,
				)
			self.assertFalse(result.succeeded)
			self.assertTrue((output / "unexpected.py").is_file())

	def test_real_cli_runtime_features_preserve_existing_outputs(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable:
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		cases = (
			("int next(){static int value=0;return ++value;}void main(){print(next());}",
			 "local static storage requires"),
			("void main(){auto next=[value=0]() mutable -> int{return ++value;};print(next());}",
			 "source lambdas are not supported"),
			("int count(int... values){return cast<int>(len(values));}void main(){fn(array<int>)->int callback=count;print(callback(array<int>{1,2,3}));}",
			 "variadic function values are not supported"),
			("namespace Super{int value(){return 42;}}class Base{public:int value=7;}class Derived:Base{public:int read(){return Super::value;}}void main(){Derived value=Derived();print(value.read());}",
			 "Super receivers are not supported"),
		)
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			source = root / "app.vs"
			options = PythonAotOptions(veroscript_executable=executable)
			for bundle in (False, True):
				generate = generate_python_bundle if bundle else generate_python
				output = root / ("bundle" if bundle else "app.py")
				source.write_text(SOURCE, encoding="utf-8")
				self.assertTrue(generate(source, output, options=options).succeeded)
				paths = sorted(output.rglob("*")) if bundle else [output]
				original = {path: path.read_bytes() for path in paths if path.is_file()}
				for content, diagnostic in cases:
					with self.subTest(bundle=bundle, diagnostic=diagnostic):
						source.write_text(content, encoding="utf-8")
						result = generate(source, output, options=options, check=False)
						self.assertFalse(result.succeeded)
						self.assertNotEqual(result.returncode, 0)
						self.assertEqual(result.files, ())
						self.assertIn(diagnostic, result.stderr)
						self.assertEqual({path: path.read_bytes() for path in original}, original)
						if bundle:
							self.assertEqual(sorted(output.rglob("*")), paths)
						self.assertFalse(any(root.glob(".*.veroscript-python-aot-*")))

	def test_real_cli_generates_and_runs_file_and_bundle(self) -> None:
		executable = os.environ.get("VEROSCRIPT_EXECUTABLE")
		if not executable:
			self.skipTest("VEROSCRIPT_EXECUTABLE is not available")
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			source = root / "app.vs"
			source.write_text(SOURCE, encoding="utf-8")
			options = PythonAotOptions(veroscript_executable=executable)
			generated = root / "nested" / "app.py"
			file_result = generate_python(source, generated, options=options)
			self.assertTrue(file_result.succeeded)
			completed = subprocess.run(
				[sys.executable, str(generated)],
				check=False,
				capture_output=True,
				text=True,
				encoding="utf-8",
				errors="replace",
			)
			self.assertEqual(completed.returncode, 0, completed.stderr)
			self.assertEqual(completed.stdout.strip(), "42")

			cli_output = root / "cli.py"
			environment = os.environ.copy()
			module_paths = [] if TEST_INSTALLED else [
				str(ROOT / "VeroScriptPythonFrontend" / "python"),
				str(Path(__file__).resolve().parents[1] / "python"),
			]
			if environment.get("PYTHONPATH"):
				module_paths.append(environment["PYTHONPATH"])
			environment["PYTHONPATH"] = os.pathsep.join(module_paths)
			completed = subprocess.run(
				[
					sys.executable,
					"-m",
					"veroscript_python_aot",
					str(source),
					"-o",
					str(cli_output),
					"--veroscript",
					executable,
				],
				check=False,
				capture_output=True,
				text=True,
				encoding="utf-8",
				errors="replace",
				env=environment,
			)
			self.assertEqual(completed.returncode, 0, completed.stderr)
			self.assertTrue(cli_output.is_file())

			bundle = root / "app_\N{SNAKE}"
			bundle.mkdir()
			bundle_result = generate_python_bundle(
				source, bundle, bundle_name="app", options=options
			)
			self.assertTrue(bundle_result.succeeded)
			completed = subprocess.run(
				[sys.executable, str(bundle)],
				check=False,
				capture_output=True,
				text=True,
				encoding="utf-8",
				errors="replace",
			)
			self.assertEqual(completed.returncode, 0, completed.stderr)
			self.assertEqual(completed.stdout.strip(), "42")

			replacement = generate_python_bundle(
				source, bundle, bundle_name="app", options=options
			)
			self.assertTrue(replacement.succeeded)


if __name__ == "__main__":
	unittest.main()
