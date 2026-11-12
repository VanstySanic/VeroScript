# CLI process と asset 契約

## エントリーポイント

`VeroScriptCli` は `veroscript`、`veroscript-lsp`、`veroscript-dap` の thin process layer です。通常 main は compile/check/run と package operation を dispatch します。REPL/watch code は interactive source/file-change orchestration を追加します。LSP/DAP main は language behavior を Tooling に delegate します。

CLI は compiler、VM、debugger、workspace semantics を重複実装してはいけません。subcommand name、option、environment variable、exit code、diagnostic ID、output format は compatibility token であり、localize しません。

## リソースの解決

resolution は source root、SDK root、`bin` directory、standard-library directory、localization directory を区別します。executable と runtime asset は compatible installation から来なければなりません。environment-variable/`PATH` fallback は discovery behavior であり、installation を混在させる許可ではありません。

## プロセスの動作

stdout、stderr、diagnostic line handling、input stream、process cancellation、exit-code propagation は IDE、SDK、shell consumer に対して stable でなければなりません。LSP/DAP framing は Tooling transport implementation の責務です。CLI が所有するのは process startup と termination だけです。

## Unicode のプロセス境界

Windows の `wmain` は `VeroScript::Cli::Run` の前に UTF-16 引数を一度だけ UTF-8 に変換します。引数を所有する文字列とポインター配列は呼び出し全体で有効でなければなりません。ファイル操作には UTF-8 パスの補助関数を使います。`VeroScriptUnicodeCliTests` は VM、mixed、JIT 優先モードで非 ASCII のソースとインポートパス、スクリプト引数、パッケージ実行を確認します。端末表示とパイプのデコードは利用側の責任です。

## 登録とディスパッチの契約

Core は `VeroScriptCommandLine.h` を公開し、CLI はレジストリ、引数解析、実行振り分けを所有します。ネイティブの `Create` 中に `INativeExtensionHost::GetService("veroscript.cli", "0.1.1", error)` を取得し、互換の `ICommandLineExtensionService` を使って所有する `FCommandLineModule` 記述子を同期登録します。登録は実行前に閉じ、不正な宣言はバッチ全体を無効にします。複数ロールのモジュールは CLI 以外のホストでサービスが存在しない場合にも対応します。 各モジュールはコマンドまたはオプションを一つ以上宣言し、オプションを宣言する場合は Configure コールバックを指定します。

ID と名前は大文字小文字を区別する ASCII 英数字、アンダースコア、ハイフンで最大 64 文字、先頭/末尾ハイフンは禁止です。Core 名は予約されています。最大 256 モジュール、各モジュールのコマンドとオプション合計は 256 です。説明/用法/値ラベルは制御文字なしで最大 8192 バイトです。オプションは既定で値必須かつ繰り返し不可です。指定したフラグは空文字列一つ、未指定オプションはマップ項目なしになります。値が別オプションに見える場合は `--name=value` を使います。

Configure はモジュール ID 順に一度だけ状態を検証/保存し、通常実行やコマンド振り分けより前に呼ばれます。自身の値だけを受け取り、スクリプトを実行してはいけません。ヘルプは設定とコマンド実行を省略しますが、読み込みと登録は行います。Run は UTF-8 引数、言語タグ、ストリーム、選択環境を受け取ります。借用コンテキストを保持せず、登録を所有する環境への強参照循環を避けます。ネイティブ Destroy の復帰前にワーカーを終了させてください。ホストはコードを保持し、例外を捕捉し、終了コード 0..255 を検証します。ブロックするネイティブコマンドは自身でキャンセルを管理します。

[NativeModule のソース例](../../../Examples/NativeModule/README.md) は対応 SDK によるビルドとパッケージ統合を示します。`VeroScriptCliModulesTests` と `VeroScriptCliRegistryTests` は引数解析、名前衝突、Unicode、寿命、有効化/無効化、引数境界を検証します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/process-and-assets.md) · [fr-FR](../fr-FR/process-and-assets.md) · [ja-JP](process-and-assets.md) · [ko-KR](../ko-KR/process-and-assets.md) · [zh-CN](../zh-CN/process-and-assets.md) · [zh-TW](../zh-TW/process-and-assets.md)
