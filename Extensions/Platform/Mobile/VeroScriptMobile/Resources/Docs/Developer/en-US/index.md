# Mobile — Developer documentation

Keep UI dispatch and application lifecycle in the platform adapter/host; Core remains platform-neutral. Embedded-only builds exclude desktop CLI/tooling and language exporters. Static Interop belongs to the source dependency graph or merged iOS archive; the desktop Interop package remains shared. Platform identity must distinguish Android/Linux, iOS/macOS and device/simulator. Platform allowance is not proof of a built/selected backend or device acceptance. Test resource imports, cache invalidation/corruption, input/output caps, cancellation and destruction.

```text
cmake -S . -B build/embedded -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF -DBUILD_TESTING=ON
cmake --build build/embedded --config Release --target VeroScriptMobileTests
```

`package-veroscript-mobile.py` produces separate experimental output and refuses an existing destination; it does not publish to the desktop release collection.

Toolchain versions and platform-specific acceptance are maintained in [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/en-US/index.md) and [iOS](../../../../../VeroScriptIOS/Resources/Docs/Developer/en-US/index.md).

[User documentation](../../User/en-US/index.md)


## Mobile resources and localization

<a id="mobile-localization"></a>

The `mobile` localization profile assembles Core, C++ Session, Interop and `Extensions/Platform/Mobile/VeroScriptMobile/Resources/Localization`; it excludes desktop Tooling/UI catalogs. Android Gradle stages identical catalogs into AAR assets and the packager verifies exact bytes and inventory. iOS copies them to `assets/localization`. Applications unpack/load explicitly before selecting a language; deployment must not introduce environment/resource discovery on mobile. Mobile packages contain functional files, runtime resources and legal notices. They omit README, manuals and examples; obtain the integrated User documentation from the separate Resources distribution. Native resource paths and device acceptance requirements are unchanged.

Mobile `TranslateMessage` delegates through the public C ABI. Translation buffer ownership and error behavior are specified in [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/en-US/abi-and-lifetime.md); Android JNI encoding is maintained in [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/en-US/index.md).

Core and target-AOT implementation fingerprints include `Shared/Private/VeroScriptFloatParsing.h`, `VeroScriptFloatFormatting.h` and `VeroScriptJsonString.h`. Python source distributions carry all required `Shared/Private` inputs, including `VeroScriptDarwinPaths.h` for Application/package runtime. The Android Python source digest also covers Application and its C sources. These are private build inputs, not public SDK headers or device-acceptance evidence.

<a id="mobile-execution-worker"></a>

`Private/VeroScriptMobile.cpp::WithCompilerStack` checks the caller stack on Android/Apple. Below 8 MiB it creates an 8 MiB pthread and joins it synchronously; otherwise execution stays on the caller. The worker owns creation, execution and destruction of the entire FSession, not just parsing. It captures exceptions and rethrows after join. Preserve the active-session pointer cleanup under the cancellation mutex before session destruction and the outer executing-state cleanup on every exit. Direct FSession calls do not use this helper; the host supplies their stack. The budget does not raise language nesting, call-depth or execution limits. Test small-stack valid input, parser-limit diagnostics, exception propagation, cancellation and reuse. The Android fixture includes bounded-nesting recovery; Apple regressions explicitly fix the caller stack at 512 KiB.

<a id="mobile-message-coverage"></a>

Keep the Mobile catalog complete for C++ session/runner errors, Android AOT/Java validation and iOS factory/backend errors. Native C++ and Java validation use the existing translation entry points; JNI/NSError translate adapter exceptions at their boundary. For std::system_error, translate the project context without replacing its error code/category; the OS suffix follows the system library’s language. Resource failures retain the existing fallback. Catalog keys and placeholder sets must match across all six languages.

Pure Core/C++/C source hosts can disable adapters with `VEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF`. `VEROSCRIPT_INTEROP_STATIC=ON` is a source/static-mobile build option, not a change to the shared desktop Interop distribution.

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
