# GUI

Gui はウィジェットツリー、ビルダー、属性、イベント、ML/CSS を提供します。RmlUi は個別に選択するレイアウトと描画の実装です。

- [フレームワークと RmlUi の統合](Reference/Framework.md)
- [マークアップとスタイル](Reference/Markup.md)

`vs-gui` / `vs-rmlui`. CMake: `VeroScriptGui` / `VeroScriptRmlUi`; VS: `VS::Gui`.

[VS Code](../../../../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/User/ja-JP/index.md) · [Rider](../../../../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/User/ja-JP/index.md)

`gui-compile` は Application 対応を必要とし、標準 ZIP `.vsapp` アプリケーションコンテナーを生成します。起動記述、GUI のコンパイル済みイメージ、リソース情報、明示的に埋め込んだアセットを保持します。GUI の検査／プレビューと通常の CLI の検査／実行／inspect は同じコンテナーを読みます。GUI スクリプト対応の Runtime でも実行できます。従来の生の GUI `.vsapp` は再コンパイルしてください。バックエンドはインストール環境のサービス選択規則に従い、ホスト、プロジェクト設定、コマンドで上書きできます。エディターは RmlUi を固定しません。

[チュートリアル: コレクション、ナビゲーション、ダイアログ](../../../../../../../Resources/Docs/Tutorials/ja-JP/toolchain/24-gui-collections.md)

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
