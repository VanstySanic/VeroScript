# 路徑模組

模組檔案：`path.vs`

匯入方式：

```VeroScript
import "path.vs";
```

## 函式

| 函式 | 說明 |
| --- | --- |
| `std::filesystem::basename(string path)` | 傳回最後一個路徑分隔符號後的名稱。 |
| `std::filesystem::dirname(string path)` | 傳回最後一個路徑分隔符號前的目錄部分；沒有分隔符號時回傳 `"."`。 |
| `std::filesystem::join_path(string left, string right)` | 使用路徑分隔符號連接兩段路徑。 |
| `std::filesystem::extension(string path)` | 傳回檔案副檔名，包含點；沒有副檔名時傳回空字串。 |
| `std::filesystem::change_extension(string path, string new_ext)` | 替換或追加副檔名。 |

路徑模組同時辨識 `/` 和 `\` 作為路徑分隔符號。 `std::filesystem::join_path` 會處理左側末端已有分隔符號以及任意一側為空的情況。

`std::filesystem::change_extension` 接受帶點或不帶點的新副檔名；例如傳入 `"json"` 和 `".json"` 的結果相同。以點開頭的檔案名稱不會把開頭的點單獨當作副檔名。
`std::filesystem::dirname` 會保留 Windows 磁碟機根目錄的分隔符號，例如 `std::filesystem::dirname("C:\\foo.txt")` 回傳 `C:\`。
`std::filesystem::join_path` 只做詞法拼接：任一側為空時原樣回傳另一側；右側以 `/`、`\\` 開頭或具有 `C:` 這樣的 ASCII 磁碟機前綴時，原樣回傳右側。其他情況按需插入 `/`；左側恰好為 `C:` 時保留磁碟機相對形式 `C:file`。它不存取磁碟，也不正規化 `.` 或 `..`。


- [標準函式庫基礎](foundation.md)

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/path.md) · [fr-FR](../../fr-FR/standard-library/path.md) · [ja-JP](../../ja-JP/standard-library/path.md) · [ko-KR](../../ko-KR/standard-library/path.md) · [zh-CN](../../zh-CN/standard-library/path.md) · [zh-TW](path.md)
