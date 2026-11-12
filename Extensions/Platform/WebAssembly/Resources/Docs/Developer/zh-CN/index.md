# VeroScript WebAssembly 技术文档

也保留单行准确写法：`void main(string[] args)` 与 `int main(string[] args)`。
本目录记录 UI-free WebAssembly SDK。它与 `VeroScriptWebAssembly.h`、`VeroScriptWebAssemblyC.h`、TypeScript binding、Emscripten build、browser test 绑定；不记录 editor、HTML application、CSS framework 或 frontend product。

## 公开接口

WebAssembly module 有三种 surface：implementation-facing C++ wrapper `VeroScript::WebAssembly::FRuntime`、提供 opaque runtime handle/result/virtual-file/language/catalog/run 的 WebAssembly C ABI，以及 browser-facing generated TypeScript module/declaration。TypeScript SDK 必须与 C/C++ contract 保持 parity。

runtime 依赖 language core、C++ host API、shared standard-library/localization asset，不得引入第二套 language 或 bytecode version contract。

## 执行策略

参阅[WebAssembly ABI、产物与发行契约 — 程序与输入能力](release-contract.md#web-execution-policy)。

published baseline 不 advertise script-level worker thread。unbounded script 通过 terminate owning Web Worker 停止。interactive input 在 provider wait 时 suspend runtime；每个 Wasm module 只有一个 active interactive run。

## artifact 与 binding rule

standard library/localization catalog embed 在 generated virtual filesystem。release artifact set、generated declaration、binding parity check 是一个 contract。修改 `VeroScriptWebAssemblyC.h`、TypeScript binding、result field、backend name、language tag 或 generated file name 时，必须同时更新 parity/browser test。

language selection 使用从 embedded catalog discovered 的 canonical BCP 47 tag。不得 hard-code language count。catalog key、API identifier、backend name、diagnostic、version value 不 translate。

## runtime API 细节

`createVeroScriptRuntime()` 创建 interactive input 的 Asyncify runtime；`createFastVeroScriptRuntime()` 是 upfront input 的 low-latency factory。 `runSource` 在 creation 后 synchronous；`runSourceInteractive` 返回 promise 并在 provider wait 时 suspend VM/script state。只有一个 interactive run 可 active。

runSource 接收 file label、source overlay、standard input、script argument、backend。支持 `void main(string[]
args)` 与 `int main(string[]
args)`；non-zero result 是 `exitCode` success。embedded NUL 被 reject。Spawn 不属于 browser baseline，worker attempt 返回 structured diagnostic 而不 hang Wasm。

artifact advertise `supportsCompiledPrograms` 时，`compileSource` 一次 compile source graph 并返回 owned `VeroScriptProgram`。repeated `run` reuse program 且使用 fresh state；`runInteractive` 仅限 Asyncify artifact。program 在 runtime 前 dispose；runtime disposal release remaining handle。compiled-program 是 artifact capability，不是 language/bytecode version。

## 详细页面

- [Web 运行时与绑定](runtime-and-bindings.md)
- [WebAssembly ABI、生成物与 release 契约](release-contract.md)
- [Web 构建与生成物契约](build-and-artifacts.md)

## C ABI 运行时与程序句柄

C runtime handle 同一时间由一个 caller control。result-producing call 返回的 pointer 在 next replacement operation 或 runtime destruction 前由 runtime own。Asyncify input wait 期间，stateful getter/text operation unavailable；host 在 interactive call 完成后 retry。

vero_web_supports_threads 报告 artifact 是否有 pthread-backed script concurrency。default browser artifact 改为对 concurrent program 使用 independent Web Worker。该 capability 独立于 supportsInteractiveInput/supportsCompiledPrograms SDK flag。

context entry point 接收 borrowed virtual-file path/source pair。compile 创建 runtime-owned program handle；run_program/run_program_interactive reuse compiled module 与 fresh state。若要求 deterministic cleanup，必须在 discard owning runtime 前调用 vero_web_destroy_program。

## 事实来源

以 public WebAssembly header、`Bindings/`、native WebAssembly implementation、Emscripten build script、artifact audit、declaration parity、smoke/browser test 为准。WebAssembly ABI、generated artifact、execution policy、input-provider、cancellation、asset-packaging 变更时同步更新本文档。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
