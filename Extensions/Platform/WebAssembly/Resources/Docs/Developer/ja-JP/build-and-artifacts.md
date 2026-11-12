# WebAssembly build と artifact 契約

## Emscripten モジュール

WebAssembly CMake target は `VeroScriptWebAssembly` を core に link し、shared module entry point から interactive input 対応 `veroscript-engine` と非対応 `veroscript-engine-fast` の二 executable を作成します。両方が `stdlib/` と `localization/` を virtual filesystem に embed します。

generated module は allocation、WebAssembly ABI version/thread capability query、create/destroy、language selection、catalog discovery、source run/check、source option、interactive-context entry point を export します。export list は generated SDK contract の一部です。

## SDK ソースの生成

build は TypeScript source/declaration を WebAssembly output directory に copy します。native public header、generated TypeScript declaration、parity test は一致しなければなりません。mismatch を隠すため generated file を hand-edit してはいけません。

リポジトリのルートで次のコマンドを実行し、SPDX 宣言と Tab インデントを含む型宣言を再生成します。その後 `npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run typecheck` で一致を検証します。

```sh
npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run generate:declarations
```

## リリースの検証

artifact audit は complete interactive/fast module set、generated declaration、SDK source を必要とします。browser smoke test は headless execution、interactive input、event-loop behavior、Web Worker execution、worker termination を実行します。size budget は release review limit で ABI limit ではありません。

## export と declaration parity

generated Emscripten export list は thread-capability query、language/catalog query、source option/context call、compiled-program create/execute/destroy、interactive program execution、context checking を含みます。TypeScript binding は additive compiled-program export を feature-detect し、その capability より前の artifact でも unsupported operation を作らず利用可能にします。

browser test は borrowed virtual-file array、program-handle reuse/cleanup、program run ごとの fresh state、program suspend 中の interactive input、language discovery/selection、result-pointer lifetime、thread-capability branch を検証します。どれも language/bytecode version contract を変更しません。

## 対話実行のスタック領域

対話エンジンは実行を一時停止するときに Wasm ヒープから 1 MiB の Asyncify 保存スタックを確保し、実行状態の復元後に解放します。これにより、束縛メソッドの入れ子呼び出し内で入力を待機できます。深いネイティブ再入はホストの呼び出しスタック容量にも制限されます。高速エンジンは Asyncify 領域を確保しません。

この保存スタックは 16 MiB の C/C++ スタックとは別の領域で、束縛メソッドが VM に再入する際のネイティブフレームを保持します。公開前の検証は Node と Chromium で、ソース実行とコンパイル済みプログラム、`vm` と `mixed` の両方を対象に、32 段の呼び出しチェーンの停止と復元を 2 回行います。また、128 段のネイティブ再入が入力要求前に `diag.call_stack_overflow` で拒否され、その後同じプログラムを浅いネストで正常に実行できることを確認します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/build-and-artifacts.md) · [fr-FR](../fr-FR/build-and-artifacts.md) · [ja-JP](build-and-artifacts.md) · [ko-KR](../ko-KR/build-and-artifacts.md) · [zh-CN](../zh-CN/build-and-artifacts.md) · [zh-TW](../zh-TW/build-and-artifacts.md)
