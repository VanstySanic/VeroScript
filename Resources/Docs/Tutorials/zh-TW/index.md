# VeroScript 教學課程

這裡的內容面向「跟著做出一個結果」的學習過程。每篇教學都盡量從一個小目標開始，逐步增加程式碼，並在結尾給予練習；語言規則和完整 API 仍以旁邊的[文件](../../User/zh-TW/index.md)為準。

## 開始之前

你需要準備：

1. 已安裝 VeroScript 命令列工具，並能在終端機執行 `veroscript --version`。
2. 一個用來儲存腳本的工作目錄。
3. 能夠編輯純文字檔案的編輯器。

程式碼區塊標籤標明語言或終端機類型。`powershell` 指令在 PowerShell 執行，`sh`／`bash` 指令在對應 shell 執行，並依作業系統調整路徑；`text` 展示預期輸出。完整程式通常使用 `int main` 並明確返回結束狀態；不需返回狀態的短範例也可使用 `void main`。

`--check` 檢查程式而不呼叫應用程式 `main`；已啟用的[腳本編譯擴充](../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/compiler.md)仍可在編譯器上限與權限政策下執行。

## 推薦學習路線

### 第一階段：執行第一個腳本

- [01：第一個腳本](getting-started/01-first-script.md)
- [02：檢查、執行和讀取命令列參數](getting-started/02-cli-and-arguments.md)

### 第二階段：掌握語言骨架

- [03：變數、條件與迴圈](language/03-values-and-control-flow.md)
- [04：函式與容器](language/04-functions-and-collections.md)
- [05：結構體、類別與介面](language/05-structs-classes-and-interfaces.md)
- [06：引用、引用傳回和 Lambda 擷取](language/06-references-and-captures.md)
- [07：模組、匯入和預處理器](language/07-modules-and-preprocessor.md)

### 第三階段：完成實際任務

- [08：字串、檔案與路徑](standard-library/08-strings-files-and-paths.md)
- [09：並行與事件](standard-library/09-concurrency-and-events.md)
- [20：讀取與複製二進位資料封包](standard-library/20-managed-memory.md)
- [22：格式化應用程式文字並保留快照](standard-library/22-application-i18n.md)

### 第四階段：交付與整合

- [10：建置套件並發佈應用程式](toolchain/10-packages-and-distribution.md)
- [11：除錯與 AOT 產生](toolchain/11-debugging-and-aot.md)
- [12：宿主綁定與腳本整合](advanced/12-host-bindings.md)
- [13：執行階段反射](advanced/13-reflection.md)
- [16：WebAssembly SDK 互動式輸入](toolchain/16-web-interactive-input.md)
- [17：可選值、弱引用和共享結構體控制代碼](advanced/17-value-utility-types.md)
- [18：選擇和使用 C++ 工具鏈模組](cpp/index.md)
- [Python 原始碼工具](python/index.md)
- [C# 原始碼工具](csharp/index.md)
- [Tooling 工作流程：連接編輯器與偵錯器](toolchain/17-tooling-workflow.md)
- [19：在行動應用程式中執行指令碼](toolchain/19-mobile-embedding.md)
- [21：安裝本機擴充套件](toolchain/21-package-manager.md)
- [23：建置程式庫與應用程式](toolchain/23-application-build.md)
- [24：集合、導覽與對話方塊](toolchain/24-gui-collections.md)


### 第五階段：標準化項目

- [14：使用 `std` 標準命名空間](advanced/14-standard-namespace.md)
- [15：設定工作區與診斷本地化](toolchain/15-workspace-and-localization.md)

## 如何使用教學課程

建議先依序完成前兩階段。第三階段的教學可以根據任務選擇；第四階段適合已經能獨立編寫腳本之後閱讀。

每篇教學都可以單獨閱讀，但不要跳過命令列中的 `--check`：它能在執行前發現語法、類型和匯入問題。完成教學後的練習比複製程式碼更重要，即使只修改一個輸入或增加一個分支，也能幫助你確認自己了解程式碼。

## 參考資料

- [語言基本特性](../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/index.md)：尋找語法和語意。
- [標準函式庫參考](../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/index.md)：尋找函式簽章與邊界條件。
- [工具鏈參考](../../User/zh-TW/toolchain/index.md)：尋找 CLI、套件、偵錯和宿主整合選項。
- [可執行範例索引](../../../../VeroScript/Resources/Docs/User/zh-TW/examples.md)：把教學中的主題與倉庫範例對應。

<!-- docs-navigation -->

---

[教學](index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
