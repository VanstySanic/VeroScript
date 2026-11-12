# FontBackend

## サービスの利用

`FontBackend::CreateFontService()` は `font/font-backend` を提供します。FreeType は読み込み、メトリクス、A8／カラー glyph、HarfBuzz は文脈形、合字、script、言語、方向を扱います。各 face は入力を所有して変更を同期し、公開型に第三者の型は含みません。

ネイティブファクトリー、または対応するホストでバックエンド `font-backend` を指定した登録済み Font スクリプト API を使用します。保持された face はプロバイダーモジュールも保持します。Font／FontBackend と必要な provider／Font Bindings を導入します。既定フォントとライセンスは Font が所有します。段落 bidi 解析は `FTextLayout` の役割で、文字の対応範囲はフォントにも依存します。

## SDK と統合

```cmake
find_package(VeroScriptFontBackend 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::FontBackend)
```

ネイティブ SDK は Core なしで利用できます。任意のスクリプトバインドとネイティブプロバイダーは、実際の Core ビルド、公開 ABI、ツールチェーンを一致させてください。製品バージョンだけでは不十分です。必要なネイティブ依存と Bindings コンポーネントも導入します。

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/ja-JP/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/ja-JP/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/ja-JP/index.md)

FontBackend は FreeType、HarfBuzz、SheenBidi、段落解析と原文 notice を所有します。CreateFontService() と CreateTextLayoutService() は別 service で、任意 native provider は両方を font-backend として登録します。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
