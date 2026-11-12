# インストーラーのビルドとバンドル契約

## ビルド対象

App と Tool は .NET 9 を対象とし、`VeroScriptInstaller.Core` を使用します。App は言語カタログをリソースとして埋め込み、バンドルは単一実行ファイル内の自己展開コンテンツとして保持します。Tool は GUI なしでバンドルアーカイブを作成・検証します。

`VeroScriptInstaller/global.json` は .NET SDK `9.0.100` と `latestFeature` ロールフォワードを選択します。例はリポジトリのルートから開始し、`Push-Location` により Installer ディレクトリで SDK を解決します。.NET 10 モバイルワークロードがあっても .NET 9 SDK を保持してください。C# モバイルのツールチェーンは別です。

```powershell
Push-Location VeroScriptInstaller
try
{
	dotnet build App/VeroScriptInstaller.csproj -c Release -warnaserror
	dotnet build Tool/VeroScriptInstaller.Tool.csproj -c Release -warnaserror
	dotnet run --project Tests/VeroScriptInstaller.Tests.csproj -c Release
}
finally
{
	Pop-Location
}
```

Tests は回帰テスト用の実行ファイルです。`dotnet run` を使い、`dotnet test` による検出を前提にしないでください。App のビルドだけでは配布用ペイロードは入りません。配布スクリプトを使用します。

## Windows の完全配布

完全な処理は Release の `win-x64` と `win-arm64` を対象とします。CMake、MSVC、全製品用の .NET 8/9 SDK、Node.js/npm、WebAssembly を再ビルドする Emscripten が必要です。`Extensions/Platform/WebAssembly/VeroScriptWebAssembly` で `npm ci` を実行してください。ブラウザーテストには Playwright Chromium などの利用可能なブラウザーも必要です。

```powershell
.\Resources\Tools\Packaging\package-veroscript-all.ps1 -ConfigurePreset windows-msvc-vs2026 -Runtime win-x64
```

Windows ARM64 では `-Runtime win-arm64` と、`-ConfigurePreset windows-msvc-vs2026-arm64` などの対応するプリセットを指定し、MSVC ARM64 ツールと Windows SDK をインストールしてください。完全公開では Python Wheel マトリックスを含むネイティブ実行検証を行うため、必要な ARM64 CPython インタープリター一式を備えたネイティブ Windows ARM64 ホストを使用します。x64 でネイティブ成果物をクロスコンパイルするだけでは、これらの検証は完了しません。`win-arm64` はネイティブ ARM64 を表し、ARM64EC やエミュレーション上の x64 は含みません。

Visual Studio に合うプリセットを選ぶか、省略して自動選択します。既定の出力先は `Release` です。`-SkipBuild` はネイティブと Web の既存出力を再利用しますが、マネージドアプリの公開と配置した WebAssembly SDK の検証は行います。既存出力のソースと構成が確認済みの場合にだけ使ってください。成果物は `Release/<business-route>/<target>/<version>/` に配置し、各版で必要な `archives/`、`sdks/`、`packages/`、`installers/`、`symbols/` を作成します。共有配布は `any`、WebAssembly SDK は `wasm32` を使用します。マニフェスト、チェックサム、索引による管理は維持します。製品バージョンは独立しています。Core/C++ 依存関係、ロック、バージョン整理、ロールバックは[配布規則](release-layout.md)を参照してください。

## Windows のネイティブランタイム依存関係

MSVC の Release パッケージでは、`vs_install_windows_runtime` を通じて選択したツールチェーンの再頒布可能 DLL をインストールします。`VeroScriptWindowsRuntime` コンポーネントは Runtime ルートに配置し、Interop、C++ ツール、Python はそれぞれのインストールコンポーネントを使います。パッケージの組み立てでは SDK とマネージドランタイム内のネイティブエントリーポイントの隣にも DLL を配置します。システムディレクトリからコピーせず、ツールチェーンの再頒布可能ファイルを使用してください。デバッグ CRT と Universal CRT は同梱しません。

