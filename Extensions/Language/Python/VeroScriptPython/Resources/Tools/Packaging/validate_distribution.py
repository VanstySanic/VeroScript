# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import base64
import configparser
import csv
from email.parser import BytesParser
from email.policy import default
import hashlib
import io
from pathlib import Path
from pathlib import PurePosixPath
import re
import stat
import sys
import tarfile
from typing import NoReturn
import zipfile

def _packaging_tools() -> Path:
	for parent in Path(__file__).resolve().parents:
		tools = parent / 'Resources/Tools/Packaging'
		if (tools / 'veroscript_products.py').is_file():
			return tools
	raise RuntimeError('Cannot locate VeroScript shared packaging tools')


sys.path.insert(0, str(_packaging_tools()))
from veroscript_products import auxiliary_content


WHEEL_PATTERN = re.compile(
	r"^veroscript-(?P<version>[^-]+)-(?P<python>cp\d+)-"
	r"(?P<abi>[^-]+)-(?P<platform>.+)\.whl$"
)
SDIST_PATTERN = re.compile(r"^veroscript-(?P<version>[^-]+)\.tar\.gz$")
SUPPORTED_PYTHON_TAGS = {"cp311", "cp312", "cp313", "cp314", "cp315"}
WINDOWS_PLATFORM_FAMILIES = {"win_amd64": "windows-amd64", "win32": "windows-x86", "win_arm64": "windows-arm64"}
REQUIRED_PLATFORMS = {
	"linux-x86_64",
	"linux-aarch64",
	*WINDOWS_PLATFORM_FAMILIES.values(),
	"macos-x86_64",
	"macos-arm64",
}
REQUIRED_LICENSE_FILES = {
	"LICENSE",
}
APPLICATION_LICENSE_FILES = {
	"miniz/LICENSE",
	"json/LICENSE.MIT",
	"sha256/LICENSE",
}
REQUIRED_APPLICATION_SOURCES = {
	"CMakeLists.txt",
	"Resources/Tools/CMake/VeroScriptApplicationConfig.cmake.in",
	"Private/Tools/VeroScriptRuntimeMain.cpp",
	"Private/ApplicationInternal.h",
	"Private/ApplicationInternal.cpp",
	*(f"Public/{name}.h" for name in (
		"VeroScriptApplication", "VeroScriptArchive", "VeroScriptExecutable",
		"VeroScriptProcess", "VeroScriptProtection")),
	*(f"Private/{name}.cpp" for name in (
		"VeroScriptApplication", "VeroScriptApplicationHost", "VeroScriptApplicationOptions",
		"VeroScriptArchive", "VeroScriptExecutable", "VeroScriptProcess",
		"VeroScriptProtection", "VeroScriptSodium")),
	*(f"ThirdParty/{name}" for name in APPLICATION_LICENSE_FILES),
	*(f"ThirdParty/miniz/{name}" for name in (
		"miniz.c", "miniz.h", "miniz_common.h", "miniz_tdef.c", "miniz_tdef.h",
		"miniz_tinfl.c", "miniz_tinfl.h", "miniz_zip.c", "miniz_zip.h")),
	"ThirdParty/json/json.hpp",
	"ThirdParty/sha256/picosha2.h",
}
REQUIRED_VM_SOURCES = {
	"CMakeLists.txt",
	"Public/VeroScriptBytecodeVm.h",
	"Public/VeroScriptBytecodeVmAssembly.h",
	"Resources/VeroScriptBytecodeVmConfig.cmake.in",
	*(f"Private/{name}" for name in (
		"VeroScriptBytecodeVm.cpp", "VeroScriptBytecodeVmDiagnostics.cpp",
		"VeroScriptBytecodeVmExecution.cpp", "VeroScriptBytecodeVmExecution.h",
		"VeroScriptBytecodeVmMembers.cpp", "VeroScriptBytecodeVmMembers.inl",
		"VeroScriptBytecodeVmModule.cpp", "VeroScriptVmStringLocals.cpp", "VeroScriptVmStringLocals.h")),
}
CATALOG_SOURCES = {
	"host-binding.json": "Script/VeroScript",
	"language.json": "Script/VeroScript",
	"memory.json": "Script/VeroScript",
	"runtime.json": "Script/VeroScript",
	"shared.json": "Script/VeroScript",
	"embedding.json": "Script/Extensions/Language/Cpp/VeroScriptCpp",
	"interop.json": "Script/VeroScriptInterop",
	"python-tools.json": "Script/Extensions/Language/Python",
	"python-frontend.json": "Script/Extensions/Language/Python/VeroScriptPythonFrontend",
	"python-bindgen.json": "Script/Extensions/Language/Python/VeroScriptPythonBindGen",
	"python-transpiler.json": "Script/Extensions/Language/Python/VeroScriptPythonTranspiler",
	"python-aot.json": "Script/Extensions/Language/Python/VeroScriptPythonAot",
}
REQUIRED_CATALOGS = set(CATALOG_SOURCES)
REQUIRED_LANGUAGES = {"en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"}
REQUIRED_STDLIB = {
	"algorithm.vs",
	"bigint.vs",
	"concurrent.vs",
	"core.vs",
	"delegate.vs",
	"io.vs",
	"math.vs",
	"memory.vs",
	"numeric.vs",
	"path.vs",
	"random.vs",
	"rational.vs",
	"string.vs",
}
REQUIRED_DEPENDENCIES = {
	'typing_extensions>=4.16; python_version < "3.15"',
}
REQUIRED_CONSOLE_SCRIPTS = {
	"veroscript-python-frontend": "veroscript_python_frontend.__main__:main",
	"veroscript-python-bindgen": "veroscript_python_bindgen.__main__:main",
	"veroscript-python-transpiler": "veroscript_python_transpiler.__main__:main",
	"veroscript-python-aot": "veroscript_python_aot.__main__:main",
}
CPYTHON_RUNTIME_PATTERN = re.compile(
	r"^(?:python(?:3|\d{2,3})?(?:_d)?\.(?:exe|dll|lib)|"
	r"python(?:3|\d{2,3})?\.(?:zip|_pth)|"
	r"libpython\d(?:\.\d+)*(?:m)?\.(?:so(?:\.\d+)*|dylib)|"
	r"pyvenv\.cfg)$",
	re.IGNORECASE,
)
ABSOLUTE_WINDOWS_PDB_PATTERN = re.compile(
	rb"[A-Za-z]:[\\/][^\x00\r\n]{1,1024}\.pdb(?=\x00)",
	re.IGNORECASE,
)
MANYLINUX_PLATFORM_PATTERN = re.compile(
	r"^(?:manylinux(?:2014|_\d+_\d+)_(x86_64|aarch64)|manylinux(?:1|2010)_(x86_64))$"
)
LINUX_PLATFORM_PATTERN = re.compile(r"^linux_(x86_64|aarch64)$")
MACOS_PLATFORM_PATTERN = re.compile(r"^macosx_\d+_\d+_(x86_64|arm64)$")
FORBIDDEN_SDIST_PREFIXES = (
	(".idea",),
	(".tmp",),
	("app",),
	("ide",),
	("utils",),
	("script", "cpp", "veroscriptcpp", "private", "cpp", "tests"),
	("script", "veroscript", "benchmarks"),
	("script", "veroscript", "resources", "benchmarks"),
	("script", "veroscript", "docs"),
	("script", "veroscript", "resources", "docs"),
	("script", "extensions", "cpp", "resources", "docs"),
	("script", "extensions", "python", "resources", "docs"),
	("script", "veroscript", "private", "script", "tests"),
	("script", "veroscriptinterop", "docs"),
	("script", "veroscriptinterop", "resources", "docs"),
	("script", "veroscriptinterop", "private", "interop", "tests"),
	("script", "veroscriptutils", "docs"),
	("script", "veroscript", "scripts", "examples"),
	("script", "veroscriptutils", "tutorials"),
	("thirdparty", "asmjit", ".github"),
	("thirdparty", "asmjit", "asmjit-testing"),
	("thirdparty", "asmjit", "tools"),
)
SDIST_BUILD_FILENAMES = {
	"cmakecache.txt", "cmakeuserpresets.json", "cmake_install.cmake",
	"ctesttestfile.cmake", "install_manifest.txt", "compile_commands.json",
	".ninja_deps", ".ninja_log", "build.ninja",
}
SDIST_BUILD_SUFFIXES = (
	".obj", ".o", ".a", ".lib", ".exp", ".exe", ".dll", ".pyd", ".so",
	".dylib", ".wasm", ".pdb", ".ilk", ".idb", ".pch", ".ipch", ".gch",
	".tmp", ".temp", ".log", ".bak", ".orig", ".rej",
	".vsbc", ".vsc", ".vsapp", ".vslib", ".vspkg",
	".whl", ".nupkg", ".snupkg", ".vsix", ".apk", ".aab", ".aar",
)


