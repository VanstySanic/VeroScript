# Font

## サービスの利用

`Font::IService` の face はフォントのバイト列を所有し、`IFace` は情報、メトリクス、シェーピング、ラスタライズを提供します。位置は符号付き 26.6 ピクセル、cluster は元 UTF-8 のバイト位置です。`FShapeOptions` は既定で 16 ピクセル、自動方向、カーニング有効、全範囲です。画像は A8 または premultiplied sRGB RGBA8 で、Image の出力と異なります。契約は `font` / `veroscript.font` / `0.1.2` です。

FTextLayout::Create(service,text,direction,error) の最初に ITextLayoutService を明示します。Font は共通 interface を所有し、別 FontBackend が SheenBidi 3.0.0 / Unicode 17.0.0 による CreateTextLayoutService() を提供します。契約は text-layout / veroscript.text-layout / 0.1.0。cache key は backend ID だけでなく service instance identity も保持します。

`FTextLayout::Create(service, text, direction, error)` は明示的に選択したレイアウトサービスを通じて不変の論理テキストを解析します。`GetVisualRuns` は行ごとの bidi 順序を求め、行は単一段落内の UTF-8 境界で指定します。`ShapeLine` は前後の文脈を保持し、視覚 cluster と caret 位置を返します。改行位置、atlas、GPU は引き続き呼び出し側の責務です。`ShapeWithFallback` は選択した face を保持します。既定 Lato Latin の登録名は `Vero Sans` で、他の文字には対応フォントが必要です。スクリプトは `VS::Font::Service::Create("font-backend").LoadDefault()` を使い、位置はピクセルです。

## SDK と統合

```cmake
find_package(VeroScriptFont 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Font)
```

ネイティブ SDK は Core なしで利用できます。任意のスクリプトバインドとネイティブプロバイダーは、実際の Core ビルド、公開 ABI、ツールチェーンを一致させてください。製品バージョンだけでは不十分です。必要なネイティブ依存と Bindings コンポーネントも導入します。

- [Image](../../../../../VeroScriptImage/Resources/Docs/User/ja-JP/index.md) · [Font](index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/ja-JP/index.md)

ShapeLine は表示する各クラスターの最後のグリフで字間を一度だけ適用し、返すグリフメトリクスのアドバンスは変更しません。キャレットの X は論理的なペン終点であり、描画領域の端ではありません。負の字間では終点がクラスターの描画領域内に入る場合があります。

<a id="font-layout-boundaries"></a>

自動段落方向は最初の強い文字を使い、明示的な左右方向は基底方向を固定します。折り返し後の視覚ランは UAX #9 L1/L2 に従います。`FTextLine` は視覚順グリフ、論理バイトクラスタ範囲とキャレットを返し、方向境界の二つの位置は affinity で選びます。不可視の書式制御だけのクラスタはグリフや字間なしで論理キャレット端点を残します。クリップボード文字列とオフセットは論理順に保ちます。合字内部の精密なキャレット、完全な書記素移動、折り返しと配置はホスト側の責務です。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
