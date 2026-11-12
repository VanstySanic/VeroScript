# 依業務發布與獨立版本

本頁遵循唯一的[打包與發行規範](../../../../../Resources/Docs/Standards/zh-TW/PACKAGING.md)，不相容或轉換舊布局。清理舊 Release 與無用候選後依新規則重建，產品版本以發行登錄為準。

業務優先使用 `Release/<business-route>/<target>/<version>/`，版本內依用途分 `archives/`、`sdks/`、`packages/`、`installers/`、`symbols/`。共用包歸 `any`，WebAssembly SDK 歸 `wasm32`，原生程式保留真實 OS/架構。根 `release-index.json` 指向 `indexes/<target>.json`，目標業務可參照共用與原生多份清單。完整欄位、職責、負載與參照清理以規範為準。

共用登錄與發行腳本實作分類路由、格式校驗及驗收記錄。分發前須完成真實建置並通過負載／庫存稽核；目標執行與安裝生命週期結果分別記錄，包括失敗或未執行的檢查。純規劃與靜態檢查本身不構成建置或執行驗收證據。

僅清理已驗證歸屬、無程序使用的舊產物與可再生暫存，保護原始碼、憑據、有用工具鏈及磁碟。格式變更不遷移舊集合，新集合更新仍須鎖、可復原提交、閉包與摘要。僅改規範時只做文件檢查，不打包。

```text
Release/
  release-index.json
  indexes/<target>.json
  <business-route>/<target>/<version>/
    archives/
    sdks/
    packages/
    installers/
    symbols/
    release-manifest.json
    checksums.txt
```

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

分別記錄 `supportedTargets` 的實際負載目標與 `consumerTargets` 的可引用發行目標；例如 wasm32 SDK 可供原生宿主索引引用。組合原生與公共分發時維持真實且匹配的生態版本。

## 規劃與實作入口

從儲存庫根目錄執行。`configuration` 讀取登錄版本、路由、產品覆蓋及格式版本；規劃與產生器共用路由函式，不建置、暫存或發佈產物。重複 `--target` 與 `--business` 可縮小範圍。`indexSelections` 可同時參照原生與共用清單；選用項目和 wheel 標籤仍須由真實產生器輸出確認。

原始碼輸入及建置命令見[建置與捆綁](build-and-bundle.md)，最終庫存核驗及限制見[發行驗收](release-validation.md)。以下命令僅檢查中繼資料；只修改規範時，完成上面的文件檢查即可。

```text
python Resources/Tools/Packaging/veroscript_business_release.py configuration
python Resources/Tools/Packaging/veroscript_release_plan.py --target win-x64 --business Python --business Resources
```

<a id="release-qualification"></a>

## 庫存與驗收記錄

目標索引的 `complete` 表示必要業務庫存齊全；`qualificationStatus` 彙總已記錄的驗收，`qualified` 還要求庫存完整且必要檢查滿足。真實建置及完整性核驗通過的產物，可在執行／裝置檢查失敗或未執行時分發；宣稱目標可用性前須查閱記錄，缺少記錄為 `not-recorded`。

業務項目可包含版本為 1 的 `qualification`，綁定 `manifests[]` 的路徑及 SHA-256。`--records` 輸入 JSON 物件，將已發佈業務 ID 對應至含 `checks` 的物件。每項檢查具有真實的 `status` 和非空 `reason`；`required` 預設為 true，`evidenceSha256` 可選。必須記錄成功的 `build` 及明確為必要的 `target-runtime` 檢查；`failed`、`not-run` 如實保留，負載改變後舊記錄失效。記錄工具在集合鎖內更新所選目標索引及根摘要。

```text
python Resources/Tools/Packaging/veroscript_qualification.py --repo-root . --release-directory Release --target win-x64 --records .tmp/acceptance-records.json
```

## 既有集合的發佈預先檢查

發佈前，既有目標索引的 `requiredBusinesses` 必須與目前產品登錄定義完全一致。若業務集合已有變更，請在 `.tmp` 或 `dist` 下選擇新的空白專用輸出目錄，從原始碼重建集合。預設 `Release` 輸出亦執行相同檢查。規劃預覽預設包含所有已登錄的桌面及行動目標，包括 `win-x86`，可用 `--target` 縮小範圍。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/release-layout.md) · [fr-FR](../fr-FR/release-layout.md) · [ja-JP](../ja-JP/release-layout.md) · [ko-KR](../ko-KR/release-layout.md) · [zh-CN](../zh-CN/release-layout.md) · [zh-TW](release-layout.md)
