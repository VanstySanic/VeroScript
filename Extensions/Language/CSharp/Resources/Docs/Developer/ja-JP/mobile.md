# CSharp — Mobile SDK

このページは C# モバイル SDK をビルドして公開する保守担当者向けです。デスクトップとモバイルは `VeroScriptCSharp.csproj` と NuGet ID `VeroScript.CSharp` を共有します。既定のターゲットは .NET 8/9 で、`VeroScriptIncludeMobile=true` により .NET 10 Android/iOS の両ターゲットを追加します。Android は API 26、arm64-v8a/x86_64、iOS は 15.0、実機 arm64 とシミュレーター arm64/x86_64 が必要です。モバイルを含むパッケージには 4 つのマネージドターゲット、対応ワークロード、検証済みの完全なネイティブルートが必要です。

`prepare-native.py` は同じソースリビジョンの Android AAR と静的 `VeroScriptIOS.xcframework` SDK を使用します。これらは Core/C++/Interop を含みます。C# アプリは CPython SDK や Python モバイルパッケージ作成ツールを必要とせず、Java JNI ラッパーやネイティブ JIT モジュールも使用しません。新しいディレクトリに配置し、検証後にパッケージを作成してください。

## ネイティブ SDK の準備

```sh
gradle --project-dir Extensions/Platform/Mobile/VeroScriptAndroid --no-daemon assembleRelease
python3 Resources/Tools/Packaging/package-veroscript-mobile.py ios \
  --cmake /path/to/cmake --out-root dist/mobile/ios
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py prepare \
  --android-aar Extensions/Platform/Mobile/VeroScriptAndroid/build/outputs/aar/VeroScriptAndroid-release.aar \
  --ios-sdk dist/mobile/ios --output dist/csharp-mobile/native
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py verify \
  dist/csharp-mobile/native
dotnet pack Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj \
  -c Release -p:VeroScriptIncludeMobile=true \
  -p:VeroScriptMobileNativeRoot=/absolute/path/to/native \
  -o /absolute/path/to/mobile-nuget
```

Gradle の出力名が異なる場合は実際の AAR 名を指定します。Android は設定された SDK/NDK/Gradle、iOS は macOS と完全な Xcode が必要です。モバイルのマネージドターゲットには .NET 10 Android/iOS ワークロードが必要です。準備用インタープリターが `python3` 以外の場合は `VeroScriptMobilePython` で指定します。準備処理は SDK のダウンロードやアプリ署名を行いません。

## 統合契約

準備処理は正本のヘッダー、標準ライブラリ、6 言語カタログ、再配布の通知、SHA-256 ファイル一覧を検証します。Android の ID/API と 16 KB ELF LOAD アラインメントを確認し、両 ABI の `libVeroScriptInterop.so` と対応する `libc++_shared.so` を保持します。iOS 静的アーカイブのオブジェクトについて実機/シミュレーターのアーキテクチャと最低 OS を検証します。iOS SDK には Foundation/libc++ が必要です。既存の出力ディレクトリは拒否します。

NuGet は `buildTransitive/VeroScript.CSharp.targets` を自動インポートします。ソースの `ProjectReference` ではこのファイルをインポートし、`VeroScriptMobileNativeRoot` を指定します。Android のライブラリ/アセットと、iOS の `__Internal`、`NativeReference`、`ForceLoad`、生成した `ReferenceNativeSymbol` の保持を維持します。参照解決後に間接参照も含め実際の iOS バインディングアセンブリを検証し、デスクトップ/Android 用は拒否します。標準ライブラリとカタログはアプリのリソースです。バイトコードキャッシュはアプリ管理の書き込み可能なディレクトリを使います。

NuGet は準備済みルートの相対配置を `native/` 配下に保ち、`android/<abi>`、`ios`、`assets`、`licenses` を含みます。パッケージ化でこれらのパス要素を重複させません。MSBuild ターゲットは `OutputType=Exe`、または SDK 推論後の Android の `AndroidApplication=true` でアプリを判定します。Android が出力型を `Library` に推論してもネイティブライブラリと MAUI アセットの選択を有効に保ち、通常のクラスライブラリには適用しません。

リソース配置はパスとダイジェストを確認し、大文字小文字を含む正確な `stdlib/core.vs` を要求します。ストリームを解放し、再試行で古いファイルを修復します。失敗後に検証済みファイルが残る場合は、実行前に再試行します。非同期の配置完了後、セッションの作成、設定、使用、破棄を同じワーカースレッド上で行います。VM を明示的に選択してください。パッケージはネイティブ JIT を提供しません。

[アプリへの統合](../../User/ja-JP/mobile.md)

## リリース検証

ポータブル C# テストは動的インポートと `IOS` 静的インポートをネイティブエクスポート一覧と照合します。リソース破損、パストラバーサル、キャンセル、ストリーム解放、古いファイルの修復、実際の標準ライブラリ/カタログ配置を検証します。Python フィクスチャーは SDK 準備、MSBuild アセット選択、不適合 RID/OS、不完全なターゲット構成、解決済みの不適合 iOS アセンブリの拒否を検証します。これらはホストテストです。

安定版のモバイルリリース前に、MAUI サンプルを Release で Android エミュレーター/実機と iOS シミュレーター/実機でビルド・実行します。Android 16 KB ページ環境、依存関係と APK ZIP のアラインメント、iOS 署名/リンク、AOT/トリミング、キャンセル、コールバック、リソースのインポート、コールドスタートを含めます。ツールのバージョン、パッケージ検査、実測結果を記録してください。対象プラットフォームでの検証完了までモバイル対応は実験的です。
## 出版文書

モバイルパッケージには機能ファイル、実行時リソース、ライセンスを含めます。README、マニュアル、サンプルは含めず、統合 User 文書は独立した Resources 配布物から取得します。ネイティブリソースのパスと実機での検証要件は維持します。

## デスクトップとモバイルで共有する NuGet フィード

上のコマンドはモバイル対応の `VeroScript.CSharp` パッケージを準備します。デスクトップ／モバイルコレクションの CSharp を公開する場合は、登録された C# の 6 パッケージ ID と対応する `.nupkg`、`.snupkg` を含むフィード正本を用意します。`VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY` でフィードを選択します。作成済みパッケージを `package-veroscript-mobile-release.py` で再利用する場合は、同じモバイル対応 `VeroScript.CSharp` ファイルを `--csharp-package` に渡します。全ターゲットで同じバイト列を使います。全入力と実行例は[モバイル公開手順](../../../../../../../Resources/Tools/Packaging/README.md#mobile-sdk-tooling)を参照してください。

<!-- docs-navigation -->

---

[開発者文書](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
