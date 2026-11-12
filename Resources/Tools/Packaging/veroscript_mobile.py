# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Audit mobile platform identity, minimum OS and native library alignment."""
from __future__ import annotations

from pathlib import Path
import copy
import hashlib
import io
import json
import mmap
import os
import shutil
import struct
import subprocess
import tempfile
import zipfile


def checked(data: bytes, offset: int, size: int) -> bytes:
	if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
		raise ValueError("Truncated mobile binary")
	return data[offset:offset + size]


def verify_android_elf(data: bytes, abi: str, min_sdk: int = 26) -> dict:
	header = checked(data, 0, 64)
	machine = {"arm64-v8a": 183, "x86_64": 62}[abi]
	if header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", header, 16) != (3, machine):
		raise ValueError("Expected an Android 64-bit shared library of the requested ABI")
	offset = struct.unpack_from("<Q", header, 32)[0]
	stride, count = struct.unpack_from("<HH", header, 54)
	if stride != 56 or count == 0:
		raise ValueError("Invalid Android ELF program table")
	checked(data, offset, stride * count)
	api = None
	loads = 0
	for index in range(count):
		entry = checked(data, offset + index * stride, stride)
		kind, _flags, file_offset, address, _physical, file_size, memory_size, alignment = struct.unpack("<IIQQQQQQ", entry)
		checked(data, file_offset, file_size)
		if kind == 1:
			loads += 1
			if _flags & 3 == 3:
				raise ValueError("Android ELF LOAD segment cannot be writable and executable")
			if (alignment < 16384 or alignment & (alignment - 1)
					or (address - file_offset) % alignment or file_size > memory_size):
				raise ValueError("Android ELF LOAD segments must support 16 KB pages")
		if kind == 4:
			cursor, end = file_offset, file_offset + file_size
			while cursor < end:
				if end - cursor < 12:
					raise ValueError("Truncated Android ELF note")
				name_size, description_size, note_type = struct.unpack("<III", checked(data, cursor, 12))
				cursor += 12
				name = checked(data, cursor, name_size)
				cursor += (name_size + 3) & ~3
				description = checked(data, cursor, description_size)
				cursor += (description_size + 3) & ~3
				if cursor > end:
					raise ValueError("Android ELF note exceeds its segment")
				if name.rstrip(b"\0") == b"Android" and note_type == 1 and description_size >= 4:
					found = struct.unpack_from("<I", description)[0]
					if api is not None and api != found:
						raise ValueError("Conflicting Android API notes")
					api = found
	if not loads or api is None or api < 21 or api > min_sdk:
		raise ValueError("Missing Android identity note or library requires a newer API than minSdk")
	return {"abi": abi, "minimumApi": api, "loadSegments": loads, "pageAlignment": 16384}


def _android_runtime_fingerprint(data: bytes) -> str:
	"""Compare mapped bytes, including build-id/dynamic exports, across stripping."""
	header = checked(data, 0, 64)
	offset = struct.unpack_from("<Q", header, 32)[0]
	stride, count = struct.unpack_from("<HH", header, 54)
	digest = hashlib.sha256()
	for index in range(count):
		entry = checked(data, offset + index * stride, stride)
		kind, _flags, file_offset, _address, _physical, file_size, _memory_size, _alignment = struct.unpack("<IIQQQQQQ", entry)
		if kind != 1:
			continue
		digest.update(entry)
		payload = bytearray(checked(data, file_offset, file_size))
		# llvm-strip moves/removes the non-loadable section table. Its ELF-header
		# references are bookkeeping, not a difference in the deployed program.
		for start, end in ((40, 48), (58, 64)):
			begin, finish = max(start, file_offset), min(end, file_offset + file_size)
			if begin < finish:
				payload[begin - file_offset:finish - file_offset] = b"\0" * (finish - begin)
		digest.update(payload)
	return digest.hexdigest()