class _CaseSensitiveConfigParser(configparser.ConfigParser):
	def optionxform(self, optionstr: str) -> str:
		return optionstr


def fail(message: str) -> NoReturn:
	raise SystemExit(f"error: {message}")


def validate_archive_names(
	names: list[str], archive: Path, directory_names: set[str] | None = None
) -> None:
	if len(names) != len(set(names)):
		fail(f"{archive.name} contains duplicate archive entries")
	unsafe: list[str] = []
	normalized: list[str] = []
	for name in names:
		path_text = name[:-1] if name.endswith("/") else name
		raw_parts = path_text.split("/")
		if (
			not path_text
			or "\\" in name
			or "\0" in name
			or PurePosixPath(path_text).is_absolute()
			or any(part in {"", ".", ".."} for part in raw_parts)
			or bool(raw_parts and re.fullmatch(r"[A-Za-z]:", raw_parts[0]))
		):
			unsafe.append(name)
		normalized.append(path_text)
	if unsafe:
		fail(f"{archive.name} contains unsafe paths: {unsafe!r}")
	if len(normalized) != len(set(normalized)):
		fail(f"{archive.name} contains conflicting file and directory entries")
	folded = [name.casefold() for name in normalized]
	if len(folded) != len(set(folded)):
		fail(f"{archive.name} contains case-insensitive path collisions")
	known_directories = {
		name[:-1] if name.endswith("/") else name
		for name in (directory_names or set())
	}
	file_paths = {
		normalized_name
		for original, normalized_name in zip(names, normalized)
		if not original.endswith("/") and normalized_name not in known_directories
	}
	for path_text in normalized:
		parts = path_text.split("/")
		for index in range(1, len(parts)):
			parent = "/".join(parts[:index])
			if parent in file_paths:
				fail(
					f"{archive.name} uses file {parent!r} as a parent directory"
				)


