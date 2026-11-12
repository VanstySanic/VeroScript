# 開発者文書 — VS Code

エディタとネイティブ間の共通担当範囲と六言語の保守は [共通エディタ統合](../../../../../../../../Resources/Docs/Developer/ja-JP/editors.md) で管理します。

Core、Tooling、VM、JIT、GUI プロバイダーは、C++ 言語モードを含む対応したネイティブソース／ビルド構成を使用してください。製品バージョンが同じだけでは ABI 互換性を確認できません。SDK 統合テストは静的ジェネリック、handle、入れ子の型のレシーバーについて補完とホバーを検証します。任意の `GetLastJitDiagnostics` スナップショットは C++ ホスト API に属し、現在の LSP/DAP 通信プロトコルでは公開されません。

UI メッセージには `vscode.l10n.t` と `Resources/Localization/Bundles` を使います。`Resources/Localization/<locale>/manifest.json` から `package.nls` を生成し、引数番号とマニフェスト参照を保持します。

Node.js 22.18.0+ とデスクトップ VS Code 1.106.0+ を使用し、モジュール内で以下を実行します。単体テストは設定/UI、統合テストはデスクトップ Electron/VS Code と現行 Runtime を必要とします。domain fixture は VM を実行しません。package:contents と VSIX ライセンスを確認し、ネイティブ、遠隔ホスト、実アプリ domain は別途検証します。

```sh
npm ci
npm run typecheck
npm run build
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
```

デスクトップの Extension Host スイートは一つずつ実行します。複数の VS Code テストウィンドウを同時に開くと、補完の確定、元に戻す操作、Enter のテストで必要な編集フォーカスを奪い合う場合があります。

ネイティブ引数テストは CLI 呼び出しごとに180秒を許容し、未使用のホストマクロを含めず、引数転送を GUI 全体のコールドコンパイルから独立させます。マクロ設定は専用の設定／プロトコルテストで扱い、実際の GUI コンパイルには別の受け入れ検証があります。これらの予算は LSP やデバッグの期限を変更しません。実際のプロセス引数を比較する前に JIT テストパスを正規化してください。

[Standards](../../../../../../../../Resources/Docs/Standards/ja-JP/GENERAL.md)



## ホスト統合と domain プロトコル

C++、UE などのホストは互換 TCP DAP と実際のネイティブバインディングを提供する必要があります。埋め込み例の RunDapTcp は Stdio.LiveBindings と VeroScriptDapTcp::Run を使用します。寿命、実行スレッド、切断後の待ち受けはホストが管理します。プラグインはスクリプト VM をデバッグし、ネイティブコードは対応するデバッガーを使用します。domain モードはホストが準備したブリッジを必要とし、実行中の JIT VM は引き継げません。編集用 bindingCatalogPaths、インポート、マクロ、標準ライブラリをホストに合わせてください。メタデータはローカル Runtime に実際のコールバックを登録しません。接続コマンドはコンパイル設定をホストに任せ、上書きは launch.json で明示します。設定範囲外のホスト依存ソースは自分で保存してください。アドレスはリモート環境を含む拡張ホスト側で解決されます。

ホストは IVeroScriptLiveDebugDomain を提供し、initialize で veroscriptSupportsDomainAttach: true を宣言する必要があります。宣言がなければ program に切り替えず失敗します。初回接続/initialize の期限は 10 秒です。出力に識別情報と不透明なリビジョンを記録します。ソースの自動比較や LSP 同期は未実装です。変数編集は veroscriptCanSetVariable に従い、非対応の一時停止/編集はエラーになります。切断では terminateDebuggee: false を送信し、接続喪失後は自動再接続しません。キャンセル、寿命、安全点はホストが管理し、実行前にブリッジを準備して現在の単一コンテキスト内でデバッグ対象呼び出しを直列化します。実際の UE 統合は別途検証が必要です。

ホストのフラグはステップ実行、式の評価、条件付きブレークポイントにも適用され、省略されたフラグは非対応として扱います。プラグインは対応する VS Code の機能情報を更新し、非対応の要求を明示的に拒否します。通常のブレークポイントは使用できます。ホストが veroscriptCanTerminate を宣言しても、停止・切断時のキャンセルはホストが管理します。 ホストの `terminated` 後も、`terminateDebuggee: false` でエディターの最終 `disconnect` を完了します。保留中の操作は一度だけ失敗し、遅延応答と重複する終了イベントは無視します。通信が閉じた場合、最後の切断要求にはローカルで応答します。

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/Developer/ja-JP/workspace-and-debug.md)

## メモリ検査の契約

DAP の `memoryReference` と `veroMemory` は、JavaScript の安全な整数範囲を超える値も含め、不透明な十進文字列として保持します。検査用の子項目は読み取り専用で、ハンドルは現在の停止中のみ有効です。ホストの機能宣言を転送し、メタデータから `readMemory`／`writeMemory` 対応を推測しないでください。統合テストはネイティブメモリ型の編集、実行、チェック、デバッグとドメインメタデータ転送を検証します。ドメインのプロトコル用テスト代替は実際のホスト VM を検証しません。

## クライアントの処理順序と双方向 DAP

