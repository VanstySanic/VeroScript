# 安裝與管理擴充套件

專案根相依項明確指定來源後，同名且未指定來源的傳遞相依項使用該來源，仍檢查各自版本與摘要約束；明確來源衝突仍報錯。離線安裝協定套件時，須先宣告本機 `vs-network` 封存來源，再安裝 `vs-http` 或 `vs-websocket`。

`veroscript-project.json` 儲存專案相依、套件啟用狀態與設定；散發套件根目錄使用 `veroscript-package.json`；建置圖使用 `veroscript-build.json`。三個檔案分別驗證各自格式與 Schema。

選用套件管理器將指令碼程式庫、資源及原生擴充安裝到明確的專案或 Runtime/Core SDK。分發物是以 .vspkg 或 .zip 命名的 ZIP，根含 veroscript-package.json；它不同於 .vslib 程式庫、.vsapp 應用及 .vsbc 快取。

## 選擇環境

命令預設以啟動目錄為專案根。--path 選擇其他專案；--root 選擇目前 CLI 所屬的已驗證 Runtime/Core SDK，--root --path 選擇另一已驗證元件。相對路徑以啟動目錄為準。專案/SDK 預設存放 .veroscript/packages，Runtime 使用 Packages。沒有使用者安裝範圍、隱含合併或執行時自動安裝。

## 命令

```sh
veroscript env --json
veroscript install ./library.vspkg --path ./app
veroscript install --path ./app --locked --offline
veroscript list --path ./app --json
veroscript show example-math --path ./app
veroscript disable example-math --path ./app
veroscript enable example-math --path ./app
veroscript verify --path ./app
veroscript uninstall example-math --path ./app
veroscript clean --path ./app
veroscript pack ./library --output ./library.vspkg
veroscript --path ./app ./app/main.vs
veroscript deploy --path ./app --output ./deployment
```

install 解析並安裝；--locked 保留鎖定，--offline 禁止連線。遠端產物需完整快取，本機封存來源仍須存在；已鎖定目錄快照可在原目錄移除後繼續使用。--upgrade 重新選版，不能與 --locked 合用。list/show 檢視、verify 驗證完整性；disable 保留檔案及版本鎖但不能破壞有效相依，enable 恢復選擇。clean 僅回收無參照、無活動租約且屬於本環境的舊世代。

pack 不覆寫既有輸出。請使用新輸出路徑，或先移動舊封存檔，再產生替代封存檔。

## 升級、解除安裝與重複安裝

```sh
veroscript install example-math --path ./app --upgrade
veroscript install --path ./app --upgrade
veroscript uninstall example-math --path ./app
```

第一條重新選擇指定根相依，第二條重新選擇整個相依圖；沒有獨立的 `update` 子命令。`--upgrade` 仍遵守宣告的版本限制及來源，不能與 `--locked` 併用。首次以未指定版本的名稱新增遠端相依時，會固定到實際選中的精確版本；要超出該固定版本，應修改版本要求，或明確安裝 `name@version`。本機目錄／封存相依仍使用本機來源。[本機範例](../../../Examples/README.md)可在不連線套件庫的情況下示範兩種升級命令。

未指定 `--upgrade` 時，優先重用仍符合要求的鎖定版本及相同套件內容。版本要求、來源或本機內容變更時，可能需要重新解析。重複安裝仍會建立新的安裝世代，並再次執行安裝動作、可信處理器及獲准的掛鉤，不是偵測到已安裝就略過所有處理。存在掛鉤時，每次執行仍需 `--allow-hooks`。`--locked` 限制鎖定結果，`--offline` 禁止連線，兩者都不會略過安裝處理。

`uninstall` 接受已宣告的根相依，移除對應相依宣告，並裁剪其他根不再需要的間接相依；仍被其他根使用的共用相依會保留。不能按名稱直接解除安裝僅作為間接相依存在的套件，應移除或調整依賴它的根。無參照的舊安裝世代可在活動租約釋放後另以 `clean` 回收。

## 執行與部署

執行、檢查、eval、REPL、watch、位元組碼封裝及受支援原始碼 AOT 使用選定環境。未明確選擇時，指令碼從入口尋找專案，eval/REPL 從啟動位置尋找，再回退到目前元件。LSP/DAP/Interop 接入由各介面卡負責，不自動共用 CLI 選擇器；DAP 拒絕未支援的明確選擇。AOT 仍需目標對應的原生橋接。

deploy 要求新輸出目錄，複製啟用的 runtime 相依閉包、資源、授權及可攜載入快照，不含僅 compile/development 相依。應用/.vsapp 另行提供。跨目標安裝僅驗證、複製目標資料，不執行目標程式庫；各平台須獨立驗收。

匯出已安裝的跨目標環境時，透過 `deploy --target <file>` 提供安裝時使用的完整目標描述檔，或呼叫 `FInstalledEnvironment::DeployForTarget`。匯出會驗證並複製相依閉包資料。載入部署以執行時仍須符合主控端目標。

## 設定、錯誤與語言

`recovery-conflict` 表示管理檔案與中斷交易日誌的舊值、新值皆不一致。外部編輯和 `transaction.json` 會保留；回復受阻時可能回報 `recovery-required`。先備份衝突檔案，依保留的日誌解決衝突後重試，不要直接刪除日誌。

