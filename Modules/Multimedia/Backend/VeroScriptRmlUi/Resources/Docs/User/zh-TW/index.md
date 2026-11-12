# RmlUi

## 使用服務

`RmlUi::CreateBackend` 將 GUI 樹接入 Graphics 繪製器。原生宿主須提供字型與文字版面服務，載入圖片且沒有自訂解碼器時也須 Image。繪製借用裝置及恰好一個 surface/texture 目標；明確的 layout-only 模式無須繪製目標。成功同步及更新後才能繪製。GUI 服務為 `gui` / `veroscript.gui`，介面版本直接跟隨 Gui 發行版本，目前為 `0.1.1`。

後端提供型別化版面、文字、基礎控制項、輸入及其宣告的 Graphics 合成能力。ML/CSS 屬於 GUI 前端，不接受任意瀏覽器 CSS 或原生 RML。在所屬執行緒關閉，並在關閉借用資源前回收延遲退役物件；清理失敗保留資源以供重試。實際能力與限制取決於原生提供者，編輯器外掛不會自動安裝這些提供者。

## SDK 與整合

```cmake
find_package(VeroScriptRmlUi 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::RmlUi)
```

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/zh-TW/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/zh-TW/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/zh-TW/index.md)

<a id="composition-resources"></a>

## 合成與資源限制

RmlUi 支援巢狀／反向遮罩、圖層擷取、透明度、亮度、對比度、反相、灰階、飽和度、褐色、色相旋轉、模糊和投影。遮罩與濾鏡需要 Graphics 合成，不支援的操作會報錯。模糊 sigma 最大 64 像素，投影 sigma 最大 64/3 像素。漸層按幾何體點陣化一次並快取為紋理，每邊最多 8192、總計 16,777,216 像素。

已載入的資源識別視為不可變。影像缺少或無效會產生更新／繪製錯誤；替換失敗資料後須換用新識別或重建內容。影像輸入上限為 64 MiB，解碼後每邊最多 16,384、總計 16,777,216 像素，自訂字型累計保留上限為 128 MiB。標準 `ReadGuiResourceFile` 讀取器亦限制每個檔案最多 64 MiB；自訂讀取器不會取消影像或字型累計上限。應在紋理開始使用前設定讀取器。

<a id="font-atlas-and-services"></a>

字型圖集為 1024 × 1024 像素，字形及效果連同打包邊距都必須容納其中。失敗回報 `rmlui.font_atlas_failed`，並保留待重建狀態，使重試也回報失敗而非默默漏字。存活的 RmlUi 上下文共用字型登錄，必須使用相同的字型與文字配置服務實體；僅後端 ID 相同並不足夠。所有上下文關閉後才可選用其他實體。透過 `RmlUi::FOptions` 或 GUI 後端選項注入資源，不要在上下文存活期間替換。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