連続するエディターイベントは、最新のドキュメント状態の同期を一回にまとめます。処理中に届いたイベントと明示的な再起動要求は次の処理に保持し、現在の処理が失敗しても続行します。ドメイン DAP はローカルイベントを追加して送信メッセージを採番し直すため、逆方向の要求への応答では `request_seq` をホストの元の番号に戻します。応答順序が逆でも対応を保持します。プロトコル用テスト代替は、端末や子デバッグセッションを起動せずに番号の対応を検証します。

## ネイティブモジュールソースの統合

`ModuleSources` は言語クライアントの境界で URI を変換し、公開された `moduleSourceMethod` を呼び出します。エディターの URI は元のプロトコル URI、サーバーの寿命、コンパイル指紋を保持し、サーバーには元の URI のみを送ります。読み取り専用ビューはソースのオーバーレイや編集対象になりません。欠落、破棄、キャンセル、変更されたスナップショットを拒否し、応答待ちの間の変更も確認します。モジュールのシンボルと診断は元のクライアントに属します。 自動再起動では、クライアントとコンパイル指紋が同じでも、新しいソースの寿命を開始します。

`VEROSCRIPT_TEST_MODULE_LSP` に `VeroScriptModuleEnvironmentToolingTests` 実行ファイルの絶対パスを設定すると、Extension Host でネイティブの `--lsp` テスト代替を実行します。未指定時はスキップされ、ネイティブ環境の検証にはなりません。既定の Runtime サーバーはネイティブ起動オプションを使用し、処理器の JSON 設定や CLI パッケージ環境の自動注入は追加しません。DAP の `sourceReference` と固定ソース応答は VS Code の標準処理を利用します。

アノテーション名の比較、正規表記、シグネチャの引数解析はネイティブ側で行い、複数行ラベル、入れ子のジェネリック、カンマを含むリテラルも扱います。結果は SDK を通して転送します。統合テストではクライアントを置き換えずに同じカタログの `caseSensitive` を変更し、VS Code から `source.organizeImports` を適用して正確な範囲、CRLF、冪等性を確認します。

## 変数の表示と正確な数値型

SDK とドメインブリッジを通じて、各 DAP 変数の `presentationHint` 全体、特に `attributes: ["readOnly"]` を保持します。`supportsSetVariable` と `veroscriptCanSetVariable` はセッションの操作機能を表し、すべての変数が書き込み可能になるわけではありません。最終的な検証はネイティブ側が行います。回帰ケースでは不変の `variant` の子項目、入れ子の値、ウォッチの子項目、通常のローカル変数の編集、およびドメインの編集機能が有効・無効の両方での情報転送を確認します。

正確な数値型はネイティブの名前と文字列の値を保持します。TextMate のコンテキスト別ケースでは `std::bigint`、`std::rational`、`std::numeric::rounding_mode` を扱い、末尾の名前をキーワードとして予約しません。追加・変更したケースは、対応するツールで型チェックと実際の Extension Host/VSIX 検証を行うまで未検証であり、以前の報告は新しいネイティブソースの検証を意味しません。

## 追加拡張のツール API

`Public/api.d.ts` が API 1 を定義します。`getToolchain(scopeUri, sourceFile?)` は基本拡張と同じ Runtime、コンパイル設定、環境、実行バックエンド・引数、ソース範囲を解決します。scope URI が設定、任意のソースがファイル変数を決定します。CLI バージョンを照会する場合があるので明示実行時のみ使用します。`compileArguments` はコマンド・エントリ・実行バックエンドを含みません。GUI チェック／コンパイルは VM を強制し JIT モジュールを省きます。VSIX に `Public` を含め、テストの `ready`／`snapshot` を維持します。対応する Runtime で実際の Extension Host と導入済み VSIX による API 検証を行ってください。原生 GUI の実行には別の検証ゲートがあります。

インタープリター実装は独立した BytecodeVm モジュールにあります。移動したエディターフィクスチャーにも `Vm/` を含む Runtime 一式を保持します。ネイティブ LSP/DAP の起動処理が既定のファクトリーを提供し、エディターは新しいプロトコルフィールドを追加したり、独自ホストのファクトリーを推測したりしません。SDK 回帰では、推論された関数キャプチャーのホバー・補完・署名・定義、UTF-16 範囲、不完全な編集後の現在のレシーバーのホバーを検証します。

ネイティブモジュールのフィクスチャーを移動する場合は、`VEROSCRIPT_TEST_VM_BACKEND` に Runtime の `Vm/` ディレクトリ内の対応するネイティブ VM モジュールの絶対パスも設定してください。この設定はフィクスチャーのビルド時に記録された VM パスを上書きします。通常の Runtime LSP/DAP プログラムは、ネイティブの既定ファクトリーを引き続き使用します。

publisher は `package.json` の宣言に従い、GUI の依存は基本拡張の識別子と一致させます。ルート README を詳細ページとして同梱し、除外したリポジトリファイルへリンクしないでください。機能パッケージの文書例外は `extension/readme.md` のみです。Marketplace 公開には登録済み publisher の権限と検証完了が必要で、マニフェストの変更では publisher は登録されません。

ファイルアイコンは[共有ジェネレーター](../../../../../../../../Resources/Assets/icons/README.md#regeneration)で再生成します。ビルド時に[共有 256px 製品 PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png)を拡張機能の表示アイコンへコピーします。

<!-- docs-navigation -->

---

[開発者文書](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
