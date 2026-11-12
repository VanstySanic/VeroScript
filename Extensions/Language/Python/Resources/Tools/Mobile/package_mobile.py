# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build application-ready CPython extension slices for Android or iOS."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import uuid

from mobile_sdk import ROOT, inspect_sdk
from verify_mobile import audit_slice, verify_package
from veroscript_mobile import strip_mobile_debug


def select_matrix(descriptors: list[Path], target_platform: str) -> list[dict]:
	sdks = []
	for descriptor in descriptors:
		nonce = uuid.uuid4().hex
		record = inspect_sdk(descriptor, build_nonce=nonce)
		record["buildNonce"] = nonce
		record["descriptor"] = str(descriptor.resolve())
		sdks.append(record)
	expected = ({("arm64-v8a", "device"), ("x86_64", "device")} if target_platform == "android"
				else {("arm64", "device"), ("arm64", "simulator"), ("x86_64", "simulator")})
	actual = [(sdk["architecture"], sdk["variant"]) for sdk in sdks]
	if any(sdk["platform"] != target_platform for sdk in sdks) or set(actual) != expected or len(actual) != len(expected):
		raise ValueError(f"Expected exactly one target SDK for each slice: {sorted(expected)}")
	if len({sdk["pythonVersion"] for sdk in sdks}) != 1:
		raise ValueError("All mobile slices must use the same CPython version")
	return sdks


def build_commands(sdk: dict, build: Path, stage: Path, host_python: str, cmake: str, ndk: Path | None) -> list[list[str]]:
	# FindPython3 requires an absolute interpreter artifact. CMake resolves a
	# relative toolchain path against its source/build tree, not the caller's cwd.
	executable = shutil.which(host_python)
	if executable is None:
		raise ValueError(f"Cannot locate host Python executable: {host_python}")
	host_python = str(Path(executable).absolute())
	command = [cmake, "-S", str(ROOT), "-B", str(build),
			   "-DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=OFF", "-DVS_BUILD_VEROSCRIPT_PYTHON_ONLY=ON",
			   "-DVS_BUILD_VEROSCRIPT_PYTHON=ON", "-DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF",
			   "-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF", "-DVEROSCRIPT_INTEROP_STATIC=ON",
			   "-DVS_VEROSCRIPT_PYTHON_PACKAGE_BUILD=OFF", "-DBUILD_TESTING=OFF", "-DVS_ENABLE_IPO=OFF",
			   "-DCMAKE_BUILD_TYPE=Release", "-DPython3_EXECUTABLE=" + host_python,
			   "-DVEROSCRIPT_PYTHON_BUILD_NONCE=" + sdk["buildNonce"],
			   "-DVEROSCRIPT_PYTHON_TARGET_SDK=" + sdk["descriptor"]]
	if sdk["platform"] == "android":
		command += ["-DVEROSCRIPT_BUILD_APPLICATION=ON"]
		if ndk is None:
			raise ValueError("Android extension builds require --ndk")
		ndk = ndk.resolve()
		command += ["-G", "Ninja", "-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"),
					"-DANDROID_ABI=" + sdk["architecture"], "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_shared"]
	else:
		command += ["-G", "Xcode", "-DCMAKE_SYSTEM_NAME=iOS",
					"-DCMAKE_OSX_SYSROOT=" + ("iphoneos" if sdk["variant"] == "device" else "iphonesimulator"),
					"-DCMAKE_OSX_ARCHITECTURES=" + sdk["architecture"], "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0",
					"-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO"]
	return [command, [cmake, "--build", str(build), "--config", "Release", "--target", "VeroScriptPython"],
			[cmake, "--install", str(build), "--config", "Release", "--component", "VeroScriptPython", "--prefix", str(stage)]]


def android_runtime(ndk: Path, architecture: str) -> Path:
	triple = "aarch64-linux-android" if architecture == "arm64-v8a" else "x86_64-linux-android"
	candidates = list((ndk / "toolchains/llvm/prebuilt").glob(f"*/sysroot/usr/lib/{triple}/libc++_shared.so"))
	if len(candidates) != 1:
		raise ValueError(f"Cannot identify one NDK libc++_shared.so for {architecture}")
	return candidates[0]


def install_ios_markers(stage: Path, sdk: dict) -> None:
	# CPython's AppleFrameworkLoader recognizes the generic .fwork suffix.
	# Both simulator architectures must share one origin in the fat framework.
	name = "_veroscript.fwork"
	binary = "veroscript._veroscript"
	framework = stage / "Frameworks" / (binary + ".framework")
	marker = stage / "python/veroscript" / name
	marker.write_text(f"Frameworks/{binary}.framework/{binary}\n", encoding="utf-8")
	(framework / (binary + ".origin")).write_text(f"python/veroscript/{name}\n", encoding="utf-8")


