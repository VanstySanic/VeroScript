# VeroScript GUI VS Code 開發文件

## 架構

原生 GUI 驗收須使用相符的 Core、Tooling、VM 與提供者建置；產品版本相同不代表 ABI 相容。編輯器中繼資料檢查與實際檢查、應用程式容器編譯及有限影格預覽分別驗證。

產物回歸透過帶有所選提供者的 `gui-check`、獨立的一般 `--check`，以及繼承所選原始碼專案環境和編譯參數的一般 `--check` 驗證 `.vsapp`。顯式標準函式庫路徑與 `--path` 同時傳入，涵蓋原生標準函式庫根目錄去重。總期限涵蓋原生工作與封存驗證，各命令仍有獨立期限。原生 Core／ABI 建置變更後，應重新產生已安裝的測試環境。

Runtime、LSP、DAP 留在主擴充套件。GUI 透過相依啟用取得 `Public/api.d.ts` 的 API 1，僅明確執行時解析工具鏈。來源 URI 展開執行變數，工作所屬資料夾決定設定。編輯及自動工作探索不解析或啟動原生工具；解析設定工作可查詢 CLI 版本。原生 GUI 生產程式碼獨立維護。

結構模型依文件版本快取，解析有大小與深度上限；變更或取消後捨棄非同步中繼資料／路徑結果。自訂快取和監視器依解析設定隔離。原名及 Mark 區分大小寫，衝突別名省略，繼承遍歷避免重複。編輯提示不取代編譯器語意。

## 建置與驗收

使用 Node.js 22.18+、桌面 VS Code 1.106+，分別在指定模組目錄執行。兩邊皆需安裝相依套件，因共用宣告參照主擴充套件 SDK 型別。開發測試載入兩個擴充套件，套件測試以隔離設定依序安裝兩個本機 VSIX；測試 CLI 不應向 Marketplace 取得未發佈相依項。

```sh
# VeroScriptVSCode
npm ci
npm run typecheck
npm run package
# VeroScriptGuiVSCode
npm ci
npm run typecheck
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
node Resources/Tools/test.mjs --packaged --skip-build
```

桌面 Extension Host 測試應逐套依序執行。多個 VS Code 測試視窗同時開啟時，可能爭用補全確認、復原及 Enter 案例所需的編輯器焦點。

原生 GUI 驗收為每個檢查、編譯或預覽工作保留 600 秒，並僅按該測試的工作序列調整整體期限；編輯器提供者仍使用較短的等待期限。明確執行的 GUI 命令可能冷編譯完整框架，正常完成也可能超過一分鐘。產品工作保持非同步，可從 VS Code 停止；非零結束、缺少輸出或超過測試預算仍判定驗收失敗。

單元案例涵蓋結構恢復、UTF-16 位置、樣式邊界、中繼資料、衝突／循環、CLI 順序、語法作用域。Extension Host 涵蓋相依、語言、完成、暫留、連結、標籤、診斷及原生服務休眠。以 `VEROSCRIPT_TEST_RUNTIME` 指定相符 GUI Runtime，再設 `VEROSCRIPT_GUI_TEST_NATIVE=1` 要求原生檢查／編譯。具相容原生提供者的桌面另設 `VEROSCRIPT_GUI_TEST_PREVIEW=1` 要求兩影格標記預覽。Windows、Linux、macOS 以及空格／非 ASCII 路徑、無資料夾、多工作區、遠端及真實預覽視窗分別驗收。跳過案例不等於原生通過。

選用的 `VEROSCRIPT_TEST_DIRECTORY` 與主擴充套件一致，以絕對路徑指定隔離測試工作區及設定檔的父目錄。驗收原生套件環境時，將其放在 `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH` 所選專案內，使產生的 GUI 原始碼歸屬於該環境。無關的暫存工作區可能被原生原始碼歸屬檢查拒絕；請勿透過放寬編譯器存取範圍繞過測試配置不符。

