# 開發者文件 — VS Code

編輯器與原生宿主的公共職責及六語言維護統一見 [公共編輯器整合](../../../../../../../../Resources/Docs/Developer/zh-TW/editors.md)。

Core、Tooling、VM、JIT 與 GUI 提供者應來自相符的原生原始碼／建置設定，包括 C++ 語言模式；僅產品版本相同無法證明 ABI 相容。SDK 整合案例透過補全與懸停涵蓋靜態泛型、handle 和巢狀型別接收者。選用的 `GetLastJitDiagnostics` 快照屬於 C++ 宿主 API，目前 LSP／DAP 線路協定不提供此介面。

執行時 UI 訊息採用 `vscode.l10n.t` 與 `Resources/Localization/Bundles`。`Resources/Localization/<locale>/manifest.json` 生成 `package.nls`；保持參數編號與清單引用一致。

使用 Node.js 22.18.0+ 與桌面 VS Code 1.106.0+，在模組目錄執行以下命令。單元測試涵蓋設定與介面資源；整合測試需要桌面 Electron/VS Code 和目前 Runtime。執行域協定 fixture 不執行 VM。檢查 package:contents 與 VSIX 授權；原生驗收、遠端宿主、真實應用執行域分別提供報告。

```sh
npm ci
npm run typecheck
npm run build
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
```

桌面 Extension Host 測試應逐套依序執行。多個 VS Code 測試視窗同時開啟時，可能爭用補全確認、復原及 Enter 案例所需的編輯器焦點。

原生參數測試允許每次 CLI 呼叫最多 180 秒，不注入未使用的宿主巨集，使參數傳遞驗證獨立於完整 GUI 冷編譯。巨集設定由專用的設定／通訊協定案例涵蓋，實際 GUI 編譯另行驗收。這些測試預算不改變 LSP 或偵錯期限。比較實際處理程序參數前，先正規化 JIT 測試路徑。

[Standards](../../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)



## 宿主整合與執行域協定

C++、UE 或其他宿主必須提供相容 TCP DAP 端點與真實原生繫結。內嵌範例的 RunDapTcp 使用 Stdio.LiveBindings 啟動 VeroScriptDapTcp::Run。生命週期、執行緒及斷開後的監聽由宿主負責。外掛偵錯指令碼 VM；原生程式碼使用對應偵錯工具。執行域附加需要下述宿主預備的偵錯橋接；不能接管已進入 JIT 的 VM。編輯時透過 bindingCatalogPaths 設定符合的中繼資料，並使匯入、巨集及標準函式庫與宿主一致。中繼資料不會向本機 Runtime 註冊真實回呼。附加命令讓宿主決定編譯選項，需要覆寫時在 launch.json 明確設定。設定範圍之外的宿主相依來源請自行儲存。位址由擴充功能宿主解析，包括遠端環境。

宿主須提供 IVeroScriptLiveDebugDomain，並在 initialize 宣告 veroscriptSupportsDomainAttach: true。缺少宣告會拒絕連線，不會退回載入指令碼；初始連線/initialize 期限為 10 秒。輸出記錄身分與不透明的原始碼/繫結修訂；暫不自動比較原始碼或同步 LSP 中繼資料。變數修改遵循 veroscriptCanSetVariable，不支援的暫停/修改請求會明確報錯。斷開傳送 terminateDebuggee: false；連線遺失會結束工作階段，不自動重連。宿主負責取消、生命週期及安全點排程，執行前準備偵錯橋接，並在目前單一活動內容模型中序列化偵錯呼叫。實際 UE 接入仍須另行驗證。

宿主能力標記也控制逐步執行、運算式求值和條件中斷點，省略的標記視為不支援。外掛更新 VS Code 可表達的對應能力，並明確拒絕不支援的請求；一般中斷點仍可使用。即使宿主宣告 veroscriptCanTerminate，停止/斷開也保留由宿主取消腳本的策略。 宿主發出 `terminated` 後，外掛仍完成編輯器的最終 `disconnect` 握手，並保持 `terminateDebuggee: false`。未完成操作只失敗一次，遲到回應及重複終止事件會被忽略；傳輸已關閉時，在本機確認最終斷開請求。

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/Developer/zh-TW/workspace-and-debug.md)

## 記憶體檢視契約

DAP 的 `memoryReference` 和 `veroMemory` 欄位應作為不透明的十進位字串保留，包括超出 JavaScript 安全整數範圍的值。變數檢視子項唯讀，控制代碼僅在目前暫停有效。轉送宿主能力宣告，不能從中繼資料推斷支援 `readMemory`／`writeMemory`。整合測試涵蓋原生記憶體型別的編輯、執行、檢查、偵錯及線上域中繼資料轉送；線上域協定測試替身不驗證真實宿主 VM。

## 用戶端排程與雙向 DAP

