# VeroScript — Rider

## インストール

設定 → Plugins → ディスクからインストールで ZIP を選択します。Rider 2026.2.2（262.10315.191）は検証用 SDK と宣言された最小ビルドです。後続版は互換性確認が必要です。編集、Run、Check、ローカルデバッグには 対応する Runtime を展開し、Languages & Frameworks → VeroScript でルートを指定します。直下に `veroscript`、`veroscript-lsp`、`veroscript-dap`、`stdlib/`、`localization/` が必要です。プロジェクト個人設定が全体既定値に優先し、両方空なら `PATH` を検索します。基本着色は Runtime 不要です。バイナリ交換後は言語サーバーを再起動します。

Runtime を更新・移動するときは、`Vm/` と対応するネイティブライブラリを含む展開先全体を保持します。
既定の VM はツール側が提供するため、Rider に追加の設定は不要です。

## 設定

意味設定は検証される JSON オブジェクトです。`importPaths`、`implicitImportPaths`、`stdlibPath` または `stdlibSearchPaths`、`bindingCatalogPaths`、`defines`、`predefinedMacros`、`allowCircularImports`、`allowProjectWideAccess`、`lspTraceAnalysis`、`lspRequestTimeoutMs`、`lspAnalysisTimeoutMs`、`maxCompletionItems`、`maxWorkspaceSymbolResults`、`maxReferenceResults`、`maxWorkspaceFiles`、`maxWorkspaceFileBytes` を指定できます。カタログはシンボル情報のみで、実行コールバックはホスト登録が必要です。共有設定は `.idea/veroscript.xml`、Runtime パスは個人設定です。ツール言語は BCP 47、空なら IDE 言語です。UI は六言語を提供します。

## 編集

`//` コメントの末尾でバックスラッシュが LF または CRLF の直前にある場合、次の物理行もコメントになります。ローカルの色分けと空ブロック内の Enter は同じ規則を使い、GUI マークアップのインライン VS にも適用されます。バックスラッシュの後に空白やタブがある場合は継続しません。

ホバーではシンボルの分類と名前を分け、組み込みの基本型とコンテナー型には分類の見出しを一つ表示します。ソースで定義した型エイリアスの分類と宣言は保持します。シグネチャ、オーバーロード、説明、例、定義位置は Runtime の内容を保持します。

VeroScript 設定の「空のブロック内で Enter を押すと開始波括弧を改行する」は既定で有効です。同じ行の空の宣言ブロックまたは制御ブロック内で Enter を押すと、開き波括弧を独立した行へ移し、内部のインデント位置にカーソルを置きます。コメント、文字列、選択範囲、複数カーソル、曖昧なヘッダーや複数行のヘッダーでは通常の Enter 動作を使います。この設定だけの変更では LSP を再起動しません。文書全体や範囲の整形は引き続きサーバーのスタイルに従います。

診断は Runtime の結果を保持します。`void t(){}` は有効な完全な関数ですが、`void t` は未完成で `()` が必要です。入力中の診断は、完成したコードをサーバーが受理すると消えます。

## パス

プロジェクトパスはルート基準です。作業ディレクトリもルートから解決し、プログラム、入力、JIT を含む実行上書きパスは解決後の作業ディレクトリ基準です。継承パスの基準は変わりません。`$PROJECT_DIR$` は常にルートです。省略項目は継承し、空を含む明示リストは起動時に置換します。実行設定は LSP 設定を変更しません。

## 実行と確認

保存済み `.vs` を右クリックするか実行構成を作成します。`main` のないトップレベル文も入口になります。`run` は実行、`check` は引数なしのコンパイル確認、`attach` は Debug で接続です。引数は IDE の引用規則（`""` は空引数）、環境は文字列の JSON オブジェクトです。通常入力はバイト、デバッグ入力は用意した UTF-8 テキストです。ローカル操作は先に保存します。Current File が非表示なら Registry の `run.current.file.item.in.run.configurations.combobox` を有効にします。

コンテキスト操作は保存済みのローカルファイルを対象に Run/Check を選択し、テンプレートの
アタッチ専用項目を除去します。Check は継承したスクリプト引数、入力、JIT パスも除去し、
適用可能な作業ディレクトリ、環境、コンパイラー設定は保持します。

## ローカルデバッグと JIT

