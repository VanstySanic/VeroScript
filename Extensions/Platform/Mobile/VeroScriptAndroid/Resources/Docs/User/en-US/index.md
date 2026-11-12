# Android — Experimental mobile embedding

The experimental AAR targets `arm64-v8a` and `x86_64`, API 26+, with VM and NDK `c++_shared`. Add the supplied AAR to the app and keep compatible native dependencies. It includes `assets/veroscript/stdlib`, C ABI Prefab headers and license assets; put application scripts under `src/main/assets/scripts`. `scripts/helper.vs` maps to `/veroscript/scripts/helper.vs`. Create/run/close on a worker; keep the engine reference for `requestCancel()`. `executeAsset` reads the entry, or use `execute(source, absoluteLabel)`. Each execution has fresh state; concurrent execution fails. `close()` waits and is repeatable; do not call it synchronously from the engine’s own callback. UTF-8 byte arrays preserve NUL/supplementary characters; check `outputTruncated`.

Import the SDK’s [VeroScriptEngine](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Platform/Mobile/VeroScriptAndroid/src/main/java/com/vanstysanic/veroscript/android/VeroScriptEngine.java) type. The following method fragment runs on a worker and assumes an Android `context` is available.

```java
try (VeroScriptEngine engine = new VeroScriptEngine(context.getAssets(), "scripts"))
{
	VeroScriptEngine.Result result = engine.executeAsset("scripts/main.vs");
	// Inspect result.success, output, diagnostics and outputTruncated.
}
```

[Experimental mobile embedding](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/en-US/index.md)

## Explicit mobile localization

[Explicit mobile localization](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/en-US/index.md)

## Managed byte-storage limits

Android exposes these budgets as the final `maxBufferBytes` / `maxRetainedBytes` arguments of its full `VeroScriptEngine` constructor. Java `long` carries the uint64 bit pattern; use positive byte counts for ordinary budgets. The defaults are 16 MiB / 32 MiB; zero is unlimited.

## Controlled native execution and static AOT

See [Experimental mobile embedding — Controlled native execution and static AOT](../../../../../VeroScriptMobile/Resources/Docs/User/en-US/index.md#mobile-jit-selection).

<a id="android-native-selection"></a>

Android accepts `ExecutionBackend`, `jitBackendPath`, an application-created `AotPrograms` catalog and `aotProgramId`; use `executePackage` for generated applications. Native bootstrap uses `VeroScriptAndroid.h`. Catalogs retain their programs; generated entry code must remain statically linked, and IDs never contain function addresses.

<!-- docs-navigation -->

---

[User documentation](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
