# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import importlib.util
import io
from pathlib import Path
import re
import stat
import subprocess
import sys
import tarfile
import tempfile
from typing import Protocol, cast
import unittest
import zipfile


class _Validator(Protocol):
	SUPPORTED_PYTHON_TAGS: set[str]
	REQUIRED_CONSOLE_SCRIPTS: dict[str, str]

	def validate_archive_names(
		self,
		names: list[str],
		archive: Path,
		directory_names: set[str] | None = None,
	) -> None:
		...

	def validate_wheel(self, path: Path) -> tuple[str, str, str]:
		...

	def validate_sdist(self, path: Path) -> str:
		...

	def metadata_version(self, data: bytes, archive: Path) -> str:
		...

	def source_module(self, names: set[str]) -> str:
		...

	def validate_console_scripts(self, data: bytes, archive: Path) -> None:
		...

	def platform_family(self, platform_tag: str) -> str:
		...

	def expanded_wheel_tags(
		self, python_tag: str, abi_tag: str, platform_tag: str
	) -> set[str]:
		...

	def embedded_absolute_pdb_paths(self, data: bytes) -> set[str]:
		...

	def is_forbidden_sdist_source_path(self, name: str) -> bool:
		...


def _load_validator() -> _Validator:
	path = Path(__file__).resolve().parents[1] / "Resources" / "Tools" / "Packaging" / "validate_distribution.py"
	spec = importlib.util.spec_from_file_location("veroscript_distribution_validator", path)
	if spec is None or spec.loader is None:
		raise RuntimeError("cannot load VeroScriptPython distribution validator")
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return cast(_Validator, module)


VALIDATOR = _load_validator()