配布検証ではネイティブアーキテクチャを確認し、各ネイティブエントリーポイントの隣に `msvcp140.dll`、`vcruntime140.dll`、x64 ではさらに `vcruntime140_1.dll` を要求します。ARM64 はネイティブ CRT を使用し、ARM64 再頒布可能コンポーネントに同梱された x64 用の `vcruntime140_1.dll` を除外します。同じディレクトリの `licenses/MicrosoftVisualCpp/NOTICE.txt` も必要です。元の `Resources/Tools/CMake/WindowsRuntimeNotice.txt` は独立した第三者通知として保持してください。プロジェクトの MIT ライセンスで置き換えることはできません。依存ファイルの不足は公開を失敗させ、MSVC 再頒布可能ファイルの不足は Windows Wheel の構成を失敗させます。Universal CRT は OS 側の前提条件です。

## Python 公開の前提条件

完全公開で Python を選択する場合、パイプラインの実行前に CPython 3.11–3.15 一式を準備してください。`-SkipBuild` / `--skip-build` でも Wheel とソース配布物のビルドと検証を行います。インタープリターの指定、事前検証、Wheel の各 ABI と SDK の単一ネイティブ ABI の違いは [Python マトリックスの契約](../../../../../Extensions/Language/Python/Resources/Docs/Developer/ja-JP/toolchain-and-packaging.md)を参照してください。

## バンドルの契約

マニフェストは製品 ID、プラットフォーム、アーキテクチャ、選択可能なコンポーネント、相対パス、ハッシュを定義します。インストールと修復は現在のソースではなくバンドルを使用します。展開したバンドルは `manifest.json` と `payload/` を含み、ポータブル Runtime/SDK アーカイブとは異なります。

```powershell
Push-Location VeroScriptInstaller
try
{
	$tool = 'Tool/VeroScriptInstaller.Tool.csproj'
	dotnet run --project $tool -c Release -- build-bundle --output ../.tmp/runtime-installer-bundle --version 0.1.1 --profile runtime --source-runtime D:/Packages/Runtime
	dotnet run --project $tool -c Release -- verify --bundle ../.tmp/runtime-installer-bundle
	dotnet run --project $tool -c Release -- pack-bundle --bundle ../.tmp/runtime-installer-bundle --output ../.tmp/runtime-installer-bundle.zip
}
finally
{
	Pop-Location
}
```

`D:/Packages/Runtime` は CLI/LSP/DAP、標準ライブラリ、ローカライズ資源を含む準備済みペイロードの例です。`--source-*` オプションはプロファイルに合わせてください。[製品定義](../../../Examples/Products/README.md)と[自動化](automation.md)に、Tool によるインストール、移設、修復、削除を説明しています。

## 単一ファイルのバンドル転送形式

`App/InstallerBundleContent.targets` は `InstallerBundleArchive` を最大 32 個、各 1 GiB 以下のコンテンツに分割し、`VeroScriptInstaller.EmbeddedBundle.zip.0000` から連番を付けます。単一ファイル公開は `IncludeAllContentForSelfExtract` でこれらを同梱し、別途ダウンロードするファイルにはしません。アセンブリメタデータは ZIP 全体の `VeroScriptInstaller.BundleSha256` と `VeroScriptInstaller.BundlePartCount` を記録します。

`EmbeddedInstallerBundle` は展開済み分割ファイルを開き、リンク、不正な個数・サイズを拒否し、ZIP 全体の SHA-256 を確認します。アーカイブ全体をメモリに保持したり別の ZIP にコピーしたりせず、シーク可能なストリームを提供します。バンドル展開先は Linux では `AppContext.BaseDirectory/VeroScriptInstaller.Unpack`、それ以外では `Path.GetTempPath()/VeroScriptInstaller` です。展開とマニフェストの検証も維持します。公開ターゲットは単一実行ファイルの欠落・空ファイルを拒否しますが、これだけで大規模インストーラーの対象上の検証が完了したとはしません。

通常、失敗時は所有する配布ステージを削除します。Windows の完全公開の `-KeepFailedStage`、Unix の完全公開の `--keep-failed-stage` は失敗したステージを診断用に残し、パスを表示します。成功時は削除します。残したファイルは候補であり、再利用前にソース、設定、ハッシュを再確認します。

