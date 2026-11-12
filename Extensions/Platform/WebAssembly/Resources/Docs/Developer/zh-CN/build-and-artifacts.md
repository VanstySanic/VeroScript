# Web 构建与生成物契约

## Emscripten 模块

WebAssembly CMake target 将 `VeroScriptWebAssembly` 链接到 core，并从 shared module entry point 生成两个 executable：支持 interactive input 的 `veroscript-engine`，以及不支持它的 `veroscript-engine-fast`。两者都将 `stdlib/` 和 `localization/` embed 到 virtual filesystem。

generated module export allocation、WebAssembly ABI version/thread capability query、create/destroy、language selection、catalog discovery、source run/check、source option 和 interactive-context entry point。export list 是 generated SDK contract 的一部分。

## SDK 源生成

build 将 TypeScript source 与 declaration copy 到 WebAssembly output directory。native public header、generated TypeScript declaration 与 parity test 必须一致。不得 hand-edit generated file 来掩盖 mismatch。

在仓库根目录运行下面的命令，重新生成带有 SPDX 声明和 Tab 缩进的类型声明；随后运行 `npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run typecheck` 验证一致性。

```sh
npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run generate:declarations
```

## release 检查

artifact audit 要求完整 interactive/fast module set、generated declaration 与 SDK source。browser smoke test 覆盖 headless execution、interactive input、event-loop behavior、Web Worker execution、worker termination。size budget 是 release review limit，而不是 ABI limit。

## export 与 declaration parity

generated Emscripten export list 必须包括 thread-capability query、language/catalog query、source option/context call、compiled-program create/execute/destroy、interactive program execution 和 context checking。TypeScript binding 必须 feature-detect additive compiled-program export，以便 capability 之前的 artifact 仍可使用，且不虚构 unsupported operation。

browser test 必须覆盖 borrowed virtual-file array、program-handle reuse/cleanup、每次 program run 的 fresh state、program suspended 时 interactive input、language discovery/selection、result-pointer lifetime 与 thread-capability branch。这些 check 不改变 language 或 bytecode version contract。

## 交互栈工作区

交互引擎暂停执行时，会从 Wasm 堆分配 1 MiB 的 Asyncify 栈快照工作区，并在恢复执行后释放。它支持嵌套绑定方法调用内部的输入等待。深层原生重入仍受宿主调用栈容量约束。快速引擎不分配 Asyncify 工作区。

此保存栈与 16 MiB 的 C/C++ 栈相互独立，用于保留绑定方法重入 VM 时的原生帧。发布门禁在 Node 和 Chromium 中覆盖源码与编译程序、`vm` 与 `mixed` 两种策略，连续两次暂停并恢复 32 层绑定调用链；同时验证 128 层原生重入在请求输入前被核心以 `diag.call_stack_overflow` 拒绝，随后同一程序可用较浅嵌套正常运行。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/build-and-artifacts.md) · [fr-FR](../fr-FR/build-and-artifacts.md) · [ja-JP](../ja-JP/build-and-artifacts.md) · [ko-KR](../ko-KR/build-and-artifacts.md) · [zh-CN](build-and-artifacts.md) · [zh-TW](../zh-TW/build-and-artifacts.md)
