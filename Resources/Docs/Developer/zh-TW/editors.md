# 編輯器整合維護

本指南負責編輯器用戶端與原生執行／協定宿主之間的公共邊界。用戶端實作和資源格式仍歸下面各模組的指南。

編輯器客戶端負責設定、傳輸與 IDE 展示；語意分析和 LSP/DAP 契約由原生 Tooling/Core 管理。繫結目錄僅提供中繼資料。執行域測試使用協定 fixture，不能證明真實 VM 安全點或應用/UE 宿主可用。執行前安裝 Session 偵錯橋，由宿主串行調試上下文並管理取消。`jitCapability` 是服務契約；一般 Run 可明確傳遞相容模組路徑，也可繼承 CLI 模組環境的選擇（未設定預設為 auto）；LSP、Check、本機 VM 偵錯不載入 JIT。

UI 與文件的六語言同步維護，保留命令選項、訊息 ID 與協定值，並按用戶端實際採用的格式器驗證預留位置。User 說明操作，Developer 說明用戶端架構、建置與發行；SDK、Runtime 及平台相容性以所選版本和真實驗收證據為準。

## 用戶端專屬實作

- [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/zh-TW/index.md)
- [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/zh-TW/index.md)
- [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/zh-TW/index.md)
- [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/zh-TW/index.md)

## 公共原生契約

- [Tooling / LSP / DAP](../../../../VeroScriptTooling/Resources/Docs/Developer/zh-TW/workspace-and-debug.md)
- [C++ Session / DAP](../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/session-debugging.md)

<a id="acceptance-evidence"></a>

每輪驗收記錄作業系統／架構、編輯器與 Runtime 版本、完整原始碼識別、套件雜湊、真實結果及略過項目。原生執行還需要相符 Runtime 的獨立驗收。歷史結果僅適用於其記錄的產物及環境。

<a id="editor-icon-inputs"></a>

## 共用圖示輸入

共用輪廓及輸出系列由 `Resources/Tools/Assets/generate-icons.cjs` 維護。產品圖示使用 `#60A5FA`，檔案標記使用 `#5D75AE`，編輯器字形保留各自的佈景主題顏色及原生對齊。完整重新產生會更新產品／檔案的 SVG、PNG、ICO、ICNS 及編輯器 SVG；`--brand-only` 更新產品系列。Node.js、`sharp` 及 `--sharp-module` 用法見[資源重新產生步驟](../../../Assets/icons/README.md#regeneration)。Runtime 打包使用已維護的檔案。

VS Code 各擴充套件在建置時從產品 PNG 更新展示 PNG。JetBrains 中含 V 的 SVG 由共用產生器維護，Gradle 將外掛清單圖示放入 `META-INF`，其他圖示資源保留在 `Icons`。UI 六語言之間保留資源路徑、佈景主題後綴及資訊清單參照。具體複製與打包細節由各模組指南維護。

VS Code 將模組根 README 作為擴充套件詳細資料頁隨包提供，JetBrains 使用外掛描述檔中的介紹及 vendor 欄位。產品展示保持簡潔，並連結六語言文件選擇頁；公開手冊及維護步驟繼續由各自正本和出版視圖負責。

<!-- docs-navigation -->

---

[文件維護](index.md)

[en-US](../en-US/editors.md) · [fr-FR](../fr-FR/editors.md) · [ja-JP](../ja-JP/editors.md) · [ko-KR](../ko-KR/editors.md) · [zh-CN](../zh-CN/editors.md) · [zh-TW](editors.md)
