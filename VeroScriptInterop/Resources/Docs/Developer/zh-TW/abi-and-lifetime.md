# Interop ABI 與生命週期

`VeroScriptInterop` 是 C11-compatible process ABI。public header 為 session、value、callable、package、buffer、call context、interception、generic host type、external serialization 定義 opaque handle。

## VM 後端選擇

`VS_INTEROP_CAP_VM_BACKEND_SELECTION` 用於探索 `vs_interop_session_set_vm_backend` 與 `vs_interop_session_get_vm_backend_info`。標準 Interop 產品明確組裝靜態位元組碼 VM；自訂建置可設定 `VEROSCRIPT_INTEROP_DEFAULT_VM=OFF`，再選擇與 Core 相符的外部 VM。後端選擇獨立於 VM/Mixed/JIT 執行策略，必須在首次執行前於建立者執行緒、工作階段閒置時設定；空路徑清除選擇，失敗保留原選擇。Core 一旦提交執行（包括 JIT 執行）即凍結選擇，`vs_interop_session_clear` 不會解除凍結。

查詢前初始化輸出的 `struct_size`，查詢失敗保留輸出。領域狀態使用 `VS_INTEROP_VM_SELECTED`、`VS_INTEROP_VM_FROZEN`，後端能力使用 `VS_INTEROP_VM_CAP_*`。文字檢視借用至後端替換或工作階段銷毀；靜態選擇的 `library_path` 為空。ApplyConfig 保留選擇，指令碼編譯擴充繼承工作階段 VM。

## version 與 status

ABI version、runtime semantic version、capability bit、execution backend、run mode、package kind、value kind、status code 是獨立 field。ABI query function 分別報告它們；binding 不得將 ABI value 當成 product version 顯示或比較。

目前 `VS_INTEROP_ABI_VERSION` 為 `0x00000101`（0.1.1），編碼為 `(major << 16) | (minor << 8) | patch`：主版本占 16 位元，次版本與修補各占 8 位元。C# 與 CPython 配接器要求完全相等；能力位元以及依 `struct_size` 追加的尾端欄位不能免除該版本檢查。ABI 變更須獨立於各業務套件版本進行審查。

每個適用 operation 回傳 ABI status。last-error buffer 用於 operation-level failure；structured diagnostic 經由 session diagnostic API 讀取，僅在 presentation boundary format。

## 所有權

opaque handle 依 function 是 explicit owner 或 borrowed reference。string/byte view 不擁有資源。ABI 回傳的 buffer 必須以 matching buffer function destroy，或在 owner release 前 copy。callback context、borrowed enumeration value 僅在 documented call 內 valid。

C header 會從 C/C++ compile/link，故 calling convention、struct layout、enum value、nullability、symbol name 都是 ABI 的一部分。即使 additive function 也需同步更新 header、implementation、test、adapter。

## 工作階段 JIT 快取擴充

`VS_INTEROP_CAP_SESSION_JIT_CACHE` gate session cache limit/statistic export。此 extension 向 `vs_interop_jit_policy` append `reuse_session_code`。為 original 32-byte layout 編譯的 caller 仍 valid，預設取得 enabled reuse。current caller 傳送 full structure，能將該 byte 設成 zero。這在允許 explicit opt-out 時保留 ABI 0.1.1。

程式碼重用適用於符合條件的普通 Run、EvalOnce 和函式呼叫，只保留編譯狀態，不保留 VM 全域變數或腳本物件。後端相容且繫結修訂號穩定時，持久工作階段也可重用編譯程式碼。變更攔截器或變更回呼會使保留程式碼失效。實際採用 VM 時略過此快取。受控執行可保留匹配的後端快取及治理身分，並共享累計編譯配額；編譯嘗試/快取項目策略預算為零表示對應計數不限額，原生程式碼及完整載荷上限仍有效。`vs_interop_session_set_jit_cache_limits` 使目前上下文失效；預設上限為 8 MiB/32 MiB，零表示禁止保留對應資源。受控 Mixed 預設走 VM；量測目標負載後可啟用 `enable_controlled_mixed`，明確 JIT 選擇獨立於此開關。檢查後端的 `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION` 和實際執行統計 `controlled_native_executions`；不支援或優先 VM 的入口繼續保留治理檢查。

`vs_interop_session_get_jit_cache_stats` 報告 current context 的 cumulative compile attempt、settled retained byte、resource-limit latch。它和 `vs_interop_session_get_last_jit_stats` 的 last-run delta 不同，也不是 process peak-memory limit。policy、limit、backend、module、相關 host observability 變更可 invalidate context/reset statistic。

## 新 context 與 channel 契約

channel operation 使用 value handle contract，不會把 value 變成 borrowed callback object。blocking/timed/non-blocking operation 有不同 symbol/result semantics；`close` 是 waiter synchronization point。host 不得從無關的 `VeroScriptStatus` value 推論 timeout/closed result。

