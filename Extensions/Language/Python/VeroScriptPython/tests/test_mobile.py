# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Host tests for mobile SDK contracts and release gates; no target SDK needed."""
from __future__ import annotations

import errno
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[5]
TOOLS = ROOT / "Extensions/Language/Python/Resources/Tools/Mobile"
sys.path.insert(0, str(TOOLS))
from mobile_sdk import inspect_sdk, ios_binary, product_version, read_json, sdk_path, sysconfig_values, verify_android_elf
from package_mobile import build_commands, install_ios_markers, package, select_matrix
from verify_mobile import audit_slice, audit_xcframework, macho_members, python_resources, verify_package
from device_smoke import check_compiler_nesting, device_identity, exercise_runtime, require, run


def elf(architecture: str = "arm64-v8a", *, build_id: str = "a" * 64, dependency: str = "libpython3.14.so") -> bytes:
	strings = b"\0PyInit__veroscript\0" + dependency.encode() + b"\0libc++_shared.so\0"
	python_offset = len(b"\0PyInit__veroscript\0")
	cpp_offset = python_offset + len(dependency) + 1
	data = bytearray(200)
	string_offset = len(data)
	data.extend(strings)
	data.extend(b"\0" * (-len(data) % 8))
	symbols_offset = len(data)
	data.extend(bytes(24) + struct.pack("<IBBHQQ", 1, 0x12, 0, 1, 1, 1))
	dynamic_offset = len(data)
	data.extend(struct.pack("<qQqQqQ", 1, python_offset, 1, cpp_offset, 0, 0))
	sections_offset = len(data)
	data.extend(bytes(64))
	data.extend(struct.pack("<IIQQQQIIQQ", 0, 3, 0, 0, string_offset, len(strings), 0, 0, 1, 0))
	data.extend(struct.pack("<IIQQQQIIQQ", 0, 11, 0, 0, symbols_offset, 48, 1, 0, 8, 24))
	data.extend(struct.pack("<IIQQQQIIQQ", 0, 6, 0, 0, dynamic_offset, 48, 1, 0, 8, 16))
	data.extend(build_id.encode())
	machine = 183 if architecture == "arm64-v8a" else 62
	ident = b"\x7fELF\x02\x01\x01" + bytes(9)
	data[:64] = struct.pack("<16sHHIQQQIHHHHHH", ident, 3, machine, 1, 0, 64, sections_offset, 0, 64, 56, 2, 64, 4, 0)
	data[64:120] = struct.pack("<IIQQQQQQ", 1, 5, 0, 0, 0, len(data), len(data), 16384)
	data[120:176] = struct.pack("<IIQQQQQQ", 4, 4, 176, 176, 176, 24, 24, 4)
	data[176:200] = struct.pack("<III", 8, 4, 1) + b"Android\0" + struct.pack("<I", 26)
	return bytes(data)


def macho(architecture: str, simulator: bool, *, python: bool = False, build_id: str = "a" * 64) -> bytes:
	cpu = 0x100000C if architecture == "arm64" else 0x1000007

	def dylib(command: int, name: str) -> bytes:
		raw = name.encode() + b"\0"
		length = (24 + len(raw) + 7) & ~7
		return struct.pack("<IIIIII", command, length, 24, 0, 0, 0) + raw + bytes(length - 24 - len(raw))

	name = "Python" if python else "veroscript._veroscript"
	commands = struct.pack("<IIIIII", 0x32, 24, 7 if simulator else 2, 15 << 16, 18 << 16, 0)
	commands += dylib(0xD, f"@rpath/{name}.framework/{name}")
	commands += dylib(0xC, "@rpath/Python.framework/Python")
	symbol_offset = 32 + len(commands) + 24
	strings = b"\0_PyInit__veroscript\0"
	commands += struct.pack("<IIIIII", 2, 24, symbol_offset, 1, symbol_offset + 16, len(strings))
	header = struct.pack("<IIIIIIII", 0xFEEDFACF, cpu, 0, 6, 4, len(commands), 0, 0)
	return header + commands + struct.pack("<IBBHQ", 1, 0xF, 1, 0, 1) + strings + build_id.encode()


def record(platform: str, architecture: str, variant: str = "device") -> dict:
	target = f"{platform}-{variant}-{architecture}-cp314"
	return {"target": target, "platform": platform, "architecture": architecture, "variant": variant,
			"minimumVersion": "26" if platform == "android" else "15.0", "pythonVersion": "3.14.7",
			"productVersion": product_version(),
			"extensionSuffix": ".cpython-314-android.so" if platform == "android" else ".cpython-314-iphoneos.so",
			"buildId": hashlib.sha256(target.encode()).hexdigest(), "sdkFingerprint": "b" * 64}


