# 文件維護

[GitHub 原始碼儲存庫](https://github.com/VanstySanic/VeroScript)

<a id="documentation-ownership"></a>

## 正本與維護歸屬

| 內容 | 維護正本 |
| --- | --- |
| 公共編輯器整合 | [編輯器整合維護](editors.md) |
| 公共閱讀路線、跨模組關係、教學和本專案開發與貢獻規範 | 根目錄 `Resources/Docs` |
| 模組行為、公開 API、預設值和限制，以及模組架構、建置與維護 | 所屬模組的 `Resources/Docs/User/<locale>` 和 `Developer/<locale>` |
| 多媒體模組共用的提供者維護說明 | [多媒體維護](multimedia.md) |
| 匯出的 All/User 目錄與 ZIP | 生成產物；修改連結指向的正本後重新出版 |

每個主題、每種語言只維護一份正本。入口頁可摘要並連結，完整契約、操作流程和會變動的預設值由所屬位置維護。六語言並行正本同步更新；跨模組共用流程歸此處，協定和 SDK 細節歸所屬模組。文件舊版本由版本控制保留。

`documentation.json` 登記集合的語言、模組清單和配套資源；各模組的 `manifest.json` 將本模組 User 正本對應至穩定出版路徑，Developer 頁面仍按所屬模組歸集。同一來源檔案在 All 集合中只出版一次；User 套件從中投影相同的 User/Tutorials 內容。出版副本不是另一份維護正本。

## 本地化與路徑

包括 Overview 在內，六語言保持相同頁面檔名和語義；API 名稱、診斷 ID、JSON 鍵、命令和可執行程式碼保持原樣。既有驗收結果只適用於記錄的原始碼、產物及環境。

模組清單將 User 正本對應到穩定出版路徑。所包含頁面和資源間改寫為相對連結，保留語言、錨點、編碼路徑、查詢參數、圖片和參照式連結，程式碼及 HTML 註解保持原樣。僅屬於原始碼的引用跳轉線上儲存庫。舊文件透過版本控制查看，不複製到出版包。

維護文件和本地化檔案時，遵循[共用檔案換行規則](../../Standards/zh-TW/GENERAL.md)。此儲存庫格式約定不會改變語言和工具對 CRLF 輸入的支援。出版流程產生正規化副本，不改寫維護正本或二進位資源。

## 驗證與出版

從儲存庫根目錄執行下列命令。每次出版都從目前正本重新集成，修改來源頁面後無須同步產生副本；僅提交維護的正本。集成期間若正本變更則拒絕出版，無論成功或失敗都會清理暫存目錄。CI 驗證語言對齊、檔案集合、出版位元組和連結。Markdown/JSON 出版文字使用 LF；原始授權聲明、二進位資源和正本保持原始位元組。

```powershell
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
python Resources/Tools/Packaging/veroscript_docs.py publish --audience All --output .tmp/docs-preview
python Resources/Tools/Packaging/veroscript_docs.py verify-publication --audience All --output .tmp/docs-preview
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -OutRoot .tmp/docs-collection-package
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -Audience User -OutRoot .tmp/docs-user-package
```

`build` 和 `publish` 的輸出必須是儲存庫 `.tmp` 或 `dist` 內的專用空子目錄。原始碼 `Release` 與既有受管理集合下禁止直接輸出；Resources 透過交易式業務發佈器發佈。每次預覽使用新目錄；重複匯出獨立 ZIP 時，使用 `package-veroscript-docs.ps1`，它會先暫存並驗證新套件，再替換舊產物。

<a id="integrated-publication-views"></a>

`package-veroscript-docs.ps1` 預設匯出 All：現行 Overview、User、Developer、Standards、完整 Tutorials 與連結資源。`-Audience User` 將參考發布至 `docs/<locale>/...`，完整教學發布至 `docs/Tutorials/<locale>/...`，並包含其相依資源。兩種讀者均不包含 History。

此腳本匯出獨立文件，不發佈至受管理產品集合。預設輸出為 `dist/docs/<Audience>/<version>/`，指定 `-SourceRoot` 時為 `dist/docs/Custom/<version>/`；可用 `-OutRoot` 指定其他專用輸出目錄。完整 Resources 發行仍透過產品發佈器整合 User 視圖。

完整發行透過 Resources 於 `Release/Resources/any/<version>/Resources/Docs/` 發布 User 視圖，與 `Resources/Scripts`、`Resources/Assets` 並列。目前 Runtime/SDK/擴充歸檔與安裝程式不附帶編寫的文件與範例；酬載政策請參閱 Installer Developer 的 [release-layout.md](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-TW/release-layout.md)。

[本專案開發與貢獻規範](../../Standards/zh-TW/README.md) 僅適用於 VeroScript 儲存庫開發與貢獻，不是 SDK 使用要求或 MIT 授權的附加條件。`documentation.json` 的 `standardsAudience` 固定為 `Developer`：Standards 僅作為開發者資料進入 `All`，不進入 `User`、Resources 中的使用者文件、Runtime/SDK/擴充套件發行包或安裝程式。原始碼著作權與授權聲明繼續保留。

本專案開發與貢獻規範維護在 `Resources/Docs/Standards/<locale>`：`zh-CN` 定義規則，其他五種語言保持等價。出版直接讀取正本。本獨立儲存庫沒有 IDE 規範鏡像；`sync-standards` 僅在透過文件設定發現 ValenceStudio 使用方時更新受管參考副本，否則不會建立鏡像。

需要 Python 3.11+。`check` 驗證原始文件和 已發現的規範使用方；`check-collection` 驗證暫存完整集合；`build/verify` 預覽 User 擷取；`publish/verify-publication` 從目前正本出版並核驗 All 或 User。自訂 `-SourceRoot` 匯出明確提供的目錄，不能與 `-Audience` 混用。缺失或越界連結、缺失錨點、空目錄樹、檔案系統連結和特殊項目會被拒絕；來源與輸出不能重疊。ZIP 包含隱藏資源與空目錄，替換失敗時復原舊產物，回復失敗則保留復原備份。線上原始碼引用需要連網；HTML 呈現器須轉換 `.md` 連結並保留標題 ID。產品建置、承載內容驗證與執行驗收狀態記錄見[發行驗收流程](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-TW/release-validation.md)。

## 模組清單

頂層 `Resources/Docs/documentation.json` 中的 `$module/` 前綴表示所屬原始碼模組的根目錄。`VeroScript` 等產品目錄名稱保持原樣；各模組 manifest 內的路徑仍相對於自己的 Docs 目錄。移動或改名外層目錄時，還須更新人工文件中的命令及儲存庫工作流程使用的實際路徑。供 Windows PowerShell 5.1 使用的指令碼包含非 ASCII 文字時須保留 UTF-8 BOM。

```json
{
  "schemaVersion": 1,
  "module": "VeroScriptCli",
  "user": [
    {
      "source": "User/{locale}/index.md",
      "output": "{locale}/toolchain/cli.md"
    },
    {
      "source": "User/{locale}/packages.md",
      "output": "{locale}/toolchain/packages.md"
    }
  ],
  "developer": true
}
```

## 文件類型與內容

User 面向使用者的工作、公開 API 行為、設定與限制；Developer 面向架構、實作責任、建置、測試和出版；Tutorials 按前提、步驟及預期結果教學；Standards 僅收錄維護者已決定的本專案開發與貢獻規範。舊計畫和驗收證據保存於版本控制或暫存輸出，不能證明變更後工作區已驗收。

文件本文、標題及檔名避免具體日曆日期與非必要個人識別資訊。通用說明直接描述行為，不反覆標註目前發行版本；相容性、工具要求、ABI 或格式契約、可執行範例、產物選擇及原始授權聲明所需的準確版本仍須保留。變動中的識別碼優先連結維護正本，不重複抄錄。附日期的日誌及驗收記錄放入被忽略的暫存輸出。User 不重複測試方法或內部實作細節。

## 連結與讀者邊界

User 中的相對連結應落在 User 檢視、Tutorials 或已登記的配套資源內。User 到 Developer 或 Standards 的相對連結會阻止 All 與 User 兩種出版。伺服器實作、維護與驗收流程放在 Developer，User 說明使用者可觀察的行為；Developer 可以連結對應的 User 契約。修改跨模組連結後，應檢查原始文件集與兩種出版套件。

## 模組參考

| 類別 | 參考文件 |
| --- | --- |
| 語言與工具 | [VeroScript](../../../../VeroScript/Resources/Docs/Developer/zh-TW/index.md) · [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/zh-TW/index.md) · [Tooling](../../../../VeroScriptTooling/Resources/Docs/Developer/zh-TW/index.md) |
| 宿主 SDK | [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/zh-TW/index.md) · [C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-TW/index.md) · [Python](../../../../Extensions/Language/Python/Resources/Docs/Developer/zh-TW/index.md) · [C#](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/zh-TW/index.md) · [WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/zh-TW/index.md) |
| 執行引擎 | [Bytecode VM](../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/Developer/zh-TW/index.md) · [AsmJit](../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/zh-TW/index.md) |
| 套件與應用程式 | [PackageManager](../../../../VeroScriptPackageManager/Resources/Docs/Developer/zh-TW/index.md) · [Application](../../../../VeroScriptApplication/Resources/Docs/Developer/zh-TW/index.md) · [Build](../../../../VeroScriptBuild/Resources/Docs/Developer/zh-TW/index.md) · [Installer](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-TW/index.md) |
| 編輯器 | [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/zh-TW/index.md) · [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/zh-TW/index.md) · [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/zh-TW/index.md) · [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/zh-TW/index.md) |
| 嵌入平台 | [Mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/zh-TW/index.md) · [Android](../../../../Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Docs/Developer/zh-TW/index.md) · [iOS](../../../../Extensions/Platform/Mobile/VeroScriptIOS/Resources/Docs/Developer/zh-TW/index.md) |
| 資料與網路 | [JSON](../../../../Modules/Network/VeroScriptJson/Resources/Docs/Developer/zh-TW/index.md) · [Network](../../../../Modules/Network/VeroScriptNetwork/Resources/Docs/Developer/zh-TW/index.md) · [HTTP](../../../../Modules/Network/VeroScriptHttp/Resources/Docs/Developer/zh-TW/index.md) · [WebSocket](../../../../Modules/Network/VeroScriptWebSocket/Resources/Docs/Developer/zh-TW/index.md) · [Boost](../../../../Modules/Network/Backend/VeroScriptBoost/Resources/Docs/Developer/zh-TW/index.md) · [Curl](../../../../Modules/Network/Backend/VeroScriptCurl/Resources/Docs/Developer/zh-TW/index.md) |
| 多媒體與 GUI | [Platform](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/zh-TW/index.md) · [Graphics](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/zh-TW/index.md) · [Audio](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/zh-TW/index.md) · [Image](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/zh-TW/index.md) · [Font](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/zh-TW/index.md) · [Gui](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/zh-TW/index.md) · [Sdl3](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-TW/index.md) · [Stb](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/zh-TW/index.md) · [FontBackend](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/zh-TW/index.md) · [RmlUi](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/zh-TW/index.md) |

同儲存庫現行指南之間使用相對連結，不能用線上 URL 繞過讀者界線或讓已包含文件相依於網路；原始碼引用保持既有行為。

<a id="current-publication-policy"></a>

All 和 User 均只出版現行文件。History 目錄、舊計畫和驗收快照不屬於出版輸入；歷史修訂由版本控制保留，確切驗收證據放在被忽略的暫存輸出。原始碼或自訂 SourceRoot 含 History 路徑時會拒絕出版，大小寫變體也不例外。按所選讀者整合 Overview、User、Developer、Standards、完整 Tutorials 及其相依資源。

<a id="module-entrypoints"></a>

模組根 README 介紹用途、關聯模組、文件和授權入口；Resources/Docs/README 負責六語言及讀者導覽，也是整合出版使用的入口。完整使用說明歸 User，架構、建置和驗證歸 Developer，分步驟教學歸 Tutorials。語言工具子模組連結到所屬工具組的文件正本。入口保持簡短，檔案移動時同步相對連結，不在 README 維護另一份完整手冊或驗收記錄。

<!-- docs-navigation -->

---

[開發文件](index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