class MobileDebugSymbols(ValueError):
	pass


def _verify_android_no_debug(data: bytes) -> None:
	header = checked(data, 0, 64)
	offset = struct.unpack_from("<Q", header, 40)[0]
	stride, count, strings_index = struct.unpack_from("<HHH", header, 58)
	if not offset and not count:
		return
	if stride != 64 or not count or strings_index >= count:
		raise ValueError("Invalid Android ELF section table")
	checked(data, offset, stride * count)
	if strings_index == 0:
		# SHN_UNDEF denotes an ELF without a section-name string table.
		if any(struct.unpack_from('<I', data, offset + index * stride)[0] for index in range(count)):
			raise ValueError('Android ELF has section names without a string table')
		return
	strings_header = checked(data, offset + stride * strings_index, stride)
	strings_offset, strings_size = struct.unpack_from("<QQ", strings_header, 24)
	strings = checked(data, strings_offset, strings_size)
	for index in range(count):
		name_offset = struct.unpack_from("<I", data, offset + index * stride)[0]
		if name_offset >= len(strings):
			raise ValueError("Invalid Android ELF section name")
		end = strings.find(b"\0", name_offset)
		if end < 0:
			raise ValueError("Invalid Android ELF section name")
		name = strings[name_offset:end]
		if name.startswith((b".debug_", b".zdebug_")) or name in {b".gdb_index", b".gnu_debugdata"}:
			raise MobileDebugSymbols("Android functional libraries must not contain debug symbols")


def verify_mobile_no_debug(data, target: str) -> None:
	"""Inspect every native object; bitcode/unknown formats are never skipped."""
	if data[:8] == b"!<arch>\n":
		from veroscript_mobile_native import ar_objects
		for member in ar_objects(data):
			verify_mobile_no_debug(member, target)
	elif target == 'android' and data[:6] == b"\x7fELF\x02\x01":
		_verify_android_no_debug(data)
	elif target == 'ios' and data[:4] == b"\xcf\xfa\xed\xfe":
		header = checked(data, 0, 32)
		count, size = struct.unpack_from('<II', header, 16)
		checked(data, 32, size)
		cursor, end = 32, 32 + size
		for _ in range(count):
			command, length = struct.unpack('<II', checked(data, cursor, 8))
			if length < 8 or cursor + length > end:
				raise ValueError('Invalid mobile Mach-O command')
			if command == 0x19:
				if length < 72:
					raise ValueError('Truncated mobile Mach-O segment')
				segment = checked(data, cursor + 8, 16).rstrip(b'\0')
				if segment == b'__DWARF':
					raise MobileDebugSymbols('iOS functional libraries must not contain debug symbols')
				if segment == b'__LLVM':
					raise ValueError('Embedded LLVM bitcode is not a mobile native library')
				sections = struct.unpack_from('<I', data, cursor + 64)[0]
				if sections * 80 > length - 72:
					raise ValueError('Invalid mobile Mach-O sections')
				for index in range(sections):
					section = checked(data, cursor + 72 + index * 80, 80)
					name, owner = section[:16].rstrip(b'\0'), section[16:32].rstrip(b'\0')
					if owner == b'__DWARF' or name.startswith((b'__debug_', b'__zdebug_')):
						raise MobileDebugSymbols('iOS functional libraries must not contain debug symbols')
					if owner == b'__LLVM' or name == b'__bitcode':
						raise ValueError('Embedded LLVM bitcode is not a mobile native library')
			cursor += length
		if cursor != end:
			raise ValueError('Invalid mobile Mach-O command inventory')
	elif target == 'ios' and data[:4] in {b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}:
		wide = data[:4] == b'\xca\xfe\xba\xbf'
		count = struct.unpack('>I', checked(data, 4, 4))[0]
		if not count:
			raise ValueError('Empty mobile universal library')
		for index in range(count):
			record = checked(data, 8 + index * (32 if wide else 20), 32 if wide else 20)
			offset, size = struct.unpack_from('>QQ' if wide else '>II', record, 8)
			verify_mobile_no_debug(checked(data, offset, size), target)
	else:
		raise ValueError('Unsupported mobile native object or LLVM bitcode')


