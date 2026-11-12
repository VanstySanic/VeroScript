# C++ 構建與測試契約

## 目標

C++ 組透過 `VeroScriptCpp`、`VeroScriptCppAot`、`VeroScriptCppFrontend`、`VeroScriptCppBindGen` 和 `VeroScriptCppTranspiler` 構建。每個庫目標排除專門測試源；測試連結其所驗證的公共目標。

## 測試類別

測試覆蓋公共頭編譯、引擎/會話整合、JIT 整合、AOT 源與 Bundle 輸出、前端解析/合併/型別對映、BindGen 目錄/引用生成，以及 Transpiler 源/對映生成。凡是改變公共結果、生成物、診斷或支援子集規則的修改，都必須擴充套件相應類別，不能只依賴端到端測試。

## 安裝邊界

公共頭和生成庫是安裝輸入；README 和生成 API 引用是文件，不是實現頭。已安裝的消費者不得需要構建樹路徑。

獨立分發的 C++ 包在所需 `Runtime` 模組下提供 `VeroScriptCppConfig.cmake`。消費者將匹配的 Core SDK 根和 C++ 包的 `Runtime` 目錄同時加入 `CMAKE_PREFIX_PATH`，呼叫 `find_package(VeroScriptCpp <version> EXACT CONFIG
REQUIRED)`，並連結 `VeroScript::VeroScriptCpp`。可選 `Tools` 與 `Aot` 元件僅在其同級模組已安裝時匯出匯入目標。

C++ 包記錄同一釋出構建中 Core 庫的 SHA-256 身份。若混用來自另一構建的同版本 Core SDK，配置必須失敗。釋出流水線在製作歸檔或安裝器前，必須只使用暫存包路徑編譯並執行 `Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal`，不得使用倉庫 include/library 路徑。

## 記憶體整合門檻

`VeroScriptCppMemoryIntegrationTests` 涵蓋 Engine 配額、Session 連續呼叫、原生記憶體服務、熱重載身分保留及配額失敗回復、關閉域之前的腳本清理，以及 Session 銷毀後的租借釋放。
`CppMinimal/memory.cpp` 是安裝後的 Runtime 消費程式。修改記憶體契約時，須同時執行這兩項、`VeroScriptMemoryTests`、Session／偵錯／JIT 測試和 AOT 拒絕測試。可選後端參數用於驗證真實 JIT 模組。參見[宿主記憶體規則](../../User/zh-TW/embedding.md)。

## 清理與 AOT 回歸門禁

`VeroScriptCppSessionDebugTests` 涵蓋清空、替換、關閉熱重新載入、釋放 callable 和解構時的清理回呼重入保護，包括拒絕新增保留根及清理後重用。`VeroScriptCppJitContractTests` 檢查結構化原生註冊的快取失效。發佈前須使用 `VEROSCRIPT_AOT_REQUIRE_NATIVE=1` 執行 `VeroScriptCppAotHelloTests`，實際編譯執行產生的 C++ 並與 VM 對照，驗證自增／自減的副作用次數、生命週期、泛型數值寬度及溢位失敗。`VeroScriptCppAotEmitTests` 也驗證不支援的使用者型別自增／自減會明確拒絕，包括泛型特化。

## 模組巨集整合門檻

`VeroScriptCppEngineIntegrationTests` 涵蓋宿主巨集種子、原始碼覆蓋、明確／轉匯出／隱式可見性、原始碼及檔案快取、相依失效、熱重載、相依失敗後保留舊程式、實體匯入診斷，以及移除偵錯資訊的程式庫巨集介面。`VeroScriptCppAotHelloTests` 對公開可變參數巨集和呼叫端展開執行原生／VM 對照。`CppMinimal/main.cpp` 透過暫存 SDK 標頭使用公開巨集中繼資料。模組前置處理或位元碼介面變更時須執行這些門檻。

## 註記處理整合門檻

`VeroScriptCppAnnotationTests` 在 VM／Mixed／實際 JIT 下驗證 Engine 檢查及執行、Session 原始碼／檔案載入與熱重載、非致命診斷、相依快取及不可快取的處理器。也驗證原始碼載入、檔案載入、獨立編譯及重載期間拒絕回呼重入，以及取消後的狀態保留與重用。`VeroScriptCppAotEmitTests` 檢查提高生成預算後不會被第二次預設準備拒絕；原生 `VeroScriptCppAotHelloTests` 對照生成虛方法正文、跨檔案泛型正文及宣告／實作範圍。SDK 使用範例透過匯出的標頭、處理器登錄及 Session／AOT API 驗證安裝配置。

