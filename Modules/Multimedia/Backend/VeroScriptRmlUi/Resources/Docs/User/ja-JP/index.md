# RmlUi

## サービスの利用

`RmlUi::CreateBackend` は GUI ツリーを Graphics に接続します。ネイティブホストはフォントとテキストレイアウトのサービスを渡し、独自画像デコーダーがない場合は Image も渡します。描画は device と一つの surface／texture を借用し、明示的な layout-only モードには描画先が不要です。成功した同期と更新の後に描画します。GUI サービスは `gui` / `veroscript.gui` で、インターフェイス版は Gui のリリース版に従い、現在は `0.1.1` です。

型付きレイアウト、テキスト、基本 control、入力、宣言された Graphics 合成機能を使えます。ML／CSS は GUI frontend の範囲で、任意のブラウザー CSS や native RML ではありません。所有スレッドで閉じ、借用リソースを閉じる前に退役待ちを回収します。清掃失敗時は再試行用にリソースを保持します。機能と制限はネイティブプロバイダーに依存し、エディタープラグインだけでは導入されません。

## SDK と統合

```cmake
find_package(VeroScriptRmlUi 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::RmlUi)
```

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/ja-JP/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/ja-JP/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/ja-JP/index.md)

<a id="composition-resources"></a>

## 合成とリソース上限

RmlUi はネスト／反転マスク、レイヤーキャプチャ、不透明度、明度、コントラスト、反転、グレースケール、彩度、セピア、色相回転、ぼかし、ドロップシャドウを扱います。マスクとフィルターには Graphics 合成が必要で、未対応の操作はエラーになります。ぼかしの sigma は最大 64 ピクセル、影は 64/3 ピクセルです。グラデーションはジオメトリごとに一度ラスタライズしてテクスチャにキャッシュし、各辺 8192、合計 16,777,216 ピクセルに制限します。

読み込み済みリソースの識別子は不変です。不足／不正画像は更新・描画エラーとなるため、失敗したデータの置換後は新しい識別子かコンテキストを使います。画像入力は 64 MiB、デコード後は各辺 16,384、合計 16,777,216 ピクセル、保持するカスタムフォントは合計 128 MiB が上限です。標準の `ReadGuiResourceFile` も各ファイルを 64 MiB に制限します。カスタムリーダーでも画像とフォント総量の上限は維持されます。テクスチャの使用開始前にリーダーを設定してください。

<a id="font-atlas-and-services"></a>

フォントアトラスは 1024 × 1024 ピクセルで、グリフと効果は配置パディングを含めて収めます。失敗は `rmlui.font_atlas_failed` となり未生成状態を保つため、再試行も文字を黙って欠落させず失敗を返します。生存する RmlUi コンテキストはフォント登録を共有し、同じフォント／レイアウトサービス実体を使います。バックエンド ID の一致だけでは不十分です。すべて閉じた後は別実体を選べます。`RmlUi::FOptions` または GUI バックエンドオプションから注入し、生存中に置き換えません。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
