# 發行驗收流程

本頁遵循唯一的[打包與發行規範](../../../../../Resources/Docs/Standards/zh-TW/PACKAGING.md)，不相容或轉換舊布局。清理舊 Release 與無用候選後依新規則重建，產品版本以發行登錄為準。

## 驗收階段

先固定預期原始碼、設定與目標發行設定，產生器前置條件見[建置與捆綁](build-and-bundle.md)。核對候選庫存後，在目標環境完成必要的使用端、應用與安裝生命週期驗收。交叉編譯成功不等於執行通過；簽章及其他改變位元組的步驟結束後，將結果綁定至最終產物摘要。

## 庫存與目前正本核驗

下例從儲存庫根目錄核驗既有集合；將 `win-x64` 換成實際使用目標。僅核驗 `any`／`wasm32` 共用索引不能證明原生或行動整套發行完整。

```text
python Resources/Tools/Packaging/verify-veroscript-release.py --release-directory Release --target win-x64 --require-complete --check-source
```

`--require-complete` 核對該目標的必要業務集合並拒絕 partial 業務，不執行或認證目標執行、裝置、圖形、安裝生命週期、簽章或公證驗收。`--check-source` 額外將驗證器涵蓋的原始碼所屬公開負載與目前正本比較，不證明二進位檔已由該原始碼重新建置；僅稽核較早的有效新格式集合所記錄的契約時才省略。`--version` 要求每筆選取業務參照具有該真實版本，不代表集合統一版本。

```powershell
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -ReleaseDirectory Release -Target win-x64 -RequireComplete -CheckSource -RequireSignedInstallers
```

## 驗收證據與最終複核

以 `passed`、`failed`、`not-run` 或 `not-applicable` 記錄結果、原因、原始碼識別、工具鏈、設定、目標／裝置識別及最終摘要。交叉連結、模擬器、真機、C# AOT／裁剪、Python 應用及安裝／修復／解除安裝分別記錄。缺少環境及未簽章產物保留明確缺口。Windows 包裝器可用 `-RequireSignedInstallers` 強制核對 Authenticode，但不會簽章，也無法替代安裝生命週期驗收。

分發前須完成真實建置，並通過負載、目標識別、相依、清單及摘要檢查。具備環境時執行目標執行與安裝生命週期驗收，失敗或未執行均如實記錄；這些缺口不再阻止已建置且有效的產物一併分發。透過[庫存與驗收記錄](release-layout.md#release-qualification)區分 `complete` 與 `qualified`。分發後複核最終庫存及摘要，輸入改變時使受影響證據失效。僅修改規範時，只執行下列文件校驗，不啟動建置或產品打包。

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

<a id="installer-download-validation"></a>

## 安裝程式下載檔案的靜態驗證

`veroscript_installer.py` 核對下載檔名及目標架構，讀取 .NET 單一檔案 bundle，將內嵌內容分片與組件記錄的完整 bundle SHA-256 及分片數比較。跨目標組包亦與暫存階段驗證的 ZIP 摘要比較。Linux 驗證 `.tar.gz` 內唯一可執行程式及其權限，macOS 驗證 `.dmg` 內的應用程式與內嵌負載。其他作業系統上檢查 DMG 需要透過 `VEROSCRIPT_7ZIP` 或 PATH 提供 7-Zip。目標執行及安裝生命週期檢查分別記錄。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/release-validation.md) · [fr-FR](../fr-FR/release-validation.md) · [ja-JP](../ja-JP/release-validation.md) · [ko-KR](../ko-KR/release-validation.md) · [zh-CN](../zh-CN/release-validation.md) · [zh-TW](release-validation.md)
