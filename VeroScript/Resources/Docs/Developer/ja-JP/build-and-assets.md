# コアのビルドとランタイム asset

[交換可能な VM バックエンド](vm-backends.md)

## ビルドターゲット

[交換可能な VM バックエンド](vm-backends.md#vm-provider-assembly) を参照してください。

## ランタイム asset

このターゲットは `VeroScriptRuntimeAssets` に依存します。共通の `Resources/Tools/CMake/VeroScriptLocalization.cmake` 配置プロファイルを使い、古い JSON カタログを削除し、変更されたカタログと標準ライブラリのファイルを実行時の出力ディレクトリにコピーします。個別のテストターゲットにもこの依存関係が適用され、現在のソースに対応するカタログを使います。

ローダーは `VERO_LOCALIZATION_DIR`、続いて配置先とソースリポジトリのルートを検索します。正確な順序は[診断のローカライズ](localization-and-resources.md)を参照してください。有効なツリーには `en-US` の基準、修飾された正規 BCP 47 カルチャディレクトリ、一意のメッセージ ID が必要です。翻訳ファイルとキーは英語の部分集合でもよく、不足分は英語を使います。モジュールと標準ライブラリは既存 ID を上書きせずに有効なカタログをマージできます。使用可能な言語は読み込み済みリソースから検索します。

## バイナリ互換性

bytecode と package の互換性は asmjit の有無から独立しています。optional backend は公開済み runtime asset、diagnostic、module format の契約を変えてはなりません。

## ビルド基盤とソースカタログの追加

独立リポジトリの補助ツールは `Resources/Tools/CMake` にあります。ルートからは `Script/` を付けずに参照します。 公開 locale API の `InitializeModuleFromEnvironment` は、検出したソースモジュールからの相対パスを受け取ります。例は `VeroScriptTooling` と `Extensions/Language/Cpp/VeroScriptCpp` です。コアがソースカタログを選んだ場合だけ、既存 ID を上書きせず追加します。明示的なカタログ置換でこの検索状態は消去されます。`VeroScriptLocaleSourceTests` はモジュール名変更後の検出と追加を確認します。

- [ローカライズ資源と配布](localization-and-resources.md)

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/build-and-assets.md) · [fr-FR](../fr-FR/build-and-assets.md) · [ja-JP](build-and-assets.md) · [ko-KR](../ko-KR/build-and-assets.md) · [zh-CN](../zh-CN/build-and-assets.md) · [zh-TW](../zh-TW/build-and-assets.md)
