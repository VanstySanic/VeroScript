#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Prepare and audit C# mobile native assets from matching Android and iOS SDKs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import shutil
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

REPO = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(REPO / "Resources/Tools/Packaging"))
from veroscript_mobile import checked, mobile_catalog_files, verify_android_aar, verify_android_elf, verify_ios_archive
from veroscript_products import verify_functional_payload


def digest(data: bytes) -> str:
	return hashlib.sha256(data).hexdigest()


def child(root: Path, route: str) -> Path:
	if (not route or "\\" in route or ":" in route or "\0" in route
			or PurePosixPath(route).is_absolute() or any(part in {"", ".", ".."} for part in route.split("/"))):
		raise ValueError(f"Invalid SDK resource path: {route}")
	result = root.joinpath(*route.split("/"))
	if not result.resolve().is_relative_to(root.resolve()) or result.is_symlink():
		raise ValueError(f"SDK path escapes its root: {route}")
	return result


def resource_route(route: str) -> None:
	if (re.fullmatch(r"[A-Za-z0-9_./-]+", route) is None
			or any(part in {"", ".", ".."} or part.endswith(".") for part in route.split("/"))):
		raise ValueError(f"Non-portable mobile resource path: {route}")


def tree(root: Path) -> dict[str, bytes]:
	result = {}
	for path in sorted(root.rglob("*")):
		if path.is_symlink():
			raise ValueError(f"Linked SDK payload: {path}")
		if path.is_file():
			result[path.relative_to(root).as_posix()] = path.read_bytes()
	return result


