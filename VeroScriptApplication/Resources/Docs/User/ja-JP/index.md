# アプリケーションコンテナーと実行サービス

実行可能なアプリは標準 ZIP `.vsapp` コンテナーで配布します。`veroscript-app.json`、`veroscript-app.files.json` と、通常はコンパイル済みエントリー `program/entry.bin` を含み、ソースエントリーにも対応します。`.vslib` はコンパイル済みライブラリーです。インストール用 `.vspkg/.zip` は別のルート `veroscript-package.json` を使用し、利用前にインストールが必要です。

`veroscript application.vsapp` または `veroscript-runtime application.vsapp -- 引数` で実行し、`--check` ではエントリーを実行せず検査します。同じ内容からディレクトリー、ZIP、任意の PE/ELF 実行ファイルを作れます。Runtime、依存環境、ランチャーは独立して選択します。読み込み時にインストールや PATH 検索は行いません。

C++ は `VeroScriptSession::LoadApplication`、または `CreateApplicationServices()` → `FApplication::Open` → `Prepare` を使用します。ソースには `FPreparedApplication::CompileSource` を使い、保存したマクロとコンパイル設定を適用します。C# は `VeroScriptSession.LoadApplication`、Python は `Session.load_application` を使用します。C ABI の所有ハンドルは対応する破棄関数で解放します。JSON オプションのパスは絶対パスです。 `VeroScriptSession::LoadFile` と `VeroScriptEngine::RunFile` もアプリケーションのコンテナーとディレクトリを認識します。`ReadEntryPackage` はネイティブファクトリーをロードせずイメージを検査します。

[C++ SDK — 統合の順序](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/index.md#explicit-vm-selection) を参照してください。

全ファイルの長さ、SHA-256、実行権限を検査し、余分なファイル、不正パス、リンク、破損 ZIP、予算超過を拒否します。内部パスは UTF-8 で最大 511 バイトです。書き込み可能なキャッシュはアプリ本体と分離し、所有権記録とプロセス間ロックを使います。

任意の libsodium アダプターは XChaCha20-Poly1305 IETF と Ed25519 を提供します。外側の ZIP は暗号化しません。信頼する公開鍵と独立署名数は利用側で設定し、失敗しても平文へ切り替えません。ディレクトリーと単一実行ファイルには内容署名を使い、OS のコード署名は別工程です。鍵ファイルを配布物へ入れないでください。

Runtime への委譲は一度だけで、引数、出力、終了コードを転送します。`VeroScriptSession::ReloadApplication` はコードと環境を同時更新し、失敗時に戻します。Session の所有スレッドと寿命契約に従ってください。停止中のネイティブコールバックは強制中断できません。SDK は `find_package(VeroScriptApplication 0.1.1 EXACT CONFIG REQUIRED)` を使います。

署名済みアプリを再構成する場合、新しい内容の署名ポリシーを明示してください。

GUI アプリケーションも同じ `.vsapp` コンテナーを使います。`gui-compile` が GUI イメージとリソースを用意し、GUI の検査／プレビューと通常の CLI の inspect／検査／実行が同じファイルを読みます。実行には GUI 対応ホストと手動選択した互換プロバイダーが必要です。

## ホスト設定と署名

`--application-options /absolute/host-options.json` でホスト設定を選択します。C ABI は `vs_interop_application_services_create`、`vs_interop_application_read`、`vs_interop_session_load_application` と対応する破棄関数を提供します。`cacheRoot`、`stdlibSearchPaths`、`cryptoLibrary`、`keyring` のパスは絶対パスです。信頼設定は `requireSignature`、`minimumSignatures`、`requiredKeys`、`policyIdentity` で、読み込むアプリではなく利用側が与えます。パッケージのバイト API は通常のアプリ ZIP も受け付けます。

XChaCha20-Poly1305 IETF はコンパイル済みイメージ、ライブラリー、明示的に選択したリソースを暗号化し、外側 ZIP とメタデータは可読のままです。暗号化したソースインポートやインストール ZIP には対応しません。libsodium アダプターには絶対パスのネイティブライブラリーを与えます。Ed25519 配列は 1～16 レコードで、種別、正確な長さ、SHA-256、鍵 ID／バージョンを結び付けます。内部署名は `veroscript-app.files.json.sig.json`、成果物署名は `<filename>.sig.json` です。署名を除去しても必須方針は解除されません。同じ鍵の別名は独立署名者に数えず、未知／非信頼鍵は閾値を満たしません。

委譲はネイティブコールバックやメモリー上の秘密を渡しません。このサービス自体は GUI、モバイルのライフサイクル、ストア承認を提供しません。コード置換前に外部のスクリプト値ルートを解放し、ネイティブカタログ変更時は `LoadApplication` と新しいアプリ寿命を使用します。SDK は `VeroScript::VeroScriptApplication` にリンクします。ランタイム記述子は Core、OS、CPU、環境、ABI を識別し、パッケージインストールの記述子とは独立です。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

## 独立 Runtime の実行バックエンドオプション

ホストオプションはアプリケーションパスの前、アプリケーション引数は `--` の後に置きます。`--vm-backend` と `--jit-backend` は実行サービスを選び、platform/graphics/GUI/image/font/text-layout の提供者とは独立です。`auto`、`none`、対応するファイル／ディレクトリ、または環境にリゾルバーがある場合はインストール済み ID を受け取ります。自動 VM は同梱の製品代替を使えますが、明示的な無効化は上書きしません。以下は入口を呼ばずに検査／準備し、その後 JIT 無効で実行するコマンドです。GUI コンテナーには対応する GUI 有効ホストと提供者も必要です。

```sh
veroscript-runtime --check --vm-backend auto --jit-backend none /absolute/application.vsapp
veroscript-runtime --vm-backend auto --jit-backend none /absolute/application.vsapp -- argument1
```

アプリケーション内容とキャッシュには実体のあるファイルシステムパスを使います。macOS では root 所有のシステム別名 `/tmp`、`/var`、`/etc` が、それぞれ `/private/tmp`、`/private/var`、`/private/etc` を指す場合だけ受け付けます。他のシンボリックリンクや特殊エントリーは拒否され、内容やキャッシュ内のリンクを許可するものではありません。

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
