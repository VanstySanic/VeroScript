# Experimental mobile embedding

The Android and iOS adapters remain experimental. Use SDK artifacts matching your target and validate your application in its actual deployment environment. `FSessionOptions` supplies an absolute standard-library directory and source-resource snapshot. Real or virtual paths are allowed when every virtual source is supplied through `Sources`; use the same absolute labels for entry and relative imports. An optional cache directory must be real, writable and absolute; empty disables it. Recreate the runner after updating resources.

```cpp
VeroScript::Mobile::FSessionOptions options;
options.StdlibDirectory = "/app/resources/stdlib";
VeroScript::Mobile::FRunner runner(std::move(options));
auto result = runner.Execute("int main(){ print(42); return 0; }", "/app/scripts/main.vs");
```

`FRunner` permits one execution at a time and creates a fresh session for each call, so globals do not persist. Execute calls are synchronous. On Android and Apple platforms, execution may use an internal worker; host callbacks must not assume the calling thread or UI thread. Construction and execution may read resources or compile: schedule them off the UI thread. Another thread may request cancellation while the object stays alive. Wait for execution/cancellation calls to finish before destruction. `FSession` creation, ordinary calls and destruction belong to its creator thread, for which the host must provide at least 8 MiB of native compiler stack; its cancellation remains latched. Callbacks must enforce their own permissions and cancellation. Cancellation neither preempts compilation nor forcibly ends a blocked native callback.

When caching is enabled, entry and imported source contents participate in invalidation. A hit reuses bytecode but can still load and parse resources to check their current identity; do not assume zero compilation-startup work. Unusable or corrupt caches fall back to the normal compilation path.

Defaults: VM, 1,000,000 instructions, 100 ms and call depth 256. Only console output is permitted; input, files, environment access, directory changes and tasks are disabled. Output and diagnostics share 1 MiB; errors retain at most 64 KiB. Source resources and each entry have a 16 MiB budget, with at most 4096 resources and 4096-byte paths. These are input/output limits, not a VM heap quota or complete sandbox. Mobile hosts explicitly load unpacked catalogs with `vs_interop_locale_load_catalog_directory` and select a language; there is no environment, cwd or executable-neighbor resource discovery. Without catalogs, diagnostics remain English. iOS devices and simulators prohibit dynamic JIT libraries and native emission.

## Explicit mobile localization

Mobile SDKs supply six-language catalogs but do not load them implicitly. Unpack/copy the whole catalog tree to a real app-owned directory and load it before creating engines or running scripts; do not pass an APK asset/virtual source path. Android AAR resources are under `veroscript/localization`; iOS SDK resources are under `assets/localization`. Android exposes `VeroScriptEngine.loadCatalogDirectory(path)` and `setLanguage(tag)`; both return boolean success. iOS/C++ use `vs_interop_locale_load_catalog_directory` and `vs_interop_locale_set_language`. Release error buffers with `vs_interop_buffer_destroy`. Check every return value. Selection is process-wide, not per engine, and failure preserves the previous selection. Without catalogs, unknown messages remain English. Compile diagnostics and adapter validation errors use the same selection; program output is unchanged. Allocation failures may retain their English fallback. Operating-system error details can follow the system library’s language even when the adapter context is translated.

## Managed byte-storage limits

The default is 16 MiB per buffer and 32 MiB retained bytes. Core mobile options expose `MaxBufferBytes` and `MaxRetainedBytes`; zero means unlimited. These quotas cover owned capacity and external byte storage, including growth peaks, not the whole VM heap. Native addresses are disabled by the default console-only policy.

## Controlled native execution and static AOT

<a id="mobile-jit-selection"></a>

VM remains the mobile default. Android can explicitly select an optional `vs-asmjit` AAR with a JIT file/directory. Keep exactly one matching `libc++_shared.so`; an app using a filesystem JIT path must extract native libraries (app `jniLibs.useLegacyPackaging = true`). iOS device and simulator prohibit dynamic JIT.

<a id="mobile-core-aot"></a>

`CompileSourceToCoreAot` / `GenerateCoreAotSourceBundle` in `VeroScriptCppCoreAot.h` produce target C++, exact `.vsapp` / `.vsbc` bytes and coverage JSON. Compile the output with the target SDK, select Mixed and execute the exact package. Unsupported functions, module and field initialization use VM; strict native mode rejects uncovered work. This profile keeps Core semantics and differs from standalone source export.

Platform API: [Android](../../../../../VeroScriptAndroid/Resources/Docs/User/en-US/index.md#android-native-selection) · [iOS](../../../../../VeroScriptIOS/Resources/Docs/User/en-US/index.md#ios-native-selection).

See [Optional JIT backend — Controlled native execution and static AOT](../../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/en-US/index.md#controlled-native-execution).

The complete mobile SDK package provides `aot/include` for compiling generated C++: public Core service headers and the matching target build identity. Add it alongside the Interop headers. This is a header bundle; keep one linked Interop runtime. An AAR-only installation needs this matching bundle separately.

VM selection is independent of `ExecutionBackend` and JIT. Empty `FSessionOptions.VmBackendPath` keeps the explicitly assembled static VM; a nonempty value must name an absolute native library file/directory, and failure never falls back. The full Android constructor accepts `vmBackendPath`; iOS exposes `VSMConfiguration.vmBackendPath` and retains static assembly by default.

<!-- docs-navigation -->

---

[User documentation](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
