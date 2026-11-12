# VeroScript C++ 技術文書

この directory は C++ source、public header、CMake target、test、generated artifact に結び付く developer material です。user guide ではなく API name、CLI option、JSON key、diagnostic、code example は翻訳しません。

## モジュールの責務

`VeroScriptCpp` は embedding/session surface、`VeroScriptCppAot` は single-translation-unit C++ bundle emitter、`VeroScriptCppFrontend` は stable C++17 AST、`VeroScriptCppBindGen` は Binding Catalog metadata、`VeroScriptCppTranspiler` は semantics-preserving subset の VeroScript lowering/validation を提供します。core は `VeroScript/` にあり C++ module は core に依存できますが逆は不可です。Frontend は BindGen/Transpiler で共有され、parse 後 policy は別です。

## 意味論の境界

tool は bounded subset のみを受けます。unsupported ownership/linkage/pointer/template/preprocessor/control flow は diagnostic を出し silent approximation は ABI/source-correctness regression です。Frontend は representable な qualified name、alias、attribute、range、constructor/destructor、friend、type-parameter template、out-of-class definition を保持します。BindGen が保持できても VeroScript source equivalent がない metadata は Transpiler が拒否します。

## output と transaction rule

multi-input tool は全 input を parse/merge 後に output を置換します。catalog、API reference、source map、manifest は atomic logical result で、duplicate basename は deterministic path-derived name にします。parse/merge/validation/final compilation failure は partial tree を残しません。generated API reference は document artifact、JSON Binding Catalog は compiler/IDE machine input です。

## concurrency と host contract

embedding/AOT host は full run 中 callback state を保持し worker が到達する state を synchronize します。script global/static field は native worker ごとに分離され、`channel<T>` は explicit synchronized capability です。native callback、interceptor、mutation observer、reachable AOT host symbol は concurrent になり得ます。

## engine と session contract

`VeroScriptEngine` は `CheckSource`、`CheckFile`、`RunSource`、`RunFile` を提供し file operation は bytecode cache、source label、stream、policy を explicit input とします。`VeroScriptSession` は stateful で run 中の mutation を拒否します。`LoadFile` が reload path を確立し、`Reload`、`ReloadCompiledModule`、`ReloadPackage` は stage/validate/commit して failure 時に working module を保持します。`Run(ERunMode::Eval)` は `main` を呼ばずにモジュールを初期化します。呼び出し間のグローバル状態は `SetHotReloadEnabled(true)` の場合だけ保持されます。`Clear()` は state/last JIT stats/reusable code を消去します。`SetJitPolicy`、`SetJitCacheLimits`、`GetJitCacheStats` は eligible session invocation の bounded reuse を公開します。

## public C++ AOT と tool output

AOT API は generated `.cpp` string または relative source/manifest を持つ `FCppAotSourceBundle` を返します。BindGen は merged catalog/optional API reference/manifest、Transpiler は source/map と project file record/manifest を返します。shared writer は atomic です。`FTypeMapOptions` は raw host pointer、rvalue reference、known host/enum/type parameter/alias を明示制御し、frontend AST は source range、attribute、alias、template、C linkage、qualifier、body、out-of-class definition を記録します。

## 詳細ページ

- [C++ グループアーキテクチャ](architecture.md)
- [C++ embedding と session](session.md)
- [C++ frontend、BindGen、Transpiler](frontend-and-tools.md)
- [C++ AOT emitter](aot.md)
- [C++ build と test の契約](build-and-tests.md)

## execution governance と session observability

`VeroScriptSession` は `SetExecutionLimits`、`SetExecutionControl`、`RequestCancel`、`SetCapabilities`、`SetJitPolicy`、`GetLastJitStats` と `EExecutionBackend` を提供します。zero limit は unlimited、cancellation は cooperative、capability restriction は VM が強制します。call surface は instance call、construction、field/global、callable、reflection、external serialization、entry-only bypass の `CallMethodBypassingInterceptor` を含みます。

## 仕様の基準となるソース

`Public/`、`Private*` implementation、module CMake、focused test が根拠です。public contract、output format、subset、ownership、concurrency guarantee の変更と同時に更新し identifier/path/target/diagnostic/version は source のままです。

## Session JIT 再利用

Session コードの再利用条件、制限付き実行、保持上限、集計は [C++ Session](session.md#session-jit-cache) で管理します。

## 保持バイト割当量

`MaxNativeBytes` または `MaxRetainedPayloadBytes` のゼロは該当領域の保持を許可しません。ポリシー予算のゼロ（無制限）とは異なります。

## キャンセルとホストへの通知

[C++ embedding と session — キャンセルとホストへの通知](session.md#session-control-and-streams) を参照してください。

同じ `FExecutionControl` トークンを共有する Session はキャンセル状態も共有します。`Reset()` と再利用の前に以前のキャンセル呼び出しの完了を待ち、古い要求がそのトークンを再利用する次の実行をキャンセルしないようにします。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