編輯器事件批次共用一次最新文件狀態同步；同步期間到達的事件和明確重新啟動請求保留到下一輪，目前輪失敗後仍繼續處理。線上域 DAP 會加入本機事件並重新編號輸出訊息，反向請求回應的 `request_seq` 必須映射回宿主原編號，也包括亂序返回。協定測試替身驗證編號映射，不實際啟動終端或子偵錯工作階段。

## 原生模組原始碼接入

`ModuleSources` 在語言用戶端邊界轉換 URI，並呼叫伺服器公布的 `moduleSourceMethod`。編輯器 URI 保留原始協定 URI、伺服器生命週期及編譯指紋；送回伺服器時僅使用原始 URI。唯讀檢視不進入原始碼覆蓋層，也不成為可寫入的編輯目標。缺失、已退役、已取消或已變更的快照均被拒絕，包括等待回應期間發生的變更。模組符號和診斷保留所屬用戶端歸屬。 自動重新啟動時，即使用戶端物件和編譯指紋未變，也必須建立新的原始碼生命週期。

將 `VEROSCRIPT_TEST_MODULE_LSP` 設為 `VeroScriptModuleEnvironmentToolingTests` 執行檔的絕對路徑，可在 Extension Host 測試中執行原生 `--lsp` 測試替身；未提供時會跳過，不能據此宣稱完成原生模組環境驗收。預設 Runtime 服務使用原生啟動選項：外掛不增加原生處理器的 JSON 設定，也不自動注入 CLI 套件環境。DAP 的 `sourceReference` 及凍結原始碼回應繼續由 VS Code 標準流程處理。

註記名稱比對、標準拼寫及簽章參數解析由原生工具處理，包括多行標籤、巢狀泛型及含逗號的字面值。用戶端透過 SDK 轉送結果。整合測試在不替換用戶端的情況下修改同一繫結目錄的 `caseSensitive` 策略，並透過 VS Code 套用 `source.organizeImports` 編輯，核對精確範圍、CRLF 保留及冪等性。

## 變數呈現與精確數值型別

透過 SDK 和執行域橋完整保留各 DAP 變數的 `presentationHint`，尤其是 `attributes: ["readOnly"]`。`supportsSetVariable` 和 `veroscriptCanSetVariable` 描述工作階段操作能力，不代表所有變數都可寫入；原生驗證仍是最終依據。回歸案例涵蓋不可變 `variant` 的子項、巢狀承載值、監看子項與一般區域變數的可編輯性，以及兩種執行域編輯能力設定下的提示轉送。

精確數值型別保留原生名稱與文字值。上下文 TextMate 案例涵蓋 `std::bigint`、`std::rational` 和 `std::numeric::rounding_mode`，不將其葉節點名稱保留為關鍵字。新增或修改的案例在使用相符工具完成型別檢查、真實 Extension Host 與 VSIX 驗證前仍屬未驗證，舊報告不能證明新原生程式碼已通過驗收。

## 配套擴充套件工具 API

`Public/api.d.ts` 定義工具 API 1。`getToolchain(scopeUri, sourceFile?)` 共用 Runtime、編譯參數、環境、執行後端／參數及來源範圍。scope URI 選擇工作區設定，可選來源檔案展開檔案變數。解析可能查詢 CLI 版本，僅明確執行時呼叫。`compileArguments` 不含命令、進入點或執行後端；呼叫方追加命令。GUI 檢查／編譯強制 VM 並省略 JIT 模組。VSIX 保留 `Public` 宣告，維持測試的 `ready`／`snapshot` 匯出。應使用相符 Runtime，在真實 Extension Host 與安裝後的 VSIX 中驗證此 API；原生 GUI 執行另有獨立驗收要求。

直譯器實作已獨立至 BytecodeVm 模組。搬移編輯器測試環境時保留完整發行 Runtime，包括 `Vm/`。原生 LSP/DAP 啟動層提供預設工廠；擴充套件不會加入新的線路協定欄位或推測自訂宿主的工廠。SDK 回歸涵蓋推導出的函式擷取暫留資訊、補全、參數提示、定義跳轉、UTF-16 範圍，以及不完整編輯後目前接收者的暫留資訊更新。

遷移原生模組測試夾具時，還應將 `VEROSCRIPT_TEST_VM_BACKEND` 設為 Runtime 的 `Vm/` 目錄中相符原生 VM 模組的絕對路徑。這會覆寫夾具建置時記錄的 VM 路徑；一般 Runtime 的 LSP/DAP 仍由原生啟動層提供預設工廠。

發佈者以 `package.json` 的宣告為準，GUI 相依必須與主擴充套件標識一致。根 README 隨套件提供，作為詳細資料頁，不連結至未打包的倉庫檔案；功能套件文件過濾僅豁免 `extension/readme.md`。發佈 Marketplace 需要對應已註冊發佈者的權限並完成驗收，修改資訊清單不等於註冊發佈者。

檔案圖示由[共用圖示產生器](../../../../../../../../Resources/Assets/icons/README.md#regeneration)重新產生。建置時將[共用 256px 產品 PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png)複製為擴充套件顯示圖示。

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
