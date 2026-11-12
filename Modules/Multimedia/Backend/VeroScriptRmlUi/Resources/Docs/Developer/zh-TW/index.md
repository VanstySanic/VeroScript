# 維護 VeroScriptRmlUi

## 原始碼職責與驗證

本模組維護GUI 繪製、版面配置、輸入配接及其內建相依項目。公開介面宣告位於 [`Public/VeroScriptRmlUi.h`](../../../../Public/VeroScriptRmlUi.h)，模組專屬輸入位於 [`Resources/Tests`](../../../Tests)。

公共提供者邊界、重建要求、驗證與法律資源處理統一維護在[多媒體維護](../../../../../../../../Resources/Docs/Developer/zh-TW/multimedia.md)。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/zh-TW/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)

文字測量使用有界的內容快取：寬度鍵包含段落與行範圍、方向、語言、字距、字偶距及後備字型修訂；段落分析亦比對實際文字佈局服務。重新建立相同不可變段落的擁有者不必重做分析。每個字級字型的寬度快取最多 1,024 項／512 KiB；段落分析最多 2,048 項／8 MiB 文字。完整字形、游標及選取範圍佈局仍使用正常塑形路徑。

巢狀的自動高度縱向容器使用區塊流與內部間距元素，避免巢狀 Flex 反覆測量固有尺寸。Hidden 子控制項保留空間，Collapsed 與浮層不參與此流。橫向、Grid、Wrap、Splitter 和可編輯內容保留專用佈局。雙色仿射線性漸層可使用頂點色，其餘漸層仍光柵化。隔離的裁切遮罩使用有界暫存目標；預算不足時退回通用合成器。巢狀、柔邊及反向遮罩維持預乘透明度與 source-over 語義。

測量期間保留最近一個段落的分析，包括單個超過 LRU 文字預算的段落；下一段落分析會取代它，釋放字型資源時一併清除。

<a id="native-provider-maintenance"></a>

可選 `VeroScriptRmlUiModule` 提供原生 GUI 服務，須符合 Core／Gui 建置。本地修補的 RmlUi 標頭、編譯程式庫及修補／授權清單應同步，不能混用系統 RmlUi 與這些配接器標頭。原生 SDK 資料路徑不要求啟用可選 Core 繫結提供者。Font、文字布局、Image 和 Graphics 仍為明確選擇的相依；服務實體壽命和圖集失敗行為遵循 RmlUi 使用者契約。修改資源快取或同步時須保留這些邊界。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
