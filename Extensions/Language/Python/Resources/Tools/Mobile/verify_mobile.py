# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Audit mobile Python deployment payloads and require matching device evidence."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import plistlib
import re
import stat
import struct
import sys

from mobile_sdk import ROOT, checked, ios_binary, product_version, read_json, verify_android_elf
from veroscript_mobile import verify_mobile_no_debug


def directory_entries(directory: Path, expected: set[str]) -> None:
	if not directory.is_dir() or {path.name for path in directory.iterdir()} != expected:
		raise ValueError(f"Unexpected mobile package layout: {directory}")


def python_resources(*, application: bool = False) -> dict[str, bytes]:
	"""The complete Python resource profile from the matching source release."""
	files = {}
	if application:
		for path in ("miniz/LICENSE", "json/LICENSE.MIT", "sha256/LICENSE"):
			files["licenses/application/" + path] = (ROOT / "VeroScriptApplication/ThirdParty" / path).read_bytes()
	binding = ROOT / "Extensions/Language/Python/VeroScriptPython/python/veroscript"
	for name in ("__init__.py", "_mobile.py", "_veroscript.pyi", "py.typed"):
		files[name] = (binding / name).read_bytes()
	stdlib = ROOT / "VeroScript/Resources/Scripts/stdlib"
	for path in sorted(stdlib.rglob("*.vs")):
		files["stdlib/" + path.relative_to(stdlib).as_posix()] = path.read_bytes()
	roots = ["VeroScript", "Extensions/Language/Cpp/VeroScriptCpp", "VeroScriptInterop", "Extensions/Language/Python"]
	roots += ["Extensions/Language/Python/VeroScriptPython" + name for name in ("Frontend", "BindGen", "Transpiler", "Aot")]
	for route in roots:
		catalog = ROOT / route / "Resources/Localization"
		if not (catalog / "en-US").is_dir():
			raise ValueError(f"Missing Python localization source: {catalog}")
		for path in sorted(catalog.rglob("*.json")):
			key = "localization/" + path.relative_to(catalog).as_posix()
			if key in files:
				raise ValueError(f"Duplicate Python localization source: {key}")
			files[key] = path.read_bytes()
	if "stdlib/core.vs" not in files:
		raise ValueError("Missing Python standard-library source")
	return files


def elf_symbols_and_dependencies(data: bytes) -> tuple[set[str], set[str]]:
	section_offset = struct.unpack_from("<Q", checked(data, 0, 64), 40)[0]
	stride, count = struct.unpack_from("<HH", data, 58)
	if stride != 64 or not count:
		raise ValueError("Missing ELF section table")
	sections = [struct.unpack("<IIQQQQIIQQ", checked(data, section_offset + i * stride, stride)) for i in range(count)]

	def text(table: bytes, offset: int) -> str:
		if not 0 <= offset < len(table) or b"\0" not in table[offset:]:
			raise ValueError("Invalid ELF string offset")
		return table[offset:].split(b"\0", 1)[0].decode("utf-8")

	exports, dependencies = set(), set()
	for section in sections:
		kind, offset, size, link, entry_size = section[1], section[4], section[5], section[6], section[9]
		if kind not in {6, 11}:
			continue
		if not 0 <= link < len(sections):
			raise ValueError("Invalid ELF linked string section")
		strings = sections[link]
		if strings[1] != 3:
			raise ValueError("ELF dynamic symbols must reference a string table")
		table = checked(data, strings[4], strings[5])
		expected = 16 if kind == 6 else 24
		if entry_size != expected or size % expected:
			raise ValueError("Invalid ELF dynamic/symbol entry size")
		for cursor in range(offset, offset + size, expected):
			raw = checked(data, cursor, expected)
			if kind == 6:
				tag, value = struct.unpack("<qQ", raw)
				if tag == 1:
					dependencies.add(text(table, value))
			else:
				name, info, other, index, value, length = struct.unpack("<IBBHQQ", raw)
				if index and info >> 4 in {1, 2} and info & 0xF in {2, 10} and other & 3 in {0, 3}:
					exports.add(text(table, name))
	return exports, dependencies


