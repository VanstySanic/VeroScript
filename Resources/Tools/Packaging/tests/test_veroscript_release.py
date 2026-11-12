# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import io
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_release import (PACKAGES, PRODUCTS, WEBASSEMBLY_FILES, archive_entries, archive_member_digest, archive_tree, host_target, native_identity,
							   publish, safe_output, scratch_directory, verify_release, write_manifest)
from installer_fixtures import installer_apphost

CORE_BUILD_ID = hashlib.sha256(b"ar").hexdigest()


def elf(arch="x64"):
	data = bytearray(64)
	data[:6] = b"\x7fELF\x02\x01"
	struct.pack_into("<H", data, 18, 62 if arch == "x64" else 183)
	return bytes(data)


def fixture_archive(source, destination):
	if not destination.name.endswith(".tar.gz"):
		return archive_tree(source, destination)
	destination.parent.mkdir(parents=True, exist_ok=True)
	def mode(info):
		# NTFS cannot store Unix executable modes; encode fixture modes explicitly.
		info.mode = 0o755 if info.isfile() else info.mode
		return info
	with tarfile.open(destination, "w:gz") as archive:
		archive.add(source, arcname=source.name, filter=mode)


class ReleaseContractTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory()
		self.addCleanup(self.temporary.cleanup)
		self.repo = Path(self.temporary.name).resolve()
		(self.repo / ".tmp").mkdir()

	def test_host_target_accepts_platform_architecture_names(self):
		for system, machine, expected in (
			('Windows', 'AMD64', 'win-x64'), ('Windows', 'ARM64', 'win-arm64'),
			('Windows', 'x86', 'win-x86'), ('Linux', 'x86_64', 'linux-x64'),
			('Linux', 'aarch64', 'linux-arm64'), ('Darwin', 'arm64', 'macos-arm64'),
			('Darwin', 'x86_64', 'macos-x64')):
			with self.subTest(system=system, machine=machine), patch('veroscript_release.platform.system', return_value=system), patch('veroscript_release.platform.machine', return_value=machine):
				self.assertEqual(host_target(), expected)

	def test_windows_runtime_rejects_missing_and_wrong_architecture_dependencies(self):
		from veroscript_release import verify_windows_runtime, WINDOWS_RUNTIME_DLLS
		def pe(machine):
			data = bytearray(128)
			data[:2] = b'MZ'
			struct.pack_into('<I', data, 60, 64)
			data[64:68] = b'PE\0\0'
			struct.pack_into('<H', data, 68, machine)
			return bytes(data)
		for arch, machine in (('x64', 0x8664), ('arm64', 0xAA64), ('x86', 0x14C)):
			for binary in ('veroscript.exe', 'veroscript-runtime.exe', 'veroscript-launcher.exe',
						   'VeroScriptBoostModule.dll', 'VeroScriptCurlModule.dll', 'VeroScriptBoostRuntime.dll',
						   'VeroScriptInterop.dll', '_veroscript.cp314.pyd'):
				with self.subTest(arch=arch, binary=binary):
					prefix = 'SDK/Runtime/bin/'
					entries = {prefix + binary: (pe(machine), 0)}
					libraries = WINDOWS_RUNTIME_DLLS if arch == 'x64' else WINDOWS_RUNTIME_DLLS[:2]
					entries.update({prefix + name: (pe(machine), 0) for name in libraries})
					notice = prefix + 'licenses/MicrosoftVisualCpp/NOTICE.txt'
					entries[notice] = (b'Microsoft runtime notice', 0)
					verify_windows_runtime(entries, ('windows', arch))
					optional = prefix + 'msvcp140_atomic_wait.dll'
					entries[optional] = (pe(0xAA64 if machine == 0x8664 else 0x8664), 0)
					with self.assertRaisesRegex(ValueError, 'runtime dependency architecture'):
						verify_windows_runtime(entries, ('windows', arch))
					entries.pop(optional)
					wrong_native = dict(entries)
					wrong_native[prefix + binary] = (pe(0xAA64 if machine == 0x8664 else 0x8664), 0)
					with self.assertRaisesRegex(ValueError, 'native architecture mismatch'):
						verify_windows_runtime(wrong_native, ('windows', arch))
					for name in libraries:
						missing = dict(entries)
						missing.pop(prefix + name)
						with self.assertRaisesRegex(ValueError, 'runtime dependency'):
							verify_windows_runtime(missing, ('windows', arch))
						wrong = dict(entries)
						wrong[prefix + name] = (pe(0xAA64 if machine == 0x8664 else 0x8664), 0)
						with self.assertRaisesRegex(ValueError, 'runtime dependency'):
							verify_windows_runtime(wrong, ('windows', arch))
					entries.pop(notice)
					with self.assertRaisesRegex(ValueError, 'runtime notice'):
						verify_windows_runtime(entries, ('windows', arch))

	def test_windows_wheel_native_architecture_is_checked_after_rehash(self):
		def pe(machine):
			data = bytearray(128)
			data[:2] = b'MZ'
			struct.pack_into('<I', data, 60, 64)
			data[64:68] = b'PE\0\0'
			struct.pack_into('<H', data, 68, machine)
			return bytes(data)

		notice = self.repo / 'Resources/Tools/CMake/WindowsRuntimeNotice.txt'
		notice.parent.mkdir(parents=True)
		notice.write_text('Microsoft runtime notice')
		header = self.repo / 'VeroScript/Public/VeroScriptVersion.h'
		header.parent.mkdir(parents=True)
		header.write_text('// Fixture source-root marker\n')
		root = self.repo / '.tmp/windows-wheel-release'
		wheel = root / 'distributions/python/veroscript-1.0.0-cp314-cp314-win_amd64.whl'
		wheel.parent.mkdir(parents=True)
		for machine in (0x8664, 0xAA64):
			with zipfile.ZipFile(wheel, 'w') as archive:
				archive.writestr('veroscript-1.0.0.dist-info/METADATA', 'Name: veroscript\nVersion: 1.0.0\n')
				archive.writestr('veroscript/_veroscript.cp314-win_amd64.pyd', pe(machine))
				for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
					archive.writestr('veroscript/' + name, pe(0x8664))
				archive.writestr('veroscript/licenses/MicrosoftVisualCpp/NOTICE.txt', 'Microsoft runtime notice')
			write_manifest(root, '1.0.0', 'win-x64', CORE_BUILD_ID)
			if machine == 0x8664:
				verify_release(root, self.repo, businesses={'Python'}, partial_businesses={'Python'})
			else:
				with self.assertRaisesRegex(ValueError, 'native architecture mismatch'):
					verify_release(root, self.repo, businesses={'Python'}, partial_businesses={'Python'})

	def test_windows_arm64_python_extension_machine_is_checked_after_rehash(self):
		def pe(machine):
			data = bytearray(128)
			data[:2] = b'MZ'
			struct.pack_into('<I', data, 60, 64)
			data[64:68] = b'PE\0\0'
			struct.pack_into('<H', data, 68, machine)
			return bytes(data)

		root = self.repo / '.tmp/windows-arm64-release'
		source = self.repo / '.tmp/VeroScriptPython-1.0.0-win-arm64'
		extension = source / 'Runtime/Lib/site-packages/veroscript/_veroscript.cp312-win_arm64.pyd'
		extension.parent.mkdir(parents=True)
		extension.write_bytes(pe(0xAA64))
		for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
			(extension.parent / name).write_bytes(pe(0xAA64))
		notice = extension.parent / 'licenses/MicrosoftVisualCpp/NOTICE.txt'
		notice.parent.mkdir(parents=True)
		notice.write_text('Microsoft runtime notice')
		(source / 'LICENSE').write_bytes((Path(__file__).resolve().parents[4] / 'LICENSE').read_bytes())
		archive = root / 'languages' / (source.name + '.zip')
		archive_tree(source, archive)
		installer = root / 'installer/VeroScriptPythonInstaller-1.0.0-win-arm64.exe'
		installer.parent.mkdir(parents=True)
		installer.write_bytes(installer_apphost(pe(0xAA64)))
		write_manifest(root, '1.0.0', 'win-arm64', CORE_BUILD_ID)
		verify_release(root, businesses={'Python'})

		manifest_path = root / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text())
		manifest['selectedBusinesses'] = ['Python']
		manifest['partialBusinesses'] = ['Python']
		manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
		verifier = Path(__file__).resolve().parents[1] / 'verify-veroscript-release.py'
		command = [sys.executable, str(verifier), '--release-directory', str(root), '--target']
		result = subprocess.run(command + ['win-arm64'], capture_output=True, text=True)
		self.assertEqual(result.returncode, 0, result.stderr)
		result = subprocess.run(command + ['win-x64'], capture_output=True, text=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn('Requested target does not match', result.stderr)

		for filename in ('_veroscript.cp312-win_arm64.pyd', '_veroscript.cp312-win_arm64.PYD',
						 'VEROSCRIPTINTEROP.DLL', 'VEROSCRIPTCPPBINDGEN.EXE'):
			with self.subTest(filename=filename):
				extension.unlink()
				extension = extension.with_name(filename)
				extension.write_bytes(pe(0xAA64))
				archive_tree(source, archive)
				write_manifest(root, '1.0.0', 'win-arm64', CORE_BUILD_ID)
				verify_release(root, businesses={'Python'})

				extension.write_bytes(pe(0x8664))
				archive_tree(source, archive)
				write_manifest(root, '1.0.0', 'win-arm64', CORE_BUILD_ID)
				with self.assertRaisesRegex(ValueError, 'Windows SDK native architecture mismatch'):
					verify_release(root, businesses={'Python'})
				if os.name == 'nt' and filename.endswith('.PYD'):
					manifest = json.loads(manifest_path.read_text())
					manifest['selectedBusinesses'] = ['Python']
					manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
					result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
											 str(verifier.with_suffix('.ps1')), '-ReleaseDirectory',
											 str(root), '-Target', 'win-arm64', '-PythonExecutable', sys.executable],
											capture_output=True)
					self.assertNotEqual(result.returncode, 0)
					self.assertIn(b'Windows SDK native architecture mismatch', result.stderr)

	def make_release(self, root, arch="x64", *, csharp_version='1.0.0'):
		root.mkdir(parents=True)
		target = "linux-" + arch
		build_id = CORE_BUILD_ID
		for category, names in PACKAGES.items():
			for product in names:
				source = self.repo / ".tmp" / (product + "-1.0.0-" + target)
				source.mkdir(exist_ok=True)
				files = {"LICENSE": (Path(__file__).resolve().parents[4] / "LICENSE").read_bytes()}
				if category == "runtime":
					files.update({tool: elf(arch) for tool in ("veroscript", "veroscript-lsp", "veroscript-dap")})
					files.update({"lib/libVeroScriptInterop.so": elf(arch), "include/VeroScript/VeroScriptInterop.h": b"header",
								  "stdlib/core.vs": b"core", "localization/en-US/shared.json": b"{}",
								  "Vm/libVeroScriptVmBytecode.so": elf(arch)})
				if category == "sdk":
					files.update({"lib/libVeroScript.a": b"ar", "lib/libVeroScriptTooling.a": b"ar", "include/Script/VeroScript.h": b"header",
								  "lib/cmake/VeroScript/VeroScriptConfig.cmake": build_id.encode(),
								  "assets/stdlib/core.vs": b"core", "assets/localization/en-US/shared.json": b"{}",
								  "bin/Vm/libVeroScriptVmBytecode.so": elf(arch), "lib/libVeroScriptBytecodeVm.a": b"ar",
								  "include/Vm/VeroScriptBytecodeVm.h": b"header",
								  "lib/cmake/VeroScriptBytecodeVm/VeroScriptBytecodeVmConfig.cmake": b"config",
								  "lib/cmake/VeroScriptExecutionContract/VeroScriptExecutionContractConfig.cmake": b"config"})
				if category == "interop":
					files.update({"lib/libVeroScriptInterop.so": elf(arch), "include/VeroScript/VeroScriptInterop.h": b"header",
								  "lib/cmake/VeroScriptInterop/VeroScriptInteropConfig.cmake": b"config"})
				if category == "jit":
					files.update({"libVeroScriptJitAsmJit.so": elf(arch), "licenses/asmjit/LICENSE.md": b"license",
								  "licenses/asmjit/VERSION.txt": b"version"})
				if category == "vm":
					files.update({"libVeroScriptVmBytecode.so": elf(arch),
								  "veroscript-package.json": json.dumps({"format": "veroscript-package",
									  "formatVersion": "0.1.1", "name": "vs-bytecode-vm", "version": "1.0.0",
									  "entries": {"backends": [{"service": "vm", "id": "vs-bytecode",
										  "contract": "vm", "path": "libVeroScriptVmBytecode.so"}]}}).encode()})
				if category == 'modules':
					from veroscript_products import FOUNDATION_PACKAGE_NAMES, MODULE_PACKAGE_NAMES
					from veroscript_modules import backend_services, module_dependencies
					business = {name: key for key, name in {**MODULE_PACKAGE_NAMES, **FOUNDATION_PACKAGE_NAMES}.items()}[product]
					base = 'VeroScript' + business
					meta = {'schemaVersion': 1, 'business': business, 'version': '1.0.0', 'target': target,
							'profile': 'native-data-sdk',
							'dependencies': module_dependencies(business, {name: '1.0.0' for name in MODULE_PACKAGE_NAMES})}
					if business != 'Json':
						meta['networkImplementationId'] = 'e' * 64
					if business in {'Boost', 'Curl'}:
						meta['backendServices'] = backend_services(business)
					if business == 'Boost':
						meta['boostRuntimeId'] = '0' * 64
						meta.update(nativeTls=False, websocketCompression=False)
					files.update({'veroscript-module.json': json.dumps(meta).encode(),
								  f'lib/lib{base}.a': b'ar', f'include/{base}/{base}.h': b'header',
								  f'lib/cmake/{base}/{base}Config.cmake': b'config',
								  f'lib/cmake/{base}/{base}ConfigVersion.cmake': b'version'})
					for locale in ('en-US', 'fr-FR', 'ja-JP', 'ko-KR', 'zh-CN', 'zh-TW'):
						files[f'share/{base}/Resources/Localization/{locale}/{business.lower()}.json'] = b'{"fixture":"message"}'
					if business == 'Boost':
						files.update({'licenses/VeroScriptBoost/boost/LICENSE_1_0.txt': b'license',
									  'lib/veroscript/modules/libVeroScriptBoostRuntime.so': elf(arch)})
					if business == 'Json':
						files.update({'licenses/VeroScriptJson/jsoncons/LICENSE': b'license',
									  'licenses/VeroScriptJson/quickjs-regexp/LICENSE': b'license'})
					if business == 'Curl':
						files['licenses/VeroScriptCurl/curl/COPYING'] = b'license'
					from veroscript_modules import MODULE_SOURCES
					hooks = {'Network': ('NetworkProvider.h', 'NetworkStream.h', 'NetworkDiagnostics.h', 'NetworkUpgrade.h'),
							 'Http': ('HttpClientHistory.h',), 'WebSocket': ('WebSocketProtocols.h',)}
					for filename in hooks.get(business, ()):
						data = b'provider contract fixture'
						files[f'include/{base}/Backend/{filename}'] = data
						header = self.repo / MODULE_SOURCES[business] / 'Internal' / filename
						header.parent.mkdir(parents=True, exist_ok=True)
						header.write_bytes(data)
				if category == "symbols":
					files["veroscript.debug"] = b"symbols"
				if category == "languages":
					files.update({module + "/component.json": b"{}" for module in ("Runtime", "Tools", "Aot", "Symbols")})
					if product == "VeroScriptCpp":
						files.update({"Runtime/lib/libVeroScriptCpp.a": b"ar", "Runtime/include/Cpp/VeroScriptSession.h": b"header",
									  "Runtime/lib/cmake/VeroScriptCpp/VeroScriptCppConfig.cmake": build_id.encode()})
					if product == "VeroScriptCSharp":
						for framework in ("net8.0", "net9.0"):
							files[f"Runtime/lib/dotnet/{framework}/VeroScriptCSharp.dll"] = b"managed"
							files[f"Runtime/lib/dotnet/{framework}/libVeroScriptInterop.so"] = elf(arch)
					if product == "VeroScriptPython":
						prefix = "Runtime/lib/python3.13/site-packages/veroscript/"
						for name in ("__init__.py", "_veroscript.pyi", "py.typed"):
							files[prefix + name] = b"facade"
						files[prefix + "libVeroScriptInterop.so"] = elf(arch)
						files[prefix + "_veroscript.cpython-313.so"] = elf(arch)
				from veroscript_products import FOUNDATION_PACKAGE_NAMES
				if product in FOUNDATION_PACKAGE_NAMES.values():
					from foundation_fixtures import foundation_files
					business = {name: key for key, name in FOUNDATION_PACKAGE_NAMES.items()}[product]
					files = foundation_files(business, {key: '1.0.0' for key in FOUNDATION_PACKAGE_NAMES}, target, elf(arch), files['LICENSE'])
				for name, data in files.items():
					destination = source / name
					destination.parent.mkdir(parents=True, exist_ok=True)
					destination.write_bytes(data)
					if name.startswith("veroscript") and not name.endswith(".debug"):
						destination.chmod(0o755)
				fixture_archive(source, root / category / (source.name + ".tar.gz"))
		web = self.repo / ".tmp/VeroScriptWebAssemblySDK-1.0.0-wasm32"
		web.mkdir(exist_ok=True)
		for name in WEBASSEMBLY_FILES:
			(web / name).write_bytes(b"web")
		(web / 'LICENSE').write_bytes((Path(__file__).resolve().parents[4] / 'LICENSE').read_bytes())
		archive_tree(web, root / "webassembly/VeroScriptWebAssemblySDK-1.0.0-wasm32.zip")
		for product in PRODUCTS:
			installer = root / "installer" / f"VeroScript{product}Installer-1.0.0-{target}"
			installer.parent.mkdir(exist_ok=True)
			installer.write_bytes(installer_apphost(elf(arch)))
			installer.chmod(0o755)
			fixture_archive(installer, installer.with_name(installer.name + ".tar.gz"))
			installer.unlink()
		(root / "Resources").mkdir()
		(root / "Resources/README.md").write_text("Resources")
		(root / "Resources/Assets/icons").mkdir(parents=True)
		(root / "Resources/Assets/icons/icon.svg").write_text("<svg/>")
		from veroscript_csharp_packages import PACKAGE_IDS
		feed = root / 'distributions/csharp'
		feed.mkdir(parents=True)
		for package in PACKAGE_IDS:
			for suffix in ('nupkg', 'snupkg'):
				with zipfile.ZipFile(feed / f'{package}.{csharp_version}.{suffix}', 'w') as archive:
					archive.writestr(zipfile.ZipInfo(f'{package}.nuspec'),
						f'<package><metadata><id>{package}</id><version>{csharp_version}</version></metadata></package>')
					extension = 'dll' if suffix == 'nupkg' else 'pdb'
					archive.writestr(zipfile.ZipInfo(f'lib/net9.0/{package}.{extension}'), b'managed payload')
		write_manifest(root, "1.0.0", target, build_id)
		return root

	def test_full_unix_inventory_and_executable_modes(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		self.assertEqual(verify_release(root)["architecture"], "x64")
		write_manifest(root, "1.0.0", "linux-x64", CORE_BUILD_ID)
		self.assertEqual(verify_release(root)["target"], "linux-x64")

	def test_sdk_offline_package_hashes_use_the_complete_archive_member(self):
		root = self.make_release(self.repo / '.tmp/canonical-feed')
		package = root / 'distributions/csharp/VeroScript.CSharp.1.0.0.nupkg'
		with zipfile.ZipFile(package, 'w') as archive:
			archive.writestr('VeroScript.CSharp.nuspec', '<package><metadata><id>VeroScript.CSharp</id><version>1.0.0</version></metadata></package>')
			archive.writestr('lib/net8.0/VeroScriptCSharp.dll', b'managed payload' * 10000)
		source = self.repo / '.tmp/VeroScriptCSharp-1.0.0-linux-x64'
		offline = source / 'Runtime/share/VeroScript/nuget' / package.name
		offline.parent.mkdir(parents=True)
		offline.write_bytes(package.read_bytes())
		sdk = root / 'languages' / (source.name + '.tar.gz')
		fixture_archive(source, sdk)
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		verify_release(root)
		offline.write_bytes(package.read_bytes() + b'changed offline copy')
		fixture_archive(source, sdk)
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		with self.assertRaisesRegex(ValueError, 'SDK offline NuGet differs'):
			verify_release(root)

	def test_complete_desktop_csharp_requires_the_canonical_nuget_inventory(self):
		root = self.make_release(self.repo / '.tmp/missing-nuget')
		missing = 'VeroScript.CSharpBindGen.Tool.1.0.0.snupkg'
		(root / 'distributions/csharp' / missing).unlink()
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		with self.assertRaisesRegex(ValueError, r'missing=.*CSharpBindGen\.Tool\.1\.0\.0\.snupkg'):
			verify_release(root)
		verify_release(root, partial_businesses={'CSharp'})

	def test_installable_vm_uses_its_dedicated_backend_contract(self):
		from veroscript_vm import verify_installable_backend
		manifest = {'format': 'veroscript-package', 'formatVersion': '0.1.1',
					'name': 'vs-bytecode-vm', 'version': '0.1.1', 'licenseFile': 'LICENSE',
					'target': {'os': 'linux', 'arch': 'x64', 'environment': 'desktop',
							   'coreBuild': 'c' * 64, 'abis': {'vm': 'matching-contract'}},
					'entries': {'libraries': [], 'backends': [{'service': 'vm', 'id': 'vs-bytecode',
								'contract': 'vm', 'path': 'libVeroScriptVmBytecode.so', 'libraries': []}]}}
		entries = {'LICENSE': (b'license', 0), 'libVeroScriptVmBytecode.so': (elf(), 0)}
		for contract in ('vm', 'jit'):
			manifest['entries']['backends'][0]['contract'] = contract
			entries['veroscript-package.json'] = (json.dumps(manifest).encode(), 0)
			if contract == 'vm':
				verify_installable_backend(entries, '0.1.1', 'linux-x64')
			else:
				with self.assertRaisesRegex(ValueError, 'VM identity/entry mismatch'):
					verify_installable_backend(entries, '0.1.1', 'linux-x64')

	def test_bundled_vm_is_required_and_matches_architecture_after_rehash(self):
		root = self.make_release(self.repo / '.tmp/vm-products')
		for category, product, relative in (
				('runtime', 'VeroScriptRuntime', 'Vm/libVeroScriptVmBytecode.so'),
				('sdk', 'VeroScriptSDK', 'bin/Vm/libVeroScriptVmBytecode.so')):
			source = self.repo / '.tmp' / (product + '-1.0.0-linux-x64')
			library = source / relative
			archive = root / category / (source.name + '.tar.gz')
			for data in (b'', elf('arm64')):
				with self.subTest(category=category, data=data[:6]):
					library.write_bytes(data)
					fixture_archive(source, archive)
					write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
					with self.assertRaisesRegex(ValueError, 'bundled VM backend'):
						verify_release(root)
			library.write_bytes(elf())
			fixture_archive(source, archive)
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		verify_release(root)

	def test_old_jit_archive_name_is_rejected(self):
		root = self.make_release(self.repo / '.tmp/historical-jit')
		canonical = root / 'jit/vs-asmjit-1.0.0-linux-x64.tar.gz'
		source = self.repo / '.tmp/VeroScriptJitAsmJit-1.0.0-linux-x64'
		with tarfile.open(canonical) as reader:
			for member in reader:
				if member.isfile():
					path = source / Path(member.name).relative_to('vs-asmjit-1.0.0-linux-x64')
					path.parent.mkdir(parents=True, exist_ok=True)
					path.write_bytes(reader.extractfile(member).read())
		fixture_archive(source, root / ('jit/' + source.name + '.tar.gz'))
		canonical.unlink()
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		with self.assertRaises(ValueError):
			verify_release(root)

	def test_module_documentation_is_rejected_after_rehash(self):
		root = self.make_release(self.repo / '.tmp/module-docs')
		path = root / 'jit/vs-asmjit-1.0.0-linux-x64.tar.gz'
		with tarfile.open(path, 'r:gz') as archive:
			members = [(item, archive.extractfile(item).read() if item.isfile() else None) for item in archive]
		with tarfile.open(path, 'w:gz') as archive:
			for item, data in members:
				archive.addfile(item, io.BytesIO(data) if data is not None else None)
			item = tarfile.TarInfo('vs-asmjit-1.0.0-linux-x64/Docs/guide.md')
			item.size = 5
			archive.addfile(item, io.BytesIO(b'guide'))
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		with self.assertRaisesRegex(ValueError, 'Non-functional'):
			verify_release(root)

	def test_functional_sdk_contract_rejects_guides_and_readme(self):
		root = self.make_release(self.repo / '.tmp/functional-sdk')
		source = self.repo / '.tmp/VeroScriptSDK-1.0.0-linux-x64'
		(source / 'README.md').write_text('Core SDK setup and compatibility.')
		guide = source / 'Docs/guide.md'
		guide.parent.mkdir()
		guide.write_text('Optional manual')
		archive = root / 'sdk' / (source.name + '.tar.gz')
		for expected_failure in (True, False):
			if not expected_failure:
				guide.unlink()
				(source / 'README.md').unlink()
			fixture_archive(source, archive)
			write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
			path = root / 'release-manifest.json'
			manifest = json.loads(path.read_text())
			manifest['functionalPayloadBusinesses'] = ['CoreSDK']
			path.write_text(json.dumps(manifest))
			if expected_failure:
				with self.assertRaisesRegex(ValueError, 'Non-functional'):
					verify_release(root)
			else:
				verify_release(root)

	@unittest.skipUnless(os.name == 'nt', 'Requires Windows PowerShell')
	def test_powershell_unix_audit_defaults_to_manifest_target(self):
		from veroscript_business_release import artifact_business

		root = self.make_release(self.repo / '.tmp/candidate')
		manifest_path = root / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text())
		for artifact in manifest['artifacts']:
			name = artifact['path']
			if not name.startswith('Resources/') and artifact_business(name) != 'Python':
				(root / name).unlink()
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		manifest = json.loads(manifest_path.read_text())
		manifest['selectedBusinesses'] = ['Python']
		manifest['partialBusinesses'] = ['Python']
		manifest_path.write_text(json.dumps(manifest), encoding='utf-8')

		verifier = Path(__file__).resolve().parents[1] / 'verify-veroscript-release.ps1'
		command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
				   str(verifier), '-ReleaseDirectory', str(root), '-PythonExecutable', sys.executable]
		for arguments in ([], ['-Target', 'linux-x64'], ['-Version', '1.0.0']):
			with self.subTest(arguments=arguments):
				result = subprocess.run(command + arguments, capture_output=True)
				self.assertEqual(result.returncode, 0, result.stderr)
		result = subprocess.run(command + ['-Target', 'linux-arm64'], capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn(b'Requested target does not match', result.stderr)
		result = subprocess.run(command + ['-Version', '0.1.1'], capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn(b'Requested version', result.stderr)
		manifest['businessVersions'] = {}
		manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
		result = subprocess.run(command + ['-Version', '0.1.1'], capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn(b'Requested version', result.stderr)
		manifest['nativeVersion'] = '1.0.0'
		manifest['businessVersions'] = {'Python': '9.9.9', 'Cpp': '1.0.0'}
		manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
		result = subprocess.run(command + ['-Version', '1.0.0'], capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn(b'Requested version', result.stderr)
		manifest['businessVersions'] = {'Python': '1.0.0', 'Cpp': '9.9.9'}
		# The aggregate version may belong to an unselected CoreSDK business.
		manifest['version'] = '2.0.0'
		manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
		result = subprocess.run(command + ['-Version', '1.0.0'], capture_output=True)
		self.assertEqual(result.returncode, 0, result.stderr)

	@unittest.skipUnless(os.name == 'nt', 'Requires Windows PowerShell')
	def test_powershell_requested_version_checks_default_full_selection(self):
		from veroscript_business_release import ALL_BUSINESSES

		root = self.make_release(self.repo / '.tmp/candidate')
		web_archive = root / 'webassembly/VeroScriptWebAssemblySDK-1.0.0-wasm32.zip'
		web_source = self.repo / '.tmp/VeroScriptWebAssemblySDK-1.0.0-wasm32'
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID,
					   business_versions={business: '1.0.0' for business in ALL_BUSINESSES},
					   native_version='1.0.0')
		manifest_path = root / 'release-manifest.json'
		manifest = json.loads(manifest_path.read_text())
		self.assertEqual(verify_release(root)['version'], '1.0.0')
		verifier = Path(__file__).resolve().parents[1] / 'verify-veroscript-release.ps1'
		command = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
				   str(verifier), '-ReleaseDirectory', str(root), '-PythonExecutable', sys.executable]
		for selection in (None, []):
			with self.subTest(selection=selection):
				manifest['selectedBusinesses'] = selection
				manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
				result = subprocess.run(command + ['-Version', '0.1.1'], capture_output=True)
				self.assertNotEqual(result.returncode, 0)
				self.assertIn(b'Requested version', result.stderr)

	def test_native_package_requires_project_license(self):
		root = self.make_release(self.repo / '.tmp/candidate')
		source = self.repo / '.tmp/VeroScriptRuntime-1.0.0-linux-x64'
		archive = root / 'runtime/VeroScriptRuntime-1.0.0-linux-x64.tar.gz'
		for notice in (None, b''):
			with self.subTest(notice=notice):
				if notice is None:
					(source / 'LICENSE').unlink()
				else:
					(source / 'LICENSE').write_bytes(notice)
				fixture_archive(source, archive)
				write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
				with self.assertRaisesRegex(ValueError, 'Missing/empty project license'):
					verify_release(root)

	def test_native_package_license_must_match_source_notice(self):
		root = self.make_release(self.repo / '.tmp/candidate')
		manifest = json.loads((root / 'release-manifest.json').read_text())
		(self.repo / 'LICENSE').write_bytes((Path(__file__).resolve().parents[4] / 'LICENSE').read_bytes())
		for item in manifest['artifacts']:
			if not item['path'].startswith(('runtime/', 'sdk/', 'interop/', 'languages/', 'symbols/', 'webassembly/')):
				continue
			archive = root / item['path']
			source = self.repo / '.tmp' / archive.name.removesuffix('.tar.gz').removesuffix('.zip')
			(source / 'LICENSE').write_text('Old project notice', encoding='utf-8')
			fixture_archive(source, archive)
		write_manifest(root, '1.0.0', 'linux-x64', CORE_BUILD_ID)
		with self.assertRaisesRegex(ValueError, 'Stale project license'):
			verify_release(root, self.repo)

	def test_tampering_and_unlisted_files_are_rejected(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		(root / "Resources/README.md").write_text("changed")
		with self.assertRaisesRegex(ValueError, "Missing/stale"):
			verify_release(root)

	def test_shared_asset_tampering_is_rejected(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		(root / "Resources/Assets/icons/icon.svg").write_text("changed")
		with self.assertRaisesRegex(ValueError, "Missing/stale"):
			verify_release(root)

	def test_incomplete_web_payload_is_rejected_after_rehash(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		web = self.repo / ".tmp/VeroScriptWebAssemblySDK-1.0.0-wasm32"
		for name in WEBASSEMBLY_FILES:
			for missing in (True, False):
				with self.subTest(name=name, missing=missing):
					if missing:
						(web / name).unlink()
					else:
						(web / name).write_bytes(b"")
					archive_tree(web, root / "webassembly/VeroScriptWebAssemblySDK-1.0.0-wasm32.zip")
					write_manifest(root, "1.0.0", "linux-x64", CORE_BUILD_ID)
					with self.assertRaisesRegex(ValueError, "Missing/empty WebAssembly SDK"):
						verify_release(root)
					(web / name).write_bytes(b"web")

	def test_manifest_traversal_and_duplicate_entries_are_rejected(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		manifest = json.loads((root / "release-manifest.json").read_text())
		manifest["artifacts"][0]["path"] = "../outside"
		(root / "release-manifest.json").write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, "Unsafe"):
			verify_release(root)
		manifest["artifacts"][0]["path"] = manifest["artifacts"][1]["path"]
		manifest["artifacts"][0] = dict(manifest["artifacts"][1])
		(root / "release-manifest.json").write_text(json.dumps(manifest))
		with self.assertRaisesRegex(ValueError, "duplicate"):
			verify_release(root)

	def test_component_output_cannot_modify_full_release(self):
		root = self.make_release(self.repo / ".tmp/candidate-publication/1.0.0/linux-x64")
		with self.assertRaisesRegex(ValueError, "complete release"):
			safe_output(self.repo, root / "runtime", component=True)
		path = self.repo / ".tmp/candidate-publication/1.0.0/components/linux-x64/runtime"
		self.assertEqual(safe_output(self.repo, path, component=True), path)

	def test_wrong_installer_target_is_rejected_after_rehash(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		installer = root / "installer/VeroScriptRuntimeInstaller-1.0.0-linux-x64"
		installer.write_bytes(installer_apphost(elf('arm64')))
		fixture_archive(installer, installer.with_name(installer.name + '.tar.gz'))
		installer.unlink()
		write_manifest(root, "1.0.0", "linux-x64", CORE_BUILD_ID)
		with self.assertRaisesRegex(ValueError, "architecture mismatch|archive mismatch"):
			verify_release(root)

	def test_tar_symlink_escape_is_rejected(self):
		archive = self.repo / "link.tar.gz"
		with tarfile.open(archive, "w:gz") as output:
			entry = tarfile.TarInfo("root/link")
			entry.type = tarfile.SYMTYPE
			entry.linkname = "../../outside"
			output.addfile(entry)
		with self.assertRaisesRegex(ValueError, "Unsafe archive link"):
			archive_entries(archive)

	def test_tar_gzip_footer_corruption_and_truncation_are_rejected(self):
		archive = self.repo / "footer.tar.gz"
		with tarfile.open(archive, "w:gz") as output:
			entry = tarfile.TarInfo("root/tool")
			data = b"payload" * (300 * 1024)
			entry.size = len(data)
			output.addfile(entry, io.BytesIO(data))
		original = archive.read_bytes()
		self.assertEqual(archive_entries(archive, max_entry_bytes=32)["root/tool"][0], data[:32])
		for mutation in ("crc", "size", "truncated"):
			damaged = bytearray(original)
			if mutation == "truncated":
				del damaged[-8:]
			else:
				damaged[-8 if mutation == "crc" else -4] ^= 1
			archive.write_bytes(damaged)
			for prefix in (None, 32):
				with self.subTest(mutation=mutation, prefix=prefix), self.assertRaises((OSError, EOFError)):
					archive_entries(archive, max_entry_bytes=prefix)

	def test_tar_gzip_corruption_is_rejected_after_rehash(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		archive = root / "runtime/VeroScriptRuntime-1.0.0-linux-x64.tar.gz"
		damaged = bytearray(archive.read_bytes())
		damaged[-8] ^= 1
		archive.write_bytes(damaged)
		write_manifest(root, "1.0.0", "linux-x64", CORE_BUILD_ID)
		with self.assertRaises((OSError, EOFError)):
			verify_release(root)

	def test_core_library_identity_is_checked_after_rehash(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		source = self.repo / ".tmp/VeroScriptSDK-1.0.0-linux-x64"
		(source / "lib/libVeroScript.a").write_bytes(b"different Core build")
		fixture_archive(source, root / "sdk" / (source.name + ".tar.gz"))
		write_manifest(root, "1.0.0", "linux-x64", CORE_BUILD_ID)
		with self.assertRaisesRegex(ValueError, "Core library build identity mismatch"):
			verify_release(root)

	def test_archive_identity_hashes_the_full_member_in_zip_and_tar(self):
		source = self.repo / ".tmp/core"
		source.mkdir()
		data = b"a" * (2 * 1024 * 1024) + b"distinct tail"
		(source / "static.a").write_bytes(data)
		for suffix in (".zip", ".tar.gz"):
			with self.subTest(suffix=suffix):
				archive = self.repo / ("core" + suffix)
				archive_tree(source, archive)
				self.assertEqual(archive_member_digest(archive, "core/static.a"), hashlib.sha256(data).hexdigest())

	def test_sdk_does_not_require_packaged_examples(self):
		root = self.make_release(self.repo / "dist/release")
		source = self.repo / ".tmp/VeroScriptSDK-1.0.0-linux-x64"
		fixture_archive(source, root / "sdk" / (source.name + ".tar.gz"))
		write_manifest(root, "1.0.0", "linux-x64", CORE_BUILD_ID)
		verify_release(root)

	def test_sdk_tool_execution_mode_is_checked_after_rehash(self):
		root = self.make_release(self.repo / ".tmp/candidate")
		source = self.repo / ".tmp/VeroScriptCpp-1.0.0-linux-x64"
		tool = source / "Tools/bin/VeroScriptCppBindGen"
		tool.parent.mkdir(parents=True)
		tool.write_bytes(elf())
		archive = root / "languages" / (source.name + ".tar.gz")
		def mode(info):
			info.mode = 0o644 if info.name.endswith("/VeroScriptCppBindGen") else 0o755
			return info
		with tarfile.open(archive, "w:gz") as packed:
			packed.add(source, arcname=source.name, filter=mode)
		write_manifest(root, "1.0.0", "linux-x64", CORE_BUILD_ID)
		with self.assertRaisesRegex(ValueError, "SDK tool is not executable"):
			verify_release(root)

	def test_native_library_link_is_preserved_and_audited(self):
		archive = self.repo / "library.tar.gz"
		with tarfile.open(archive, "w:gz") as output:
			entry = tarfile.TarInfo("root/lib/lib.so.1")
			data = elf()
			entry.size = len(data)
			entry.mode = 0o755
			output.addfile(entry, io.BytesIO(data))
			entry = tarfile.TarInfo("root/lib/lib.so")
			entry.type = tarfile.SYMTYPE
			entry.linkname = "lib.so.1"
			output.addfile(entry)
		entries = archive_entries(archive)
		self.assertEqual(entries["root/lib/lib.so"], entries["root/lib/lib.so.1"])

	def test_dangling_library_link_is_rejected(self):
		archive = self.repo / "dangling.tar.gz"
		with tarfile.open(archive, "w:gz") as output:
			entry = tarfile.TarInfo("root/lib.so")
			entry.type = tarfile.SYMTYPE
			entry.linkname = "missing.so"
			output.addfile(entry)
		with self.assertRaisesRegex(ValueError, "Dangling/cyclic"):
			archive_entries(archive)

	def test_zip_path_escape_is_rejected(self):
		archive = self.repo / "bad.zip"
		with zipfile.ZipFile(archive, "w") as output:
			output.writestr("../outside", b"bad")
		with self.assertRaisesRegex(ValueError, "Unsafe"):
			archive_entries(archive)

	def test_output_cannot_replace_source_or_collection(self):
		for name in ("Script/VeroScript", "Script/Release", "dist", "../outside"):
			with self.subTest(name=name), self.assertRaises(ValueError):
				safe_output(self.repo, self.repo / name)

	def test_output_rejects_linked_ancestor(self):
		target = self.repo / "unrelated-source"
		target.mkdir()
		link = self.repo / ".tmp/linked-output"
		if os.name == "nt":
			subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)],
						   check=True, capture_output=True)
		else:
			link.symlink_to(target, target_is_directory=True)
		with self.assertRaisesRegex(ValueError, "Linked output"):
			safe_output(self.repo, link / "release")
		self.assertFalse((target / "release").exists())

	def test_target_publication_preserves_siblings_and_rolls_back(self):
		old = self.make_release(self.repo / ".tmp/candidate-publication/1.0.0/linux-x64")
		sibling = self.repo / ".tmp/candidate-publication/1.0.0/win-x64"
		sibling.mkdir()
		(sibling / "keep").write_text("untouched")
		candidate = self.make_release(self.repo / ".tmp/candidate")
		original = Path.rename
		def rename(path, target):
			if path == candidate:
				raise OSError("simulated publish failure")
			return original(path, target)
		with patch.object(Path, "rename", rename), patch("veroscript_release.verify_release", return_value={"version": "1.0.0", "platform": "linux", "architecture": "x64"}):
			with self.assertRaisesRegex(OSError, "simulated"):
				publish(self.repo, candidate, old)
		self.assertTrue((old / "release-manifest.json").is_file())
		self.assertEqual((sibling / "keep").read_text(), "untouched")

	def test_linked_scratch_cannot_redirect_staging_or_backup(self):
		candidate = self.make_release(self.repo / "candidate")
		destination = self.make_release(self.repo / "dist/published")
		(self.repo / ".tmp").rename(self.repo / "original-scratch")
		outside = Path(tempfile.mkdtemp())
		self.addCleanup(lambda: outside.rmdir())
		link = self.repo / ".tmp"
		if os.name == "nt":
			subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(outside)],
						   check=True, capture_output=True)
			self.addCleanup(lambda: os.rmdir(link))
		else:
			link.symlink_to(outside, target_is_directory=True)
			self.addCleanup(link.unlink)
		with self.assertRaisesRegex(ValueError, "Linked release scratch"):
			scratch_directory(self.repo)
		# The fixture validates archive contracts; it has no real SDK source tree.
		identity = verify_release(candidate)
		with patch("veroscript_release.verify_release", return_value=identity):
			with self.assertRaisesRegex(ValueError, "Linked release scratch"):
				publish(self.repo, candidate, destination)
		self.assertTrue(candidate.is_dir())
		self.assertTrue((destination / "release-manifest.json").is_file())
		self.assertEqual(list(outside.iterdir()), [])

	def test_publication_cannot_replace_another_target(self):
		old = self.make_release(self.repo / ".tmp/published", arch="arm64")
		candidate = self.make_release(self.repo / ".tmp/candidate")
		with patch("veroscript_release.verify_release", return_value={"version": "1.0.0", "platform": "linux", "architecture": "x64"}):
			with self.assertRaisesRegex(ValueError, "different version/target"):
				publish(self.repo, candidate, old)
		self.assertEqual(json.loads((old / "release-manifest.json").read_text())["architecture"], "arm64")
		self.assertTrue(candidate.is_dir())

	def test_native_architecture_is_read_from_binary(self):
		self.assertEqual(native_identity(elf("arm64")), ("linux", "arm64"))
		self.assertEqual(native_identity(elf("x64")), ("linux", "x64"))
		self.assertEqual(native_identity(b"\xcf\xfa\xed\xfe" + struct.pack("<I", 0x0100000c)), ("macos", "arm64"))
		with self.assertRaises(ValueError):
			native_identity(b"not a binary")


if __name__ == "__main__":
	unittest.main()
