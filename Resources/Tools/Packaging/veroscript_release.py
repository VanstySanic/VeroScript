# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Shared release contracts for native Unix packaging and portable auditing."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
from veroscript_paths import module_root, repository_root, module_route, source_route
import platform
import re
import shutil
import stat
import struct
import subprocess
import tarfile
import time
import uuid
import zipfile
from veroscript_products import ASMJIT_PACKAGE_NAME, BYTECODE_VM_PACKAGE_NAME, MODULE_PACKAGE_NAMES, FOUNDATION_PACKAGE_NAMES, ALL_BUSINESSES, business_route, INSTALLER_BUSINESSES, MODULE_BUSINESSES, verify_module_payload, verify_functional_payload
from veroscript_products import INSTALLABLE_PACKAGE_NAMES, installable_archive, installer_filename

# Keep native build/host gates separate from multi-ABI mobile distributions.
from veroscript_products import NATIVE_TARGETS as TARGETS, MOBILE_TARGETS, RELEASE_TARGETS
PRODUCTS = INSTALLER_BUSINESSES
PACKAGES = {"runtime": ("VeroScriptRuntime",), "sdk": ("VeroScriptSDK",),
			"interop": ("VeroScriptInteropSDK",),
			"languages": ("VeroScriptCpp", "VeroScriptCSharp", "VeroScriptPython"),
			"symbols": ("VeroScriptSymbols",), "jit": (ASMJIT_PACKAGE_NAME,), "vm": (BYTECODE_VM_PACKAGE_NAME,),
			"modules": (*MODULE_PACKAGE_NAMES.values(), *FOUNDATION_PACKAGE_NAMES.values())}
FORBIDDEN = {".veroscript", ".verocache", "__pycache__", ".pytest_cache"}
GENERATED = {".pyc", ".pyo", ".vsbc", ".vsc"}
WEBASSEMBLY_FILES = ("veroscript-engine.mjs", "veroscript-engine.wasm", "veroscript-engine.d.mts",
			 "veroscript-engine-fast.mjs", "veroscript-engine-fast.wasm", "veroscript-engine-fast.d.mts",
			 "veroscript-webassembly.ts", "veroscript-webassembly.d.ts")
WINDOWS_NATIVE_NAMES = {"veroscript.exe", "veroscript-lsp.exe", "veroscript-dap.exe", "veroscriptinterop.dll",
						"veroscript-runtime.exe", "veroscript-launcher.exe",
						"veroscriptcppbindgen.exe", "veroscriptcpptranspiler.exe", "veroscriptjitasmjit.dll",
						"veroscriptvmbytecode.dll", "veroscriptjsonmodule.dll", "veroscriptnetworkmodule.dll", "veroscripthttpmodule.dll",
						"veroscriptboostmodule.dll", "veroscriptcurlmodule.dll", "veroscriptboostruntime.dll",
						"veroscriptwebsocketmodule.dll", "veroscriptnetworkruntime.dll", "veroscriptsdl3.dll",
						"veroscriptguitoolsmodule.dll", "veroscriptrmluimodule.dll",
						"veroscriptstbmodule.dll", "veroscriptfontbackendmodule.dll"}
WINDOWS_RUNTIME_DLLS = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")


def verify_windows_runtime(entries: dict, identity: tuple[str, str]) -> None:
	"""Require the release CRT beside each shipped native entry point."""
	folded = {name.casefold(): value for name, value in entries.items()}
	runtime_libraries = {}
	for key, value in folded.items():
		directory, separator, filename = key.rpartition('/')
		if filename.endswith('.dll') and filename.startswith(('msvcp', 'vcruntime', 'concrt', 'vcomp')):
			runtime_libraries.setdefault(directory + separator, []).append((key, value))
	for name in entries:
		filename = name.rsplit("/", 1)[-1].casefold()
		if filename not in WINDOWS_NATIVE_NAMES and not filename.endswith(".pyd"):
			continue
		if native_identity(entries[name][0][:4096]) != identity:
			raise ValueError(f"Windows native architecture mismatch: {name}")
		parent = name.rsplit("/", 1)[0] + "/" if "/" in name else ""
		for library in WINDOWS_RUNTIME_DLLS if identity[1] == 'x64' else WINDOWS_RUNTIME_DLLS[:2]:
			key = (parent + library).casefold()
			if key not in folded or native_identity(folded[key][0][:4096]) != identity:
				raise ValueError(f"Missing/wrong Windows runtime dependency: {name}/{library}")
		for key, value in runtime_libraries.get(parent.casefold(), ()):
			if native_identity(value[0][:4096]) != identity:
				raise ValueError(f"Wrong Windows runtime dependency architecture: {key}")
		notice = (parent + "licenses/MicrosoftVisualCpp/NOTICE.txt").casefold()
		if notice not in folded or not folded[notice][0].strip():
			raise ValueError(f"Missing Windows runtime notice beside: {name}")


def digest(path: Path) -> str:
	with path.open("rb") as stream:
		return hashlib.file_digest(stream, "sha256").hexdigest()


def core_public_abi(headers: Path) -> str:
	"""Match the normalized, sorted public-header contract hashed by CMake."""
	# CMake sorts case-sensitive path strings even on Windows.
	files = sorted(headers.rglob("*.h"), key=lambda path: path.as_posix())
	if not files:
		raise ValueError(f"Missing Core public headers: {headers}")
	hashes = "".join(hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
					 for path in files)
	return hashlib.sha256(hashes.encode("ascii")).hexdigest()


def binary_prefix(path: Path) -> bytes:
	with path.open("rb") as stream:
		return stream.read(4096)


def version(value: str) -> str:
	if not re.fullmatch(r"\d+\.\d+\.\d+", value):
		raise ValueError(f"Invalid release version: {value}")
	return value


def jit_identity(version_output: str) -> dict:
	match = re.search(r"; jit-backend=([A-Za-z0-9_.-]+); jit-target=([^;\] ]+); native=(on|off)\]", version_output)
	if not match:
		raise ValueError(f"Runtime does not report its JIT module identity: {version_output}")
	return {"backend": match[1], "target": match[2], "nativeCode": match[3] == "on"}


