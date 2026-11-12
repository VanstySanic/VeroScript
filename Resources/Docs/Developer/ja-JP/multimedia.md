# マルチメディアの保守

この共通保守ガイドは、マルチメディアモジュール間のプロバイダー境界と検証手順を管理します。公開動作と API は各モジュールの User、実装の詳細は Developer に置きます。

公開ヘッダーに第三者ライブラリの型を公開しません。共通契約は Media モジュール、具体的な実装と原文ライセンスはバックエンドが所有します。更新時はエラー、出力保持、所有権を維持します。サービス契約や Core の識別情報が変わったら、提供側と利用側を共に再ビルドします。

実行が許可される環境で、モジュールの CMake エントリと登録済みテストを使用します。インストール済み CMake 利用側、任意のバインド／プロバイダー、対応を宣言する各プラットフォームを個別に確認します。静的検査だけで描画、フォント、機器、モバイル、IDE の動作を認定しません。一時測定記録を User に混ぜないでください。

再配布するすべての依存ライブラリの原文ライセンスと固定ソースの記録を保持します。第三者の法的文章は原文のまま保存します。

## モジュールガイド

| モジュール | 利用 | 保守 |
| --- | --- | --- |
| VeroScriptPlatform | [ユーザー](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptGraphics | [ユーザー](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptAudio | [ユーザー](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptImage | [ユーザー](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptFont | [ユーザー](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptGui | [ユーザー](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptSdl3 | [ユーザー](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptStb | [ユーザー](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptFontBackend | [ユーザー](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/ja-JP/index.md) |
| VeroScriptRmlUi | [ユーザー](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/User/ja-JP/index.md) | [開発者](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/ja-JP/index.md) |

GUI／foundation の集約コンポーネント install は CMake 3.25+ の `block(SCOPE_FOR VARIABLES ...)` を使います。入れ子のコンポーネント選択を復元し、`CMAKE_INSTALL_MANIFEST_FILES` と `CMAKE_ABSOLUTE_DESTINATION_FILES` を伝播して、Gui／RmlUi／リソースの依存 install とファイル一覧を維持します。インストール済み SDK の利用側と最終配布フィルターは別に検証してください。

<!-- docs-navigation -->

---

[文書の保守](index.md)

[en-US](../en-US/multimedia.md) · [fr-FR](../fr-FR/multimedia.md) · [ja-JP](multimedia.md) · [ko-KR](../ko-KR/multimedia.md) · [zh-CN](../zh-CN/multimedia.md) · [zh-TW](../zh-TW/multimedia.md)