def macho_exports(data: bytes) -> set[str]:
	count, size = struct.unpack_from("<II", checked(data, 0, 32), 16)
	cursor = 32
	exports = set()
	for _ in range(count):
		command, length = struct.unpack("<II", checked(data, cursor, 8))
		if length < 8 or cursor + length > 32 + size:
			raise ValueError("Invalid Mach-O load command")
		if command == 2:
			if length != 24:
				raise ValueError("Invalid Mach-O symbol-table command")
			offset, number, string_offset, string_size = struct.unpack("<IIII", checked(data, cursor + 8, 16))
			strings = checked(data, string_offset, string_size)
			for index in range(number):
				name, kind, section, description, value = struct.unpack("<IBBHQ", checked(data, offset + 16 * index, 16))
				if kind & 0xE0 == 0 and not kind & 0x10 and kind & 1 and kind & 0xE == 0xE and section:
					if name >= len(strings) or b"\0" not in strings[name:]:
						raise ValueError("Invalid Mach-O symbol name")
					exports.add(strings[name:].split(b"\0", 1)[0].decode("utf-8"))
		cursor += length
	return exports


def macho_dependencies(data: bytes) -> set[str]:
	count, size = struct.unpack_from("<II", checked(data, 0, 32), 16)
	cursor = 32
	dependencies = set()
	for _ in range(count):
		command, length = struct.unpack("<II", checked(data, cursor, 8))
		if length < 8 or cursor + length > 32 + size:
			raise ValueError("Invalid Mach-O load command")
		if command in {0xC, 0x80000018, 0x8000001F, 0x80000023}:
			if length < 24:
				raise ValueError("Invalid Mach-O dependency command")
			offset = struct.unpack_from("<I", data, cursor + 8)[0]
			if not 24 <= offset < length:
				raise ValueError("Invalid Mach-O dependency offset")
			raw = checked(data, cursor + offset, length - offset)
			if b"\0" not in raw:
				raise ValueError("Unterminated Mach-O dependency")
			dependencies.add(raw.split(b"\0", 1)[0].decode("utf-8"))
		cursor += length
	return dependencies