def host_target() -> str:
	system = {"Linux": "linux", "Darwin": "macos", "Windows": "win"}.get(platform.system())
	arch = {"x86_64": "x64", "amd64": "x64", "aarch64": "arm64", "arm64": "arm64",
			"x86": "x86", "i386": "x86", "i686": "x86"}.get(platform.machine().lower())
	target = f"{system}-{arch}"
	if target not in TARGETS:
		raise ValueError(f"Unsupported native host: {platform.system()} {platform.machine()}")
	return target


def linked_path(path: Path) -> bool:
	"""Detect Unix symlinks and Windows reparse points, including junctions."""
	try:
		info = path.lstat()
	except FileNotFoundError:
		return False
	return stat.S_ISLNK(info.st_mode) or bool(
		getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


def scratch_directory(repo: Path) -> Path:
	"""Keep staging and recoverable backups in a real repository directory."""
	scratch = repo.resolve() / ".tmp"
	if linked_path(scratch):
		raise ValueError(f"Linked release scratch directory is forbidden: {scratch}")
	scratch.mkdir(exist_ok=True)
	return scratch


def safe_output(repo: Path, path: Path, *, component: bool = False) -> Path:
	"""Require a dedicated repository output, checking links before resolving."""
	repo = repo.resolve()
	path = Path(os.path.abspath(path))
	if not path.is_relative_to(repo) or path == repo:
		raise ValueError(f"Release output must be below the repository: {path}")
	relative = path.relative_to(repo)
	release = module_root(repo) / 'Release'
	business_directories = {release / business_route(business) for business in ALL_BUSINESSES}
	allowed = path.is_relative_to(release) or relative.parts[0] in {".tmp", "dist"}
	if path.is_relative_to(release):
		raise ValueError(f'Refusing direct output into a complete release collection; use the business publisher: {path}')
	if not allowed:
		raise ValueError(f"Release output must be under {release}, dist or .tmp: {path}")
	if (path == release or len(relative.parts) == 1 or path in business_directories
			or path in {release / 'Extensions', release / 'Modules'}
			or any(path == directory / target for directory in business_directories for target in RELEASE_TARGETS)
			or path.parent == release and path.name in RELEASE_TARGETS):
		raise ValueError(f"Refusing to replace a release collection root: {path}")
	current = path
	while current != repo:
		if linked_path(current):
			raise ValueError(f"Linked output path is forbidden: {current}")
		if (current / 'release-index.json').is_file():
			raise ValueError(f'Direct output cannot modify a managed release collection: {current}')
		if component and (current / "release-manifest.json").is_file():
			raise ValueError(f"Component output cannot modify a complete release: {current}")
		if component and current.exists() and any(current.glob('release-manifest-*.json')):
			raise ValueError(f'Component output cannot directly modify a managed business release: {current}')
		if component and current.exists() and any(current.glob('release-index-*.json')):
			raise ValueError(f'Component output cannot modify a release platform collection: {current}')
		current = current.parent
	if not path.resolve().is_relative_to(repo):
		raise ValueError(f"Resolved output escapes the repository: {path}")
	if path.exists():
		if not path.is_dir():
			raise ValueError(f"Release output is not a directory: {path}")
		if any(path.iterdir()) and not component:
			identity = json.loads((path / "release-manifest.json").read_text(encoding="utf-8-sig"))
			if identity.get("product") != "VeroScript Release Candidate" or not (path / "checksums.txt").is_file():
				raise ValueError(f"Refusing to replace an unrecognized release: {path}")
	return path


def portable_asset_bytes(path: Path) -> bytes:
	"""Rebase source asset links to the adjacent directories in a release."""
	data = path.read_bytes()
	if path.suffix.lower() != ".md":
		return data
	for ancestor in path.resolve().parents:
		if (ancestor / "Resources/Docs/documentation.json").is_file():
			relative = path.resolve().relative_to(ancestor)
			if relative.parts[0] in {".tmp", "Release", "dist", "build"}:
				# Staged documents already contain their published links. Use
				# only the legacy asset rewrite below, without source discovery.
				break
			from veroscript_docs import portable_bytes
			repo = ancestor if (ancestor / "CMakeLists.txt").is_file() else ancestor.parent
			return portable_bytes(repo, path)
	from veroscript_docs import rewrite_links
	pattern = re.compile(r'^((?:\.\./)+)VeroScript/Resources/Scripts/((?:stdlib|examples)(?=[/#]|$))')
	def rebase(url):
		def replace(match):
			up, tail = match.groups()
			return '../' * max(0, up.count('../') - 1) + tail
		return pattern.sub(replace, url)
	return rewrite_links(data.decode('utf-8'), rebase).encode('utf-8')



def release_asset_files(repo: Path):
	"""Source assets keyed by their paths in the complete release."""
	roots = [(module_root(repo) / "Resources/Assets", "Resources/Assets/")]
	scripts = module_root(repo) / "VeroScript/Resources/Scripts"
	roots.extend((scripts / name, "Resources/Scripts/" + name + "/") for name in ("stdlib", "examples"))
	roots.append((module_root(repo) / "Modules/Multimedia/VeroScriptGui/Resources/Examples", "Resources/Examples/Gui/"))
	for source, prefix in roots:
		for file in source.rglob("*"):
			relative = file.relative_to(source)
			if relative.parts[0] in {"dist", "dist-next", "build"} or relative.parts[:2] == ("Resources", "Docs") or any(p in FORBIDDEN for p in relative.parts) or file.suffix in GENERATED | {".pdb"}:
				continue
			if file.is_file():
				yield prefix + relative.as_posix(), file


def sdk_asset_tree(root: Path, *, examples: bool = False) -> None:
	"""Adapt portable asset links to Core SDK examples/scripts and Docs."""
	from veroscript_docs import rewrite_links
	for path in root.rglob("*.md"):
		if path.is_symlink():
			continue
		data = portable_asset_bytes(path)
		if examples:
			data = rewrite_links(data.decode("utf-8"), lambda url: re.sub(r"^((?:\.\./)+)docs/", r"\1../Docs/", url)).encode("utf-8")
			data = re.sub(rb"(?m)^(veroscript(?: --[^\s]+)*) examples/(?!scripts/)", rb"\1 examples/scripts/", data)
			data = data.replace(b"Run an example from the directory that contains the packaged `veroscript` executable:",
								b"With `veroscript` on `PATH`, run from the SDK root (or installer product root):")
		else:
			data = rewrite_links(data.decode("utf-8"), lambda url: re.sub(r"^((?:\.\./)+)examples/(?!scripts/)", r"\1examples/scripts/", url)).encode("utf-8")
		if data != path.read_bytes():
			path.write_bytes(data)


def copy_tree(source: Path, destination: Path, *, exclude_root: tuple[str, ...] = ()) -> None:
	if not source.is_dir():
		raise ValueError(f"Missing source tree: {source}")
	def ignore(directory, names):
		result = {name for name in names if name in FORBIDDEN or Path(name).suffix in GENERATED}
		if Path(directory) == source:
			result.update(set(names) & set(exclude_root))
		return result
	# SDK library symlinks must survive; the audit rejects escaping targets.
	shutil.copytree(source, destination, symlinks=True, dirs_exist_ok=True, ignore=ignore)
	for path in destination.rglob("*.md"):
		if not path.is_symlink():
			original = source / path.relative_to(destination)
			if not original.is_file():
				continue
			data = portable_asset_bytes(original)
			if data != path.read_bytes():
				path.write_bytes(data)


def archive_tree(source: Path, output: Path) -> None:
	output.parent.mkdir(parents=True, exist_ok=True)
	if output.name.endswith(".tar.gz"):
		with tarfile.open(output, "w:gz", dereference=False, compresslevel=6) as archive:
			archive.add(source, arcname=source.name)
	else:
		with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
			for path in sorted(source.rglob("*")):
				if path.is_symlink():
					raise ValueError(f"ZIP cannot represent native SDK links: {path}")
				if path.is_file():
					entry = zipfile.ZipInfo(path.relative_to(source.parent).as_posix(), (1980, 1, 1, 0, 0, 0))
					entry.create_system = 3
					entry.external_attr = (stat.S_IFREG | (0o755 if path.stat().st_mode & 0o111 else 0o644)) << 16
					entry.compress_type = zipfile.ZIP_DEFLATED
					archive.writestr(entry, path.read_bytes(), compresslevel=6)


def native_identity(data: bytes) -> tuple[str, str]:
	if data[:4] == b"\x7fELF" and len(data) >= 20:
		endian = "<" if data[5] == 1 else ">"
		machine = struct.unpack_from(endian + "H", data, 18)[0]
		arch = {62: "x64", 183: "arm64"}.get(machine)
		if arch and data[4] == 2:
			return "linux", arch
	if len(data) >= 8:
		magic = data[:4]
		if magic in {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf"}:
			cpu = struct.unpack_from(("<" if magic[0] == 0xcf else ">") + "I", data, 4)[0]
			arch = {0x01000007: "x64", 0x0100000c: "arm64"}.get(cpu)
			if arch:
				return "macos", arch
	if data[:2] == b"MZ" and len(data) >= 64:
		offset = struct.unpack_from("<I", data, 60)[0]
		if len(data) >= offset + 6 and data[offset:offset+4] == b"PE\0\0":
			machine = struct.unpack_from("<H", data, offset + 4)[0]
			arch = {0x8664: 'x64', 0x14c: 'x86', 0xaa64: 'arm64'}.get(machine)
			if arch:
				return "windows", arch
	raise ValueError("Unrecognized or unsupported native binary architecture")


def safe_member(name: str) -> str:
	path = PurePosixPath(name)
	if not name or "\\" in name or path.is_absolute() or ".." in path.parts or ":" in name:
		raise ValueError(f"Unsafe archive/manifest path: {name}")
	if any(part in FORBIDDEN for part in path.parts) or path.suffix in GENERATED:
		raise ValueError(f"Generated workspace content in release: {name}")
	return path.as_posix()


def archive_entries(path: Path, *, max_entry_bytes: int | None = None) -> dict[str, tuple[bytes, int]]:
	"""Read regular entries, validating paths, symlinks, duplicates and CRCs."""
	entries = {}
	links = {}
	seen = set()
	if path.name.endswith(".tar.gz"):
		with tarfile.open(path, "r:gz") as archive:
			for item in archive:
				name = safe_member(item.name)
				if name in seen:
					raise ValueError(f"Duplicate archive entry: {name}")
				seen.add(name)
				if item.isdir():
					continue
				if item.issym():
					target = PurePosixPath(item.linkname)
					if target.is_absolute() or ".." in target.parts or "\\" in item.linkname:
						raise ValueError(f"Unsafe archive link: {name} -> {item.linkname}")
					links[name] = (PurePosixPath(name).parent / target).as_posix()
					continue
				if not item.isfile():
					raise ValueError(f"Unsupported archive member: {name}")
				stream = archive.extractfile(item)
				entries[name] = (stream.read(-1 if max_entry_bytes is None else max_entry_bytes), item.mode)
			# Tar iteration stops at its end marker before gzip has necessarily
			# read the CRC/size footer. Drain the remaining compressed stream in
			# bounded chunks so corrupt or truncated gzip trailers are rejected.
			while archive.fileobj.read(1024 * 1024):
				pass
	else:
		with zipfile.ZipFile(path) as archive:
			for item in archive.infolist():
				name = safe_member(item.filename.rstrip("/"))
				if name in seen:
					raise ValueError(f"Duplicate archive entry: {name}")
				seen.add(name)
				mode = item.external_attr >> 16
				if stat.S_ISLNK(mode):
					raise ValueError(f"ZIP link is not supported: {name}")
				if not item.is_dir():
					with archive.open(item) as stream:
						data = stream.read(-1 if max_entry_bytes is None else max_entry_bytes)
						if max_entry_bytes is not None:
							# Drain the member in bounded chunks to verify its CRC.
							while stream.read(1024 * 1024):
								pass
						entries[name] = (data, mode)
	def resolve_link(name, chain):
		if name in entries:
			return entries[name]
		if name in chain or name not in links:
			raise ValueError(f"Dangling/cyclic archive link: {name}")
		value = resolve_link(links[name], chain | {name})
		entries[name] = value
		return value
	for name in links:
		resolve_link(name, set())
	return entries


def archive_member_digest(path: Path, member: str) -> str:
	"""Hash a regular archive member without retaining native libraries in memory."""
	if path.name.endswith(".tar.gz"):
		with tarfile.open(path, "r|gz") as archive:
			for item in archive:
				if safe_member(item.name) == member:
					if not item.isfile():
						raise ValueError(f"Expected a regular archive member: {member}")
					with archive.extractfile(item) as stream:
						return hashlib.file_digest(stream, "sha256").hexdigest()
	else:
		with zipfile.ZipFile(path) as archive:
			for item in archive.infolist():
				if safe_member(item.filename.rstrip("/")) == member:
					if item.is_dir() or stat.S_ISLNK(item.external_attr >> 16):
						raise ValueError(f"Expected a regular archive member: {member}")
					with archive.open(item) as stream:
						return hashlib.file_digest(stream, "sha256").hexdigest()
	raise ValueError(f"Missing archive member: {member}")


def write_manifest(root: Path, release_version: str, target: str, build_id: str, *, build_environment: dict | None = None,
				   business_versions: dict | None = None, native_version: str | None = None) -> None:
	artifacts = []
	for path in sorted(root.rglob("*")):
		if path.parent == root and path.name in {"release-manifest.json", "checksums.txt"}:
			continue
		if path.is_file():
			if path.is_symlink():
				raise ValueError(f"Public loose files cannot be symlinks: {path}")
			artifacts.append({"path": path.relative_to(root).as_posix(), "size": path.stat().st_size, "sha256": digest(path)})
	manifest = {"schemaVersion": 2, "product": "VeroScript Release Candidate", "version": release_version,
				"platform": RELEASE_TARGETS[target][0], "architecture": RELEASE_TARGETS[target][1],
				"target": target, "buildId": build_id, "artifacts": artifacts}
	if build_environment is not None:
		manifest["buildEnvironment"] = build_environment
	if business_versions is not None:
		manifest["businessVersions"] = business_versions
		manifest["nativeVersion"] = version(native_version or release_version)
	(root / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
	(root / "checksums.txt").write_text("".join(f"{a['sha256']}  {a['path']}\n" for a in artifacts), encoding="utf-8")


def verify_release(root: Path, repo: Path | None = None, *, businesses: set[str] | None = None,
				   partial_businesses: set[str] | None = None,
				   allow_historical_module_dependencies: bool = False, allow_historical_ide_dependencies: bool = False) -> dict:
	manifest = json.loads((root / "release-manifest.json").read_text(encoding="utf-8-sig"))
	if 'pythonWheelVersions' in manifest:
		from veroscript_python_wheels import validate_versions
		validate_versions(manifest['pythonWheelVersions'])
	v = version(manifest["version"])
	identity = (manifest.get("platform"), manifest.get("architecture"))
	target = next((key for key, value in RELEASE_TARGETS.items() if value == identity), None)
	if manifest.get("schemaVersion") != 2 or manifest.get("product") != "VeroScript Release Candidate" or not target:
		raise ValueError("Invalid release identity")
	if manifest.get("target", target) != target or not re.fullmatch(r"[0-9a-f]{64}", manifest.get("buildId", "")):
		raise ValueError("Invalid target/build identity")
	listed = {}
	checked_directories = set()
	for item in manifest["artifacts"]:
		name = safe_member(item["path"])
		if name != item["path"] or name in listed or name in {"release-manifest.json", "checksums.txt"}:
			raise ValueError(f"Invalid/duplicate manifest path: {name}")
		path = root / name
		parents = [parent for parent in path.parents if parent != root.parent and parent not in checked_directories]
		if linked_path(path) or any(linked_path(parent) for parent in parents):
			raise ValueError(f"Linked release path: {path}")
		checked_directories.update(parents)
		if not path.is_file() or path.stat().st_size != item["size"] or digest(path).lower() != item["sha256"].lower():
			raise ValueError(f"Missing/stale release artifact: {name}")
		listed[name] = item
	actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file() or p.is_symlink()}
	if actual != set(listed) | {"release-manifest.json", "checksums.txt"}:
		raise ValueError("Release contains missing or unlisted files")
	lines = (root / "checksums.txt").read_text(encoding="utf-8-sig").splitlines()
	expected = {f"{a['sha256']}  {a['path']}" for a in listed.values()}
	if len(lines) != len(expected) or set(lines) != expected:
		raise ValueError("Checksum file does not match manifest")
	if target in MOBILE_TARGETS:
		from veroscript_mobile_release import verify_mobile_release
		return verify_mobile_release(root, manifest, repo, businesses=businesses, partial_businesses=partial_businesses)
	extension = ".zip" if identity[0] == "windows" else ".tar.gz"
	from veroscript_business_release import BUSINESSES, ALL_BUSINESSES, PREFIXES, artifact_business, distribution_identity
	versions = manifest.get("businessVersions") or {business: v for business in ALL_BUSINESSES}
	if any(business not in ALL_BUSINESSES for business in versions):
		raise ValueError("Unknown business version")
	for value in versions.values():
		version(value)
	if manifest.get("businessVersions"):
		version(manifest["nativeVersion"])
	declared = manifest.get('selectedBusinesses')
	if declared is not None and (not isinstance(declared, list) or
			any(not isinstance(item, str) for item in declared) or len(declared) != len(set(declared)) or
			not declared or not set(declared).issubset(ALL_BUSINESSES)):
		raise ValueError('Invalid release business selection')
	selected = businesses if businesses is not None else set(declared if declared is not None else BUSINESSES)
	from veroscript_products import target_businesses
	if not selected or not selected.issubset(target_businesses(target, optional=True)):
		raise ValueError("Invalid business selection")
	functional = set(manifest.get('functionalPayloadBusinesses', []))
	if not functional.issubset(ALL_BUSINESSES):
		raise ValueError('Unknown functional payload business')
	if repo:
		functional.update(selected)
	expected_files = {f"{category}/{name}-{versions[PREFIXES[name]]}-{target}{extension}"
					  for category, names in PACKAGES.items() for name in names if PREFIXES[name] in selected}
	installable = set(manifest.get('installableModuleBusinesses', []))
	if not installable.issubset(INSTALLABLE_PACKAGE_NAMES) or not installable.issubset(selected):
		raise ValueError('Invalid installable module business selection')
	expected_files.update(installable_archive(b, versions[b], target) for b in installable)
	from veroscript_module_symbols import MODULES, symbol_archive_name, verify_module_symbols
	module_symbols = {'symbols/' + symbol_archive_name(b, versions[b], target): b for b in selected if b in MODULES}
	# Debug-less builds have no placeholder archive; only registered module symbol identities are optional.
	expected_files.update(name for name in module_symbols if name in listed)
	if 'WebAssembly' in selected:
		expected_files.add(f"webassembly/VeroScriptWebAssemblySDK-{versions['WebAssembly']}-wasm32.zip")
	if 'CSharp' in selected:
		from veroscript_csharp_packages import PACKAGE_IDS
		expected_files.update(f"distributions/csharp/{package}.{versions['CSharp']}.{suffix}"
			for package in PACKAGE_IDS for suffix in ('nupkg', 'snupkg'))
	from veroscript_ide import IDE_ARTIFACTS, verify_ide_artifact
	for business, (prefix, suffix) in IDE_ARTIFACTS.items():
		if business in selected:
			expected_files.add(f"ide/{prefix}-{versions[business]}{suffix}")
	installers = {'installer/' + installer_filename(product, versions[product], target): product
				  for product in PRODUCTS if product in selected}
	expected_files.update(installers)
	actual_products = set(name for name in listed if not name.startswith("Resources/"))
	from veroscript_business_release import artifact_business, distribution_identity
	distributions = {name for name in actual_products if Path(name).name.startswith(('veroscript-', 'VeroScript.CSharp'))}
	csharp_package_hashes = {Path(name).name: listed[name]['sha256'].lower() for name in distributions
		if name.endswith(('.nupkg', '.snupkg'))}
	optional = {name for name in expected_files if artifact_business(name) in (partial_businesses or set())}
	if not expected_files - optional <= actual_products or not actual_products - distributions <= expected_files:
		missing = sorted(expected_files - optional - actual_products)
		extra = sorted(actual_products - distributions - expected_files)
		raise ValueError(f'Unexpected/missing release products: missing={missing}, unexpected={extra}')
	from veroscript_ide import verify_ide_dependencies
	ide_identities = {business: listed[f'ide/{prefix}-{versions[business]}{suffix}']['sha256']
		for business, (prefix, suffix) in IDE_ARTIFACTS.items() if business in selected
		and f'ide/{prefix}-{versions[business]}{suffix}' in listed}
	verify_ide_dependencies(manifest.get('ideDependencies', {}), selected, versions, ide_identities,
		allow_historical=allow_historical_ide_dependencies)
	feature_metadata = {}
	installable_metadata = {}
	for name in actual_products:
		path = root / name
		if name in module_symbols:
			business = module_symbols[name]
			verify_module_symbols(path, business, versions[business], target, peer_directory=root)
			continue
		if name in installers:
			from veroscript_installer import verify_installer_download
			business = installers[name]
			verify_installer_download(path, business, versions[business], target, listed[name]['sha256'], repo)
			continue
		if name.startswith('ide/'):
			business = artifact_business(name)
			verify_ide_artifact(path, business, versions[business], repo, functional_only=business in functional)
			recorded = manifest.get('businessBuildIds', {}).get(business)
			if recorded and recorded != digest(path):
				raise ValueError('IDE artifact build identity mismatch')
			continue
		if name in distributions:
			business, package_version = distribution_identity(path)
			if business in functional:
				verify_functional_payload(archive_entries(path, max_entry_bytes=4096))
			if business not in selected or package_version != versions[business]:
				raise ValueError('Distribution/business release version mismatch')
			if (identity[0] == 'windows' and path.suffix == '.whl' and repo
					and (module_root(repo) / 'Resources/Tools/CMake/WindowsRuntimeNotice.txt').is_file()):
				verify_windows_runtime(archive_entries(path, max_entry_bytes=1024 * 1024), identity)
			continue
		# Native static libraries can exceed a GiB with DWARF. The outer archive
		# hash covers all bytes; member audits need headers and public sources.
		entries = archive_entries(path, max_entry_bytes=1024 * 1024)
		if repo and identity[0] == 'windows':
			from veroscript_icons import PRODUCT_EXECUTABLES, verify_windows_executable_icon
			executables = [member for member in entries if Path(member).name.casefold() in PRODUCT_EXECUTABLES]
			if executables:
				with zipfile.ZipFile(path) as archive:
					for member in executables:
						with archive.open(member) as stream:
							verify_windows_executable_icon(repo, stream, f'{name}/{member}')
		if path.suffix == '.vspkg':
			business = artifact_business(name)
			if business == 'Vm':
				from veroscript_vm import verify_installable_backend
				verify_installable_backend(entries, versions[business], target)
			elif business == 'Jit':
				from veroscript_jit import verify_installable_backend
				verify_installable_backend(entries, versions[business], target)
			elif business in FOUNDATION_PACKAGE_NAMES:
				from veroscript_foundation_packages import verify_installable_foundation
				metadata = verify_installable_foundation(entries, business, versions[business], target)
				recorded = manifest.get('businessBuildIds', {}).get(business)
				if recorded and recorded != metadata['payloadId']:
					raise ValueError('Installable foundation/business payload identity mismatch')
			else:
				from veroscript_module_packages import verify_installable_entries
				installable_metadata[business] = verify_installable_entries(entries, business, versions[business], target)
			if repo and entries['LICENSE'][0].decode('utf-8-sig').replace('\r\n', '\n').strip() != (repo / 'LICENSE').read_text(encoding='utf-8-sig').strip():
				raise ValueError('Stale installable module license')
			continue
		prefix = path.name.removesuffix(".tar.gz").removesuffix(".zip") + "/"
		if not entries or (not name.startswith("installer/") and any(not key.startswith(prefix) for key in entries)):
			raise ValueError(f"Archive root mismatch: {name}")
		if artifact_business(name) in MODULE_BUSINESSES and not name.startswith('installer/') :
			verify_module_payload(key[len(prefix):] for key in entries)
		if artifact_business(name) in functional and not name.startswith('installer/'):
			verify_functional_payload(key[len(prefix):] for key in entries)
		if name.startswith('languages/VeroScriptCSharp-'):
			for member_name in entries:
				package_name = Path(member_name).name
				if package_name in csharp_package_hashes and archive_member_digest(path, member_name) != csharp_package_hashes[package_name]:
					raise ValueError('SDK offline NuGet differs from the canonical package: ' + package_name)
		if not name.startswith("installer/"):
			notices = [entries[key][0] for key in (prefix + "LICENSE", prefix + "Runtime/LICENSE") if key in entries]
			if not notices or any(not notice.strip() for notice in notices):
				raise ValueError(f"Missing/empty project license: {name}")
			if repo and (repo / "LICENSE").is_file():
				expected_notice = (repo / "LICENSE").read_text(encoding="utf-8-sig").strip()
				if any(notice.decode("utf-8-sig").replace("\r\n", "\n").strip() != expected_notice for notice in notices):
					raise ValueError(f"Stale project license: {name}")
		if identity[0] == 'windows':
			for member, (data, _) in entries.items():
				filename = member.rsplit('/', 1)[-1].casefold()
				if (filename in WINDOWS_NATIVE_NAMES or filename.endswith('.pyd')) and native_identity(data[:4096]) != identity:
					raise ValueError(f'Windows SDK native architecture mismatch: {name}/{member}')
			# Recorded historical collections retain their original contract.
			# Publication from current sources must include current dependencies.
			if repo and (module_root(repo) / 'Resources/Tools/CMake/WindowsRuntimeNotice.txt').is_file():
				verify_windows_runtime(entries, identity)
		if name.startswith("sdk/"):
			library = "lib/VeroScript.lib" if identity[0] == "windows" else "lib/libVeroScript.a"
			if archive_member_digest(path, prefix + library) != manifest["buildId"]:
				raise ValueError("Core library build identity mismatch")
		if name.startswith(('runtime/', 'sdk/')):
			from veroscript_packages import verify_package_client_payload
			verify_package_client_payload(entries, prefix, 'core-sdk' if name.startswith('sdk/') else 'runtime',
										  core_version=versions['CoreSDK'] if name.startswith('sdk/') else None,
										  build_id=manifest['buildId'])
			from veroscript_applications import verify_application_payload
			verify_application_payload(path, entries, prefix, 'core-sdk' if name.startswith('sdk/') else 'runtime',
				identity, versions.get('CoreSDK'), manifest['buildId'], manifest.get('nativeVersion', v))
			vm_library = "VeroScriptVmBytecode.dll" if identity[0] == "windows" else "libVeroScriptVmBytecode." + ("so" if identity[0] == "linux" else "dylib")
			vm_path = ("bin/" if name.startswith('sdk/') else "") + "Vm/" + vm_library
			vm_data = entries.get(prefix + vm_path, (b'', 0))[0]
			if not vm_data or native_identity(vm_data[:4096]) != identity:
				raise ValueError(f"Missing/wrong bundled VM backend: {name}/{vm_path}")
			if name.startswith('sdk/'):
				static_vm = "VeroScriptBytecodeVm.lib" if identity[0] == "windows" else "libVeroScriptBytecodeVm.a"
				for required in ("lib/" + static_vm, "include/Vm/VeroScriptBytecodeVm.h",
								 "lib/cmake/VeroScriptBytecodeVm/VeroScriptBytecodeVmConfig.cmake",
								 "lib/cmake/VeroScriptExecutionContract/VeroScriptExecutionContractConfig.cmake"):
					if not entries.get(prefix + required, (b'', 0))[0]:
						raise ValueError(f"Missing VM SDK entry: {name}/{required}")
		if name.startswith("runtime/"):
			if repo:
				from veroscript_icons import verify_runtime_icon_entries
				verify_runtime_icon_entries(repo, entries, prefix, target)
			suffix = ".exe" if identity[0] == "windows" else ""
			for tool in ("veroscript", "veroscript-lsp", "veroscript-dap"):
				data, mode = entries[prefix + tool + suffix]
				if native_identity(data[:4096]) != identity or (identity[0] != "windows" and not mode & 0o111):
					raise ValueError(f"Runtime binary identity/permissions mismatch: {tool}")
		if name.startswith("jit/"):
			shared = "VeroScriptJitAsmJit.dll" if identity[0] == "windows" else "libVeroScriptJitAsmJit." + ("so" if identity[0] == "linux" else "dylib")
			for required in (shared, "licenses/asmjit/LICENSE.md", "licenses/asmjit/VERSION.txt"):
				if prefix + required not in entries or not entries[prefix + required][0]:
					raise ValueError(f"Missing independent JIT payload: {name}/{required}")
			if native_identity(entries[prefix + shared][0][:4096]) != identity:
				raise ValueError(f"JIT backend architecture mismatch: {name}")
		if name.startswith("vm/"):
			shared = "VeroScriptVmBytecode.dll" if identity[0] == "windows" else "libVeroScriptVmBytecode." + ("so" if identity[0] == "linux" else "dylib")
			for required in (shared, "LICENSE", "veroscript-package.json"):
				if prefix + required not in entries or not entries[prefix + required][0]:
					raise ValueError(f"Missing independent VM payload: {name}/{required}")
			if native_identity(entries[prefix + shared][0][:4096]) != identity:
				raise ValueError(f"VM backend architecture mismatch: {name}")
		if name.startswith("modules/"):
			from veroscript_modules import verify_module_entries
			business = artifact_business(name)
			file_digests = None
			if business in FOUNDATION_PACKAGE_NAMES:
				from veroscript_foundation import archive_digests
				file_digests = {key[len(prefix):]: value for key, value in archive_digests(path).items()}
			feature_metadata[business] = verify_module_entries({key[len(prefix):]: value for key, value in entries.items()},
								  business, versions[business], target, repo, file_digests=file_digests)
			if business in FOUNDATION_PACKAGE_NAMES:
				recorded = manifest.get('businessBuildIds', {}).get(business)
				if recorded and recorded != feature_metadata[business]['payloadId']:
					raise ValueError('Foundation/business payload identity mismatch: ' + business)
		if name.startswith("webassembly/"):
			for required in WEBASSEMBLY_FILES:
				if prefix + required not in entries or not entries[prefix + required][0]:
					raise ValueError(f"Missing/empty WebAssembly SDK entry in {name}: {required}")
		if identity[0] != "windows" and not name.startswith("installer/"):
			relative_entries = {key[len(prefix):]: value for key, value in entries.items()}
			def require(*paths):
				for required in paths:
					if required not in relative_entries:
						raise ValueError(f"Missing Unix SDK entry in {name}: {required}")
			shared = "libVeroScriptInterop." + ("so" if identity[0] == "linux" else "dylib")
			if name.startswith("runtime/"):
				require("lib/" + shared, "include/VeroScript/VeroScriptInterop.h", "stdlib/core.vs", "localization/en-US/shared.json")
			elif name.startswith("sdk/"):
				require("lib/libVeroScript.a", "lib/libVeroScriptTooling.a", "lib/cmake/VeroScript/VeroScriptConfig.cmake", "include/Script/VeroScript.h",
						"assets/stdlib/core.vs", "assets/localization/en-US/shared.json")
				if manifest["buildId"].encode() not in relative_entries["lib/cmake/VeroScript/VeroScriptConfig.cmake"][0]:
					raise ValueError("Core SDK build identity mismatch")
			elif name.startswith("interop/"):
				require("lib/" + shared, "include/VeroScript/VeroScriptInterop.h", "lib/cmake/VeroScriptInterop/VeroScriptInteropConfig.cmake")
			elif name.startswith("languages/"):
				for module in ("Runtime", "Tools", "Aot", "Symbols"):
					if not any(key.startswith(module + "/") for key in relative_entries):
						raise ValueError(f"Missing language module: {name}/{module}")
				if "VeroScriptCpp-" in name:
					require("Runtime/lib/libVeroScriptCpp.a", "Runtime/lib/cmake/VeroScriptCpp/VeroScriptCppConfig.cmake", "Runtime/include/Cpp/VeroScriptSession.h")
					cpp_build_id = manifest.get('businessBuildIds', {}).get('Cpp', manifest['buildId'])
					if cpp_build_id.encode() not in relative_entries["Runtime/lib/cmake/VeroScriptCpp/VeroScriptCppConfig.cmake"][0]:
						raise ValueError("C++ SDK build identity mismatch")
				if "VeroScriptCSharp-" in name:
					for framework in ("net8.0", "net9.0"):
						require(f"Runtime/lib/dotnet/{framework}/VeroScriptCSharp.dll", f"Runtime/lib/dotnet/{framework}/{shared}")
				if "VeroScriptPython-" in name:
					facades = [key for key in relative_entries if key.startswith("Runtime/") and key.endswith("/veroscript/__init__.py")]
					if len(facades) != 1:
						raise ValueError("Missing/ambiguous Python ABI package")
					python_prefix = facades[0].removesuffix("__init__.py")
					require(python_prefix + "_veroscript.pyi", python_prefix + "py.typed", python_prefix + shared)
					if not any(key.startswith(python_prefix + "_veroscript.") and key.endswith(".so") for key in relative_entries):
						raise ValueError("Missing Python native extension")
			elif name.startswith("symbols/"):
				if not any(key.endswith(".debug") if identity[0] == "linux" else ".dSYM/" in key for key in relative_entries):
					raise ValueError("Missing platform-native debug symbols")
			for key, (data, mode) in relative_entries.items():
				if data[:4] == b"\x7fELF" or data[:4] in {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf"}:
					# Separate .debug files deliberately have no executable payload.
					if not key.endswith(".debug") and ".dSYM/" not in key and native_identity(data[:4096]) != identity:
						raise ValueError(f"Wrong native target in SDK: {name}/{key}")
					if (not key.endswith(".debug") and ".dSYM/" not in key
							and "/bin/" in "/" + key
							and not re.search(r"\.(so(?:\.\d+)*|dylib)$", key)
							and not mode & 0o111):
						raise ValueError(f"SDK tool is not executable: {name}/{key}")
		if repo:
			sources = {"sdk/": [("include/Script", "VeroScript/Public"), ("include/Tooling", "VeroScriptTooling/Public")],
					   "interop/": [("include/VeroScript", "VeroScriptInterop/Public")],
					   "languages/VeroScriptCpp-": [("Runtime/include/Cpp", "Extensions/Language/Cpp/VeroScriptCpp/Public"),
												   ("Tools/include/CppFrontend", "Extensions/Language/Cpp/VeroScriptCppFrontend/Public"),
												   ("Tools/include/CppBindGen", "Extensions/Language/Cpp/VeroScriptCppBindGen/Public"),
												   ("Tools/include/CppTranspiler", "Extensions/Language/Cpp/VeroScriptCppTranspiler/Public"),
												   ("Aot/include/CppAot", "Extensions/Language/Cpp/VeroScriptCppAot/Public")],
					   "webassembly/": [("", "Extensions/Platform/WebAssembly/VeroScriptWebAssembly/Bindings")]}
			if name.startswith('sdk/'):
				sources['sdk/'].append(('include/Vm', 'Modules/Execution/VeroScriptBytecodeVm/Public'))
				if (prefix + 'bin/veroscript-component.json' in entries or
					prefix + 'lib/cmake/VeroScriptPackageRuntime/VeroScriptPackageRuntimeConfig.cmake' in entries):
					sources['sdk/'].append(('include/PackageManager', 'VeroScriptPackageManager/Public'))
				for product, folder in (('VeroScriptApplication', 'Application'), ('VeroScriptBuild', 'Build')):
					if prefix + f'lib/cmake/{product}/{product}Config.cmake' in entries:
						sources['sdk/'].append(('include/' + folder, product + '/Public'))
						sources['sdk/'].append(('share/VeroScript/' + folder.lower() + '/Resources/Schemas', product + '/Resources/Schemas'))
				if prefix + 'lib/cmake/VeroScriptPackageManager/VeroScriptPackageManagerConfig.cmake' in entries:
					sources['sdk/'].append(('share/VeroScript/package-manager/Resources/Schemas', 'VeroScriptPackageManager/Resources/Schemas'))
			for category, pairs in sources.items():
				if not name.startswith(category):
					continue
				for dest, source in pairs:
					source_root = module_root(repo) / source
					source_files = set()
					for file in source_root.rglob("*"):
						if file.is_file() and file.suffix not in GENERATED:
							if (file.name == 'VeroScriptApplicationPackages.h' and
								prefix + 'lib/cmake/VeroScriptApplicationPackages/VeroScriptApplicationPackagesConfig.cmake' not in entries):
								continue
							key = prefix + (dest + "/" if dest else "") + file.relative_to(source_root).as_posix()
							source_files.add(key)
							if key not in entries or entries[key][0] != file.read_bytes():
								raise ValueError(f"Stale public SDK source: {key}")
					if source == "VeroScript/Public" and source_files:
						key = prefix + dest + "/VeroScriptJitBuild.h"
						source_files.add(key)
						abi = re.findall(rb'^#define VEROSCRIPT_JIT_PUBLIC_ABI "([0-9a-f]{64})"\r?$',
										 entries.get(key, (b"", 0))[0], re.MULTILINE)
						if abi != [core_public_abi(source_root).encode("ascii")]:
							raise ValueError(f"Missing/stale Core JIT public ABI header: {key}")
					if dest and {key for key in entries if key.startswith(prefix + dest + "/")} != source_files:
						raise ValueError(f"Unexpected public headers in {name}/{dest}")
	for business, metadata in installable_metadata.items():
		if metadata != feature_metadata.get(business):
			raise ValueError('Installable module and SDK payload identities differ')
		if metadata['payloadId'] != manifest.get('businessBuildIds', {}).get(business):
			raise ValueError('Installable module/business payload identity mismatch')
	network_metadata = feature_metadata.get('Network', {})
	network_identity = network_metadata.get('networkImplementationId')
	for business, metadata in feature_metadata.items():
		uses_selected_network = (business == 'Network' or
			metadata.get('dependencies', {}).get('Network') == network_metadata.get('version'))
		if uses_selected_network and network_identity and metadata.get('networkImplementationId') != network_identity:
			raise ValueError('Module Network foundation implementations differ: ' + business)
		for dependency, dependency_version in metadata.get('dependencies', {}).items():
			if (dependency in feature_metadata and feature_metadata[dependency]['version'] != dependency_version
					and not allow_historical_module_dependencies):
				raise ValueError('Module dependency versions differ: ' + business + '/' + dependency)
	for business in FOUNDATION_PACKAGE_NAMES:
		metadata = feature_metadata.get(business)
		if not metadata:
			continue
		for dependency, dependency_version in metadata['dependencies'].items():
			if dependency in feature_metadata:
				selected_dependency = feature_metadata[dependency]
				if (selected_dependency['version'] == dependency_version and
						selected_dependency['payloadId'] != metadata['dependencyBuildIds'][dependency]):
					raise ValueError('Foundation packages use different dependency builds')
			elif dependency == 'CoreSDK' and dependency in selected:
				core_build = manifest.get('businessBuildIds', {}).get('CoreSDK', manifest['buildId'])
				if versions['CoreSDK'] == dependency_version and core_build != metadata['dependencyBuildIds']['CoreSDK']:
					raise ValueError('Foundation bindings use a different Core SDK build')
			elif dependency == 'Cpp' and dependency in selected:
				cpp_build = manifest.get('businessBuildIds', {}).get('Cpp', manifest['buildId'])
				if versions['Cpp'] == dependency_version and cpp_build != metadata['dependencyBuildIds']['Cpp']:
					raise ValueError('Foundation bindings use a different C++ Runtime SDK build')
	if 'Python' in selected:
		wheel_versions = manifest.get('pythonWheelVersions')
		if repo and (module_root(repo) / 'Extensions/Language/Python/pyproject.toml').is_file():
			from veroscript_python_wheels import supported_versions
			declared = supported_versions(repo)
			if wheel_versions and tuple(wheel_versions) != declared:
				raise ValueError('Recorded Python wheel matrix differs from declared support')
			wheel_versions = declared
		if wheel_versions:
			from veroscript_python_wheels import verify_matrix
			verify_matrix([root / name for name in distributions], repo, target, versions['Python'], tuple(wheel_versions),
				require_complete='Python' not in (partial_businesses or set()))
	if repo and 'Resources' in selected and 'Resources' not in (partial_businesses or set()):
		expected_resources = {relative: hashlib.sha256(portable_asset_bytes(file)).hexdigest()
							  for relative, file in release_asset_files(repo)}
		from veroscript_docs import publication_bundle, verify_publication
		expected_resources.update({"Resources/Docs/" + relative: hashlib.sha256(data).hexdigest() for relative, data in publication_bundle(repo, 'User').items()})
		verify_publication(repo, root / "Resources/Docs", 'User')
		if set(expected_resources) != {key for key in listed if key.startswith("Resources/")}:
			raise ValueError("Source/release resource inventory mismatch")
		for name, hash_value in expected_resources.items():
			if hash_value.lower() != listed[name]["sha256"].lower():
				raise ValueError(f"Stale release resource: {name}")
	return manifest


def stage_localization(repo: Path, destination: Path, profile: str = "runtime") -> None:
	"""Assemble only the catalogs owned by a product's dependency modules."""
	subprocess.run(["cmake", f"-DVEROSCRIPT_SOURCE_ROOT={repo}",
					f"-DVEROSCRIPT_MODULE_ROOT={module_root(repo)}",
					f"-DVEROSCRIPT_LOCALIZATION_OUTPUT={destination}",
					f"-DVEROSCRIPT_LOCALIZATION_PROFILE={profile}",
					"-P", str(module_root(repo) / "Resources/Tools/CMake/VeroScriptLocalization.cmake")], check=True)


def publish(repo: Path, candidate: Path, destination: Path) -> None:
	destination = safe_output(repo, destination)
	identity = verify_release(candidate, repo)
	previous_manifest = destination / "release-manifest.json"
	if previous_manifest.is_file():
		previous = json.loads(previous_manifest.read_text(encoding="utf-8-sig"))
		if any(previous.get(key) != identity.get(key) for key in ("version", "platform", "architecture")):
			raise ValueError(f"Release output belongs to a different version/target: {destination}")
	backup = scratch_directory(repo) / ("veroscript-release-backup-" + uuid.uuid4().hex)
	destination.parent.mkdir(parents=True, exist_ok=True)
	moved = False
	def move(source, target):
		for attempt in range(7):
			try:
				source.rename(target)
				return
			except PermissionError:
				if os.name != "nt" or attempt == 6:
					raise
				time.sleep(min(0.2 * 2**attempt, 2))
	if destination.exists():
		move(destination, backup)
		moved = True
	try:
		move(candidate, destination)
	except BaseException:
		if moved:
			move(backup, destination)
		raise
	# Keep a recoverable previous release. Cleanup is a separate explicit action.
