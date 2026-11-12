# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Check standalone Unix Runtime replacement and rollback using real archives."""
import contextlib
import importlib.util
import io
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
from test_veroscript_release import elf
from veroscript_business_release import ALL_BUSINESSES
from veroscript_release import archive_entries
from veroscript_icons import runtime_icon_files
from test_veroscript_icons import populate_icon_sources

spec = importlib.util.spec_from_file_location("veroscript_runtime_package", SCRIPTS / "package-veroscript-runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
spec = importlib.util.spec_from_file_location('veroscript_full_package', SCRIPTS / 'package-veroscript-all.py')
full_package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(full_package)


class StandaloneRuntimeTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix="vero runtime package ")
		self.addCleanup(temporary.cleanup)
		self.repo = Path(temporary.name).resolve()
		self.bin = self.repo / "build/bin"
		self.bin.mkdir(parents=True)
		for tool in ("veroscript", "veroscript-lsp", "veroscript-dap"):
			(self.bin / tool).write_bytes(elf() + b"original")
		(self.bin / 'Vm').mkdir()
		(self.bin / 'Vm/libVeroScriptVmBytecode.so').write_bytes(elf() + b'vm')
		self.interop = self.bin / "libVeroScriptInterop.so"
		self.interop.write_bytes(elf())
		files = {
			"VeroScript/Public/VeroScriptVersion.h": "// Module marker\n",
			"Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/VERSION.txt": "fixture version\n",
			"Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md": "fixture backend license\n",
			"VeroScript/Resources/Scripts/stdlib/core.vs": "// core\n",
			"VeroScriptInterop/Public/VeroScriptInterop.h": "// header\n",
			"LICENSE": "fixture license\n",
			"Resources/Tools/Packaging/ReleaseVersions.json": json.dumps({
				"schemaVersion": 1, "nativeVersion": "0.1.1",
				"businesses": {name: "0.1.1" for name in ALL_BUSINESSES}}),
		}
		for relative, content in files.items():
			path = self.repo / relative
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text(content, encoding="utf-8")
		populate_icon_sources(self.repo)
		self.output = self.repo / ".tmp/runtime-output"
		self.name = "VeroScriptRuntime-0.1.1-linux-x64"

	def package(self, backend="asmjit", *, publish=False):
		def localization(repo, destination):
			destination.mkdir(parents=True)
			(destination / "fixture.json").write_text("{}", encoding="utf-8")

		arguments = ["package-veroscript-runtime.py", "--repo-root", str(self.repo),
					 "--bin-directory", str(self.bin), "--interop-library", str(self.interop),
					 "--version", "0.1.1", "--no-examples"]
		if not publish:
			arguments.extend(["--out-root", str(self.output)])
		output = io.StringIO()
		with patch.object(sys, "argv", arguments), \
				patch.object(runtime, "host_target", return_value="linux-x64"), \
				patch.object(runtime.subprocess, "check_output", return_value=f"VeroScript 0.1.1 [linux-x64; jit-backend={backend}; jit-target={'linux-x86_64' if backend == 'asmjit' else 'none'}; native={'on' if backend == 'asmjit' else 'off'}]"), \
				patch.object(runtime, "stage_localization", side_effect=localization), \
				patch.object(runtime, "stage_package_client"), \
				contextlib.redirect_stdout(output):
			runtime.main()
		return output.getvalue()

	def inventory(self):
		return {p.relative_to(self.output): p.read_bytes()
				for p in self.output.rglob("*") if p.is_file()}

	def test_default_publication_reports_the_existing_business_archive(self):
		def publish(repo, stage, root, business, version, target):
			self.assertEqual(repo, self.repo)
			shutil.copytree(stage, root / business / target / version)

		with patch.object(runtime, "publish_component", side_effect=publish):
			output = self.package(publish=True)
		archive = self.repo / "Release/Runtime/linux-x64/0.1.1" / (self.name + ".tar.gz")
		self.assertTrue(archive.is_file())
		self.assertIn(f"Runtime archive: {archive}", output)
		self.assertFalse((self.repo / "Release/linux-x64/Runtime").exists())

	def test_replacement_publishes_current_archive_and_removes_backup(self):
		self.package()
		updated = elf() + b"updated"
		(self.bin / "veroscript").write_bytes(updated)
		self.package()
		self.assertEqual((self.output / self.name / "veroscript").read_bytes(), updated)
		entries = archive_entries(self.output / (self.name + ".tar.gz"))
		self.assertEqual(entries[self.name + "/veroscript"][0], updated)
		self.assertEqual(list((self.repo / ".tmp").glob("veroscript-runtime-backup-*")), [])
		self.assertEqual(list((self.repo / ".tmp").glob("veroscript-runtime-stage-*")), [])

	def test_runtime_without_application_product_keeps_required_vm(self):
		self.assertFalse((self.bin / 'veroscript-runtime').exists())
		self.package(backend='none')
		entries = archive_entries(self.output / (self.name + '.tar.gz'))
		self.assertEqual(entries[self.name + '/Vm/libVeroScriptVmBytecode.so'][0], elf() + b'vm')
		for relative, source in runtime_icon_files(self.repo, 'linux-x64').items():
			self.assertEqual(entries[self.name + '/' + relative][0], source.read_bytes())

	def test_missing_vm_preserves_previously_published_runtime(self):
		self.package(backend='none')
		before = self.inventory()
		shutil.rmtree(self.bin / 'Vm')
		with self.assertRaisesRegex(ValueError, 'explicit Vm component'):
			self.package(backend='none')
		self.assertEqual(self.inventory(), before)

	def test_full_runtime_inventory_hashes_final_postprocessed_vm(self):
		self.check_full_runtime_inventory(True)

	def test_full_runtime_does_not_copy_stale_application_tools_when_disabled(self):
		self.check_full_runtime_inventory(False)

	def check_full_runtime_inventory(self, application):
		for name in ('miniz/LICENSE', 'json/LICENSE.MIT', 'sha256/LICENSE'):
			notice = self.repo / 'VeroScriptApplication/ThirdParty' / name
			notice.parent.mkdir(parents=True, exist_ok=True)
			notice.write_text('fixture license')
		(self.bin / 'veroscript-runtime').write_bytes(elf() + b'application')
		release = full_package.Release.__new__(full_package.Release)
		release.args = SimpleNamespace(release_versions={'nativeVersion': '0.1.1'})
		release.repo, release.bin = self.repo, self.bin
		release.application = application
		release.build, release.work = self.repo / 'build', self.repo / '.tmp/full-work'
		release.work.mkdir(parents=True)
		release.platform, release.arch, release.target = 'linux', 'x64', 'linux-x64'
		release.interop, release.shared = self.interop, self.interop.name
		release.settings, release.build_environment = {}, {}
		release.versions, release.package_client = {'Runtime': '0.1.1'}, False
		original, processed = elf() + b'original debug sections', elf() + b'processed'
		(self.bin / 'Vm/libVeroScriptVmBytecode.so').write_bytes(original)

		def install_vm(build, destination):
			(destination / 'Vm').mkdir(exist_ok=True)
			(destination / 'Vm/libVeroScriptVmBytecode.so').write_bytes(original)

		def prepare(root, symbols):
			(root / 'Vm/libVeroScriptVmBytecode.so').write_bytes(processed)

		class FinishedRuntime(Exception):
			pass

		def archive(category, name, source):
			metadata = json.loads((source / 'runtime-manifest.json').read_text())
			self.assertEqual(metadata['application'], application)
			if not application:
				self.assertFalse((source / 'veroscript-runtime').exists())
				self.assertFalse((source / 'veroscript-runtime.json').exists())
				self.assertFalse((source / 'licenses/application').exists())
				self.assertEqual((source / 'Vm/libVeroScriptVmBytecode.so').read_bytes(), processed)
				raise FinishedRuntime()
			descriptor = json.loads((source / 'veroscript-runtime.json').read_text())
			vm = next(item for item in descriptor['files'] if item['path'] == 'Vm/libVeroScriptVmBytecode.so')
			payload = (source / vm['path']).read_bytes()
			self.assertEqual(payload, processed)
			self.assertEqual(vm['sha256'], hashlib.sha256(payload).hexdigest())
			self.assertEqual(vm['bytes'], str(len(payload)))
			raise FinishedRuntime()

		release.prepare_native_tree, release.archive = prepare, archive
		identity = 'VeroScript 0.1.1 [linux-x64; jit-backend=none; jit-target=none; native=off]'
		target = {'core': '0.1.1', 'coreBuild': 'fixture', 'abis': {'native-extension': 'fixture'}}
		with patch.object(full_package, 'check_collection'), patch.object(full_package, 'stage_localization'), \
				patch.object(full_package, 'run', return_value=identity), \
				patch.object(full_package, 'stage_vm_runtime', side_effect=install_vm), \
				patch.object(full_package, 'stage_package_client'), \
				patch('veroscript_applications.subprocess.check_output', return_value=json.dumps(target)), \
				self.assertRaises(FinishedRuntime):
			release.assemble()

	def test_backend_free_runtime_needs_no_optional_sources(self):
		shutil.rmtree(self.repo / "Modules/Execution")
		self.package(backend="none")
		package = self.output / self.name
		manifest = json.loads((package / "runtime-manifest.json").read_text())
		self.assertEqual(manifest["jit"], {"backend": "none", "target": "none", "nativeCode": False})
		self.assertFalse((package / "licenses/asmjit").exists())

	def test_failed_replacement_restores_previous_output(self):
		self.package()
		before = self.inventory()
		rename = Path.rename

		def fail_stage(path, destination):
			if path.name == "output" and path.parent.name.startswith("veroscript-runtime-stage-"):
				raise OSError("injected replacement failure")
			return rename(path, destination)

		with patch.object(Path, "rename", fail_stage), self.assertRaisesRegex(OSError, "replacement failure"):
			self.package()
		self.assertEqual(self.inventory(), before)
		self.assertEqual(list((self.repo / ".tmp").glob("veroscript-runtime-backup-*")), [])

	def test_archive_failure_keeps_previous_output(self):
		self.package()
		before = self.inventory()
		with patch.object(runtime, "archive_tree", side_effect=OSError("injected archive failure")), \
				self.assertRaisesRegex(OSError, "archive failure"):
			self.package()
		self.assertEqual(self.inventory(), before)

	def test_cleanup_failure_keeps_new_package_and_reports_publication_state(self):
		self.package()
		updated = elf() + b"updated"
		(self.bin / "veroscript").write_bytes(updated)
		remove = shutil.rmtree

		def fail_backup(path, *arguments, **keywords):
			if Path(path).name.startswith("veroscript-runtime-backup-"):
				raise OSError("injected cleanup failure")
			return remove(path, *arguments, **keywords)

		with patch.object(runtime.shutil, "rmtree", fail_backup), \
				self.assertRaisesRegex(RuntimeError, "package published.*cleanup failed"):
			self.package()
		self.assertEqual((self.output / self.name / "veroscript").read_bytes(), updated)
		backups = list((self.repo / ".tmp").glob("veroscript-runtime-backup-*"))
		self.assertEqual(len(backups), 1)
		self.assertEqual((backups[0] / self.name / "veroscript").read_bytes(), elf() + b"original")


if __name__ == "__main__":
	unittest.main()
