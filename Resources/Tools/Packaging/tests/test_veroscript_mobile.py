# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Mobile package identity checks; synthetic files do not replace SDK/device tests."""
from pathlib import Path
import importlib.util
import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_mobile import verify_android_aar, verify_android_elf, verify_ios_archive, verify_ios_object
from veroscript_mobile import mobile_catalog_files, verify_android_jit_aar, stage_android_aar
from veroscript_mobile import strip_mobile_debug, verify_mobile_no_debug

spec = importlib.util.spec_from_file_location("package_veroscript_mobile", Path(__file__).resolve().parents[1] / "package-veroscript-mobile.py")
package_mobile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package_mobile)


def elf(machine=183, alignment=16384, api=26, note_name=b"Android\0"):
	data = bytearray(256)
	data[:7] = b"\x7fELF\x02\x01\x01"
	struct.pack_into("<HHI", data, 16, 3, machine, 1)
	struct.pack_into("<Q", data, 32, 64)
	struct.pack_into("<HHH", data, 52, 64, 56, 2)
	struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0, 0, 256, 256, alignment)
	struct.pack_into("<IIQQQQQQ", data, 120, 4, 4, 192, 192, 0, 24, 24, 4)
	struct.pack_into("<III8sI", data, 192, 8, 4, 1, note_name, api)
	return bytes(data)


def elf_with_debug(machine=183):
	data = bytearray(elf(machine=machine))
	data.extend(b"\0" * (512 - len(data)))
	strings = b"\0.shstrtab\0.debug_info\0"
	data[256:256 + len(strings)] = strings
	data[288:304] = b"debug fixture!!!"
	struct.pack_into("<Q", data, 40, 320)
	struct.pack_into("<HHH", data, 58, 64, 3, 1)
	struct.pack_into("<IIQQQQIIQQ", data, 384, 1, 3, 0, 0, 256, len(strings), 0, 0, 1, 0)
	struct.pack_into("<IIQQQQIIQQ", data, 448, 11, 1, 0, 0, 288, 16, 0, 0, 1, 0)
	return bytes(data)


def macho(cpu=0x100000C, platform=2, minimum=15 << 16, sdk=18 << 16):
	return struct.pack("<IIIIIIIIIIIIII", 0xFEEDFACF, cpu, 0, 1, 1, 24, 0, 0,
					   0x32, 24, platform, minimum, sdk, 0)


def ar(payload):
	return b"!<arch>\n" + ("member.o/".ljust(16) + "0".ljust(12) + "0".ljust(6)
						   + "0".ljust(6) + "100644".ljust(8) + str(len(payload)).ljust(10)
						   + "`\n").encode("ascii") + payload + (b"\n" if len(payload) & 1 else b"")


def aar_entries():
	entries = {name: b"fixture" for name in (
		"AndroidManifest.xml", "assets/veroscript/stdlib/core.vs",
		"assets/veroscript/licenses/LICENSE", "assets/veroscript/licenses/NOTICE",
		"assets/veroscript/licenses/NOTICE.toolchain", "assets/veroscript/licenses/MINIZ-LICENSE.txt",
		"assets/veroscript/licenses/JSON-LICENSE.txt", "assets/veroscript/licenses/SHA256-LICENSE.txt")}
	classes = io.BytesIO()
	with zipfile.ZipFile(classes, "w") as jar:
		for name in ("VeroScriptEngine.class", "VeroScriptEngine$Result.class", "VeroScriptEngine$AotPrograms.class", "VeroScriptEngine$ExecutionBackend.class"):
			jar.writestr("com/vanstysanic/veroscript/android/" + name, b"fixture")
	entries["classes.jar"] = classes.getvalue()
	entries["prefab/prefab.json"] = json.dumps(
		{"schema_version": 2, "name": "VeroScriptAndroid", "dependencies": []}).encode()
	root = "prefab/modules/VeroScriptInterop/"
	entries[root + "module.json"] = b'{"export_libraries":[]}'
	entries[root + "include/VeroScriptInterop.h"] = b"fixture header"
	for abi, machine in (("arm64-v8a", 183), ("x86_64", 62)):
		for library in ("VeroScriptAndroid", "VeroScriptInterop", "c++_shared"):
			entries[f"jni/{abi}/lib{library}.so"] = elf(machine=machine)
		directory = root + f"libs/android.{abi}/"
		entries[directory + "abi.json"] = json.dumps(
			{"abi": abi, "api": 26, "ndk": 28, "stl": "c++_shared"}).encode()
		entries[directory + "libVeroScriptInterop.so"] = elf(machine=machine)
	return entries