每次原生驗收均應記錄 Runtime 來源或發行快照，舊報告不能驗證新原生程式碼。CLI 逾時屬於驗收失敗。編輯不得自動預覽。

## 編輯器中繼資料

將範例存為 `demo.veroscript-gui.json` 並加入 `veroscriptGui.schemaFiles`，僅作編輯提示。宿主仍須註冊真實型別與處理器。後面的資料取代同名完整型別，需含全部自有成員；基底使用完整名稱。可選 `routedEvents` 增加共用事件。

```json
{
	"schemaVersion": 1,
	"types": [{
		"name": "Demo::Card",
		"mark": "card",
		"bases": ["VS::Gui::Widget"],
		"properties": [{ "name": "Title", "mark": "title", "type": "string", "bindable": true }],
		"events": [{ "name": "Selected", "mark": "selected", "type": "std::multicast_delegate<void()>" }]
	}]
}
```

內建中繼資料從原生 `VeroScriptGui.vs` 進入點沿本機 `export import` 收集，輸入列於 `builtin.json`。保留未指定 `Mark` 別名的已註解控制項，確保複合控制項繼承完整。以 `npm run schema:update` 更新後審查原生常值及樣式／繫結旗標。一般建置檢查快照同步，並拒絕重複型別或遺漏基底類別。單元與 Extension Host 迴歸涵蓋複合標籤、繼承屬性／事件、位置值、CSS 提示及選取色彩。中繼資料檢查不代表原生執行驗收。 原生命名空間重構後保留完整限定的 `VS::Gui::Internal` 基底類別，維持繼承提示。

中繼資料擷取接受 `@` 與註解名稱之間合法的語言空白，例如 `@GuiWidget` 和 `@ GuiWidget`，兩者產生相同的屬性、事件與繼承資訊。原始碼排版變更後重新執行 `npm run schema:check`；擷取結果變更時使用 `npm run schema:update`。`builtin.json` 記錄來源路徑及中繼資料，不保存原始碼內容摘要。

## 發行

功能套件僅含 bundle、資訊清單、在地化、語法、Schema、圖示與 MIT 授權，不含原生二進位、工具、測試或開發文件。目前沒有 GUI 執行期第三方 JavaScript 相依，新增時須更新聲明。LICENSE 與倉庫一致，六種語系同步。本模組保留獨立 VSIX 打包；整合 IDE 打包已獨立登記 GuiVSCode，會建置基礎擴充套件，並用兩個本機 VSIX 聯合驗收。

本輪靜態回歸審查涵蓋 EOF／標籤邊界、數值範圍、帶空白賦值、關閉引號後重複屬性、內嵌 XML 註解及空大綱項。CSS URL 在快取的樣式解析中僅讀一次，支援原生大小寫與跳脫；連結要求共用範圍，不再重複遍歷不透明區間。準備設定工作可能查詢 CLI 版本；編輯及自動工作探索不啟動 GUI 命令。原生驗收以 VS Code 註冊提供者取得設定工作，使套件測試使用已安裝擴充套件。需要套件環境時，可將 `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH` 設為已準備環境的絕對路徑。回歸案例亦涵蓋與 JavaScript 原型成員同名之自訂標籤的屬性補全。隔離測試設定停用單字建議以免干擾擴充套件結果，並使用受支援的編輯器關閉命令。原生驗收須有相符 Runtime，且成功完成檢查、編譯及預覽。

請參閱[編輯器整合維護](../../../../../../../../Resources/Docs/Developer/zh-TW/editors.md#acceptance-evidence)。
發佈者以 `package.json` 的宣告為準，GUI 相依必須與主擴充套件標識一致。根 README 隨套件提供，作為詳細資料頁，不連結至未打包的倉庫檔案；功能套件文件過濾僅豁免 `extension/readme.md`。發佈 Marketplace 需要對應已註冊發佈者的權限並完成驗收，修改資訊清單不等於註冊發佈者。

建置時將[共用 256px 產品 PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png)複製為擴充套件顯示圖示。


[zh-TW](../../User/zh-TW/index.md) · [Documentation](../../README.md)
