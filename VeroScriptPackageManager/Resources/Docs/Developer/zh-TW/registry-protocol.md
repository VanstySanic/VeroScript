# 存放庫協定

註冊來源驗證封存時須讀取 ZIP 根的 `veroscript-package.json`，專案設定不能取代套件清單。API 路由、JSON 格式識別與格式版本 `0.1.1` 維持一致。

用戶端協定為 0.1.1，使用明確 API 基址、HTTPS、UTF-8 JSON 及一致 sourceId。本文描述伺服器須實作的介面，不代表已託管服務。

官方 CLI 的 API 基底位址為 `https://veroscript.org/api`，邏輯 `sourceId` 為 `veroscript.official`，下列路由接在此基底位址之後。專案／鎖定檔來源及明確的 API、鏡像選項遵循現有選擇規則；嵌入宿主自行設定來源。

| 方法 | 路徑 | 契約 |
| --- | --- | --- |
| GET | `/search?q=...` | `sourceId + search JSON` |
| GET | `/packages/{name}` | `veroscript-registry-package` |
| GET | `/packages/{name}/versions/{version}` | `veroscript-registry-version` |
| GET | `/artifacts/{id}/download` | `ZIP` |
| POST | `/uploads` | `veroscript-upload` |
| PUT | `{upload.url}` | `ZIP` |
| POST | `/packages/{name}/versions` | `mode=append + sourceId/name/version/dependencies/conflicts/artifacts/idempotencyKey` |

版本物件 veroscript-registry-version 含共用 dependencies/conflicts 及 artifacts，產物含 id/size/sha256/manifestSha256/target/requires，可選 blocked。套件物件 veroscript-registry-package 含 formatVersion/sourceId/name/versions，可選 withdrawn/blocked 狀態。上傳回應 veroscript-upload 含 uploadId/method=PUT/url，可選 headers/expiresAt。 產物 ID 為非空 ASCII 字母、數字及 `.`、`_`、`-`，且不能為 `.` 或 `..`；倉庫產物統一為 ZIP 歸檔，不使用 `kind` 欄位。

同版只能是一個 portable 產物或互斥目標產物，不得 portable/native 模糊重疊。所有產物的正規名、版本、相依、衝突一致。sha256 對完整 ZIP 位元組計算，manifestSha256 對根 veroscript-package.json 原始位元組計算並保留空白；.vspkg 與 .zip 共用協定。

## 同版本追加平台與後端接入

首次發布與後續補平台都使用相同的 `publish` 命令，只需提供新增的 `.vspkg`／`.zip`；完全相同的既有產物不再上傳。套件名稱、版本、共同相依性和衝突必須相同。既有產物的 ID、位元組、SHA-256、清單摘要、大小、目標和需求不變。portable 目標與所有目標重疊；作業系統、架構、environment、coreBuild 或共同 ABI 鍵的不同等值限制可證明互斥，僅最低版本或所需能力不同則不行。

後端需要配合：

1. 保留 `POST /packages/{name}/versions`，要求 `mode: "append"`。不存在時建立版本，存在時原子追加上傳參照；清單是增量而非替換。[發布請求結構](../../../Schemas/veroscript-publication.schema.json)定義請求內容。
2. 驗證套件所有權、上傳位元組／清單及共同相依性、衝突不變。保留既有 ID 與不可變欄位；相同產物沿用原 ID。與全部既有及本批目標比較，包含封鎖的產物；修改、刪除、重疊回傳衝突，封鎖或撤回的版本不得追加。衝突拒絕整批。
3. 在同一交易中檢查並插入；互斥的並行追加可以同時成功。客戶端預檢不能取代伺服器交易檢查。
4. 持久保存冪等鍵與規範請求；同鍵同請求回傳原結果，不同請求拒絕。上傳配置亦同。保留完成的上傳參照供重試，不要求客戶端保存簽章 URL 或認證標頭。 上傳配置可在保持相同 uploadId 時重新簽發過期臨時 URL；相同位元組的 PUT 重傳必須安全且等冪。
5. 回傳包含全部既有及新增產物、穩定 ID 的完整版本，不只回傳增量。客戶端核對曾觀察到的身分及全部請求產物，允許有效且互斥的並行追加。
6. 來源版本 GET 僅在版本確實不存在時回傳 404，提交後回傳最新完整版本。預檢與復原直接存取來源，使用 `Cache-Control: no-cache`，略過鏡像及安裝快取。回應遺失時以完整匹配的 GET 確認；否則保留上傳參照及原請求／冪等鍵重試。
7. 追加後更新 ETag、CDN 與鏡像中繼資料；既有下載身分、位元組與各目標鎖定檔仍有效。撤回／封鎖是獨立狀態，不能用於替換產物。

協定維持 `0.1.1`，核心、Interop、JIT 介面不變。後端必須先實作上述語義，再啟用實際發布。

錯誤含 code/message/requestId 及選用結構詳情。GET 支援 ETag/If-None-Match 及有界重試，上傳/提交 POST 不做盲目 GET 式重試。認證限於準確主機/路徑，恢復記錄依邏輯來源、API、產物集合隔離。伺服器實作授權、過期、驗證、索引、儲存、撤回/封鎖、鏡像同步及稽核。

[結構定義](../../../Schemas) · [維護指南](index.md)

內建 HTTP 傳輸會依每一跳正規化後的實際 URL 重新驗證憑證，包括重新導向。自訂 `IHttpTransport` 必須在每次實際請求時遵守 `FHttpRequest.Credential`：傳回空值時不傳送 Authorization，且仍遵守原有的重新導向來源／路徑限制。套件庫明確提供的上傳簽章認證與套件庫憑證解析分別處理。同一路徑附帶查詢參數時仍屬於該憑證範圍。

若百分號解碼後的路徑出現獨立的 `.` 或 `..` 區段（包括編碼的分隔符號），也不傳送解析器提供的憑證；一般編碼檔名與 UTF-8 路徑仍可正常使用。

官方倉庫須將 `vs-` 開頭的套件名稱保留給授權的官方發布者，涵蓋套件建立及後續發布。AsmJit 的規範套件名稱為 `vs-asmjit`，服務後端 ID 為 `asmjit`。保留規則由伺服器的所有權及發布授權實施；用戶端繼續通用名稱驗證，本機或私人來源中的此前綴不證明官方來源。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md) · [en-US](../en-US/registry-protocol.md) · [fr-FR](../fr-FR/registry-protocol.md) · [ja-JP](../ja-JP/registry-protocol.md) · [ko-KR](../ko-KR/registry-protocol.md) · [zh-CN](../zh-CN/registry-protocol.md) · [zh-TW](registry-protocol.md)
