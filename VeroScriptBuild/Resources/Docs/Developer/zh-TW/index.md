# Build 服務維護

## 規劃與執行

`FBuilder::Plan` 凍結相依圖、原始碼／產物身分、提供者與啟動器範本，不執行處理器、工廠或子處理程序。`FBuildRequest` 的服務邊界路徑為絕對路徑；設定內路徑相對所選專案根目錄解析，不改變處理程序目錄。`FServices` 持有主機繫結目錄、回呼與明確工具鏈／環境身分；提供者的 `Plan` 不得產生執行副作用。

`FBuilder::Execute` 重新核對凍結輸入，輸入變更後必須重新規劃。提供者透過 `FStepContext::TrackInput` 回報額外輸入，不得取代既有摘要。僅在宣告的輸入欄位解釋產物參照，提供者 `options` 保持不透明 JSON。快取身分包含設定、上游內容、提供者身分及輸出雜湊；自訂快取需明確宣告確定性與工具鏈身分。動態處理器及密碼／信任敏感編譯會保守地重新執行。

## 發布與目標原則

每個目標在其磁碟區內帶有所有權憑據原子發布，不得覆寫外來或已修改的輸出。後續目標失敗不復原已發布的獨立目標。內容使用期間保留產物擁有者與部署租約；`Deploy` 及 `LeaseDeployment` 使用已安裝的凍結環境，不得退回安裝。

應用內容依所選目標的平台／ABI 驗證，而非建置主機；實際原生執行仍需對應目標驗證。原始碼應用保留可攜預設值及 runtime+compile 用途，拒絕開發專用搜尋路徑。Runtime 元件與啟動器範本為明確驗證輸入，範本變更使規劃／快取失效。原生平台封裝、簽章與內容組裝分開處理。

處理程序接收執行檔與引數陣列，不經 shell。受限指令碼僅使用宣告的輸入輸出輔助介面及配額；可信原生提供者與處理程序不構成作業系統沙箱。資源變換前驗證既有產物簽章；重新加密只解碼既有信封一次，變更簽章內容需新原則，未變更的複本保留簽章。驗證失敗時禁止發布，不得將秘密金鑰序列化至設定或產物。

## 整合與驗證

同步維護 `Public/VeroScriptBuild.h`、規劃／執行、`Resources/Schemas/veroscript-build.schema.json`、CLI 解析及 Application／套件適配器。根建置啟用 `VeroScriptBuildTests`；`Resources/Examples/hello` 與 `Resources/Examples/generated` 展示應用／程式庫／發行輸出及指令碼產生器。維護時核對過期規劃、快取失效、擁有者租約、目標選擇、簽章失敗與原子發布。CLI 操作與可執行範例歸使用者指南；平台執行及簽章需另行記錄驗證。

<!-- docs-navigation -->

---

[使用者指南](../../User/zh-TW/index.md) · [開發文件](../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
