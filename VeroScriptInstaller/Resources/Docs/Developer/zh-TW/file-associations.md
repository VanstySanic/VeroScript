# 安裝器檔案關聯實作

公開行為統一維護在[安裝指南](../../User/zh-TW/index.md#file-associations)。`Core/FileAssociationModels.cs` 宣告 `FileAssociationOptions`、`FileAssociationRecord` 及值級所有權記錄；`InstallOptions.FileAssociations` 預設全部為 false。`InstallerEngine.SupportsFileAssociations` 與 `GetSupportedFileAssociations(manifest)` 公開登記配接器與套件能力；安裝要求已選擇 Runtime、Windows manifest 和對應的已宣告執行檔／圖示。

`Core/FileAssociationRegistration.cs` 負責規劃；`Core/Platform/IFileAssociationRegistry.cs` 分離登記存取與規劃。Windows 實作在目前使用者下寫入 Classes、已註冊應用程式和所擁有的能力項，處理常式身分按安裝隔離。不修改副檔名預設 ProgID 或 `UserChoice`；`.vspkg` 只登記受保護的 DefaultIcon。`Core/Platform/DefaultAppsSettings.cs` 的成功僅表示已要求設定頁面。

安裝記錄中的可選 `fileAssociations` 保存選項、所有者身分、值及所建立的機碼；缺失欄位表示無關聯記錄。套用前記錄交易意圖，套用時重新檢查舊值和所有者標記，回復按逆序進行。修復／更新不能重新接管後續使用者修改，tombstone 保留失去所有權的狀態。僅清理所擁有的空機碼，保留其他應用程式和並行值。

bundle Tool 的 `install` 接受無值旗標 `--associate-vs`、`--associate-vsapp`、`--package-icon`、`--choose-defaults`，最後一個至少要求前兩個中的一個。這些是 Tool 參數，不是圖形安裝器參數。App 負責可選精靈頁和 `App/Resources/Localization/<locale>/associations.json`；六語言保持鍵與預留位置一致，設定開啟失敗與安裝成功分別報告。

允許執行時，透過隔離的登記配接器／登錄區 hive 覆蓋能力缺失、取消 Runtime、既有所有者、使用者修改、修復／更新、部分套用、復原和解除安裝，再單獨驗證真實 Windows shell。靜態文件驗證不代表登錄副作用、圖示重新整理或預設應用程式 UI 已通過驗收。

登錄機碼與值名稱比較不區分大小寫，所有者識別值仍須完全相符。套用與回復在 UI 執行緒外執行。回復先還原登記值，再還原受管理檔案，抑制中間 shell 通知，待登錄參照和原始圖示路徑均可用後再通知。

<!-- docs-navigation -->

---

[Installer](index.md)

[en-US](../en-US/file-associations.md) · [fr-FR](../fr-FR/file-associations.md) · [ja-JP](../ja-JP/file-associations.md) · [ko-KR](../ko-KR/file-associations.md) · [zh-CN](../zh-CN/file-associations.md) · [zh-TW](file-associations.md)
