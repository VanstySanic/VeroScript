# Stb

二進位 PGM／PPM 依 `Maxval`（1–65535）正規化為 RGBA8。無效最大值或超出範圍的樣本會回報錯誤並保留原輸出；255／65535 滿量程輸入維持原有量化與位元組順序行為。

## 使用服務

`Stb::CreateImageService()` 提供 `image/stb`。PNG、JPEG、BMP、TGA 及其他已啟用 stb_image 格式輸出直通 alpha sRGB RGBA8；GIF 只取首幀，HDR/浮點輸出停用。top-down BMP、16 位元 PGM/PPM 位元組順序及 CgBI PNG 統一到該契約。失敗保留原輸出，並遵守明確解碼限制。

使用獨立結果/錯誤物件且呼叫期間不修改輸入時可並行解碼；缺少解碼器執行緒區域儲存的建置跨執行個體序列化。`VS::Image` 來自 Image 繫結，選用 `VeroScriptStbModule` 僅提供後端。請同時安裝 Image、Stb 和適用的 Bindings 元件。

## SDK 與整合

```cmake
find_package(VeroScriptStb 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Stb)
```

原生 SDK 不要求 Core。選用腳本繫結與原生提供者必須符合實際 Core 建置、公開 ABI 及工具鏈，僅產品版本相同不足以保證相容。請同時安裝原生相依及需要的 Bindings 元件。

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/zh-TW/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/zh-TW/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/zh-TW/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
