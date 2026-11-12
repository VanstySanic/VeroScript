#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build and audit independent Android AAR or iOS XCFramework SDKs (0.1.1)."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

from veroscript_paths import repository_root
from veroscript_release import safe_output, core_public_abi
from veroscript_mobile import verify_android_aar, verify_android_jit_aar, verify_ios_archive, mobile_catalog_files, stage_android_aar, strip_mobile_debug
from veroscript_products import clean_functional_payload
from veroscript_business_release import load_versions


def find_archive(build: Path, name: str) -> Path:
	library_root = build / "lib"
	candidates = [path for path in library_root.rglob("lib" + name + ".a")
				  if any(part in {"Release", "Release-iphoneos", "Release-iphonesimulator"}
						 for part in path.parent.relative_to(library_root).parts)]
	if len(candidates) != 1:
		raise ValueError(f"Expected exactly one Release archive for {name}: {candidates}")
	return candidates[0]


def aot_source_contract(repo: Path) -> dict[str, str]:
	implementation = [path for path in (repo / "VeroScript/Private").rglob("*")
					  if path.suffix in {".cpp", ".h", ".inl"} and "Tests" not in path.parts]
	if not implementation:
		raise ValueError("Missing Core implementation for target AOT headers")
	implementation.extend(repo / "Shared/Private" / name
		for name in ("VeroScriptFloatParsing.h", "VeroScriptFloatFormatting.h", "VeroScriptJsonString.h"))
	implementation.sort(key=lambda path: path.as_posix())
	identity = hashlib.sha256("".join(hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
									 for path in implementation).encode("ascii")).hexdigest()
	return {"VEROSCRIPT_JIT_PUBLIC_ABI": core_public_abi(repo / "VeroScript/Public"),
			"VEROSCRIPT_CORE_IMPLEMENTATION_ID": identity}


def matching_aot_header(header: Path, contract: dict[str, str]) -> bool:
	data = header.read_bytes()
	return all(re.findall(rb'^#define ' + name.encode("ascii") + rb' "([0-9a-f]{64})"\r?$', data, re.MULTILINE)
			   == [expected.encode("ascii")] for name, expected in contract.items())


def stage_aot_headers(repo: Path, generated_headers: list[Path], stage: Path) -> dict:
	"""Ship C++ service declarations; applications still link one Interop runtime."""
	public = repo / "VeroScript/Public"
	contract = aot_source_contract(repo)
	# Validate every target slice before writing the shared header bundle.
	if not generated_headers or not all(matching_aot_header(header, contract) for header in generated_headers):
		raise ValueError("Stale target AOT service headers; rebuild all mobile slices")
	if any(path.is_symlink() or getattr(path, "is_junction", lambda: False)() for path in public.rglob("*")):
		raise ValueError("Linked Core AOT service headers")
	destination = stage / "aot/include"
	shutil.copytree(public, destination)
	shutil.copy2(generated_headers[0], destination / "VeroScriptJitBuild.h")
	return {"profile": "core-linked-aot", "includeDirectory": "aot/include",
			"coreImplementation": contract["VEROSCRIPT_CORE_IMPLEMENTATION_ID"],
			"publicAbi": contract["VEROSCRIPT_JIT_PUBLIC_ABI"]}


