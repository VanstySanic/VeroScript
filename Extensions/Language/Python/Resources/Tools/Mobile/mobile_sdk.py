# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Validate a target CPython SDK without importing or executing target Python."""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import sysconfig

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / "Resources/Tools/Packaging"))
from veroscript_mobile import checked, verify_android_elf as _verify_android_elf  # noqa: E402


def verify_android_elf(data: bytes, abi: str, min_sdk: int = 26) -> dict:
	identity = _verify_android_elf(data, abi, min_sdk)
	offset = struct.unpack_from("<Q", data, 32)[0]
	stride, count = struct.unpack_from("<HH", data, 54)
	segments = [struct.unpack("<IIQQQQQQ", checked(data, offset + index * stride, stride))
				for index in range(count)]
	for kind, _, _, address, _, _, memory_size, _ in segments:
		if kind != 0x6474E552:
			continue
		end = address + memory_size
		page_start, page_end = address & ~16383, (end + 16383) & ~16383
		# Bionic protects every page touched by RELRO. An unaligned end is
		# valid when it covers a LOAD suffix (as in NDK r28's shared STL)
		# and the remaining bytes in that page contain no writable data.
		for load_kind, flags, _, start, _, _, size, _ in segments:
			if load_kind == 1 and flags & 2 and (
				max(page_start, start) < min(address, start + size)
				or max(end, start) < min(page_end, start + size)
			):
				raise ValueError("Android ELF RELRO protection overlaps writable data on a 16 KB page")
	return identity


def json_object(pairs: list[tuple[str, object]]) -> dict:
	result = {}
	for key, value in pairs:
		if key in result:
			raise ValueError(f"Duplicate metadata key: {key}")
		result[key] = value
	return result


def read_json(path: Path) -> dict:
	value = json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=json_object)
	if not isinstance(value, dict):
		raise ValueError(f"Expected a metadata object: {path}")
	return value


def sdk_path(root: Path, value: object) -> Path:
	if not isinstance(value, str) or not value or ";" in value or "\0" in value:
		raise ValueError("SDK paths must be nonempty strings without NUL or semicolons")
	path = Path(value)
	if path.is_absolute() or ".." in path.parts:
		raise ValueError("SDK paths must be relative and remain inside the SDK")
	resolved = (root / path).resolve(strict=True)
	if not resolved.is_relative_to(root) or ";" in str(resolved):
		raise ValueError("SDK path escapes the SDK directory")
	return resolved


def sysconfig_values(path: Path) -> dict:
	tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
	values = []
	for node in tree.body:
		if isinstance(node, ast.Assign) and any(
			isinstance(target, ast.Name) and target.id == "build_time_vars" for target in node.targets
		):
			values.append(ast.literal_eval(node.value))
	if len(values) != 1 or not isinstance(values[0], dict):
		raise ValueError("Target sysconfigdata must contain one literal build_time_vars dictionary")
	return values[0]


def ios_binary(data: bytes, architecture: str, simulator: bool, minimum: str) -> dict:
	header = checked(data, 0, 32)
	cpu = {"arm64": 0x100000C, "x86_64": 0x1000007}[architecture]
	if header[:4] != b"\xcf\xfa\xed\xfe" or struct.unpack_from("<I", header, 4)[0] != cpu:
		raise ValueError("Expected a thin Mach-O binary of the target architecture")
	if struct.unpack_from("<I", header, 12)[0] != 6:
		raise ValueError("Expected a dynamic iOS framework binary")
	count, size = struct.unpack_from("<II", header, 16)
	checked(data, 32, size)
	cursor, end = 32, 32 + size
	identity = None
	install_name = None
	parts = [int(part) for part in minimum.split(".")]
	encoded = (parts[0] << 16) | (parts[1] << 8)
	for _ in range(count):
		command, length = struct.unpack("<II", checked(data, cursor, 8))
		if length < 8 or length % 4 or cursor + length > end:
			raise ValueError("Malformed Mach-O load commands")
		if command == 0x32:
			if length < 24:
				raise ValueError("Malformed LC_BUILD_VERSION")
			platform, version, sdk, tools = struct.unpack("<IIII", checked(data, cursor + 8, 16))
			if length < 24 or tools * 8 > length - 24 or platform != (7 if simulator else 2):
				raise ValueError("Wrong Apple platform or malformed LC_BUILD_VERSION")
			if identity is not None or not 0 < version <= encoded or (sdk and sdk < version):
				raise ValueError("Invalid or incompatible iOS minimum version")
			identity = {"platform": platform, "minimumOS": version}
		if command == 0xD:
			if length < 24 or install_name is not None:
				raise ValueError("Malformed or duplicate Mach-O install name")
			offset = struct.unpack_from("<I", data, cursor + 8)[0]
			if not 24 <= offset < length:
				raise ValueError("Invalid Mach-O install-name offset")
			raw = checked(data, cursor + offset, length - offset)
			if b"\0" not in raw:
				raise ValueError("Unterminated Mach-O install name")
			install_name = raw.split(b"\0", 1)[0].decode("utf-8")
		cursor += length
	if cursor != end or identity is None or not install_name:
		raise ValueError("Missing iOS build identity or install name")
	return {**identity, "installName": install_name}