--packages-dir、--cache-dir、--lockfile 明確覆寫位置。不要編輯儲存路徑遷移現有安裝，應在新環境重裝或部署。environment-changed 保留並行修改，應重新讀取後重試；layout-conflict 表示狀態/鎖/所有權路徑重疊；restart-required 表示存活原生實作不能與取代版本共存。

套件命令的 --lang 及可重複 --fallback-lang 選擇顯示文字，與編譯器診斷語言獨立。套件管理器內建診斷目前包含英文訊息及穩定 code 欄位，應依 code 分支。--json 區分機器結果與提示訊息。關閉選用管理器的建置不提供這些命令。

[撰寫分發套件](authoring.md) · [從 C++ 使用安裝環境](embedding.md)

## 從相依清單安裝

```sh
veroscript install -r requirements.txt
veroscript install --requirement "config/dependencies.txt" --path ./app
veroscript install -r requirements.txt --root
veroscript install -r requirements.txt --locked --offline
```

`-r FILE`、`-rFILE`、`--requirement FILE`、`--requirement=FILE` 可重複使用，僅適用於 `install`，也能與個別套件參數並用。檔名不限，無須 `.txt` 副檔名。命令列清單路徑以啟動目錄為基準，獨立於 `--path`／`--root` 的安裝位置。清單中的巢狀清單、本機目錄和封存檔以各自所在清單的目錄為基準。支援絕對路徑、UTF-8 和空格；包含空格或註解字元的路徑請加引號。引號中的 Windows 反斜線保持原樣，也可使用正斜線。

UTF-8 清單支援 LF、CRLF 和可選的 UTF-8 BOM：

```text
# Registry requirements use VeroScript versions.
gui==0.1.1
math >= 0.1.1, < 0.2.0 # both comparisons apply
widgets@0.1.1-beta.1
-r "lists/common.txt"
"../archives/local library.vspkg"
tools @ "../tools"
assets @ https://example.com/assets/download
```

每個邏輯行為一項相依或本機 `-r`／`--requirement` 包含。忽略空白行及 `#` 註解；行內註解的 `#` 前需有空白且不在引號內。行尾未跳脫的反斜線以空格接續下一實體行，之後再移除註解。同名項目取版本限制交集，明確來源或設定不同則報錯。具名直接來源核對實際套件名稱。本機路徑換算至所選專案後存入 `veroscript-project.json`，URL／封存檔摘要照常記錄。保留其他專案相依，明確項目更新對應根相依；整批共用一次求解及安裝交易。`--locked`、`--offline`、`--upgrade` 和掛鉤授權維持既有語義。失敗不提交部分結果，清單遭並行修改會使待提交計畫失效。

版本比較支援 `==`／`=`、`<`、`<=`、`>`、`>=`；逗號或空格代表 AND。支援原有 `name@<限制>` 與 `name@*`。裸套件名沿用既有專案相依；首次新增的無版本登錄來源套件固定選中版本。直接來源使用帶空格的 `name @ <路徑或URL>`。無斜線的目錄請用 `./目錄` 或引號區別於套件名。

此格式借鑑 pip，採用 VeroScript 名稱與版本；不接受 Python extras、環境標記、VCS／editable、限制檔、`~=`、`!=` 或 pip 選項。包含必須為本機檔案，不展開環境變數、`~`、shell 跳脫或萬用字元。登錄來源、鏡像、憑證和權限由現有 CLI／宿主選項提供。循環包含、無效 UTF-8 和語法錯誤附檔案／行號診斷；讀取、深度、檔案與項目數、累計中繼資料、取消和期限沿用現有限額。必須明確指定清單；普通 `install` 仍讀取 `veroscript-project.json`，不自動搜尋文字清單。

### 離線範例

將[本機套件範例](../../../Examples/README.md)的 `library` 目錄複製到 `workspace/vendor/library`，建立以下兩個檔案，再從 `workspace` 執行命令。列表結果包含 `example-math`，相依資訊會儲存至目標應用的 veroscript-project.json 與鎖定檔。

`workspace/requirements.txt`:

```text
# Local package list
-r lists/local.txt
```

`workspace/lists/local.txt`:

```text
"../vendor/library"
```

```sh
mkdir app
veroscript install -r requirements.txt --path ./app --offline
veroscript install --requirement=requirements.txt --path ./app --locked --offline
veroscript list --path ./app --json
```

包含循環、無效編碼及資源超限會被拒絕。診斷在可用時提供來源檔案與起始實體行；`requirements-changed` 表示提交前清單已變更，請重新讀取檔案後重試。

明確的 URL 項目在 `--locked` 或 `--offline` 下重用同一 URL 已記錄的摘要；具名參照也須符合套件名稱。快取位元組仍會重新驗證，未知或變更的 URL 不會借用其他 URL 的快取身分。離線重複安裝需要保留專案相依記錄及完整、可驗證的封存快取。

首次安裝在建立專案／狀態紀錄前中斷也會傳回 `recovery-required`，請使用明確的套件寫入操作完成復原。有效且已完整套用的 `committed` 日誌不阻止讀取。`combine-processing` 表示生成物或原生載入器對應不同相依／設定，應在同一環境安裝所需相依圖。`pack` 不取代既有輸出，包括同時建立的目標。

套件儲存及管理檔案拒絕符號連結和特殊項目。macOS 僅在 root 所有的 `/tmp`、`/var`、`/etc` 指向預期 `/private/...` 目標時允許這些系統別名作為一般暫存專案或快取的上層路徑。套件封存項目及所選儲存內部連結仍遵循原有拒絕規則。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
