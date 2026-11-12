# 在 VS Code 中使用 VeroScript GUI

先安裝 **VeroScript**，再安裝 **VeroScript GUI**。本機主擴充套件需要工具 API 1，請配套更新。GUI 擴充套件負責 `.vswml`、`.vswcss`；`.vs`、LSP、DAP、Runtime 選擇和編譯設定由主擴充套件負責。顏色跟隨 VS Code 主題。

編輯支援控制項、屬性、`bind:`／`on:`、列舉值和資源路徑提示，暫留資訊、大綱、摺疊、配對標籤醒目提示及六／八位十六進位色彩編輯。原名及 Mark 別名區分大小寫，`bind:`／`on:` 不需要 `xmlns`。VSWCSS 使用原生 GUI 子集。結構訊息為資訊層級；最終正確性由原生編譯器判定。內嵌指令碼有語法色彩，沒有獨立 GUI LSP 或運算式語意分析。

內建提示已涵蓋 ListView/ListItem、TreeView/TreeItem、TableView/TableColumn/TableRow、ComboBox、TabView/TabItem、Menu/MenuItem 和 Dialog，包括繼承的屬性與事件。SelectionColor / selection-color 支援標記色彩編輯，Popup Placement 提供 below、right、center 候選。執行時需使用配套重新建置的 GUI 提供者與指令碼程式庫；更新後的原生結構不可混用先前的 0.1.1 二進位檔。 [GUI 框架](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-TW/Reference/Framework.md).

使用 **VeroScript GUI：檢查 GUI、編譯 GUI 應用程式、預覽 GUI、選擇 GUI 進入點、顯示 GUI 輸出**。預覽啟動原生應用程式。以 `veroscript.runtimePath` 選擇已啟用 GUI 和 GUI 指令碼的相符 Runtime，並準備應用程式需要的原生提供者。單純編輯不會啟動原生程序。

明確執行的 GUI 命令在冷啟動時可能編譯完整框架，耗時可超過一分鐘。工作以非同步方式執行，可視需要使用 VS Code 的工作控制停止。

## 工作區設定

`veroscriptGui.entry` 指定 `.vs`／`.vswml`，檢查和預覽亦支援 `.vsapp`。留空使用選取檔案；樣式預設使用同名 `.vswml`，共用樣式應明確選擇進入點。`assets` 指定編譯嵌入檔案，`packageName` 指定套件名稱，`schemaFiles` 增加編輯提示。陣列取代預設值。

`backends` 僅接受 `platform`、`graphics`、`gui`、`image`、`font`、`text-layout`。擴充套件值省略或留空時繼承原生環境；指定後端識別碼會選取該後端，`auto` 要求原生候選選取，`none` 停用該服務。停用預覽必要的服務會傳回原生錯誤。`environmentPath` 透過 `--path` 明確載入 CLI 套件環境，後端選項先於環境載入。它不會設定主擴充套件的 LSP 或偵錯器。參數、環境、工作目錄及編譯選項繼承主擴充套件。檢查／編譯使用 VM，不加入 JIT 模組；預覽繼承執行後端。`previewFrames` 為 0 時不傳影格限制；正安全整數僅適用於 `.vswml`，`.vs` 自行管理迴圈。

標準原生 Context 工廠僅在建構參數、本次呼叫與應用／環境預設值均未提供選擇時後備為 `rmlui`，保留明確的 `auto` 和 `none`。擴充套件設定留空表示不傳覆寫選項；原生環境設定中明確儲存的空後端值則表示停用服務。VM/JIT 執行選項由主擴充套件負責，不屬於 GUI 服務角色。

路徑和程序位於工作區擴充套件宿主，遠端亦同。相對設定與工作路徑以所屬工作區為基準，無工作區時以選取檔案目錄為基準。支援 `${workspaceFolder}`、`${file}`、`${fileDirname}`、`${env:NAME}`；檔案變數需要檔案情境，工作區變數需要資料夾。文件資源以宣告檔案目錄為基準，不將資源 ID 強制視為檔案。

## 工作與中繼資料

工作使用 `type: "veroscript-gui"`、`action: "check" | "compile" | "preview"` 和 `input`。編譯還需要不同且以 `.vsapp` 結尾的 `output`。選用 `assets`、`name`、`frames`、`args`、`backends`、`environmentPath` 覆寫對應設定；陣列取代繼承值，`args` 僅供預覽。命令會詢問儲存涉及的來源檔案；設定工作遵循 VS Code 儲存原則。請另外儲存來源範圍之外的相依檔案。原生錯誤提供檔案與行號導覽，不將 UTF-8 位元組欄誤當 UTF-16 欄。

將自訂 `*.veroscript-gui.json` 加入 `schemaFiles` 可擴充提示並取得 JSON 格式驗證。使用 `schemaVersion: 1`、`types`；型別有 `name` 及選用 `mark`、`bases`、`properties`、`events`，成員有 `name`、`type` 及選用 `mark`、`bindable`、`style`、`values`、`description`。中繼資料不註冊可執行繫結或編譯處理器；衝突標籤別名不提供為候選。

```json
{
	"schemaVersion": 1,
	"types": [
		{
			"name": "Demo::Card",
			"mark": "card",
			"bases": ["VS::Gui::Widget"],
			"properties": [
				{ "name": "Title", "mark": "title", "type": "string", "bindable": true }
			]
		}
	]
}
```

GUI `.vs` 語意提示和偵錯需要主擴充套件設定相容宿主及繫結目錄；沒有自動 GUI 處理器啟動、獨立 GUI 偵錯器或視覺設計器。超過 2 Mi UTF-16 程式碼單元的檔案停止本機結構分析。

未完成標籤與檔案末尾 CSS 宣告仍可完成；保留 `=`／`:` 前空白，數值尺寸以完整值取代。標籤關閉後不提供屬性，類別／ID 選取器不混入控制項型別。CSS URL 連結遵循原生大小寫與跳脫規則；指令碼／樣式內 XML 註解中的標籤不破壞結構範圍。

GUI 編譯需要原生 Application 支援，產出標準 `.vsapp` 應用程式容器。同一產物可用於 GUI 檢查／預覽與通用 CLI inspect／檢查／執行；宿主需要提供 GUI 支援及所需提供者。先前的原始 GUI `.vsapp` 產物需要重新編譯。後端保持手動選擇，擴充套件不會自動選擇 RmlUi。

請使用完整且相符的 GUI Runtime，包括隨附的 `Vm/` 模組。GUI 檢查與編譯可能執行以指令碼撰寫的編譯處理器，因此同時需要 Runtime VM 與所選 GUI 提供者。提供者維持手動選擇；編輯器中繼資料描述可編輯的宣告，執行階段通知實作的變化不會直接增加標記屬性。

通用 CLI 的 `--check` 可獨立檢查產生的 `.vsapp`，也可透過 `--path` 選擇相符的套件環境。GUI 檢查透過 `environmentPath` 選擇該環境。請使用修復後的 Runtime 及相符的已安裝環境；Core／ABI 更新後，應重新產生面向舊建置的環境。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
