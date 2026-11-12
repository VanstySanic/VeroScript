# 套件管理器維護

用戶端負責取得、ZIP、解析、收據、交易、發行及環境組合。核心負責已解析模組/資源/原生契約，不依賴 HTTP、封存或解析器。伺服器負責帳戶、授權、不可變儲存、原則及索引。

## 建置與歸屬

桌面預設 VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON，embedded/mobile/Wasm/Python 精簡建置預設關閉。固定的靜態 curl、miniz、nlohmann/json、PicoSHA2 及授權由模組維護。VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL 或既有 CURL 目標要求 CURL 7.85+ 及相符執行相依。內附 curl 原始碼固定 SHA-256。

VeroScriptPackageManager 安裝元件匯出標頭、程式庫、Schema、相依身分與法律聲明，不再安裝範例和 README；六語言文件透過 Resources 出版。使用內附 curl 時也安裝其建置元件，Core SDK 封裝會執行兩步。正式封裝隨後依據發布的 Core SDK 版本及實際暫存 Core 程式庫的 SHA-256 確定相依資訊，包含移除符號後的程式庫；僅比較原生產品版本或沿用較早建置的摘要不足以證明相符。外部 Core SDK 使用端測試也會建立 HTTP transport，實際驗證套件管理員 ZIP/HTTP 連結相依項目。原始碼 API 的英文診斷與結構化 code 契約仍獨立於套件顯示快照。

## 交易與壽命

先暫存原始碼快照及產物，再公布不可變內容，以可恢復日誌提交要求/鎖/狀態/所有權。提交前複核規劃輸入及儲存身分，保留外部修改並拒絕過時規劃。恢復/回復不能沿用已取消權杖，讀者須拒絕未完成交易。

每代保留跨處理程序檔案租約。clean 檢查收據、全部目標設定、所有權、內容及活動租約，保留被修改或外部檔案。恢復僅能回收具完整所有權收據及租約中繼資料的已公布孤兒世代，遺棄暫存由宿主另管。回呼為同步借用，不得重入同一環境；原生實作經最終解構保持快照/模組租約。

交易重放在寫入前驗證完整日誌（唯一且允許的目標、string/null 資料及單筆預算）與目前檔案。檔案須符合日誌的 `old` 或 `new`；其他內容回報 `recovery-conflict`，保留日誌及外部編輯。每次寫入前再次驗證，略過已還原的值；回復受阻時以 `recovery-required` 保留交易。備份編輯並還原符合日誌的值，完成復原後再套用編輯。`JournalRecoveryTests` 涵蓋部分提交、回復、檔案缺失、無效日誌、重複路徑與寫入期間的編輯。

## 掛鉤編譯

每次預檢／執行前編譯都將 `FLimits.HookConstantEvaluationLimits` 傳給核心 `FCompileOptions.ConstantEvaluationLimits`。此為宿主策略，不寫入套件 JSON 或鎖定檔。工作量、呼叫深度、運算式深度和求值記憶體須為正數，允許零快取。編譯遵守整個操作的期限／取消，保留宿主分析觀察器與原始取消診斷；VM 執行限額獨立生效。`ConstantHookTests` 涵蓋原始碼／二進位庫匯入、泛型捨棄分支、uint64、限額、一次性取消、零快取重試及解除安裝交易。

## 驗證

建置 VeroScriptPackageManagerTests，執行 VeroScriptPackageManagerCliTests 與 VeroScriptPackageManagerSdkTests。涵蓋不可變來源、離線快取、目標解析、選用用途、封存與名稱、認證範圍、語言/格式、型別化/秘密輸入、掛鉤限額、中斷交易、並行編輯、清理租約、原生分析/靜態/動態註冊、明確 JIT 及部署閉包。SDK 使用端須以不相符核心驗證拒絕；平台/裝置驗收獨立進行。

