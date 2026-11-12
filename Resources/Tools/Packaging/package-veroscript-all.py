#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Assemble and validate a complete native Linux/macOS release.

"""
from __future__ import annotations
from veroscript_packages import stage_package_client, stage_package_manager_sdk, finalize_package_manager_sdk
from veroscript_applications import stage_application_runtime
import argparse
import json
import os
import platform
from pathlib import Path
from veroscript_paths import module_root, repository_root, module_route, source_route
import re
import shutil
import subprocess
import sys
import tempfile
from xml.sax.saxutils import escape, quoteattr

from veroscript_release import (TARGETS, WEBASSEMBLY_FILES, archive_tree, binary_prefix, copy_tree, digest, host_target,
							   jit_identity, native_identity, publish, safe_output, scratch_directory, sdk_asset_tree, portable_asset_bytes, stage_localization, verify_release, version, write_manifest)

from veroscript_docs import publish_docs, check_collection
from veroscript_business_release import BUSINESSES, ALL_BUSINESSES, IDE_BUSINESSES, PREFIXES, load_versions, collection_root, publish_businesses, verify_collection
from veroscript_ide import check_ide_sources, check_ide_prerequisites, stage_ide
from veroscript_native_symbols import LinuxNativeSymbols, MacNativeSymbols
from veroscript_jit import stage_backend, pack_backend as pack_jit_backend
from veroscript_vm import stage_runtime as stage_vm_runtime, stage_sdk as stage_vm_sdk, stage_backend as stage_vm_backend, pack_backend as pack_vm_backend
from veroscript_products import ASMJIT_PACKAGE_NAME, BYTECODE_VM_PACKAGE_NAME, clean_functional_payload, FEATURE_MODULE_BUSINESSES, SDK_MODULE_BUSINESSES, MODULE_PACKAGE_NAMES
from veroscript_installer import package_unix_installer
from veroscript_modules import assemble_modules
from veroscript_module_consumers import verify_consumers
from veroscript_icons import stage_runtime_icons, verify_runtime_bundle
from veroscript_csharp_packages import stage_csharp_packages

TOOLS = ("veroscript", "veroscript-lsp", "veroscript-dap")
BUILD_TARGETS = ("VeroScriptCli", "VeroScriptLsp", "VeroScriptDap", "VeroScriptInterop", "VeroScriptRuntime", "VeroScriptLauncher",
				 "VeroScriptCppBindGen", "VeroScriptCppTranspiler", "VeroScriptCppAot",
				 "VeroScriptCSharpAot", "VeroScriptPythonAot", "VeroScriptCSharpPackage",
				 "VeroScriptCSharpToolsPackage", "VeroScriptCSharpTranspilerPackage", "VeroScriptPython",
				 "VeroScriptPythonFrontend", "VeroScriptPythonBindGen", "VeroScriptPythonTranspiler", "VeroScriptAsmJitBackend",
				 "VeroScriptJsonModule", "VeroScriptNetworkModule", "VeroScriptHttpModule", "VeroScriptWebSocketModule", "VeroScriptBoostModule", "VeroScriptCurlModule", "VeroScriptModulePackageTool",
				 "VeroScriptPlatformBindings", "VeroScriptGraphicsBindings", "VeroScriptAudioBindings", "VeroScriptSdl3",
				 "VeroScriptImageBindings", "VeroScriptFontBindings", "VeroScriptStbModule", "VeroScriptFontBackendModule",
				 "VeroScriptGuiScript", "VeroScriptGuiToolsModule", "VeroScriptRmlUiModule",
				 "VeroScriptBytecodeVm", "VeroScriptBytecodeVmBackend")


def run(*arguments, cwd=None, env=None, capture=False):
	command = [str(arg) for arg in arguments]
	print("+ " + " ".join(command), flush=True)
	result = subprocess.run(command, cwd=cwd, env=env, check=True,
							text=True, stdout=subprocess.PIPE if capture else None)
	return result.stdout if capture else None


def cache(path: Path) -> dict[str, str]:
	result = {}
	for line in (path / "CMakeCache.txt").read_text(encoding='utf-8').splitlines():
		if "=" in line and ":" in line and not line.startswith(("#", "//")):
			key, value = line.split("=", 1)
			result[key.split(":", 1)[0]] = value
	return result


def copy_file(source: Path, destination: Path):
	if not source.is_file():
		raise ValueError(f"Required release file is missing: {source}")
	destination.parent.mkdir(parents=True, exist_ok=True)
	shutil.copy2(source, destination)
	if source.suffix.lower() == ".md":
		destination.write_bytes(portable_asset_bytes(source))


class Release:
	def __init__(self, args, repo, stage, build, target):
		self.args, self.repo, self.stage, self.build, self.target = args, repo, stage, build, target
		self.root, self.work = stage / "release", stage / "work"
		self.root.mkdir()
		self.work.mkdir()
		self.platform, self.arch = TARGETS[target]
		self.versions = args.release_versions['businesses']
		self.ide_build_ids = {}
		self.ide_dependencies = {}
		self.module_build_ids = {}
		self.module_symbol_status = {}
		self.v = self.versions['CoreSDK']
		self.bin = build / "bin"
		if not (self.bin / "veroscript").is_file():
			self.bin /= "Release"
		self.lib = build / "lib"
		if not (self.lib / "libVeroScript.a").is_file():
			self.lib /= "Release"
		self.build_id = digest(self.lib / "libVeroScript.a")
		self.shared = "libVeroScriptInterop." + ("so" if self.platform == "linux" else "dylib")
		self.interop = next((p for p in (self.lib / self.shared, self.bin / self.shared) if p.is_file()), None)
		if not self.interop:
			raise ValueError("Missing native Interop runtime")
		self.settings = cache(build)
		self.package_client = self.settings.get('VEROSCRIPT_BUILD_PACKAGE_MANAGER', 'OFF').upper() in {'ON', '1', 'TRUE', 'YES'}
		self.application = self.settings.get('VEROSCRIPT_BUILD_APPLICATION', 'OFF').upper() in {'ON', '1', 'TRUE', 'YES'}
		self.package_runtime = self.package_client or self.settings.get('VEROSCRIPT_BUILD_PACKAGE_RUNTIME', 'OFF').upper() in {'ON', '1', 'TRUE', 'YES'}
		self.build_system = self.settings.get('VEROSCRIPT_BUILD_BUILD_SYSTEM', 'OFF').upper() in {'ON', '1', 'TRUE', 'YES'}
		self.native_symbols = (LinuxNativeSymbols(self.work / 'native-release',
							   self.settings.get('CMAKE_OBJCOPY', 'objcopy'),
							   self.settings.get('CMAKE_OBJDUMP', 'objdump'))
							   if self.platform == 'linux' else MacNativeSymbols(self.work / 'native-release',
								   self.settings.get('CMAKE_STRIP', 'strip')))
		if self.native_symbols:
			core = self.native_symbols.prepare(self.lib / 'libVeroScript.a')
			self.build_id = digest(core[0] if core else self.lib / 'libVeroScript.a')
		interpreter = self.settings.get("Python3_EXECUTABLE", self.settings.get("_Python3_EXECUTABLE", "python3"))
		self.build_environment = {
			"compiler": run(self.settings["CMAKE_CXX_COMPILER"], "--version", capture=True).splitlines()[0],
			"system": platform.platform(),
			"libc": list(platform.libc_ver()),
			"python": json.loads(run(interpreter, "-c", "import json,sys,sysconfig; print(json.dumps({'version':sys.version.split()[0],'soabi':sysconfig.get_config_var('SOABI')}))", capture=True)),
			"configuration": "Release",
			"jitBackend": "asmjit" if self.settings.get("VEROSCRIPT_BUILD_ASMJIT_BACKEND", "ON") in {"ON", "1", "TRUE"} else "none",
			"asmjit": self.settings.get("VEROSCRIPT_ASMJIT_NATIVE_ENABLED", "OFF"),
		}

	def licenses(self, path):
		copy_file(self.repo / "LICENSE", path / "LICENSE")

	def tree(self, source, destination):
		copy_tree(module_root(self.repo) / source, destination)

	def install(self, component, destination):
		run("cmake", "--install", self.build, "--config", "Release", "--prefix", destination,
			"--component", component)

	def config(self, name, destination):
		has_asmjit = int(self.build_environment["asmjit"] in {"ON", "1", "TRUE"})
		package_version = self.versions['Cpp' if name == 'cpp' else 'CoreSDK']
		values = {"VERSION": package_version, "CORE_VERSION": self.v, "BUILD_ID": self.build_id, "STATIC_PREFIX": "lib", "STATIC_SUFFIX": ".a",
				  "EXE_SUFFIX": "", "HAS_ASMJIT": str(has_asmjit), "JIT_TEST_HOOKS": "0",
				  "APPLICATION_ENABLED": str(int(self.settings.get("VEROSCRIPT_BUILD_APPLICATION", "OFF") == "ON")),
				  "ASMJIT_X64": str(int(has_asmjit and self.arch == "x64")),
				  "ASMJIT_A64": str(int(has_asmjit and self.arch == "arm64"))}
		product = "VeroScriptCpp" if name == "cpp" else "VeroScript"
		owner = "Extensions/Language/Cpp/VeroScriptCpp" if name == "cpp" else "VeroScript"
		text = (module_root(self.repo) / owner / "Resources/Tools/CMake" / f"{product}Config.cmake.in").read_text(encoding='utf-8')
		for key, value in values.items():
			text = text.replace("@" + key + "@", value)
		if re.search(r"@[A-Z_]+@", text):
			raise ValueError("Unexpanded SDK configuration token")
		directory = destination / "lib/cmake" / product
		directory.mkdir(parents=True, exist_ok=True)
		(directory / f"{product}Config.cmake").write_text(text, encoding='utf-8')
		run("cmake", f"-DOUTPUT_FILE:FILEPATH={directory / (product + 'ConfigVersion.cmake')}",
			f"-DPACKAGE_SEMANTIC_VERSION:STRING={package_version}", "-DPACKAGE_POINTER_SIZE:STRING=8",
			"-P", module_root(self.repo) / "Resources/Tools/CMake/write-cmake-package-version.cmake")

	def consumer(self, sdk, sample, name, prefixes=None):
		output = self.work / (name + "-consumer")
		business = 'Cpp' if name.startswith('cpp') else ('Interop' if name == 'interop' else 'CoreSDK')
		abi_arguments = []
		if name == 'interop':
			abi_source = module_root(self.repo) / 'VeroScriptInterop/Private/Tests/VeroScriptInteropCHeaderTest.c'
			abi_arguments.append(f'-DVEROSCRIPT_SDK_ABI_TEST_SOURCE:FILEPATH={abi_source}')
		sample_sources = {'core': 'VeroScript/Resources/Examples/EmbedMinimal',
						  'core-annotation': 'VeroScript/Resources/Examples/AnnotationProcessing',
						  'core-script-extensions': 'VeroScript/Resources/Examples/ScriptCompileExtensions',
						  'interop': 'VeroScriptInterop/Resources/Examples/InteropMinimal',
						  'cpp': 'Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal',
						  'cpp-installed': 'Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal'}
		run("cmake", "-S", module_root(self.repo) / sample_sources[name], "-B", output, "-G", "Ninja",
			f"-DCMAKE_PREFIX_PATH={';'.join(str(p) for p in (prefixes or [sdk]))}",
			f"-DVEROSCRIPT_SDK_PACKAGE_VERSION:STRING={self.versions[business]}",
			f"-DVEROSCRIPT_NATIVE_VERSION:STRING={self.args.release_versions['nativeVersion']}",
			"-DCMAKE_BUILD_TYPE=Release", *abi_arguments)
		run("cmake", "--build", output, "--parallel", self.args.jobs)
		run("ctest", "--test-dir", output, "--output-on-failure", "--no-tests=error")

	def csharp_consumer(self, runtime):
		for framework in ("net8.0", "net9.0"):
			project = self.work / ("csharp-consumer-" + framework)
			project.mkdir()
			libraries = runtime / "lib/dotnet" / framework
			(project / "Consumer.csproj").write_text(
				f'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>{framework}</TargetFramework></PropertyGroup><ItemGroup>'
				f'<Reference Include="VeroScriptCSharp"><HintPath>{escape(str(libraries / "VeroScriptCSharp.dll"))}</HintPath></Reference>'
				f'<Content Include={quoteattr(str(libraries / self.shared))}><Link>{self.shared}</Link><CopyToOutputDirectory>Always</CopyToOutputDirectory></Content>'
				'</ItemGroup></Project>', encoding="utf-8")
			(project / "Program.cs").write_text(
				'using System; using VeroScript;\nusing var session = new VeroScriptSession();\n'
				'if (!session.LoadSource("int answer() { return 42; }").Success) throw new Exception("C# SDK load failed");\n'
				'var call = session.CallFunction("answer");\nusing var value = call.ReturnValue;\n'
				'if (!call.Result.Success || value.GetInt32() != 42) throw new Exception("C# SDK native call failed");\n'
				'Console.WriteLine("Packaged C# SDK native consumer passed");\n', encoding="utf-8")
			run("dotnet", "run", "--project", project / "Consumer.csproj", "-c", "Release")

	def archive(self, category, name, source):
		self.licenses(source)
		package_version = self.versions[PREFIXES[name]]
		output = self.root / category / f"{name}-{package_version}-{self.target}.tar.gz"
		# Stable root name independent of the scratch directory.
		portable = self.work / f"{name}-{package_version}-{self.target}"
		try:
			if source != portable:
				copy_tree(source, portable)
			clean_functional_payload(portable)
			archive_tree(portable, output)
		finally:
			# The original stage remains available for installer assembly and
			# diagnostics; the archive root copy is no longer an input afterwards.
			if source != portable and portable.exists():
				shutil.rmtree(portable)

	def symbols(self, destination, sources):
		destination.mkdir(parents=True, exist_ok=True)
		for source in sources:
			if self.native_symbols:
				self.native_symbols.export_symbols(source, destination)
			elif source.suffix == ".a":
				# Static archives retain DWARF in their object files.
				copy_file(source, destination / source.name)
			else:
				run("dsymutil", source, "-o", destination / (source.name + ".dSYM"))

	def prepare_native_tree(self, root, symbols=None):
		if self.native_symbols:
			self.native_symbols.prepare_tree(root, symbols)

	def installer(self, profile, name, components):
		package_version = self.versions[name]
		tool = module_root(self.repo) / "VeroScriptInstaller/Tool/VeroScriptInstaller.Tool.csproj"
		installer_project_root = tool.parents[1]
		bundle = self.work / (profile + "-bundle")
		command = ["dotnet", "run", "--project", tool, "-c", "Release", "--", "build-bundle",
				   "--output", bundle, "--version", package_version, "--profile", profile,
				   "--platform", self.platform, "--architecture", self.arch]
		for component, source in components.items():
			if component in ('docs', 'examples'):
				continue
			command += ["--source-" + component, source]
		run(*command, cwd=installer_project_root)
		if profile == 'runtime':
			verify_runtime_bundle(self.repo, bundle, self.target)
		run("dotnet", "run", "--project", tool, "-c", "Release", "--", "verify", "--bundle", bundle, cwd=installer_project_root)
		embedded = self.work / (profile + "-bundle.zip")
		run("dotnet", "run", "--project", tool, "-c", "Release", "--", "pack-bundle", "--bundle", bundle, "--output", embedded, cwd=installer_project_root)
		expected_payload = digest(embedded)
		# The verified ZIP is now the sole publish input. Release its expanded
		# copy before self-contained validation expands the same payload again.
		shutil.rmtree(bundle)
		rid = self.target.replace("macos-", "osx-")
		published = self.work / (profile + "-publish")
		run("dotnet", "publish", module_root(self.repo) / "VeroScriptInstaller/App/VeroScriptInstaller.csproj",
			"-c", "Release", "-r", rid, "--self-contained", "true", "-p:PublishSingleFile=true",
			"-p:IncludeNativeLibrariesForSelfExtract=true", "-p:EnableCompressionInSingleFile=true",
			"-p:PublishTrimmed=false", "-p:DebugType=None", "-p:DebugSymbols=false",
			f"-p:Version={package_version}", f"-p:InstallerBundleArchive={embedded}", "-o", published, cwd=installer_project_root)
		installer = published / "VeroScriptInstaller"
		installer.chmod(installer.stat().st_mode | 0o111)
		extracted = self.work / (profile + "-single-file-cache")
		installer_env = os.environ | {'DOTNET_BUNDLE_EXTRACT_BASE_DIR': str(extracted)}
		run(installer, "--verify-localization", env=installer_env)
		run(installer, "--verify-embedded-bundle", env=installer_env)
		notice = subprocess.check_output([installer, "--license"], text=True, encoding="utf-8", env=installer_env)
		if notice != (self.repo / "LICENSE").read_text(encoding="utf-8-sig"):
			raise ValueError("The installer does not retain the complete project license notice")
		notices = subprocess.check_output([installer, "--third-party-licenses"], text=True, encoding="utf-8", env=installer_env)
		if notices != (module_root(self.repo) / "VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt").read_text(encoding="utf-8-sig"):
			raise ValueError("The installer does not retain the complete third-party license notices")
		if native_identity(binary_prefix(installer)) != (self.platform, self.arch):
			raise ValueError("Installer native architecture mismatch")
		# Release the expanded bundle/cache before creating the final download.
		for directory in (extracted,):
			shutil.rmtree(directory)
		embedded.unlink()
		package_unix_installer(self.repo, installer, self.root / 'installer', name, package_version, self.target, expected_payload)
		shutil.rmtree(published)

	def assemble(self):
		check_collection(self.repo)
		for tool in TOOLS:
			if native_identity(binary_prefix(self.bin / tool)) != (self.platform, self.arch):
				raise ValueError(f"Native architecture mismatch: {tool}")
		identity = run(self.bin / "veroscript", "--version", capture=True)
		if not identity.startswith(f"VeroScript {self.args.release_versions['nativeVersion']} ") or f"[{self.target};" not in identity:
			raise ValueError(f"Runtime identity mismatch: {identity}")
		jit = jit_identity(identity)
		if jit["backend"] != "none" or jit["nativeCode"]:
			raise ValueError("Runtime JIT identity does not match the build configuration")
		self.build_environment["jit"] = jit
		if self.settings.get("VEROSCRIPT_ENABLE_JIT_TEST_HOOKS", "OFF") not in {"OFF", "0", "FALSE"}:
			raise ValueError("Production packages must disable JIT test hooks")
		scripts = module_root(self.repo) / "VeroScript/Resources/Scripts"
		runtime = self.work / "runtime"
		for tool in TOOLS:
			copy_file(self.bin / tool, runtime / tool)
		copy_file(self.interop, runtime / "lib" / self.shared)
		self.tree("VeroScriptInterop/Public", runtime / "include/VeroScript")
		copy_tree(scripts / "stdlib", runtime / "stdlib")
		stage_localization(self.repo, runtime / "localization")
		stage_runtime_icons(self.repo, runtime, self.target)
		stage_vm_runtime(self.build, runtime)
		if self.application:
			stage_application_runtime(module_root(self.repo), self.bin, runtime)
		self.prepare_native_tree(runtime, self.work / 'native-symbols')
		self.licenses(runtime)
		if self.application:
			stage_application_runtime(module_root(self.repo), runtime, runtime)
		run("cmake", f"-DVERO_CLI={runtime / 'veroscript'}",
			f"-DVERO_EXAMPLES_ROOT={module_root(self.repo) / 'VeroScript/Resources/Scripts/examples'}",
			"-DVERO_PROCESS_TIMEOUT_SECONDS=600",
			f"-DVERO_PACKAGE_OUTPUT_ROOT={self.work}", "-P", module_root(self.repo) / "VeroScriptCli/Private/Tests/VeroScriptExamplesCliTest.cmake")
		(runtime / "runtime-manifest.json").write_text(json.dumps({"schemaVersion": 1, "product": "VeroScript Runtime", "version": self.versions['Runtime'], "platform": self.platform, "architecture": self.arch, "includesExamples": False, "packageManager": self.package_client, "application": self.application, "jit": jit}, indent=2) + '\n', encoding='utf-8')
		stage_package_client(module_root(self.repo), runtime / "veroscript", runtime, "runtime")
		self.archive("runtime", "VeroScriptRuntime", runtime)
		sdk = self.work / "sdk"
		for module in ("VeroScript", "VeroScriptTooling"):
			folder = "Script" if module == "VeroScript" else "Tooling"
			self.tree(f"{module}/Public", sdk / "include" / folder)
			copy_file(self.lib / ("lib" + module + ".a"), sdk / "lib" / ("lib" + module + ".a"))
		copy_file(self.build / "VeroScript/Generated/VeroScriptJitBuild.h", sdk / "include/Script/VeroScriptJitBuild.h")
		for tool in TOOLS:
			copy_file(self.bin / tool, sdk / "bin" / tool)
		copy_tree(scripts / "stdlib", sdk / "assets/stdlib")
		stage_localization(self.repo, sdk / "assets/localization")
		stage_vm_sdk(self.build, sdk)
		stage_package_manager_sdk(self.build, sdk)
		if self.application:
			stage_application_runtime(module_root(self.repo), self.bin, sdk, "bin")
		self.prepare_native_tree(sdk, self.work / 'native-symbols')
		self.licenses(sdk)
		if self.application:
			stage_application_runtime(module_root(self.repo), sdk / "bin", sdk, "bin")
		finalize_package_manager_sdk(sdk, self.v)
		self.config("core", sdk)
		(sdk / "sdk-manifest.json").write_text(json.dumps({"schemaVersion": 1, "product": "VeroScript Core SDK", "version": self.v, "buildId": self.build_id, "platform": self.platform, "architecture": self.arch, "packageManager": self.package_client, "application": self.application, "packageRuntime": self.package_runtime, "buildSystem": self.build_system, "jit": jit}, indent=2) + '\n', encoding='utf-8')
		stage_package_client(module_root(self.repo), sdk / "bin/veroscript", sdk, "core-sdk")
		self.consumer(sdk, "examples/embed-minimal", "core")
		self.consumer(sdk, "examples/annotation-processing", "core-annotation")
		self.consumer(sdk, "examples/script-compile-extensions", "core-script-extensions")
		assets_env = os.environ.copy()
		assets_env.pop("VERO_PATH", None)
		assets_env.pop("VERO_STDLIB_PATH", None)
		run(sdk / "bin/veroscript", "--bc-cache=off", scripts / "examples/standard_library_algorithms.vs", cwd=self.work, env=assets_env)
		self.archive("sdk", "VeroScriptSDK", sdk)
		interop = self.work / "interop"
		self.install("VeroScriptInterop", interop)
		run('cmake', f'-DOUTPUT_FILE:FILEPATH={interop / "lib/cmake/VeroScriptInterop/VeroScriptInteropConfigVersion.cmake"}',
			f'-DPACKAGE_SEMANTIC_VERSION:STRING={self.versions["Interop"]}', '-DPACKAGE_POINTER_SIZE:STRING=8',
			'-P', module_root(self.repo) / 'Resources/Tools/CMake/write-cmake-package-version.cmake')
		interop_config = interop / 'lib/cmake/VeroScriptInterop/VeroScriptInteropConfig.cmake'
		interop_config.write_text(re.sub(r'set\(VeroScriptInterop_VERSION "[^"]+"\)',
										f'set(VeroScriptInterop_VERSION "{self.versions["Interop"]}")', interop_config.read_text(encoding='utf-8')), encoding='utf-8')
		self.prepare_native_tree(interop, self.work / 'interop-symbols')
		self.consumer(interop, "examples/interop-minimal", "interop")
		self.archive("interop", "VeroScriptInteropSDK", interop)
		web = self.work / f"VeroScriptWebAssemblySDK-{self.versions['WebAssembly']}-wasm32"
		for file in WEBASSEMBLY_FILES:
			copy_file(self.args.webassembly_build_directory / "bin" / file, web / file)
		self.licenses(web)
		web_tests = module_root(self.repo) / "Extensions/Platform/WebAssembly/VeroScriptWebAssembly"
		run("npm", "ci", "--no-audit", "--no-fund", cwd=web_tests)
		run("npm", "run", "typecheck", cwd=web_tests)
		run("node", web_tests / "Tests/VeroScriptWebAssemblyArtifactAudit.mjs", web, "Release")
		run("node", web_tests / "Tests/VeroScriptWebAssemblySmoke.mjs", web / "veroscript-engine.mjs")
		run("node", web_tests / "Tests/VeroScriptWebAssemblySmoke.mjs", web / "veroscript-engine-fast.mjs", "--non-interactive")
		run("node", web_tests / "Tests/VeroScriptWebAssemblyBindingsSmoke.mjs", web / "veroscript-webassembly.ts")
		run("npm", "run", "test:browser", "--", web, cwd=web_tests)
		clean_functional_payload(web)
		archive_tree(web, self.root / "webassembly" / (web.name + ".zip"))
		docs = self.work / "docs"
		publish_docs(self.repo, docs, 'User')
		native_symbols = self.work / "native-symbols"
		native_sources = [self.bin / tool for tool in TOOLS] + [self.lib / "libVeroScript.a", self.lib / "libVeroScriptTooling.a"]
		if self.platform == 'linux':
			native_sources.append(self.interop)
		self.symbols(native_symbols, native_sources)
		self.archive("symbols", "VeroScriptSymbols", native_symbols)
		interop_symbols = self.work / "interop-symbols"
		self.symbols(interop_symbols, [self.interop])
		self.installer("runtime", "Runtime", {"runtime": runtime})
		self.installer("core-sdk", "CoreSDK", {"sdk": sdk, "symbols": native_symbols})
		self.installer("interop", "Interop", {"interop-sdk": interop, "symbols": interop_symbols})
		for language, display in (("cpp", "Cpp"), ("csharp", "CSharp"), ("python", "Python")):
			modules = self.language(language)
			portable = self.work / (language + "-portable")
			for name, source in modules.items():
				copy_tree(source, portable / name)
			self.archive("languages", "VeroScript" + display, portable)
			if language == "cpp":
				self.consumer(portable, "Examples/session-cache-minimal", "cpp", [sdk, portable / "Runtime"])
				# The installer preserves its established AOT directory spelling.
				# Exercise that layout on case-sensitive filesystems as well.
				(portable / "Aot").rename(portable / "AOT")
				try:
					self.consumer(portable, "Examples/session-cache-minimal", "cpp-installed", [sdk, portable / "Runtime"])
				finally:
					(portable / "AOT").rename(portable / "Aot")
			component_names = {"Runtime": language + "-runtime", "Tools": language + "-tools", "Aot": language + "-aot", "Examples": "examples", "Symbols": "symbols"}
			self.installer(language, display, {component_names[k]: v for k, v in modules.items() if k != 'Examples'})
		self.installer("web", "WebAssembly", {"web-sdk": web})
		backend = self.work / ASMJIT_PACKAGE_NAME
		stage_backend(self.repo, self.build, backend, self.target, self.versions["Jit"])
		self.archive("jit", ASMJIT_PACKAGE_NAME, backend)
		pack_jit_backend(self.repo, backend,
						 self.root / 'jit' / f'{ASMJIT_PACKAGE_NAME}-{self.versions["Jit"]}-{self.target}.vspkg',
						 self.bin / 'VeroScriptModulePackageTool', self.target, self.versions['Jit'])
		vm_backend = self.work / BYTECODE_VM_PACKAGE_NAME
		stage_vm_backend(self.build, vm_backend, self.bin / 'VeroScriptModulePackageTool', package_version=self.versions['Vm'])
		self.archive("vm", BYTECODE_VM_PACKAGE_NAME, vm_backend)
		pack_vm_backend(vm_backend, self.root / 'vm' / f'{BYTECODE_VM_PACKAGE_NAME}-{self.versions["Vm"]}-{self.target}.vspkg',
						self.bin / 'VeroScriptModulePackageTool')
		builds = {business: self.build for business in SDK_MODULE_BUSINESSES}
		from veroscript_module_symbols import assemble_module_symbols
		self.module_symbol_status.update(assemble_module_symbols(self.repo, {b: self.build for b in ('Jit', 'Vm')},
			self.root, self.versions, self.target, ['Jit', 'Vm']))
		self.module_build_ids = assemble_modules(self.repo, builds, self.root, self.versions, self.target, SDK_MODULE_BUSINESSES, core_sdk=sdk, cpp_sdk=self.work / "cpp-runtime", symbol_status=self.module_symbol_status)
		verify_consumers(self.repo, self.root, builds, list(SDK_MODULE_BUSINESSES), self.target, sdk, jobs=self.args.jobs, cpp_sdk=self.work / "cpp-runtime")
		from veroscript_module_packages import verify_installable_consumers
		verify_installable_consumers(self.repo, self.root, builds, list(SDK_MODULE_BUSINESSES), self.target)
		copy_tree(module_root(self.repo) / "Resources/Assets", self.root / "Resources/Assets")
		copy_tree(docs, self.root / "Resources/Docs")
		for tree in ("stdlib", "examples"):
			copy_tree(scripts / tree, self.root / "Resources/Scripts" / tree)
		copy_tree(module_root(self.repo) / "Modules/Multimedia/VeroScriptGui/Resources/Examples",
				  self.root / "Resources/Examples/Gui")
		python_selected = not self.args.business or 'Python' in self.args.business
		if python_selected:
			self.python_wheels()
		from veroscript_products import DESKTOP_BUSINESSES
		selected = self.args.business or DESKTOP_BUSINESSES
		self.ide_build_ids = stage_ide(self.repo, self.root, self.versions, selected, runtime,
									  backend / ('libVeroScriptJitAsmJit.' + ('so' if self.platform == 'linux' else 'dylib')),
									  dependency_pins=self.ide_dependencies)
		manifest = self.write_release_manifest(python_selected)
		verify_release(self.root, self.repo, businesses=set(manifest['selectedBusinesses']),
					   partial_businesses={'Python'} if not python_selected else None)

	def python_wheels(self):
		command = [sys.executable, module_root(self.repo) / 'Resources/Tools/Packaging/build-veroscript-python-wheels.py',
				   '--repo-root', self.repo, '--target', self.target,
				   '--version', self.versions['Python'], '--output', self.root / 'distributions/python',
				   '--work-directory', self.work / 'python-wheels', '--native-executable', self.bin / 'veroscript',
				   '--jobs', self.args.jobs, '--cmake-generator', self.settings['CMAKE_GENERATOR']]
		for executable in self.args.python_executable:
			command.extend(['--python', executable])
		run(*command)

	def write_release_manifest(self, python_selected: bool):
		write_manifest(self.root, self.v, self.target, self.build_id, build_environment=self.build_environment,
					   business_versions=self.versions, native_version=self.args.release_versions['nativeVersion'])
		path = self.root / 'release-manifest.json'
		manifest = json.loads(path.read_text(encoding='utf-8'))
		manifest['selectedBusinesses'] = list(BUSINESSES) + list(self.ide_build_ids)
		manifest['businessBuildIds'] = {**self.ide_build_ids, **self.module_build_ids}
		from veroscript_module_symbols import portable_symbol_status
		manifest['moduleSymbolStatus'] = portable_symbol_status(self.module_symbol_status)
		if self.ide_dependencies:
			manifest['ideDependencies'] = self.ide_dependencies
		manifest['functionalPayloadBusinesses'] = manifest['selectedBusinesses']
		manifest['installableModuleBusinesses'] = ['Jit', 'Vm', *SDK_MODULE_BUSINESSES]
		if not python_selected:
			manifest['partialBusinesses'] = ['Python']
		path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
		return manifest

	def language(self, language):
		modules = {name: self.work / (language + "-" + name.lower()) for name in ("Runtime", "Tools", "Aot", "Symbols")}
		for path in modules.values():
			path.mkdir(parents=True, exist_ok=True)
			self.licenses(path)
		prefix = {"cpp": "Cpp", "csharp": "CSharp", "python": "Python"}[language]
		native = []
		if language == "cpp":
			self.tree("Extensions/Language/Cpp/VeroScriptCpp/Public", modules["Runtime"] / "include/Cpp")
			copy_file(self.lib / "libVeroScriptCpp.a", modules["Runtime"] / "lib/libVeroScriptCpp.a")
			native.append(self.lib / "libVeroScriptCpp.a")
			self.install("VeroScriptCppTools", modules["Tools"])
			self.tree("Extensions/Language/Cpp/VeroScriptCppAot/Public", modules["Aot"] / "include/CppAot")
			copy_file(self.lib / "libVeroScriptCppAot.a", modules["Aot"] / "lib/libVeroScriptCppAot.a")
			self.config("cpp", modules["Runtime"])
			stage_localization(self.repo, modules["Runtime"] / "localization", "cpp")
			for name in ("VeroScriptCppFrontend", "VeroScriptCppBindGenLib", "VeroScriptCppTranspilerLib"):
				native.append(self.lib / ("lib" + name + ".a"))
			native.extend(self.bin / name for name in ("VeroScriptCppBindGen", "VeroScriptCppTranspiler"))
		else:
			self.install("VeroScript" + prefix, modules["Runtime"])
			self.install("VeroScript" + prefix + "Tools", modules["Tools"])
			if language == "csharp":
				stage_csharp_packages(self.build, modules['Runtime'], modules['Tools'],
					self.root / 'distributions/csharp', self.versions['CSharp'])
				for framework in ("net8.0", "net9.0"):
					copy_file(self.interop, modules["Runtime"] / "lib/dotnet" / framework / self.shared)
					stage_localization(self.repo, modules["Runtime"] / "lib/dotnet" / framework / "localization", "interop")
				for config in modules["Tools"].rglob("VeroScriptCSharpBindGen.Cli.runtimeconfig.json"):
					copy_file(self.interop, config.parent / self.shared)
				self.install("VeroScriptCSharpAot", modules["Aot"])
				for source in (module_root(self.repo) / "Extensions/Language/CSharp").glob("*/bin/Release/net*/*.pdb"):
					copy_file(source, modules["Symbols"] / source.parent.name / source.name)
			else:
				self.tree("Extensions/Language/Python/VeroScriptPythonAot/Public", modules["Aot"] / "include/PythonAot")
				copy_file(self.lib / "libVeroScriptPythonAot.a", modules["Aot"] / "lib/libVeroScriptPythonAot.a")
				packages = list(modules["Runtime"].glob("lib*/python*/site-packages"))
				if len(packages) != 1:
					raise ValueError("Missing/ambiguous packaged Python ABI layout")
				native.extend(packages[0].rglob("_veroscript*.so"))
		native.append(self.lib / f"libVeroScript{prefix}Aot.a")
		if language != "cpp":
			native.append(self.interop)
		self.symbols(modules["Symbols"], native)
		for name in ('Runtime', 'Tools', 'Aot'):
			self.prepare_native_tree(modules[name], modules['Symbols'])
		if language == 'csharp':
			self.csharp_consumer(modules['Runtime'])
			for tool in ('veroscript-csharp-bindgen/VeroScriptCSharpBindGen.Cli', 'veroscript-csharp-transpiler/VeroScriptCSharpTranspiler.Cli'):
				run(modules['Tools'] / 'bin/dotnet-tools' / tool, '--help', cwd=self.work)
		elif language == 'python':
			env = os.environ.copy()
			env['PYTHONPATH'] = str(packages[0])
			interpreter = self.settings.get('Python3_EXECUTABLE', self.settings.get('_Python3_EXECUTABLE', 'python3'))
			run(interpreter, '-c', 'import veroscript\nwith veroscript.Session() as s:\n    assert s.load_source("int answer(){return 42;}").success\n    assert s.call("answer") == 42\nprint(veroscript.__file__)', cwd=self.work, env=env)
		return modules


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--version", type=version, help='Explicit version override for selected businesses')
	parser.add_argument('--versions-file', type=Path)
	from veroscript_products import DESKTOP_BUSINESSES
	parser.add_argument('--business', action='append', choices=DESKTOP_BUSINESSES)
	parser.add_argument("--target", choices=tuple(k for k in TARGETS if not k.startswith("win-")))
	parser.add_argument("--configure-preset")
	parser.add_argument("--build-preset", help="Compatibility option; must reference the selected configure preset")
	parser.add_argument("--build-directory", type=Path)
	parser.add_argument("--webassembly-build-directory", type=Path)
	parser.add_argument("--repo-root", type=Path, default=repository_root())
	parser.add_argument("--out-root", type=Path)
	parser.add_argument("--skip-build", action="store_true", help="Reuse prevalidated native and WebAssembly outputs; all package tests still run")
	parser.add_argument('--keep-failed-stage', action='store_true', help='Retain the owned staging directory when publication fails')
	parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 8))
	parser.add_argument('--python-executable', action='append', default=[], help='CPython matrix executable; repeat as needed')
	parser.add_argument('--core-sdk', type=Path, help='Installed Core SDK for a module-only release')
	parser.add_argument('--cpp-sdk', type=Path, help='Matching C++ Runtime SDK prefix for module-only Gui releases')
	parser.add_argument('--data-only', action='store_true', help='Omit script adapters from an SDK module-only release')
	args = parser.parse_args()
	repo = args.repo_root.resolve()
	args.release_versions = load_versions(repo, args.versions_file, args.version, args.business)
	if args.business and set(args.business).issubset(SDK_MODULE_BUSINESSES):
		command = [sys.executable, Path(__file__).with_name('package-veroscript-modules.py'), '--repo-root', repo]
		if args.build_directory is None and args.configure_preset:
			command += ['--build-directory', repo / 'build' / args.configure_preset]
		for key in ('build_directory', 'versions_file', 'version', 'target', 'out_root', 'core_sdk', 'cpp_sdk'):
			if getattr(args, key) is not None:
				command += ['--' + key.replace('_', '-'), getattr(args, key)]
		for business in args.business:
			command += ['--business', business]
		if args.skip_build:
			command += ['--skip-build']
		if args.data_only:
			command += ['--data-only']
		run(*command, '--jobs', args.jobs)
		return
	if args.data_only:
		raise ValueError('--data-only requires an SDK module-only --business selection')
	target = args.target or host_target()
	if target != host_target() or target.startswith("win-"):
		raise ValueError("Full Unix packaging requires a native matching Linux/macOS host")
	if args.jobs < 1:
		raise ValueError("--jobs must be positive")
	preset = args.configure_preset or ("linux-ninja-release" if target.startswith("linux") else "macos-ninja-release")
	if args.build_preset:
		presets = json.loads((repo / "CMakePresets.json").read_text(encoding='utf-8'))
		build_preset = next((p for p in presets["buildPresets"] if p["name"] == args.build_preset), None)
		if not build_preset or build_preset.get("configurePreset") != preset:
			raise ValueError("Build/configure presets do not match")
	build = (repo / (args.build_directory or Path("build") / preset)).resolve()
	args.webassembly_build_directory = (repo / (args.webassembly_build_directory or Path("build/webassembly"))).resolve()
	output = collection_root(repo, (repo / args.out_root if args.out_root else module_root(repo) / 'Release'))
	for directory in (build, args.webassembly_build_directory):
		if (directory / "CMakeCache.txt").is_file():
			previous = cache(directory)
			if Path(previous["CMAKE_HOME_DIRECTORY"]).resolve() != repo:
				raise ValueError(f"Build cache belongs to a different source: {directory}")
	check_collection(repo)
	# Select the installer's supported SDK before any long native build/checks.
	run('dotnet', '--version', cwd=module_root(repo) / 'VeroScriptInstaller')
	check_ide_sources(repo, args.release_versions['businesses'],
					  [business for business in (args.business or ALL_BUSINESSES) if business in IDE_BUSINESSES])
	check_ide_prerequisites(repo, args.business or ALL_BUSINESSES)
	if not args.business or 'Python' in args.business:
		from veroscript_python_wheels import discover_interpreters
		discover_interpreters(repo, target, args.python_executable)
	os.environ.setdefault("DOTNET_ROLL_FORWARD", "Major")
	if not args.skip_build:
		if not (build / "CMakeCache.txt").exists():
			if args.build_directory and not args.configure_preset:
				run("cmake", "-S", repo, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
			else:
				run("cmake", "--preset", preset, "-S", repo, "-B", build, cwd=repo)
		settings = cache(build)
		flags = settings.get("CMAKE_CXX_FLAGS_RELEASE", "-O3 -DNDEBUG")
		if "-g" not in flags.split():
			flags += " -g"
		run("cmake", "-S", repo, "-B", build, f"-DCMAKE_CXX_FLAGS_RELEASE={flags}", "-DCMAKE_INSTALL_LIBDIR=lib",
			'-DVEROSCRIPT_BUILD_JSON=ON', '-DVEROSCRIPT_BUILD_NETWORK=ON', '-DVEROSCRIPT_BUILD_HTTP=ON', '-DVEROSCRIPT_BUILD_WEBSOCKET=ON', '-DVEROSCRIPT_BUILD_BOOST=ON', '-DVEROSCRIPT_BUILD_CURL=ON',
			'-DVEROSCRIPT_JSON_BUILD_BINDINGS=ON', '-DVEROSCRIPT_NETWORK_BUILD_BINDINGS=ON',
			'-DVEROSCRIPT_BUILD_PLATFORM=ON', '-DVEROSCRIPT_BUILD_GRAPHICS=ON', '-DVEROSCRIPT_BUILD_AUDIO=ON',
			'-DVEROSCRIPT_BUILD_SDL3=ON', '-DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=ON',
			'-DVEROSCRIPT_BUILD_GUI=ON', '-DVEROSCRIPT_GUI_SCRIPTING=ON', '-DVEROSCRIPT_GUI_BUILD_RMLUI=ON',
			'-DVEROSCRIPT_BUILD_STB=ON', '-DVEROSCRIPT_BUILD_FONT_BACKEND=ON', '-DVEROSCRIPT_RMLUI_NATIVE_EXTENSION=ON',
			'-DVEROSCRIPT_STB_NATIVE_EXTENSION=ON', '-DVEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION=ON',
			'-DVEROSCRIPT_SDL3_GPU=ON', '-DVEROSCRIPT_SDL3_AUDIO=ON', '-DVEROSCRIPT_SDL3_CONTROLLERS=ON',
			f'-DVS_VEROSCRIPT_CSHARP_VERSION={args.release_versions["businesses"]["CSharp"]}',
			f'-DVS_VEROSCRIPT_ASMJIT_VERSION={args.release_versions["businesses"]["Jit"]}',
			f'-DVS_VEROSCRIPT_VM_VERSION={args.release_versions["businesses"]["Vm"]}',
			'-DVEROSCRIPT_BUILD_VM_BACKEND=ON', '-DVEROSCRIPT_BUILD_APPLICATION=ON',
			'-DVEROSCRIPT_BUILD_BUILD_SYSTEM=ON', '-DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON',
			'-DVEROSCRIPT_BUILD_PACKAGE_RUNTIME=ON')
		targets = BUILD_TARGETS
		run("cmake", "--build", build, "--config", "Release", "--parallel", args.jobs, "--target", *targets)
		run("emcmake", "cmake", "-S", repo, "-B", args.webassembly_build_directory, "-G", "Ninja",
			"-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF", "-DVS_ENABLE_IPO=ON",
			"-DVS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY=ON", "-DVEROSCRIPT_BUILD_VM_BACKEND=ON",
			"-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF",
			"-DVEROSCRIPT_ENABLE_ASMJIT=OFF")
		run("cmake", "--build", args.webassembly_build_directory, "--parallel", args.jobs, "--target", "VeroScriptWebAssemblySdkSources")
	settings = cache(build)
	if settings.get('VS_VEROSCRIPT_CSHARP_VERSION') != args.release_versions['businesses']['CSharp']:
		raise ValueError('C# build/package version mismatch; rebuild without --skip-build')
	if Path(settings["CMAKE_HOME_DIRECTORY"]).resolve() != repo or settings.get("CMAKE_BUILD_TYPE") != "Release":
		raise ValueError("Native cache belongs to a different source/configuration")
	if "-g" not in settings.get("CMAKE_CXX_FLAGS_RELEASE", "").split():
		raise ValueError("Native Release must contain debug information for the Symbols product; rebuild without --skip-build")
	web_settings = cache(args.webassembly_build_directory)
	if Path(web_settings["CMAKE_HOME_DIRECTORY"]).resolve() != repo or web_settings.get("CMAKE_BUILD_TYPE") != "Release":
		raise ValueError("WebAssembly cache belongs to a different source/configuration")
	scratch = scratch_directory(repo)
	directory = Path(tempfile.mkdtemp(prefix='veroscript-release-stage-', dir=scratch))
	succeeded = False
	try:
		release = Release(args, repo, directory, build, target)
		release.assemble()
		publish_businesses(repo, release.root, output, args.business or list(DESKTOP_BUSINESSES))
		succeeded = True
	finally:
		if succeeded or not args.keep_failed_stage:
			shutil.rmtree(directory)
		else:
			print(f'Failed staging retained for diagnostics: {directory}', flush=True)
	verify_collection(output, target)
	print(f"VeroScript business releases: {output}")


if __name__ == "__main__":
	main()
