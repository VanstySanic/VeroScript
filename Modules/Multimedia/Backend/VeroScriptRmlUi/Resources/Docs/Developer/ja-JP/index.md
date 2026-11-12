# 保守: VeroScriptRmlUi

## 所有範囲と検証

このモジュールは GUI の描画、レイアウト、入力アダプターと同梱依存ライブラリ を管理します。公開インターフェースは [`Public/VeroScriptRmlUi.h`](../../../../Public/VeroScriptRmlUi.h)、モジュール固有の入力は [`Resources/Tests`](../../../Tests) にあります。

共通のプロバイダー境界、再ビルド要件、検証、法的リソースの扱いは [マルチメディアの保守](../../../../../../../../Resources/Docs/Developer/ja-JP/multimedia.md) で管理します。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/ja-JP/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/ja-JP/GENERAL.md)

テキスト計測は内容に基づく有界キャッシュを使います。幅のキーには段落・行範囲、方向、言語、字間、カーニング、フォールバックフォントの改訂を含め、段落解析では実際のテキストレイアウトサービスも区別します。同じ不変テキストの所有者を作り直しても解析を再利用できます。幅はフォント面・サイズごとに最大 1,024 件／512 KiB、段落は最大 2,048 件／8 MiB のテキストです。字形、キャレット、選択範囲は通常のシェーピングを維持します。

入れ子の高さ自動の縦コンテナはブロックフローと内部の間隔要素を使い、入れ子の Flex による固有寸法の反復計測を避けます。Hidden は領域を維持し、Collapsed と浮動要素はフローから除外します。横方向、Grid、Wrap、Splitter、編集領域は専用経路を維持します。2 色のアフィンな線形グラデーションは頂点色を使用でき、その他はラスタライズします。分離したクリップマスクは有界の一時ターゲットを使い、予算不足時は汎用コンポジタへ戻ります。多重・ソフト・反転マスクの乗算済み source-over 合成を維持します。

計測中は直近の段落解析を保持し、単一の段落が LRU のテキスト予算を超える場合も再利用します。次の段落解析で置き換え、フォントリソースの解放時にも破棄します。

<a id="native-provider-maintenance"></a>

任意の `VeroScriptRmlUiModule` はネイティブ GUI 提供者で、Core/Gui ビルドとの一致が必要です。ローカル修正済み RmlUi ヘッダー、ライブラリ、パッチ／ライセンス一覧を同期し、システム RmlUi とアダプターヘッダーを混用しません。ネイティブ SDK のデータ経路は任意の Core バインディング提供者を必要としません。Font、レイアウト、Image、Graphics は選択依存であり、サービス実体の寿命とアトラス失敗は公開 RmlUi ガイドに従います。キャッシュや同期の変更でもこの境界を保持します。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