def verify_native_inventory(entries: dict[str, bytes], allowed: set[str]) -> None:
	magic = {b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe",
			 b"\xfe\xed\xfa\xce", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
			 b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
	for route, data in entries.items():
		if route not in allowed and (data[:4] in magic or data[:2] == b"MZ" or data[:8] == b"!<arch>\n"
									or PurePosixPath(route).suffix.lower() in
									{".so", ".dylib", ".dll", ".pyd", ".exe", ".a", ".lib"}):
			raise ValueError(f"Unexpected native payload: {route}")


def archive_slices(data: bytes) -> dict[str, bytes]:
	if data.startswith(b"!<arch>\n"):
		return {"thin": data}
	magic = checked(data, 0, 8)[:4]
	if magic not in {b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"}:
		raise ValueError("Expected a static iOS archive or universal static archive")
	count = struct.unpack_from(">I", data, 4)[0]
	stride = 32 if magic[-1] == 0xBF else 20
	if count != 2:
		raise ValueError("Universal simulator archive must contain arm64 and x86_64")
	checked(data, 8, count * stride)
	result, regions = {}, []
	for index in range(count):
		entry = checked(data, 8 + index * stride, stride)
		cpu = struct.unpack_from(">I", entry)[0]
		architecture = {0x100000C: "arm64", 0x1000007: "x64"}.get(cpu)
		offset, size = struct.unpack_from(">QQ" if stride == 32 else ">II", entry, 8)
		alignment = struct.unpack_from(">I", entry, 24 if stride == 32 else 16)[0]
		if alignment > (63 if stride == 32 else 31) or offset % (1 << alignment):
			raise ValueError("Invalid universal static archive alignment")
		if architecture is None or architecture in result or offset < 8 + count * stride or size == 0:
			raise ValueError("Invalid universal static archive slice")
		if any(offset < end and start < offset + size for start, end in regions):
			raise ValueError("Overlapping universal static archive slices")
		regions.append((offset, offset + size))
		result[architecture] = checked(data, offset, size)
	return result


def verify_framework(root: Path) -> dict:
	# Parse Apple metadata and every archive object, independently of the host OS.
	entries = tree(root)
	info = plistlib.loads((root / "Info.plist").read_bytes())
	seen, records = set(), {}
	native_paths = set()
	expected_header = (REPO / "VeroScriptInterop/Public/VeroScriptInterop.h").read_bytes()
	for library in info.get("AvailableLibraries", []):
		variant = library.get("SupportedPlatformVariant", "device")
		if library.get("SupportedPlatform") != "ios" or variant not in {"device", "simulator"}:
			raise ValueError("XCFramework contains a non-iOS slice")
		directory = child(root, library["LibraryIdentifier"])
		binary = child(directory, library["LibraryPath"])
		if binary.suffix != ".a":
			raise ValueError("C# iOS requires static XCFramework libraries")
		native_paths.add(binary.relative_to(root).as_posix())
		headers = child(directory, library["HeadersPath"])
		if (headers / "VeroScriptInterop.h").read_bytes() != expected_header:
			raise ValueError("iOS Interop header does not match this source revision")
		architectures = library["SupportedArchitectures"]
		if not architectures or len(architectures) != len(set(architectures)):
			raise ValueError("Invalid XCFramework architectures")
		slices = archive_slices(binary.read_bytes())
		expected = {"arm64" if arch == "arm64" else "x64" if arch == "x86_64" else arch for arch in architectures}
		if "thin" in slices:
			if len(expected) != 1:
				raise ValueError("Thin archive does not satisfy XCFramework architecture metadata")
			slices = {next(iter(expected)): slices["thin"]}
		if set(slices) != expected:
			raise ValueError("Universal archive does not satisfy XCFramework architecture metadata")
		with tempfile.TemporaryDirectory(prefix="veroscript-csharp-archive-") as temporary:
			for architecture, data in slices.items():
				identity = (variant, architecture)
				if identity in seen or identity not in {("device", "arm64"), ("simulator", "arm64"), ("simulator", "x64")}:
					raise ValueError("Duplicate or unsupported iOS slice")
				seen.add(identity)
				path = Path(temporary) / "slice.a"
				path.write_bytes(data)
				records[variant + "-" + architecture] = verify_ios_archive(path, architecture, variant == "simulator")
	if seen != {("device", "arm64"), ("simulator", "arm64"), ("simulator", "x64")}:
		raise ValueError("Missing iOS device or simulator slice")
	verify_native_inventory(entries, native_paths)
	return records


def resources() -> dict[str, bytes]:
	result = {"stdlib/" + path.relative_to(REPO / "VeroScript/Resources/Scripts/stdlib").as_posix(): path.read_bytes()
			  for path in sorted((REPO / "VeroScript/Resources/Scripts/stdlib").rglob("*.vs"))}
	result.update({"localization/" + route: data for route, data in mobile_catalog_files(REPO).items()})
	for route in result:
		resource_route(route)
	return result


def static_symbols() -> bytes:
	interop_root = REPO / "Extensions/Language/CSharp/VeroScriptCSharp/Interop"
	names = set()
	# NativeMethods is split across feature files; file names do not identify
	# the whole import surface. Match declarations rather than call sites.
	for path in sorted(interop_root.glob("*.cs")):
		source = path.read_text(encoding="utf-8-sig")
		if re.search(r"\bpartial\s+class\s+NativeMethods\b", source):
			names.update(re.findall(r"\bextern\s+\w+\s+(vs_interop_[A-Za-z0-9_]+)\s*\(", source))
	exports = set(re.findall(r"^\s*(vs_interop_[A-Za-z0-9_]+)\s*$",
							(REPO / "VeroScriptInterop/Private/VeroScriptInterop.def")
							.read_text(encoding="utf-8-sig"), re.MULTILINE))
	if len(names) < 150 or names != exports:
		raise ValueError("Managed iOS imports do not match the native ABI manifest")
	root = ET.Element("Project")
	items = ET.SubElement(root, "ItemGroup")
	for name in sorted(names):
		ET.SubElement(items, "ReferenceNativeSymbol", Include=name, SymbolType="Function")
	ET.indent(root, space="  ")
	return b"<!-- SPDX-FileCopyrightText: VanstySanic -->\n<!-- SPDX-License-Identifier: MIT -->\n\n" + ET.tostring(root, encoding="utf-8") + b"\n"


def verify(root: Path) -> dict:
	entries = tree(root)
	verify_functional_payload(entries)
	metadata = json.loads(entries["mobile-sdk.json"])
	if (metadata.get("schemaVersion") != 1 or metadata.get("abiVersion") != "0x00000101"
			or metadata.get("nativeJit") is not False):
		raise ValueError("Invalid C# mobile SDK contract")
	actual = {route: digest(data) for route, data in entries.items() if route != "mobile-sdk.json"}
	if actual != metadata.get("files"):
		raise ValueError("Missing, changed or unexpected C# mobile SDK payload")
	for abi in ("arm64-v8a", "x86_64"):
		for name in ("libVeroScriptInterop.so", "libc++_shared.so"):
			verify_android_elf(entries[f"android/{abi}/{name}"], abi)
	verify_framework(root / "ios/VeroScriptIOS.xcframework")
	# The XCFramework was audited above; outside it only the four Android
	# runtime libraries are native payloads advertised by this SDK.
	verify_native_inventory(
		{route: data for route, data in entries.items() if not route.startswith("ios/VeroScriptIOS.xcframework/")},
		{f"android/{abi}/{name}" for abi in ("arm64-v8a", "x86_64")
		 for name in ("libVeroScriptInterop.so", "libc++_shared.so")})
	if entries.get("ios/VeroScriptInterop.symbols.targets") != static_symbols():
		raise ValueError("Missing or stale iOS static native symbol roots")
	expected = resources()
	manifest = json.loads(entries["assets/resources.json"])
	if manifest != {"schemaVersion": 1, "files": {route: digest(data) for route, data in expected.items()}}:
		raise ValueError("Missing or stale mobile resource manifest")
	for route, data in expected.items():
		if entries.get("assets/" + route) != data:
			raise ValueError(f"Missing or stale mobile resource: {route}")
	for name in ("LICENSE", "NOTICE", "NOTICE.toolchain", "MINIZ-LICENSE.txt", "JSON-LICENSE.txt", "SHA256-LICENSE.txt"):
		if not entries.get("licenses/" + name):
			raise ValueError(f"Missing native redistribution notice: {name}")
	return metadata


def prepare(android_aar: Path, ios_sdk: Path, output: Path) -> None:
	if output.exists():
		raise ValueError("Choose a new output directory; existing SDKs are not overwritten")
	verify_android_aar(android_aar, REPO / "VeroScript/Resources/Scripts/stdlib", mobile_catalog_files(REPO),
					   REPO / "VeroScriptInterop/Public/VeroScriptInterop.h")
	verify_framework(ios_sdk / "VeroScriptIOS.xcframework")
	expected = resources()
	ios_resources = tree(ios_sdk / "assets")
	if any(ios_resources.get(route) != data for route, data in expected.items()):
		raise ValueError("iOS SDK resources do not match this source revision")
	output.parent.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory(prefix="veroscript-csharp-mobile-", dir=output.parent) as temporary:
		stage = Path(temporary) / "sdk"
		stage.mkdir()
		with zipfile.ZipFile(android_aar) as archive:
			for abi in ("arm64-v8a", "x86_64"):
				for name in ("libVeroScriptInterop.so", "libc++_shared.so"):
					target = stage / "android" / abi / name
					target.parent.mkdir(parents=True, exist_ok=True)
					target.write_bytes(archive.read(f"jni/{abi}/{name}"))
			for name in ("LICENSE", "NOTICE", "NOTICE.toolchain", "MINIZ-LICENSE.txt", "JSON-LICENSE.txt", "SHA256-LICENSE.txt"):
				target = stage / "licenses" / name
				target.parent.mkdir(parents=True, exist_ok=True)
				target.write_bytes(archive.read("assets/veroscript/licenses/" + name))
		shutil.copytree(ios_sdk / "VeroScriptIOS.xcframework", stage / "ios/VeroScriptIOS.xcframework")
		(stage / "ios/VeroScriptInterop.symbols.targets").write_bytes(static_symbols())
		for route, data in expected.items():
			target = stage / "assets" / route
			target.parent.mkdir(parents=True, exist_ok=True)
			target.write_bytes(data)
		(stage / "assets/resources.json").write_text(json.dumps(
			{"schemaVersion": 1, "files": {route: digest(data) for route, data in expected.items()}}, indent=2) + "\n", encoding="utf-8")
		metadata = {"schemaVersion": 1, "abiVersion": "0x00000101", "nativeJit": False,
					"applicationValidation": "required: Android emulator/device and iOS simulator/device; signing and Release trimming/AOT",
					"files": {route: digest(data) for route, data in tree(stage).items()}}
		(stage / "mobile-sdk.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
		verify(stage)
		stage.rename(output)


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	commands = parser.add_subparsers(dest="command", required=True)
	preparation = commands.add_parser("prepare")
	preparation.add_argument("--android-aar", type=Path, required=True)
	preparation.add_argument("--ios-sdk", type=Path, required=True)
	preparation.add_argument("--output", type=Path, required=True)
	audit = commands.add_parser("verify")
	audit.add_argument("root", type=Path)
	args = parser.parse_args()
	if args.command == "prepare":
		prepare(args.android_aar.resolve(), args.ios_sdk.resolve(), args.output.resolve())
		print(f"C# mobile native SDK prepared: {args.output}")
	else:
		verify(args.root.resolve())
		print(f"C# mobile native SDK verified: {args.root}")


if __name__ == "__main__":
	main()