通常 Run は `vm`、`mixed`、`jit` を選べます。JIT ファイル／ディレクトリ欄は互換モジュールを任意で固定します。空欄なら CLI のパス上書きを省略し、モジュール環境の選択を継承します。バックエンド未設定時の既定は auto です。JIT が無効、または利用可能な自動候補がない場合、mixed は VM を使用し、強制 jit は実行前に失敗します。Check/ローカル Debug は JIT をロードせず、attach はパス空が必要です。Debug は VM です。入口停止は `inherit`/`true`/`false`、深さ 0 は既定値。準備予算 1–3600 秒、既定 30 秒です。行/条件ブレーク、停止、ステップ、スタック、スコープ、ページ化変数、評価と代入を提供します。再開で参照失効。自動ホバーは名前パスのみ、他式は明示評価です。program 実行中のブレーク変更は次の停止で同期します。

変数の子要素は 1 回に最大 100 件表示します。ホストが件数を示さず完全な一覧を返す場合も展開できます。実行を再開すると、未表示の旧ページは失効します。

デバッガーが読み取り専用とした変数には変更操作を表示しません。他の変数の編集可否は引き続きホストの機能とデバッグのコンテキストに従います。

## プログラム接続

attach/program は TCP DAP ホストへの接続で、任意 PID や実行中 VM への接管ではありません。`veroscript --debug --dap-port=4711 script.vs` を起動しアドレス/ポートを設定します。プログラム空はホスト入口継承、作業ディレクトリ/入力/環境は空、backend/深さはホスト選択です。明示した意味上書きのみ送信します。非空引数は置換、空上書きオプションは引数消去です。一部空の意味リストはホスト既定値を継承し消去できません。

プログラムとドメインへのアタッチではホストアドレスの前後の空白を除去し、内部の空白や制御文字を拒否します。

## domain 接続

attach/domain は任意の期待 ID を指定でき、ローカル Runtime/ファイル不要です。起動上書きを空にし、環境 `{}`、入口 `inherit`、深さ 0、backend mixed、親環境継承を保持します。ホストが `veroscriptSupportsDomainAttach` を宣言しない場合、プログラムをロードせず失敗します。同一接続で複数呼び出しを扱い、ホスト能力に従って停止/ステップ/評価/条件/変更します。識別情報と不透明な版を表示しますがソース比較/LSP 自動同期はありません。物理ソースはローカルでアクセス可能なパスが必要で、パスマッピングはありません。アダプターのソース参照は読み取り専用スナップショットで表示します。接続 5 秒、初期化 10 秒、domain 接続は準備予算です。切断は `terminateDebuggee=false`、取消/プロセスはホスト管理です。接続喪失時は終了し自動再接続しません。

## 問題の調査

言語サービス状態、デバッグコンソール、IDE ログを確認し、同じ Runtime のツールを揃えます。対象はローカル Rider と Windows/Linux x64 Runtime です。他 IDE、Remote Development、Windows から WSL 起動、macOS/ARM64、実際のアプリ/UE domain 統合は別途検証が必要です。

```json
{"importPaths":["src"],"defines":["FEATURE=1"],"bindingCatalogPaths":["bindings/catalog.json"]}
```

[LSP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/lsp.md)

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/debugging.md)


## 管理対象メモリ

対応する最新の Runtime ツールを使用すると、`std::memory` の補完、ホバー、セマンティック着色は LSP が提供します。デバッグの一時停止中に変数またはウォッチの buffer や view を展開すると、`[byte_length]`、buffer の `[object_id]`、利用可能なアドレスを確認できます。検査項目は読み取り専用で、アドレスは一時的な観測値です。プラグインは任意のメモリ読み書きを提供しません。実行を再開すると変数参照は失効するため、次の停止位置で再度展開してください。

[std::memory](../../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md)

## コンパイル拡張と生成ソース

現在のツールは公開マクロ（`#define_export`、`export import`）とソースコンパイル拡張（`import compiler "extension.vs";`）をサポートします。対応する CLI/LSP/DAP ツールと同じインポートパスを使用してください。DAP `sourceReference` を持つ生成メソッドと仮想モジュールは、読み取り専用のデバッグスナップショットで開きます。スナップショットでは Run、Check、ブレークポイント、LSP 同期は無効です。ソース取得失敗は通知されますが、一時停止中のセッションは終了しません。ネイティブホストの処理レジストリとモジュール環境はホストが設定し、プラグインは追加の意味設定 JSON フィールドとして公開しません。

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
