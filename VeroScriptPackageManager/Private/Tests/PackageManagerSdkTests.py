# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build and run a consumer using only an installed package-manager/Core SDK."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def main():
	parser = argparse.ArgumentParser()
	for name in ("source", "build", "cmake", "generator", "config", "core", "core-generated", "tooling", "cli"):
		parser.add_argument("--" + name, required=True)
	parser.add_argument("--platform", default="")
	args = parser.parse_args()
	source, build = Path(args.source), Path(args.build)

	def run(*items):
		subprocess.run([str(item) for item in items], check=True)

	with tempfile.TemporaryDirectory(prefix="vspm-sdk-") as temporary:
		base = Path(temporary)
		stage = base / "sdk"
		run(args.cmake, "--install", build, "--config", args.config, "--prefix", stage,
			"--component", "VeroScriptPackageManager")
		# Authored guides and examples are published separately through Resources.
		# The installed SDK keeps functional schema and dependency assets.
		resources = stage / "share/VeroScript/package-manager"
		for relative in ("README.md", "Resources/Docs", "Resources/Examples"):
			if (resources / relative).exists():
				raise RuntimeError("SDK contains auxiliary documentation/examples: " + relative)
		for schema in (source / "VeroScriptPackageManager/Resources/Schemas").glob("*.json"):
			deployed = resources / "Resources/Schemas" / schema.name
			if not deployed.is_file() or deployed.read_bytes() != schema.read_bytes():
				raise RuntimeError("SDK has a missing or stale functional schema: " + schema.name)
		# The client component includes its actual bundled curl, regardless of
		# which module created the dependency. It must relocate with the SDK.
		for module, folder, library in (("VeroScript", "Script", args.core),
										("VeroScriptTooling", "Tooling", args.tooling)):
			shutil.copytree(source / module / "Public", stage / "include" / folder)
			shutil.copy2(library, stage / "lib" / Path(library).name)
		shutil.copy2(Path(args.core_generated) / "VeroScriptJitBuild.h", stage / "include/Script")
		for component in ("VeroScriptExecutionContract", "VeroScriptBytecodeVmSdk", "VeroScriptRuntimeVm"):
			prefix = stage / "bin" if component == "VeroScriptRuntimeVm" else stage
			run(args.cmake, "--install", build, "--prefix", prefix, "--config", args.config, "--component", component)
		core = Path(args.core)
		sha = hashlib.sha256(core.read_bytes()).hexdigest()
		config = stage / "lib/cmake/VeroScript"
		config.mkdir(parents=True)
		template = (source / "VeroScript/Resources/Tools/CMake/VeroScriptConfig.cmake.in").read_text(
			encoding="utf-8")
		for key, value in {"VERSION": "0.1.1", "BUILD_ID": sha,
						   "STATIC_PREFIX": "lib" if core.name.startswith("lib") else "",
						   "STATIC_SUFFIX": core.suffix}.items():
			template = template.replace("@" + key + "@", value)
		config_file = config / "VeroScriptConfig.cmake"
		config_file.write_text(template, encoding="utf-8")
		run(args.cmake, "-DOUTPUT_FILE:FILEPATH=" + str(config / "VeroScriptConfigVersion.cmake"),
			"-DPACKAGE_SEMANTIC_VERSION:STRING=0.1.1",
			"-DPACKAGE_POINTER_SIZE:STRING=" + str(struct.calcsize("P")), "-P",
			source / "Resources/Tools/CMake/write-cmake-package-version.cmake")
		consumer = source / "VeroScriptPackageManager/Resources/Examples/Embed"
		# Single-config generators choose the runtime ABI during configure;
		# multi-config generators still select it through --config when building.
		options = ["-G", args.generator, "-DCMAKE_PREFIX_PATH=" + str(stage),
			"-DCMAKE_BUILD_TYPE=" + args.config]
		if args.platform:
			options += ["-A", args.platform]
		run(args.cmake, "-S", consumer, "-B", base / "consumer", *options)
		run(args.cmake, "--build", base / "consumer", "--config", args.config, "--parallel", "4")
		local = base / "local"
		shutil.copytree(source / "VeroScriptPackageManager/Resources/Examples/Local", local)
		sdk_cli = stage / "bin" / Path(args.cli).name
		sdk_cli.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(args.cli, sdk_cli)
		spec = importlib.util.spec_from_file_location("vero_package_assets",
			source / "Resources/Tools/Packaging/veroscript_packages.py")
		assets = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(assets)
		assets.stage_package_client(source, sdk_cli, stage, "core-sdk")
		project = base / "sdk-default-project"
		project.mkdir()
		probe = subprocess.run([str(sdk_cli), "env", "--json"], cwd=project, check=True,
							   capture_output=True, text=True, encoding="utf-8")
		selected = json.loads(probe.stdout)
		if selected["scope"] != "project" or not Path(selected["root"]).samefile(project):
			raise RuntimeError("SDK CLI default did not select the startup project")
		subprocess.run([str(sdk_cli), "install", str(local / "library"), "--offline"], cwd=project, check=True)
		if not (project / ".veroscript/packages").is_dir() or (stage / ".veroscript").exists():
			raise RuntimeError("SDK CLI default installation changed its component")
		probe = subprocess.run([str(sdk_cli), "env", "--root", "--json"], cwd=base, check=True,
							   capture_output=True, text=True, encoding="utf-8")
		if not Path(json.loads(probe.stdout)["root"]).samefile(stage):
			raise RuntimeError("SDK CLI --root did not select its own component")
		run(sdk_cli, "install", local / "library", "--root", "--offline")
		if not (stage / ".veroscript/packages").is_dir():
			raise RuntimeError("SDK shared installation was not created under its component root")
		probe = subprocess.run([args.cli, "env", "--root", "--path", str(stage), "--json"],
							   cwd=base, check=True, capture_output=True, text=True, encoding="utf-8")
		if not Path(json.loads(probe.stdout)["root"]).samefile(stage):
			raise RuntimeError("explicit SDK component path was not selected")
		runtime = base / "runtime"
		runtime.mkdir()
		runtime_cli = runtime / Path(args.cli).name
		shutil.copy2(args.cli, runtime_cli)
		assets.stage_package_client(source, runtime_cli, runtime, "runtime")
		run(runtime_cli, "install", local / "library", "--root", "--offline")
		if not (runtime / "Packages").is_dir():
			raise RuntimeError("Runtime packaging helper did not select its shared component")
		run(args.cli, "install", "--path=" + str(local / "app"), "--offline")
		name = "package-manager-embed" + (".exe" if core.suffix == ".lib" else "")
		executable = base / "consumer" / args.config / name
		if not executable.is_file():
			executable = base / "consumer" / name
		run(executable, local / "app", local / "app/main.vs")
		# A package environment must explicitly authorize the SDK standard
		# library roots; source-tree auto-discovery is not an SDK contract.
		stdlib = stage / "assets/stdlib"
		shutil.copytree(source / "VeroScript/Resources/Scripts/stdlib", stdlib)
		stdlib_entry = local / "app/stdlib-main.vs"
		stdlib_entry.write_text(
			'import "core.vs";\nimport "example/math.vs";\n'
			'int main() { print(std::clamp_int(answer(), 0, 100)); return 0; }\n',
			encoding="utf-8")
		probe = subprocess.run([str(executable), str(local / "app"), str(stdlib_entry), str(stdlib)],
							   cwd=base, check=True, capture_output=True, text=True, encoding="utf-8")
		if probe.stdout.strip() != "42":
			raise RuntimeError("installed SDK standard-library example returned an unexpected value")
		# Use source examples with the installed CLI and C++ SDK consumer: the
		# package registers its annotation policy before ordinary compilation.
		compiler = base / "compiler-example"
		shutil.copytree(source / "VeroScriptPackageManager/Resources/Examples/Compiler", compiler)
		compiler_app = compiler / "app"
		subprocess.run([str(sdk_cli), "install", "--offline"], cwd=compiler_app, check=True)
		probe = subprocess.run(
			[str(executable), str(compiler_app), str(compiler_app / "main.vs"), str(stdlib)],
			cwd=base, capture_output=True, text=True, encoding="utf-8")
		if probe.returncode or probe.stdout.strip() != "42":
			raise RuntimeError("installed SDK compiler-package annotation example failed: "
							   f"exit {probe.returncode}\n{probe.stdout}\n{probe.stderr}")
		# Exercise the static SDK's complete dependency closure inside a shared
		# host as well. Executable-only consumers do not detect missing PIC.
		shared_source = base / "shared-host-source"
		shared_source.mkdir()
		(shared_source / "CMakeLists.txt").write_text(
			'cmake_minimum_required(VERSION 3.25)\n'
			'project(PackageSharedHost LANGUAGES CXX)\n'
			'find_package(VeroScriptPackageManager 0.1.1 EXACT CONFIG REQUIRED)\n'
			'add_library(package-shared-host SHARED host.cpp)\n'
			'set_target_properties(package-shared-host PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)\n'
			'target_link_libraries(package-shared-host PRIVATE VeroScript::VeroScriptPackageManager)\n'
			'target_compile_features(package-shared-host PRIVATE cxx_std_17)\n'
			'add_executable(package-shared-consumer consumer.cpp)\n'
			'target_link_libraries(package-shared-consumer PRIVATE package-shared-host)\n',
			encoding="utf-8")
		(shared_source / "host.cpp").write_text(
			'#include "VeroScriptPackageManager.h"\n'
			'int CheckPackageSharedHost(const char* root) {\n'
			'  using namespace VeroScript::Packages;\n'
			'  FPackageManager manager; FEnvironmentInput input; input.Root = root;\n'
			'  FDiagnostic error; std::shared_ptr<const FInstalledEnvironment> installed;\n'
			'  return manager.Load(input, installed, error) && installed ? 0 : 1;\n'
			'}\n', encoding="utf-8")
		(shared_source / "consumer.cpp").write_text(
			'int CheckPackageSharedHost(const char*);\n'
			'int main(int argc, char** argv) {\n'
			'  return argc == 2 ? CheckPackageSharedHost(argv[1]) : 2;\n'
			'}\n', encoding="utf-8")
		shared_build = base / "shared-host-build"
		run(args.cmake, "-S", shared_source, "-B", shared_build, *options)
		run(args.cmake, "--build", shared_build, "--config", args.config, "--parallel", "4")
		shared_name = "package-shared-consumer" + (".exe" if core.suffix == ".lib" else "")
		shared_executable = shared_build / args.config / shared_name
		if not shared_executable.is_file():
			shared_executable = shared_build / shared_name
		run(shared_executable, local / "app")
		config_file.write_text(template.replace(sha, "0" * 64), encoding="utf-8")
		mismatch = subprocess.run([args.cmake, "-S", str(consumer), "-B", str(base / "mismatch"),
								   *options], capture_output=True, text=True, encoding="utf-8",
								  errors="replace")
		if mismatch.returncode == 0 or "matching Core SDK" not in mismatch.stdout + mismatch.stderr:
			raise RuntimeError("installed SDK accepted a different Core build")
	print("Installed SDK consumer, local package import and Core build identity checks passed.")


if __name__ == "__main__":
	main()
