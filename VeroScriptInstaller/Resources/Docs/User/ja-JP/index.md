# VeroScript のインストールと利用

OS とアーキテクチャに合うパッケージを選択してください。Runtime はスクリプト実行用で、CLI、LSP、DAP を含みます。SDK はホストアプリケーションへの組み込み用です。インストーラーは選択したコンポーネントを配置します。ポータブルアーカイブは任意のディレクトリへ展開できます。

## 製品を選ぶ

| 製品 | 用途 |
|---|---|
| Runtime | スクリプト実行、CLI/LSP/DAP、標準ライブラリ |
| Core SDK | コアと Tooling の C++ ヘッダー、ライブラリ、CMake パッケージ |
| Interop SDK | ネイティブや他言語ホスト向け C ABI |
| C++ SDK | C++ アダプター。同じビルドの Core SDK が必要 |
| C# SDK | .NET アダプターと対応するネイティブ Interop ランタイム |
| Python SDK | CPython アダプターと Python ツール |
| WebAssembly SDK | JavaScript ホスト向け WebAssembly ランタイムと TypeScript API |

Runtime は SDK をインストールしません。インストーラーではパッケージに含まれるツール、AOT、シンボルを選択できます。文書とサンプルは Resources から別途取得してください。SDK の既定の保存先はバージョン別で、並行インストールできます。製品間のバージョンは異なる場合があるため、各 SDK の依存関係と ABI 要件を確認してください。

`Release/<business-route>/<target>/<version>/` では、デスクトップ用インストーラーは `installers`、ポータブル Runtime やモジュールのアーカイブは `archives`、開発 SDK は `sdks`、エコシステムのパッケージやエディター拡張は `packages`、デバッグシンボルは `symbols` から取得します。共通配布は `any`、ポータブル WebAssembly SDK は `wasm32` を使います。任意の Modules は個別に取得し、独立したインストーラーは提供しません。

## インストール、更新、移設

Windows では `.exe` インストーラーを起動します。Linux では `.tar.gz` を展開して内部のプログラムを実行し、macOS では `.dmg` を開いて内部の `.app` を起動します。ウィザードで機能コンポーネント、インストール先、表示された場合はファイル関連付け、内容の確認の順に進んでインストールします。「次へ」と「戻る」でページを移動できます。コンポーネントが多い場合はページを分け、ウィンドウ全体のスクロールは不要です。文書とサンプルのインストール項目はありません。インストーラーは自己完結しており、対象マシンに別の .NET Runtime は不要です。Runtime の `PATH` 追加は既定で無効です。有効化後は新しい端末を開いてください。

インストーラーは現在のコンポーネント配置先ディレクトリだけで、同じ製品の有効な記録を探します。一致するインストールが見つかった場合に更新モードへ入り、同じ製品バージョンの再パッケージにも対応します。更新はユーザーが作成したファイルを保持します。新しいパッケージのファイルとユーザーファイルが競合する場合は失敗するため、競合ファイルを移動してから再試行してください。インストール済みの任意コンポーネントを選択解除すると管理対象ファイルだけを削除し、ユーザーファイルは保持します。PATH の選択解除では、その記録が所有する項目だけを削除します。

既存のインストールを検出した場合、記録から初期値を設定するのは、まだ手動で変更していないコンポーネント、PATH、対応するファイル関連付けの項目だけです。更新とアンインストールの確認画面では、所有する PATH 項目を削除する場合にその旨を表示します。

インストール先ルートはウィザードで選択した親ディレクトリであり、`SDK/Core/<version>` のようなコンポーネントディレクトリではありません。組み込み製品のコンポーネント配置先にはバージョンが含まれるため、同じルートでも異なる製品バージョンを並行してインストールできます。他のバージョンの自動検索、移行、アンインストールは行いません。旧版を置き換えるには、新版をインストールして動作を確認し、旧版の元のインストーラーと記録で旧版をアンインストールします。ルートを変更する場合は、新しいルートにインストールして自分のファイルをコピーし、元の記録で旧インストールを削除してください。管理対象のコンポーネントディレクトリを直接移動しないでください。

