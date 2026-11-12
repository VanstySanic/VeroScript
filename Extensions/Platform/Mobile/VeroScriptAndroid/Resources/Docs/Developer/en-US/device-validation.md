# Android AAR target validation

## Prerequisites and build

This standalone fixture consumes the built base AAR through Java/JNI and C11/C++17 Prefab; it does not compile a second Core. Its default release APK enables R8 and uses a debug signing key only for the fixture. Use Python 3.11+ and the pinned JDK, Gradle, SDK, NDK and CMake versions in the [Android build guide](index.md). Build matching SDK artifacts first, using a new output directory. `--with-asmjit` also prepares the optional JIT AAR.

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --with-asmjit --out-root dist/mobile/android-acceptance --gradle <Gradle-8.11.1-executable>
```

## Run against an explicit target

Start an emulator or connect a debug-enabled device, then choose its exact serial from `adb devices`. Supply the matching base AAR; the example explicitly requests the x86_64 installation ABI.

```text
python Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device/run.py --aar <absolute-base.aar> --sdk <Android-Sdk> --gradle <Gradle-8.11.1-executable> --java-home <JDK-17> --serial <adb-serial> --abi x86_64 --out <report-directory>
```

Add `--jit-aar <absolute-jit.aar>` for the optional backend and governed native-execution counters. Use `--abi arm64-v8a` for the ARM64 slice. `--page-size 4096` or `--page-size 16384` checks the native Bionic `sysconf` page size; `--debug` selects the unminified variant. Cover API 26 and later versions and both page sizes. Use a separate output directory for each configuration. Enable CheckJNI separately on a test emulator when qualifying JNI.

## Results and acceptance scope

The runner uninstalls/replaces only the test application declared in [its build configuration](../../../../Resources/Tests/Device/build.gradle.kts) and its data, and leaves the selected device/emulator running. It preserves the APK, command logs, `commands.json` and `device-smoke.json`. It checks final APK 16 KB ZIP alignment, the returned case results and `INSTRUMENTATION_CODE: -1`; an adb process exit code alone is insufficient. Each attempt first sets `acceptancePassed` to false. It becomes true only after all checks, including the requested ABI and page size, pass.

Cases cover asset imports/Unicode names, UTF-8/NUL output, float32 rounding, memory/execution/output budgets, cache reuse, capability rejection, recovery, explicit backend selection, cancellation, concurrent execution rejection, close, localization and JNI AOT catalog lifetime. The catalog case does not run generated AOT business code. An ARM64 library under emulator native translation does not qualify physical ARM64 hardware; retain actual device/emulator identity and artifact hashes with the report. This fixture does not replace Python, C#, graphics or final application acceptance.

The Python-only report-handling regression below mocks native commands. It validates runner logic, not an Android runtime. Fixture presence, a host-only check or a build alone is not target acceptance.

```text
python -m unittest discover -s Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device -p test_run.py
```

<!-- docs-navigation -->

---

[Android](index.md) · [VeroScript](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](device-validation.md) · [fr-FR](../fr-FR/device-validation.md) · [ja-JP](../ja-JP/device-validation.md) · [ko-KR](../ko-KR/device-validation.md) · [zh-CN](../zh-CN/device-validation.md) · [zh-TW](../zh-TW/device-validation.md)