def strip_mobile_debug(path: Path, target: str, tool: str | Path | None = None) -> None:
	"""Strip only a staged copy, preserving symbols/relocations needed by consumers."""
	with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
		try:
			verify_mobile_no_debug(data, target)
			return
		except MobileDebugSymbols:
			pass
	if target == 'android':
		if tool is None or not Path(tool).is_file():
			raise ValueError('Android SDK debug stripping requires the recorded target llvm-strip')
		command = [str(tool), '--strip-debug', str(path)]
	elif target == 'ios':
		# Use a same-volume output and replace only after validation, so
		# stripping failures preserve the staged input. cctools may still
		# require system /tmp space internally when processing archives.
		with tempfile.TemporaryDirectory(prefix='.veroscript-strip-', dir=path.parent) as directory:
			output = Path(directory) / path.name
			command = [str(tool)] if tool else ['xcrun', 'strip']
			subprocess.run([*command, '-S', '-o', str(output), str(path)], check=True)
			with output.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
				verify_mobile_no_debug(data, target)
			shutil.copymode(path, output)
			os.replace(output, path)
		return
	else:
		raise ValueError('Unsupported mobile strip target')
	subprocess.run(command, check=True)
	with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
		verify_mobile_no_debug(data, target)


def stage_android_aar(source: Path, destination: Path) -> None:
	"""Keep Prefab and JNI on one stripped runtime, without changing build output."""
	if source.resolve() == destination.resolve() or destination.exists():
		raise ValueError("Choose a new staged Android AAR path")
	destination.parent.mkdir(parents=True, exist_ok=True)
	with tempfile.NamedTemporaryFile(prefix=".veroscript-aar-", suffix=".tmp", dir=destination.parent, delete=False) as stream:
		temporary = Path(stream.name)
	try:
		with zipfile.ZipFile(source) as original, zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as output:
			names = original.namelist()
			if len(names) != len(set(names)):
				raise ValueError("Duplicate AAR entries")
			for name in names:
				if name.startswith(("/", "\\")) or "\\" in name or ".." in name.split("/"):
					raise ValueError("Unsafe AAR entry")
			for item in original.infolist():
				parts = item.filename.split("/")
				if item.filename.startswith("prefab/") and item.filename.endswith(".so"):
					if len(parts) != 6 or parts[:2] != ["prefab", "modules"] or parts[3] != "libs" or not parts[4].startswith("android."):
						raise ValueError("Unexpected Android Prefab library path")
					abi = parts[4][len("android."):]
					if abi not in {"arm64-v8a", "x86_64"}:
						raise ValueError("Unexpected Android Prefab ABI")
					prefab = original.read(item)
					runtime_name = f"jni/{abi}/{parts[5]}"
					if runtime_name not in names:
						raise ValueError("Android Prefab library has no matching JNI runtime")
					runtime = original.read(runtime_name)
					verify_android_elf(prefab, abi)
					verify_android_elf(runtime, abi)
					_verify_android_no_debug(runtime)
					if _android_runtime_fingerprint(prefab) != _android_runtime_fingerprint(runtime):
						raise ValueError("Android Prefab and JNI libraries have different mapped code/data")
					output.writestr(copy.copy(item), runtime)
				else:
					with original.open(item) as input_stream, output.open(copy.copy(item), "w") as output_stream:
						shutil.copyfileobj(input_stream, output_stream, 1024 * 1024)
			output.comment = original.comment
		verify_android_aar(temporary)
		os.replace(temporary, destination)
	finally:
		temporary.unlink(missing_ok=True)