## Windows のネイティブランタイムファイル

Windows パッケージでは、対応する Visual C++ ランタイム DLL をネイティブプログラムやライブラリの隣に同梱します。アーカイブ全体を展開するかインストーラーを使用し、配置時にはこれらの DLL と `licenses/MicrosoftVisualCpp/NOTICE.txt` を対応するプログラムやネイティブライブラリと一緒に保持してください。ネイティブプログラムの実行ファイル、Interop DLL、Python 拡張だけをコピーすると必要な依存ファイルが不足する場合があります。Universal CRT は引き続き OS 側で必要です。

## 修復とアンインストール

各コンポーネントルートには `.veroscript-install.json` があります。修復には元の製品パッケージが必要で、バージョン、プラットフォーム、アーキテクチャ、マニフェスト指紋が記録と完全一致しなければなりません。元のインストーラーを保管してください。アンインストールは埋め込み payload を再読込せず、修復とアンインストールは同時に指定できません。

以下の操作を一つ選択してください。インストーラーと記録のパスは実際のものに置き換えます。各コマンドは対応するウィンドウを開き、確認後に実行します。

修復のみ:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --repair-record $record
```

アンインストールのみ:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --uninstall-record $record
```

アンインストールは管理対象ファイルと、このインストールが現在も所有する PATH・関連付け・アイコンの値だけを削除し、ユーザーファイルのあるディレクトリを残します。中断後は記録の読込、修復、アンインストールが最初にトランザクションを回復します。`.veroscript-transaction-*.json`、staging、backup ディレクトリを手動削除して回復を回避しないでください。

## CMake から SDK を使う

展開した Core SDK ルートを `CMAKE_PREFIX_PATH` に追加します。

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScript)
```

Core SDK は `VeroScript::VeroScript` と `VeroScript::VeroScriptTooling` を提供し、インポートターゲットが公開ヘッダーのコンパイル要件を伝播します。JIT バックエンドは別モジュールであり、Core は `VeroScript::asmjit` をエクスポートしません。C++/AOT ライブラリは別の C++ 製品です。インストーラー版では `<install-root>/SDK/Core/<version>` を `CMAKE_PREFIX_PATH` に追加します。`<install-root>` はウィザードで選択したインストール先ルートです。サンプルは別途取得してください。現在のポータブル SDK とインストーラーには含まれません。

 C++ SDK は一致する Core SDK と `Runtime` ディレクトリを必要とします。

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScriptCpp)
```

`VeroScriptCppConfig.cmake` は C++ パッケージのビルド時に記録された Core SDK のバージョンを完全一致で要求し、Core ライブラリの SHA-256 ビルド識別子を検証します。Core と C++ 製品のバージョンは異なる場合があります。C++ の配布マニフェストに記録された Core 依存関係を使ってください。バージョン番号の一致だけではビルド互換性を保証できません。ポータブルパッケージでは `<cpp-package>/Runtime`、既定のインストール配置では `<install-root>/SDK/Cpp/<version>/Runtime` を追加します。対応コンポーネントをインストールした場合は `COMPONENTS Tools Aot` を要求できます。

Core SDK のツールは `bin` を基準に `assets/stdlib` と `assets/localization` を探します。Windows の Core SDK ルートから、別途入手した Resources 配布物の例を実行できます。絶対パスを実際の配置先に置き換えてください。

```powershell
.\bin\veroscript.exe --bc-cache=off 'D:\Packages\Resources\Scripts\examples\standard_library_algorithms.vs'
```

## オフライン文書とサンプル

対応する Resources 配布物を別途取得してください。`Resources/Docs/README.md` を開いて言語を選び、相対リンクからリファレンスとチュートリアルを読みます。単独の文書 ZIP では展開したパッケージ内の `docs/README.md` を使用します。Resources のスクリプト例は `Resources/Scripts/examples` にあります。現在の SDK アーカイブとインストーラーには文書・サンプルコンポーネントは含まれません。オンラインソース参照にはネットワークが必要で、例には対応する SDK と依存ライブラリが必要な場合があります。

