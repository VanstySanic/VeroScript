# 建置與封裝應用程式

`--build-file <檔案>` 可指定任意設定檔名，支援絕對路徑及相對命令啟動目錄的路徑。未指定 `--path` 時以設定檔父目錄作為專案根；指定後，設定內輸入路徑以該專案根為準。預設檔名為 `veroscript-build.json`。

`VeroScriptBuild` 讀取嚴格的 `veroscript-build.json`。設定內路徑相對指定專案根目錄；命令列路徑相對啟動目錄。服務不變更工作目錄。

`--profile <名稱>` 選擇 `profiles` 中宣告的組態，預設值為 `release`。`--target <名稱>` 選擇建置目標，可重複指定；未明確選擇目標時使用組態中的 `defaultTargets`。

```sh
veroscript build --path /absolute/project --plan --json
veroscript build --path /absolute/project --build-file /absolute/config/release.json --plan --json
```

上述命令只規劃；移除 `--plan` 即執行。可用位置參數或 `--target` 選擇目標。依賴套件須先安裝並鎖定；建置不自動下載、安裝或發布。

目標包括 library、application、distribution、deployment、bundle、custom、group，分別處理 `.vslib`、`.vsapp`、`.vspkg/.zip`、唯讀部署環境、發行組合、自訂步驟與目標群組。產物引用使用 `{ "target": "library", "output": "main" }`，原始碼樹成員另加 `path`。隨附 hello 範例涵蓋這些基本路線。

建置時的應用驗證採用所選建置目標的平台與 ABI，而非建置機器的身分。宣告的元件齊備時可以準備其他目標的產物，但原生元件仍需在對應目標環境驗證與執行。

Runtime、依賴環境與啟動器各自可選；原始碼應保留 runtime＋compile 用途。PE/ELF 單一檔案重用應用程式 ZIP 內容；其他平台封裝透過自訂提供者。收據及租約管理所有權、快取與原子發布，不覆蓋外部修改。

進程及腳本步驟須明確授權。腳本只透過三個 build 回呼讀取宣告輸入、寫入暫存輸出及輸出訊息，並受 VM 限額控制。原生進程不是作業系統沙箱。保護設定只存放金鑰 ID／版本，金鑰檔不可加入發行物。

原始碼應用會先驗證實際封裝的內容，並保存宏與編譯選項。開發機搜尋路徑不會嵌入，請使用 files、modules 和明確環境；編輯器覆寫先轉成產生的原始碼樹。`Resources/Examples/generated` 示範腳本產生器。目錄及單檔程式使用內容簽章。

重新組裝已簽章應用時，必須明確指定新產物的簽章政策。

## 步驟、輸出與快取

`--allow-process` 與 `--allow-script` 授權執行對應步驟；`--plan` 不執行處理器、自訂步驟、安裝鉤子或子處理程序。處理程序接收執行檔及引數陣列，不經 shell。指令碼提供 `build_read_text(input, relative)`、`build_write_text(output, relative, text)`、`build_message(text)`。匯入限制在指令碼目錄、宣告的輸入目錄與明確標準程式庫根目錄。標準檔案、環境、主控台、工作、位址及非確定性服務被停用，並套用指令數、時間、深度、記憶體及數值限制。可信原生提供者與處理程序不構成作業系統沙箱。

請先將 `Resources/Examples/hello` 複製至可寫位置。輸出位於 `dist/<profile>/<target-identity>/<target>/`，可攜目標身分為 `portable`。外來或已修改檔案不會被覆寫。每個目標在同一磁碟區原子發布，後續失敗不復原已發布的獨立目標；使用產物期間保留傳回的租約。快取綁定設定、凍結輸入、上游內容、提供者身分與輸出雜湊；自訂步驟需明確宣告確定性及工具鏈身分，動態處理器與信任敏感編譯會保守地重新執行。輸入變更後需重新規劃。

嵌入宿主執行 `script` 建置步驟時，須透過 `FServices::Compile.ScriptCompileExtensions.VmBackendFactory` 或 `VmBackendPath` 明確提供 VM，兩者互斥；缺少時回報 `script-vm`。桌面 `veroscript build` 使用產品組裝的 VM，並保留權限、指令、期限、取消與記憶體限制。VM 路徑與內容參與快取校驗；自訂工廠須提供穩定的 `FServices::Identity`，才能重用腳本步驟快取。

## 元件與保護

原始碼應用需要 `["runtime", "compile"]` 用途，已編譯應用通常為 `["runtime"]`。`bundle.runtime` 僅複製已驗證 `veroscript-runtime.json` 列出的檔案，不複製整套 SDK。啟動器使用明確且相容的預建範本，不搜尋 PATH、不呼叫編譯器。範本在規劃時凍結，變更使規劃／快取失效。PE/ELF 將原生元件具現化至已驗證快取，Mach-O 等封裝交由自訂提供者。原生相依不同時需按平台／ABI 分別輸出。

產物參照僅在宣告的輸入欄位解釋，提供者 `options` 保持不透明 JSON；重新回報輸入不得取代凍結摘要。主機 import/stdlib 根目錄相對專案解析，優先於目標根目錄。保護原則在變換前驗證既有資源簽章；重新加密只解碼既有信封一次，變更簽章內容需新原則。新簽章取代舊簽章，未變更複本保留簽章，驗證失敗不得發布。使用 `--crypto-library <absolute-library> --keyring <absolute-file>`，不得將金鑰檔加入散發內容。註冊來源 `publish` 僅接受可安裝散發套件。完整設定見 `Resources/Schemas/veroscript-build.schema.json` 與 Application 指南。

Windows Runtime 清單會保留已暫存的 MSVC 執行階段程式庫及授權說明；攜帶 Runtime 的目錄／ZIP 應用也會將這些程式庫放在啟動器旁。單一檔案啟動器仍須滿足原生載入器的前置條件：動態 CRT 建置要求目標系統具有相符的 VC++ Redistributable；需要免安裝此程式庫時，應從原始碼對相關原生元件採用一致的靜態 CRT 建置。

<!-- docs-navigation -->

---

[使用者文件](../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
