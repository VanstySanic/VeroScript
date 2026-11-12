# 在地化資源與部署

本頁面向在地化目錄維護者與建置、打包工具開發者。語言選擇、自訂目錄與 SDK 使用見[診斷在地化](../../User/zh-TW/localization.md)。

## 原始碼歸屬與部署

原始碼中的本地化資源由所屬模組或區域維護，統一位於 `Resources/Localization/<culture>/`。只有多個 Python 工具共用的訊息放在 Python 區域的共享目錄。建置與打包依元件相依關係彙整到部署目錄 `localization/`：Runtime/Core SDK 攜帶 CLI、Tooling、LSP、DAP 與 Interop 所需資源；WebAssembly 攜帶核心與 C++ 宿主資源；Python 套件攜帶核心、C++、Interop 與 Python 工具資源。IDE、安裝程式介面資源保持獨立。

```text
VeroScript/Resources/Localization/<culture>/
  language.json, runtime.json, shared.json, host-binding.json, memory.json, i18n.json
Extensions/Language/Cpp/VeroScriptCpp/Resources/Localization/<culture>/embedding.json
VeroScriptInterop/Resources/Localization/<culture>/interop.json
VeroScriptTooling/Resources/Localization/<culture>/
  tooling.json, lsp.json, dap.json
Extensions/Language/Python/Resources/Localization/<culture>/python-tools.json
Extensions/Language/Python/VeroScriptPythonFrontend/Resources/Localization/<culture>/python-frontend.json
Extensions/Language/Python/VeroScriptPythonBindGen/Resources/Localization/<culture>/python-bindgen.json
Extensions/Language/Python/VeroScriptPythonTranspiler/Resources/Localization/<culture>/python-transpiler.json
Extensions/Language/Python/VeroScriptPythonAot/Resources/Localization/<culture>/python-aot.json
VeroScriptInstaller/App/Resources/Localization/<culture>/
```

直接從原始碼倉庫執行時，核心發現自己的目錄，各 SDK 或工具模組補充自己的 Resources/Localization。明確載入的目錄和部署目錄保持權威；成功替換目錄後，不會自動混入原始碼資源。

## 原始碼資源探索

`VERO_LOCALIZATION_DIR` 指定完整的本地化根目錄。未指定或無法載入時，核心依序嘗試以下位置，在首個有效目錄樹處停止：

1. 可執行檔旁的 `localization`，然後是相對於可執行檔目錄的 `../assets/localization`；
2. 可執行檔目錄的最多八層祖先：每一層先嘗試 `localization`，再從該目錄本身或儲存庫的直接子目錄識別原始碼模組，讀取其核心本地化目錄；
3. 目前工作目錄的 `localization`；
4. 在目前目錄、父目錄、祖父目錄依序識別原始碼模組；識別成功後讀取模組內的 `VeroScript/Resources/Localization`。

原始碼探索接受包含 `VeroScript/Public/VeroScriptVersion.h` 的模組目錄，或包含 `CMakeLists.txt` 且恰有一個此類直接子目錄的儲存庫。外層目錄可以改名；缺失或有歧義的原始碼模組會被忽略。`InitializeModuleFromEnvironment` 僅在初始化已選擇原始碼目錄後，補充相對於原始碼模組的 `Resources/Localization`。成功載入的明確指定目錄或部署目錄保留自己的資源集合。

## 發佈檢查

- 每個發行套件都必須包含完整且有效的 `en-US` 目錄；
- 其他需要支援的語言必須隨目標宿主複製、嵌入或包裝；
- 翻譯文化的檔案路徑和訊息 ID 必須是英文基準的子集；
- 不要使用舊的 `en.json`/`zh-CN.json` 網域優先佈局，目前格式是 `<culture>/<component>.json`；
- 診斷 ID 比顯示文字穩定，宿主、LSP 和 DAP 應使用 `MessageId` 做邏輯判斷；
- 打包後應驗證可用語言集合、缺失鍵的英文回退和腳本參數佔位符替換。


## 行動資源與本地化

公共行動目錄清單、部署路徑和明確載入流程統一維護在 [Mobile](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/zh-TW/index.md#mobile-localization)。

翻譯緩衝區所有權、錯誤與匯出維護契約見 [Interop](../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-TW/abi-and-lifetime.md#localization-abi)。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/localization-and-resources.md) · [fr-FR](../fr-FR/localization-and-resources.md) · [ja-JP](../ja-JP/localization-and-resources.md) · [ko-KR](../ko-KR/localization-and-resources.md) · [zh-CN](../zh-CN/localization-and-resources.md) · [zh-TW](localization-and-resources.md)
