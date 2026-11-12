# 在 Rider 中使用 VeroScript GUI

## 安裝與設定

先安裝 **VeroScript**，再透過 **Settings → Plugins → Install Plugin from Disk** 安裝相容的 **VeroScript GUI**，並依提示重新啟動。Rider 要求及 Runtime 選擇以下方基礎外掛指南為準。

按[基礎插件指南](../../../../../VeroScriptJetBrains/Resources/Docs/User/zh-TW/index.md)
在「語言與框架 → VeroScript」設定 Runtime。GUI 任務需要匹配的 GUI 版 CLI、GUI 腳本庫及
原生提供者，插件不會安裝這些元件。沒有 Runtime 時，仍可使用本機標記/樣式著色和內建補全。

## 編輯標記與樣式

內建補全包括 `ListView`、`TreeView`、`TableView`、`ComboBox`、`TabView`、`Menu` 和 `Dialog`，
以及繼承的選取/彈出屬性、自訂事件與樣式屬性。資料項目與虛擬化透過 GUI 指令碼 API 設定。
請使用包含這些控制項的配套 GUI Runtime。

`.vswml` 使用原生 GUI 標記語法，`.vswcss` 使用 GUI 樣式語言，不等同於 HTML 或完整 CSS。
`bind:`、`on:` 不需要宣告 `xmlns`。補全包括內建控件、繼承屬性、取值綁定、事件、樣式屬性
及支援的偽狀態，保留大小寫規則，並遵循 Rider 的首項選取設定。指令 `src` 支援本機路徑導覽和
補全；相對路徑以所屬檔案為基準，目前系統識別的絕對路徑以檔案系統根為基準。
分隔符遵循目前系統規則，保留檔名中的 `?` 等字面字元。
宿主不透明資源識別碼，以及含 SDK 會裁剪的首尾空白或實體跳脫（`&...;`）的路徑不提供本機導覽。
普通路徑引用參與 Rider 的檔案/目錄重新命名，寫入名稱時保留標記跳脫。

以 `=` 接受屬性或以 `:` 接受樣式屬性時，不會重複既有標點，並將游標移至值處；既有值會保留。
Tab 取代完整名稱，包括連字號名稱及 `bind:`/`on:` 前綴。

內嵌 `<script>`、`<style>`（包括 CDATA）支援詞法著色。需要完整 VS 語意編輯時，將程式碼
放進外部 `.vs`，使用 `<script src="logic.vs" type="App::Logic" />` 引用；外部腳本繼續使用
基礎插件的 LSP。本插件沒有實作內嵌腳本/綁定的語意補全、自訂控件 Schema 分析、格式化
或自動編譯診斷。請透過「檢查 GUI」呼叫原生工具驗證。
GUI API 的語意解析與偵錯仍以所設定的原生 LSP/DAP 及宿主支援為準；本插件不會為它們產生可執行綁定。

`page.vswml` 範例：

```xml
<Stack Width="100%" Height="100%">
	<style src="page.vswcss" />
	<Text Text="Hello, VeroScript GUI!" />
</Stack>
```

`page.vswcss` 範例：

```css
:root {
    padding: 16px;
    gap: 12px;
    color: #edf4fc;
    background-color: #101927;
    font-size: 18px;
}
```

完整語意見[框架指南](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-TW/Reference/Framework.md)
及[綜合範例](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/Showcase/page.vswml)。
自訂控件不會僅因未列入內建補全目錄而被標記成錯誤。

## 檢查、預覽與烘焙

右鍵本機 `.vswml`、`.vs` 或 `.vsapp` 入口，選擇 **VeroScript GUI**。
`.vswcss` 透過引用它的標記入口檢查，不能直接執行。

| 任務 | 行為 |
| --- | --- |
| 檢查 GUI | 呼叫 `gui-check`，檢查／編譯，但不呼叫應用入口、不建立預覽視窗。 |
| 預覽 GUI | 呼叫 `gui-preview`，執行入口並開啟獨立原生視窗；可透過 Rider 停止或關閉視窗。 |
| 設定 GUI 應用程式烘焙 | 開啟編譯設定，核對 `.vsapp` 輸出與資源，再點「執行」呼叫 `gui-compile`。 |

檢查或烘焙原始碼時仍可能執行腳本編譯處理器。請使用包含 VM 和 GUI 腳本程式庫的完整相符 Runtime；這些工作不需要執行用的 JIT 提供者。

可在執行組態中新增 **VeroScript GUI**。`.vswml` 支援從上下文建立預覽組態；普通 `.vs` 的
執行與偵錯繼續由基礎插件提供。GUI 任務目前僅支援「執行」，沒有 GUI 專用 DAP 啟動適配。
宿主已準備偵錯域時，可按基礎插件說明附加。預覽需要明確執行，沒有內嵌設計器或自動熱重載。

## 設定細節

- 上下文操作繼承 GUI 設定範本，清除不適用於所選任務/入口的選項；適用的自訂烘焙輸出會保留。
- 繼承基礎插件的 Runtime、語言和專案編譯設定。單次執行的編譯 JSON 覆寫不改變編輯設定。
  繼承路徑保留專案來源；執行覆寫中的相對路徑按工作目錄解析。
- 上下文入口使用 `$PROJECT_DIR$`。工作目錄留空表示專案根目錄。ML/CSS 內部的相對資源引用
  始終以所屬原始碼檔案為基準。
- 「套件環境」對應 `--path`。後端欄位留空時不傳覆寫項，繼承 Runtime/套件環境預設值。
  `auto` 交由原生端選擇可用提供者，`none` 停用該服務；停用預覽必需服務時會報錯。
  原生環境中明確儲存的空後端值按 `none` 處理，與此處留空不同。對應提供者仍需安裝或由 GUI Runtime 提供。
- 標準 `Context` 工廠僅在建構參數、呼叫選擇及工廠預設值皆未指定 GUI 後端時退回 `rmlui`，
  不會取代明確的 `auto`/`none`。六個後端欄位只管理 GUI 服務；VM/JIT 執行選項不屬於這些欄位。
- 檢查和烘焙使用 VM；預覽保留原生執行預設值。
- 持久化入口路徑時只正規化本機路徑分隔符，保留 POSIX 檔名中的反斜杠。
- 預覽參數放在 `--` 後。影格數限制為正 `int64`，僅用於 `.vswml`；留空或 `0` 表示不限。
  腳本與 `.vsapp` 自行管理執行迴圈。
- 烘焙要求 `.vswml` 或 `.vs` 原始碼、以 `.vsapp` 結尾的輸出，可填寫名稱與資源 JSON 陣列，
  如 `["assets/logo.png"]`。輸出不能覆寫入口或列出的資源。預設輸出位於 `.tmp/gui/`，
  如需長期保留產物，請修改路徑。
- 環境變數為字串值的 JSON 物件，可控制是否繼承父行程環境。原生錯誤引用本機檔案時，
  主控台提供行導覽。Runtime 不支援命令、缺少提供者等錯誤會顯示在主控台。

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