def is_bundled_cpython(name: str) -> bool:
	path = PurePosixPath(name)
	parts = {part.casefold() for part in path.parts}
	return (
		bool(CPYTHON_RUNTIME_PATTERN.fullmatch(path.name))
		or "python.framework" in parts
		or (
			path.name.casefold() == "python.h"
			and "include" in parts
		)
	)


def embedded_absolute_pdb_paths(data: bytes) -> set[str]:
	return {
		match.group().decode("utf-8", errors="replace")
		for match in ABSOLUTE_WINDOWS_PDB_PATTERN.finditer(data)
	}


def is_forbidden_sdist_source_path(name: str) -> bool:
	path = PurePosixPath(name)
	parts = tuple(part.casefold() for part in path.parts[1:])
	filename = path.name.casefold()
	if (
		any(
			part in {"cmakefiles", ".git"}
			or part.endswith((".dsym", ".xcarchive", ".xcframework"))
			for part in parts
		)
		or filename in SDIST_BUILD_FILENAMES
		# Preserve the canonical LGPL COPYING.LIB notice.
		or (filename.endswith(SDIST_BUILD_SUFFIXES) and filename != "copying.lib")
		or re.search(r"\.so\.\d+(?:\.\d+)*$", filename)
		or (
			filename.startswith(("veroscript", "vs-"))
			and filename.endswith((".zip", ".tar.gz"))
		)
	):
		return True
	owned = {'veroscript', 'veroscriptinterop', 'veroscriptapplication', 'veroscriptutils', 'extensions', 'resources'}
	if parts and parts[0] in owned:
		parts = ('script', *parts)
	elif len(parts) > 1 and parts[1] in owned:
		parts = ('script', *parts[1:])
	return auxiliary_content(name) or any(
		parts[: len(prefix)] == prefix for prefix in FORBIDDEN_SDIST_PREFIXES
	) or (parts[:1] == ("script",) and any(
		parts[index:index + 2] == ("resources", "docs") for index in range(len(parts) - 1)
	))


def validate_wheel_record(
	archive: zipfile.ZipFile,
	archive_path: Path,
	names: set[str],
	record_name: str,
) -> None:
	rows = list(csv.reader(io.StringIO(archive.read(record_name).decode("utf-8"))))
	if any(len(row) != 3 for row in rows):
		fail(f"{archive_path.name} has a malformed RECORD")
	recorded = {row[0] for row in rows}
	if len(recorded) != len(rows):
		fail(f"{archive_path.name} RECORD contains duplicate entries")
	if recorded != names:
		fail(
			f"{archive_path.name} RECORD file list differs from wheel contents; "
			f"missing={sorted(names - recorded)!r}, extra={sorted(recorded - names)!r}"
		)
	for name, encoded_hash, encoded_size in rows:
		if name == record_name:
			if encoded_hash or encoded_size:
				fail(f"{archive_path.name} RECORD must not hash itself")
			continue
		data = archive.read(name)
		if encoded_size != str(len(data)):
			fail(f"{archive_path.name} has an invalid size for {name}")
		expected_hash = (
			base64.urlsafe_b64encode(hashlib.sha256(data).digest())
			.rstrip(b"=")
			.decode("ascii")
		)
		if encoded_hash != f"sha256={expected_hash}":
			fail(f"{archive_path.name} has an invalid hash for {name}")


def metadata_licenses(data: bytes) -> set[str]:
	return set(BytesParser(policy=default).parsebytes(data).get_all('License-File', []))


