# バックエンドの保守

[コア JIT サービスと並行処理](../../../../../../../VeroScript/Resources/Docs/Developer/ja-JP/jit-and-concurrency.md#native-abi-version) を参照してください。

Core は実行契約、公開 JIT 契約、ランタイムサービス、検証、スケジューリング、世代集計を所有します。具体的な IR、lowering、ネイティブ生成、ポータブル Linear 実行、実行コード保管は Core ではなく `Modules/Execution/VeroScriptAsmJit` が所有します。`Private/JitSupport` は汎用キャッシュとモジュールローダーを実装します。バイナリ互換性は製品バージョンだけでなく公開ヘッダーの指紋とコンパイラー/STL/runtime/プラットフォームの照合で決まります。 バイトコードインタープリターは独立した `VeroScriptBytecodeVm` モジュールに属します。

## ビルドと ABI

`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` は具体バックエンドなしで Core をビルドします。`ON` にすると `VEROSCRIPT_ENABLE_ASMJIT=ON` はネイティブ生成、`OFF` はポータブル Linear を有効にします。ソースホストは `VeroScriptAsmJit` をリンクし `CreateAsmJitBackendFactory()` を注入します。`VeroScriptAsmJitBackend` は Core のコピーを含まない共有モジュールです。インポートした emitter は必要な PIC を提供し、所有者が共有依存を配置します。Windows の動的ロードには `/MD` または `/MDd` が必要です。

```cmake
target_link_libraries(MyHost PRIVATE VeroScriptAsmJit)
```

## ランタイム契約

コンパイルの `Ready / Unsupported / Deferred / Failed` と呼び出しの `NotEntered / Returned / ControlTransfer / Failed` を区別します。VM 再実行を許すのは `NotEntered` だけです。公開前に Core が所有権、サービス、予算を検証します。実行コードはコードとモジュールを保持し、コンテキスト、フレーム、値は呼び出し中だけ借用します。入れ子呼び出し後はビューを再取得し、エラー／制御／コールバックのライブフラグを高速経路判断ごとに読み直します。

Session コードの再利用条件、制限付き実行、保持上限、集計は [C++ Session](../../../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ja-JP/session.md#session-jit-cache) で管理します。

## 公開検証

`veroscript_jit_query` の POD 交渉は C++ ファクトリー作成前に API、ヘッダー指紋、コンパイラー/STL、C++ モード、ランタイム、プラットフォームを照合します。ファクトリー、ワーカー、実行コードの寿命までライブラリを保持します。Core 単独、注入契約、非互換ライブラリ、明示選択、再実行しないフォールバック、資源回収、Session キャッシュ無効化を検証します。配布は `package-veroscript-jit.py` を使い、独立バージョンでも ABI 照合は必要です。iOS 実機／シミュレーターは動的ロードとネイティブ生成を禁止し、静的ポータブルバックエンドは別途検証が必要です。

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/ja-JP/jit-and-concurrency.md)

## 保持バイト割当量

`MaxNativeBytes` または `MaxRetainedPayloadBytes` のゼロは該当領域の保持を許可しません。ポリシー予算のゼロ（無制限）とは異なります。

公式バックエンドアーカイブは `Release/Modules/Execution/Jit/<target>/<version>/archives/` に公開します。Modules の新規専用インストーラーはありません。機能ペイロードは実行依存と法的通知を保持し、文書、例、テスト、ベンチマーク、シンボルは含めません。正本文書は Resources/Docs に保ち、統合 Resources 文書で公開します。

## 同梱依存ライブラリの保守

AsmJit はこのモジュールが所有します。ソースを `ThirdParty/asmjit` に置き、[元のライセンス](../../../../ThirdParty/asmjit/LICENSE.md)を保持し、上流のリビジョンを [VERSION.txt](../../../../ThirdParty/asmjit/VERSION.txt) に記録します。Resources/Docs や Core にソースを移しません。`VEROSCRIPT_ENABLE_ASMJIT=OFF` は構築するバックエンドの移植可能な実装を選び、`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` はこのバックエンドモジュールを除外します。

更新時は同梱リビジョンを置き換え、通知とコード生成データベースのライセンスを保持し、VERSION.txt と[モジュールの通知](../../../Licenses/NOTICES.md)のライセンス転載を正確に更新してから、再構成して JIT とパッケージのテストを実行します。コンパイラー/STL/ランタイムの互換性を維持し、ネイティブ生成と移植可能な構成の両方を検証します。インストールコンポーネントは LICENSE.md と VERSION.txt を `licenses/asmjit/` に出力するため、所有元のソースとバイト単位で比較します。共通の通知索引は Resources/Licenses/README.md に置き、依存関係の通知は利用するモジュールが所有します。

## 実行制御付きスカラーブロックと部分的なループ対応

短い整数ループはネイティブ CFG と Core のプリミティブ値ブロックで実行できます。各バイトコード命令で引き続き制限・キャンセル・タイムアウトを確認し計数し、一括で計数を先払いしません。EnableScalarBlocks はカスタムのホスト実行チェックと変更オブザーバーを入口前に拒否します。未対応の表現は現在の命令から通常サービスに戻し、チェックや計数を重複させず関数も再実行しません。複雑なループはコストに基づき VM を選択します。

型が一致する文字列 += は通常の CFG 検証に参加します。未最適化の文字列ループは、同じ関数にある別のネイティブループを無効にしません。実行制御付き Mixed の明示的な有効化規則は変わりません。

短い分岐のない整数レシーバ式では、ネイティブ呼び出しの橋渡しより Core の有界呼び出し計画を優先できます。通常のバックエンドのコンパイルと検証後に行うコスト判断です。失敗、変更監視、実行制御は既存の VM 経路を保ち、大きな本体や呼び出し、ループはそれぞれの計画を選択します。

`bPreferVmForNestedExecution` は実行制御のないインタープリター側の呼び出し元だけに適用し、ホストの直接呼び出しとネイティブ側の呼び出し元はネイティブ入口を保持します。

[コア JIT サービスと並行処理 — 並行処理と検証](../../../../../../../VeroScript/Resources/Docs/Developer/ja-JP/jit-and-concurrency.md#construction-dispatch) を参照してください。

<!-- docs-navigation -->

---

[開発者文書](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