def verify_android_jit_aar(path: Path) -> dict:
	"""Optional backend only; the matching base AAR owns Core and the shared STL."""
	identities = {}
	with zipfile.ZipFile(path) as archive:
		names = archive.namelist()
		if len(names) != len(set(names)):
			raise ValueError("Duplicate JIT AAR entries")
		for name in names:
			if name.startswith(("/", "\\")) or "\\" in name or ".." in name.split("/"):
				raise ValueError("Unsafe JIT AAR entry")
		for name in ("AndroidManifest.xml", "assets/veroscript/licenses/asmjit/LICENSE",
					 "assets/veroscript/licenses/asmjit/LICENSE.md", "assets/veroscript/licenses/asmjit/VERSION.txt"):
			if name not in names or not archive.read(name):
				raise ValueError(f"Missing optional JIT AAR entry: {name}")
		expected = {f"jni/{abi}/libVeroScriptJitAsmJit.so" for abi in ("arm64-v8a", "x86_64")}
		actual = {name for name in names if name.startswith("jni/") and name.endswith(".so")}
		if actual != expected:
			raise ValueError("Optional JIT AAR must contain exactly its two target libraries, without Core or STL duplicates")
		for name in sorted(expected):
			abi = name.split("/")[1]
			data = archive.read(name)
			identities[abi] = verify_android_elf(data, abi)
			_verify_android_no_debug(data)
	return identities


def mobile_catalog_files(repo: Path) -> dict[str, bytes]:
	"""The explicit mobile profile: Core, C++ session, Interop and adapter messages."""
	catalogs = {}
	for module in ("VeroScript", "Extensions/Language/Cpp/VeroScriptCpp", "VeroScriptInterop",
				   "Extensions/Platform/Mobile/VeroScriptMobile"):
		root = repo / module / "Resources/Localization"
		if not (root / "en-US").is_dir():
			raise ValueError(f"Missing mobile localization baseline: {root}")
		for path in sorted(root.rglob("*.json")):
			if path.is_symlink() or any(parent.is_symlink() for parent in path.parents):
				raise ValueError(f"Linked mobile localization source: {path}")
			key = path.relative_to(root).as_posix()
			if key in catalogs:
				raise ValueError(f"Duplicate mobile localization resource: {key}")
			catalogs[key] = path.read_bytes()
	return catalogs


