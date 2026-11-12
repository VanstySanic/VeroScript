# 後端維護

請參閱[核心 JIT 服務與並行](../../../../../../../VeroScript/Resources/Docs/Developer/zh-TW/jit-and-concurrency.md#native-abi-version)。

Core 負責執行契約、公開 JIT 契約、執行階段服務、驗證、排程和同代計量。具體 IR、降低、原生發碼、可攜式 Linear 執行和可執行儲存歸 `Modules/Execution/VeroScriptAsmJit`，不歸 Core。`Private/JitSupport` 實作通用快取和模組載入器。二進位後端相容性由公開標頭指紋及編譯器／STL／執行程式庫／平台交握決定，不能僅憑產品版本判斷。 位元組碼解譯器歸獨立的 `VeroScriptBytecodeVm` 模組。

## 建置與 ABI

`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` 建置不含具體後端的 Core。設為 `ON` 後，`VEROSCRIPT_ENABLE_ASMJIT=ON` 啟用原生發碼，`OFF` 保留可攜式 Linear。原始碼宿主連結 `VeroScriptAsmJit` 並注入 `CreateAsmJitBackendFactory()`；`VeroScriptAsmJitBackend` 產生不含 Core 副本的共享模組。匯入的發碼目標須提供所需的相容 PIC，其擁有者部署共享相依項。Windows 動態載入要求 `/MD` 或 `/MDd`。

```cmake
target_link_libraries(MyHost PRIVATE VeroScriptAsmJit)
```

## 執行階段契約

區分編譯結果 `Ready / Unsupported / Deferred / Failed` 與呼叫結果 `NotEntered / Returned / ControlTransfer / Failed`。只有 `NotEntered` 允許 VM 重播。發布前由 Core 驗證所有權、服務及預算。可執行成品保留程式碼及模組所有權；呼叫內容、框架和值僅在呼叫期間借用。巢狀呼叫後重新取得檢視；每次快速路徑判斷重新讀取即時錯誤／控制／回呼旗標。

Session 程式碼重用資格、執行治理、保留限制與記帳統一維護在 [C++ Session](../../../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-TW/session.md#session-jit-cache)。

## 發布驗證

`veroscript_jit_query` POD 交握在建立 C++ 工廠前檢查 API、公開標頭指紋、編譯器／STL、C++ 模式、執行程式庫與平台。工廠、工作執行緒和可執行成品存活時保留程式庫。驗證純 Core、注入契約、不相容程式庫、明確檔案／目錄選擇、不重播的退回、資源回收和 Session 快取失效。用 `package-veroscript-jit.py` 打包；獨立版本不能免除 ABI 相符。iOS 真機和模擬器均禁止動態載入及原生發碼；靜態可攜式後端須另行驗證。

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/zh-TW/jit-and-concurrency.md)

## 保留位元組配額

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 為零不允許保留對應位元組；與表示無限制的零策略預算不同。

官方後端歸檔發行至 `Release/Modules/Execution/Jit/<target>/<version>/archives/`。Modules 不再建立新獨立安裝程式；功能酬載保留執行相依項及法律聲明，不含文件、範例、測試、效能基準及符號。原始碼文件正本仍放於 Resources/Docs，透過 Resources 整合文件出版。

## 內附相依套件維護

AsmJit 由本模組負責，原始碼保留於 `ThirdParty/asmjit`，保留[原始授權條款](../../../../ThirdParty/asmjit/LICENSE.md)，於 [VERSION.txt](../../../../ThirdParty/asmjit/VERSION.txt) 記錄上游修訂。不要將第三方原始碼放入 Resources/Docs 或複製到 Core。`VEROSCRIPT_ENABLE_ASMJIT=OFF` 為已建置的後端選擇可攜實作；`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` 則不建置本後端模組。

更新相依套件時替換內附修訂，保留原始聲明及程式碼產生資料庫的授權條款，同步 VERSION.txt 與[模組聲明](../../../Licenses/NOTICES.md)中逐字保留的授權正文，重新設定並執行 JIT 與封裝測試。維持編譯器/STL/執行階段相容，驗證原生產碼與可攜兩種設定。後端安裝元件將 LICENSE.md 與 VERSION.txt 匯出至 `licenses/asmjit/`，須與所屬原始檔逐位元組比較。公共聲明索引位於 Resources/Licenses/README.md，相依聲明由使用該相依項的模組維護。

## 受控純量區塊與部分迴圈覆蓋

受控短整數迴圈可由原生 CFG 與 Core 原始純量區塊執行；每條位元碼仍檢查並計量限額、取消與逾時，不批次預支計數。EnableScalarBlocks 在入口前拒絕自訂宿主執行檢查和變更觀察器。純量表示不適用時在當前指令恢復普通服務，不重複檢查計數，也不重播函式。複雜迴圈仍依成本選擇 VM。

型別一致的字串 += 參與普通 CFG 驗證；未最佳化文字迴圈不會遮蔽同一函式中已有原生實作的非文字迴圈。受控 Mixed 的明確啟用規則不變。

短小、無分支的整數接收者運算式可優先使用 Core 的有界呼叫計畫，以減少原生呼叫橋接成本。該選擇發生在正常後端編譯／驗證之後；失敗、變更觀察器及執行治理仍走既有 VM 路徑。較大主體、呼叫及迴圈繼續獨立選擇後端計畫。

`bPreferVmForNestedExecution` 僅適用於無執行治理的直譯器呼叫者；宿主直接呼叫與原生呼叫者保留原生入口。

請參閱[核心 JIT 服務與並行 — 並行與驗證](../../../../../../../VeroScript/Resources/Docs/Developer/zh-TW/jit-and-concurrency.md#construction-dispatch)。

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
