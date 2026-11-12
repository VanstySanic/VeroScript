# VeroScript コア技術文書

[交換可能な VM バックエンド](vm-backends.md)

この directory は core maintainer と module author 向けの、実装、public header、build target、test に拘束された技術契約です。end-user tutorial や API reference の代替ではありません。

## Pipeline

core は source、overlay、import、package を解決し、tokenize、preprocess、public AST、semantic/type/generic check、debug 情報付き bytecode、module/package、VM/JIT 実行を順に処理します。failure は structured diagnostic/source location を保持し、別の program や partial artifact を作りません。`VeroScriptModuleLoader` の import path、implicit import、stdlib root、overlay、`-D` macro、host type name、circular/project-wide policy は explicit input で、normalization が source fingerprint に寄与します。stdlib root は隣接 `localization/` tree を merge できます。

## ランタイムの不変条件

`VeroScriptVm`、`FValue`、object/container handle、function/host value、reference は一つの ownership model です。destruction、reference、mutation event、external serialization、retained callable は identity/lifetime を保存します。container mutation callback は完了した mutation を観測し、exception は rollback しません。script global/class static field、task registry/task ID は worker-local で、channel が explicit synchronized capability、session/callable creator-thread restriction は embedding contract です。

## 実行バックエンド

[コアアーキテクチャとコンパイルパイプライン — 境界と意味論](architecture-and-pipeline.md#core-vm-ownership) を参照してください。

## 公開部と内部実装の境界

`Public/` は supported native include surface、`Private/` は compiler/parser/VM/JIT/implementation detail を含みます。外部 consumer と language adapter は public header を使います。source tree 内の少数の embedding/tooling/AOT implementation target だけが CMake から private include path を明示的に受け取ります。これは review 対象の friend dependency であり、installed/supported API ではなく、core owner の review なしに増やしません。

native C++ SDK は build-matched static-library contract です。consumer は同一の header、library、architecture、runtime、build identity で再ビルドし、build 間の C++ binary-layout compatibility は保証されません。stable process ABI は C11 `VeroScriptInterop` surface のみです。public C++ layout、enum value、bytecode format、host-binding record、diagnostic ID、package metadata の変更には引き続き compatibility review が必要です。

VM の所有権、bytecode metadata、exception、control flow は全 backend で同一です。worker は global/static state を分離し、`channel<T>` が明示的な cross-worker synchronization です。callback と到達可能 host state は並行になり得るため、lifetime と synchronization は host の責任です。public header、stable C ABI、released package format が consumer contract であり、private implementation は依存対象ではありません。

## localization と asset

core は shared localization asset root の runtime/tooling catalog を canonical BCP 47 directory 名で load します。locale discovery は directory/catalog based で locale 数は compile-time constant ではありません。source identifier、diagnostic ID、JSON key、API name、path、version、code sample は翻訳しない technical token です。

## session、package、host binding

session surface は source/file/package load、compile without commit、compiled module commit、run、clear、reload を分離します。file-backed reload は source path を保持し overlay は reload path を作りません。package は application/library shape を検証し、hot reload は opt-in で明示的に reloadable な state だけを保持します。host binding catalog は native function、host type、generic template/instantiation、member、operator、clone callback、inheritable type、scoped enum、subclass factory を validate し、reflection は private compiler structure を露出せず metadata を enumerate します。

## execution governance と JIT observability

`Vm`、`Mixed`、`Jit` は requested policy であり、debug、保持された初期化区間、有界クリーンアップ、unsupported entry、governance により effective backend は VM へ fallback できます。`FExecutionLimits` の zero は unlimited、`FExecutionControl` の cancellation は cooperative です。`RequestCancel` は shared control を mark しますが、active native/managed callback を強制終了しません。`FScriptCapabilities` による file/environment/console/task restriction は VM で強制され、JIT は迂回できません。

`FJitPolicy` と `FJitStats` は adaptive compilation と直近 execution を公開します。通常の free-function call は制限付きの条件下で新 VM 間の session code reuse を利用できます。`FSessionJitCacheLimits` の既定値は native code 8 MiB、retained payload 32 MiB、zero は対応 resource の retention を禁止します。cache stats は context の累積・安定値であり、live または process peak ではありません。

## 詳細ページ

- [コアアーキテクチャとコンパイルパイプライン](architecture-and-pipeline.md)
- [コアのビルドとランタイム asset](build-and-assets.md)
- [コア実行、状態、package](execution-and-state.md)
- [コア JIT と並行性](jit-and-concurrency.md)

JIT policy と `FJitStats` は public observability contract です。statistics は compile、cache、dispatch、fallback/failure、budget、entry-kind、fast-path activity を区別します。unsupported compilation は execution 自体が failure でない限り通常の fallback decision であって language diagnostic ではありません。

## コンパイラーモデルの更新

parser/compiler model は static type modifier と static data member を module-owned backing global として保持します。static access は declaring type hierarchy/interface で解決され、generic instantiation は substituted static storage を持ちます。AST は lambda capture、generic parameter/bound、alias、annotation、source range を保持し、AST consumer は identity/location を保存します。unsupported capture/generic form は明示的に failure します。

## 変更時の根拠

言語動作は public header、core implementation、module CMake、focused test、release asset manifest を根拠とします。compile model、backend、concurrency、package、localization、stable contract の変更では本書を同じ change で更新します。identifier、path、target name、diagnostic ID、JSON key、version value は source のままで翻訳しません。

- [ローカライズ資源と配布](localization-and-resources.md)

- [管理メモリの実装契約](memory-runtime.md)

- [コンパイル処理とソースメタデータ](compile-processing.md)
- [スクリプトによるコンパイル拡張](script-compile-extensions.md)

- [アプリケーション国際化の実行契約](i18n-runtime.md)

- [モジュール環境とネイティブ拡張](module-environments.md)

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
