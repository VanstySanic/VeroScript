# Font

## 使用服務

`Font::IService` 的 face 保有字型位元組，`IFace` 提供中繼資料、度量、塑形和點陣化。塑形座標為有號 26.6 像素，cluster 為原始 UTF-8 位元組偏移。`FShapeOptions` 預設 16 像素、自動方向、啟用 kerning、完整範圍。點陣圖為 A8 覆蓋或預乘 sRGB RGBA8，不能與 Image 的直通 alpha 混同。服務契約為 `font` / `veroscript.font` / `0.1.2`。

FTextLayout::Create(service,text,direction,error) 首參須明確提供 ITextLayoutService。Font 僅擁有通用服務/分析介面，獨立 FontBackend 透過 SheenBidi 3.0.0 / Unicode 17.0.0 提供 CreateTextLayoutService()。契約為 text-layout / veroscript.text-layout / 0.1.0。版面快取須包含服務實例身分，不能僅依後端 ID。

`FTextLayout::Create(service, text, direction, error)` 透過明確選用的版面服務分析不可變邏輯文字。`GetVisualRuns` 執行逐行雙向重排，行須位於同一段落且以 UTF-8 邊界起迄。`ShapeLine` 保留上下文並傳回視覺 cluster 與插入點；換行位置及 atlas/GPU 仍由呼叫端管理。`ShapeWithFallback` 保有所選 face。預設 Lato Latin 註冊為 `Vero Sans`，其他文字須提供合適字型。腳本使用 `VS::Font::Service::Create("font-backend").LoadDefault()`，腳本座標為像素。

## SDK 與整合

```cmake
find_package(VeroScriptFont 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Font)
```

原生 SDK 不要求 Core。選用腳本繫結與原生提供者必須符合實際 Core 建置、公開 ABI 及工具鏈，僅產品版本相同不足以保證相容。請同時安裝原生相依及需要的 Bindings 元件。

- [Image](../../../../../VeroScriptImage/Resources/Docs/User/zh-TW/index.md) · [Font](index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/zh-TW/index.md)

ShapeLine 在每個可見叢集的最後一個 glyph 上套用一次字距，回傳的 glyph 度量保留原始推進量。游標 X 是邏輯畫筆終點，而非視覺外框邊緣；負字距可能讓終點落入叢集的視覺範圍內。

<a id="font-layout-boundaries"></a>

自動段落方向使用首個強方向字元，明確左右方向則固定基底方向；換行後的視覺 run 遵循 UAX #9 L1/L2。`FTextLine` 傳回視覺順序字形、邏輯位元組叢集範圍與游標停靠點；方向邊界可以有兩個游標位置，由 affinity 選擇。僅含不可見格式控制的叢集保留邏輯游標端點，不產生字形或字距。剪貼簿文字和偏移保持邏輯順序。精確連字內部游標、完整字素導覽、換行與對齊仍由宿主負責。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
