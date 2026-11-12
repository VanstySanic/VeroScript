# ネットワーク: ソースの所属

Network は不透明なコンテキスト、操作、割当制限と共通ストリーム／ファイル契約、Http/WebSocket はプロトコルの窓口とバインディングを所有します。Boost は Asio/Beast、OpenSSL 統合と BoostRuntime、Curl は HTTP クライアントと curl ソースを所有します。サービス／基盤の識別情報、作成元イメージの寿命、公開オブジェクトの制御ブロックをアンロードまで維持します。

手動登録するホストは FBackendCatalog::Binding() をフロントエンド登録だけに渡し、全プロバイダー登録後に Update を呼びます。プロバイダーはこのカタログを保持してはいけません。パッケージ管理がカタログ構成とサービス選択を行います。契約、基盤、対応する Core の識別情報が変わった場合は関係するイメージを再構築します。

`Public` · `Internal` （版管理したプロバイダー拡張契約） · `Private` · `Resources/Docs` · `Resources/Examples` · `Resources/Localization` · `Resources/Tests` · `Resources/Tools/CMake`

`VeroScriptNetwork/Internal` → `VeroScript::NetworkProvider` （不透明な context/operation、割当制限、ストリームとアップグレードの契約）。

`Modules/Network/Backend/VeroScriptBoost/Private/Runtime` → `VeroScriptBoostRuntime`; `Modules/Network/Backend/VeroScriptCurl/Private` → HTTP クライアント。共通のフロントエンド／プロバイダー用ビルドヘルパーは `VeroScriptNetwork/Resources/Tools/CMake` に所属します。Network、Http、WebSocket のパッケージ構成テンプレートは各モジュールの `Resources/Tools/CMake` に置き、カテゴリのルートは CMake ターゲットの構成だけを担当します。

SDK は Public と明示的に版管理した不透明な契約を include/VeroScript*/Backend に導入します。具体的な context/operation 構造は非公開です。他モジュールの Private を参照しないでください。公開前に対応 Core で provider とバインディングを再構築し、各プラットフォームの実測結果で検証します。

## ビルドとネイティブモジュール登録

ルートビルドでは `VEROSCRIPT_BUILD_NETWORK`、`VEROSCRIPT_BUILD_HTTP`、`VEROSCRIPT_BUILD_WEBSOCKET` の既定値は OFF です。ネットワークのいずれかの選択が有効になると Boost の既定値は ON となり、Curl は HTTP の選択に従います。プロバイダーは `VEROSCRIPT_BUILD_BOOST` と `VEROSCRIPT_BUILD_CURL` で選択します。`Modules/Network` の単独ビルドでは Boost、HTTP、WebSocket が既定で有効です。`VEROSCRIPT_NETWORK_BUILD_BINDINGS` は Core ターゲットが既に存在する場合のみ既定で ON です。それ以外では完全に一致するインストール済み Core SDK とともに明示的に有効にします。Curl の `VEROSCRIPT_CURL_USE_SYSTEM_LIBRARY` は既定で OFF で、有効時は curl 8.13 以降が必要です。同梱の静的ライブラリは PIC を使用し、Linux の同梱構成には OpenSSL 開発ファイルが必要です。

Network をプロトコルより先に登録し、参照可能な依存関係を宣言します。静的ホストは各モジュールの `QueryModule` / `GetImplementationIdentity`、動的ローダーは `veroscript_extension_query` を使用します。静的 `ContentIdentity` は実装を識別します。プロバイダーに別の Core を組み込まないでください。BoostRuntime とフロントエンド基盤は各イメージ間で一致させ、非公開シンボルやバックエンドのビルド設定をフロントエンド SDK に公開しません。

<a id="network-execution-assembly"></a>

Core は実行契約を提供し、公式バイトコード VM は独立したバックエンドです。バイトコード実行を選ぶホストは `LoadVmBackend` / `SetVmBackendFactory` で互換性のあるファクトリを明示的に設定し、コンパイル中にスクリプト拡張を実行する場合は `FCompileOptions::ScriptCompileExtensions.VmBackendFactory` にも設定します。単独ビルドのバインディングテストには `-DVEROSCRIPT_TEST_VM_LIBRARY=<VM ライブラリの絶対パス>` が必要です。完全な Core SDK は各プラットフォームのライブラリを `bin/Vm` に含みます。`BUILD_TESTING=OFF` ではテストなしでバインディングをビルドでき、データ層のみのビルドに VM は不要です。

バックエンドなしの SDK、機能の有効／無効、欠落・不一致、明示選択、動的／静的な寿命、保持された結果、移設した SDK、厳密な依存関係を個別に検証します。実行が認められた場合にネイティブプロトコルと各プラットフォームを検証してください。文書の静的確認は実行時の認定にはなりません。

<a id="cross-image-results"></a>

操作結果は生成側イメージの `CreateTypedResult`／`OwnResult` で取り出し、`FOwnedResult`、コンテキスト、ペイロードの所有権を保持します。`FOperation::Result<T>()` は型名・サイズ・アラインメントを照合し、イメージ間の `std::any_cast` の RTTI アドレス一致に依存しません。一致する C++ ABI／foundation ビルドは依然必要で、移植可能な C ABI ではありません。ヘッダー／結果格納変更後は利用側と提供側を再ビルドします。終端時のコールバックを一度だけ解放する swap を維持してください。

POSIX の非置換アトミック出力は利用可能なら `renameat2(RENAME_NOREPLACE)` を使い、未対応のシステムコール／ファイルシステムの場合だけハードリンクへ切り替えます。明示的エラーと一時ファイルの後処理を維持します。

<a id="provider-facade-ownership"></a>

提供者のファサードラップには所有元の不透明コンテキストを渡し、作成イメージの寿命アンカーを保持します。HTTP フロントエンドが型付き交換ファクトリを渡し、`shared_from_this()` と HTTP upgrade の所有権を同じ制御ブロックに保ちます。提供者側で確保した制御ブロックにコンテキストをメンバー保持するだけでは安全にアンロードできません。返却オブジェクトと保持結果は共通のファクトリ／所有権ヘルパーを使います。カタログは環境との循環を防ぐ弱参照でファクトリをキャッシュし、提供者登録からカタログを保持しません。

## ローカライズ資材のステージング

ソース側の補助ファイル `Resources/Tools/CMake/VeroScriptLocalization.cmake` は、`network`、`http`、`websocket`、`network-all` と独立した `json` プロファイルを選択します。HTTP/WebSocket のプロファイルには Network を含み、`network-all` はネットワーク 3 モジュールを統合します。これらはビルド／配布用の入力であり、実行時 SDK API ではありません。ステージングは出力先のプロファイルを置換して不要な項目を削除するため、プロファイルを順次重ねず、必要な全リソースルートを先に選択してください。利用者向けの登録方法は User の診断リファレンスで説明します。

<!-- docs-navigation -->

---

[User](../../User/ja-JP/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
