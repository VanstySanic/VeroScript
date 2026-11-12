# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Consume installed application/build/read-only SDK exports without the HTTP client."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	for name in ("repo", "build", "cmake", "generator", "config", "core", "tooling"):
		parser.add_argument("--" + name, required=True)
	parser.add_argument("--platform", default="")
	args = parser.parse_args()
	repo, build = Path(args.repo), Path(args.build)

	def run(*values):
		subprocess.run([str(value) for value in values], check=True, timeout=150)

	with tempfile.TemporaryDirectory(prefix="vero-application-sdk-") as temporary:
		root = Path(temporary)
		sdk = root / "sdk"
		for component in ("VeroScriptApplication", "VeroScriptPackageRuntime", "VeroScriptApplicationPackages", "VeroScriptBuild"):
			run(args.cmake, "--install", build, "--prefix", sdk, "--config", args.config, "--component", component)
		for module, folder, names in (
			("VeroScriptApplication", "application", ("json/LICENSE.MIT", "miniz/LICENSE", "sha256/LICENSE", "sources.json")),
			("VeroScriptBuild", "build", ("json/LICENSE.MIT", "sources.json"))):
			for name in names:
				installed = sdk / "licenses" / folder / name
				if not installed.is_file() or installed.read_bytes() != (repo / module / "ThirdParty" / name).read_bytes():
					raise AssertionError(f"SDK is missing the owning module's dependency notice: {module}/{name}")
		for module, folder, library in (("VeroScript", "Script", args.core), ("VeroScriptTooling", "Tooling", args.tooling)):
			shutil.copytree(repo / module / "Public", sdk / "include" / folder)
			shutil.copy2(library, sdk / "lib" / Path(library).name)
		shutil.copy2(build / "VeroScript/Generated/VeroScriptJitBuild.h", sdk / "include/Script")
		for component in ("VeroScriptExecutionContract", "VeroScriptBytecodeVmSdk", "VeroScriptRuntimeVm"):
			prefix = sdk / "bin" if component == "VeroScriptRuntimeVm" else sdk
			run(args.cmake, "--install", build, "--prefix", prefix, "--config", args.config, "--component", component)
		core = Path(args.core)
		with core.open("rb") as stream:
			identity = hashlib.file_digest(stream, "sha256").hexdigest()
		config = sdk / "lib/cmake/VeroScript"
		config.mkdir(parents=True)
		template = (repo / "VeroScript/Resources/Tools/CMake/VeroScriptConfig.cmake.in").read_text(encoding="utf-8")
		for key, value in {"VERSION": "0.1.1", "BUILD_ID": identity, "STATIC_PREFIX": "lib" if core.name.startswith("lib") else "", "STATIC_SUFFIX": core.suffix}.items():
			template = template.replace("@" + key + "@", value)
		(config / "VeroScriptConfig.cmake").write_text(template, encoding="utf-8")
		run(args.cmake, "-DOUTPUT_FILE:FILEPATH=" + str(config / "VeroScriptConfigVersion.cmake"),
			"-DPACKAGE_SEMANTIC_VERSION:STRING=0.1.1", "-DPACKAGE_POINTER_SIZE:STRING=" + str(struct.calcsize("P")),
			"-P", repo / "Resources/Tools/CMake/write-cmake-package-version.cmake")
		options = ["-G", args.generator, "-DCMAKE_BUILD_TYPE=" + args.config, "-DCMAKE_PREFIX_PATH=" + str(sdk)]
		if args.platform:
			options += ["-A", args.platform]
		consumer = root / "consumer"
		run(args.cmake, "-S", repo / "VeroScriptApplication/Resources/Examples/Embed", "-B", consumer, *options)
		if any("CURL" in path.name for path in (sdk / "lib/cmake").iterdir()):
			raise AssertionError("Read-only SDK unexpectedly installed HTTP dependencies")
		for project in consumer.glob("*.vcxproj"):
			if "libcurl" in project.read_text(encoding="utf-8").lower():
				raise AssertionError("Read-only SDK consumer links the HTTP client")
		run(args.cmake, "--build", consumer, "--config", args.config, "--parallel", "4")
		executable = consumer / ("application-consumer.exe" if core.suffix == ".lib" else "application-consumer")
		if not executable.is_file():
			executable = consumer / args.config / executable.name
		run(executable)
		(config / "VeroScriptConfig.cmake").write_text(template.replace(identity, "0" * 64), encoding="utf-8")
		wrong = subprocess.run([str(args.cmake), "-S", str(repo / "VeroScriptApplication/Resources/Examples/Embed"),
								"-B", str(root / "wrong-core"), *options], capture_output=True, text=True)
		if wrong.returncode == 0 or "matching Core SDK" not in wrong.stdout + wrong.stderr:
			raise AssertionError("SDK accepted a different Core library identity")
	print("Installed Application, Build and PackageRuntime SDK integration passed without HTTP.")


if __name__ == "__main__":
	main()
