# CSharp — Mobile SDK

This page is for maintainers who build and publish the C# mobile SDK. Use the same `VeroScriptCSharp.csproj` and `VeroScript.CSharp` package identity for desktop and mobile. Default targets are .NET 8/9; `VeroScriptIncludeMobile=true` adds both .NET 10 Android/iOS targets. Android requires API 26 with arm64-v8a/x86_64; iOS requires 15.0 with arm64 device and arm64/x86_64 simulator. Mobile-enabled packing requires all four managed targets, matching workloads, and a complete verified native root.

`prepare-native.py` consumes an Android AAR and the static `VeroScriptIOS.xcframework` SDK from this source revision. These inputs contain Core/C++/Interop. The C# application does not require a CPython SDK or the Python mobile producer. It does not use the Java JNI wrapper or a native JIT module. Stage into a new directory, validate it, then pack.

## Native SDK preparation

```sh
gradle --project-dir Extensions/Platform/Mobile/VeroScriptAndroid --no-daemon assembleRelease
python3 Resources/Tools/Packaging/package-veroscript-mobile.py ios \
  --cmake /path/to/cmake --out-root dist/mobile/ios
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py prepare \
  --android-aar Extensions/Platform/Mobile/VeroScriptAndroid/build/outputs/aar/VeroScriptAndroid-release.aar \
  --ios-sdk dist/mobile/ios --output dist/csharp-mobile/native
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py verify \
  dist/csharp-mobile/native
dotnet pack Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj \
  -c Release -p:VeroScriptIncludeMobile=true \
  -p:VeroScriptMobileNativeRoot=/absolute/path/to/native \
  -o /absolute/path/to/mobile-nuget
```

Use the actual AAR output name if Gradle chooses a different filename. Android requires its configured SDK/NDK/Gradle toolchain; iOS requires macOS and full Xcode. Mobile managed targets require the .NET 10 Android/iOS workloads. `VeroScriptMobilePython` selects the preparation interpreter if it is not named `python3`. Preparation does not download SDKs or sign applications.

## Integration contracts

Preparation verifies the original headers, standard library, six-language catalogs, redistribution notices and SHA-256 inventory. It checks Android identity/API and 16 KB ELF LOAD alignment, retaining `libVeroScriptInterop.so` and matching `libc++_shared.so` for both ABIs. It audits static iOS archive objects for device/simulator architecture and minimum OS. The iOS SDK requires Foundation/libc++. Existing output directories are rejected.

NuGet imports `buildTransitive/VeroScript.CSharp.targets` automatically. Source `ProjectReference` consumers must import that targets file and provide `VeroScriptMobileNativeRoot`. Preserve Android native libraries/assets and iOS `__Internal`, `NativeReference`, `ForceLoad` and generated `ReferenceNativeSymbol` retention. After reference resolution, verify the actual iOS binding assembly, including transitive references; reject desktop/Android binding assemblies. The standard library and catalogs are app resources; the bytecode cache uses a writable app-owned directory.

NuGet preserves the prepared root’s relative layout under `native/`, including `android/<abi>`, `ios`, `assets` and `licenses`; do not duplicate those path components when packing. The MSBuild targets recognize an application through `OutputType=Exe`, or on Android through `AndroidApplication=true` after SDK inference. Keep native-library and MAUI asset selection active for Android applications whose inferred output type is `Library`, while excluding ordinary class-library consumers.

Resource installation checks routes and digests, requires the exact case-sensitive `stdlib/core.vs`, disposes streams and repairs stale files on retry. Installation can leave verified files after failure; retry before running. Sessions must be created, configured, used and disposed on one worker thread after asynchronous resource installation has completed. Select VM explicitly. The package does not provide a native JIT.

[Application integration](../../User/en-US/mobile.md)

## Release verification

Portable C# tests check dynamic and `IOS` static import profiles against the native export manifest. They exercise resource corruption, traversal, cancellation, stream disposal, stale-file repair and actual standard-library/catalog installation. Python fixtures test SDK preparation, MSBuild asset selection, incompatible RIDs/OS levels, incomplete target matrices and resolved iOS assembly rejection. These are host tests.

Before a stable mobile release, build and run the MAUI example in Release on Android emulator/device and iOS simulator/device. Include Android 16 KB page systems, dependency and APK ZIP alignment, iOS signing/linking, AOT/trimming, cancellation, callbacks, resource imports and cold startup. Record toolchain versions, package inspection and actual outcomes. Mobile support remains experimental until these target-platform gates are completed.
## Publication documents

Mobile packages contain functional files, runtime resources and legal notices. They omit README, manuals and examples; obtain the integrated User documentation from the separate Resources distribution. Native resource paths and device acceptance requirements are unchanged.

## Shared desktop and mobile NuGet feed

The command above prepares the mobile-enabled `VeroScript.CSharp` package. When publishing the CSharp business in a desktop or mobile collection, prepare one canonical feed containing all six registered C# package IDs and the matching `.nupkg` and `.snupkg` files. Select the feed with `VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY`. To reuse that package with `package-veroscript-mobile-release.py`, pass the same mobile-enabled `VeroScript.CSharp` file through `--csharp-package`. Reuse those bytes across targets. The complete producer inputs and example are maintained in the [mobile publication procedure](../../../../../../../Resources/Tools/Packaging/README.md#mobile-sdk-tooling).

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
