# 輸入輸出模組

模組檔案：`io.vs`

匯入方式：

```VeroScript
import "io.vs";
```

## 檔案和目錄

| 函式 | 說明 |
| --- | --- |
| `std::io::read_file(string path)` | 讀取整個文字檔。 |
| `std::io::write_file(string path, string content)` | 覆寫寫入文字檔。 |
| `std::io::append_file(string path, string content)` | 追加文字；檔案不存在時建立。 |
| `std::io::read_bytes(string path)` | 將完整檔案讀入擁有的位元組緩衝區。 |
| `std::io::read_bytes(string path, int64 max_bytes)` | 按明確最大位元組數讀取，超出上限會報錯。 |
| `std::io::write_bytes(string path, region source)` | 以緩衝區或檢視中的位元組覆寫檔案。 |
| `std::io::append_bytes(string path, region source)` | 附加緩衝區或檢視中的位元組。 |
| `std::filesystem::exists(string path)` | 判斷路徑是否存在。 |
| `std::filesystem::delete_file(string path)` | 刪除檔案。 |
| `std::filesystem::is_file(string path)` | 判斷路徑是否指向一般檔案。 |
| `std::filesystem::is_directory(string path)` | 判斷路徑是否指向目錄。 |
| `std::filesystem::remove_dir(string path)` | 刪除一個空目錄。 |
| `std::filesystem::list_dir(string path)` | 傳回目錄項目名稱陣列。 |
| `std::filesystem::mkdir(string path)` | 建立目錄及其父目錄。 |
| `std::filesystem::cwd()` | 傳回目前工作目錄。 |
| `std::filesystem::chdir(string path)` | 修改目前工作目錄。 |

`std::filesystem::exists` 只在確認路徑不存在時回傳 `false`；檢查失敗會報錯。`std::filesystem::delete_file` 可刪除檔案或符號連結，但拒絕實際目錄。`std::filesystem::chdir` 修改整個宿主程序的工作目錄，也影響其他會話和宿主程式碼；它同時需要環境寫入與程序目錄變更權限。原生 C++ 宿主使用 `FScriptCapabilities::bAllowProcessDirectoryChange`；C、C#、Python 宿主使用腳本權限位元 `PROCESS_DIRECTORY_CHANGE`。

檔案路徑的特定可存取範圍取決於宿主程序的權限和目前工作目錄。讀寫、刪除、建立目錄或切換目錄失敗時，執行時會報告錯誤。

`std::filesystem::is_file` 和 `std::filesystem::is_directory` 會跟隨符號連結；路徑不存在時回傳 `false`，其他檢查錯誤會報錯。兩者需要檔案讀取權限。`std::filesystem::remove_dir` 需要檔案刪除權限；路徑不存在時回傳 `false`，檔案、非空目錄及目錄符號連結會報錯；不會遞迴刪除。

## 標準輸入

| 函式 | 說明 |
| --- | --- |
| `std::io::read_line()` | 讀取一行文本，不包含行尾換行；EOF 傳回空字串。 |
| `std::io::try_read_line()` | 僅在正常 EOF 時傳回空 optional。 |
| `std::io::try_read_token()` | 傳回下一個 token，正常 EOF 時傳回空 optional。 |
| `std::io::pause()` | 等待一行輸入；非交互環境遇到 EOF 時立即回傳。 |
| `std::io::read_token()` | 讀取下一個空白分隔的標記。 |
| `std::io::read_string()` | 讀取字串標記。 |
| `std::io::read_int()` | 讀取整數標記。 |
| `std::io::read_double()` | 讀取浮點數標記。 |
| `std::io::read_bool()` | 讀取布林值標記。 |

令牌輸入函式以空白分隔輸入。需要保留整行時使用 `std::io::read_line`，需要逐一讀取值時使用對應的 `read_*` 函式。


- [標準函式庫基礎](foundation.md)

`region` 表示 buffer、view、readonly_view 的對應多載。檔案讀取拒絕目錄，包括空目錄。位元組 I/O 上限、明確零最大值及 EOF 約定請見[基礎參考](foundation.md#io-contract)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/io.md) · [fr-FR](../../fr-FR/standard-library/io.md) · [ja-JP](../../ja-JP/standard-library/io.md) · [ko-KR](../../ko-KR/standard-library/io.md) · [zh-CN](../../zh-CN/standard-library/io.md) · [zh-TW](io.md)