def android(repo: Path, stage: Path, gradle: str, with_asmjit: bool = False, *, versions: dict | None = None) -> dict:
	versions = versions or load_versions(repo)
	project = repo / "Extensions/Platform/Mobile/VeroScriptAndroid"
	executable = shutil.which(gradle)
	if executable is None:
		raise FileNotFoundError(f"Cannot find Gradle executable: {gradle}")
	subprocess.run([executable, "--project-dir", str(project), "--no-daemon", "assembleRelease"], check=True)
	archives = list((project / "build/outputs/aar").glob("*-release.aar"))
	if len(archives) != 1:
		raise ValueError("Expected one Android release AAR")
	aar = stage / f"VeroScriptAndroid-{versions['nativeVersion']}.aar"
	stage_android_aar(archives[0], aar)
	result = verify_android_aar(aar, repo / "VeroScript/Resources/Scripts/stdlib", mobile_catalog_files(repo),
							   repo / "VeroScriptInterop/Public/VeroScriptInterop.h")
	metadata = {"platform": "android", "minimumApi": 26, "nativeLibraries": result,
				"deviceValidation": "required: physical device, emulator, 4 KB and 16 KB pages"}
	generated = list(project.rglob("VeroScriptJitBuild.h"))
	headers = []
	contract = aot_source_contract(repo)
	for abi in ("arm64-v8a", "x86_64"):
		candidates = [path for path in generated if abi in path.parts]
		if not candidates:
			raise ValueError(f"Missing Android target AOT service headers: {abi}")
		# Several Gradle configurations may remain. Only the current source
		# contract is relevant; the generated file contains no ABI-specific flags.
		header = next((path for path in sorted(candidates) if matching_aot_header(path, contract)), None)
		if header is None:
			raise ValueError(f"Stale Android target AOT service headers: {abi}")
		headers.append(header)
	metadata["aotHeaders"] = stage_aot_headers(repo, headers, stage)
	if with_asmjit:
		backend = repo / "Modules/Execution/VeroScriptAsmJit/Resources/Tools/Android"
		subprocess.run([executable, "--project-dir", str(backend), "--no-daemon",
			'-PveroscriptAsmJitVersion=' + versions['businesses']['Jit'], "assembleRelease"], check=True)
		candidates = list((backend / "build/outputs/aar").glob("*-release.aar"))
		if len(candidates) != 1:
			raise ValueError("Expected one optional Android AsmJit release AAR")
		libraries = verify_android_jit_aar(candidates[0])
		shutil.copy2(candidates[0], stage / f"vs-asmjit-{versions['businesses']['Jit']}-android.aar")
		metadata["jitBackend"] = {"package": "vs-asmjit", "backendId": "asmjit", "nativeLibraries": libraries,
								  "validation": "required: governed machine-code execution on target devices"}
	return metadata