class DistributionValidatorTests(unittest.TestCase):
	_VALID_METADATA = (
		"Metadata-Version: 2.4\n"
		"Name: veroscript\n"
		"Version: 1.0.0\n"
		"Requires-Python: <3.16,>=3.11\n"
		'Requires-Dist: typing_extensions>=4.16; python_version < "3.15"\n'
		"License-Expression: MIT\n"
		"License-File: LICENSE\n\n"
	)

	def test_supported_cpython_minors_are_explicit(self) -> None:
		self.assertEqual(
			VALIDATOR.SUPPORTED_PYTHON_TAGS,
			{"cp311", "cp312", "cp313", "cp314", "cp315"},
		)

	def test_metadata_uses_only_the_project_license(self) -> None:
		self.assertEqual('1.0.0', VALIDATOR.metadata_version(self._VALID_METADATA.encode(), Path('binary.whl')))
		stale_metadata = self._VALID_METADATA.replace('License-Expression: MIT', 'License-Expression: Zlib')
		with self.assertRaisesRegex(SystemExit, 'License-Expression'):
			VALIDATOR.metadata_version(stale_metadata.encode(), Path('stale-license.whl'))
		metadata = self._VALID_METADATA.replace('License-File: LICENSE', 'License-File: Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md')
		with self.assertRaisesRegex(SystemExit, 'License-File metadata'):
			VALIDATOR.metadata_version(metadata.encode(), Path('missing-project-license.whl'))

	def test_wheel_requires_application_vendor_licenses(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "veroscript-1.0.0-cp314-cp314-win_amd64.whl"
			dist_info = "veroscript-1.0.0.dist-info/"
			with zipfile.ZipFile(path, "w") as archive:
				archive.writestr(dist_info + "METADATA", self._VALID_METADATA)
				archive.writestr(dist_info + "WHEEL",
					"Wheel-Version: 1.0\nRoot-Is-Purelib: false\nTag: cp314-cp314-win_amd64\n")
				archive.writestr(dist_info + "entry_points.txt", "[console_scripts]\n" + "".join(
					f"{name} = {target}\n"
					for name, target in VALIDATOR.REQUIRED_CONSOLE_SCRIPTS.items()
				))
				archive.writestr(dist_info + "RECORD", "")
			with self.assertRaises(SystemExit) as error:
				VALIDATOR.validate_wheel(path)
			for filename in ("miniz/LICENSE", "json/LICENSE.MIT", "sha256/LICENSE"):
				with self.subTest(filename=filename):
					self.assertIn("veroscript/licenses/application/" + filename, str(error.exception))

	def test_source_module_discovery_accepts_a_renamed_module(self) -> None:
		names = {'veroscript-1.0.0/Engine Sources/VeroScript/Public/VeroScriptVersion.h'}
		self.assertEqual('Engine Sources', VALIDATOR.source_module(names))
		for invalid in (set(), names | {'veroscript-1.0.0/OtherModule/VeroScript/Public/VeroScriptVersion.h'}):
			with self.subTest(names=invalid), self.assertRaisesRegex(SystemExit, 'one source module'):
				VALIDATOR.source_module(invalid)

	def test_source_module_discovery_accepts_repository_root(self) -> None:
		names = {'veroscript-1.0.0/VeroScript/Public/VeroScriptVersion.h'}
		self.assertEqual('.', VALIDATOR.source_module(names))
		with self.assertRaises(SystemExit):
			VALIDATOR.source_module(names | {
				'veroscript-1.0.0/OtherModule/VeroScript/Public/VeroScriptVersion.h'})
		self.assertTrue(VALIDATOR.is_forbidden_sdist_source_path(
			'veroscript-1.0.0/VeroScript/Resources/Benchmarks/Results/run.csv'))

	def test_source_distribution_can_bundle_native_dependencies(self) -> None:
		self.assertEqual('_native', VALIDATOR.source_module({
			'veroscript-1.0.0/_native/VeroScript/Public/VeroScriptVersion.h'}))
		self.assertTrue(VALIDATOR.is_forbidden_sdist_source_path(
			'veroscript-1.0.0/_native/VeroScript/Resources/Benchmarks/Results/run.csv'))

	@unittest.skipUnless(importlib.util.find_spec('build') and importlib.util.find_spec('scikit_build_core'),
						 'Source packaging regression requires build and scikit-build-core')
	def test_source_distribution_repacking_preserves_complete_payload(self) -> None:
		project = Path(__file__).resolve().parents[2]
		if not (project / 'pyproject.toml').is_file():
			self.skipTest('This test builds the module-owned checkout project')

		def build_sdist(source: Path, output: Path) -> Path:
			result = subprocess.run(
				[sys.executable, '-m', 'build', str(source), '--sdist', '--no-isolation',
				 '--outdir', str(output)], capture_output=True, text=True)
			self.assertEqual(0, result.returncode, result.stdout + result.stderr)
			archives = list(output.glob('veroscript-*.tar.gz'))
			self.assertEqual(1, len(archives))
			VALIDATOR.validate_sdist(archives[0])
			return archives[0]

		def payload(path: Path) -> dict[str, bytes]:
			with tarfile.open(path) as archive:
				return {member.name: archive.extractfile(member).read()
						for member in archive.getmembers() if member.isfile()}

		with tempfile.TemporaryDirectory(prefix='vero standalone source package ') as temporary:
			workspace = Path(temporary)
			first = build_sdist(project, workspace / 'first')
			contents = payload(first)
			icon = 'Resources/Assets/icons/veroscript-icon.ico'
			icons = [name for name in contents if name.endswith('/' + icon)]
			self.assertEqual(1, len(icons))
			self.assertEqual((project.parents[2] / icon).read_bytes(), contents[icons[0]])
			for suffix in (
				'Resources/Assets/icons/veroscript-icon.ico',
				'Shared/Private/VeroScriptFloatParsing.h',
				'Shared/Private/VeroScriptFloatFormatting.h',
				'Shared/Private/VeroScriptDarwinPaths.h',
				'Shared/Private/VeroScriptJsonString.h',
				'VeroScript/Public/VeroScriptAnnotationProcessing.h',
				'VeroScript/Private/VeroScriptProgramPreparation.cpp',
				'VeroScriptInterop/Public/VeroScriptInteropAnnotation.h',
				'VeroScriptInterop/Private/VeroScriptInteropAnnotation.cpp',
				'VeroScript/Public/VeroScriptMemory.h',
				'VeroScript/Public/VeroScriptNumericBindings.h',
				'VeroScript/Public/VeroScriptVariant.h',
				'VeroScript/Public/VeroScriptVmBackend.h',
				'Modules/Execution/VeroScriptBytecodeVm/CMakeLists.txt',
				'Modules/Execution/VeroScriptBytecodeVm/Public/VeroScriptBytecodeVm.h',
				'Modules/Execution/VeroScriptBytecodeVm/Public/VeroScriptBytecodeVmAssembly.h',
				'Modules/Execution/VeroScriptBytecodeVm/Private/VeroScriptBytecodeVm.cpp',
				'Modules/Execution/VeroScriptBytecodeVm/Private/VeroScriptBytecodeVmExecution.cpp',
				'Modules/Execution/VeroScriptBytecodeVm/Resources/VeroScriptBytecodeVmConfig.cmake.in',
				'VeroScriptInterop/Private/VeroScriptInteropNumerics.cpp',
				'VeroScriptApplication/CMakeLists.txt',
				'VeroScriptApplication/Public/VeroScriptApplication.h',
				'VeroScriptApplication/Private/VeroScriptApplication.cpp',
				'VeroScriptApplication/Private/Tools/VeroScriptRuntimeMain.cpp',
				'VeroScriptApplication/ThirdParty/miniz/miniz.c',
				'VeroScriptApplication/ThirdParty/json/json.hpp',
				'VeroScriptApplication/ThirdParty/sha256/picosha2.h',
				'VeroScriptApplication/ThirdParty/miniz/LICENSE',
				'VeroScriptApplication/ThirdParty/json/LICENSE.MIT',
				'VeroScriptApplication/ThirdParty/sha256/LICENSE',
				'VeroScript/Public/VeroScriptConsole.h',
				'VeroScript/Private/VeroScriptConsole.cpp',
				'VeroScript/Private/VeroScriptConsoleNative.h',
				'VeroScript/Resources/Scripts/stdlib/memory.vs',
				'VeroScript/Resources/Scripts/stdlib/bigint.vs',
				'VeroScript/Resources/Scripts/stdlib/rational.vs',
				'Modules/Execution/VeroScriptAsmJit/Resources/Licenses/NOTICES.md',
				'Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/LICENSE.md',
				'Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/VERSION.txt',
				'Shared/Private/VeroScriptFloatFormatting.h',
				'Shared/Private/VeroScriptDarwinPaths.h',
				'Resources/Tools/Packaging/veroscript_products.py',
				'Resources/Tools/Packaging/veroscript_paths.py',
				'VeroScript/Resources/Localization/fr-FR/memory.json',
				'Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPythonMemory.inl',
				'Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPythonNumerics.inl',
				'Extensions/Language/Python/VeroScriptPython/Private/VeroScriptPythonVariant.inl',
			):
				with self.subTest(missing=suffix):
					missing = [name for name in contents if name.endswith('/' + suffix)]
					self.assertEqual(1, len(missing))
					damaged = workspace / 'damaged' / first.name
					damaged.parent.mkdir(exist_ok=True)
					with tarfile.open(damaged, 'w:gz') as archive:
						for name, data in contents.items():
							if name == missing[0]:
								continue
							member = tarfile.TarInfo(name)
							member.size = len(data)
							archive.addfile(member, io.BytesIO(data))
					with self.assertRaisesRegex(SystemExit, re.escape(suffix)):
						VALIDATOR.validate_sdist(damaged)
			for name, data in contents.items():
				destination = workspace / 'extracted' / name
				destination.parent.mkdir(parents=True, exist_ok=True)
				destination.write_bytes(data)
			unpacked = workspace / 'extracted' / first.name.removesuffix('.tar.gz')
			# Repacking an unpacked source tree must ignore local outputs even
			# without the checkout's Git ignore rules or standard build paths.
			for artifact in ('CMakeCache.txt', 'CMakeFiles/probe.o', 'build.ninja',
						 '.ninja_log', 'probe.dll', 'probe.pdb', 'libprobe.so.1',
						 'probe.log', 'probe.tmp', 'probe.bak', 'probe.vsapp',
						 'probe.vslib', 'probe.vspkg', 'vs-json-0.1.1-win-x64.zip'):
				generated = unpacked / '_native/VeroScript/custom-output' / artifact
				generated.parent.mkdir(parents=True, exist_ok=True)
				generated.write_bytes(b'local build output')
			second = build_sdist(unpacked, workspace / 'second')
			self.assertEqual(contents, payload(second))

	def test_renamed_source_module_still_rejects_generated_resources(self) -> None:
		for route in ('Engine Sources/VeroScript/Benchmarks/Results/run.csv',
					  'Engine Sources/Extensions/Language/Python/README.md',
					  'Engine Sources/Extensions/Language/Python/VeroScriptPython/tests/test_wheel.py',
					  'Engine Sources/Extensions/Language/Python/VeroScriptPythonBindGen/Resources/Examples/HostApi.py',
					  'Engine Sources/VeroScript/Resources/Benchmarks/Results/run.csv',
					  'Engine Sources/VeroScript/Resources/Benchmarks/RuntimeBenchmarks.cpp',
					  'Engine Sources/Resources/Docs/User/en-US/index.md',
					  'Engine Sources/VeroScript/Resources/Docs/Developer/en-US/index.md'):
			self.assertTrue(VALIDATOR.is_forbidden_sdist_source_path('veroscript-1.0.0/' + route))

	def test_compressed_platform_tags_expand_to_wheel_metadata_tags(self) -> None:
		platform_tag = "manylinux_2_17_x86_64.manylinux2014_x86_64"
		self.assertEqual(
			VALIDATOR.platform_family(platform_tag),
			"manylinux-x86_64",
		)
		self.assertEqual(
			VALIDATOR.expanded_wheel_tags("cp311", "cp311", platform_tag),
			{
				"cp311-cp311-manylinux_2_17_x86_64",
				"cp311-cp311-manylinux2014_x86_64",
			},
		)
		with self.assertRaisesRegex(SystemExit, "multiple platform families"):
			VALIDATOR.platform_family("win_amd64.manylinux_2_17_x86_64")

	def test_platform_tags_fail_closed(self) -> None:
		for tag, expected_family in (
			("manylinux1_x86_64", "manylinux-x86_64"),
			("manylinux_2_17_x86_64", "manylinux-x86_64"),
			("manylinux2014_aarch64", "manylinux-aarch64"),
			("linux_aarch64", "linux-aarch64-local"),
			("macosx_10_9_x86_64", "macos-x86_64"),
			("macosx_11_0_arm64", "macos-arm64"),
			("win_amd64", "windows-amd64"),
			("win32", "windows-x86"),
			("win_arm64", "windows-arm64"),
		):
			with self.subTest(tag=tag):
				self.assertEqual(VALIDATOR.platform_family(tag), expected_family)

		for tag in (
			"notmanylinux_fake_x86_64",
			"manylinux_x86_64",
			"macosx_bad_x86_64",
			"macosx__arm64",
		):
			with self.subTest(tag=tag):
				with self.assertRaisesRegex(SystemExit, "unsupported wheel platform"):
					VALIDATOR.platform_family(tag)

	def test_absolute_windows_pdb_paths_are_detected(self) -> None:
		self.assertEqual(
			VALIDATOR.embedded_absolute_pdb_paths(
				b"RSDSbinary-prefix\0"
				b"C:\\build\\wheel\\_veroscript.pdb\0binary-suffix"
			),
			{r"C:\build\wheel\_veroscript.pdb"},
		)
		self.assertEqual(
			VALIDATOR.embedded_absolute_pdb_paths(
				b"RSDSbinary-prefix\0_veroscript.pdb\0binary-suffix"
			),
			set(),
		)

	def test_distribution_support_contract_fails_closed(self) -> None:
		archive = Path("candidate.whl")
		self.assertEqual(
			VALIDATOR.metadata_version(self._VALID_METADATA.encode(), archive),
			"1.0.0",
		)
		for invalid, message in (
			(
				self._VALID_METADATA.replace("<3.16,>=3.11", ">=3.11"),
				"Requires-Python",
			),
			(
				self._VALID_METADATA.replace(
					'Requires-Dist: typing_extensions>=4.16; python_version < "3.15"\n',
					"",
				),
				"Requires-Dist",
			),
		):
			with self.subTest(message=message):
				with self.assertRaisesRegex(SystemExit, message):
					VALIDATOR.metadata_version(invalid.encode(), archive)

	def test_console_script_targets_fail_closed(self) -> None:
		archive = Path("candidate.whl")
		valid = "[console_scripts]\n" + "".join(
			f"{name} = {target}\n"
			for name, target in VALIDATOR.REQUIRED_CONSOLE_SCRIPTS.items()
		)
		VALIDATOR.validate_console_scripts(valid.encode(), archive)
		with self.assertRaisesRegex(SystemExit, "unexpected console scripts"):
			VALIDATOR.validate_console_scripts(
				valid.replace(
					"veroscript_python_aot.__main__:main",
					"veroscript_python_aot.__main__:missing",
				).encode(),
				archive,
			)
		with self.assertRaisesRegex(SystemExit, "entry-point groups"):
			VALIDATOR.validate_console_scripts(
				(valid + "\n[unexpected]\ncommand = module:main\n").encode(),
				archive,
			)

	def test_archive_paths_fail_closed(self) -> None:
		archive = Path("candidate.whl")
		VALIDATOR.validate_archive_names(
			["veroscript/", "veroscript/__init__.py"], archive
		)
		for unsafe in (
			"../outside",
			"/absolute",
			"C:/absolute",
			"veroscript\\outside",
			"veroscript/./module.py",
			"veroscript//module.py",
			"veroscript/../module.py",
			"veroscript/\0module.py",
		):
			with self.subTest(unsafe=unsafe):
				with self.assertRaisesRegex(SystemExit, "unsafe paths"):
					VALIDATOR.validate_archive_names([unsafe], archive)
		with self.assertRaisesRegex(SystemExit, "duplicate archive entries"):
			VALIDATOR.validate_archive_names(["same", "same"], archive)
		with self.assertRaisesRegex(SystemExit, "conflicting file and directory"):
			VALIDATOR.validate_archive_names(["same", "same/"], archive)
		with self.assertRaisesRegex(SystemExit, "case-insensitive path collisions"):
			VALIDATOR.validate_archive_names(
				["veroscript/module.py", "VeroScript/module.py"], archive
			)
		with self.assertRaisesRegex(SystemExit, "as a parent directory"):
			VALIDATOR.validate_archive_names(
				["veroscript", "veroscript/module.py"], archive
			)

	def test_wheel_symbolic_link_is_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			path = (
				Path(directory)
				/ "veroscript-1.0.0-cp314-cp314-win_amd64.whl"
			)
			with zipfile.ZipFile(path, "w") as archive:
				link = zipfile.ZipInfo("veroscript/runtime-link")
				link.create_system = 3
				link.external_attr = (stat.S_IFLNK | 0o777) << 16
				archive.writestr(link, "target")
			with self.assertRaisesRegex(SystemExit, "symbolic links"):
				VALIDATOR.validate_wheel(path)

	def test_wheel_special_file_is_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			path = (
				Path(directory)
				/ "veroscript-1.0.0-cp314-cp314-win_amd64.whl"
			)
			with zipfile.ZipFile(path, "w") as archive:
				fifo = zipfile.ZipInfo("veroscript/runtime-pipe")
				fifo.create_system = 3
				fifo.external_attr = (stat.S_IFIFO | 0o600) << 16
				archive.writestr(fifo, b"")
			with self.assertRaisesRegex(SystemExit, "special file entries"):
				VALIDATOR.validate_wheel(path)

	def test_sdist_links_and_special_entries_are_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "veroscript-1.0.0.tar.gz"
			with tarfile.open(path, "w:gz") as archive:
				root = tarfile.TarInfo("veroscript-1.0.0")
				root.type = tarfile.DIRTYPE
				archive.addfile(root)
				link = tarfile.TarInfo("veroscript-1.0.0/runtime-link")
				link.type = tarfile.SYMTYPE
				link.linkname = "outside"
				archive.addfile(link)
			with self.assertRaisesRegex(SystemExit, "links or special archive entries"):
				VALIDATOR.validate_sdist(path)

	def test_sdist_local_build_artifacts_are_rejected(self) -> None:
		metadata = self._VALID_METADATA.encode()
		artifacts = (
			"veroscript-1.0.0/_native/VeroScript/custom-output/CMakeCache.txt",
			"veroscript-1.0.0/_native/VeroScript/custom-output/CMakeFiles/probe.cpp",
			"veroscript-1.0.0/_native/VeroScript/custom-output/build.ninja",
			"veroscript-1.0.0/_native/VeroScript/custom-output/.ninja_deps",
			"veroscript-1.0.0/_native/VeroScript/custom-output/probe.dll",
			"veroscript-1.0.0/_native/VeroScript/custom-output/probe.pdb",
			"veroscript-1.0.0/_native/VeroScript/custom-output/libprobe.so.1",
			"veroscript-1.0.0/_native/VeroScript/custom-output/probe.log",
			"veroscript-1.0.0/_native/VeroScript/custom-output/probe.tmp",
			"veroscript-1.0.0/_native/VeroScript/custom-output/probe.vsapp",
			"veroscript-1.0.0/_native/VeroScript/custom-output/probe.vslib",
			"veroscript-1.0.0/_native/VeroScript/custom-output/probe.vspkg",
			"veroscript-1.0.0/_native/vs-json-0.1.1-win-x64.zip",
			"veroscript-1.0.0/_native/leftover.whl",
			"veroscript-1.0.0/_native/leftover.nupkg",
			"veroscript-1.0.0/_native/leftover.vsix",
			"veroscript-1.0.0/_native/leftover.aar",
			"veroscript-1.0.0/_native/library.dSYM/Contents/Info.plist",
			"veroscript-1.0.0/_native/library.xcframework/Info.plist",
			"veroscript-1.0.0/.codex-review/generated-sdk.zip",
			"veroscript-1.0.0/IDE/review-evidence.json",
			"veroscript-1.0.0/Script/VeroScript/Benchmarks/Results/run.csv",
			"veroscript-1.0.0/Script/VeroScript/Resources/Benchmarks/Results/run.csv",
			"veroscript-1.0.0/Script/VeroScript/Resources/Benchmarks/RuntimeBenchmarks.cpp",
			"veroscript-1.0.0/Script/VeroScriptUtils/docs/internal.md",
			"veroscript-1.0.0/Script/VeroScript/Resources/Docs/Developer/en-US/index.md",
			"veroscript-1.0.0/Script/Extensions/Language/Python/Resources/Docs/User/en-US/index.md",
			"veroscript-1.0.0/Script/Extensions/Language/Cpp/Resources/Docs/Developer/en-US/index.md",
		)
		for artifact_name in artifacts:
			with self.subTest(artifact_name=artifact_name):
				with tempfile.TemporaryDirectory() as directory:
					path = Path(directory) / "veroscript-1.0.0.tar.gz"
					with tarfile.open(path, "w:gz") as archive:
						for name, data in (
							("veroscript-1.0.0/PKG-INFO", metadata),
							(artifact_name, b"local artifact"),
						):
							member = tarfile.TarInfo(name)
							member.size = len(data)
							archive.addfile(member, io.BytesIO(data))
					with self.assertRaisesRegex(
						SystemExit, "local build or review artifacts"
					):
						VALIDATOR.validate_sdist(path)

	def test_source_payload_preserves_licenses_build_inputs_and_vendored_assets(self) -> None:
		for name in (
			'_native/ThirdParty/upstream/LICENSE',
			'_native/ThirdParty/upstream/NOTICES.md',
			'_native/ThirdParty/upstream/COPYING.LIB',
			'_native/ThirdParty/upstream/source.zip',
			'_native/ThirdParty/gradle/gradle-wrapper.jar',
			'_native/Public/VeroScript.h',
			'_native/Resources/Scripts/stdlib/math.vs',
			'_native/Resources/Tools/CMake/Config.cmake.in',
			'_native/Resources/Assets/font.ttf',
		):
			with self.subTest(name=name):
				self.assertFalse(VALIDATOR.is_forbidden_sdist_source_path('veroscript-0.1.1/' + name))


if __name__ == "__main__":
	unittest.main()