def source_digest() -> str:
	digest = hashlib.sha256()
	for route in ("Shared/Private", "VeroScript", "VeroScriptApplication", "VeroScriptInterop", "Modules/Execution/VeroScriptBytecodeVm", "Extensions/Language/Cpp/VeroScriptCpp",
				  "Extensions/Language/Python/VeroScriptPython", "Resources/Tools/CMake"):
		source_root = ROOT / route
		sources = []
		for current, directories, filenames in os.walk(source_root):
			directory = Path(current)
			# A legal output can be placed below a source module. Never hash
			# our active staging tree, a generated CMake build, or an earlier
			# mobile package: they can contain SDK/native configuration files
			# which change while the slices are being built. Prune before
			# descending so large build trees do not inflate configure cost.
			# An explicitly selected source root remains source even if an
			# earlier in-source configure left a CMakeCache.txt beside it.
			if directory != source_root and any(
				(directory / marker).is_file() for marker in ("CMakeCache.txt", "python-mobile.json")
			):
				directories.clear()
				continue
			directories[:] = [name for name in directories
							  if name not in {"Tests", "tests", "__pycache__", "build"}
							  and not name.startswith(".veroscript-python-mobile-")]
			for name in filenames:
				path = directory / name
				if name not in {"CMakeCache.txt", "python-mobile.json"} and path.is_file() and path.suffix in {
					".c", ".cpp", ".h", ".inl", ".inc", ".def", ".cmake", ".txt", ".py", ".pyi", ".vs", ".json"
				}:
					sources.append(path)
		for path in sorted(sources):
			digest.update(path.relative_to(ROOT).as_posix().encode())
			digest.update(b"\0" + path.read_bytes() + b"\0")
	digest.update((ROOT / "CMakeLists.txt").read_bytes())
	return digest.hexdigest()


def product_version() -> str:
	text = (ROOT / "VeroScript/Public/VeroScriptVersion.h").read_text(encoding="utf-8-sig")
	match = re.search(r"VEROSCRIPT_VERSION\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", text)
	if match is None:
		raise ValueError("Missing native VeroScript version declaration")
	return ".".join(match.groups())


def _header_walk_error(error: OSError) -> None:
	raise error


