# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise project initialization, non-overwriting creation and build consumption."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import unittest


class ProjectInitTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix="vero project init ")
		self.addCleanup(self.temporary.cleanup)
		self.root = Path(self.temporary.name).resolve()

	def run_cli(self, *arguments, cwd=None, code=0):
		result = subprocess.run([str(self.cli), *map(str, arguments)], cwd=cwd or self.root,
			capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)
		self.assertEqual(result.returncode, code, f"{arguments}\n{result.stdout}\n{result.stderr}")
		return result

	def verify_project(self, root):
		project = json.loads((root / "veroscript-project.json").read_text(encoding="utf-8"))
		build = json.loads((root / "veroscript-build.json").read_text(encoding="utf-8"))
		self.assertEqual(project["format"], "veroscript-project")
		self.assertEqual(project["formatVersion"], "0.1.1")
		self.assertEqual(project["version"], "0.1.1")
		self.assertRegex(project["name"], r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
		self.assertTrue(2 <= len(project["name"]) <= 64)
		self.assertEqual(project["dependencies"], {})
		self.assertEqual(build["format"], "veroscript-build")
		self.assertEqual(build["formatVersion"], "0.1.1")
		for name in build["defaultTargets"]:
			self.assertIn(name, build["targets"])
		entry = build["targets"]["application"]["entry"]
		self.assertTrue((root / entry).is_file())
		self.assertFalse((root / "veroscript.lock").exists())
		self.assertFalse((root / ".veroscript").exists())
		self.assertFalse((root / "dist").exists())
		return project, build

	def test_current_directory(self):
		self.run_cli("project", "init")
		self.verify_project(self.root)
		self.assertEqual({p.name for p in self.root.iterdir()},
			{"veroscript-project.json", "veroscript-build.json", "main.vs"})

	def test_relative_absolute_and_unicode_paths(self):
		for index, spelling in enumerate(("nested/My App 中文", "absolute/项目", "equals/another project")):
			with self.subTest(spelling=spelling):
				selected = self.root / spelling
				if index == 0:
					self.run_cli("project", "init", "--path", "./nested/../" + spelling)
				elif index == 1:
					self.run_cli("project", "init", "--path", selected)
				else:
					self.run_cli("project", "init", "--path=" + str(selected))
				self.verify_project(selected)
		self.assertFalse((self.root / "veroscript-project.json").exists())

	def test_names_are_valid_without_changing_directory_name(self):
		for folder in ("My App_中文", "纯中文", "X", "A" * 100):
			with self.subTest(folder=folder):
				selected = self.root / folder
				self.run_cli("project", "init", "--path", selected)
				self.verify_project(selected)
				self.assertTrue(selected.is_dir())

	def test_repeated_initialization_preserves_all_bytes_and_timestamps(self):
		self.run_cli("project", "init")
		for name in ("veroscript-project.json", "veroscript-build.json", "main.vs"):
			(self.root / name).write_bytes(("custom user content: " + name + "\r\n").encode())
		before = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in self.root.iterdir()}
		result = self.run_cli("project", "init")
		self.assertIn("Kept ", result.stdout)
		self.assertEqual(before, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in self.root.iterdir()})

	def test_missing_build_preserves_project_and_source(self):
		project = self.root / "veroscript-project.json"
		entry = self.root / "main.vs"
		project.write_bytes(b"existing project\r\n")
		entry.write_bytes(b"int main(){return 19;}\r\n")
		self.run_cli("project", "init")
		self.assertEqual(project.read_bytes(), b"existing project\r\n")
		self.assertEqual(entry.read_bytes(), b"int main(){return 19;}\r\n")
		self.assertEqual(json.loads((self.root / "veroscript-build.json").read_text())["targets"]["application"]["entry"], "main.vs")

	def test_existing_read_only_files_are_kept(self):
		self.run_cli("project", "init")
		for path in self.root.iterdir():
			os.chmod(path, 0o444)
			self.addCleanup(os.chmod, path, 0o666)
		self.run_cli("project", "init")
		self.verify_project(self.root)

	def test_existing_build_keeps_its_entry_policy(self):
		build = self.root / "veroscript-build.json"
		build.write_bytes(b"custom build with a different entry\r\n")
		self.run_cli("project", "init")
		self.assertEqual(build.read_bytes(), b"custom build with a different entry\r\n")
		self.assertTrue((self.root / "veroscript-project.json").is_file())
		self.assertFalse((self.root / "main.vs").exists())

	def test_help_and_invalid_arguments_do_not_create_files(self):
		selected = self.root / "must not be created"
		for arguments in (("project", "--help"), ("project", "init", "--help"),
			("project", "init", "--path", selected, "-h")):
			self.run_cli(*arguments)
		for arguments in (("project",), ("project", "unknown"), ("project", "init", "--path"),
			("project", "init", "--path="), ("project", "init", "--path", ""),
			("project", "init", "--path", selected, "--unknown"),
			("project", "init", "--path", selected, "--path=other"),
			("project", "init", "--path", "--help"), ("project", "init", "positional"),
			("project", "init", "--root"), ("project", "init", "--force")):
			with self.subTest(arguments=arguments):
				self.run_cli(*arguments, code=2)
		self.assertEqual(list(self.root.iterdir()), [])

	def test_directory_conflict_is_detected_before_creating_files(self):
		for name in ("veroscript-project.json", "veroscript-build.json", "main.vs"):
			with self.subTest(name=name):
				selected = self.root / name.replace(".", "-")
				(selected / name).mkdir(parents=True)
				self.run_cli("project", "init", "--path", selected, code=2)
				self.assertEqual([p.name for p in selected.iterdir()], [name])

	def test_non_directory_project_path(self):
		file = self.root / "user-file"
		file.write_bytes(b"preserve")
		self.run_cli("project", "init", "--path", file, code=2)
		self.run_cli("project", "init", "--path", file / "nested", code=2)
		self.assertEqual(file.read_bytes(), b"preserve")

	def test_link_destination_is_not_followed(self):
		external = self.root / "external"
		external.write_bytes(b"preserve")
		link = self.root / "veroscript-project.json"
		try:
			link.symlink_to(external)
		except OSError as error:
			self.skipTest(f"Symlink creation is unavailable: {error}")
		self.run_cli("project", "init", code=2)
		self.assertEqual(external.read_bytes(), b"preserve")
		self.assertTrue(link.is_symlink())
		self.assertFalse((self.root / "veroscript-build.json").exists())

	@unittest.skipIf(os.name == "nt", "POSIX file-size limits are required")
	def test_failed_writes_remove_incomplete_files_and_allow_retry(self):
		import resource

		def limit_file_size():
			signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
			resource.setrlimit(resource.RLIMIT_FSIZE, (1, 1))

		for existing_project in (False, True):
			with self.subTest(existing_project=existing_project):
				selected = self.root / str(existing_project)
				selected.mkdir()
				if existing_project:
					self.run_cli("project", "init", "--path", selected)
					(selected / "main.vs").unlink()
					(selected / "veroscript-build.json").unlink()
				before = {path.name: path.read_bytes() for path in selected.iterdir()}
				result = subprocess.run([str(self.cli), "project", "init", "--path", str(selected)],
					cwd=self.root, capture_output=True, timeout=90, preexec_fn=limit_file_size)
				self.assertEqual(result.returncode, 2, result.stderr)
				self.assertEqual(before, {path.name: path.read_bytes() for path in selected.iterdir()})
				self.run_cli("project", "init", "--path", selected)
				self.verify_project(selected)

	def test_concurrent_initializers_preserve_existing_source(self):
		for existing_source in (False, True):
			with self.subTest(existing_source=existing_source):
				selected = self.root / str(existing_source)
				selected.mkdir()
				entry = selected / "main.vs"
				if existing_source:
					entry.write_bytes(b"int main(){return 23;}\n")
				with ThreadPoolExecutor(max_workers=4) as pool:
					list(pool.map(lambda _: self.run_cli("project", "init", "--path", selected), range(8)))
				self.verify_project(selected)
				if existing_source:
					self.assertEqual(entry.read_bytes(), b"int main(){return 23;}\n")

	def test_generated_configuration_builds_and_runs(self):
		if not self.build:
			self.skipTest("Build integration requires the full CLI with build services")
		self.run_cli("project", "init")
		self.verify_project(self.root)
		self.build_and_run()

	def test_documented_project_example_builds_and_runs(self):
		if not self.build:
			self.skipTest("Build integration requires the full CLI with build services")
		example = self.repo / "VeroScriptCli/Resources/Examples/Project"
		for name in ("veroscript-project.json", "veroscript-build.json", "main.vs"):
			shutil.copy2(example / name, self.root / name)
		self.verify_project(self.root)
		self.build_and_run()

	def build_and_run(self):
		plan = json.loads(self.run_cli("build", "--plan", "--json").stdout)
		self.assertEqual(plan["format"], "veroscript-build-plan")
		self.assertFalse((self.root / "dist").exists())
		result = json.loads(self.run_cli("build", "--json").stdout)
		application = next(item["path"] for target in result["targets"]
			for item in target["artifacts"] if item["kind"] == "application")
		self.assertTrue(Path(application).is_file())
		self.run_cli(application)


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--cli", type=Path, required=True)
	parser.add_argument("--repo", type=Path, required=True)
	parser.add_argument("--build", action="store_true")
	arguments = parser.parse_args()
	ProjectInitTests.cli = arguments.cli.resolve()
	ProjectInitTests.repo = arguments.repo.resolve()
	ProjectInitTests.build = arguments.build
	unittest.main(argv=[__file__], verbosity=2)
