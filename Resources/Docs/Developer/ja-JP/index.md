# 文書の保守

[GitHub のソースコード](https://github.com/VanstySanic/VeroScript)

<a id="documentation-ownership"></a>

## 正本と管理範囲

| 内容 | 保守する正本 |
| --- | --- |
| 共通エディタ統合 | [エディタ統合の保守](editors.md) |
| 共通の読む順序、モジュール間の関係、チュートリアル、本プロジェクトの開発・貢献規約 | ルートの `Resources/Docs` |
| モジュールの動作、公開 API、既定値、制限、および構造、ビルド、保守 | 所有モジュールの `Resources/Docs/User/<locale>` と `Developer/<locale>` |
| マルチメディアのプロバイダーに共通する保守 | [マルチメディアの保守](multimedia.md) |
| 出力された All/User ツリーと ZIP | 生成物。参照先の正本を編集して再公開する |

各テーマ・言語につき一つの正本を維持します。入口は要約とリンクにとどめ、完全な契約・手順・変化する既定値は所有先へ置きます。6 言語の並行正本を同時に更新します。共有手順はここ、プロトコルや SDK の詳細は所属モジュールへ置き、旧版はバージョン管理で保持します。

`documentation.json` は集合全体の言語、モジュールマニフェスト、関連リソースを登録します。各モジュールの `manifest.json` は User 正本を安定した公開パスに対応付け、Developer ページはそのモジュールに属します。各ソースは All 集合内で一度だけ公開されます。User パッケージは同じ User/Tutorials 内容を抽出します。公開コピーは別の保守対象正本ではありません。

## ローカライズとパス

Overview を含め、6 言語のファイル名と意味を一致させます。API 名、診断 ID、JSON キー、コマンド、実行可能コードは保ちます。過去の検証結果は記録されたソース・成果物・環境だけに適用されます。

マニフェストは User 正本を安定した出版パスへ対応付けます。含まれる文書・資源間は相対 URL にし、言語、アンカー、符号化、クエリ、画像、参照リンクを保ち、コードと HTML コメントは変更しません。ソース専用参照はオンラインリポジトリへ移動します。旧文書はバージョン管理から参照し、出版にはコピーしません。

保守する文書とローカライズファイルの形式は、[共通の改行規則](../../Standards/ja-JP/GENERAL.md)に従います。このリポジトリの規則は、言語やツールが CRLF 入力を受け付ける動作を変えません。出版時は正規化したコピーを生成し、保守する正本やバイナリ資源は書き換えません。

## 検証と公開

以下のコマンドはリポジトリルートで実行します。公開のたびに現在の原本から再構築するため、ソースページ編集後の同期は不要です。管理する原本のみをコミットします。生成中に原本が変更された場合は公開を拒否し、成功時も失敗時も一時ディレクトリを清掃します。CI は言語間のファイル対応、一覧、公開バイト、リンクを検証します。公開する Markdown/JSON テキストには LF を使用し、ライセンス通知の原文、バイナリ素材、原本のバイトは保持します。

```powershell
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
python Resources/Tools/Packaging/veroscript_docs.py publish --audience All --output .tmp/docs-preview
python Resources/Tools/Packaging/veroscript_docs.py verify-publication --audience All --output .tmp/docs-preview
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -OutRoot .tmp/docs-collection-package
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -Audience User -OutRoot .tmp/docs-user-package
```

`build` と `publish` の出力先は、リポジトリの `.tmp` または `dist` 内の専用の空サブディレクトリにします。ソースの `Release` や既存の管理対象集合への直接出力は拒否され、Resources はトランザクションを使う製品公開処理で配布します。プレビューには毎回新しいディレクトリを使います。独立した ZIP を繰り返しエクスポートする場合は `package-veroscript-docs.ps1` を使い、新しいパッケージの準備と検証後に旧成果物を置き換えます。

<a id="integrated-publication-views"></a>

`package-veroscript-docs.ps1` の既定 All は現行 Overview、User、Developer、Standards、完全な Tutorials とリンク資源を含みます。`-Audience User` は参考を `docs/<locale>/...`、完全な教程を `docs/Tutorials/<locale>/...` に依存資源とともに出版します。どちらも History を含みません。

このスクリプトは独立した文書をエクスポートし、管理対象の製品集合には公開しません。既定の出力先は `dist/docs/<Audience>/<version>/`、`-SourceRoot` を使う場合は `dist/docs/Custom/<version>/` です。別の専用出力ディレクトリは `-OutRoot` で指定します。完全な Resources 配布は製品公開処理で User ビューを組み立てます。

完全リリースは Resources を通じて User ビューを `Release/Resources/any/<version>/Resources/Docs/` に公開し、`Resources/Scripts` と `Resources/Assets` を並べます。現在の Runtime/SDK/拡張アーカイブとインストーラーには文書とサンプルを含めません。ペイロード方針は Installer Developer の [release-layout.md](../../../../VeroScriptInstaller/Resources/Docs/Developer/ja-JP/release-layout.md) を参照してください。

[本プロジェクトの開発・貢献規約](../../Standards/ja-JP/README.md)は VeroScript リポジトリの開発と貢献にのみ適用し、SDK の利用要件や MIT ライセンスの追加条件ではありません。`documentation.json` の `standardsAudience` は `Developer` に固定します。Standards は開発者向け資料として `All` にのみ含め、`User`、Resources 内の利用者向け文書、Runtime/SDK/拡張の配布物、インストーラーには含めません。ソースの著作権表示とライセンス表示は保持します。

本プロジェクトの開発・貢献規約は `Resources/Docs/Standards/<locale>` で保守します。`zh-CN` が規則を定義し、他の五言語は同等に保ちます。出版は正本を直接読みます。この独立リポジトリには IDE の複製はありません。`sync-standards` は文書カタログから ValenceStudio の利用先が見つかった場合だけ管理対象の参照を更新し、それ以外では複製を作成しません。

Python 3.11+ が必要です。`check` はソースと検出された規約の利用先、`check-collection` は一時的な全体集合を検証します。`build/verify` は User 抽出のプレビュー、`publish/verify-publication` は現在の原本から All または User を公開して検証します。`-SourceRoot` は明示したツリーを出力し、`-Audience` と併用できません。欠落・範囲外リンク、欠落アンカー、空のツリー、ファイルシステムリンク、特殊項目を拒否し、ソースと出力は重複できません。ZIP は隠しリソースと空ディレクトリを含み、置換失敗時は旧成果物を復元します。復元も失敗した場合は復旧用バックアップを保持します。オンラインソース参照には接続が必要で、HTML レンダラーは `.md` リンクを変換して見出し ID を維持する必要があります。製品のビルド、配布内容の検査、実行時の受入結果の記録は[公開の検証手順](../../../../VeroScriptInstaller/Resources/Docs/Developer/ja-JP/release-validation.md)に従ってください。

## モジュールのマニフェスト

上位の `Resources/Docs/documentation.json` では、接頭辞 `$module/` が所有元のソースモジュールのルートを表します。`VeroScript` などの製品ディレクトリ名は変更しません。各モジュールのマニフェストは引き続き自身の Docs ディレクトリからの相対パスを使用します。外側のディレクトリを移動または改名した場合は、原本文書のコマンドとリポジトリのワークフローの実際のパスも更新します。Windows PowerShell 5.1 で使うスクリプトに非 ASCII 文字が含まれる場合は UTF-8 BOM を保持します。

```json
{
  "schemaVersion": 1,
  "module": "VeroScriptCli",
  "user": [
    {
      "source": "User/{locale}/index.md",
      "output": "{locale}/toolchain/cli.md"
    },
    {
      "source": "User/{locale}/packages.md",
      "output": "{locale}/toolchain/packages.md"
    }
  ],
  "developer": true
}
```

## 文書の目的

User は操作・公開 API・設定・制限、Developer は構造・実装責任・ビルド・検証・出版、Tutorials は前提・手順・期待結果、Standards は本プロジェクトの開発・貢献に関する承認済み規則だけを扱います。過去の計画と検証証拠はバージョン管理または一時出力に保管し、変更後のワークスペースの合格とはしません。

本文、タイトル、ファイル名には暦日や不要な個人識別子を入れません。一般的な説明では、現在のリリース番号を繰り返さず動作を説明します。互換性、ツール要件、ABI・形式契約、実行用の例、成果物の選択、元のライセンス表示に必要な正確なバージョンは保持します。変わる識別子を転記せず、管理元のメタデータへリンクします。日付付きログと検証記録は無視される一時出力へ置きます。User にテスト方法や内部実装を繰り返しません。

## リンクと読者の区分

User の相対リンクは User ビュー、Tutorials、登録済みの補助リソース内で解決する必要があります。User から Developer または Standards への相対リンクは All と User の両方の公開を失敗させます。サーバー実装、保守、検証手順は Developer に、利用者が観測できる動作は User に記載します。Developer から対応する User の契約へはリンクできます。モジュール間リンクの変更後はソース文書集と両方の公開パッケージを検証してください。

## モジュールリファレンス

| 分野 | リファレンス |
| --- | --- |
| 言語とツール | [VeroScript](../../../../VeroScript/Resources/Docs/Developer/ja-JP/index.md) · [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/ja-JP/index.md) · [Tooling](../../../../VeroScriptTooling/Resources/Docs/Developer/ja-JP/index.md) |
| ホスト SDK | [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/ja-JP/index.md) · [C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ja-JP/index.md) · [Python](../../../../Extensions/Language/Python/Resources/Docs/Developer/ja-JP/index.md) · [C#](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/ja-JP/index.md) · [WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/ja-JP/index.md) |
| 実行エンジン | [Bytecode VM](../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/Developer/ja-JP/index.md) · [AsmJit](../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/ja-JP/index.md) |
| パッケージとアプリケーション | [PackageManager](../../../../VeroScriptPackageManager/Resources/Docs/Developer/ja-JP/index.md) · [Application](../../../../VeroScriptApplication/Resources/Docs/Developer/ja-JP/index.md) · [Build](../../../../VeroScriptBuild/Resources/Docs/Developer/ja-JP/index.md) · [Installer](../../../../VeroScriptInstaller/Resources/Docs/Developer/ja-JP/index.md) |
| エディタ | [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/ja-JP/index.md) · [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/ja-JP/index.md) · [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/ja-JP/index.md) · [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/ja-JP/index.md) |
| 組み込みプラットフォーム | [Mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/ja-JP/index.md) · [Android](../../../../Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Docs/Developer/ja-JP/index.md) · [iOS](../../../../Extensions/Platform/Mobile/VeroScriptIOS/Resources/Docs/Developer/ja-JP/index.md) |
| データとネットワーク | [JSON](../../../../Modules/Network/VeroScriptJson/Resources/Docs/Developer/ja-JP/index.md) · [Network](../../../../Modules/Network/VeroScriptNetwork/Resources/Docs/Developer/ja-JP/index.md) · [HTTP](../../../../Modules/Network/VeroScriptHttp/Resources/Docs/Developer/ja-JP/index.md) · [WebSocket](../../../../Modules/Network/VeroScriptWebSocket/Resources/Docs/Developer/ja-JP/index.md) · [Boost](../../../../Modules/Network/Backend/VeroScriptBoost/Resources/Docs/Developer/ja-JP/index.md) · [Curl](../../../../Modules/Network/Backend/VeroScriptCurl/Resources/Docs/Developer/ja-JP/index.md) |
| マルチメディアと GUI | [Platform](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/ja-JP/index.md) · [Graphics](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/ja-JP/index.md) · [Audio](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/ja-JP/index.md) · [Image](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/ja-JP/index.md) · [Font](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/ja-JP/index.md) · [Gui](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/ja-JP/index.md) · [Sdl3](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/ja-JP/index.md) · [Stb](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/ja-JP/index.md) · [FontBackend](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/ja-JP/index.md) · [RmlUi](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/ja-JP/index.md) |

同じリポジトリの現行ガイド間は相対リンクを使います。オンライン URL で読者境界を回避したり、含まれる文書をネットワーク依存にしてはいけません。ソースコード参照は既存の動作を保ちます。

<a id="current-publication-policy"></a>

All と User は現行文書だけを出版します。History ディレクトリ、古い計画、検証スナップショットは出版入力にせず、旧版はバージョン管理、厳密な検証証拠は無視対象の一時出力に保存します。History パスを含むソースやカスタム SourceRoot は大文字・小文字を問わず拒否します。選択した対象読者に応じて Overview、User、Developer、Standards、完全な Tutorials と依存資源を出版します。

<a id="module-entrypoints"></a>

モジュール直下の README は目的、関連モジュール、文書とライセンスへの入口を示します。Resources/Docs/README は集成出版で使う六言語・読者別の選択ページです。詳細な利用方法は User、実装・ビルド・検証は Developer、手順付きの学習は Tutorials に置きます。言語ツールの子モジュールはグループの正本へリンクします。入口は簡潔に保ち、ファイル移動時は相対リンクを更新し、別の完全なマニュアルや検証記録を README で保守しません。

<!-- docs-navigation -->

---

[開発者文書](index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