## インストーラーの言語

インストーラーは英語、フランス語、日本語、韓国語、簡体字中国語、繁体字中国語を提供します。ウィンドウで選択するか、起動時に `--language` を指定してください。初期値はシステムの UI 言語に従い、一致する言語がない場合は英語になります。この設定はインストーラーの表示用です。スクリプト診断には CLI の `--lang` または SDK の言語 API を別途使います。次の Windows 例では保存先とバージョンをダウンロードしたファイルに合わせてください。

```powershell
& 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe' --language ja-JP
```

## Linux/macOS でのインストール

Linux では対応する `linux-x64` または `linux-arm64` の `.tar.gz` インストーラーを展開し、内部のプログラムを実行します。macOS では対応する `macos-x64` または `macos-arm64` の `.dmg` を開き、内部のインストーラー `.app` を起動します。グラフィカルデスクトップが必要です。修復とアンインストールは Windows と同じ記録オプションを受け取ります。以下は Linux の例です。macOS では `installer` を元のインストーラーの `<installer>.app/Contents/MacOS/VeroScriptInstaller` に設定し、読み取り専用イメージ上のアプリには `chmod` を実行しません。例のパスは実際のものに置き換えてください。ポータブル Runtime/SDK アーカイブの展開時も実行権限を保持してください。

以下の操作を一つ選択してください。インストーラーと記録のパスは実際のものに置き換えます。各コマンドは対応するウィンドウを開き、確認後に実行します。

修復のみ:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --repair-record "$record"
```

アンインストールのみ:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --uninstall-record "$record"
```

## ライセンス通知

プロジェクトの [MIT ライセンス全文](../../../../../LICENSE)は、この文書に含まれています。依存ライブラリの通知は、ダウンロードしたインストーラーから確認できます。各通知オプションは単独で実行してください。原文を出力し、インストール画面を開かずに終了します。例のパスは実際のファイルに置き換えてください。通知は原文の言語を保持し、`--language` はインストール画面の言語を別途選択します。

この例は Windows PowerShell 5.1 で通知の文字を正しく表示するため、一時的に UTF-8 を使用し、終了後に元の端末エンコードを復元します。

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$noticeEncoding = [Console]::OutputEncoding
try {
	[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
	& $installer --license | Out-Host
	& $installer --third-party-licenses | Out-Host
}
finally
{
	[Console]::OutputEncoding = $noticeEncoding
}
```

<a id="file-associations"></a>

## Windows のファイル関連付け

Windows インストーラーに Runtime と対応アイコンが含まれる場合、Runtime を選ぶと任意の関連付けを設定できます。新規インストールでは無効です。`.vs` や `.vsapp` をその Runtime で実行するよう登録するか、`.vspkg` のアイコンだけを選びます。関連付けた `.vs` や `.vsapp` を開くとファイルを実行します。`.vspkg` の設定はパッケージの実行やインストールを行いません。

登録は現在のユーザーに実行ハンドラーとアイコンを追加します。既定のアプリは Windows の設定で自分で選択してください。登録だけでは既定のアプリは変更されません。設定画面を開く任意の操作はインストール成功後にそのページを要求し、開けなかった場合や登録が不完全な場合は結果画面に表示します。Windows は選択済みの既定アプリのアイコンを表示する場合があります。

更新時は、変更していない項目に記録済みの設定を使います。選択解除、修復、ロールバック、アンインストールでは他の登録や後からの変更を保持し、このインストールが所有する未変更の値だけを削除・復元できます。他のプラットフォームや必要な Runtime ファイルを含まないパッケージでは選択できません。

`.vspkg` のアイコンは既存の登録がない場合のみ追加します。既定のアプリの項目を解除しても、今回の設定画面の要求を取り消すだけで、Windows で確定した選択はリセットしません。アンインストールは保護された既定アプリの記録を変更せず、以前のアプリへの自動復元も保証しません。関連付けの所有情報がない古い記録では、関連付けを削除しません。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