class MobileBinaryTests(unittest.TestCase):
	def test_elf_without_section_names_requires_undefined_name_offsets(self):
		data = bytearray(elf())
		data.extend(bytes(128))
		struct.pack_into('<Q', data, 40, 256)
		struct.pack_into('<HHH', data, 58, 64, 2, 0)
		verify_mobile_no_debug(data, 'android')
		struct.pack_into('<I', data, 320, 1)
		with self.assertRaisesRegex(ValueError, 'without a string table'):
			verify_mobile_no_debug(data, 'android')

	def test_staged_native_debug_is_stripped_before_inventory_for_both_platforms(self):
		apple_debug = bytearray(macho())
		struct.pack_into('<II', apple_debug, 16, 2, 96)
		apple_debug.extend(struct.pack('<II16sQQQQIIII', 0x19, 72, b'__DWARF', 0, 0, 0, 0, 0, 0, 0, 0))
		apple_object_debug = bytearray(macho())
		struct.pack_into('<II', apple_object_debug, 16, 2, 176)
		apple_object_debug.extend(struct.pack('<II16sQQQQIIII', 0x19, 152, b'', 0, 0, 0, 0, 0, 0, 1, 0))
		apple_object_debug.extend(struct.pack('<16s16sQQIIIIIIII', b'__debug_info', b'__DWARF', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			tool = root / 'llvm-strip'
			tool.write_bytes(b'fixture target tool')
			for target, original, stripped in (
				('android', ar(elf_with_debug()), ar(elf())),
				('ios', ar(bytes(apple_debug)), ar(macho())),
				('ios', ar(bytes(apple_object_debug)), ar(macho()))):
				with self.subTest(target=target):
					library = root / (target + '.a')
					library.write_bytes(original)
					with self.assertRaisesRegex(ValueError, 'debug symbols'):
						verify_mobile_no_debug(original, target)
					def execute(arguments, **options):
						if target == 'android':
							self.assertEqual(arguments, [str(tool), '--strip-debug', str(library)])
							library.write_bytes(stripped)
						else:
							self.assertEqual(arguments[:4], ['xcrun', 'strip', '-S', '-o'])
							self.assertEqual(arguments[-1], str(library))
							output = Path(arguments[4])
							self.assertEqual(output.parent.parent, root)
							self.assertEqual(library.read_bytes(), original)
							output.write_bytes(stripped)
					with patch('veroscript_mobile.subprocess.run', side_effect=execute):
						strip_mobile_debug(library, target, tool if target == 'android' else None)
					verify_mobile_no_debug(library.read_bytes(), target)
					with patch('veroscript_mobile.subprocess.run') as command:
						strip_mobile_debug(library, target)
						command.assert_not_called()
					if target == 'ios':
						library.write_bytes(original)
						with patch('veroscript_mobile.subprocess.run', side_effect=OSError('strip failed')):
							with self.assertRaisesRegex(OSError, 'strip failed'):
								strip_mobile_debug(library, target)
						self.assertEqual(library.read_bytes(), original)
						self.assertEqual(list(root.glob('.veroscript-strip-*')), [])
			for unsupported in (b'BC\xc0\xde' + b'\0' * 64, ar(b'BC\xc0\xde' + b'\0' * 64)):
				with self.assertRaisesRegex(ValueError, 'LLVM bitcode'):
					verify_mobile_no_debug(unsupported, 'android')

	def test_android_requires_identity_architecture_and_page_alignment(self):
		self.assertEqual(verify_android_elf(elf(), "arm64-v8a")["minimumApi"], 26)
		self.assertEqual(verify_android_elf(elf(machine=62), "x86_64")["pageAlignment"], 16384)
		for data in (elf(machine=62), elf(alignment=4096), elf(alignment=24576),
					 elf(api=27), elf(note_name=b"GNU\0"), elf()[:210]):
			with self.subTest(data=data[-12:]), self.assertRaises(ValueError):
				verify_android_elf(data, "arm64-v8a")

	def test_android_rejects_writable_executable_load_segments(self):
		data = bytearray(elf())
		struct.pack_into("<I", data, 68, 7)
		with self.assertRaisesRegex(ValueError, "writable and executable"):
			verify_android_elf(bytes(data), "arm64-v8a")

	def test_optional_android_jit_contains_only_backend_libraries(self):
		entries = {"AndroidManifest.xml": b"fixture"}
		for name in ("LICENSE", "LICENSE.md", "VERSION.txt"):
			entries["assets/veroscript/licenses/asmjit/" + name] = b"fixture"
		for abi, machine in (("arm64-v8a", 183), ("x86_64", 62)):
			entries[f"jni/{abi}/libVeroScriptJitAsmJit.so"] = elf(machine=machine)
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "jit.aar"
			def write(values):
				with zipfile.ZipFile(path, "w") as archive:
					for name, data in values.items():
						archive.writestr(name, data)
			write(entries)
			self.assertEqual(len(verify_android_jit_aar(path)), 2)
			for name, data in (("jni/arm64-v8a/libc++_shared.so", elf()),
							   ("jni/arm64-v8a/libVeroScriptInterop.so", elf()),
							   ("../escape", b"bad")):
				write({**entries, name: data})
				with self.assertRaises(ValueError):
					verify_android_jit_aar(path)
			wrong = dict(entries)
			wrong["jni/x86_64/libVeroScriptJitAsmJit.so"] = elf()
			write(wrong)
			with self.assertRaises(ValueError):
				verify_android_jit_aar(path)

	def test_ios_rejects_same_architecture_from_wrong_apple_sdk(self):
		self.assertEqual(verify_ios_object(macho(), "arm64", False)["platform"], "ios")
		self.assertEqual(verify_ios_object(macho(platform=7), "arm64", True)["platform"], "ios-simulator")
		for data in (macho(platform=1), macho(platform=7), macho(cpu=0x1000007),
					 macho(minimum=16 << 16), macho(minimum=0), macho()[:52]):
			with self.subTest(data=data[-12:]), self.assertRaises(ValueError):
				verify_ios_object(data, "arm64", False)

	def test_every_archive_member_is_checked(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "mobile.a"
			path.write_bytes(ar(macho()))
			self.assertEqual(verify_ios_archive(path, "arm64", False), 1)
			path.write_bytes(ar(macho()) + ar(macho(platform=1))[8:])
			with self.assertRaises(ValueError):
				verify_ios_archive(path, "arm64", False)

	def test_ios_accepts_unspecified_sdk_but_still_checks_platform_and_minimum(self):
		# LLVM MC can emit LC_BUILD_VERSION with SDK 0 (displayed as n/a).
		self.assertEqual(verify_ios_object(macho(sdk=0), "arm64", False)["sdk"], 0)
		self.assertEqual(verify_ios_object(macho(platform=7, sdk=0), "arm64", True)["platform"], "ios-simulator")
		for data in (macho(platform=1, sdk=0), macho(minimum=16 << 16, sdk=0), macho(sdk=14 << 16)):
			with self.subTest(data=data[-16:]), self.assertRaises(ValueError):
				verify_ios_object(data, "arm64", False)

	def test_aar_requires_both_abis_and_checks_all_dependencies(self):
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "mobile.aar"
			def write(omit=None, bad_alignment=False, catalogs=None):
				with zipfile.ZipFile(path, "w") as archive:
					for key, value in (catalogs or {}).items():
						archive.writestr("assets/veroscript/localization/" + key, value)
					for name, data in aar_entries().items():
						if name != omit:
							if bad_alignment and name.startswith("jni/") and name.endswith("libc++_shared.so"):
								data = elf(machine=183 if "arm64-v8a" in name else 62, alignment=4096)
							archive.writestr(name, data)
			write()
			self.assertEqual(len(verify_android_aar(path)), 6)
			catalogs = {"en-US/mobile.json": b'{"mobile.fixture":"original"}'}
			write(catalogs=catalogs)
			self.assertEqual(len(verify_android_aar(path, catalogs=catalogs)), 6)
			with self.assertRaisesRegex(ValueError, "localization"):
				verify_android_aar(path, catalogs={"en-US/mobile.json": b"stale"})
			with self.assertRaisesRegex(ValueError, "localization"):
				verify_android_aar(path, catalogs={})
			write(omit="jni/x86_64/libVeroScriptInterop.so")
			with self.assertRaises(ValueError):
				verify_android_aar(path)
			write(bad_alignment=True)
			with self.assertRaises(ValueError):
				verify_android_aar(path)

	def test_aar_requires_usable_prefab_for_native_sdk_consumers(self):
		root = "prefab/modules/VeroScriptInterop/"
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "mobile.aar"
			header = Path(directory) / "VeroScriptInterop.h"
			header.write_bytes(b"fixture header")
			cases = (
				("prefab/prefab.json", None),
				("prefab/prefab.json", b"not-json"),
				("prefab/prefab.json", b'{"schema_version":99,"name":"fixture"}'),
				(root + "include/VeroScriptInterop.h", None),
				(root + "libs/android.x86_64/libVeroScriptInterop.so", None),
				(root + "libs/android.x86_64/libVeroScriptInterop.so", elf()),
				(root + "libs/android.x86_64/libVeroScriptInterop.so", elf(machine=62, alignment=4096)),
				(root + "libs/android.arm64-v8a/abi.json",
				 b'{"abi":"arm64-v8a","api":27,"ndk":28,"stl":"c++_shared"}'),
				(root + "libs/android.arm64-v8a/abi.json",
				 b'{"abi":"arm64-v8a","api":26,"ndk":28,"stl":"c++_static"}'),
				(root + "libs/android.arm64-v8a/abi.json",
				 b'{"abi":"x86_64","api":26,"ndk":28,"stl":"c++_shared"}'),
				(root + "libs/android.arm64-v8a/abi.json",
				 b'{"abi":"arm64-v8a","api":25,"ndk":28,"stl":"c++_shared"}'),
				(root + "libs/android.arm64-v8a/abi.json", b"[]"),
				(root + "module.json", b'{"library_name":"libWrongLibrary"}'),
			)
			for name, replacement in cases:
				with self.subTest(name=name, replacement=replacement):
					entries = aar_entries()
					if replacement is None:
						del entries[name]
					else:
						entries[name] = replacement
					with zipfile.ZipFile(path, "w") as archive:
						for key, value in entries.items():
							archive.writestr(key, value)
					with self.assertRaisesRegex(ValueError, "Prefab"):
						verify_android_aar(path)
			with zipfile.ZipFile(path, "w") as archive:
				for key, value in aar_entries().items():
					archive.writestr(key, value)
			self.assertEqual(len(verify_android_aar(path, interop_header=header)), 6)
			header.write_bytes(b"updated header")
			with self.assertRaisesRegex(ValueError, "Prefab"):
				verify_android_aar(path, interop_header=header)
			# Optional module metadata may be absent, but the published Prefab
			# runtime must match the stripped JNI bytes exactly.
			entries = aar_entries()
			del entries[root + "module.json"]
			with zipfile.ZipFile(path, "w") as archive:
				for key, value in entries.items():
					archive.writestr(key, value)
			self.assertEqual(len(verify_android_aar(path)), 6)
			entries[root + "libs/android.arm64-v8a/libVeroScriptInterop.so"] += b"debug fixture"
			with zipfile.ZipFile(path, "w") as archive:
				for key, value in entries.items():
					archive.writestr(key, value)
			with self.assertRaisesRegex(ValueError, "stripped JNI"):
				verify_android_aar(path)

	def test_staged_aar_keeps_one_stripped_runtime_for_jni_and_prefab(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			source, destination = root / "raw.aar", root / "release.aar"
			entries = aar_entries()
			for abi, machine in (("arm64-v8a", 183), ("x86_64", 62)):
				for library in ("VeroScriptInterop", "VeroScriptAndroid"):
					entries[f"prefab/modules/{library}/libs/android.{abi}/lib{library}.so"] = elf_with_debug(machine)
			with zipfile.ZipFile(source, "w") as archive:
				for name, data in entries.items():
					archive.writestr(name, data)
			before = source.read_bytes()
			stage_android_aar(source, destination)
			self.assertEqual(source.read_bytes(), before)
			self.assertEqual(len(verify_android_aar(destination)), 6)
			with zipfile.ZipFile(destination) as archive:
				self.assertEqual(set(archive.namelist()), set(entries))
				for name, data in entries.items():
					if name.startswith("prefab/") and name.endswith(".so"):
						parts = name.split("/")
						data = entries[f"jni/{parts[4][len('android.'): ]}/{parts[5]}"]
					self.assertEqual(archive.read(name), data)

	def test_staging_rejects_different_mapped_code_and_debug_jni(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			for case in ("mapped", "debug-jni"):
				with self.subTest(case=case):
					entries = aar_entries()
					prefab = "prefab/modules/VeroScriptInterop/libs/android.arm64-v8a/libVeroScriptInterop.so"
					if case == "mapped":
						data = bytearray(elf_with_debug())
						data[224] = 1
						entries[prefab] = bytes(data)
					else:
						entries[prefab] = entries["jni/arm64-v8a/libVeroScriptInterop.so"] = elf_with_debug()
					source, destination = root / (case + ".aar"), root / (case + "-release.aar")
					with zipfile.ZipFile(source, "w") as archive:
						for name, data in entries.items():
							archive.writestr(name, data)
					with self.assertRaisesRegex(ValueError, "mapped code/data|debug symbols"):
						stage_android_aar(source, destination)
					self.assertFalse(destination.exists())

	def test_aar_rejects_stale_standard_library(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			classes = io.BytesIO()
			with zipfile.ZipFile(classes, "w") as jar:
				for name in ("VeroScriptEngine.class", "VeroScriptEngine$Result.class", "VeroScriptEngine$AotPrograms.class", "VeroScriptEngine$ExecutionBackend.class"):
					jar.writestr("com/vanstysanic/veroscript/android/" + name, b"fixture")
			(root / "core.vs").write_bytes(b"updated")
			path = root / "mobile.aar"
			with zipfile.ZipFile(path, "w") as archive:
				archive.writestr("AndroidManifest.xml", b"fixture")
				archive.writestr("classes.jar", classes.getvalue())
				archive.writestr("assets/veroscript/stdlib/core.vs", b"stale")
				for name in ("LICENSE", "NOTICE", "NOTICE.toolchain", "MINIZ-LICENSE.txt", "JSON-LICENSE.txt", "SHA256-LICENSE.txt"):
					archive.writestr("assets/veroscript/licenses/" + name, b"fixture")
			with self.assertRaisesRegex(ValueError, "stale"):
				verify_android_aar(path, root)


class MobileBuildEntryTests(unittest.TestCase):
	def test_mobile_consumers_reuse_target_ssl_instead_of_host_environment(self):
		import veroscript_mobile_native as native
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			(root / 'mobile-native-sdk.json').write_text(json.dumps({'nativeVersion': '0.1.1', 'externalDependencies': ['OpenSSL']}))
			template = root / 'Resources/Tools/Packaging/MobileNativeConsumer/CMakeLists.txt'
			template.parent.mkdir(parents=True)
			template.write_text('fixture mobile consumer')
			binary = root / 'consumer'
			binary.write_bytes(b'fixture target executable')
			settings = {'CMAKE_GENERATOR': 'Xcode', 'CMAKE_COMMAND': 'cmake', 'CMAKE_SYSTEM_NAME': 'iOS',
				'CMAKE_PREFIX_PATH': '/target/ssl', 'CMAKE_FIND_ROOT_PATH': '/target/ssl',
				'OPENSSL_ROOT_DIR': '/target/ssl', 'OPENSSL_INCLUDE_DIR': '/target/ssl/include',
				'OPENSSL_SSL_LIBRARY': '/target/ssl/lib/libssl.a', 'OPENSSL_CRYPTO_LIBRARY': '/target/ssl/lib/libcrypto.a'}
			builds = {name: root / name for name in native.SLICES['ios']}
			with patch.dict(os.environ, {'OPENSSL_ROOT_DIR': '/host/openssl'}), \
					patch('veroscript_modules.build_cache', return_value=settings), \
					patch.object(native, 'consumer_executable', return_value=binary), \
					patch.object(native.subprocess, 'run') as command:
				native.qualify_sdk(root, root, 'Curl', 'ios', builds, root / 'work')
				self.assertEqual(len(command.call_args_list), 6)
				for call in command.call_args_list:
					self.assertNotIn('OPENSSL_ROOT_DIR', call.kwargs['env'])
					if '-S' in call.args[0]:
						for key in ('OPENSSL_ROOT_DIR', 'OPENSSL_INCLUDE_DIR', 'OPENSSL_SSL_LIBRARY', 'OPENSSL_CRYPTO_LIBRARY'):
							self.assertIn('-D' + key + ':PATH=' + settings[key], call.args[0])

	def test_ios_combines_the_vm_archive_in_every_static_slice(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			stage = root / "stage"
			stage.mkdir()
			(root / "Extensions/Platform/Mobile/VeroScriptIOS/Public").mkdir(parents=True)
			interop = root / "VeroScriptInterop/Public"
			interop.mkdir(parents=True)
			(interop / "VeroScriptInterop.h").write_text("#pragma once\n")
			(root / "VeroScript/Resources/Scripts/stdlib").mkdir(parents=True)
			commands = []

			def execute(arguments, **options):
				commands.append(list(map(str, arguments)))
				if arguments[:2] == ["xcrun", "libtool"]:
					Path(arguments[arguments.index("-o") + 1]).write_bytes(b"archive")
				elif arguments[:2] == ["xcrun", "lipo"]:
					Path(arguments[arguments.index("-output") + 1]).write_bytes(b"universal")

			with patch.object(package_mobile.platform, "system", return_value="Darwin"), \
				 patch.object(package_mobile.subprocess, "run", side_effect=execute), \
				 patch.object(package_mobile, "strip_mobile_debug"), \
				 patch.object(package_mobile, "find_archive", side_effect=lambda build, name: build / ("lib" + name + ".a")), \
				 patch.object(package_mobile, "verify_ios_archive", return_value=1), \
				 patch.object(package_mobile, "mobile_catalog_files", return_value={}), \
				 patch.object(package_mobile, "stage_aot_headers", return_value={}):
				package_mobile.ios(root, stage, "cmake")
			merges = [command for command in commands if command[:2] == ["xcrun", "libtool"]]
			self.assertEqual(len(merges), 3)
			self.assertTrue(all(any(Path(arg).name == "libVeroScriptBytecodeVm.a" for arg in command) for command in merges))
			configurations = [command for command in commands if command[:2] == ["cmake", "-S"]]
			self.assertTrue(all("-DVEROSCRIPT_BUILD_VM_BACKEND=ON" in command
								and "-DVEROSCRIPT_INTEROP_DEFAULT_VM=ON" in command for command in configurations))
			for option in ('VEROSCRIPT_BUILD_APPLICATION', 'VEROSCRIPT_BUILD_PACKAGE_MANAGER',
						   'VEROSCRIPT_BUILD_PACKAGE_RUNTIME', 'VEROSCRIPT_BUILD_BUILD_SYSTEM', 'VEROSCRIPT_ENABLE_ASMJIT'):
				self.assertTrue(all('-D' + option + '=OFF' in command for command in configurations))

	def test_mobile_catalogs_include_complete_owned_profile(self):
		repo = Path(__file__).resolve().parents[4]
		catalogs = mobile_catalog_files(repo)
		for tag in ("en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"):
			self.assertTrue(all(f"{tag}/{name}.json" in catalogs for name in
								("language", "runtime", "embedding", "interop", "mobile")))
		self.assertFalse(any(key.endswith(("tooling.json", "lsp.json", "dap.json")) for key in catalogs))

	def test_release_archive_selection_ignores_ancestor_directory_names(self):
		with tempfile.TemporaryDirectory(prefix="ReleaseEngineering-") as directory:
			build = Path(directory) / "build"
			release = build / "lib/Release-iphoneos/libVeroScriptIOS.a"
			for candidate in (release, build / "lib/Debug-iphoneos/libVeroScriptIOS.a",
							  build / "lib/ReleaseBackup/libVeroScriptIOS.a"):
				candidate.parent.mkdir(parents=True)
				candidate.write_bytes(b"fixture")
			self.assertEqual(package_mobile.find_archive(build, "VeroScriptIOS"), release)
			release.unlink()
			with self.assertRaises(ValueError):
				package_mobile.find_archive(build, "VeroScriptIOS")

	@unittest.skipUnless(os.name == "nt", "Windows Gradle batch command resolution")
	def test_android_finds_gradle_batch_on_path(self):
		with tempfile.TemporaryDirectory(prefix="vero gradle ") as directory:
			root = Path(directory)
			command = root / "gradle.cmd"
			marker = root / "gradle-invoked.txt"
			command.write_text('@echo off\necho built>"' + str(marker) + '"\n', encoding="utf-8")
			project = root / "Extensions/Platform/Mobile/VeroScriptAndroid"
			archive = project / "build/outputs/aar/veroscript-release.aar"
			archive.parent.mkdir(parents=True)
			with zipfile.ZipFile(archive, "w") as output:
				for name, data in aar_entries().items():
					output.writestr(name, data)
			stage = root / "stage"
			stage.mkdir()
			# The producer now reads native/product versions from the real release contract.
			tools = root / 'Resources/Tools/Packaging'
			tools.mkdir(parents=True)
			(tools / 'ReleaseVersions.json').write_bytes((Path(__file__).resolve().parents[1] / 'ReleaseVersions.json').read_bytes())
			source_marker = root / 'VeroScript/Public/VeroScriptVersion.h'
			source_marker.parent.mkdir(parents=True)
			source_marker.write_text('// source root marker\n', encoding='utf-8')
			with patch.dict(os.environ, {"PATH": str(root) + os.pathsep + os.environ["PATH"]}), \
					patch.object(package_mobile, "mobile_catalog_files", return_value={}), \
					patch.object(package_mobile, "verify_android_aar", return_value={}), \
					patch.object(package_mobile, "stage_aot_headers", return_value={}), \
					patch.object(package_mobile, "aot_source_contract", return_value={}), \
					patch.object(package_mobile, "matching_aot_header", return_value=True):
				for abi in ("arm64-v8a", "x86_64"):
					header = project / ".cxx/Release" / abi / "VeroScriptJitBuild.h"
					header.parent.mkdir(parents=True)
					header.write_text("a" * 64)
				package_mobile.android(root, stage, "gradle")
			self.assertTrue(marker.is_file(), "Gradle batch command was not executed")
			self.assertTrue((stage / "VeroScriptAndroid-0.1.1.aar").is_file())

	def test_aot_service_header_bundle_preserves_matching_target_contract(self):
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			public = root / "VeroScript/Public"
			private = root / "VeroScript/Private"
			public.mkdir(parents=True)
			private.mkdir(parents=True)
			(public / "VeroScriptCompiledExecution.h").write_text("#pragma once\n", encoding="utf-8")
			implementation = b"// Core implementation\n"
			(private / "Core.cpp").write_bytes(implementation)
			(private / "a.cpp").write_bytes(b"// Lower-case file\n")
			(private / "B.cpp").write_bytes(b"// Upper-case file\n")
			shared = root / "Shared/Private/VeroScriptFloatParsing.h"
			shared.parent.mkdir(parents=True)
			shared.write_bytes(b"// Shared float conversion\n")
			json_string = shared.with_name("VeroScriptJsonString.h")
			json_string.write_bytes(b"// Shared JSON escaping\n")
			formatting = shared.with_name("VeroScriptFloatFormatting.h")
			formatting.write_bytes(b"// Shared float formatting\n")
			identity = hashlib.sha256("".join(hashlib.sha256(path.read_bytes()).hexdigest()
				for path in sorted([*private.glob("*.cpp"), shared, formatting, json_string], key=lambda path: path.as_posix())).encode("ascii")).hexdigest()
			abi = package_mobile.core_public_abi(public)
			generated = root / "VeroScriptJitBuild.h"
			generated.write_text(f'#define VEROSCRIPT_JIT_PUBLIC_ABI "{abi}"\n#define VEROSCRIPT_CORE_IMPLEMENTATION_ID "{identity}"\n', encoding="utf-8")
			metadata = package_mobile.stage_aot_headers(root, [generated], root / "sdk")
			self.assertEqual(metadata["includeDirectory"], "aot/include")
			self.assertEqual((root / "sdk/aot/include/VeroScriptJitBuild.h").read_bytes(), generated.read_bytes())
			self.assertTrue((root / "sdk/aot/include/VeroScriptCompiledExecution.h").is_file())
			original_shared = shared.read_bytes()
			shared.write_text("// Changed shared float conversion\n", encoding="utf-8")
			with self.assertRaisesRegex(ValueError, "Stale"):
				package_mobile.stage_aot_headers(root, [generated], root / "stale-shared-sdk")
			self.assertFalse((root / "stale-shared-sdk").exists())
			shared.write_bytes(original_shared)
			original_json = json_string.read_bytes()
			json_string.write_text("// Changed JSON escaping\n", encoding="utf-8")
			with self.assertRaisesRegex(ValueError, "Stale"):
				package_mobile.stage_aot_headers(root, [generated], root / "stale-json-sdk")
			self.assertFalse((root / "stale-json-sdk").exists())
			json_string.write_bytes(original_json)
			(private / "Core.cpp").write_text("// Changed implementation\n", encoding="utf-8")
			with self.assertRaisesRegex(ValueError, "Stale"):
				package_mobile.stage_aot_headers(root, [generated], root / "stale-sdk")
			self.assertFalse((root / "stale-sdk").exists())


if __name__ == "__main__":
	unittest.main()
