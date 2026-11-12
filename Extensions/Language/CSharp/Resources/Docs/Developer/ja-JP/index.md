# VeroScript C# 技術文書

この directory は C# group の developer document で、C# source、Interop header、CMake target、managed test に結び付きます。end-user tutorial や public API XML documentation の複製ではありません。

## モジュールの責務

`VeroScriptCSharp` は `VeroScriptInterop` 上の NativeAOT-compatible .NET 8/9 runtime surface、`VeroScriptCSharpAot` は C# AOT emitter、`VeroScriptCSharpBindGen` は declaration-to-catalog/generation、`VeroScriptCSharpFrontend` は shared semantic declaration model を提供します。managed adapter は stable C ABI だけを利用し、C++ private header や compiler/runtime の再実装を含みません。

## ABI と lifetime rule

Interop handle は opaque ownership object です。lease、buffer、package、callable、callback registration、session operation は `VeroScriptInterop.h` と wrapper の lifetime rule に従います。wrapper は encoding/buffer/status/diagnostic/callback exception を意味を変えず marshal し、ABI failure を success に変えたり捨てたりしません。

## frontend と generator boundary

frontend は user code を実行せず、BindGen は metadata を emit して native thunk を作りません。unsupported signature/ownership/default/inheritance は output replacement 前に source diagnostic で失敗します。AOT は別 lowering path です。

## 互換性の規則

checked API contract と package verification は public type、layout、enum、callback convention、metadata、framework を保護します。technical token は翻訳しません。

## マネージドランタイムの詳細

`VeroScriptRuntime` は process compatibility gate で `EnsureCompatible()` が session 前に必要です。`VeroScriptSession` は native handle/managed root owner、`VeroScriptValue` borrowed value は scope-limited、`VeroScriptCallable` の owner-thread/dispose rule は lifetime-only、`VeroScriptPackage` は metadata/signed byte/load state を所有します。

## binding と generated-code detail

host binding は documentation、annotation、type parameter、parameter metadata、enum underlying type、const/mutable flag、operator、clone/equality/hash protocol、subclass factory、generic-host callback を保持する metadata と callback であり function-pointer table ではありません。

## 詳細ページ

- [C# runtime と Interop adapter](runtime-and-interop.md)
- [C# binding、package、互換性](bindings-and-packages.md)
- [C# toolchain と package 契約](toolchain-and-packaging.md)

## マネージド実行の契約

`VeroScriptConfiguration` は overlay/import/cache、depth、`MaxInstructions`、`TimeoutMilliseconds`、execution control、capability policy、allowed capability、`VeroScriptExecutionBackend` を持ち zero limit は unlimited です。`RequestCancellation` は non-owner thread から設計された唯一の session operation で session は return まで生存します。`VeroScriptJitPolicy`、`GetLastJitStats`、`SetJitCacheLimits`、`GetJitCacheStats` は `SessionJitCache` capability がある場合に adaptive compilation/last snapshot/bounded context を公開します。非対応のエントリは VM にフォールバックします。デバッガー接続時と保持状態の初期化には VM を使い、対応する呼び出しは同じ世代のコードを再利用できます。実行ガバナンスが有効な `Mixed` は既定で VM を使い、対象の負荷を測定してから `VeroScriptJitPolicy.EnableControlledMixed` で制御付き JIT を有効にします。明示的な `Jit` 選択はこのスイッチに依存せず、未対応または VM が優先される入口は制限を保った VM にフォールバックします。

## 仕様の基準となるソース

`VeroScriptCSharp/`、frontend/BindGen project、Interop public header、CMake、managed test を根拠とし、ABI/ownership/threading/generated code/package contract の変更と同時に更新します。

`VeroScriptCSharp`: [Android と iOS / MAUI](../../User/ja-JP/mobile.md).

モバイル対応パッケージも同じプロジェクトとパッケージ名を使用します。`VeroScriptIncludeMobile=true` により .NET 8/9 と .NET 10 Android/iOS のターゲット、および検証済みネイティブ SDK を含めます。


[Android/iOS の CSharp](mobile.md)

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
