# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Module ownership, deployment boundaries and incremental catalog staging."""
import ast
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[4]
MODULE = Path(__file__).resolve().parents[4]
SCRIPT = MODULE / "Resources/Tools/CMake/VeroScriptLocalization.cmake"
LOCALES = set(json.loads((MODULE / "Resources/Docs/documentation.json").read_text(encoding="utf-8"))["locales"])
JETBRAINS_LOCALIZATION = MODULE / "Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Localization"
VSCODE_LOCALIZATION = MODULE / "Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Localization"


def read_unique(path):
	def object_pairs(pairs):
		result = {}
		for key, value in pairs:
			if key in result:
				raise ValueError(f"Duplicate message ID in {path}: {key}")
			result[key] = value
		return result
	return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=object_pairs)


class LocalizationTests(unittest.TestCase):
	def test_json_network_literal_diagnostic_ids_have_catalog_entries(self):
		roots = (MODULE / 'Modules/Network/VeroScriptJson',
				 *(MODULE / 'Modules/Network' / ('VeroScript' + name) for name in ('Network', 'Http', 'WebSocket')))
		entries = {}
		files = []
		for root in roots:
			for path in (root / 'Resources/Localization/en-US').glob('*.json'):
				entries.update(read_unique(path))
			for scope in ('Private', 'Public', 'Internal'):
				files.extend(path for path in (root / scope).rglob('*') if path.suffix in {'.h', '.cpp', '.inl'})

		used = set()
		for path in files:
			used.update(re.findall(r'"((?:json|network|http|websocket)\.[a-z_.]+)"', path.read_text(encoding='utf-8')))
		self.assertTrue(used)
		self.assertFalse(used - entries.keys(), sorted(used - entries.keys()))

	def test_owned_catalogs_have_matching_translations_and_no_duplicate_ids(self):
		# Discover module-owned diagnostics so adding an SDK or tool cannot
		# silently leave its translations outside this validation gate.
		roots = sorted(set(MODULE.glob("*/Resources/Localization"))
					   | set(MODULE.glob("Extensions/*/Resources/Localization"))
					   | set(MODULE.glob("Extensions/*/*/Resources/Localization"))
					   | set(MODULE.glob("Modules/*/Resources/Localization"))
					   | set(MODULE.glob("Modules/*/*/Resources/Localization")))
		self.assertTrue(roots)
		messages = {}
		for root in roots:
			# JetBrains UI uses Java resource bundles, validated separately below.
			if root in (JETBRAINS_LOCALIZATION,
						MODULE / "Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Localization"):
				continue
			uses_bundles = root in (VSCODE_LOCALIZATION,
				MODULE / "Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Localization")
			directories = LOCALES | {"Bundles"} if uses_bundles else LOCALES
			self.assertEqual(directories, {p.name for p in root.iterdir() if p.is_dir()}, root)
			english_files = {p.relative_to(root / "en-US") for p in (root / "en-US").rglob("*.json")}
			self.assertTrue(english_files, root)
			for culture in root.iterdir():
				if not culture.is_dir() or culture.name == "Bundles":
					continue
				self.assertEqual(english_files, {p.relative_to(culture) for p in culture.rglob("*.json")})
				# VS Code manifest keys belong to each installed extension.
				scope = root if uses_bundles else None
				owned = messages.setdefault((scope, culture.name), {})
				for relative in english_files:
					entries = read_unique(culture / relative)
					english = read_unique(root / "en-US" / relative)
					self.assertEqual(english.keys(), entries.keys())
					self.assertFalse(owned.keys() & entries.keys(), culture / relative)
					self.assertTrue(all(isinstance(value, str) and value for value in entries.values()))
					for key, value in entries.items():
						self.assertEqual(set(re.findall(r'\{(\d+)\}', english[key])),
										 set(re.findall(r'\{(\d+)\}', value)),
										 f"{culture / relative}: {key}")
					owned.update(entries)

	def test_jetbrains_bundles_have_all_languages_keys_and_format_arguments(self):
		root = JETBRAINS_LOCALIZATION / "messages"
		expected = {"VeroScriptBundle" + ("" if locale == "en-US" else "_" + locale.replace("-", "_"))
					+ ".properties" for locale in LOCALES}
		self.assertEqual(expected, {p.name for p in root.iterdir()})
		bundles = {}
		for path in sorted(root.iterdir()):
			entries = {}
			for line in path.read_text(encoding="utf-8").splitlines():
				if not line.strip() or line.lstrip().startswith(("#", "!")):
					continue
				# Owned bundles use explicit, single-line key=value entries.
				key, separator, value = line.partition("=")
				self.assertTrue(separator and re.fullmatch(r"[\w.]+", key), path)
				self.assertNotIn(key, entries, path)
				self.assertTrue(value.strip() and not value.endswith("\\"), path)
				entries[key] = value
			self.assertTrue(entries, path)
			bundles[path.name] = entries
		english = bundles["VeroScriptBundle.properties"]
		for name, entries in bundles.items():
			self.assertEqual(english.keys(), entries.keys(), name)
			for key, value in entries.items():
				self.assertEqual(set(re.findall(r"\{(\d+)\}", english[key])),
								 set(re.findall(r"\{(\d+)\}", value)), f"{name}: {key}")

	def test_jetbrains_ui_message_references_resolve_in_owned_bundle(self):
		english = (JETBRAINS_LOCALIZATION / "messages/VeroScriptBundle.properties").read_text(encoding="utf-8")
		keys = {line.partition("=")[0] for line in english.splitlines()
				if line.strip() and not line.lstrip().startswith(("#", "!"))}
		sources = JETBRAINS_LOCALIZATION.parent.parent / "src/main/kotlin"
		referenced = set()
		for path in sources.rglob("*.kt"):
			referenced.update(re.findall(r'\bmessage\("([\w.]+)"', path.read_text(encoding="utf-8")))
		self.assertTrue(referenced)
		self.assertFalse(referenced - keys, sorted(referenced - keys))

	def test_vscode_bundles_have_all_languages_keys_and_format_arguments(self):
		root = VSCODE_LOCALIZATION / "Bundles"
		suffixes = {"en-US": "", "fr-FR": ".fr", "ja-JP": ".ja", "ko-KR": ".ko",
					"zh-CN": ".zh-cn", "zh-TW": ".zh-tw"}
		self.assertEqual(LOCALES, suffixes.keys())
		self.assertEqual({f"bundle.l10n{suffix}.json" for suffix in suffixes.values()},
						 {p.name for p in root.iterdir()})
		english = read_unique(root / "bundle.l10n.json")
		self.assertTrue(english)
		for suffix in suffixes.values():
			path = root / f"bundle.l10n{suffix}.json"
			entries = read_unique(path)
			self.assertEqual(english.keys(), entries.keys(), path)
			for key, value in entries.items():
				self.assertTrue(isinstance(value, str) and value.strip(), f"{path}: {key}")
				self.assertEqual(set(re.findall(r"\{(\d+)\}", english[key])),
								 set(re.findall(r"\{(\d+)\}", value)), f"{path}: {key}")

	def test_ui_catalogs_have_all_languages_keys_and_format_arguments(self):
		# GUI catalogs have separate identity spaces from the runtime and each other.
		for module in ("VeroScriptInstaller",):
			root = MODULE / module / "App/Resources/Localization"
			self.assertEqual(LOCALES, {p.name for p in root.iterdir() if p.is_dir()}, root)
			files = {p.relative_to(root / "en-US") for p in (root / "en-US").rglob("*.json")}
			self.assertTrue(files, root)
			for locale in sorted(LOCALES):
				seen = set()
				self.assertEqual(files, {p.relative_to(root / locale) for p in (root / locale).rglob("*.json")})
				for relative in files:
					english = read_unique(root / "en-US" / relative)
					actual = read_unique(root / locale / relative)
					self.assertEqual(english.keys(), actual.keys(), root / locale / relative)
					self.assertFalse(seen & actual.keys(), root / locale / relative)
					seen.update(actual)
					for key, value in actual.items():
						self.assertIsInstance(value, str)
						self.assertTrue(value.strip(), root / locale / relative)
						# Composite format permits escaped braces, alignment and formats.
						pattern = r'(?<!\{)\{(\d+)(?:[^{}]*)\}(?!\})'
						self.assertEqual(set(re.findall(pattern, english[key])), set(re.findall(pattern, value)),
										 f"{root / locale / relative}: {key}")

	def test_vscode_ui_and_manifest_references_resolve_in_owned_catalogs(self):
		root = VSCODE_LOCALIZATION.parent.parent
		english = read_unique(VSCODE_LOCALIZATION / "Bundles/bundle.l10n.json")
		referenced = set()
		literal = r"\bvscode\.l10n\.t\(\s*('(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\")"
		for path in (root / "Private").glob("*.ts"):
			for value in re.findall(literal, path.read_text(encoding="utf-8")):
				referenced.add(ast.literal_eval(value))
		self.assertTrue(referenced)
		self.assertFalse(referenced - english.keys(), sorted(referenced - english.keys()))
		manifest = (root / "package.json").read_text(encoding="utf-8")
		keys = set(re.findall(r'"%([\w.-]+)%"', manifest))
		self.assertTrue(keys)
		owned = read_unique(VSCODE_LOCALIZATION / "en-US/manifest.json")
		self.assertFalse(keys - owned.keys(), sorted(keys - owned.keys()))

	def test_installer_ui_references_resolve_in_owned_catalogs(self):
		root = MODULE / "VeroScriptInstaller/App"
		owned = {key for path in (root / "Resources/Localization/en-US").glob("*.json")
				 for key in read_unique(path)}
		referenced = set()
		for path in root.rglob("*.cs"):
			if {"obj", "bin"} & set(path.relative_to(root).parts):
				continue
			source = path.read_text(encoding="utf-8")
			referenced.update(re.findall(r'\b_?localizer\["([\w.-]+)"\]', source))
			referenced.update(re.findall(r'new InstallerStatus\("([\w.-]+)"', source))
		self.assertTrue(referenced)
		self.assertFalse(referenced - owned, sorted(referenced - owned))

	def stage(self, output, profile, repo=REPO, check=True):
		return subprocess.run(["cmake", f"-DVEROSCRIPT_SOURCE_ROOT={repo}",
			f"-DVEROSCRIPT_LOCALIZATION_OUTPUT={output}",
			f"-DVEROSCRIPT_LOCALIZATION_PROFILE={profile}", "-P", str(SCRIPT)],
			capture_output=True, text=True, check=check)

	@unittest.skipUnless(shutil.which("cmake"), "CMake is required for deployment tests")
	def test_every_deployment_profile_contains_exact_owned_catalog_bytes(self):
		owners = {
			"core": ("VeroScript",),
			"cpp": ("VeroScript", "Extensions/Language/Cpp/VeroScriptCpp"),
			"interop": ("VeroScript", "Extensions/Language/Cpp/VeroScriptCpp", "VeroScriptInterop"),
			"tooling": ("VeroScript", "VeroScriptTooling"),
			"runtime": ("VeroScript", "Extensions/Language/Cpp/VeroScriptCpp", "VeroScriptInterop", "VeroScriptTooling"),
		}
		python_tools = ("Extensions/Language/Python", *(f"Extensions/Language/Python/VeroScriptPython{tool}"
						for tool in ("Frontend", "BindGen", "Transpiler", "Aot")))
		owners["python"] = (*owners["interop"], *python_tools)
		owners["python-tools"] = python_tools
		owners["mobile"] = (*owners["interop"], "Extensions/Platform/Mobile/VeroScriptMobile")
		owners["json"] = ("Modules/Network/VeroScriptJson",)
		owners["network"] = ("Modules/Network/VeroScriptNetwork",)
		owners["http"] = (*owners["network"], "Modules/Network/VeroScriptHttp")
		owners["websocket"] = (*owners["network"], "Modules/Network/VeroScriptWebSocket")
		owners["network-all"] = (*owners["http"], "Modules/Network/VeroScriptWebSocket")
		with tempfile.TemporaryDirectory(prefix="veroscript profiles ") as temporary:
			# Reuse the destination across profiles to also catch leaked catalogs.
			output = Path(temporary) / "profile output/localization"
			for profile, modules in owners.items():
				with self.subTest(profile=profile):
					expected = {}
					for module in modules:
						root = MODULE / module / "Resources/Localization"
						for path in root.rglob("*.json"):
							route = path.relative_to(root).as_posix()
							self.assertNotIn(route, expected)
							expected[route] = path.read_bytes()
					self.stage(output, profile)
					actual = {path.relative_to(output).as_posix(): path.read_bytes()
							  for path in output.rglob("*.json")}
					self.assertEqual(expected, actual)
					self.assertEqual(LOCALES, {path.name for path in output.iterdir() if path.is_dir()})

	@unittest.skipUnless(shutil.which("cmake"), "CMake is required for deployment tests")
	def test_profiles_and_incremental_updates_remove_unrelated_and_stale_catalogs(self):
		with tempfile.TemporaryDirectory(prefix="veroscript localization ") as temporary:
			output = Path(temporary) / "new parent" / "nested" / "localization"
			result = self.stage(output, "python")
			self.assertEqual("", result.stderr)
			self.assertTrue((output / "en-US/python-aot.json").is_file())
			self.assertTrue((output / "en-US/interop.json").is_file())
			self.assertFalse((output / "en-US/dap.json").exists())
			unchanged = output / "en-US/language.json"
			timestamp = unchanged.stat().st_mtime_ns
			(output / "es-ES").mkdir()
			(output / "es-ES/obsolete.json").write_text('{}')
			self.stage(output, "runtime")
			self.assertEqual(timestamp, unchanged.stat().st_mtime_ns)
			self.assertFalse((output / "es-ES").exists())
			self.assertFalse((output / "en-US/python-aot.json").exists())
			self.assertFalse((output / "en-US/python-tools.json").exists())
			self.assertTrue((output / "en-US/dap.json").is_file())
			self.stage(output, "cpp")
			self.assertTrue((output / "en-US/embedding.json").is_file())
			self.assertFalse((output / "en-US/interop.json").exists())
			self.assertFalse((output / "en-US/tooling.json").exists())
			self.stage(output, "python-tools")
			self.assertFalse((output / "en-US/language.json").exists())
			self.assertTrue((output / "en-US/python-bindgen.json").is_file())

	@unittest.skipUnless(shutil.which("cmake"), "CMake is required for deployment tests")
	def test_conflicting_sources_and_source_overlap_preserve_existing_files(self):
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary)
			core = repo / "Script/VeroScript/Resources/Localization"
			cpp = repo / "Script/Extensions/Language/Cpp/VeroScriptCpp/Resources/Localization"
			for root in (core, cpp):
				(root / "en-US").mkdir(parents=True)
				(root / "en-US/conflict.json").write_text('{"diag.test":"preserve"}')
			output = repo / "output/localization"
			output.mkdir(parents=True)
			sentinel = output / "previous.txt"
			sentinel.write_text("previous package")
			result = self.stage(output, "cpp", repo, check=False)
			self.assertNotEqual(0, result.returncode)
			self.assertIn("Conflicting localization file", result.stderr)
			self.assertEqual("previous package", sentinel.read_text())
			result = self.stage(core, "core", repo, check=False)
			self.assertNotEqual(0, result.returncode)
			self.assertIn("overlaps sources", result.stderr)
			self.assertEqual('{"diag.test":"preserve"}', (core / "en-US/conflict.json").read_text())
			result = self.stage(cpp, "core", repo, check=False)
			self.assertNotEqual(0, result.returncode)
			self.assertIn("overlaps sources", result.stderr)
			self.assertEqual('{"diag.test":"preserve"}', (cpp / "en-US/conflict.json").read_text())
			if os.name == "nt":
				result = self.stage(Path(str(core).lower()), "cpp", repo, check=False)
				self.assertNotEqual(0, result.returncode)
				self.assertIn("overlaps sources", result.stderr)
				self.assertEqual('{"diag.test":"preserve"}', (core / "en-US/conflict.json").read_text())
			linked_parent = repo / "linked parent"
			try:
				linked_parent.symlink_to(core, target_is_directory=True)
			except OSError:
				pass  # Windows may require privileges to create this fixture.
			else:
				result = self.stage(linked_parent / "new" / "localization", "core", repo, check=False)
				self.assertNotEqual(0, result.returncode)
				self.assertIn("overlaps sources", result.stderr)
				self.assertFalse((core / "new").exists())


if __name__ == "__main__":
	unittest.main()
