# FontBackend

## 使用服務

`FontBackend::CreateFontService()` 提供 `font/font-backend`。FreeType 負責載入、度量、A8/彩色 glyph；HarfBuzz 處理上下文形式、連字、文字、語言及方向。每個 face 擁有輸入位元組並同步修改，公開服務不暴露第三方型別。

使用原生工廠，或在相容主程式中透過已註冊的 Font 腳本 API 選用 `font-backend`。保留的 face 同時保有提供者模組。安裝 Font/FontBackend，並按需安裝提供者及 Font Bindings。預設字型及授權歸 Font。段落雙向分析由 `FTextLayout` 提供；文字覆蓋仍取決於所選字型。

## SDK 與整合

```cmake
find_package(VeroScriptFontBackend 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::FontBackend)
```

原生 SDK 不要求 Core。選用腳本繫結與原生提供者必須符合實際 Core 建置、公開 ABI 及工具鏈，僅產品版本相同不足以保證相容。請同時安裝原生相依及需要的 Bindings 元件。

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/zh-TW/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/zh-TW/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/zh-TW/index.md)

FontBackend 擁有 FreeType、HarfBuzz、SheenBidi、段落分析及原始授權。CreateFontService() 與 CreateTextLayoutService() 提供獨立服務，選用原生提供者建置後兩者後端 ID 皆為 font-backend。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
