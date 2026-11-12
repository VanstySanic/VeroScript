# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Portable contract fixtures; synthetic native files do not establish device acceptance."""
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

MODULE = Path(__file__).resolve().parents[2]
REPO = MODULE.parents[3]
sys.path.insert(0, str(REPO / "Resources/Tools/Packaging/tests"))
from test_veroscript_mobile import aar_entries, ar, macho

spec = importlib.util.spec_from_file_location("csharp_mobile_sdk", MODULE.parent / "Resources/Tools/Mobile/prepare-native.py")
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class MobileSdkTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory(prefix="veroscript-mobile-sdk-test-")
		self.addCleanup(self.temporary.cleanup)
		self.root = Path(self.temporary.name)
		self.android = self.root / "android.aar"
		entries = aar_entries()
		entries["prefab/modules/VeroScriptInterop/include/VeroScriptInterop.h"] = (
			REPO / "VeroScriptInterop/Public/VeroScriptInterop.h").read_bytes()
		for route, data in sdk.resources().items():
			entries["assets/veroscript/" + route] = data
		with zipfile.ZipFile(self.android, "w") as archive:
			for route, data in entries.items():
				archive.writestr(route, data)
		self.ios = self.root / "ios"
		self.framework = self.ios / "VeroScriptIOS.xcframework"
		libraries = []
		for identifier, architectures, variant in (("ios-arm64", ["arm64"], "device"),
													("ios-arm64_x86_64-simulator", ["arm64", "x86_64"], "simulator")):
			directory = self.framework / identifier
			(directory / "Headers").mkdir(parents=True)
			(directory / "Headers/VeroScriptInterop.h").write_bytes(
				(REPO / "VeroScriptInterop/Public/VeroScriptInterop.h").read_bytes())
			arm = ar(macho(platform=2 if variant == "device" else 7))
			if variant == "simulator":
				x64 = ar(macho(cpu=0x1000007, platform=7))
				arm = (struct.pack(">II", 0xCAFEBABE, 2)
					   + struct.pack(">IIIII", 0x100000C, 0, 48, len(arm), 0)
					   + struct.pack(">IIIII", 0x1000007, 0, 48 + len(arm), len(x64), 0) + arm + x64)
			(directory / "mobile.a").write_bytes(arm)
			library = {"LibraryIdentifier": identifier, "SupportedArchitectures": architectures,
					   "SupportedPlatform": "ios", "LibraryPath": "mobile.a", "HeadersPath": "Headers"}
			if variant == "simulator":
				library["SupportedPlatformVariant"] = "simulator"
			libraries.append(library)
		(self.framework / "Info.plist").write_bytes(plistlib.dumps({"AvailableLibraries": libraries}))
		for route, data in sdk.resources().items():
			path = self.ios / "assets" / route
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		self.output = self.root / "native"

	def test_prepare_verify_and_preserve_existing_output(self):
		sdk.prepare(self.android, self.ios, self.output)
		metadata = sdk.verify(self.output)
		self.assertFalse(metadata["nativeJit"])
		self.assertEqual(set(sdk.verify_framework(self.framework)), {"device-arm64", "simulator-arm64", "simulator-x64"})
		self.assertFalse(any("libVeroScriptAndroid.so" in route for route in metadata["files"]))
		self.assertIn("licenses/NOTICE.toolchain", metadata["files"])
		self.assertFalse((self.output / "Resources/Docs").exists())
		self.assertFalse((self.output / "README.md").exists())
		symbols = ET.fromstring(sdk.static_symbols())
		retained = {item.get("Include") for item in symbols.findall(".//ReferenceNativeSymbol")}
		expected = {line.strip() for line in
					(REPO / "VeroScriptInterop/Private/VeroScriptInterop.def")
					.read_text(encoding="utf-8-sig").splitlines()
					if line.strip().startswith("vs_interop_")}
		self.assertEqual(retained, expected)
		self.assertIn("vs_interop_memory_acquire_lease", retained)
		self.assertIn("vs_interop_memory_release_lease", retained)
		self.assertIn("vs_interop_session_set_memory_limits", retained)
		self.assertIn("vs_interop_numeric_parse", retained)
		self.assertIn("vs_interop_value_set_variant", retained)
		self.assertIn("vs_interop_application_services_create", retained)
		self.assertIn("vs_interop_session_set_stdlib_limits", retained)
		with self.assertRaises(ValueError):
			sdk.prepare(self.android, self.ios, self.output)
		self.assertEqual(metadata, sdk.verify(self.output))
		(self.output / "android/arm64-v8a/libVeroScriptInterop.so").write_bytes(b"corrupted")
		with self.assertRaisesRegex(ValueError, "changed"):
			sdk.verify(self.output)

	def test_wrong_platform_archive_is_rejected(self):
		(self.framework / "ios-arm64/mobile.a").write_bytes(ar(macho(platform=1)))
		with self.assertRaisesRegex(ValueError, "Apple platform"):
			sdk.prepare(self.android, self.ios, self.output)
		self.assertFalse(self.output.exists())

	def test_framework_traversal_and_missing_slice_are_rejected(self):
		info = plistlib.loads((self.framework / "Info.plist").read_bytes())
		info["AvailableLibraries"][0]["LibraryIdentifier"] = "../escape"
		(self.framework / "Info.plist").write_bytes(plistlib.dumps(info))
		with self.assertRaises(ValueError):
			sdk.verify_framework(self.framework)
		info["AvailableLibraries"] = info["AvailableLibraries"][1:]
		(self.framework / "Info.plist").write_bytes(plistlib.dumps(info))
		with self.assertRaisesRegex(ValueError, "Missing iOS"):
			sdk.verify_framework(self.framework)

	def test_stale_resource_is_rejected_before_publication(self):
		(self.ios / "assets/stdlib/core.vs").write_bytes(b"stale")
		with self.assertRaisesRegex(ValueError, "resources"):
			sdk.prepare(self.android, self.ios, self.output)
		self.assertFalse(self.output.exists())

	def test_fat_slices_cannot_overlap(self):
		path = self.framework / "ios-arm64_x86_64-simulator/mobile.a"
		data = bytearray(path.read_bytes())
		struct.pack_into(">I", data, 36, 48)
		with self.assertRaisesRegex(ValueError, "Overlapping"):
			sdk.archive_slices(bytes(data))

	def test_fat_slices_require_declared_alignment(self):
		path = self.framework / "ios-arm64_x86_64-simulator/mobile.a"
		data = bytearray(path.read_bytes())
		for alignment in (14, 32):
			with self.subTest(alignment=alignment):
				struct.pack_into(">I", data, 24, alignment)
				with self.assertRaisesRegex(ValueError, "alignment"):
					sdk.archive_slices(bytes(data))
		slices = sdk.archive_slices(path.read_bytes())
		arm, intel = slices["arm64"], slices["x64"]
		wide = (struct.pack(">II", 0xCAFEBABF, 2)
				+ struct.pack(">IIQQII", 0x100000C, 0, 72, len(arm), 0, 0)
				+ struct.pack(">IIQQII", 0x1000007, 0, 72 + len(arm), len(intel), 0, 0)
				+ arm + intel)
		self.assertEqual(sdk.archive_slices(wide), slices)
		data = bytearray(wide)
		struct.pack_into(">I", data, 32, 14)
		with self.assertRaisesRegex(ValueError, "alignment"):
			sdk.archive_slices(bytes(data))

	def test_undeclared_native_payloads_are_rejected(self):
		unexpected = self.framework / "unadvertised"
		unexpected.write_bytes(macho(platform=1))
		with self.assertRaisesRegex(ValueError, "Unexpected native payload"):
			sdk.prepare(self.android, self.ios, self.output)
		self.assertFalse(self.output.exists())
		unexpected.unlink()
		sdk.prepare(self.android, self.ios, self.output)
		metadata_path = self.output / "mobile-sdk.json"
		metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
		for route, data in (("android/arm64-v8a/libVeroScriptAndroid.so", b"unadvertised library"),
							("ios/VeroScriptIOS.xcframework/extra", macho(platform=1)),
							("assets/extensionless-binary", macho(platform=1))):
			with self.subTest(route=route):
				target = self.output / route
				target.write_bytes(data)
				metadata["files"][route] = sdk.digest(data)
				metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
				with self.assertRaisesRegex(ValueError, "Unexpected native payload"):
					sdk.verify(self.output)
				target.unlink()
				del metadata["files"][route]
		metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
		self.assertEqual(sdk.verify(self.output), metadata)

	def test_resource_routes_match_the_portable_installer_contract(self):
		for route in ("stdlib/.. /escape.vs", "stdlib/folder./escape.vs", "stdlib/../../escape.vs", "stdlib/foo\\bar.vs"):
			with self.subTest(route=route), self.assertRaises(ValueError):
				sdk.resource_route(route)

	def test_msbuild_selects_assets_and_fails_for_invalid_consumers(self):
		dotnet = os.environ.get("VEROSCRIPT_CSHARP_MOBILE_DOTNET") or shutil.which("dotnet")
		if dotnet is None:
			self.skipTest("dotnet MSBuild is unavailable")
		sdk.prepare(self.android, self.ios, self.output)
		project = self.root / "consumer.proj"
		project.write_text(f'''<Project>
  <PropertyGroup>
    <TargetFramework>net10.0-ios</TargetFramework><OutputType>Exe</OutputType><UseMaui>true</UseMaui>
    <RuntimeIdentifier>ios-arm64</RuntimeIdentifier><SupportedOSPlatformVersion>15.0</SupportedOSPlatformVersion>
    <VeroScriptMobileNativeRoot>{self.output.as_posix()}</VeroScriptMobileNativeRoot>
  </PropertyGroup>
  <ItemGroup><PackageReference Include="VeroScript.CSharp" Version="0.1.1" /></ItemGroup>
  <Import Project="{(MODULE / 'buildTransitive/VeroScript.CSharp.targets').as_posix()}" />
</Project>''', encoding="utf-8")
		def run(*arguments):
			return subprocess.run([dotnet, "msbuild", str(project), "-nologo", "-verbosity:quiet",
								   "-target:ValidateVeroScriptMobileApplication", *arguments],
								  capture_output=True, text=True, encoding="utf-8", errors="replace")
		ios = run("-getItem:NativeReference,MauiAsset,AndroidNativeLibrary,ReferenceNativeSymbol")
		self.assertEqual(ios.returncode, 0, ios.stdout + ios.stderr)
		items = json.loads(ios.stdout)["Items"]
		self.assertEqual(len(items["NativeReference"]), 1)
		self.assertEqual(items["NativeReference"][0]["Kind"], "Static")
		self.assertEqual(items["NativeReference"][0]["ForceLoad"], "true")
		self.assertEqual(items["NativeReference"][0]["SmartLink"], "false")
		self.assertFalse(items["AndroidNativeLibrary"])
		symbols = ET.fromstring(sdk.static_symbols()).find("ItemGroup")
		self.assertEqual({item["Identity"] for item in items["ReferenceNativeSymbol"]},
						 {item.get("Include") for item in symbols})
		self.assertTrue(all(item["SymbolType"] == "Function" for item in items["ReferenceNativeSymbol"]))
		self.assertIn("veroscript/stdlib/core.vs", {item["LogicalName"].replace("\\", "/") for item in items["MauiAsset"]})
		self.assertIn("veroscript/licenses/NOTICE.toolchain", {item["LogicalName"] for item in items["MauiAsset"]})
		android = run("-p:TargetFramework=net10.0-android", "-p:RuntimeIdentifier=android-arm64",
					  "-p:SupportedOSPlatformVersion=26.0", "-getItem:NativeReference,AndroidNativeLibrary,ReferenceNativeSymbol")
		self.assertEqual(android.returncode, 0, android.stdout + android.stderr)
		items = json.loads(android.stdout)["Items"]
		self.assertFalse(items["NativeReference"])
		self.assertFalse(items["ReferenceNativeSymbol"])
		self.assertEqual(len(items["AndroidNativeLibrary"]), 4)
		self.assertEqual({item["Abi"] for item in items["AndroidNativeLibrary"]}, {"arm64-v8a", "x86_64"})
		# The Android SDK normalizes application OutputType to Library before
		# NuGet buildTransitive imports are evaluated.
		normalized = run("-p:TargetFramework=net10.0-android", "-p:RuntimeIdentifier=android-x64",
			"-p:SupportedOSPlatformVersion=26.0", "-p:OutputType=Library", "-p:AndroidApplication=true",
			"-getItem:AndroidNativeLibrary,MauiAsset")
		self.assertEqual(normalized.returncode, 0, normalized.stdout + normalized.stderr)
		items = json.loads(normalized.stdout)["Items"]
		self.assertEqual(len(items["AndroidNativeLibrary"]), 4)
		self.assertIn("veroscript/stdlib/core.vs", {item["LogicalName"].replace("\\", "/") for item in items["MauiAsset"]})
		invalid = run("-p:TargetFramework=net10.0-android", "-p:OutputType=Library", "-p:AndroidApplication=true",
			"-p:RuntimeIdentifier=android-x64", "-p:SupportedOSPlatformVersion=25.0")
		self.assertNotEqual(invalid.returncode, 0, invalid.stdout + invalid.stderr)
		library = run("-p:TargetFramework=net10.0-android", "-p:OutputType=Library", "-p:AndroidApplication=false",
			"-p:VeroScriptMobileNativeRoot=missing", "-getItem:AndroidNativeLibrary,MauiAsset")
		self.assertEqual(library.returncode, 0, library.stdout + library.stderr)
		self.assertFalse(any(json.loads(library.stdout)["Items"].values()))
		for parameters in (("-p:RuntimeIdentifier=osx-arm64",), ("-p:SupportedOSPlatformVersion=14.0",),
						   ("-p:RuntimeIdentifiers=ios-x64",),
						   ("-p:VeroScriptMobileNativeRoot=missing",)):
			rejected = run(*parameters)
			self.assertNotEqual(rejected.returncode, 0, rejected.stdout + rejected.stderr)
		# Evaluate real packaging items without invoking any mobile SDK workload.
		packaged = subprocess.run([dotnet, "msbuild", str(MODULE / "VeroScriptCSharp.csproj"),
								   "-nologo", "-verbosity:quiet", "-p:TargetFrameworks=net10.0",
								   "-p:TargetFramework=net10.0", "-p:VeroScriptIncludeMobile=true", "-p:VeroScriptMobileNativeRoot=" + str(self.output),
								   "-getItem:None"], capture_output=True, text=True, encoding="utf-8", errors="replace")
		self.assertEqual(packaged.returncode, 0, packaged.stdout + packaged.stderr)
		items = json.loads(packaged.stdout)["Items"]["None"]
		native = {Path(item["Identity"]).resolve() for item in items
				  if item.get("Pack") == "true" and item.get("PackagePath", "").startswith("native/")}
		self.assertEqual(native, {(self.output / route).resolve() for route in sdk.tree(self.output)})
		documentation = {item["PackagePath"].replace("\\", "/") for item in items
						 if item.get("Pack") == "true" and item.get("PackagePath", "").startswith("Resources/Docs/")}
		self.assertFalse(documentation)
		shipped = {Path(item["Identity"]).name: item for item in items if item.get("Pack") == "true"}
		self.assertTrue({"README.md", "API.md", "MOBILE.md"}.isdisjoint(shipped))
		self.assertEqual(shipped["VeroScript.CSharp.targets"]["PackagePath"], "buildTransitive/")
		rejected = subprocess.run([dotnet, "msbuild", str(MODULE / "VeroScriptCSharp.csproj"),
								   "-nologo", "-verbosity:quiet", "-p:TargetFrameworks=net10.0",
								   "-p:TargetFramework=net10.0", "-p:VeroScriptIncludeMobile=true", "-target:ValidateVeroScriptMobilePackage"],
								  capture_output=True, text=True, encoding="utf-8", errors="replace")
		self.assertNotEqual(rejected.returncode, 0, rejected.stdout + rejected.stderr)
		self.assertIn("desktop net8.0/net9.0 and Android/iOS", rejected.stdout)
		# Exercise the producer's actual pack guard with platform TFMs, independently
		# of workload imports. The SDK audit remains real; native files are fixtures.
		source = ET.parse(MODULE / "VeroScriptCSharp.csproj").getroot()
		guard = next(target for target in source.findall("Target")
					 if target.get("Name") == "ValidateVeroScriptMobilePackage")
		for command in guard.findall("Exec"):
			command.set("Command", command.get("Command").replace(
				"$(MSBuildThisFileDirectory)", MODULE.as_posix() + "/"))
		container = ET.Element("Project")
		properties = ET.SubElement(container, "PropertyGroup")
		for name, value in {"TargetFrameworks": "net8.0;net9.0;net10.0-android;net10.0-ios",
							"VeroScriptIncludeMobile": "true",
							"VeroScriptMobileNativeRoot": str(self.output),
							"VeroScriptMobilePython": sys.executable}.items():
			ET.SubElement(properties, name).text = value
		container.append(guard)
		package_guard = self.root / "package-guard.proj"
		ET.ElementTree(container).write(package_guard, encoding="utf-8")
		accepted = subprocess.run([dotnet, "msbuild", str(package_guard), "-nologo", "-verbosity:quiet",
								   "-target:ValidateVeroScriptMobilePackage"], capture_output=True,
								  text=True, encoding="utf-8", errors="replace")
		self.assertEqual(accepted.returncode, 0, accepted.stdout + accepted.stderr)
		rejected = subprocess.run([dotnet, "msbuild", str(package_guard), "-nologo", "-verbosity:quiet",
								   "-target:ValidateVeroScriptMobilePackage", "-p:TargetFrameworks=net10.0-android"],
								  capture_output=True, text=True, encoding="utf-8", errors="replace")
		self.assertNotEqual(rejected.returncode, 0, rejected.stdout + rejected.stderr)
		self.assertIn("Pack desktop net8.0/net9.0 and both Android and iOS", rejected.stdout)
		rejected = subprocess.run([dotnet, "msbuild", str(package_guard), "-nologo", "-verbosity:quiet",
								   "-target:ValidateVeroScriptMobilePackage", "-p:VeroScriptIncludeMobile=false"],
								  capture_output=True, text=True, encoding="utf-8", errors="replace")
		self.assertNotEqual(rejected.returncode, 0, rejected.stdout + rejected.stderr)
		self.assertIn("Set VeroScriptIncludeMobile=true", rejected.stdout)
		rejected = subprocess.run([dotnet, "msbuild", str(package_guard), "-nologo", "-verbosity:quiet",
								   "-target:ValidateVeroScriptMobilePackage", "-p:VeroScriptIncludeMobile=false",
								   "-p:TargetFrameworks=", "-p:TargetFramework=net10.0-ios"],
								  capture_output=True, text=True, encoding="utf-8", errors="replace")
		self.assertNotEqual(rejected.returncode, 0, rejected.stdout + rejected.stderr)
		self.assertIn("Set VeroScriptIncludeMobile=true", rejected.stdout)

	def test_ios_rejects_resolved_desktop_bindings_before_compilation(self):
		dotnet = os.environ.get("VEROSCRIPT_CSHARP_MOBILE_DOTNET") or shutil.which("dotnet")
		if dotnet is None:
			self.skipTest("dotnet MSBuild is unavailable")
		sdk.prepare(self.android, self.ios, self.output)
		project = self.root / "resolved-consumer.proj"
		# ReferencePath metadata follows ResolveReferences from the real SDK:
		# NuGet uses PathInPackage; project references use NearestTargetFramework.
		project.write_text(f'''<Project>
  <PropertyGroup>
    <TargetFramework>net10.0-ios</TargetFramework><OutputType>Exe</OutputType>
    <SupportedOSPlatformVersion>15.0</SupportedOSPlatformVersion>
    <VeroScriptMobileNativeRoot>{self.output.as_posix()}</VeroScriptMobileNativeRoot>
    <BindingFramework>net9.0</BindingFramework><ReferenceKind>Package</ReferenceKind>
  </PropertyGroup>
  <ItemGroup>
    <ReferencePath Include="custom-output/VeroScriptCSharp.dll">
      <NuGetPackageId Condition="'$(ReferenceKind)' == 'Package'">veroscript.csharp</NuGetPackageId>
      <PathInPackage Condition="'$(ReferenceKind)' == 'Package'">$(AssetDirectory)/$(BindingFramework)/VeroScriptCSharp.dll</PathInPackage>
      <ReferenceSourceTarget Condition="'$(ReferenceKind)' == 'Project'">ProjectReference</ReferenceSourceTarget>
      <NearestTargetFramework Condition="'$(ReferenceKind)' == 'Project'">$(BindingFramework)</NearestTargetFramework>
    </ReferencePath>
  </ItemGroup>
  <Import Project="{(MODULE / 'buildTransitive/VeroScript.CSharp.targets').as_posix()}" />
  <Target Name="ResolveReferences" />
  <Target Name="Review" DependsOnTargets="ValidateVeroScriptMobileApplication;ResolveReferences" />
</Project>''', encoding="utf-8")
		for kind in ("Package", "Project"):
			for framework in ("net8.0", "net9.0", "net10.0-android36.0", "net10.0-ios26.0"):
				for directory in ("lib", "ref"):
					with self.subTest(kind=kind, framework=framework, assets=directory):
						result = subprocess.run([dotnet, "msbuild", str(project), "-nologo", "-verbosity:quiet",
												 "-target:Review", "-p:ReferenceKind=" + kind,
												 "-p:BindingFramework=" + framework, "-p:AssetDirectory=" + directory],
												capture_output=True, text=True, encoding="utf-8", errors="replace")
						if "-ios" in framework:
							self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
						else:
							self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
							self.assertIn("iOS-specific VeroScriptCSharp assembly", result.stdout)
		unrelated = subprocess.run([dotnet, "msbuild", str(project), "-nologo", "-verbosity:quiet",
									"-target:Review", "-p:ReferenceKind=Unrelated"], capture_output=True,
								   text=True, encoding="utf-8", errors="replace")
		self.assertEqual(unrelated.returncode, 0, unrelated.stdout + unrelated.stderr)

	def test_unified_project_keeps_the_desktop_default_and_one_package_identity(self):
		dotnet = os.environ.get("VEROSCRIPT_CSHARP_MOBILE_DOTNET") or shutil.which("dotnet")
		if dotnet is None:
			self.skipTest("dotnet MSBuild is unavailable")
		for enabled, expected in (("false", "net8.0;net9.0"),
								  ("true", "net8.0;net9.0;net10.0-android;net10.0-ios")):
			with self.subTest(mobile=enabled):
				result = subprocess.run([dotnet, "msbuild", str(MODULE / "VeroScriptCSharp.csproj"),
										 "-nologo", "-verbosity:quiet", "-p:VeroScriptIncludeMobile=" + enabled,
										 "-getProperty:TargetFrameworks,PackageId"], capture_output=True,
										text=True, encoding="utf-8", errors="replace")
				self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
				properties = json.loads(result.stdout)["Properties"]
				self.assertEqual(properties["TargetFrameworks"], expected)
				self.assertEqual(properties["PackageId"], "VeroScript.CSharp")


if __name__ == "__main__":
	unittest.main()