def verify_android_aar(path: Path, stdlib_root: Path | None = None,
					   catalogs: dict[str, bytes] | None = None,
					   interop_header: Path | None = None) -> dict:
	identities = {}
	with zipfile.ZipFile(path) as archive:
		names = archive.namelist()
		if len(names) != len(set(names)):
			raise ValueError("Duplicate AAR entries")
		for required in ("AndroidManifest.xml", "classes.jar", "assets/veroscript/stdlib/core.vs",
						 "assets/veroscript/licenses/LICENSE", "assets/veroscript/licenses/NOTICE",
						 "assets/veroscript/licenses/NOTICE.toolchain", "assets/veroscript/licenses/MINIZ-LICENSE.txt",
						 "assets/veroscript/licenses/JSON-LICENSE.txt", "assets/veroscript/licenses/SHA256-LICENSE.txt"):
			if required not in names or not archive.read(required):
				raise ValueError(f"Missing mobile AAR entry: {required}")
		with zipfile.ZipFile(io.BytesIO(archive.read("classes.jar"))) as classes:
			class_names = classes.namelist()
			if len(class_names) != len(set(class_names)):
				raise ValueError("Duplicate Android Java classes")
			for name in ("VeroScriptEngine.class", "VeroScriptEngine$Result.class", "VeroScriptEngine$AotPrograms.class", "VeroScriptEngine$ExecutionBackend.class"):
				key = "com/vanstysanic/veroscript/android/" + name
				if key not in class_names or not classes.read(key):
					raise ValueError(f"Missing Android JNI Java class: {key}")
		if stdlib_root is not None:
			for source in stdlib_root.rglob("*.vs"):
				key = "assets/veroscript/stdlib/" + source.relative_to(stdlib_root).as_posix()
				if key not in names or archive.read(key) != source.read_bytes():
					raise ValueError(f"Missing or stale Android standard library resource: {key}")
		if catalogs is not None:
			prefix = "assets/veroscript/localization/"
			actual = {name[len(prefix):]: archive.read(name) for name in names
					  if name.startswith(prefix) and not name.endswith("/")}
			if actual != catalogs:
				raise ValueError("Missing, stale or unexpected Android localization resources")
		for abi in ("arm64-v8a", "x86_64"):
			for library in ("libVeroScriptAndroid.so", "libVeroScriptInterop.so", "libc++_shared.so"):
				key = f"jni/{abi}/{library}"
				if key not in names:
					raise ValueError(f"Missing Android ABI dependency: {key}")
		for name in names:
			if name.startswith("jni/") and name.endswith(".so"):
				parts = name.split("/")
				if (len(parts) != 3 or parts[1] not in {"arm64-v8a", "x86_64"}
						or parts[2] not in {"libVeroScriptAndroid.so", "libVeroScriptInterop.so", "libc++_shared.so"}):
					raise ValueError(f"Unexpected mobile runtime library: {name}")
				data = archive.read(name)
				identities[name] = verify_android_elf(data, parts[1])
				_verify_android_no_debug(data)
		# The JNI payload alone does not prove that the advertised NDK SDK is
		# usable. Prefab has its own headers, metadata and native library copies.
		def metadata(name: str) -> dict:
			try:
				value = json.loads(archive.read(name))
			except (KeyError, ValueError, UnicodeError) as error:
				raise ValueError(f"Missing or malformed Android Prefab metadata: {name}") from error
			if not isinstance(value, dict):
				raise ValueError(f"Expected Android Prefab metadata object: {name}")
			return value

		package = metadata("prefab/prefab.json")
		if (type(package.get("schema_version")) is not int or package["schema_version"] not in {1, 2}
				or not isinstance(package.get("name"), str) or not package["name"].strip()
				or not isinstance(package.get("dependencies", []), list)
				or not all(isinstance(name, str) and name for name in package.get("dependencies", []))):
			raise ValueError("Unsupported Android Prefab package metadata")
		root = "prefab/modules/VeroScriptInterop/"
		module = metadata(root + "module.json") if root + "module.json" in names else {}
		android_module = module.get("android", {})
		if not isinstance(android_module, dict):
			raise ValueError("Malformed Android Prefab module metadata")
		library_name = android_module.get("library_name", module.get("library_name", "libVeroScriptInterop"))
		if library_name != "libVeroScriptInterop":
			raise ValueError("Android Prefab module must export VeroScriptInterop")
		header = root + "include/VeroScriptInterop.h"
		if (header not in names or not archive.read(header)
				or (interop_header is not None and archive.read(header) != interop_header.read_bytes())):
			raise ValueError("Missing or stale Android Prefab C ABI header")
		for abi in ("arm64-v8a", "x86_64"):
			directory = root + f"libs/android.{abi}/"
			record = metadata(directory + "abi.json")
			runtime = identities[f"jni/{abi}/libVeroScriptInterop.so"]
			if (record.get("abi") != abi or type(record.get("api")) is not int
					or not runtime["minimumApi"] <= record["api"] <= 26
					or type(record.get("ndk")) is not int or record["ndk"] <= 0
					or record.get("stl") != "c++_shared" or record.get("static", False) is not False):
				raise ValueError(f"Incompatible Android Prefab ABI metadata: {abi}")
			try:
				identity = verify_android_elf(archive.read(directory + library_name + ".so"), abi)
			except (KeyError, ValueError) as error:
				raise ValueError(f"Missing or incompatible Android Prefab library: {abi}") from error
			if identity["minimumApi"] != runtime["minimumApi"]:
				raise ValueError(f"Android Prefab library and JNI runtime require different APIs: {abi}")
		# The functional SDK has one exact stripped runtime for Java and NDK
		# consumers. Debug-bearing Gradle Prefab copies are normalized in staging.
		for name in names:
			if name.startswith("prefab/") and name.endswith(".so"):
				parts = name.split("/")
				if len(parts) != 6 or parts[:2] != ["prefab", "modules"] or parts[3] != "libs" or not parts[4].startswith("android."):
					raise ValueError("Unexpected Android Prefab library path")
				abi = parts[4][len("android."):]
				runtime_name = f"jni/{abi}/{parts[5]}"
				if runtime_name not in identities or archive.read(name) != archive.read(runtime_name):
					raise ValueError("Android Prefab library must match its stripped JNI runtime")
	return identities


