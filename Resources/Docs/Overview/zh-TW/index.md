# VeroScript 綜合總覽

[GitHub 原始碼儲存庫](https://github.com/VanstySanic/VeroScript)

這份總覽按使用情境串聯語言、工具鏈、宿主 SDK 和維護文件。詳細語義、API 與限制以連結到的模組參考為準；總覽維護跨模組的關係與閱讀順序。

## 跨平台部署

| 部署方式 | 目標設定 | 指南 |
| --- | --- | --- |
| 桌面 Runtime 與 SDK | Windows (x86, x64, ARM64); Linux (x64, ARM64); macOS (x64, ARM64) | [安裝與分發](../../../../VeroScriptInstaller/Resources/Docs/User/zh-TW/index.md) |
| 行動裝置嵌入 | Android、iOS（實驗性適配器） | [行動裝置 SDK](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/zh-TW/index.md) |
| 瀏覽器與 JavaScript 宿主 | WebAssembly | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md) |

請選擇符合目標作業系統與 CPU 架構的發行套件。套件提供範圍及驗收狀態隨產品與版本而異；相依性、可用後端與部署要求請查閱對應產品的平台說明。

## 選擇閱讀路線

| 目標 | 從這裡開始 | 後續參考 |
| --- | --- | --- |
| 撰寫和執行腳本 | [概述](../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md) → [語言基本特性](../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/index.md) | [CLI](../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md)、[標準函式庫](../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/index.md)、[範例索引](../../../../VeroScript/Resources/Docs/User/zh-TW/examples.md) |
| 安裝、更新或散佈 | [安裝與散佈](../../../../VeroScriptInstaller/Resources/Docs/User/zh-TW/index.md) | [套件管理](../../../../VeroScriptCli/Resources/Docs/User/zh-TW/packages.md)、[CLI 參考](../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md) |
| 將腳本嵌入宿主 | 按下表選擇 SDK | [Interop 邊界](../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md)、[宿主整合](../../User/zh-TW/toolchain/index.md) |
| 編輯、診斷和偵錯 | [CLI 參考](../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md) | [LSP](../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/lsp.md)、[偵錯](../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/debugging.md)、[診斷本地化](../../../../VeroScript/Resources/Docs/User/zh-TW/localization.md) |
| 開發或維護實作 | [開發閱讀路線](#開發閱讀路線) | [全部開發文件](../../Developer/zh-TW/index.md) |

先確認安裝的元件與版本，再選擇執行後端、宿主繫結和套件設定。命令及 SDK 範例需要對應元件；參考文件不能代替實際建置和執行驗收。

## 選擇宿主 SDK

| 宿主 | 使用者參考 | 維護入口 |
| --- | --- | --- |
| 原生 C++ | [C++ SDK](../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/index.md) | [C++ 模組](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-TW/index.md) |
| C ABI 或其他語言 | [Interop SDK](../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md) | [ABI 與生命週期](../../../../VeroScriptInterop/Resources/Docs/Developer/zh-TW/abi-and-lifetime.md) |
| CPython | [Python SDK](../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/index.md) | [Python 模組](../../../../Extensions/Language/Python/Resources/Docs/Developer/zh-TW/index.md) |
| .NET / C# | [C# SDK](../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/index.md) | [C# 模組](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/zh-TW/index.md) |
| 瀏覽器或 JavaScript | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md) | [WebAssembly 模組](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/zh-TW/index.md) |

值、控制代碼、回呼、執行緒及釋放規則應查閱所選 SDK 的契約。跨語言轉換工具支援的原始碼子集、AOT 限制及目標環境也以各模組說明為準，不能由語言名稱推斷相容範圍。

## 開發閱讀路線

1. 從[核心架構與管線](../../../../VeroScript/Resources/Docs/Developer/zh-TW/architecture-and-pipeline.md)和[執行與狀態](../../../../VeroScript/Resources/Docs/Developer/zh-TW/execution-and-state.md)理解編譯、執行與狀態邊界；涉及後端和並行時繼續閱讀 [JIT 與並行](../../../../VeroScript/Resources/Docs/Developer/zh-TW/jit-and-concurrency.md)。
2. 沿宿主邊界閱讀 [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/zh-TW/index.md)、[C++ 工作階段](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-TW/session.md)及相應語言配接器文件。
3. 沿工具鏈邊界閱讀 [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/zh-TW/index.md)、[工具協定](../../../../VeroScriptTooling/Resources/Docs/Developer/zh-TW/architecture-and-protocols.md)。
4. 發佈前核對[建置與資源](../../../../VeroScript/Resources/Docs/Developer/zh-TW/build-and-assets.md)、[安裝程式建置與打包](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-TW/build-and-bundle.md)及[發行驗收流程](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-TW/release-validation.md)。

既有驗收結果只適用於記錄的原始碼、產物和環境。產品建置、完整性驗證與執行驗收狀態的記錄方式見[發行驗收流程](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-TW/release-validation.md)；證據保存在被忽略的輸出目錄。

## 文件與出版

完整模組目錄見[使用者入口](../../User/zh-TW/index.md)。文件歸屬、六語言維護、驗證命令和 All/User 打包流程統一維護在[文件維護指南](../../Developer/zh-TW/index.md)。

出版文件之間的連結和錨點支援離線閱讀；原始碼引用及外部網站需要網路。使用說明和教學選擇 User，維護指南與規範選擇 All。兩種出版套件均不包含 History。

[使用者文件](../../User/zh-TW/index.md) · [開發文件](../../Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