def inspect_sdk(descriptor: Path, *, check_host: bool = False, build_nonce: str = "") -> dict:
	if build_nonce and not re.fullmatch(r"[0-9a-f]{32}", build_nonce):
		raise ValueError("Invalid mobile build nonce")
	descriptor = descriptor.resolve(strict=True)
	root = descriptor.parent
	record = read_json(descriptor)
	if type(record.get("schemaVersion")) is not int or record["schemaVersion"] != 1:
		raise ValueError("Unsupported CPython SDK descriptor schema")
	platform = record.get("platform")
	architecture = record.get("architecture")
	variant = record.get("variant", "device")
	minimum = record.get("minimumVersion")
	if not all(isinstance(value, str) for value in (platform, architecture, variant, minimum)):
		raise ValueError("Target platform, architecture, variant and minimumVersion must be strings")
	if platform == "android":
		if architecture not in {"arm64-v8a", "x86_64"} or variant != "device" or minimum != "26":
			raise ValueError("Android requires arm64-v8a/x86_64 and minimumVersion 26")
	elif platform == "ios":
		if architecture not in {"arm64", "x86_64"} or variant not in {"device", "simulator"} or minimum != "15.0":
			raise ValueError("iOS requires arm64/x86_64, device/simulator, and minimumVersion 15.0")
		if variant == "device" and architecture != "arm64":
			raise ValueError("iOS devices require arm64")
	else:
		raise ValueError("Only Android and iOS target SDKs are supported")
	include = sdk_path(root, record.get("includeDirectory"))
	library = sdk_path(root, record.get("library"))
	config = sdk_path(root, record.get("sysconfigData"))
	for name in ("Python.h", "pyconfig.h", "patchlevel.h"):
		if not (include / name).is_file():
			raise ValueError(f"Missing target CPython header: {name}")
	text = (include / "patchlevel.h").read_text(encoding="utf-8")
	version = []
	for name in ("MAJOR", "MINOR", "MICRO"):
		match = re.search(r"^\s*#\s*define\s+PY_" + name + r"_VERSION\s+(\d+)\s*$", text, re.M)
		if match is None:
			raise ValueError("Cannot read target CPython version")
		version.append(int(match[1]))
	if version[:2] not in ([3, 13], [3, 14]):
		raise ValueError("Mobile extensions require CPython 3.13 or 3.14")
	values = sysconfig_values(config)
	if values.get("Py_DEBUG", 0) or values.get("Py_GIL_DISABLED", 0):
		raise ValueError("Debug and free-threaded target CPython builds are unsupported")
	minor = f"3.{version[1]}"
	suffix = values.get("EXT_SUFFIX")
	if values.get("VERSION") != minor or not isinstance(suffix, str) or not re.fullmatch(
		rf"\.cpython-3{version[1]}-[A-Za-z0-9_-]+\.so", suffix
	):
		raise ValueError("Target sysconfigdata version or ABI suffix disagrees with headers")
	if check_host and (sys.implementation.name != "cpython" or list(sys.version_info[:2]) != version[:2]
					   or sysconfig.get_config_var("Py_DEBUG") or sysconfig.get_config_var("Py_GIL_DISABLED")):
		raise ValueError(f"Use a normal host CPython {minor}; host and target major/minor must match")
	binary = library.read_bytes()
	if platform == "android":
		identity = verify_android_elf(binary, architecture, int(minimum))
		if library.name != f"libpython{minor}.so":
			raise ValueError("Android SDK library must use the libpythonX.Y.so name")
	else:
		identity = ios_binary(binary, architecture, variant == "simulator", minimum)
		if identity["installName"] != "@rpath/Python.framework/Python":
			raise ValueError("iOS SDK must use @rpath/Python.framework/Python")
	sdk_digest = hashlib.sha256(binary + config.read_bytes())
	headers = []
	for current, directories, filenames in os.walk(include, onerror=_header_walk_error):
		directory = Path(current)
		for name in directories:
			child = directory / name
			attributes = child.lstat()
			if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & getattr(
				stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400
			):
				raise ValueError(
					f"Target CPython header subdirectories must be real directories; "
					f"materialize symlinks/junctions before building: {child}"
				)
		headers.extend(directory / name for name in filenames if (directory / name).match("*.h"))
	for header in sorted(headers):
		if not header.resolve(strict=True).is_relative_to(root):
			raise ValueError("CPython header escapes the SDK directory")
		sdk_digest.update(header.relative_to(include).as_posix().encode() + b"\0" + header.read_bytes() + b"\0")
	fingerprint = sdk_digest.hexdigest()
	target = f"{platform}-{variant}-{architecture}-cp3{version[1]}"
	build_id = hashlib.sha256((source_digest() + fingerprint + target + minimum + build_nonce).encode()).hexdigest()
	return {"schemaVersion": 1, "platform": platform, "architecture": architecture, "variant": variant,
			"productVersion": product_version(),
			"minimumVersion": minimum, "pythonVersion": ".".join(map(str, version)),
			"extensionSuffix": suffix, "includeDirectory": str(include), "library": str(library),
			"target": target, "buildId": build_id, "sdkFingerprint": fingerprint}


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--sdk", type=Path, required=True)
	parser.add_argument("--check-host", action="store_true")
	parser.add_argument("--build-nonce", default="")
	arguments = parser.parse_args()
	try:
		print(json.dumps(inspect_sdk(arguments.sdk, check_host=arguments.check_host, build_nonce=arguments.build_nonce)))
	except (OSError, ValueError, SyntaxError) as error:
		parser.exit(2, f"error: {error}\n")
