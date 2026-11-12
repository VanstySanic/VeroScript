# VeroScript CLI 技術文書

`VeroScriptCli` は薄いプロセス層です。compiler、VM、C++ embedding API、language-service semantics を再実装せず、`veroscript`、`veroscript-lsp`、`veroscript-dap` の entry point を提供します。

## プロセスの責務

CLI は argument parsing、command dispatch、process-level stream、REPL/watch orchestration、source/file selection、environment/SDK asset lookup、exit-code policy、LSP/DAP process main を所有します。core、C++ session、AOT、Tooling module が language behavior の source of truth です。

command option、subcommand name、environment variable、exit code、diagnostic ID、output format は compatibility surface です。prose を改善する目的だけで翻訳または変更してはいけません。

## ランタイムリソース

standalone process は version-matched standard library と localization catalog を見つけなければなりません。asset resolution は relocatable であり、古い build-directory copy を黙って使ってはいけません。asset の欠落または非互換は startup/configuration failure であり、empty-language fallback ではありません。

## プロトコルのエントリーポイント

LSP/DAP main は protocol behavior を `VeroScriptTooling` に delegate します。CLI は、これらの service が期待する stream framing、request ordering、cancellation、stderr/stdout separation、process exit behavior を保持します。

## command と asset の詳細

通常 main は compile/check/run と package-oriented operation を dispatch します。REPL と watch process は interactive source/file-change orchestration を追加します。`VeroScriptLspMain` と `VeroScriptDapMain` は protocol-only process entry point であり、language behavior を `VeroScriptTooling` に delegate します。CLI は Session、debugger、workspace state の第二実装を作成してはいけません。

CLI path resolver は source root、SDK root、`bin` directory、standard-library directory、localization directory の区別を維持します。発見された executable と asset は compatible installation のものでなければなりません。environment-variable と `PATH` fallback は discovery behavior であって、version を混在させる許可ではありません。

## 詳細ページ

- [CLI process と asset 契約](process-and-assets.md)

## 仕様の基準となるソース

`Private/`、CLI CMake target、process test、Tooling public API、packaging/SDK smoke test を使います。command dispatch、asset lookup、stream behavior、environment variable、exit code、LSP/DAP process boundary を変える際は、この文書を更新してください。

## プロジェクトの初期化

`Private/VeroScriptCliProject.cpp` はローカルの C++17 ファイル作成で `project init` を実装し、
パッケージ環境の読み込みやコンパイルは行いません。排他的作成により並行呼び出しでも既存ファイルを
上書きせず、ネイティブ CLI レジストリは `project` を予約します。
`VeroScriptProjectInitCliTests` はパス、再実行、競合、並行作成、書き込み失敗からの復旧を検証し、
ネイティブビルドでのみ登録されます。
ビルドサービスがある場合は生成アプリケーションと文書のサンプルもビルドして実行します。
テンプレートは既存の `0.1.1` スキーマに合わせ、依存ロックやインストール状態を生成しないでください。
UTF-8 パス変換は C++17 と C++20 に対応し、パッケージ用 CMake 回帰では両モードで
初期化処理を独立してコンパイルし、警告をエラーとして扱います。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