def source_module(names: set[str]) -> str:
	roots = {parts[1] for name in names
			 if len(parts := PurePosixPath(name).parts) == 5
			 and parts[2:] == ('VeroScript', 'Public', 'VeroScriptVersion.h')}
	if any(PurePosixPath(name).parts[1:] == ('VeroScript', 'Public', 'VeroScriptVersion.h') for name in names):
		roots.add('.')
	if len(roots) != 1:
		fail('Source distribution must identify one source module')
	return roots.pop()


def metadata_version(data: bytes, archive: Path) -> str:
	metadata = BytesParser(policy=default).parsebytes(data)
	name = metadata.get("Name")
	version = metadata.get("Version")
	requires_python = metadata.get("Requires-Python")
	dependencies = set(metadata.get_all("Requires-Dist", []))
	license_expression = metadata.get("License-Expression")
	license_files = set(metadata.get_all("License-File", []))
	if name != "veroscript":
		fail(f"{archive.name} has unexpected project name {name!r}")
	if not version:
		fail(f"{archive.name} has no Version metadata")
	if not requires_python or set(requires_python.replace(' ', '').split(',')) != {"<3.16", ">=3.11"}:
		fail(
			f"{archive.name} has unexpected Requires-Python {requires_python!r}"
		)
	if dependencies != REQUIRED_DEPENDENCIES:
		fail(
			f"{archive.name} has unexpected Requires-Dist metadata: "
			f"{sorted(dependencies)!r}"
		)
	if license_expression != "MIT":
		fail(
			f"{archive.name} has unexpected License-Expression "
			f"{license_expression!r}"
		)
	if license_files != REQUIRED_LICENSE_FILES:
		fail(
			f"{archive.name} has incomplete License-File metadata: "
			f"{sorted(license_files)!r}"
		)
	return str(version)


def validate_console_scripts(data: bytes, archive: Path) -> None:
	parser = _CaseSensitiveConfigParser(interpolation=None)
	try:
		parser.read_string(data.decode("utf-8"))
	except (UnicodeError, configparser.Error) as error:
		fail(f"{archive.name} has malformed entry-point metadata: {error}")
	if parser.sections() != ["console_scripts"]:
		fail(
			f"{archive.name} has unexpected entry-point groups: "
			f"{parser.sections()!r}"
		)
	scripts = dict(parser.items("console_scripts", raw=True))
	if scripts != REQUIRED_CONSOLE_SCRIPTS:
		fail(
			f"{archive.name} has unexpected console scripts: "
			f"{scripts!r}"
		)


def _platform_tag_family(platform_tag: str) -> str:
	if platform_tag in WINDOWS_PLATFORM_FAMILIES:
		return WINDOWS_PLATFORM_FAMILIES[platform_tag]
	manylinux = MANYLINUX_PLATFORM_PATTERN.fullmatch(platform_tag)
	if manylinux:
		return "manylinux-" + (manylinux[1] or manylinux[2])
	linux = LINUX_PLATFORM_PATTERN.fullmatch(platform_tag)
	if linux:
		return f"linux-{linux[1]}-local"
	macos_match = MACOS_PLATFORM_PATTERN.fullmatch(platform_tag)
	if macos_match:
		return f"macos-{macos_match.group(1)}"
	fail(f"unsupported wheel platform tag {platform_tag!r}")


def platform_family(platform_tag: str) -> str:
	families = {
		_platform_tag_family(tag) for tag in platform_tag.split(".")
	}
	if len(families) != 1:
		fail(
			f"wheel platform tags span multiple platform families: "
			f"{platform_tag!r}"
		)
	return next(iter(families))


def expanded_wheel_tags(
	python_tag: str, abi_tag: str, platform_tag: str
) -> set[str]:
	return {
		f"{python_component}-{abi_component}-{platform_component}"
		for python_component in python_tag.split(".")
		for abi_component in abi_tag.split(".")
		for platform_component in platform_tag.split(".")
	}


