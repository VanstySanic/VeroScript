# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Check that standalone backend packages include their target's runtime dependencies."""
from pathlib import Path
import os
import json
import struct
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import veroscript_jit as jit
from veroscript_release import WINDOWS_RUNTIME_DLLS, archive_entries, core_public_abi
from veroscript_business_release import artifact_business


def pe(architecture):
	data = bytearray(128)
	data[:2] = b"MZ"
	struct.pack_into("<I", data, 60, 64)
	data[64:68] = b"PE\0\0"
	struct.pack_into("<H", data, 68, {"x64": 0x8664, "x86": 0x14C, "arm64": 0xAA64}[architecture])
	return bytes(data)


class StandaloneJitPackageTests(unittest.TestCase):
	def setUp(self):
		temporary = tempfile.TemporaryDirectory(prefix="vero jit package ")
		self.addCleanup(temporary.cleanup)
		self.repo = Path(temporary.name).resolve()
		self.build = self.repo / "build"
		self.build.mkdir()
		self.headers = self.repo / "VeroScript/Public"
		self.headers.mkdir(parents=True)
		(self.headers / "VeroScriptVersion.h").write_bytes(
			b"inline constexpr FSemanticVersion VEROSCRIPT_VERSION = {0, 1, 1};\n")
		(self.headers / "VeroScript.h").write_bytes(b"#pragma once\n")
		self.abi = core_public_abi(self.headers).encode("ascii")
		self.generated = self.build / "VeroScript/Generated/VeroScriptJitBuild.h"
		self.generated.parent.mkdir(parents=True)
		self.generated.write_bytes(b'#define VEROSCRIPT_JIT_PUBLIC_ABI "' + self.abi + b'"\n')
		(self.build / "CMakeCache.txt").write_text(
			f"CMAKE_HOME_DIRECTORY:INTERNAL={self.repo.as_posix()}\n"
			"CMAKE_COMMAND:INTERNAL=cmake\n"
			"VEROSCRIPT_ENABLE_JIT_TEST_HOOKS:BOOL=OFF\n"
			"VS_VEROSCRIPT_ASMJIT_VERSION:STRING=0.1.1\n", encoding="utf-8")

	def payload(self, architecture):
		destination = self.repo / architecture
		destination.mkdir()
		dependencies = WINDOWS_RUNTIME_DLLS if architecture == "x64" else WINDOWS_RUNTIME_DLLS[:2]
		for name in ("VeroScriptJitAsmJit.dll", *dependencies):
			(destination / name).write_bytes(pe(architecture))
		notice = destination / "licenses/MicrosoftVisualCpp/NOTICE.txt"
		notice.parent.mkdir(parents=True)
		notice.write_text("Microsoft runtime notice", encoding="utf-8")
		return destination, dependencies, notice

	def stage(self, destination, architecture):
		# The install output is supplied by the fixture. Test cross-target auditing
		# without executing these deliberately minimal PE files on the test host.
		with patch.object(jit.subprocess, "run"), patch.object(jit, "host_target", return_value="linux-x64"):
			jit.stage_backend(self.repo, self.build, destination, "win-" + architecture, "0.1.1")

	def test_complete_runtime_payload_is_accepted_for_each_windows_architecture(self):
		for architecture in ("x64", "x86", "arm64"):
			with self.subTest(architecture=architecture):
				destination, _, _ = self.payload(architecture)
				self.stage(destination, architecture)

	def test_missing_runtime_dependency_or_notice_is_rejected(self):
		for architecture in ("x64", "x86", "arm64"):
			destination, dependencies, notice = self.payload(architecture)
			for path in (*(destination / name for name in dependencies), notice):
				with self.subTest(architecture=architecture, file=path.name):
					original = path.read_bytes()
					path.unlink()
					try:
						with self.assertRaisesRegex(ValueError, "runtime dependency|runtime notice"):
							self.stage(destination, architecture)
					finally:
						path.write_bytes(original)

	def test_wrong_runtime_architecture_and_empty_notice_are_rejected(self):
		for architecture in ("x64", "x86", "arm64"):
			destination, dependencies, notice = self.payload(architecture)
			for name in dependencies:
				path = destination / name
				with self.subTest(architecture=architecture, file=name):
					path.write_bytes(pe("x86" if architecture != "x86" else "x64"))
					try:
						with self.assertRaisesRegex(ValueError, "runtime dependency"):
							self.stage(destination, architecture)
					finally:
						path.write_bytes(pe(architecture))
			notice.write_bytes(b"")
			with self.subTest(architecture=architecture, file="NOTICE.txt"):
				with self.assertRaisesRegex(ValueError, "runtime notice"):
					self.stage(destination, architecture)

	def test_stale_missing_or_duplicate_generated_contract_rejected_before_install(self):
		destination, _, _ = self.payload("arm64")
		original = self.generated.read_bytes()
		for data in (None, original.replace(self.abi, b"0" * 64), original * 2):
			with self.subTest(data=data), patch.object(jit.subprocess, "run") as install:
				if data is None:
					self.generated.unlink()
				else:
					self.generated.write_bytes(data)
				try:
					with self.assertRaisesRegex(ValueError, "JIT public ABI build header"):
						self.stage(destination, "arm64")
					install.assert_not_called()
				finally:
					self.generated.write_bytes(original)
		(self.headers / "VeroScript.h").write_bytes(b"// New public contract\n")
		with patch.object(jit.subprocess, "run") as install:
			with self.assertRaisesRegex(ValueError, "JIT public ABI build header"):
				self.stage(destination, "arm64")
			install.assert_not_called()

	def host_stage(self, destination, abi=None, expected_core_version=0x00010001, query_result=1, **overrides):
		cli = self.build / "bin/Release/veroscript.exe"
		cli.parent.mkdir(parents=True, exist_ok=True)
		cli.write_bytes(pe("x64"))
		def query(core_version, size, pointer):
			self.assertEqual(expected_core_version, core_version)
			descriptor = pointer._obj
			descriptor.size = size
			descriptor.core_version = expected_core_version
			descriptor.id = b"asmjit"
			descriptor.version = b"0.1.1"
			descriptor.abi = self.abi + b":test-compiler" if abi is None else abi
			descriptor.create = 1
			descriptor.destroy = 2
			for key, value in overrides.items():
				setattr(descriptor, key, value)
			return query_result
		backend = Mock(_handle=123, veroscript_jit_query=query)
		close = "FreeLibrary" if os.name == "nt" else "dlclose"
		with patch.object(jit.subprocess, "run"), patch.object(jit, "host_target", return_value="win-x64"), \
				patch.object(jit.ctypes, "CDLL", return_value=backend), \
				patch.object(jit._ctypes, close) as unload, \
				patch.object(jit.subprocess, "check_output", return_value=
							 "VeroScript 0.1.1 [win-x64; jit-backend=asmjit; jit-target=windows-x86_64; native=on]") as cli_check:
			try:
				jit.stage_backend(self.repo, self.build, destination, "win-x64", "0.1.1")
			finally:
				unload.assert_called_once_with(123)
			cli_check.assert_called_once()

	def test_same_version_backend_with_stale_binary_abi_rejected(self):
		destination, _, _ = self.payload("x64")
		for abi in (b"0" * 64 + b":test-compiler", self.abi, b""):
			with self.subTest(abi=abi), self.assertRaisesRegex(ValueError, "binary public ABI"):
				self.host_stage(destination, abi=abi)

	def test_invalid_binary_handshake_rejected(self):
		destination, _, _ = self.payload("x64")
		for fields in ({"size": 1}, {"core_version": 1}, {"core_version": 2},
					   {"create": None}, {"destroy": None}):
			with self.subTest(fields=fields), self.assertRaisesRegex(ValueError, "binary metadata"):
				self.host_stage(destination, **fields)

	def test_current_host_binary_contract_is_accepted(self):
		destination, _, _ = self.payload("x64")
		self.host_stage(destination)

	def test_query_failure_is_rejected_even_with_valid_descriptor(self):
		destination, _, _ = self.payload("x64")
		with self.assertRaisesRegex(ValueError, "binary metadata"):
			self.host_stage(destination, query_result=0)

	def test_core_version_is_read_from_source_independently_of_backend_version(self):
		(self.headers / "VeroScriptVersion.h").write_text(
			"inline constexpr FSemanticVersion VEROSCRIPT_VERSION = {2, 3, 4096};\n", encoding="utf-8")
		self.abi = core_public_abi(self.headers).encode("ascii")
		self.generated.write_bytes(b'#define VEROSCRIPT_JIT_PUBLIC_ABI "' + self.abi + b'"\n')
		destination, _, _ = self.payload("x64")
		self.host_stage(destination, expected_core_version=0x02031000)

	def test_missing_ambiguous_or_unencodable_core_version_is_rejected_before_install(self):
		declaration = "inline constexpr FSemanticVersion VEROSCRIPT_VERSION = {0, 1, 1};\n"
		destination, _, _ = self.payload("arm64")
		for text in ("// Missing version\n", declaration * 2,
					 declaration.replace("{0, 1, 1}", "{256, 1, 1}"),
					 declaration.replace("{0, 1, 1}", "{0, 256, 1}"),
					 declaration.replace("{0, 1, 1}", "{0, 1, 65536}")):
			with self.subTest(text=text), patch.object(jit.subprocess, "run") as install:
				(self.headers / "VeroScriptVersion.h").write_text(text, encoding="utf-8")
				self.abi = core_public_abi(self.headers).encode("ascii")
				self.generated.write_bytes(b'#define VEROSCRIPT_JIT_PUBLIC_ABI "' + self.abi + b'"\n')
				with self.assertRaisesRegex(ValueError, "Core product version"):
					self.stage(destination, "arm64")
				install.assert_not_called()

	def test_packaging_entry_uses_official_name_on_each_platform(self):
		for target, suffix in (("win-x64", ".zip"), ("linux-x64", ".tar.gz"),
							   ("macos-arm64", ".tar.gz")):
			with self.subTest(target=target):
				output = self.repo / ".tmp" / target
				name = "vs-asmjit-0.1.1-" + target
				events = []
				def stage(repo, build, destination, actual_target, package_version):
					self.assertEqual((actual_target, package_version), (target, "0.1.1"))
					events.append('stage')
					destination.mkdir()
					(destination / "LICENSE").write_bytes(b"license")
				arguments = ["package-veroscript-jit.py", "--repo-root", str(self.repo),
							 "--build-directory", str(self.build), "--target", target,
							 "--version", "0.1.1", "--out-root", str(output)]
				tool = self.build / 'bin/VeroScriptModulePackageTool'
				tool.parent.mkdir(exist_ok=True)
				tool.touch()
				def pack(repo, source, archive, actual_tool, actual_target, package_version):
					self.assertEqual((actual_tool, actual_target, package_version), (tool, target, '0.1.1'))
					self.assertEqual(events, ['stage'])
					self.assertTrue((source.parent / (name + suffix)).is_file())
					archive.write_bytes(b'installable fixture')
					events.append('pack')
				def symbols(repo, builds, destination, versions, actual_target, selected):
					self.assertEqual((repo, builds, versions, actual_target, selected),
						(self.repo, {'Jit': self.build}, {'Jit': '0.1.1'}, target, ['Jit']))
					self.assertEqual(events, ['stage', 'pack'])
					self.assertEqual({name + '/LICENSE'}, set(archive_entries(destination / (name + suffix))))
					self.assertEqual((destination / (name + '.vspkg')).read_bytes(), b'installable fixture')
					events.append('symbols')
				with patch.object(sys, "argv", arguments), patch.object(jit, "stage_backend", stage), \
						patch.object(jit, 'pack_backend', pack), \
						patch('veroscript_module_symbols.assemble_module_symbols', side_effect=symbols) as collect_symbols:
					jit.main()
					collect_symbols.assert_called_once()
				self.assertEqual(events, ['stage', 'pack', 'symbols'])
				self.assertEqual({name, name + suffix, name + '.vspkg'}, {path.name for path in output.iterdir()})
				self.assertEqual("Jit", artifact_business("jit/" + name + suffix))
				self.assertEqual({name + "/LICENSE"}, set(archive_entries(output / (name + suffix))))

	def test_portable_only_does_not_preserve_stale_installable_output(self):
		name = 'vs-asmjit-0.1.1-win-x64'
		def stage(repo, build, destination, target, package_version):
			events.append('stage')
			destination.mkdir()
			(destination / 'LICENSE').write_bytes(b'license')
		for stale in (False, True):
			with self.subTest(stale=stale):
				events = []
				output = self.repo / '.tmp' / ('stale' if stale else 'clean')
				output.mkdir(parents=True)
				installable = output / (name + '.vspkg')
				if stale:
					installable.write_bytes(b'old backend contract')
				arguments = ['package-veroscript-jit.py', '--repo-root', str(self.repo),
							 '--build-directory', str(self.build), '--target', 'win-x64',
							 '--version', '0.1.1', '--out-root', str(output), '--portable-only']
				def symbols(repo, builds, destination, versions, actual_target, selected):
					self.assertEqual((repo, builds, versions, actual_target, selected),
						(self.repo, {'Jit': self.build}, {'Jit': '0.1.1'}, 'win-x64', ['Jit']))
					self.assertEqual(events, ['stage'])
					self.assertEqual({name + '/LICENSE'}, set(archive_entries(destination / (name + '.zip'))))
					self.assertFalse((destination / (name + '.vspkg')).exists())
					events.append('symbols')
				with patch.object(sys, 'argv', arguments), patch.object(jit, 'stage_backend', stage), \
						patch.object(jit, 'pack_backend') as pack, \
						patch('veroscript_module_symbols.assemble_module_symbols', side_effect=symbols) as collect_symbols:
					if stale:
						with self.assertRaisesRegex(ValueError, 'output contains unrelated files'):
							jit.main()
						self.assertEqual(installable.read_bytes(), b'old backend contract')
						self.assertEqual({installable.name}, {path.name for path in output.iterdir()})
					else:
						jit.main()
						self.assertEqual({name, name + '.zip'}, {path.name for path in output.iterdir()})
					pack.assert_not_called()
					collect_symbols.assert_called_once()
				self.assertEqual(events, ['stage', 'symbols'])

	def test_installable_backend_uses_real_target_contract_without_mutating_portable_tree(self):
		source, _, _ = self.payload('x64')
		(source / 'LICENSE').write_bytes(b'license')
		notice = source / 'licenses/asmjit/LICENSE.md'
		notice.parent.mkdir()
		notice.write_bytes(b'AsmJit license')
		(notice.parent / 'VERSION.txt').write_bytes(b'0.1.1')
		context = {'os': 'windows', 'arch': 'x64', 'environment': 'desktop', 'core': '0.1.1',
				   'coreBuild': 'b' * 64, 'abis': {'jit': 'matched-jit-abi'}}
		archive = self.repo / 'result.vspkg'
		def pack(command, *, check):
			import zipfile
			stage = Path(command[3])
			with zipfile.ZipFile(archive, 'w') as writer:
				for path in stage.rglob('*'):
					if path.is_file():
						writer.write(path, path.relative_to(stage).as_posix())
		with patch.object(jit.subprocess, 'check_output', return_value=json.dumps({'id': 'asmjit', 'target': context})), \
				patch.object(jit.subprocess, 'run', side_effect=pack):
			jit.pack_backend(self.repo, source, archive, self.repo / 'tool', 'win-x64', '0.1.1')
		self.assertFalse((source / 'veroscript-package.json').exists())
		entries = archive_entries(archive)
		jit.verify_installable_backend(entries, '0.1.1', 'win-x64')
		manifest = json.loads(entries['veroscript-package.json'][0])
		self.assertEqual(manifest['target']['abis'], {'jit': 'matched-jit-abi'})
		self.assertEqual(manifest['entries']['backends'][0]['service'], 'jit')
		changed = dict(entries)
		changed.pop('licenses/asmjit/LICENSE.md')
		with self.assertRaisesRegex(ValueError, 'third-party notice'):
			jit.verify_installable_backend(changed, '0.1.1', 'win-x64')


if __name__ == "__main__":
	unittest.main()
