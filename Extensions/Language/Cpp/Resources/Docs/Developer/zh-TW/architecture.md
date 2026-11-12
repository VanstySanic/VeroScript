# C++ 組架構

## 依賴方向

C++ 組是 `VeroScript/` 之上的介面卡和工具鏈層。`VeroScriptCpp` 提供嵌入與會話操作，`VeroScriptCppAot` 直接依賴核心。`VeroScriptCppFrontend` 擁有共享 C++17 宣告模型；`VeroScriptCppBindGen` 和 `VeroScriptCppTranspiler` 獨立消費它。任何消費者都不得為了自己的降級策略改動前端 AST。

核心不包含 C++ 工具頭。`VeroScriptInterop` 是獨立穩定 C ABI，不是 C++ 前端、BindGen 或 Transpiler 的實現依賴；嵌入層可以使用核心公開的執行時和繫結介面。

## 原始碼佈局

每個原生模組從 `Public/` 匯出受支援標頭檔案，並將實現與測試放在 `Private/`。CMake 遞迴發現私有原始碼、從庫目標排除專門測試源、另建測試可執行程式。公共標頭檔案必須能在公共頭測試中編譯，不能依賴私有 include 路徑。

## 共享輸出模型

前端診斷含程式碼、訊息、源位置和 token/宣告資料；BindGen 和 Transpiler 在結果物件中保留這些診斷。專案操作必須收集所有輸入後才生成目錄、引用、原始碼對映或清單。輸出輔助程式拒絕同一檔案路徑，並且僅在完整操作成功後替換檔案。

## 變更規則

公共 struct、enum、源位置、生成檔案格式、CMake 目標或支援子集規則改變時，必須更新專門測試和相關主題頁。README 可說明命令，但公共標頭檔案和測試才是行為依據。

## 原生偵錯邊界

`VeroScriptSessionDebug.h` 定義供宿主或 Tooling 執行域使用的 C++ Session 橋接。Session 管理橋接註冊，並在實際執行的 VM 周圍保留共用偵錯器。程式取代、回復與銷毀會通知橋接；清理用 VM 不繼承偵錯器。程式、繫結、原始碼與執行內容視圖皆為借用資料。宿主複製同步中繼資料，並在安全停止點的執行緒上處理待執行的偵錯命令。

Cpp 不依賴 Tooling。Tooling 負責執行域登錄、DAP 與傳輸生命週期；UE 整合負責編輯器／PIE、物件生命週期與遊戲執行緒暫停策略。此公開邊界改變 Session 的 C++ 記憶體配置，原生宿主須以對應 SDK 重新編譯。

`VeroScriptCppSessionDebugTests` 涵蓋引數修改、物件呼叫、保留回呼、直譯器選擇、重新載入回復、例外清理及橋接取代／移除。`VeroScriptCppJitContractTests` 使用獨立公開工廠驗證後端轉換。SDK 使用範例只使用匯出標頭與程式庫。TCP 範例驗證使用有界等待與結束的子行程，不可分離借用區域 Session 的伺服器執行緒。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/architecture.md) · [fr-FR](../fr-FR/architecture.md) · [ja-JP](../ja-JP/architecture.md) · [ko-KR](../ko-KR/architecture.md) · [zh-CN](../zh-CN/architecture.md) · [zh-TW](architecture.md)