def validate_wheel(path: Path) -> tuple[str, str, str]:
	match = WHEEL_PATTERN.fullmatch(path.name)
	if match is None:
		fail(f"unexpected wheel filename {path.name!r}")
	python_tag = match.group("python")
	abi_tag = match.group("abi")
	if python_tag not in SUPPORTED_PYTHON_TAGS:
		fail(f"{path.name} has unsupported Python tag {python_tag!r}")
	if abi_tag != python_tag:
		fail(f"{path.name} must use its version-specific CPython ABI tag")
	platform_tag = match.group("platform")
	family = platform_family(platform_tag)

	with zipfile.ZipFile(path) as archive:
		entries = archive.infolist()
		validate_archive_names(
			[item.filename for item in entries],
			path,
			{item.filename for item in entries if item.is_dir()},
		)
		links = [
			item.filename
			for item in entries
			if stat.S_ISLNK((item.external_attr >> 16) & 0xFFFF)
		]
		if links:
			fail(f"{path.name} contains symbolic links: {links!r}")
		special = [
			item.filename
			for item in entries
			if (mode := (item.external_attr >> 16) & 0xFFFF)
			and stat.S_IFMT(mode) not in {0, stat.S_IFREG, stat.S_IFDIR}
		]
		if special:
			fail(f"{path.name} contains special file entries: {special!r}")
		file_names = [item.filename for item in entries if not item.is_dir()]
		names = set(file_names)
		metadata_names = [
			name for name in names if name.endswith(".dist-info/METADATA")
		]
		if len(metadata_names) != 1:
			fail(f"{path.name} must contain exactly one METADATA file")
		version = metadata_version(archive.read(metadata_names[0]), path)
		dist_info = metadata_names[0].removesuffix("METADATA")
		expected_dist_info = f"veroscript-{version}.dist-info/"
		if dist_info != expected_dist_info:
			fail(
				f"{path.name} has unexpected dist-info directory {dist_info!r}"
			)
		wheel_metadata_name = f"{dist_info}WHEEL"
		record_name = f"{dist_info}RECORD"
		entry_points_name = f"{dist_info}entry_points.txt"
		if (
			wheel_metadata_name not in names
			or record_name not in names
			or entry_points_name not in names
		):
			fail(f"{path.name} is missing WHEEL, RECORD, or entry-point metadata")
		validate_console_scripts(archive.read(entry_points_name), path)
		wheel_metadata = BytesParser(policy=default).parsebytes(
			archive.read(wheel_metadata_name)
		)
		if wheel_metadata.get("Root-Is-Purelib", "").casefold() != "false":
			fail(f"{path.name} must be marked as a platform wheel")
		expected_tags = expanded_wheel_tags(
			python_tag, abi_tag, platform_tag
		)
		wheel_tags = set(wheel_metadata.get_all("Tag", []))
		if wheel_tags != expected_tags:
			fail(
				f"{path.name} WHEEL metadata has unexpected tags "
				f"{sorted(wheel_tags)!r}; expected {sorted(expected_tags)!r}"
			)
		required = {
			"veroscript/__init__.py",
			"veroscript/_mobile.py",
			"veroscript/_veroscript.pyi",
			"veroscript/py.typed",
			"veroscript_python_frontend/__init__.py",
			"veroscript_python_frontend/__main__.py",
			"veroscript_python_frontend/_toolchain.py",
			"veroscript_python_frontend/_version.py",
			"veroscript_python_frontend/localization.py",
			"veroscript_python_frontend/py.typed",
			"veroscript_python_bindgen/__init__.py",
			"veroscript_python_bindgen/__main__.py",
			"veroscript_python_bindgen/py.typed",
			"veroscript_python_transpiler/__init__.py",
			"veroscript_python_transpiler/__main__.py",
			"veroscript_python_transpiler/py.typed",
			"veroscript_python_aot/__init__.py",
			"veroscript_python_aot/__main__.py",
			"veroscript_python_aot/py.typed",
			*(f"veroscript/licenses/application/{name}" for name in APPLICATION_LICENSE_FILES),
			*(f"veroscript/stdlib/{name}" for name in REQUIRED_STDLIB),
			*(
				f"veroscript/localization/{language}/{catalog}"
				for language in REQUIRED_LANGUAGES
				for catalog in REQUIRED_CATALOGS
			),
			*(f"{dist_info}licenses/{name}" for name in metadata_licenses(archive.read(metadata_names[0]))),
		}
		missing = sorted(required - names)
		if missing:
			fail(f"{path.name} is missing payload files: {', '.join(missing)}")
		extensions = sorted(
			name
			for name in names
			if name.startswith("veroscript/_veroscript.")
			and (name.endswith(".pyd") or name.endswith(".so"))
		)
		if len(extensions) != 1:
			fail(f"{path.name} must contain exactly one native extension")
		extension_name = PurePosixPath(extensions[0]).name
		python_digits = python_tag.removeprefix("cp")
		if family in WINDOWS_PLATFORM_FAMILIES.values():
			if not extension_name.endswith(f".{python_tag}-{platform_tag.split('.')[0]}.pyd"):
				fail(f"{path.name} contains mismatched extension {extension_name!r}")
		elif (
			f".cpython-{python_digits}-" not in extension_name
			or not extension_name.endswith(".so")
		):
			fail(f"{path.name} contains mismatched extension {extension_name!r}")
		interop_runtimes = [
			name
			for name in names
			if (
				name.rsplit("/", 1)[-1] == "VeroScriptInterop.dll"
				or name.rsplit("/", 1)[-1].startswith("libVeroScriptInterop")
				and (name.endswith(".so") or name.endswith(".dylib"))
			)
		]
		if len(interop_runtimes) != 1:
			fail(f"{path.name} must contain exactly one Interop runtime")
		absolute_pdb_paths = {
			name: sorted(paths)
			for name in (*extensions, *interop_runtimes)
			if (
				paths := embedded_absolute_pdb_paths(archive.read(name))
			)
		}
		if absolute_pdb_paths:
			fail(
				f"{path.name} leaks absolute PDB paths: "
				f"{absolute_pdb_paths!r}"
			)
		bundled_cpython = sorted(name for name in names if is_bundled_cpython(name))
		if bundled_cpython:
			fail(f"{path.name} must not bundle CPython: {bundled_cpython!r}")
		validate_wheel_record(archive, path, names, record_name)

	if version != match.group("version"):
		fail(f"{path.name} filename and METADATA versions differ")
	return version, python_tag, family


