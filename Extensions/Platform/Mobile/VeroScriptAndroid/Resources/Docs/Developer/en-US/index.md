# Android — Developer documentation

Android owns the Java/JNI and AAR/Prefab adapter. Keep UI dispatch and application lifecycle in the adapter/host. JNI uses standard UTF-8 byte arrays and explicit catalog loading/language selection; translated exceptions must not use JNI modified UTF-8. Desktop JNI smoke uses a real JVM and a test AssetManager and does not establish Android device acceptance.

Shared embedded build, lifetime, package-output and localization responsibilities are maintained in [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/en-US/index.md).

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --out-root dist/mobile/android
```

Android builds require JDK 17, Gradle 8.11.1, AGP 8.9.2, SDK 35, NDK 28.2.13676358 and CMake 3.31.6. Verify both ABI JNI/Interop/libc++ files, Android/API identity and 16 KB ELF LOAD alignment. Device/emulator, R8 and final APK ZIP alignment checks remain required.

[User documentation](../../User/en-US/index.md)


## Mobile resources and localization

The shared mobile catalog inventory, deployment paths and explicit loading procedure are maintained in [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/en-US/index.md#mobile-localization).

Translation buffer ownership, errors and export maintenance are specified in [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/en-US/abi-and-lifetime.md#localization-abi).

[Android AAR target validation](device-validation.md)

The base Android AAR explicitly builds Application support (`VEROSCRIPT_BUILD_APPLICATION=ON`), including in embedded-only builds. Retain `assets/veroscript/licenses/MINIZ-LICENSE.txt`, `JSON-LICENSE.txt` and `SHA256-LICENSE.txt` alongside LICENSE and the NDK notices. The mobile verifier requires them; enabling application containers does not enable the package-installation client or desktop tooling.

<a id="jni-host-fixture"></a>

For the optional desktop JNI fixture, configure `VEROSCRIPT_TEST_ANDROID_JNI_HOST=ON`, `BUILD_TESTING=ON` and a real JDK through JAVA_HOME. Build VeroScriptAndroidHostSmoke and run the VeroScriptAndroidJniHostSmoke CTest entry. It uses the real JNI adapter and a test AssetManager to check UTF-8/NUL, imports, exceptions, cancellation and release under `-Xcheck:jni`; it does not establish an Android ABI/API, APK or device result. Target acceptance is maintained in [the device guide](device-validation.md).

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