```sh
cmake -S . -B build/package-manager -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON
cmake --build build/package-manager --config Release --target VeroScriptPackageManagerTests VeroScriptCli
ctest --test-dir build/package-manager -C Release --output-on-failure -R VeroScriptPackageManager
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

[存放庫協定](registry-protocol.md) · [使用者文件](../../User/zh-TW/index.md) · [結構定義](../../../Schemas) · [範例](../../../Examples)

## 相依列表檔案

`PackageRequirements.cpp` 在環境變更前展開位置參數與 `FRequest.RequirementFiles`。保留 UTF-8 驗證、包含循環偵測、來源檔案/起始行診斷及 MaxManifestBytes/MaxMetadataBytes/MaxFiles/MaxDependencyDepth/MaxEdges 預算；僅合併來源與設定相容的重複約束。保留輸入摘要並於提交前重驗，並行編輯回傳 requirements-changed。列表內容不得啟用 hooks 或改變登錄服務政策。涵蓋 CLI 短/長選項、C++ 請求、巢狀及引用本機路徑、約束求解與 locked/offline 重播。協定欄位與診斷 code 保持穩定；列表內容屬於使用者輸入，不是本地化目錄。

## 相依清單服務

`FRequest.RequirementFiles` 提供與 CLI `-r`／`--requirement` 相同的安裝專用服務。頂層相對路徑需要 `Environment.WorkingDirectory`，巢狀路徑以所在清單目錄為基準。`Private/PackageRequirements.cpp` 在管理檔案寫入前解析輸入，以 `FLimits`／`FWork` 限制 UTF-8 讀取、包含、項目數和累計中繼資料，安裝提交前復核來源摘要。正規化相依與來源存入 `veroscript-project.json`／`veroscript.lock`，普通鎖定專案無須附帶原清單；再次明確使用 `-r` 則仍需指定清單及本機封存來源。不變更核心、登錄端點或格式版本。程式庫、CLI 和已安裝 SDK 驗收須涵蓋[清單用法](../../User/zh-TW/index.md)。

## 環境與產物完整性

唯讀選擇會拒絕中斷的首次安裝，即使專案／狀態紀錄尚未建立。殘留 `committed` 日誌須通過身分、目標及全部目前檔案與 `new` 位元組的驗證後才可讀取；讀取者不刪除日誌。首次寫入前依最長階段驗證日誌預算。

線上更新將產物 `signatures` 視為不可變鎖定中繼資料。`pack` 和 `deploy` 以原子操作發布暫存產物，不取代既有目標。檔案可安全改用硬連結；目錄須由系統原生支援不覆寫的重新命名。不支援時明確失敗，不改用覆寫。

環境組合重用快照已驗證的相依邊，包括已裁掉僅編譯期相依的部署圖。安裝處理結果要求自身設定及完整已選相依上下文符合；無安裝處理的原生載入器僅要求實際引用的外部程式庫提供套件具有相同不可變身分。不符合時傳回 `combine-processing`，應在同一環境安裝所需相依圖。無關腳本相依變動不會使載入器失效。

部署會同時在套件值與專案快照中保留各套件設定，作為 Overlay 重用時保持一致；部署輸出目錄允許結尾分隔符。`list` 和 `show` 在狀態檔案建立前也會拒絕中斷的首次安裝。提交前驗證所有已寫及保留的管理紀錄，期間不再呼叫宿主回呼，繼續遵守原期限。復原保留外部編輯；與已寫紀錄衝突時保留復原日誌。

## 應用與建置接入

`VeroScriptPackageRuntime` 提供環境載入、部署校驗、資源與原生註冊，不依賴 HTTP、求解、安裝、鉤子或發布。`FClientOptions.ArtifactDecoder` 驗證歸檔，`FCoreEnvironmentOptions.ArtifactDecoder` 讀取受保護庫；私鑰和回呼不寫入鎖定檔。要求簽章的政策也適用快取。

部署預設保留 runtime，原始碼應用需要 runtime＋compile。讀取者保留共享 `AcquireDeploymentDirectoryLease`，發布者替換前取得獨佔租約。`Build::FServices` 透過 `Deploy` 與 `LeaseDeployment` 回呼接入。

註冊源 URL 不變；制品可增加非空的 `signatures` 陣列，最多 16 項，綁定 distribution、實際長度、SHA-256、演算法與金鑰身份。上傳申請、提交、查詢、確認、恢復、鎖定與鏡像完整保留；伺服器重新計算摘要和長度。既有制品不可修改，同版本追加必須保持元資料一致、目標不重疊、原子性與冪等性。不傳送私鑰；不支援欄位時明確拒絕。

`veroscript install name --application-options /absolute/trust-options.json` 指定絕對信任設定路徑。本地使用 `<archive>.sig.json`；直接 URL 不猜測簽章下載位址，改用註冊源記錄或明確下載的本地檔案。安裝 ZIP 保持可讀，應用容器不會自動視為安裝套件。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