def verify_ios_object(data: bytes, architecture: str, simulator: bool, minimum_os: int = 15 << 16) -> dict:
	header = checked(data, 0, 32)
	cpu = {"arm64": 0x100000C, "x64": 0x1000007}[architecture]
	if (header[:4] != b"\xcf\xfa\xed\xfe" or struct.unpack_from("<I", header, 4)[0] != cpu
			or struct.unpack_from("<I", header, 12)[0] != 1):
		raise ValueError("Expected a 64-bit Mach-O object of the requested architecture")
	count, size = struct.unpack_from("<II", header, 16)
	checked(data, 32, size)
	cursor, end = 32, 32 + size
	platform = 7 if simulator else 2
	identity = None
	for _ in range(count):
		if end - cursor < 8:
			raise ValueError("Truncated Mach-O load command")
		command, command_size = struct.unpack("<II", checked(data, cursor, 8))
		if command_size < 8 or command_size % 4 or cursor + command_size > end:
			raise ValueError("Invalid Mach-O load command")
		if command == 0x32:
			if command_size < 24:
				raise ValueError("Truncated LC_BUILD_VERSION")
			target, version, sdk, tools = struct.unpack("<IIII", checked(data, cursor + 8, 16))
			# Object files may leave SDK unspecified (0); platform and minOS
			# still identify their target, independently of that optional field.
			if (target != platform or not version or version > minimum_os
					or (sdk != 0 and sdk < version) or tools * 8 > command_size - 24):
				raise ValueError("Wrong Apple platform or unsupported minimum OS")
			if identity is not None:
				raise ValueError("Duplicate Apple build identity")
			identity = {"architecture": architecture, "platform": "ios-simulator" if simulator else "ios",
						"minimumOS": version, "sdk": sdk}
		cursor += command_size
	if cursor != end or identity is None:
		raise ValueError("Missing or malformed Apple build identity")
	return identity


def verify_ios_archive(path: Path, architecture: str, simulator: bool) -> int:
	data = path.read_bytes()
	verify_mobile_no_debug(data, 'ios')
	if not data.startswith(b"!<arch>\n"):
		raise ValueError("Expected a thin iOS static archive before XCFramework assembly")
	cursor, objects = 8, 0
	while cursor < len(data):
		header = checked(data, cursor, 60)
		if header[58:60] != b"`\n":
			raise ValueError("Invalid static archive member")
		size = int(header[48:58].strip())
		member = checked(data, cursor + 60, size)
		name = header[:16].decode("ascii").strip()
		if name.startswith("#1/"):
			name_size = int(name[3:])
			name = checked(member, 0, name_size).rstrip(b"\0").decode("utf-8")
			member = member[name_size:]
		if name not in {"/", "//", "/SYM64/"} and not name.startswith("__.SYMDEF"):
			verify_ios_object(member, architecture, simulator)
			objects += 1
		cursor += 60 + size + (size & 1)
	if cursor != len(data) or not objects:
		raise ValueError("Empty or malformed iOS static archive")
	return objects
