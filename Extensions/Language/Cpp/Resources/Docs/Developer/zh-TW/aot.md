# C++ AOT 發射器

`VeroScriptCppAot` 從透過編譯檢查的 VeroScript 程式發射 C++。公共 API 支援源字串、檔案、程式、單個生成源字串，以及包含相對檔案和 JSON 清單的 `FCppAotSourceBundle`。

## Bundle 不變數

生成 Bundle 有意採用一個 C++ 翻譯單元和生成的 `.inl` 片段。把文字輸出拆成獨立連結的模組會改變模板執行個體化、內部連結和全域初始化順序。因此 Bundle 佈局是輸出契約，不是可選格式。

## 資格與執行時支援

發射器驗證入口模組，只接受支援的 `main` 形狀：`void main()`、`int main()` 或相應的 `string[] args` 形式。它僅為程式實際使用的值、容器、channel 和 worker 任務發射相容支援。AOT worker 全域變數和靜態欄位按原生執行緒隔離；channel 是顯式同步共享能力。

支援以字串指定任務入口、協作式 `std::concurrent::cancel_task()` / `std::concurrent::cancel_requested()`，以及檔案系統操作 `std::filesystem::is_file()`、`std::filesystem::is_directory()` 和 `std::filesystem::remove_dir()`。以可呼叫物件指定任務入口及可執行的 `field_info<T>` / `method_info<T>` 反射會由 AOT 明確拒絕並提供診斷。

當降級會改變 VeroScript 的數值、所有權、異常、析構、模組或併發語義時，發射器必須拒絕程式。編譯、資格檢查、降級或輸出寫入失敗後都不得返回部分原始碼。

生成的 `array<bool>` 為每個元素使用獨立、可定址的布林儲存槽，以便索引和參照參數提供真正的 `bool&`。元素儲存不採用位元壓縮，因此相同容量下比 `std::vector<bool>` 佔用更多記憶體。

## 維護

公共發射器標頭檔案、實現、生成原始碼測試、Bundle 測試和生成檔案驗證路徑是權威依據。新的執行時輔助程式碼只能在測試證明對應 VeroScript 行為且輔助程式碼保持 Bundle 自包含時加入。

請參閱[C++ AOT — 使用者 pair 範本](../../User/zh-TW/aot.md#cleanup-container-operations)。

獨立原始碼的十進位轉換應與 `Shared/Private/VeroScriptFloatParsing.h` 保持一致。發射器的 `VeroScriptFloatParsingSource.h`、`MakeRuntimeSupport` 與產生的 `VsFloatParsingDetail` 構成自包含輸出路徑。共用輸入遷移時應同步修改 CMake 讀取路徑與 configure 相依，產生程式碼的使用者不應依賴原始碼樹私有標頭檔；目標寬度捨入及地區／範圍行為須與 Core AOT 執行分別驗證。

<a id="standalone-file-paths"></a>

產生的 `VsPathFromUtf8` 保持自我包含。Windows 的 char8_t 與 C++17 u8path 兩個分支均先建構路徑並轉為原生分隔符號，只對至少 248 個原生 UTF-16 單位的絕對路徑增加延伸前綴。完整 UNC 使用 UNC 延伸形式，既有延伸前綴保留，相對路徑不轉為絕對。轉換前保留內嵌 NUL 拒絕。同步維護 Unicode、長絕對／UNC、相對路徑及兩個語言標準分支的檔案 I/O 產生與回歸；產生物使用者不能依賴私有原始碼標頭。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](aot.md)
