# VeroScript WebAssembly 技術文書

単一行の表記も正確に保持します：`void main(string[] args)` と `int main(string[] args)`。
この directory は UI-free WebAssembly SDK を文書化します。`VeroScriptWebAssembly.h`、`VeroScriptWebAssemblyC.h`、TypeScript binding、Emscripten build、browser test に bound され、editor、HTML application、CSS framework、frontend product は対象外です。

## 公開インターフェイス

WebAssembly module は implementation-facing C++ wrapper `VeroScript::WebAssembly::FRuntime`、opaque runtime handle/result/virtual-file/language/catalog/run の WebAssembly C ABI、browser-facing generated TypeScript module/declaration の三 surface を持ちます。TypeScript SDK は C/C++ contract と parity を保ちます。

runtime は language core、C++ host API、shared standard-library/localization asset に依存し、第二の language/bytecode version contract を導入しません。

## 実行ポリシー

[WebAssembly ABI、artifact、release 契約 — プログラムと入力の機能](release-contract.md#web-execution-policy) を参照してください。

published baseline は script-level worker thread を advertise しません。unbounded script は owning Web Worker を terminate して停止します。interactive input は provider wait 中 runtime を suspend し、Wasm module ごとに active interactive run は一つです。

## artifact と binding rule

standard library/localization catalog は generated virtual filesystem に embed されます。release artifact set、generated declaration、binding parity check は一契約です。`VeroScriptWebAssemblyC.h`、TypeScript binding、result field、backend name、language tag、generated file name の変更は parity/browser test も更新します。

language selection は embedded catalog から discovered canonical BCP 47 tag を使います。language count を hard-code しません。catalog key、API identifier、backend name、diagnostic、version value は translate しません。

## runtime API の詳細

`createVeroScriptRuntime()` は interactive input の Asyncify runtime、`createFastVeroScriptRuntime()` は upfront input 時の low-latency factory です。`runSource` は creation 後 synchronous、`runSourceInteractive` は promise を返し provider wait 中 VM/script state を suspend します。一 interactive run のみ active です。

runSource は file label、source overlay、standard input、script argument、backend を受けます。`void main(string[]
args)`/`int main(string[]
args)` は support され non-zero result は `exitCode` success です。embedded NUL は reject します。Spawn は browser baseline 外で worker attempt は structured diagnostic を返します。

`supportsCompiledPrograms` artifact では `compileSource` が source graph を一回 compile し owned `VeroScriptProgram` を返します。repeated `run` は program を reuse して fresh state、`runInteractive` は Asyncify artifact のみです。program は runtime 前に dispose し runtime disposal が remaining handle を release します。compiled-program は artifact capability で language/bytecode version ではありません。

## 詳細ページ

- [WebAssembly runtime と binding](runtime-and-bindings.md)
- [WebAssembly ABI、artifact、release 契約](release-contract.md)
- [WebAssembly build と artifact 契約](build-and-artifacts.md)

## C ABI のランタイムとプログラムハンドル

C runtime handle は one caller at a time です。result-producing call の pointer は次の replacement operation 又は runtime destruction まで runtime-owned です。Asyncify input wait 中は stateful getter/text operation が unavailable で、host は interactive call completion 後 retry します。

vero_web_supports_threads は pthread-backed script concurrency artifact を示します。default browser artifact は concurrent program に independent Web Worker を使います。この capability は supportsInteractiveInput/supportsCompiledPrograms SDK flag と別です。

context entry point は borrowed virtual-file path/source pair を受けます。compile は runtime-owned program handle を作り run_program/run_program_interactive は compiled module を fresh state で reuse します。deterministic cleanup が必要なら vero_web_destroy_program を runtime discard 前に呼びます。

## 仕様の基準となるソース

public WebAssembly header、`Bindings/`、native WebAssembly implementation、Emscripten build script、artifact audit、declaration parity、smoke/browser test を使います。WebAssembly ABI、generated artifact、execution policy、input-provider、cancellation、asset-packaging の変更とともに更新します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