def slice_files(stage: Path, metadata: dict) -> None:
	package = stage / "python/veroscript"
	for name, contents in python_resources(application=metadata["platform"] == "android").items():
		path = package / name
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(contents)
	if metadata["platform"] == "android":
		libraries = stage / "jniLibs" / metadata["architecture"]
		libraries.mkdir(parents=True)
		(libraries / "libveroscript_python.so").write_bytes(elf(metadata["architecture"], build_id=metadata["buildId"]))
		(libraries / "libc++_shared.so").write_bytes(elf(metadata["architecture"]))
	else:
		framework = stage / "Frameworks/veroscript._veroscript.framework"
		framework.mkdir(parents=True)
		(framework / "veroscript._veroscript").write_bytes(macho(metadata["architecture"], metadata["variant"] == "simulator", build_id=metadata["buildId"]))
		(framework / "Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "veroscript._veroscript", "CFBundlePackageType": "FMWK", "CFBundleVersion": metadata["productVersion"], "CFBundleShortVersionString": metadata["productVersion"]}))
		install_ios_markers(stage, metadata)


def inventory(root: Path, records: list[dict]) -> None:
	names = ["LICENSE", "THIRD-PARTY-NOTICES.md", "device_smoke.py"]
	if records[0]["platform"] == "android":
		names.append("ANDROID-NDK-NOTICE.txt")
	for name in names:
		(root / name).write_text("fixture", encoding="utf-8")
	files = {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
			 for path in root.rglob("*") if path.is_file() and path.name != "python-mobile.json"}
	(root / "python-mobile.json").write_text(json.dumps({"schemaVersion": 1, "product": "VeroScript Python Mobile",
		  "version": records[0]["productVersion"], "slices": records, "files": files,
		  "includesCPythonRuntime": False, "nativeJit": False}), encoding="utf-8")


def fat_macho(arm: bytes, intel: bytes) -> bytes:
	start, second = 64, (64 + len(arm) + 7) & ~7
	fat = struct.pack(">II", 0xCAFEBABE, 2)
	fat += struct.pack(">IIIII", 0x100000C, 0, start, len(arm), 3)
	fat += struct.pack(">IIIII", 0x1000007, 0, second, len(intel), 3)
	return fat + bytes(start - len(fat)) + arm + bytes(second - start - len(arm)) + intel


def create_xcframework(frameworks: list[Path], output: Path) -> None:
	libraries = []
	for source, identifier, architectures, variant in (
		(frameworks[0], "ios-arm64", ["arm64"], None),
		(frameworks[1], "ios-arm64_x86_64-simulator", ["arm64", "x86_64"], "simulator"),
	):
		shutil.copytree(source, output / identifier / source.name)
		library = {"LibraryIdentifier": identifier, "LibraryPath": source.name,
				   "SupportedPlatform": "ios", "SupportedArchitectures": architectures}
		if variant is not None:
			library["SupportedPlatformVariant"] = variant
		libraries.append(library)
	(output / "Info.plist").write_bytes(plistlib.dumps({"XCFrameworkFormatVersion": "1.0", "AvailableLibraries": libraries}))


class MobileContracts(unittest.TestCase):
	def setUp(self) -> None:
		self.temp = tempfile.TemporaryDirectory()
		self.addCleanup(self.temp.cleanup)
		self.root = Path(self.temp.name)

	def sdk(self, architecture: str = "arm64-v8a") -> Path:
		root = self.root / architecture
		include = root / "include"
		include.mkdir(parents=True)
		(include / "Python.h").write_text("", encoding="utf-8")
		(include / "pyconfig.h").write_text("", encoding="utf-8")
		(include / "patchlevel.h").write_text("#define PY_MAJOR_VERSION 3\n#define PY_MINOR_VERSION 14\n#define PY_MICRO_VERSION 7\n", encoding="utf-8")
		(root / "libpython3.14.so").write_bytes(elf(architecture))
		(root / "_sysconfigdata.py").write_text("build_time_vars = {'VERSION':'3.14', 'EXT_SUFFIX':'.cpython-314-android.so', 'Py_DEBUG':0, 'Py_GIL_DISABLED':0}\n", encoding="utf-8")
		descriptor = root / "sdk.json"
		descriptor.write_text(json.dumps({"schemaVersion": 1, "platform": "android", "architecture": architecture,
			"minimumVersion": "26", "includeDirectory": "include", "library": "libpython3.14.so", "sysconfigData": "_sysconfigdata.py"}), encoding="utf-8")
		return descriptor

	def android_package(self) -> tuple[Path, list[dict]]:
		root = self.root / "package"
		root.mkdir()
		records = [record("android", abi) for abi in ("arm64-v8a", "x86_64")]
		for item in records:
			slice_files(root / "slices" / item["target"], item)
		inventory(root, records)
		return root, records

	def ndk(self) -> Path:
		ndk = self.root / "ndk"
		toolchain = ndk / "build/cmake/android.toolchain.cmake"
		toolchain.parent.mkdir(parents=True)
		toolchain.write_text("fixture", encoding="utf-8")
		(ndk / "NOTICE").write_text("fixture runtime license", encoding="utf-8")
		strip = ndk / ('toolchains/llvm/prebuilt/host/bin/llvm-strip.exe' if os.name == 'nt' else 'toolchains/llvm/prebuilt/host/bin/llvm-strip')
		strip.parent.mkdir(parents=True, exist_ok=True)
		strip.write_bytes(b'fixture strip tool; clean synthetic ELF does not invoke it')
		for abi, triple in (("arm64-v8a", "aarch64-linux-android"), ("x86_64", "x86_64-linux-android")):
			path = ndk / f"toolchains/llvm/prebuilt/host/sysroot/usr/lib/{triple}/libc++_shared.so"
			path.parent.mkdir(parents=True)
			path.write_bytes(elf(abi))
		return ndk

	def tool_outputs(self, sdks: list[dict], *, fail_target: str | None = None,
					 concurrent_output: Path | None = None):
		def execute(command: list[str], **kwargs):
			if command[0] == sys.executable:
				self.assertIn("--check-host", command)
				self.assertEqual(Path(command[1]), TOOLS / "mobile_sdk.py")
			elif command[0] == "cmake":
				if "--build" in command and Path(command[2]).name == "build-" + str(fail_target):
					raise subprocess.CalledProcessError(1, command)
				if "--install" in command:
					stage = Path(command[command.index("--prefix") + 1])
					sdk = next(item for item in sdks if item["target"] == stage.name)
					slice_files(stage, sdk)
					if concurrent_output is not None and not concurrent_output.exists():
						concurrent_output.mkdir()
						(concurrent_output / "keep.txt").write_text("keep", encoding="utf-8")
			elif command[:3] == ["xcrun", "lipo", "-create"]:
				Path(command[-1]).write_bytes(fat_macho(Path(command[3]).read_bytes(), Path(command[4]).read_bytes()))
			elif command[:2] == ["xcodebuild", "-create-xcframework"]:
				frameworks = [Path(command[index + 1]) for index, value in enumerate(command) if value == "-framework"]
				create_xcframework(frameworks, Path(command[-1]))
			else:
				self.fail(f"Unexpected build tool command: {command}")
			return subprocess.CompletedProcess(command, 0)
		return execute

	def test_android_producer_publishes_a_complete_audited_package(self) -> None:
		sdks = select_matrix([self.sdk(), self.sdk("x86_64")], "android")
		ndk = self.ndk()
		output = self.root / "published"
		with patch("package_mobile.subprocess.run", side_effect=self.tool_outputs(sdks)):
			package(sdks, output, sys.executable, "cmake", ndk)
		metadata = verify_package(output)
		self.assertEqual(metadata["deviceValidation"], "pending")
		self.assertEqual((output / "THIRD-PARTY-NOTICES.md").read_bytes(),
						 (ROOT / "Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md").read_bytes())
		self.assertEqual(len(metadata["slices"]), 2)
		self.assertEqual((output / "ANDROID-NDK-NOTICE.txt").read_bytes(), (ndk / "NOTICE").read_bytes())
		self.assertEqual((output / "device_smoke.py").read_bytes(), (TOOLS / "device_smoke.py").read_bytes())
		self.assertFalse(list(self.root.glob(".veroscript-python-mobile-*")))

	def test_ios_producer_merges_simulators_and_publishes_an_audited_xcframework(self) -> None:
		sdks = [record("ios", "arm64"), record("ios", "arm64", "simulator"), record("ios", "x86_64", "simulator")]
		for sdk in sdks:
			sdk.update(buildNonce="d" * 32, descriptor=str(self.root / (sdk["target"] + ".json")))
		output = self.root / "published"
		with patch("package_mobile.platform.system", return_value="Darwin"), \
			 patch("package_mobile.subprocess.run", side_effect=self.tool_outputs(sdks)):
			package(sdks, output, sys.executable, "cmake", None)
		self.assertEqual(len(verify_package(output)["slices"]), 3)
		self.assertEqual((output / "THIRD-PARTY-NOTICES.md").read_bytes(),
						 (ROOT / "Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md").read_bytes())
		simulator = output / "VeroScriptPython.xcframework/ios-arm64_x86_64-simulator/veroscript._veroscript.framework/veroscript._veroscript"
		self.assertEqual(set(macho_members(simulator.read_bytes())), {"arm64", "x86_64"})
		self.assertFalse(list(self.root.glob(".veroscript-python-mobile-*")))

	def test_producer_failures_do_not_publish_partial_output_or_overwrite_new_output(self) -> None:
		sdks = select_matrix([self.sdk(), self.sdk("x86_64")], "android")
		ndk = self.ndk()
		output = self.root / "published"
		with patch("package_mobile.subprocess.run", side_effect=self.tool_outputs(sdks, fail_target=sdks[1]["target"])):
			with self.assertRaises(subprocess.CalledProcessError):
				package(sdks, output, sys.executable, "cmake", ndk)
		self.assertFalse(output.exists())
		self.assertFalse(list(self.root.glob(".veroscript-python-mobile-*")))
		with patch("package_mobile.subprocess.run", side_effect=self.tool_outputs(sdks, concurrent_output=output)):
			with self.assertRaisesRegex(ValueError, "created during the build"):
				package(sdks, output, sys.executable, "cmake", ndk)
		self.assertEqual((output / "keep.txt").read_text(encoding="utf-8"), "keep")
		self.assertEqual({path.name for path in output.iterdir()}, {"keep.txt"})
		self.assertFalse(list(self.root.glob(".veroscript-python-mobile-*")))

	def test_unlisted_payload_cannot_be_authorized_by_regenerating_inventory(self) -> None:
		root, records = self.android_package()
		for name in ("libpython3.14.so", "slices/undeclared-target/libpython3.14.so",
					 f"slices/{records[0]['target']}/jniLibs/unknown-abi/libnative.so",
					 f"slices/{records[0]['target']}/extras/libjit.so"):
			path = root / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(elf())
			inventory(root, records)
			with self.subTest(name=name), self.assertRaisesRegex(ValueError, "layout"):
				verify_package(root)
			path.unlink()
			while path.parent != root and not any(path.parent.iterdir()):
				parent = path.parent
				parent.rmdir()
				path = parent

	def test_sdk_is_target_data_and_header_changes_alter_identity(self) -> None:
		descriptor = self.sdk()
		first = inspect_sdk(descriptor)
		self.assertEqual(first["pythonVersion"], "3.14.7")
		(descriptor.parent / "include/pyconfig.h").write_text("#define TARGET_CHANGED 1", encoding="utf-8")
		second = inspect_sdk(descriptor)
		self.assertNotEqual(first["sdkFingerprint"], second["sdkFingerprint"])
		self.assertNotEqual(first["buildId"], second["buildId"])
		self.assertNotEqual(second["buildId"], inspect_sdk(descriptor, build_nonce="c" * 32)["buildId"])

	def test_target_sysconfig_is_never_executed(self) -> None:
		path = self.root / "config.py"
		path.write_text("raise RuntimeError('must not execute')\nbuild_time_vars = {'VERSION':'3.14'}", encoding="utf-8")
		self.assertEqual(sysconfig_values(path), {"VERSION": "3.14"})
		path.write_text("build_time_vars = dict(VERSION='3.14')", encoding="utf-8")
		with self.assertRaises(ValueError):
			sysconfig_values(path)

	def header_link(self, link: Path, target: Path, *, directory: bool = False) -> None:
		try:
			link.symlink_to(target, target_is_directory=directory)
		except NotImplementedError as error:
			self.skipTest(f"Creating SDK header symlinks is unavailable: {error}")
		except OSError as error:
			if error.errno in {errno.EPERM, errno.EACCES, errno.ENOSYS, getattr(errno, "ENOTSUP", errno.ENOSYS)} or getattr(
				error, "winerror", None
			) in {1, 5, 50, 1314}:
				self.skipTest(f"Creating SDK header symlinks is unavailable: {error}")
			raise

	def test_sdk_rejects_internal_header_directory_links(self) -> None:
		descriptor = self.sdk()
		target = descriptor.parent / "real-headers"
		target.mkdir()
		(target / "extra.h").write_text("#define EXTRA 1\n", encoding="utf-8")
		self.header_link(descriptor.parent / "include/internal", target, directory=True)
		with self.assertRaisesRegex(ValueError, "header subdirectories.*materialize"):
			inspect_sdk(descriptor)

	def test_sdk_rejects_escaping_header_directory_links(self) -> None:
		descriptor = self.sdk()
		target = self.root / "outside-sdk-headers"
		target.mkdir()
		(target / "extra.h").write_text("#define EXTRA 1\n", encoding="utf-8")
		self.header_link(descriptor.parent / "include/internal", target, directory=True)
		with self.assertRaisesRegex(ValueError, "header subdirectories.*materialize"):
			inspect_sdk(descriptor)

	def test_sdk_keeps_internal_file_links_and_resolved_include_root(self) -> None:
		descriptor = self.sdk()
		target = descriptor.parent / "extra.h"
		target.write_text("#define EXTRA 1\n", encoding="utf-8")
		self.header_link(descriptor.parent / "include/extra.h", target)
		self.header_link(descriptor.parent / "headers-link", descriptor.parent / "include", directory=True)
		metadata = read_json(descriptor)
		metadata["includeDirectory"] = "headers-link"
		descriptor.write_text(json.dumps(metadata), encoding="utf-8")
		first = inspect_sdk(descriptor)
		self.assertEqual(Path(first["includeDirectory"]), (descriptor.parent / "include").resolve())
		target.write_text("#define EXTRA 2\n", encoding="utf-8")
		second = inspect_sdk(descriptor)
		self.assertNotEqual(first["sdkFingerprint"], second["sdkFingerprint"])
		self.assertNotEqual(first["buildId"], second["buildId"])

	def test_native_include_and_export_changes_alter_mobile_build_identity(self) -> None:
		descriptor = self.sdk()
		source = self.root / "source"
		header = source / "VeroScript/Public/VeroScriptVersion.h"
		header.parent.mkdir(parents=True)
		header.write_bytes((ROOT / "VeroScript/Public/VeroScriptVersion.h").read_bytes())
		(source / "CMakeLists.txt").write_text("# fixture\n", encoding="utf-8")
		routes = [f"Extensions/Language/Python/VeroScriptPython/Private/native{suffix}"
				  for suffix in (".inl", ".inc", ".def")]
		routes.extend(f"Modules/Execution/VeroScriptBytecodeVm/{route}" for route in (
			"Private/native.cpp", "Private/native.inl", "Public/native.h", "CMakeLists.txt"))
		routes.extend(("Shared/Private/VeroScriptFloatParsing.h", "Shared/Private/VeroScriptFloatFormatting.h",
			"Shared/Private/VeroScriptDarwinPaths.h", "Shared/Private/VeroScriptJsonString.h"))
		for route in routes:
			path = source / route
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text("before\n", encoding="utf-8")
			with patch("mobile_sdk.ROOT", source):
				before = inspect_sdk(descriptor, build_nonce="d" * 32)
				path.write_text("after\n", encoding="utf-8")
				after = inspect_sdk(descriptor, build_nonce="d" * 32)
			with self.subTest(route=route):
				self.assertEqual(before["sdkFingerprint"], after["sdkFingerprint"])
				self.assertNotEqual(before["buildId"], after["buildId"])

	def test_mobile_output_and_cmake_cache_do_not_change_source_identity(self) -> None:
		descriptor = self.sdk()
		source = self.root / "source"
		header = source / "VeroScript/Public/VeroScriptVersion.h"
		header.parent.mkdir(parents=True)
		header.write_bytes((ROOT / "VeroScript/Public/VeroScriptVersion.h").read_bytes())
		(source / "CMakeLists.txt").write_text("# root source\n", encoding="utf-8")
		binding = source / "Extensions/Language/Python/VeroScriptPython"
		binding.mkdir(parents=True)
		implementation = binding / "Private/native.inl"
		implementation.parent.mkdir()
		implementation.write_text("# source\n", encoding="utf-8")
		cmake = binding / "CMakeLists.txt"
		cmake.write_text("# native source configuration\n", encoding="utf-8")
		# A cache marker at a selected source root must not hide its sources.
		(binding / "CMakeCache.txt").write_text("# stale in-source cache\n", encoding="utf-8")
		with patch("mobile_sdk.ROOT", source):
			initial = inspect_sdk(descriptor, build_nonce="d" * 32)
			for name, marker in ((".veroscript-python-mobile-fixture", None),
								 ("custom-native-build", "CMakeCache.txt"),
								 ("published-sdk", "python-mobile.json")):
				output = binding / name
				output.mkdir()
				if marker:
					(output / marker).write_text("{}\n", encoding="utf-8")
				sdk = output / "sdk/native"
				sdk.mkdir(parents=True)
				for filename in ("pyconfig.h", "target.json", "configure.cmake", "sysconfig.py"):
					path = sdk / filename
					path.write_text("first generated version\n", encoding="utf-8")
					first = inspect_sdk(descriptor, build_nonce="d" * 32)
					path.write_text("second generated version\n", encoding="utf-8")
					second = inspect_sdk(descriptor, build_nonce="d" * 32)
					with self.subTest(output=name, filename=filename):
						self.assertEqual(initial["buildId"], first["buildId"])
						self.assertEqual(initial["buildId"], second["buildId"])
			implementation.write_text("# changed real source\n", encoding="utf-8")
			changed_source = inspect_sdk(descriptor, build_nonce="d" * 32)
			cmake.write_text("# changed real native configuration\n", encoding="utf-8")
			changed_configuration = inspect_sdk(descriptor, build_nonce="d" * 32)
			self.assertNotEqual(initial["buildId"], changed_source["buildId"])
			self.assertNotEqual(changed_source["buildId"], changed_configuration["buildId"])

	def test_duplicate_json_and_escaping_paths_rejected(self) -> None:
		path = self.root / "duplicate.json"
		path.write_text('{"schemaVersion":1,"schemaVersion":1}', encoding="utf-8")
		with self.assertRaises(ValueError):
			read_json(path)
		for value in ("../outside", str(self.root), "include;other"):
			with self.subTest(value=value), self.assertRaises(ValueError):
				sdk_path(self.root, value)

	def test_target_free_threaded_and_wrong_machine_rejected(self) -> None:
		descriptor = self.sdk()
		config = descriptor.parent / "_sysconfigdata.py"
		original = config.read_text(encoding="utf-8")
		config.write_text(original.replace("'Py_GIL_DISABLED':0", "'Py_GIL_DISABLED':1"), encoding="utf-8")
		with self.assertRaisesRegex(ValueError, "free-threaded"):
			inspect_sdk(descriptor)
		config.write_text(original, encoding="utf-8")
		(descriptor.parent / "libpython3.14.so").write_bytes(elf("x86_64"))
		with self.assertRaisesRegex(ValueError, "requested ABI"):
			inspect_sdk(descriptor)

	def test_android_page_alignment_and_api_gate(self) -> None:
		data = bytearray(elf())
		verify_android_elf(data, "arm64-v8a")
		struct.pack_into("<Q", data, 112, 4096)
		with self.assertRaisesRegex(ValueError, "16 KB"):
			verify_android_elf(data, "arm64-v8a")
		data = bytearray(elf())
		struct.pack_into("<I", data, 196, 27)
		with self.assertRaisesRegex(ValueError, "newer API"):
			verify_android_elf(data, "arm64-v8a")

	def test_android_relro_page_rounding_preserves_writable_data(self) -> None:
		def library(relro_start: int, relro_size: int, writable_size: int) -> bytes:
			data = bytearray(elf())
			headers = bytes(data[64:176])
			headers += struct.pack("<IIQQQQQQ", 1, 6, 0, 0x4000, 0x4000, 0, writable_size, 16384)
			headers += struct.pack("<IIQQQQQQ", 0x6474E552, 4, 0, relro_start, relro_start, 0, relro_size, 1)
			struct.pack_into("<Q", data, 32, len(data))
			struct.pack_into("<H", data, 56, 4)
			data.extend(headers)
			return bytes(data)

		# NDK r28's STL has RELRO ending with its LOAD, followed by a gap.
		verify_android_elf(library(0x4000, 0x1000, 0x1000), "arm64-v8a")
		verify_android_elf(library(0x4000, 0x4000, 0x5000), "arm64-v8a")
		for data in (library(0x4000, 0x1000, 0x2000), library(0x5000, 0x3000, 0x4000)):
			with self.subTest(data=data), self.assertRaisesRegex(ValueError, "RELRO.*writable"):
				verify_android_elf(data, "arm64-v8a")

	def test_ios_device_and_simulator_are_distinct(self) -> None:
		data = macho("arm64", False, python=True)
		self.assertEqual(ios_binary(data, "arm64", False, "15.0")["installName"], "@rpath/Python.framework/Python")
		with self.assertRaisesRegex(ValueError, "Wrong Apple platform"):
			ios_binary(data, "arm64", True, "15.0")
		with self.assertRaisesRegex(ValueError, "architecture"):
			ios_binary(data, "x86_64", False, "15.0")

	def test_complete_matrix_and_host_target_build_commands(self) -> None:
		arm, intel = self.sdk(), self.sdk("x86_64")
		sdks = select_matrix([arm, intel], "android")
		with self.assertRaisesRegex(ValueError, "exactly one"):
			select_matrix([arm], "android")
		commands = build_commands(sdks[0], self.root / "build", self.root / "stage", sys.executable, "cmake", self.root / "ndk")
		configure = commands[0]
		self.assertIn("-DPython3_EXECUTABLE=" + sys.executable, configure)
		self.assertIn("-DVEROSCRIPT_PYTHON_TARGET_SDK=" + str(arm.resolve()), configure)
		self.assertIn("-DVEROSCRIPT_INTEROP_STATIC=ON", configure)
		self.assertIn("-DBUILD_TESTING=OFF", configure)
		self.assertTrue(all(command[0] == "cmake" for command in commands))

	def test_build_paths_are_resolved_from_callers_directory(self) -> None:
		descriptor = self.sdk()
		sdk = select_matrix([descriptor, self.sdk("x86_64")], "android")[0]
		with patch("package_mobile.shutil.which", return_value=sys.executable):
			configure = build_commands(sdk, self.root / "build", self.root / "stage", "python-on-PATH", "cmake", Path("relative-ndk"))[0]
		self.assertIn("-DPython3_EXECUTABLE=" + sys.executable, configure)
		self.assertIn("-DCMAKE_TOOLCHAIN_FILE=" + str(Path("relative-ndk").resolve() / "build/cmake/android.toolchain.cmake"), configure)

	def test_resource_omissions_and_stale_resources_cannot_pass_a_new_inventory(self) -> None:
		root, records = self.android_package()
		package = root / "slices" / records[0]["target"] / "python/veroscript"
		for name in ("stdlib/random.vs", "localization/zh-CN/language.json", "_mobile.py"):
			path = package / name
			original = path.read_bytes()
			path.unlink()
			inventory(root, records)
			with self.subTest(name=name), self.assertRaisesRegex(ValueError, "resources"):
				verify_package(root)
			path.write_bytes(original + b"\n")
			inventory(root, records)
			with self.subTest(changed=name), self.assertRaisesRegex(ValueError, "resources"):
				verify_package(root)
			path.write_bytes(original)

	def test_failed_device_retry_invalidates_old_success(self) -> None:
		report = self.root / "report.json"
		report.write_text('{"schemaVersion":1,"passed":true}', encoding="utf-8")
		with patch.object(sys, "platform", "win32"), patch.dict(sys.modules, {"veroscript": ModuleType("veroscript"), "veroscript._veroscript": ModuleType("veroscript._veroscript")}):
			with self.assertRaisesRegex(RuntimeError, "actual mobile"):
				run(report)
		self.assertIs(read_json(report)["passed"], False)

	def test_smoke_script_exercises_the_real_host_binding_without_a_device_proof(self) -> None:
		if importlib.util.find_spec("veroscript") is None:
			self.skipTest("Host runtime regression requires the built or installed binding")
		self.assertEqual(exercise_runtime(), 4)

	def test_compiler_stack_gate_checks_nesting_diagnostics_and_session_reuse(self) -> None:
		output = []
		class Session:
			def __init__(self, accepts_deep=False, recovers=True):
				self.sources = []
				self.accepts_deep, self.recovers = accepts_deep, recovers
			def load_source(self, source, label):
				self.sources.append(source)
				failed = len(self.sources) == 2 and not self.accepts_deep
				return SimpleNamespace(success=not failed, diagnostics=["nesting diagnostic"] if failed else [])
			def run(self):
				output.append("42" if len(self.sources) == 1 else "7" if self.recovers else "wrong")
				return SimpleNamespace(success=True, diagnostics=[])
		session = Session()
		check_compiler_nesting(session, output)
		self.assertEqual([source.count("(") - 2 for source in session.sources[:2]], [200, 512])
		self.assertEqual(len(session.sources), 3)
		self.assertEqual(output, ["7"])
		with self.assertRaisesRegex(RuntimeError, "Missing bounded"):
			check_compiler_nesting(Session(accepts_deep=True), output)
		with self.assertRaisesRegex(RuntimeError, "session reuse"):
			check_compiler_nesting(Session(recovers=False), output)

	def test_device_report_uses_native_abi_and_normalized_product_version(self) -> None:
		report = self.root / "report.json"
		# Deliberately model the actual core version format and an iPhone's
		# machine identifier; neither is the release manifest's ABI/version.
		facade = ModuleType("veroscript")
		facade.RUNTIME_VERSION = "0.1.1 (bytecode 0.1.1)"
		facade.__version__ = "0.1.1"
		facade.ABI_VERSION = 0x101
		facade.HOST_PLATFORM = "ios-arm64"
		native = ModuleType("veroscript._veroscript")
		native.MOBILE_BUILD_ID = "a" * 64
		with patch.dict(sys.modules, {"veroscript": facade, "veroscript._veroscript": native}), patch("device_smoke.exercise_runtime", return_value=4), patch("device_smoke.platform.machine", return_value="iPhone13,2"), patch.object(sys, "platform", "ios"):
			result = run(report)
		self.assertEqual(result["architecture"], "arm64")
		self.assertEqual(result["variant"], "device")
		self.assertEqual(result["productVersion"], "0.1.1")
		self.assertIs(result["compilerNestingChecked"], True)
		self.assertEqual(device_identity("ios", "ios-simulator-x64"), ("x86_64", "simulator"))
		self.assertEqual(device_identity("android", "android-x64"), ("x86_64", "device"))
		with self.assertRaisesRegex(RuntimeError, "Unsupported"):
			device_identity("ios", "ios-catalyst-arm64")

	def test_ios_private_initializer_is_not_an_export(self) -> None:
		metadata = record("ios", "arm64")
		slice_files(self.root, metadata)
		path = self.root / "Frameworks/veroscript._veroscript.framework/veroscript._veroscript"
		data = bytearray(path.read_bytes())
		commands_size = struct.unpack_from("<I", data, 20)[0]
		data[32 + commands_size + 4] = 0x1F  # N_PEXT | N_SECT | N_EXT
		path.write_bytes(data)
		with self.assertRaisesRegex(ValueError, "initializer"):
			audit_slice(self.root, metadata)

	def test_cmake_discovers_only_host_interpreter_and_target_sdk(self) -> None:
		cmake = os.environ.get("VEROSCRIPT_TEST_CMAKE") or shutil.which("cmake")
		if not cmake:
			self.skipTest("CMake contract probe requires CMake")
		if sys.version_info[:2] not in {(3, 13), (3, 14)}:
			self.skipTest("Mobile configure requires host CPython 3.13/3.14")
		descriptor = self.sdk()
		if sys.version_info.minor == 13:
			config = descriptor.parent / "_sysconfigdata.py"
			config.write_text(config.read_text().replace("3.14", "3.13").replace("314", "313"), encoding="utf-8")
			header = descriptor.parent / "include/patchlevel.h"
			header.write_text(header.read_text().replace("VERSION 14", "VERSION 13"), encoding="utf-8")
			(descriptor.parent / "libpython3.14.so").rename(descriptor.parent / "libpython3.13.so")
			metadata = read_json(descriptor)
			metadata["library"] = "libpython3.13.so"
			descriptor.write_text(json.dumps(metadata), encoding="utf-8")
		ios_descriptors = {}
		for variant, sysroot in (("device", "iphoneos"), ("simulator", "iphonesimulator")):
			sdk_root = self.root / ("ios-" + variant)
			shutil.copytree(descriptor.parent, sdk_root)
			library = sdk_root / "Python.framework/Python"
			library.parent.mkdir()
			library.write_bytes(macho("arm64", variant == "simulator", python=True))
			config = sdk_root / "_sysconfigdata.py"
			config.write_text(config.read_text().replace("-android.so", "-" + sysroot + ".so"), encoding="utf-8")
			metadata = read_json(sdk_root / "sdk.json")
			metadata.update(platform="ios", architecture="arm64", variant=variant,
							minimumVersion="15.0", library="Python.framework/Python")
			ios_descriptors[variant] = sdk_root / "sdk.json"
			ios_descriptors[variant].write_text(json.dumps(metadata), encoding="utf-8")
		source = self.root / "cmake-probe"
		(source / "Private").mkdir(parents=True)
		(source / "Private/VeroScriptPython.cpp").write_text("void dummy() {}\n", encoding="utf-8")
		nonce = "d" * 32
		source.joinpath("CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.25)
project(HostProbe LANGUAGES CXX)
if(POLICY CMP0190)
  cmake_policy(SET CMP0190 NEW)
endif()
set(CMAKE_CROSSCOMPILING TRUE)
if(PROBE_PLATFORM STREQUAL "ios")
  set(ANDROID FALSE)
  set(CMAKE_SYSTEM_NAME iOS)
  set(CMAKE_OSX_ARCHITECTURES arm64)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
  set(CMAKE_OSX_SYSROOT "${{PROBE_SYSROOT}}")
else()
  set(ANDROID TRUE)
  set(CMAKE_SYSTEM_VERSION 26)
  set(CMAKE_ANDROID_ARCH_ABI "${{PROBE_ABI}}")
endif()
set(VEROSCRIPT_INTEROP_STATIC ON)
set(VEROSCRIPT_MODULE_ROOT "{ROOT.as_posix()}")
include("{(ROOT / 'Extensions/Language/Python/VeroScriptPython/Resources/Tools/CMake/VeroScriptPythonCross.cmake').as_posix()}")
if(TARGET Python3::Module OR TARGET Python3::Python OR Python3_Development.Module_FOUND)
  message(FATAL_ERROR "Host Development artifacts leaked into the mobile target")
endif()
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/contract.txt" CONTENT
  "$<TARGET_PROPERTY:VeroScriptPythonTarget,INTERFACE_INCLUDE_DIRECTORIES>\\n$<TARGET_PROPERTY:VeroScriptPythonTarget,INTERFACE_LINK_LIBRARIES>\\n$<TARGET_PROPERTY:VeroScriptPython,COMPILE_DEFINITIONS>\\n")
''', encoding="utf-8")
		for name, target_sdk, platform, abi, sysroot, testing, error in (
			("valid", descriptor, "android", "arm64-v8a", "", "OFF", None),
			("abi", descriptor, "android", "x86_64", "", "OFF", "ABI/API"),
			("testing", descriptor, "android", "arm64-v8a", "", "ON", "BUILD_TESTING=OFF"),
			("ios-device", ios_descriptors["device"], "ios", "", "/SDKs/iPhoneOS.sdk", "OFF", None),
			("ios-simulator", ios_descriptors["simulator"], "ios", "", "/SDKs/iPhoneSimulator.sdk", "OFF", None),
			("ios-variant", ios_descriptors["device"], "ios", "", "/SDKs/iPhoneSimulator.sdk", "OFF", "SDK disagreement"),
		):
			build = self.root / ("probe-" + name)
			result = subprocess.run([cmake, "-S", str(source), "-B", str(build),
				"-DPython3_EXECUTABLE=" + sys.executable, "-DVEROSCRIPT_PYTHON_TARGET_SDK=" + str(target_sdk),
				"-DVEROSCRIPT_PYTHON_BUILD_NONCE=" + nonce, "-DPROBE_PLATFORM=" + platform,
				"-DPROBE_ABI=" + abi, "-DPROBE_SYSROOT=" + sysroot, "-DBUILD_TESTING=" + testing],
				capture_output=True, text=True)
			output = result.stdout + result.stderr
			with self.subTest(name=name):
				if error:
					self.assertNotEqual(result.returncode, 0, output)
					self.assertIn(error, output)
				else:
					self.assertEqual(result.returncode, 0, output)
					contract = (build / "contract.txt").read_text().replace("\\", "/")
					sdk = inspect_sdk(target_sdk, build_nonce=nonce)
					self.assertIn(sdk["includeDirectory"].replace("\\", "/"), contract)
					self.assertIn(sdk["library"].replace("\\", "/"), contract)
					self.assertIn(sdk["buildId"], contract)

	def test_android_native_dependencies_and_build_identity_gate(self) -> None:
		metadata = record("android", "arm64-v8a")
		slice_files(self.root, metadata)
		audit_slice(self.root, metadata)
		path = self.root / "jniLibs/arm64-v8a/libveroscript_python.so"
		path.write_bytes(elf(build_id=metadata["buildId"], dependency="libUnexpected.so"))
		with self.assertRaisesRegex(ValueError, "dependencies"):
			audit_slice(self.root, metadata)
		path.write_bytes(elf(build_id="c" * 64))
		with self.assertRaisesRegex(ValueError, "build identity"):
			audit_slice(self.root, metadata)

	def test_package_hash_and_missing_device_evidence_gate(self) -> None:
		root, records = self.android_package()
		verify_package(root)
		with self.assertRaisesRegex(ValueError, "Missing matching"):
			verify_package(root, require_devices=True)
		(root / "LICENSE").write_text("tampered", encoding="utf-8")
		with self.assertRaisesRegex(ValueError, "hash mismatch"):
			verify_package(root)

	def test_mobile_package_is_functional_without_documentation(self) -> None:
		root, records = self.android_package()
		verify_package(root)
		self.assertFalse((root / "README.md").exists())
		self.assertFalse((root / "Resources/Docs").exists())

	def test_mobile_package_rejects_documentation_after_rehash(self) -> None:
		root, records = self.android_package()
		for name in ("README.md", "Resources/Docs/ko-KR/python/mobile.md", "Examples/main.py"):
			path = root / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_text("manual", encoding="utf-8")
			inventory(root, records)
			with self.subTest(name=name), self.assertRaisesRegex(ValueError, "Non-functional"):
				verify_package(root)
			path.unlink()
			if name.startswith("Resources/"):
				shutil.rmtree(root / "Resources")
			elif name.startswith("Examples/"):
				shutil.rmtree(root / "Examples")

	def test_device_evidence_requires_matching_builds_and_page_sizes(self) -> None:
		root, records = self.android_package()
		reports = []
		for index, (item, page_size) in enumerate(((records[0], 4096), (records[0], 16384), (records[1], 4096))):
			path = self.root / f"report-{index}.json"
			path.write_text(json.dumps({**item, "schemaVersion": 1, "passed": True, "testCount": 4, "compilerNestingChecked": True,
									   "abiVersion": 0x101, "pageSize": page_size}), encoding="utf-8")
			reports.append(path)
		verify_package(root, reports, require_devices=True)
		legacy = read_json(reports[0])
		del legacy["compilerNestingChecked"]
		legacy_path = self.root / "legacy-proof.json"
		legacy_path.write_text(json.dumps(legacy), encoding="utf-8")
		with self.assertRaisesRegex(ValueError, "4 KB and 16 KB"):
			verify_package(root, [legacy_path, reports[1], reports[2]], require_devices=True)
		with self.assertRaisesRegex(ValueError, "4 KB and 16 KB"):
			verify_package(root, [reports[0], reports[2]], require_devices=True)
		wrong = read_json(reports[1])
		wrong["buildId"] = "c" * 64
		reports[1].write_text(json.dumps(wrong), encoding="utf-8")
		with self.assertRaisesRegex(ValueError, "4 KB and 16 KB"):
			verify_package(root, reports, require_devices=True)

	def test_slice_identity_cannot_contradict_target_name(self) -> None:
		root, records = self.android_package()
		records[0]["architecture"] = "x86_64"
		inventory(root, records)
		with self.assertRaisesRegex(ValueError, "Target identity"):
			verify_package(root)

	def test_ios_markers_and_xcframework_members(self) -> None:
		records = [record("ios", "arm64"), record("ios", "arm64", "simulator"), record("ios", "x86_64", "simulator")]
		frameworks = []
		for item in records:
			stage = self.root / "slices" / item["target"]
			slice_files(stage, item)
			audit_slice(stage, item)
			frameworks.append(stage / "Frameworks/veroscript._veroscript.framework")
		self.assertEqual((frameworks[1] / "veroscript._veroscript.origin").read_bytes(), (frameworks[2] / "veroscript._veroscript.origin").read_bytes())
		xc = self.root / "VeroScriptPython.xcframework"
		device, simulator = "ios-arm64", "ios-arm64_x86_64-simulator"
		shutil.copytree(frameworks[0], xc / device / frameworks[0].name)
		shutil.copytree(frameworks[1], xc / simulator / frameworks[1].name)
		arm, intel = ((path / "veroscript._veroscript").read_bytes() for path in frameworks[1:])
		start, second = 64, 64 + len(arm)
		second = (second + 7) & ~7
		fat = struct.pack(">II", 0xCAFEBABE, 2)
		fat += struct.pack(">IIIII", 0x100000C, 0, start, len(arm), 3)
		fat += struct.pack(">IIIII", 0x1000007, 0, second, len(intel), 3)
		fat += bytes(start - len(fat)) + arm + bytes(second - start - len(arm)) + intel
		target = xc / simulator / frameworks[1].name / "veroscript._veroscript"
		target.write_bytes(fat)
		(xc / "Info.plist").write_bytes(plistlib.dumps({"XCFrameworkFormatVersion": "1.0", "AvailableLibraries": [
			{"LibraryIdentifier": device, "LibraryPath": frameworks[0].name, "SupportedPlatform": "ios", "SupportedArchitectures": ["arm64"]},
			{"LibraryIdentifier": simulator, "LibraryPath": frameworks[1].name, "SupportedPlatform": "ios", "SupportedPlatformVariant": "simulator", "SupportedArchitectures": ["arm64", "x86_64"]}]}))
		self.assertEqual(macho_members(fat), {"arm64": arm, "x86_64": intel})
		audit_xcframework(self.root, records)
		inventory(self.root, records)
		verify_package(self.root)
		hidden = frameworks[0] / "Resources/renamed-payload"
		hidden.parent.mkdir()
		hidden.write_bytes(elf())
		inventory(self.root, records)
		with self.assertRaisesRegex(ValueError, "Unexpected native payload"):
			verify_package(self.root)
		hidden.unlink()
		hidden.parent.rmdir()
		extra = xc / "undeclared-platform"
		extra.mkdir()
		with self.assertRaisesRegex(ValueError, "layout"):
			audit_xcframework(self.root, records)
		extra.rmdir()
		target.write_bytes(arm)
		with self.assertRaisesRegex(ValueError, "disagree"):
			audit_xcframework(self.root, records)

	def test_checks_survive_optimized_python(self) -> None:
		with self.assertRaisesRegex(RuntimeError, "failure"):
			require(False, "failure")

	def test_android_loader_relative_path_and_cached_module(self) -> None:
		path = ROOT / "Extensions/Language/Python/VeroScriptPython/python/veroscript/_mobile.py"
		spec = importlib.util.spec_from_file_location("mobile_loader_test", path)
		if spec is None or spec.loader is None:
			self.fail("Missing mobile loader")
		loader = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(loader)
		with patch.object(sys, "platform", "android"), patch.dict("os.environ", {"VEROSCRIPT_ANDROID_NATIVE_LIB_DIR": "relative"}), patch.dict(sys.modules):
			sys.modules.pop("veroscript._veroscript", None)
			with self.assertRaisesRegex(ImportError, "absolute"):
				loader.load_native()
			cached = ModuleType("veroscript._veroscript")
			sys.modules["veroscript._veroscript"] = cached
			self.assertIs(loader.load_native(), cached)


if __name__ == "__main__":
	unittest.main()