def validate_sdist(path: Path) -> str:
	match = SDIST_PATTERN.fullmatch(path.name)
	if match is None:
		fail(f"unexpected source-distribution filename {path.name!r}")
	with tarfile.open(path, "r:gz") as archive:
		all_members = archive.getmembers()
		validate_archive_names(
			[member.name for member in all_members],
			path,
			{member.name for member in all_members if member.isdir()},
		)
		unsupported = [
			member.name
			for member in all_members
			if not member.isfile() and not member.isdir()
		]
		if unsupported:
			fail(
				f"{path.name} contains links or special archive entries: "
				f"{unsupported!r}"
			)
		members = [member for member in all_members if member.isfile()]
		member_names = [member.name for member in members]
		metadata_members = [
			member for member in members if member.name.endswith("/PKG-INFO")
		]
		if len(metadata_members) != 1:
			fail(f"{path.name} must contain exactly one PKG-INFO file")
		extracted = archive.extractfile(metadata_members[0])
		if extracted is None:
			fail(f"cannot read PKG-INFO from {path.name}")
		metadata = extracted.read()
		version = metadata_version(metadata, path)
		names = set(member_names)
		roots = {PurePosixPath(name).parts[0] for name in member_names}
		if len(roots) != 1:
			fail(f"{path.name} must contain exactly one top-level directory")
		root = next(iter(roots))
		expected_root = f"veroscript-{version}"
		if root != expected_root:
			fail(f"{path.name} has unexpected archive root {root!r}")
		forbidden_source_paths = sorted(
			name
			for name in names
			if len(PurePosixPath(name).parts) > 1
			and (
				PurePosixPath(name).parts[1].startswith(".codex-")
				or PurePosixPath(name).parts[1] in {".venv", "build", "dist"}
				or is_forbidden_sdist_source_path(name)
				or "__pycache__" in PurePosixPath(name).parts
				or name.endswith((".pyc", ".pyo"))
			)
		)
		if forbidden_source_paths:
			fail(
				f"{path.name} contains local build or review artifacts: "
				f"{forbidden_source_paths!r}"
			)
		module = source_module(names)
		source = root if module == '.' else f'{root}/{module}'
		required = {
			f"{source}/CMakeLists.txt",
			f"{source}/LICENSE",
			f"{source}/Resources/Assets/icons/veroscript-icon.ico",
			f"{root}/pyproject.toml",
			f"{source}/Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md",
			f"{source}/Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md",
			f"{source}/Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/VERSION.txt",
			*(f"{source}/Modules/Execution/VeroScriptBytecodeVm/{name}" for name in REQUIRED_VM_SOURCES),
			f"{source}/Extensions/Language/Python/VeroScriptPython/CMakeLists.txt",
			f"{source}/Extensions/Language/Python/VeroScriptPython/Resources/Tools/CMake/VeroScriptPythonCross.cmake",
			f"{source}/Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPython.cpp",
			f"{source}/Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPythonMemory.inl",
			f"{source}/Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPythonNumerics.inl",
			f"{source}/Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPythonVariant.inl",
			f"{source}/Extensions/Language/Python/VeroScriptPython/python/veroscript/__init__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPython/python/veroscript/_mobile.py",
			f"{source}/Extensions/Language/Python/VeroScriptPython/python/veroscript/_veroscript.pyi",
			f"{source}/Extensions/Language/Python/VeroScriptPython/python/veroscript/py.typed",
			*(f"{source}/Extensions/Language/Python/Resources/Tools/Mobile/{name}.py"
			  for name in ("mobile_sdk", "package_mobile", "verify_mobile", "device_smoke")),
			f"{source}/Resources/Tools/Packaging/veroscript_mobile.py",
			f"{source}/Resources/Tools/Packaging/veroscript_products.py",
			f"{source}/Resources/Tools/Packaging/veroscript_paths.py",
			f"{source}/Shared/Private/VeroScriptFloatParsing.h",
			f"{source}/Shared/Private/VeroScriptFloatFormatting.h",
			f"{source}/Shared/Private/VeroScriptDarwinPaths.h",
			f"{source}/Shared/Private/VeroScriptJsonString.h",
			f"{source}/Extensions/Language/Python/VeroScriptPythonFrontend/CMakeLists.txt",
			f"{source}/Extensions/Language/Python/VeroScriptPythonFrontend/python/veroscript_python_frontend/__init__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonFrontend/python/veroscript_python_frontend/__main__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonFrontend/python/veroscript_python_frontend/_toolchain.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonFrontend/python/veroscript_python_frontend/_version.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonFrontend/python/veroscript_python_frontend/localization.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonFrontend/python/veroscript_python_frontend/py.typed",
			f"{source}/Extensions/Language/Python/VeroScriptPythonBindGen/CMakeLists.txt",
			f"{source}/Extensions/Language/Python/VeroScriptPythonBindGen/python/veroscript_python_bindgen/__init__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonBindGen/python/veroscript_python_bindgen/__main__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonBindGen/python/veroscript_python_bindgen/py.typed",
			f"{source}/Extensions/Language/Python/VeroScriptPythonTranspiler/CMakeLists.txt",
			f"{source}/Extensions/Language/Python/VeroScriptPythonTranspiler/python/veroscript_python_transpiler/__init__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonTranspiler/python/veroscript_python_transpiler/__main__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonTranspiler/python/veroscript_python_transpiler/py.typed",
			f"{source}/Extensions/Language/Python/VeroScriptPythonAot/CMakeLists.txt",
			f"{source}/Extensions/Language/Python/VeroScriptPythonAot/Public/VeroScriptPythonAot.h",
			f"{source}/Extensions/Language/Python/VeroScriptPythonAot/Private/VeroScriptPythonAot.cpp",
			f"{source}/Extensions/Language/Python/VeroScriptPythonAot/python/veroscript_python_aot/__init__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonAot/python/veroscript_python_aot/__main__.py",
			f"{source}/Extensions/Language/Python/VeroScriptPythonAot/python/veroscript_python_aot/py.typed",
			f"{source}/Modules/Execution/VeroScriptAsmJit/Private/VeroScriptJitResources.cpp",
			f"{source}/Modules/Execution/VeroScriptAsmJit/CMakeLists.txt",
			f"{source}/Modules/Execution/VeroScriptAsmJit/Public/VeroScriptAsmJit.h",
			f"{source}/VeroScript/Public/VeroScriptJitBackend.h",
			f"{source}/VeroScript/Public/VeroScriptVmBackend.h",
			f"{source}/VeroScript/Public/VeroScriptVersion.h",
			*(f"{source}/VeroScript/Public/{name}.h" for name in (
				"VeroScriptAnnotation", "VeroScriptAnnotationProcessing", "VeroScriptProgramPreparation")),
			*(f"{source}/VeroScript/Private/{name}" for name in (
				"VeroScriptAstClone.h", "VeroScriptAstClone.cpp", "VeroScriptAnnotationProcessing.cpp",
				"VeroScriptProgramPreparation.cpp")),
			f"{source}/VeroScript/Public/VeroScriptMemory.h",
			f"{source}/VeroScript/Public/VeroScriptNumerics.h",
			f"{source}/VeroScript/Public/VeroScriptNumericBindings.h",
			*(f"{source}/VeroScript/Private/Numerics/{name}" for name in (
				"VeroScriptNumerics.cpp", "VeroScriptNumericsInternal.h", "VeroScriptNumericsInternal.cpp",
				"VeroScriptBigInteger.cpp", "VeroScriptRational.cpp", "VeroScriptNumericBindings.cpp",
				"VeroScriptNumericConversion.cpp", "VeroScriptNumericText.cpp")),
			f"{source}/VeroScript/Public/VeroScriptVariant.h",
			f"{source}/VeroScript/Private/VeroScriptVariantInternal.h",
			f"{source}/VeroScript/Private/VeroScriptVariant.cpp",
			f"{source}/VeroScript/Private/Compiler/VeroScriptCompilerMemory.cpp",
			f"{source}/VeroScript/Private/ExecutionSupport/VeroScriptVmMemory.cpp",
			*(f"{source}/VeroScript/Private/Memory/{name}" for name in (
				"VeroScriptMemory.cpp", "VeroScriptMemoryInternal.h", "VeroScriptMemoryInternal.cpp",
				"VeroScriptMemoryIdentity.cpp", "VeroScriptMemoryBytes.cpp", "VeroScriptMemoryBindings.cpp")),
			f"{source}/VeroScriptInterop/CMakeLists.txt",
			*(f"{source}/VeroScriptApplication/{name}" for name in REQUIRED_APPLICATION_SOURCES),
			f"{source}/VeroScriptInterop/Public/VeroScriptInteropAnnotation.h",
			f"{source}/VeroScriptInterop/Public/VeroScriptInteropCompileProcessing.h",
			f"{source}/VeroScriptInterop/Public/VeroScriptInteropModuleEnvironment.h",
			f"{source}/VeroScriptInterop/Private/VeroScriptInteropCompileProcessing.cpp",
			f"{source}/VeroScriptInterop/Private/VeroScriptInteropModuleEnvironment.cpp",
			f"{source}/VeroScript/Public/VeroScriptModuleEnvironment.h",
			f"{source}/VeroScript/Public/VeroScriptNativeExtension.h",
			f"{source}/VeroScript/Public/VeroScriptNativeExtensionLoader.h",
			f"{source}/VeroScript/Public/VeroScriptExternalDependency.h",
			f"{source}/VeroScript/Public/VeroScriptResources.h",
			f"{source}/VeroScript/Public/VeroScriptConsole.h",
			f"{source}/VeroScript/Private/VeroScriptConsole.cpp",
			f"{source}/VeroScript/Private/VeroScriptConsoleNative.h",
			f"{source}/VeroScriptInterop/Private/VeroScriptInteropAnnotation.cpp",
			f"{source}/VeroScriptInterop/Private/VeroScriptInteropMemory.cpp",
			f"{source}/VeroScriptInterop/Private/VeroScriptInteropNumerics.cpp",
			f"{source}/Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/CMakeLists.txt",
			f"{source}/Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md",
			f"{source}/Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/db/LICENSE.md",
			*(
				f"{source}/VeroScript/Resources/Scripts/stdlib/{name}"
				for name in REQUIRED_STDLIB
			),
			*(
				f"{source}/{CATALOG_SOURCES[catalog].removeprefix('Script/')}/Resources/Localization/{language}/{catalog}"
				for language in REQUIRED_LANGUAGES
				for catalog in REQUIRED_CATALOGS
			),
			*(f"{root}/{name}" for name in metadata_licenses(metadata)),
			f"{source}/Resources/Tools/Packaging/ReleaseVersions.json",
			f"{root}/Resources/Tools/Packaging/veroscript_python_metadata.py",
			f"{source}/Resources/Tools/CMake/VeroScriptHelpers.cmake",
			f"{source}/Resources/Tools/CMake/VeroScriptBuild.cmake",
			f"{source}/Resources/Tools/CMake/VeroScriptPaths.cmake",
			f"{source}/Resources/Tools/CMake/VeroScriptLocalization.cmake",
		}
		missing = sorted(required - names)
		if missing:
			fail(f"{path.name} is missing source files: {', '.join(missing)}")
		bundled_cpython = sorted(name for name in names if is_bundled_cpython(name))
		if bundled_cpython:
			fail(f"{path.name} must not bundle CPython: {bundled_cpython!r}")
	if version != match.group("version"):
		fail(f"{path.name} filename and PKG-INFO versions differ")
	return version