## 專案轉譯與路徑交易門禁

`VeroScriptCppTranspilerTests` 驗證最終模組邊界、方法實作作用域、虛呼叫、預設參數及原檔案診斷；`VeroScriptCppTranspilerNativeTests` 實際編譯執行 C++ → VeroScript → C++ 的專案鏈路。`VeroScriptCppFrontendTests` 涵蓋未支援語法的明確拒絕、路徑正規化、硬連結去重及交易寫入前的別名碰撞保護。批次解析、CLI 輸入保護與輸出交易共用 `FFilePathSet`；無法讀取檔案身分時保留原有比較邏輯。

## 終端宿主整合門檻

`VeroScriptCppConsoleTests` 在 VM／Mixed／實際 JIT 下檢查終端能力、精確文字輸出與 `print` 分組、權限拒絕、宿主回呼失敗、執行錯誤及指令耗盡後的樣式／游標恢復，以及 Session 重複呼叫和一般輸出串流。`VeroScriptCppAotHelloTests` 的原生／VM 對照檢查顏色與游標控制參數的求值順序，包括驗證失敗前的副作用。暫存 SDK 的 `CppMinimal/main.cpp` 驗證公開宿主介面及執行結束後的恢復；終端契約變更時須執行這些門檻。

原生門檻也檢查有／無命令列參數的入口在終端介面卡異常、標準及非標準異常、診斷串流失敗時回傳失敗並完成清理。

工作執行緒門檻也檢查終端守衛建構中的標準／非標準例外會使任務失敗，並確保終端恢復完成後才通知任務完成。

## AOT 求值與參考門禁

`VeroScriptCppAotHelloTests` 的原生／VM 對照涵蓋一般、泛型、擴充及建構呼叫的參數順序與轉換、值快照、ref 參數、短路運算、複合指派及全域 callable 初始化。函式值在參數之後求值，執行個體接收者先求值，與 VM 一致。後續參數擴充或重新繫結容器時，索引參考必須保持有效；索引轉換失敗時不得繼續執行後續參數的副作用。純量表達式維持直接產生，擷取的值參數透過移動傳遞，避免額外複製。

賦值回歸還檢查保存的接收者銷毀後，運算式結果仍是有效的值。簡單索引寫入將保存的容器寫回原變數或欄位；巢狀線性索引及限定全域變數則解析目前儲存路徑。右側重新綁定時，別名和物件身分必須與 VM 一致。簡單索引賦值在右側副作用之後檢查邊界，參考路徑則先轉換索引。無需轉換的純量索引寫入維持直接產生，避免複製定長陣列。

`CppMinimal` 消費範例在每次建置時，將相符 Core SDK 的標準函式庫和本地化資源部署到執行檔旁，包括不需重新連結的增量建置。部署必須恢復遺失檔案並更新已變更的 SDK 資源，在 Visual Studio 和 Ninja/Make 產生器下皆成立。

請參閱[C++ AOT — 參考回傳與求值生命週期](../../User/zh-TW/aot.md#reference-return-lifetime)。

長期索引別名和索引參考回傳需要託管儲存路徑。從暫存值接收者取得的長期參考也需要託管擁有者。這些情況會被 AOT 明確拒絕，請使用 VM 後端。

隱含結構體欄位會執行其腳本欄位預設初始化，包括泛型欄位型別和定長陣列。儲存初始化不會執行結構體建構函式正文。

原生門禁驗證區域欄位別名保留、無參數呼叫的接收者生命週期、參考賦值目標只求值一次，以及數值參考的溢位和窄化。產生測試要求需要清理的接收者和回傳區域別名被明確拒絕。成員、解構、靜態欄位及開放模板的定義位於完整型別配置和全域存取器宣告之後；原生門禁涵蓋前向全域存取，以及具約束模板的成員和靜態欄位。

門禁還驗證直接及巢狀欄位別名、for 初始化中的參考、成員及捕獲的接收者、泛型欄位和結構體欄位預設儲存。產生測試明確拒絕長期索引儲存和暫存值接收者別名。遞迴清理檢查在可能註冊泛型特化的查詢之前保存型別名稱快照。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/build-and-tests.md) · [fr-FR](../fr-FR/build-and-tests.md) · [ja-JP](../ja-JP/build-and-tests.md) · [ko-KR](../ko-KR/build-and-tests.md) · [zh-CN](../zh-CN/build-and-tests.md) · [zh-TW](build-and-tests.md)
