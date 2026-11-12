# VeroScript WebAssembly 技術文件

也保留單行正確寫法：`void main(string[] args)` 與 `int main(string[] args)`。
本目錄記錄 UI-free WebAssembly SDK。它與 `VeroScriptWebAssembly.h`、`VeroScriptWebAssemblyC.h`、TypeScript binding、Emscripten build、browser test 繫結；不記錄 editor、HTML application、CSS framework 或 frontend product。

## 公開介面

WebAssembly module 有三種 surface：implementation-facing C++ wrapper `VeroScript::WebAssembly::FRuntime`、提供 opaque runtime handle/result/virtual-file/language/catalog/run 的 WebAssembly C ABI，以及 browser-facing generated TypeScript module/declaration。TypeScript SDK 必須與 C/C++ contract 保持 parity。

runtime 依賴 language core、C++ host API、shared standard-library/localization asset，不得引入第二套 language 或 bytecode version contract。

## 執行策略

請參閱[WebAssembly ABI、產物與發行契約 — 程式與輸入能力](release-contract.md#web-execution-policy)。

published baseline 不 advertise script-level worker thread。unbounded script 透過 terminate owning Web Worker 停止。interactive input 在 provider wait 時 suspend runtime；每個 Wasm module 只有一個 active interactive run。

## artifact 與 binding rule

standard library/localization catalog embed 在 generated virtual filesystem。release artifact set、generated declaration、binding parity check 是一個 contract。修改 `VeroScriptWebAssemblyC.h`、TypeScript binding、result field、backend name、language tag 或 generated file name 時，必須同時更新 parity/browser test。

language selection 使用從 embedded catalog discovered 的 canonical BCP 47 tag。不得 hard-code language count。catalog key、API identifier、backend name、diagnostic、version value 不 translate。

## runtime API 細節

`createVeroScriptRuntime()` 建立 interactive input 的 Asyncify runtime；`createFastVeroScriptRuntime()` 是 upfront input 的 low-latency factory。 `runSource` 在 creation 後 synchronous；`runSourceInteractive` 返回 promise 並在 provider wait 時 suspend VM/script state。只有一個 interactive run 可 active。

runSource 接收 file label、source overlay、standard input、script argument、backend。支援 `void main(string[]
args)` 與 `int main(string[]
args)`；non-zero result 是 `exitCode` success。embedded NUL 被 reject。Spawn 不屬於 browser baseline，worker attempt 返回 structured diagnostic 而不 hang Wasm。

artifact advertise `supportsCompiledPrograms` 時，`compileSource` 一次 compile source graph 並返回 owned `VeroScriptProgram`。repeated `run` reuse program 且使用 fresh state；`runInteractive` 僅限 Asyncify artifact。program 在 runtime 前 dispose；runtime disposal release remaining handle。compiled-program 是 artifact capability，不是 language/bytecode version。

## 詳細頁面

- [Web 執行時與繫結](runtime-and-bindings.md)
- [WebAssembly ABI、生成物與 release 契約](release-contract.md)
- [Web 構建與生成物契約](build-and-artifacts.md)

## C ABI 執行階段與程式控制代碼

C runtime handle 同一時間由一個 caller control。result-producing call 返回的 pointer 在 next replacement operation 或 runtime destruction 前由 runtime own。Asyncify input wait 期間，stateful getter/text operation unavailable；host 在 interactive call 完成後 retry。

vero_web_supports_threads 報告 artifact 是否有 pthread-backed script concurrency。default browser artifact 改為對 concurrent program 使用 independent Web Worker。該 capability 獨立於 supportsInteractiveInput/supportsCompiledPrograms SDK flag。

context entry point 接收 borrowed virtual-file path/source pair。compile 建立 runtime-owned program handle；run_program/run_program_interactive reuse compiled module 與 fresh state。若要求 deterministic cleanup，必須在 discard owning runtime 前呼叫 vero_web_destroy_program。

## 事實來源

以 public WebAssembly header、`Bindings/`、native WebAssembly implementation、Emscripten build script、artifact audit、declaration parity、smoke/browser test 為準。WebAssembly ABI、generated artifact、execution policy、input-provider、cancellation、asset-packaging 變更時同步更新本文件。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