def ios(repo: Path, stage: Path, cmake: str) -> dict:
	if platform.system() != "Darwin":
		raise ValueError("iOS packaging requires macOS and Xcode; a macOS ARM64 archive is not an iOS archive")
	records = {}
	archives = {}
	generated_headers = []
	for sdk, architecture, cmake_arch in (("iphoneos", "arm64", "arm64"),
										  ("iphonesimulator", "arm64", "arm64"),
										  ("iphonesimulator", "x64", "x86_64")):
		identity = sdk + "-" + architecture
		build = repo / "build" / ("mobile-" + identity)
		subprocess.run([cmake, "-S", str(repo), "-B", str(build), "-G", "Xcode",
						"-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=" + sdk,
						"-DCMAKE_OSX_ARCHITECTURES=" + cmake_arch, "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0",
						"-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO", "-DVS_ENABLE_IPO=OFF",
						"-DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON", "-DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=ON",
						"-DVS_BUILD_VEROSCRIPT_PYTHON_ONLY=OFF", "-DVS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY=OFF",
						"-DVEROSCRIPT_BUILD_PACKAGE_MANAGER=OFF", "-DVEROSCRIPT_BUILD_BUILD_SYSTEM=OFF",
						"-DVEROSCRIPT_BUILD_APPLICATION=OFF", "-DVEROSCRIPT_BUILD_PACKAGE_RUNTIME=OFF",
						"-DVEROSCRIPT_ENABLE_ASMJIT=OFF",
						"-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF", "-DVEROSCRIPT_BUILD_VM_BACKEND=ON",
						"-DVEROSCRIPT_INTEROP_DEFAULT_VM=ON", "-DVEROSCRIPT_INTEROP_STATIC=ON", "-DBUILD_TESTING=OFF"], check=True)
		subprocess.run([cmake, "--build", str(build), "--config", "Release", "--target", "VeroScriptIOS"], check=True)
		generated_headers.append(build / "VeroScript/Generated/VeroScriptJitBuild.h")
		libraries = [find_archive(build, name) for name in
					 ("VeroScriptIOS", "VeroScriptMobile", "VeroScriptInterop", "VeroScriptCpp", "VeroScript", "VeroScriptBytecodeVm")]
		archive = stage / (identity + ".a")
		subprocess.run(["xcrun", "libtool", "-static", "-o", str(archive), *map(str, libraries)], check=True)
		strip_mobile_debug(archive, 'ios')
		records[identity] = {"objects": verify_ios_archive(archive, architecture, sdk == "iphonesimulator")}
		archives[identity] = archive
	simulator = stage / "ios-simulator.a"
	subprocess.run(["xcrun", "lipo", "-create", str(archives["iphonesimulator-arm64"]),
					str(archives["iphonesimulator-x64"]), "-output", str(simulator)], check=True)
	headers = stage / "headers"
	shutil.copytree(repo / "Extensions/Platform/Mobile/VeroScriptIOS/Public", headers)
	shutil.copy2(repo / "VeroScriptInterop/Public/VeroScriptInterop.h", headers)
	subprocess.run(["xcodebuild", "-create-xcframework", "-library", str(archives["iphoneos-arm64"]),
					"-headers", str(headers), "-library", str(simulator), "-headers", str(headers),
					"-output", str(stage / "VeroScriptIOS.xcframework")], check=True)
	for archive in (*archives.values(), simulator):
		archive.unlink()
	shutil.rmtree(headers)
	shutil.copytree(repo / "VeroScript/Resources/Scripts/stdlib", stage / "assets/stdlib")
	for relative, data in mobile_catalog_files(repo).items():
		path = stage / "assets/localization" / relative
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)
	aot_headers = stage_aot_headers(repo, generated_headers, stage)
	return {"platform": "ios", "minimumOS": "15.0", "slices": records, "aotHeaders": aot_headers,
			"deviceValidation": "required: simulator, physical device, signing and application integration"}


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("platform", choices=("android", "ios"))
	parser.add_argument("--repo-root", type=Path, default=repository_root())
	parser.add_argument("--out-root", type=Path, required=True)
	parser.add_argument('--versions-file', type=Path)
	parser.add_argument("--gradle", default="gradle", help="Gradle 8.11.1 executable for the pinned Android project")
	parser.add_argument("--cmake", default="cmake")
	parser.add_argument("--with-asmjit", action="store_true", help="Build the separate optional Android backend AAR")
	args = parser.parse_args()
	if args.with_asmjit and args.platform != "android":
		parser.error("--with-asmjit is supported only for Android")
	repo = args.repo_root.resolve()
	versions = load_versions(repo, args.versions_file)
	from veroscript_mobile_release import source_native_version
	if versions['nativeVersion'] != source_native_version(repo):
		parser.error('Configured nativeVersion must match VeroScriptVersion.h')
	output = safe_output(repo, repo / args.out_root, component=True)
	if output.exists():
		raise ValueError("Choose a new mobile SDK output directory")
	output.parent.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory(prefix="veroscript-mobile-", dir=output.parent) as temporary:
		stage = Path(temporary) / "package"
		stage.mkdir()
		metadata = android(repo, stage, args.gradle, args.with_asmjit, versions=versions) if args.platform == "android" else ios(repo, stage, args.cmake)
		metadata.update({"product": "VeroScript Mobile SDK", "version": versions['nativeVersion'],
						 "nativeJit": "jitBackend" in metadata, "defaultExecution": "vm"})
		(stage / "mobile-sdk.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
		shutil.copy2(repo / "LICENSE", stage / "LICENSE")
		clean_functional_payload(stage)
		stage.rename(output)
	print(f"Mobile SDK: {output}; device validation remains required")


if __name__ == "__main__":
	main()