def main() -> None:
	parser = argparse.ArgumentParser(
		description="Validate VeroScriptPython wheel and source distributions."
	)
	parser.add_argument("directory", type=Path)
	parser.add_argument("--expected-version")
	parser.add_argument("--require-release-matrix", action="store_true")
	arguments = parser.parse_args()

	wheels = sorted(arguments.directory.glob("veroscript-*.whl"))
	sdists = sorted(arguments.directory.glob("veroscript-*.tar.gz"))
	if not wheels and not sdists:
		fail(f"no VeroScriptPython distributions found in {arguments.directory}")
	if len(sdists) > 1:
		fail("more than one source distribution was provided")

	versions: set[str] = set()
	matrix: set[tuple[str, str]] = set()
	for wheel in wheels:
		version, python_tag, family = validate_wheel(wheel)
		versions.add(version)
		entry = (python_tag, family)
		if entry in matrix:
			fail(f"duplicate release matrix entry {python_tag}/{family}")
		matrix.add(entry)
	for sdist in sdists:
		versions.add(validate_sdist(sdist))

	if len(versions) != 1:
		fail(f"distribution versions disagree: {sorted(versions)}")
	version = next(iter(versions))
	if arguments.expected_version and version != arguments.expected_version:
		fail(
			f"distribution version {version!r} does not match expected "
			f"version {arguments.expected_version!r}"
		)

	if arguments.require_release_matrix:
		matrix = {(python_tag, family.removeprefix('many').removesuffix('-local'))
			for python_tag, family in matrix}
		expected = {
			(python_tag, platform)
			for python_tag in SUPPORTED_PYTHON_TAGS
			for platform in REQUIRED_PLATFORMS
		}
		missing = sorted(expected - matrix)
		extra = sorted(matrix - expected)
		if missing or extra or len(sdists) != 1:
			fail(
				"incomplete release matrix; "
				f"missing={missing}, extra={extra}, sdists={len(sdists)}"
			)

	print(
		f"Validated VeroScriptPython {version}: "
		f"{len(wheels)} wheel(s), {len(sdists)} source distribution(s)."
	)


if __name__ == "__main__":
	main()