def validate_record(record: dict) -> None:
	platform, architecture, variant = (record.get(key) for key in ("platform", "architecture", "variant"))
	if not all(isinstance(value, str) for value in (platform, architecture, variant)):
		raise ValueError("Invalid mobile target field types")
	expected = ({("arm64-v8a", "device"), ("x86_64", "device")} if platform == "android"
				else {("arm64", "device"), ("arm64", "simulator"), ("x86_64", "simulator")})
	if platform not in {"android", "ios"} or (architecture, variant) not in expected:
		raise ValueError("Invalid mobile platform/architecture/variant")
	if record.get("minimumVersion") != ("26" if platform == "android" else "15.0"):
		raise ValueError("Invalid mobile minimum version")
	version = record.get("pythonVersion")
	if not isinstance(version, str) or not re.fullmatch(r"3\.(?:13|14)\.\d+", version):
		raise ValueError("Invalid mobile CPython version")
	product_version = record.get("productVersion")
	if not isinstance(product_version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", product_version):
		raise ValueError("Invalid VeroScript product version")
	minor = version.split(".")[1]
	if record.get("target") != f"{platform}-{variant}-{architecture}-cp3{minor}":
		raise ValueError("Target identity disagrees with slice metadata")
	suffix = record.get("extensionSuffix")
	if not isinstance(suffix, str) or not re.fullmatch(rf"\.cpython-3{minor}-[A-Za-z0-9_-]+\.so", suffix):
		raise ValueError("Invalid target CPython ABI suffix")
	for key in ("buildId", "sdkFingerprint"):
		if not isinstance(record.get(key), str) or not re.fullmatch(r"[0-9a-f]{64}", record[key]):
			raise ValueError(f"Invalid mobile {key}")


def audit_slice(stage: Path, record: dict) -> None:
	validate_record(record)
	if record["productVersion"] != product_version():
		raise ValueError("Audit mobile resources using the matching VeroScript source release")
	package = stage / "python/veroscript"
	expected = python_resources(application=record["platform"] == "android")
	actual = {path.relative_to(package).as_posix(): path.read_bytes() for path in package.rglob("*") if path.is_file()}
	if record["platform"] == "ios":
		actual.pop("_veroscript.fwork", None)
	if actual != expected:
		missing = sorted(set(expected) - set(actual))
		extra = sorted(set(actual) - set(expected))
		stale = sorted(name for name in set(expected) & set(actual) if expected[name] != actual[name])
		raise ValueError(f"Incomplete or stale mobile Python resources: missing={missing}, extra={extra}, changed={stale}")
	minor = ".".join(record["pythonVersion"].split(".")[:2])
	if record["platform"] == "android":
		directory_entries(stage, {"python", "jniLibs"})
		directory_entries(stage / "python", {"veroscript"})
		directory_entries(stage / "jniLibs", {record["architecture"]})
		libraries = stage / "jniLibs" / record["architecture"]
		binary = (libraries / "libveroscript_python.so").read_bytes()
		verify_mobile_no_debug(binary, 'android')
		verify_android_elf(binary, record["architecture"], 26)
		verify_android_elf((libraries / "libc++_shared.so").read_bytes(), record["architecture"], 26)
		exports, dependencies = elf_symbols_and_dependencies(binary)
		allowed = {f"libpython{minor}.so", "libc++_shared.so", "libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so"}
		if "PyInit__veroscript" not in exports or f"libpython{minor}.so" not in dependencies or dependencies - allowed:
			raise ValueError("Missing Python initializer/runtime or unexpected Android native dependencies")
		if set(path.name for path in libraries.iterdir()) != {"libveroscript_python.so", "libc++_shared.so"}:
			raise ValueError("Unexpected native library in Android deployment slice")
	else:
		directory_entries(stage, {"python", "Frameworks"})
		directory_entries(stage / "python", {"veroscript"})
		directory_entries(stage / "Frameworks", {"veroscript._veroscript.framework"})
		framework = stage / "Frameworks/veroscript._veroscript.framework"
		binary = (framework / "veroscript._veroscript").read_bytes()
		verify_mobile_no_debug(binary, 'ios')
		identity = ios_binary(binary, record["architecture"], record["variant"] == "simulator", "15.0")
		if identity["installName"] != "@rpath/veroscript._veroscript.framework/veroscript._veroscript" or "_PyInit__veroscript" not in macho_exports(binary):
			raise ValueError("Invalid Python framework identity or missing initializer")
		dependencies = macho_dependencies(binary)
		if "@rpath/Python.framework/Python" not in dependencies or any(
			name != "@rpath/Python.framework/Python" and not name.startswith(("/usr/lib/", "/System/Library/Frameworks/"))
			for name in dependencies
		):
			raise ValueError("Missing CPython framework or unexpected iOS native dependency")
		with (framework / "Info.plist").open("rb") as stream:
			info = plistlib.load(stream)
		if not isinstance(info, dict) or info.get("CFBundleExecutable") != "veroscript._veroscript" or info.get("CFBundlePackageType") != "FMWK" or info.get("CFBundleShortVersionString") != record["productVersion"] or info.get("CFBundleVersion") != record["productVersion"]:
			raise ValueError("Invalid Python framework metadata")
		marker = "_veroscript.fwork"
		if (package / marker).read_text().strip() != "Frameworks/veroscript._veroscript.framework/veroscript._veroscript":
			raise ValueError("Invalid iOS framework marker")
		if (framework / "veroscript._veroscript.origin").read_text().strip() != "python/veroscript/" + marker:
			raise ValueError("Invalid iOS framework origin marker")
		if any(package.rglob("*.so")) or any(package.rglob("*.dylib")):
			raise ValueError("iOS Python resources must not contain executable binaries")
	if record["buildId"].encode() not in binary:
		raise ValueError("Native binary does not carry the declared mobile build identity")


def macho_members(data: bytes) -> dict[str, bytes]:
	cpu_names = {0x100000C: "arm64", 0x1000007: "x86_64"}
	if data[:4] == b"\xcf\xfa\xed\xfe":
		cpu = struct.unpack_from("<I", checked(data, 0, 32), 4)[0]
		if cpu not in cpu_names:
			raise ValueError("Unsupported XCFramework CPU")
		return {cpu_names[cpu]: data}
	if data[:4] not in {b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"}:
		raise ValueError("Invalid XCFramework Mach-O container")
	count = struct.unpack_from(">I", checked(data, 0, 8), 4)[0]
	wide = data[:4] == b"\xca\xfe\xba\xbf"
	stride = 32 if wide else 20
	if count != 2:
		raise ValueError("Simulator framework must contain both supported CPUs")
	members, ranges = {}, []
	for index in range(count):
		fields = struct.unpack(">IIQQII" if wide else ">IIIII", checked(data, 8 + index * stride, stride))
		cpu, subtype, offset, size, alignment = fields[:5]
		if cpu not in cpu_names or cpu_names[cpu] in members or alignment > 31 or offset % (1 << alignment):
			raise ValueError("Invalid or duplicate fat Mach-O member")
		if offset < 8 + count * stride or not size or any(offset < end and offset + size > start for start, end in ranges):
			raise ValueError("Overlapping fat Mach-O members")
		member = checked(data, offset, size)
		if member[:4] != b"\xcf\xfa\xed\xfe" or struct.unpack_from("<I", checked(member, 0, 32), 4)[0] != cpu:
			raise ValueError("Fat Mach-O CPU does not match its member")
		members[cpu_names[cpu]] = member
		ranges.append((offset, offset + size))
	return members


def audit_xcframework(root: Path, records: list[dict]) -> None:
	xcframework = root / "VeroScriptPython.xcframework"
	with (xcframework / "Info.plist").open("rb") as stream:
		info = plistlib.load(stream)
	if not isinstance(info, dict):
		raise ValueError("Invalid Python XCFramework metadata")
	libraries = info.get("AvailableLibraries")
	if info.get("XCFrameworkFormatVersion") != "1.0" or not isinstance(libraries, list) or len(libraries) != 2:
		raise ValueError("Invalid Python XCFramework metadata")
	variants, identifiers = set(), set()
	for library in libraries:
		if not isinstance(library, dict) or library.get("SupportedPlatform") != "ios":
			raise ValueError("Unexpected XCFramework platform")
		variant = "simulator" if library.get("SupportedPlatformVariant") == "simulator" else "device"
		if library.get("SupportedPlatformVariant") not in {None, "simulator"} or variant in variants:
			raise ValueError("Invalid XCFramework variant")
		identifier = library.get("LibraryIdentifier")
		if not isinstance(identifier, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", identifier) or identifier in identifiers:
			raise ValueError("Invalid XCFramework library identifier")
		identifiers.add(identifier)
		variants.add(variant)
		if library.get("LibraryPath") != "veroscript._veroscript.framework":
			raise ValueError("Invalid XCFramework library path")
		framework = xcframework / identifier / library["LibraryPath"]
		directory_entries(xcframework / identifier, {library["LibraryPath"]})
		members = macho_members((framework / "veroscript._veroscript").read_bytes())
		expected = {r["architecture"]: (root / "slices" / r["target"] / "Frameworks/veroscript._veroscript.framework/veroscript._veroscript").read_bytes()
					for r in records if r["variant"] == variant}
		architectures = library.get("SupportedArchitectures")
		if members != expected or not isinstance(architectures, list) or set(architectures) != set(expected) or len(architectures) != len(expected):
			raise ValueError("XCFramework binaries disagree with the audited slices")
		if (framework / "veroscript._veroscript.origin").read_text(encoding="utf-8").strip() != "python/veroscript/_veroscript.fwork":
			raise ValueError("Invalid XCFramework origin marker")
		with (framework / "Info.plist").open("rb") as stream:
			framework_info = plistlib.load(stream)
		if not isinstance(framework_info, dict) or framework_info.get("CFBundleExecutable") != "veroscript._veroscript" or framework_info.get("CFBundlePackageType") != "FMWK" or framework_info.get("CFBundleVersion") != records[0]["productVersion"] or framework_info.get("CFBundleShortVersionString") != records[0]["productVersion"]:
			raise ValueError("Invalid XCFramework framework metadata")
	directory_entries(xcframework, identifiers | {"Info.plist"})


def audit_native_inventory(root: Path, records: list[dict]) -> None:
	"""Inventory hashes alone cannot authorize extra executable payloads."""
	expected = set()
	for record in records:
		stage = root / "slices" / record["target"]
		if record["platform"] == "android":
			libraries = stage / "jniLibs" / record["architecture"]
			expected.update({libraries / "libveroscript_python.so", libraries / "libc++_shared.so"})
		else:
			expected.add(stage / "Frameworks/veroscript._veroscript.framework/veroscript._veroscript")
	if records[0]["platform"] == "ios":
		info = plistlib.loads((root / "VeroScriptPython.xcframework/Info.plist").read_bytes())
		for library in info["AvailableLibraries"]:
			expected.add(root / "VeroScriptPython.xcframework" / library["LibraryIdentifier"] /
						 library["LibraryPath"] / "veroscript._veroscript")
	magic = {b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe",
			 b"\xfe\xed\xfa\xce", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
			 b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
	for path in root.rglob("*"):
		if path.is_file() and path not in expected:
			with path.open("rb") as stream:
				header = stream.read(8)
			if (header[:4] in magic or header[:2] == b"MZ" or header == b"!<arch>\n"
					or path.suffix.lower() in {".so", ".dylib", ".dll", ".pyd", ".exe", ".a", ".lib"}):
				raise ValueError(f"Unexpected native payload: {path.relative_to(root)}")


def verify_package(root: Path, evidence: list[Path] | None = None, *, require_devices: bool = False) -> dict:
	root = root.resolve(strict=True)
	metadata = read_json(root / "python-mobile.json")
	if type(metadata.get("schemaVersion")) is not int or metadata.get("schemaVersion") != 1 or metadata.get("product") != "VeroScript Python Mobile" or metadata.get("includesCPythonRuntime") is not False or metadata.get("nativeJit") is not False:
		raise ValueError("Invalid mobile Python package contract")
	files = metadata.get("files")
	records = metadata.get("slices")
	if not isinstance(files, dict) or not files or not isinstance(records, list) or not records:
		raise ValueError("Incomplete mobile Python package inventory")
	required = {"LICENSE", "THIRD-PARTY-NOTICES.md", "device_smoke.py"}
	if not required.issubset(files):
		raise ValueError("Missing mobile licensing or device acceptance entry point")
	sys.path.insert(0, str(ROOT / "Resources/Tools/Packaging"))
	from veroscript_products import verify_functional_payload
	verify_functional_payload(files)
	actual = set()
	for path in root.rglob("*"):
		if path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
			raise ValueError("Linked files are forbidden in a published mobile Python package")
		if path.is_file():
			actual.add(path.relative_to(root).as_posix())
	if actual != set(files) | {"python-mobile.json"}:
		raise ValueError("Mobile Python package contains missing or unexpected files")
	for name, digest in files.items():
		relative = PurePosixPath(name)
		if relative.is_absolute() or ".." in relative.parts or "\\" in name or ":" in name or relative.as_posix() != name or not re.fullmatch(r"[0-9a-f]{64}", str(digest)):
			raise ValueError("Invalid package inventory path or digest")
		if hashlib.sha256(root.joinpath(*relative.parts).read_bytes()).hexdigest() != digest:
			raise ValueError(f"Mobile Python package hash mismatch: {name}")
	targets = set()
	for record in records:
		if not isinstance(record, dict) or not re.fullmatch(r"[0-9a-f]{64}", str(record.get("buildId", ""))):
			raise ValueError("Invalid mobile slice metadata")
		target = record.get("target")
		if not isinstance(target, str) or not re.fullmatch(r"(?:android|ios)-(?:device|simulator)-(?:arm64-v8a|arm64|x86_64)-cp31[34]", target) or target in targets:
			raise ValueError("Invalid or duplicate mobile target")
		targets.add(target)
		audit_slice(root / "slices" / target, record)
	platform = records[0]["platform"]
	if any(r["productVersion"] != metadata.get("version") for r in records):
		raise ValueError("Inconsistent VeroScript mobile product version")
	expected = ({("arm64-v8a", "device"), ("x86_64", "device")} if platform == "android"
				else {("arm64", "device"), ("arm64", "simulator"), ("x86_64", "simulator")})
	if len(records) != len(expected) or {(r["architecture"], r["variant"]) for r in records} != expected or len({r["pythonVersion"] for r in records}) != 1 or any(r["platform"] != platform for r in records):
		raise ValueError("Incomplete mobile Python architecture/ABI matrix")
	if platform == "ios":
		audit_xcframework(root, records)
	elif "ANDROID-NDK-NOTICE.txt" not in files:
		raise ValueError("Missing redistributed NDK runtime license notice")
	directory_entries(root / "slices", targets)
	directory_entries(root, required | {"python-mobile.json", "slices"} |
					  ({"VeroScriptPython.xcframework"} if platform == "ios" else {"ANDROID-NDK-NOTICE.txt"}))
	audit_native_inventory(root, records)
	reports = [read_json(path) for path in evidence or []]
	for record in records:
		matches = [report for report in reports if type(report.get("schemaVersion")) is int and report.get("schemaVersion") == 1
				   and report.get("buildId") == record["buildId"] and report.get("passed") is True
				   and report.get("compilerNestingChecked") is True
				   and report.get("platform") == record["platform"] and report.get("architecture") == record["architecture"]
				   and report.get("variant") == record["variant"] and report.get("pythonVersion") == record["pythonVersion"]
				   and report.get("productVersion") == record["productVersion"]
				   and type(report.get("testCount")) is int and report.get("testCount") == 4
				   and type(report.get("abiVersion")) is int and report.get("abiVersion") == 0x101]
		if require_devices and not matches:
			raise ValueError(f"Missing matching device test evidence: {record['target']}")
		if require_devices and platform == "android" and record["architecture"] == "arm64-v8a" and not {4096, 16384}.issubset({r.get("pageSize") for r in matches}):
			raise ValueError("Android arm64 release requires successful 4 KB and 16 KB page device runs")
	return metadata


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("directory", type=Path)
	parser.add_argument("--evidence", type=Path, action="append", default=[])
	parser.add_argument("--require-device-tests", action="store_true")
	args = parser.parse_args()
	try:
		verify_package(args.directory, args.evidence, require_devices=args.require_device_tests)
		print("Mobile Python package audit passed" + (" with matching device evidence" if args.require_device_tests else "; device tests remain required"))
	except (OSError, ValueError, KeyError, TypeError) as error:
		parser.exit(2, f"error: {error}\n")