## 検証と署名

組み込み・外部バンドルには同じマニフェスト、パス、ハッシュ、コンポーネント、プラットフォーム検証を適用します。アーカイブ配置、指紋、インストール記録スキーマを変える場合は、修復、移設、削除、配布のテストを一緒に更新してください。コンポーネントには現行 API とバイナリが必要です。Core ヘッダー、C++ セッションと CMake パッケージ、Python 拡張、型情報、対応する Interop DLL が対象です。最終検証はセッションキャッシュ API、C# の単独・NuGet アセンブリ、シンボルパッケージ、二種類の WebAssembly SDK と型宣言も確認します。

処理は生成したインストーラーで `--verify-localization` と `--verify-embedded-bundle` を実行し、署名後も再検証します。終了コードを確認してください。GUI の受入試験の代わりにはなりません。言語検証は六カタログ、八プロファイル、コンポーネントキー、空でない値、書式引数、部分的な外部上書き、中国語の文字体系、言語切り替えを確認します。組み込み翻訳の欠落を英語フォールバックで隠してはいけません。

ローカライズキーはマニフェスト指紋の一部です。バンドルとアプリを同時に再ビルドし、修復用に元のインストーラーを保存してください。同じバージョンでも指紋が異なる再ビルドは修復では拒否され、通常の更新でのみ使えます。

署名は任意です。必須にするには `VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT` と `VEROSCRIPT_TIMESTAMP_SERVER` を設定し、完全配布では `-RequireSignedInstallers`、単独製品では `-RequireSignature` を指定します。最終チェックサムは署名後に計算します。独立検証でも `-RequireSignedInstallers` を指定し、全インストーラーの Authenticode 状態を `Valid` にする必要があります。

## Linux/macOS の配布

完全公開と単独コンポーネントは[製品別の公開規則](release-layout.md)に従います。ネイティブ Runtime/SDK アーカイブは実行権限を保持する `.tar.gz`、WebAssembly は ZIP を使います。Linux インストーラーは `.tar.gz`、macOS インストーラーは標準の `.app` を含む `.dmg` を使います。インストーラーは自己完結しています。Linux は絶対パスの `XDG_DATA_HOME` を尊重します。Python ABI と Linux 互換性は記録したビルド環境に依存します。

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh --target linux-x64
python3 Resources/Tools/Packaging/verify-veroscript-release.py \
  --release-directory Release --target linux-x64
