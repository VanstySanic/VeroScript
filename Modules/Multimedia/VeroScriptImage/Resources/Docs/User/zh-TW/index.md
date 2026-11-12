# Image

## 使用服務

`Image::IService::Decode` 輸出緊密排列的直通 alpha sRGB RGBA8，失敗保留原結果；`Image::Validate` 檢查尺寸及緩衝區。`FDecodeOptions` 預設每邊最多 16,384、16,777,216 像素、64 MiB 輸入。資源解析、快取和 GPU 上傳由呼叫端負責。服務契約為 `image` / `veroscript.image` / `0.1.0`。

解碼器由 `stb` 等提供者實作。註冊 Image 繫結後，可用 `VS::Image::Service::Create("stb").Load("image.png")` 或 `Decode(bytes)`。尺寸與索引為 `int64`，複製過大字串前也檢查預設限制。注入的 reader 負責資源解析；未提供時，檔案路徑基於處理序工作目錄。

## SDK 與整合

```cmake
find_package(VeroScriptImage 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Image)
```

原生 SDK 不要求 Core。選用腳本繫結與原生提供者必須符合實際 Core 建置、公開 ABI 及工具鏈，僅產品版本相同不足以保證相容。請同時安裝原生相依及需要的 Bindings 元件。

- [Image](index.md) · [Font](../../../../../VeroScriptFont/Resources/Docs/User/zh-TW/index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/zh-TW/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