def package(sdks: list[dict], output: Path, host_python: str, cmake: str, ndk: Path | None) -> None:
	if output.exists() or output.is_symlink():
		raise ValueError("Choose a new mobile Python output directory")
	resolved = output.resolve()
	if resolved == ROOT or resolved in ROOT.parents or resolved == Path(resolved.anchor):
		raise ValueError("Output cannot replace a source directory or filesystem root")
	target_platform = sdks[0]["platform"]
	if target_platform == "ios" and platform.system() != "Darwin":
		raise ValueError("An actual iOS build requires macOS and full Xcode")
	if target_platform == "android" and (ndk is None or not (ndk / "build/cmake/android.toolchain.cmake").is_file()):
		raise ValueError("An actual Android build requires a valid Android NDK")
	output.parent.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory(prefix=".veroscript-python-mobile-", dir=output.parent) as directory:
		work = Path(directory)
		result = work / "package"
		result.mkdir()
		records = []
		for sdk in sdks:
			# Validate the selected HOST interpreter, never invoke target Python.
			subprocess.run([host_python, str(Path(__file__).with_name("mobile_sdk.py")),
							"--sdk", sdk["descriptor"], "--check-host"], check=True, stdout=subprocess.DEVNULL)
			stage = result / "slices" / sdk["target"]
			for command in build_commands(sdk, work / ("build-" + sdk["target"]), stage, host_python, cmake, ndk):
				subprocess.run(command, check=True)
			if target_platform == "android":
				if ndk is None:
					raise ValueError("Android extension builds require --ndk")
				shutil.copy2(android_runtime(ndk, sdk["architecture"]), stage / "jniLibs" / sdk["architecture"] / "libc++_shared.so")
				tools = [path for name in ('llvm-strip', 'llvm-strip.exe')
					for path in (ndk / 'toolchains/llvm/prebuilt').glob('*/bin/' + name) if path.is_file()]
				if len(tools) != 1:
					raise ValueError('Android Python stripping requires one target llvm-strip')
				for library in (stage / 'jniLibs' / sdk['architecture']).glob('*.so'):
					strip_mobile_debug(library, 'android', tools[0])
			else:
				strip_mobile_debug(stage / 'Frameworks/veroscript._veroscript.framework/veroscript._veroscript', 'ios')
				install_ios_markers(stage, sdk)
			record = {key: sdk[key] for key in ("target", "platform", "architecture", "variant", "minimumVersion",
											   "pythonVersion", "productVersion", "extensionSuffix", "buildId", "sdkFingerprint")}
			audit_slice(stage, record)
			records.append(record)
		if target_platform == "ios":
			command = ["xcodebuild", "-create-xcframework"]
			simulator = work / "simulator/veroscript._veroscript.framework"
			arm = next(sdk for sdk in sdks if sdk["variant"] == "simulator" and sdk["architecture"] == "arm64")
			intel = next(sdk for sdk in sdks if sdk["variant"] == "simulator" and sdk["architecture"] == "x86_64")
			source = result / "slices" / arm["target"] / "Frameworks/veroscript._veroscript.framework"
			shutil.copytree(source, simulator)
			subprocess.run(["xcrun", "lipo", "-create", str(source / "veroscript._veroscript"),
							str(result / "slices" / intel["target"] / "Frameworks/veroscript._veroscript.framework/veroscript._veroscript"),
							"-output", str(simulator / "veroscript._veroscript")], check=True)
			# Generic markers are identical for both simulator architectures.
			device = next(sdk for sdk in sdks if sdk["variant"] == "device")
			command += ["-framework", str(result / "slices" / device["target"] / "Frameworks/veroscript._veroscript.framework"),
						"-framework", str(simulator), "-output", str(result / "VeroScriptPython.xcframework")]
			subprocess.run(command, check=True)
		shutil.copy2(ROOT / "LICENSE", result / "LICENSE")
		shutil.copy2(ROOT / "Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md", result / "THIRD-PARTY-NOTICES.md")
		if ndk is not None:
			notice = ndk / "NOTICE"
			if not notice.is_file():
				raise ValueError("NDK NOTICE is required when redistributing libc++_shared.so")
			shutil.copy2(notice, result / "ANDROID-NDK-NOTICE.txt")
		shutil.copy2(Path(__file__).with_name("device_smoke.py"), result / "device_smoke.py")
		files = {path.relative_to(result).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
				 for path in sorted(result.rglob("*")) if path.is_file()}
		metadata = {"schemaVersion": 1, "product": "VeroScript Python Mobile", "slices": records, "files": files,
					"version": records[0]["productVersion"],
					"deviceValidation": "pending", "includesCPythonRuntime": False, "nativeJit": False}
		(result / "python-mobile.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
		verify_package(result)
		if output.exists() or output.is_symlink():
			raise ValueError("Mobile output directory was created during the build; choose a new destination")
		result.rename(output)


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("platform", choices=("android", "ios"))
	parser.add_argument("--sdk", type=Path, action="append", required=True)
	parser.add_argument("--out-root", type=Path)
	parser.add_argument("--host-python", default=sys.executable)
	parser.add_argument("--cmake", default="cmake")
	parser.add_argument("--ndk", type=Path)
	parser.add_argument("--plan", action="store_true", help="Validate target SDKs and display commands without building")
	args = parser.parse_args()
	try:
		sdks = select_matrix(args.sdk, args.platform)
		if args.plan:
			for sdk in sdks:
				for command in build_commands(sdk, Path("build") / sdk["target"], Path("stage") / sdk["target"],
											  args.host_python, args.cmake, args.ndk):
					print(json.dumps(command))
		else:
			if args.out_root is None:
				raise ValueError("--out-root is required for a build")
			package(sdks, args.out_root.absolute(), args.host_python, args.cmake, args.ndk)
			print(f"Built mobile Python SDK: {args.out_root}; device/application acceptance remains required")
	except (OSError, ValueError, SyntaxError, subprocess.CalledProcessError) as error:
		parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
	main()
