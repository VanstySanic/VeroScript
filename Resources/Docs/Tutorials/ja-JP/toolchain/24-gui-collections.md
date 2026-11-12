# 24: コレクション、ナビゲーション、ダイアログを使う

同梱 GUI 例を開き、項目選択、仮想テーブルのソート、モーダルダイアログの結果を確認します。リストとテーブルに各 10,000 行を持ち、ビューポート付近の行だけを実体化します。

## 1. 準備

`Vm/` を保持する対応 GUI Runtime と、設定済みのプラットフォーム／描画 `sdl3`、GUI `rmlui`、画像 `stb`、フォント／文字配置 `font-backend` を使用します。これらを提供するインストール済み環境を持つ既存 GUI プロジェクトで作業してください。ソースのコピーだけではプロバイダーはインストールされません。完全な Runtime とプロバイダー資源を保持します。

[main.vs](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/main.vs)、[page.vswml](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswml)、[page.vswcss](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswcss) の 3 ファイルを同じ書き込み可能なプロジェクトディレクトリーへコピーし、名前と相対位置を維持します。その場所でターミナルを開きます。

## 2. 検査

以下のコマンドはコンパイル処理のタイムアウトを 20 秒に設定します。命令数制限や他のホスト方針も引き続き適用されるため、プロジェクトとホストに合わせて調整してください。検査成功時は終了コード 0 で、ウィンドウを開かず `main` も実行しません。

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-check main.vs
```

## 3. 実行と操作

同じオプションでスクリプトをプレビューします。この入口は `Application.Run()` を呼ぶので、終了するにはウィンドウを閉じます。`--frames` は ML プレビュー入口だけで使用でき、この `.vs` 入口に指定すると使用方法エラーになります。

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-preview main.vs
```

ListView／TreeView の行を選び、矢印で移動します。ComboBox の選択と TabView のページを変更します。表見出しでソートできますが、UTF-8 の順序比較なので数字に見える文字列も数値ソートにはなりません。File メニューからダイアログを開き、承認／取消または Escape を選び、状態表示の結果を確認します。モーダル表示中は背景の通常ポインター／キー入力を遮断します。

## 4. 仮想化を確認

各 10,000 行のビューをスクロールします。論理データは残り、ウィジェットだけを再利用します。選択は再利用されるインスタンスではなくキーに属します。行高は固定 28 単位で、カスタム内容も収めます。可変行高とツリー仮想化は対象外です。

## 5. 演習

ラベルや状態文字列を変更し、再検査します。`count × rowHeight ≤ 16,777,216` を保ちながら行数を増やし、スクロールを確認してください。カスタム行ファクトリーは再利用状態を完全に初期化し、安定した空でない一意キーを使用します。`SetVirtualSource`、オーバースキャン、所有権と選択は [コレクション、ナビゲーション、ダイアログ](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/ja-JP/Reference/Framework.md) を参照してください。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/24-gui-collections.md) · [fr-FR](../../fr-FR/toolchain/24-gui-collections.md) · [ja-JP](24-gui-collections.md) · [ko-KR](../../ko-KR/toolchain/24-gui-collections.md) · [zh-CN](../../zh-CN/toolchain/24-gui-collections.md) · [zh-TW](../../zh-TW/toolchain/24-gui-collections.md)
