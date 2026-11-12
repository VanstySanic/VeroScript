# Network: Source ownership

Network owns opaque contexts, operations, quotas and shared stream/file contracts; Http/WebSocket own protocol facades and bindings. Boost owns Asio/Beast, OpenSSL integration and BoostRuntime; Curl owns the HTTP client and curl sources. Preserve service/foundation identity, the creating image’s lifetime anchors and returned facade control blocks across module unload.

For a manual registry host, pass FBackendCatalog::Binding() to frontend registration only, register the complete provider set, then call Update. Provider registrations must not retain that catalog. The package manager performs catalog composition and project service selection. Rebuild all participating images after contract/foundation or matched Core identity changes.

`Public` · `Internal` (versioned provider hooks) · `Private` · `Resources/Docs` · `Resources/Examples` · `Resources/Localization` · `Resources/Tests` · `Resources/Tools/CMake`

`VeroScriptNetwork/Internal` → `VeroScript::NetworkProvider` (opaque context/operation, quota, stream and upgrade hooks).

`Modules/Network/Backend/VeroScriptBoost/Private/Runtime` → `VeroScriptBoostRuntime`; `Modules/Network/Backend/VeroScriptCurl/Private` → HTTP client. `VeroScriptNetwork/Resources/Tools/CMake` owns the shared frontend/provider build helpers. Network, Http and WebSocket each own their package configuration template under their own `Resources/Tools/CMake`; category roots only coordinate CMake targets.

The SDK installs Public headers and explicitly versioned opaque hooks under include/VeroScript*/Backend. Concrete operation/context layouts stay private. Never include another module’s Private directory. Rebuild providers and bindings against the matching Core before publication; qualification follows actual platform results.

## Build and native module registration

The root build defaults the Network/Http/WebSocket frontends to OFF through `VEROSCRIPT_BUILD_NETWORK`, `VEROSCRIPT_BUILD_HTTP` and `VEROSCRIPT_BUILD_WEBSOCKET`. Boost defaults to ON when a network option is selected; Curl follows the HTTP selection. Select providers with `VEROSCRIPT_BUILD_BOOST` and `VEROSCRIPT_BUILD_CURL`. Standalone `Modules/Network` has different defaults: Boost, HTTP and WebSocket are enabled. `VEROSCRIPT_NETWORK_BUILD_BINDINGS` defaults to ON only when the Core target already exists; otherwise enable it explicitly with an exact matching installed Core SDK. Curl owns `VEROSCRIPT_CURL_USE_SYSTEM_LIBRARY` (OFF by default), requires curl 8.13 or newer for that mode, and builds its bundled static library with PIC. The bundled Linux configuration needs OpenSSL development files.

Register Network before the protocol modules and declare visible dependencies. Static hosts use each module’s `QueryModule` / `GetImplementationIdentity`; dynamic loaders use `veroscript_extension_query`. Static `ContentIdentity` identifies the implementation. Do not link a second Core into a provider. BoostRuntime and the frontend foundation must match the participating images; private backend symbols and build settings stay out of the frontend SDK.

<a id="network-execution-assembly"></a>

Core exposes the execution contract; the official bytecode VM is a separate backend. Hosts selecting bytecode execution must supply a matching factory through `LoadVmBackend` / `SetVmBackendFactory`, including `FCompileOptions::ScriptCompileExtensions.VmBackendFactory` when compilation executes script extensions. Standalone binding tests require `-DVEROSCRIPT_TEST_VM_LIBRARY=<absolute VM library>`; a full Core SDK includes the platform library under `bin/Vm`. `BUILD_TESTING=OFF` builds bindings without test execution; data-only builds require no VM.

Verify backend-free frontend SDKs, enabled/disabled capabilities, missing/incompatible services, explicit selection, dynamic/static leases, retained results, relocated SDKs and package dependency pins separately. Run native protocol/device/platform gates when execution is authorized; static documentation checks do not establish runtime acceptance.

<a id="cross-image-results"></a>

Operation results are unboxed in the producer image by `CreateTypedResult`/`OwnResult`, which retains `FOwnedResult`, its context and payload ownership. `FOperation::Result<T>()` matches the producer type name, size and alignment; it does not depend on cross-image `std::any_cast` RTTI address identity. This still requires matching C++ ABI/foundation builds and is not a portable C ABI. Rebuild all consumers/providers after header or result-storage changes. Preserve callback extraction by swap so a terminal transition releases each callback once.

POSIX atomic no-replace output prefers `renameat2(RENAME_NOREPLACE)` when available and falls back to hard linking only for unsupported syscall/filesystem cases; preserve explicit failure and temporary cleanup.

<a id="provider-facade-ownership"></a>

Provider facade wrapping receives the owning opaque context and preserves its creator-image lifetime anchor. The HTTP frontend supplies the typed exchange factory so `shared_from_this()` and HTTP upgrade ownership use the same control block. A provider-allocated control block that merely stores a context member is insufficient for safe unloading. Returned objects and retained results must use the shared factories/ownership helpers. The backend catalog caches factories weakly so environment ownership does not form a reference cycle; do not retain that catalog in provider registrations.

## Localization staging

The source helper `Resources/Tools/CMake/VeroScriptLocalization.cmake` selects `network`, `http`, `websocket`, `network-all` and the independent `json` profile. The HTTP/WebSocket profiles include Network; `network-all` combines all three network modules. These are build/deployment inputs, not runtime SDK API. Staging replaces the destination profile and removes obsolete entries; select the complete resource-root set before staging instead of overlaying profiles successively. Consumer resource mounting is documented in the User diagnostic reference.

<!-- docs-navigation -->

---

[User](../../User/en-US/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