```

`target`: `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`.

`win-x86` を含むデスクトップの別ターゲット向け組み立てには、`Resources/Tools/Packaging/package-veroscript-desktop-cross.py --help` を参照し、実際のターゲットビルドと対応する依存関係を入力します。ビルド／アーカイブの検証とターゲットでの実行検証は別々に記録します。


## 文書パッケージと統合ビュー

[文書の保守 — 検証と公開](../../../../../Resources/Docs/Developer/ja-JP/index.md#integrated-publication-views) を参照してください。

完全リリースは Resources を通じて User ビューを `Resources/Docs/` に公開し、`Resources/Scripts` と `Resources/Assets` を並べます。現在の Runtime/SDK/拡張アーカイブとインストーラーには文書とサンプルを含めません。ペイロード方針は Installer Developer の `release-layout.md` を参照してください。

共通ガイド、Overview、Standards、Tutorials は Resources/Docs、詳細な正本は各モジュールの Resources/Docs に維持します。公開時に `.tmp/docs-collection-*` の一時集合を組み立てて検証し、終了後に削除します。ソース Docs に生成集合を残しません。

## 埋め込まれたライセンス通知

Core プロジェクトはリポジトリルートの `LICENSE` と `VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt` の全文を埋め込みます。App と Tool は `--license` または `--third-party-licenses` を単独の引数として受け取り、GUI やバンドルの初期化前に終了します。通知はローカライズカタログではなく原文のリソースであり、翻訳や省略は行いません。Windows と Unix の公開処理は、両コマンドの出力をソースの通知と比較してからインストーラーを公開します。依存関係を変更したら[通知の出典と保守記録](../../../Licenses/README.md)を更新し、実行ファイルを再ビルドして両方の検証を再実行してください。旧インストーラーには旧通知が残ります。

独自製品は Tool の --definition を使います。製品用 PowerShell は構成済み VeroScript 製品名を要求します。InstallerProductDefinition.ReleaseGroup（JSON releaseGroup）が Modules なら新規インストーラー/バンドル作成は拒否されます。製品定義ガイドの完全なペイロード例とモジュールアーカイブ方針を参照してください。

- [配布構成と公開規則](release-layout.md)

Windows の Python ペイロード契約には、パッケージのエントリーポイント、型スタブ、`py.typed` マーカー、対応する Interop DLL が必要です。ネイティブ拡張だけでは要件を満たしません。

<a id="publication-paths"></a>

`veroscript_business_release.guard_tree` はリンクされた公開ルートとエントリーを拒否します。macOS のみ、root 所有の `/tmp`、`/var`、`/etc` がそれぞれ正確に `/private/tmp`、`/private/var`、`/private/etc` を指す場合は祖先として許可します。通常の一時公開先を利用できますが、リンクされた出力ルートや公開コレクション内のリンクは許可しません。パス検証変更時も祖先とルートの区別を維持します。

## 単独インストーラーのダウンロード形式

Windows の Runtime インストーラーには `package-veroscript-installer.ps1`、対応する Linux/macOS ホストでは `package-veroscript-installer.sh` を使います。既定の管理対象出力では `Release/Runtime/<target>/<version>/archives/` の Runtime アーカイブを読みます。展開済みの入力は `-RuntimePackageDir` または `--runtime-package-dir` で指定できます。ダウンロード名は `VeroScript<business>Installer-<version>-<target>` に Windows の `.exe`、Linux の `.tar.gz`、macOS の `.dmg` を付けたものです。Linux は実行ファイルを含むアーカイブを 1 つ、macOS はアプリを含むディスクイメージを 1 つ公開します。内蔵ペイロードの確認は[ダウンロード検証](release-validation.md#installer-download-validation)を参照してください。

## Windows 実行ファイルのアイコン入力

Windows の `veroscript`、`veroscript-runtime`、`veroscript-launcher`、`veroscript-lsp`、`veroscript-dap` は、ソースの CMake ヘルパー `vs_use_product_icon` を通じて `Resources/Assets/icons/veroscript-icon.ico` を埋め込みます。CLI ターゲットの宣言では `PRODUCT_ICON` で有効にします。これらの実行ファイルには RC 言語と Windows リソースコンパイラーが必要です。ルートプロジェクトは利用可能なら RC を検出して有効にします。ソースのヘルパーを再利用する親プロジェクトは Windows 実行ファイル用に RC を有効にします。ライブラリーのみの構成では RC なしで設定できます。

ICO を変更したら対象の実行ファイルを再ビルドします。`VeroScriptProductIcon.rc.in` はアイコンの SHA-256 を記録し、生成 RC の変更によって Visual Studio の増分ビルドも更新されるようにします。C/C++ のコンパイルオプションは RC に適用しないよう言語で制限します。Installer アプリはプロジェクトリソースとして ICO と 256px PNG を別途使用し、macOS のインストーラーバンドルは ICNS を使います。生成と形式の詳細は[アセットガイド](../../../../../Resources/Assets/icons/README.md)で管理します。`python Resources/Tools/Packaging/veroscript_icons.py check --require-tracked` は管理済みソースを確認します。公開時の監査は任意のビルドホストで Windows PE のアイコングループを読み、各フレームを管理済み ICO と比較できます。対象の実行ファイルは起動しません。実際のシェル表示と対象プラットフォームでの検証は別に記録します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/build-and-bundle.md) · [fr-FR](../fr-FR/build-and-bundle.md) · [ja-JP](build-and-bundle.md) · [ko-KR](../ko-KR/build-and-bundle.md) · [zh-CN](../zh-CN/build-and-bundle.md) · [zh-TW](../zh-TW/build-and-bundle.md)
