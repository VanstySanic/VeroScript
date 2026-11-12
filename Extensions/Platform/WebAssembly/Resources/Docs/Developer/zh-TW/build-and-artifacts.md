# Web 構建與生成物契約

## Emscripten 模組

WebAssembly CMake target 將 `VeroScriptWebAssembly` 連結到 core，並從 shared module entry point 生成兩個 executable：支援 interactive input 的 `veroscript-engine`，以及不支援它的 `veroscript-engine-fast`。兩者都將 `stdlib/` 和 `localization/` embed 到 virtual filesystem。

generated module export allocation、WebAssembly ABI version/thread capability query、create/destroy、language selection、catalog discovery、source run/check、source option 和 interactive-context entry point。export list 是 generated SDK contract 的一部分。

## SDK 源生成

build 將 TypeScript source 與 declaration copy 到 WebAssembly output directory。native public header、generated TypeScript declaration 與 parity test 必須一致。不得 hand-edit generated file 來掩蓋 mismatch。

在儲存庫根目錄執行下列命令，重新產生含 SPDX 宣告和 Tab 縮排的型別宣告；接著執行 `npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run typecheck` 驗證一致性。

```sh
npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run generate:declarations
```

## release 檢查

artifact audit 要求完整 interactive/fast module set、generated declaration 與 SDK source。browser smoke test 覆蓋 headless execution、interactive input、event-loop behavior、Web Worker execution、worker termination。size budget 是 release review limit，而不是 ABI limit。

## export 與 declaration parity

generated Emscripten export list 必須包括 thread-capability query、language/catalog query、source option/context call、compiled-program create/execute/destroy、interactive program execution 和 context checking。TypeScript binding 必須 feature-detect additive compiled-program export，以便 capability 之前的 artifact 仍可使用，且不虛構 unsupported operation。

browser test 必須覆蓋 borrowed virtual-file array、program-handle reuse/cleanup、每次 program run 的 fresh state、program suspended 時 interactive input、language discovery/selection、result-pointer lifetime 與 thread-capability branch。這些 check 不改變 language 或 bytecode version contract。

## 互動堆疊工作區

互動引擎暫停執行時，會從 Wasm 堆積配置 1 MiB 的 Asyncify 堆疊快照工作區，並在恢復執行後釋放。它支援巢狀繫結方法呼叫內部的輸入等待。深層原生重入仍受宿主呼叫堆疊容量約束。快速引擎不配置 Asyncify 工作區。

此保存堆疊與 16 MiB 的 C/C++ 堆疊相互獨立，用於保留繫結方法重入 VM 時的原生堆疊框架。發佈門檻在 Node 和 Chromium 中涵蓋原始碼與已編譯程式、`vm` 與 `mixed` 兩種策略，連續兩次暫停並恢復 32 層繫結呼叫鏈；同時驗證 128 層原生重入在要求輸入前被核心以 `diag.call_stack_overflow` 拒絕，隨後同一程式可用較淺巢狀正常執行。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/build-and-artifacts.md) · [fr-FR](../fr-FR/build-and-artifacts.md) · [ja-JP](../ja-JP/build-and-artifacts.md) · [ko-KR](../ko-KR/build-and-artifacts.md) · [zh-CN](../zh-CN/build-and-artifacts.md) · [zh-TW](build-and-artifacts.md)