generic host-type、call、interception、external serialization context pointer 在 callback duration 內 borrowed。accessor、mutator、error setter、continuation/invocation function 僅在 callback active 時 valid。return 後不可 retain context pointer、argument/field/byte view。callback exception/host failure 必須於 return 前轉成 context error channel。

callable handle 有 explicit retain、validity、invoke、destroy operation。owner-thread-only invocation 與 weak-`this` capture 是獨立 option，不會使 session generally thread-safe。callback 後仍需的 external serializer byte/reference visitor value 必須 copy。這些 rule 同樣適用 C、C++、managed adapter。


<a id="localization-abi"></a>

## 本地化 ABI

`vs_interop_locale_translate` 遵循正常初始化與程序語言選擇，配置模組所有的緩衝區。未知位元組（包含 NUL）保留，無效輸入清空輸出；每條路徑都須釋放緩衝區。同步 C 標頭與 `.def` 匯出，驗證原生檢查和六語言替換。記憶體配置失敗可能保留英文備援。

## 出版驗證

獨立與完整發行的 Interop 打包都會使用暫存 SDK 編譯並執行 C11 標頭測試，
連結每個公開函式。缺少匯出或匯入程式庫項目會拒絕打包。外部範例透過
`VEROSCRIPT_SDK_ABI_TEST_SOURCE` 啟用此檢查。Linux 共用程式庫測試也會在未載入模組的
反射錯誤路徑注入配置失敗，要求傳回 `OUT_OF_MEMORY` 且例外不越過 C ABI；
一般未載入模組的情況傳回 `ENGINE_ERROR`。

邊界測試也涵蓋版本查詢首次配置失敗（空 view、重試成功）、serializer 替換失敗
（保留原位元組）、無配置 multiset 計數及 memory guard 保留原始失敗 status。
UTF-16 view 配置失敗傳回 `OUT_OF_MEMORY`，不修改輸出參數；重試保留全部碼元，
後續讀取重用快取視圖。

Console 回呼附加到 session-options 前綴，不改變 C ABI 版本；僅讀取 `struct_size`
涵蓋的完整尾端欄位，舊 options 仍有效。query/control 描述終端操作，可選 write
回呼接收不附加換行的原始 UTF-8 位元組，遵循既有回呼生命週期與 worker 執行緒規則。
擴充測試在 session 最終解構時，從指令碼解構函式呼叫 native、interception 和 mutation
callback。host callback data 必須存活至解構完成。

巨集回歸透過公開 C ABI 建置、解析、匯入並執行包含再匯出物件式、函式式與可變參數
巨集的程式庫套件，同時涵蓋保留及移除偵錯資訊。測試確認建置時條件匯出、呼叫端
展開、私有巨集隔離、匯入錯誤的實體位置，以及目前 `VSBC` 讀取器拒絕
`VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` 載荷。

安裝後的原生註解 SDK 測試檢查凍結登錄表驗證、獨立 service retain/release、ApplyConfig
保留服務、建立執行緒限制，以及處理器和釋放回呼中的設定重入。它產生並呼叫方法，
驗證四種診斷級別與主要錯誤選擇，並確認清除服務會停用產生功能。原生建立函式需要
配套 Core C++ SDK；新增五個 C 匯出保持 ABI 0.1.1 與原有診斷配置相容。

測試也涵蓋 ApplyConfig 期間服務函式物件的複製與解構：拒絕設定重入，
複製配置失敗傳回 `OUT_OF_MEMORY` 並保留已安裝服務，成功重試清除舊錯誤。

迴歸也驗證複製、解構回呼中的一般函式呼叫、執行與原始碼替換均被拒絕，
並保留目前程式。設定描述元先於服務函式物件複製完成驗證，避免生命週期回呼
覆寫原始驗證錯誤。原生回呼內容呼叫仍支援合法的巢狀指令碼執行。

新增迴歸以舊的成功、退出旗標初始化結果，驗證重入被拒絕後寫回新的失敗結果
及會話診斷，也涵蓋 NULL 結果輸出。失效 callable 傳回失敗且保留傳回值。
同步銷毀會話時，包裝句柄存活至最終回呼結束，會話先標記為關閉；回呼中的
會話 API 存取與重複銷毀被安全拒絕。destroy 傳回後句柄無效。活動呼叫中的
延遲解構要求回呼資料存活至該呼叫傳回。

`vs_interop_session_retain_callback_user_data` 僅在成功時接管宿主協定回呼資料。
最後一個登錄表／值持有參考釋放後，釋放回呼僅呼叫一次，可能晚於工作階段解構並於其他執行緒執行。
後續一般註冊與泛型具現化重用相同資料時共用持有者；重複接管會被拒絕且不再次移轉所有權。
迴歸涵蓋逸出值複製、其他執行緒的最終釋放，以及逐次配置失敗後的成員註冊重試。

## 通用與腳本編譯處理

`VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING` 標識 `VeroScriptInteropCompileProcessing.h` 的原生服務。使用凍結的 `FCompileProcessorRegistry` 和 `CreateCompileProcessingService` 建立；retain/release 提供獨立 opaque handle。`vs_interop_session_set_compile_processing_service` 安裝設定，NULL 清除，ApplyConfig 保留服務。使用匹配的 Core/Interop 原生 SDK，並讓處理器程式碼與資料存活到最終釋放。設定要求建立執行緒且工作階段閒置；處理器及服務複製／釋放回呼中的重入被拒絕。

`VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS` 標識 `vs_interop_session_set_script_compile_options`。提供完整目前 `struct_size`；根、可選註冊入口、設定及允許目錄均被複製。路徑／入口名拒絕嵌入 NUL；重複設定鍵、無效指標／數量、reserved byte 與非法 bool 不取代已接受設定。上限為 64 個根以及 4096 個設定項／允許目錄。空入口名選擇 `register_extension`。

NULL 恢復核心預設值：無設定根、允許原始碼匯入、總計 1,000,000 指令和 2,000 ms。明確零執行限額表示無限制。停用原始碼匯入仍允許設定根。沿用覆蓋層、匯入與標準函式庫路徑；匯入標準函式庫原始碼的擴充需要包含 `compiler.vs` 的匹配目錄。編譯擴充在隔離 VM 執行，繼承工作階段取消控制；ApplyConfig 保留設定。業務綁定不會隱式暴露。產生的業務套件不執行生產者擴充；執行階段 JIT 執行產生的業務程式碼。

C 回歸驗證跨階段狀態、輸入複製、產生／診斷、無效輸入回復、預算及取消失敗與恢復、原始碼匯入策略和套件執行。安裝後的原生消費者驗證服務生命週期、設定保留及處理器／複製／釋放回呼中的重入。

## 已解析模組環境

`VS_INTEROP_CAP_MODULE_ENVIRONMENT` 表示支援模組環境橋接。使用相符的原生 Core/Interop SDK 建立不可變 `FModuleEnvironment`，再透過 `VeroScriptInteropModuleEnvironment.h` 的 `CreateModuleEnvironment` 包裝。C 端只傳遞不透明控制代碼：`vs_interop_module_environment_retain` 建立另一個獨立擁有的控制代碼，每個控制代碼釋放一次。設定成功後，工作階段獨立保留快照、資源、原生程式碼擁有者及相依契約。

`vs_interop_session_apply_config_with_environment(session, config, environment)` 套用完整設定並選取快照，NULL 表示移除。一般 `vs_interop_session_apply_config` 保留目前環境。兩者均要求閒置的建立執行緒；更換環境前必須 Clear 已載入程式。環境與本機繫結衝突時，在修改設定或取消控制前傳回 `ENGINE_ERROR`。環境擁有繫結目錄時，本機繫結修改傳回 `ENGINE_ERROR`，應在原生端預先組合不可變目錄；未提供目錄的環境仍允許一般 C 註冊。

載入、執行、套件及外部 JIT 使用 Core 的記憶體/檔案模組對應、編譯資源、當地語系化服務和精確外部相依校驗。本橋接使用已解析環境，不負責安裝套件或探索原生擴充。發布後的回呼保持不可變，並使用相符的原生 SDK 二進位檔。設定或工作階段最終銷毀時可能釋放擁有者；解構不得擲回例外，設定和執行重新進入會遭拒絕。Core 套用設定期間發生配置或內部失敗後，仍需重新套用完整設定再依賴其值。

後端解析遭拒時，會在提交設定或取消狀態之前回傳 `ENGINE_ERROR`，可使用有效環境重試。熱重載設定遭拒時同樣回傳 `ENGINE_ERROR`。有界回復仍處於 `RESET_REQUIRED` 時，須銷毀並重建工作階段後再執行。

<a id="interop-regression-targets"></a>

模組 CMake 門檻包括 `VeroScriptInteropTests`、`VeroScriptInteropExtendedTests`、`VeroScriptInteropTypeTests`、`VeroScriptInteropMemoryTests` 和 C11 `VeroScriptInteropCHeaderTest`；啟用時由 `VeroScriptInteropNativeSdkTests` 涵蓋已安裝的原生使用端。實際匯出須與公開 ABI 清單一致，另行取得的 C11 範例須針對解壓後 SDK 驗證。以相符的 Core／VM 檢查編譯服務壽命、錯誤執行緒拒絕、回呼與保留值，記錄真實結果及略過項目；測試原始碼存在不等於發行驗收通過。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/abi-and-lifetime.md) · [fr-FR](../fr-FR/abi-and-lifetime.md) · [ja-JP](../ja-JP/abi-and-lifetime.md) · [ko-KR](../ko-KR/abi-and-lifetime.md) · [zh-CN](../zh-CN/abi-and-lifetime.md) · [zh-TW](abi-and-lifetime.md)
