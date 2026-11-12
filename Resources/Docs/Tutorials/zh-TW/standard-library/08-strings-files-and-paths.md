# 08：字串、檔案和路徑

本篇完成一個小任務：讀取文字檔案，清理每一行，並把結果寫入另一個檔案。你會同時使用字串、輸入輸出和路徑模組。

## 第一步：準備工作目錄

在腳本旁邊建立 `input.txt`，內容例如：

```text
  VeroScript
  is practical
```

建立 `clean_text.vs`：

```VeroScript
import "io.vs";
import "path.vs";
import "string.vs";

int main()
{
	string input_path = "input.txt";
	string output_path = std::filesystem::join_path("output", "clean.txt");

	if (!std::filesystem::is_file(input_path))
	{
		print("missing: " + input_path);
		return 1;
	}

	std::filesystem::mkdir(std::filesystem::dirname(output_path));
	string content = std::io::read_file(input_path);
	string cleaned = std::strings::trim(content);
	std::io::write_file(output_path, cleaned + "\n");

	print("wrote " + output_path);
	return 0;
}
```

`io.vs` 負責檔案和目錄，`path.vs` 負責拼接路徑，`string.vs` 負責清理文字。把這三類職責分開，可以避免手動拼接路徑和重複實作字串邏輯。

## 第二步：執行並檢查結果

```powershell
veroscript --check clean_text.vs
veroscript clean_text.vs
Get-Content output/clean.txt
```

輸出檔案會去掉文字首尾的 ASCII 空白。`trim` 只移除兩端的 `0x09` 至 `0x0D` 位元組和空格（`0x20`），保留中間及非 ASCII 位元組。`is_file` 會跟隨符號連結，判斷輸入路徑是否指向一般檔案。`read_file` 和 `write_file` 使用目前工作目錄解析相對路徑，因此執行指令時要注意終端所在位置。

## 第三步：按行處理

如果要分別處理每一行，可以使用字串拆分和連接：

```VeroScript
import "io.vs";
import "string.vs";

int main()
{
	string content = std::io::read_file("input.txt");
	array<string> lines = std::strings::split(content, "\n");
	array<string> cleaned = array<string>();

	for (string line : lines)
	{
		if (std::strings::trim(line) != "")
		{
			cleaned.push(std::strings::trim(line));
		}
	}

	std::io::write_file(
		"clean-lines.txt",
		std::strings::join(cleaned, "\n") + "\n");
	return 0;
}
```

Windows 文字可能包含 `\r\n`。如果輸入來自不同平臺，可以在處理前明確清理 `\r`，不要假設所有檔案都只有一種行尾格式。

## 第四步：格式化結果

檔案處理完成後，通常需要產生一段可讀的摘要。`format` 使用花括號欄位，適合產生字串；`print` 的多參數形式也使用同一套語法：

```VeroScript
import "string.vs";

int main()
{
	string path = "output/clean.txt";
	int line_count = 2;
	string summary = format("wrote {}, lines={}", path, line_count);
	print(summary);
	printf("status=%s%%\n", "100");
	return 0;
}
```

需要 printf 風格轉換符時使用 `formatf` 或 `printf`，不要把 `%d`、`%.2f` 這類寫法放進 `format`。完整的欄位、寬度、精確度和轉換規則請見[字串語言行為](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/strings.md#%E6%A0%BC%E5%BC%8F%E5%8C%96)。

## 練習

1. 使用 `std::filesystem::change_extension` 把輸出檔案副檔名改成 `.clean.txt`。
2. 輸出輸入檔的 `std::filesystem::basename`、`dirname` 和 `extension`。
3. 如果輸出檔已經存在，改用 `std::io::append_file` 追加一筆處理記錄。
4. 將摘要改為 `format` 產生，並嘗試用 `formatf` 輸出帶兩位小數的處理耗時。
5. 刪除 `output/clean.txt`，用 `std::filesystem::is_directory` 檢查 `output`，再用 `std::filesystem::remove_dir` 刪除空目錄。

對照範例：[`examples/path_helpers.vs`](../../../../../VeroScript/Resources/Scripts/examples/path_helpers.vs)。完整 API 清單請見[輸入輸出](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/io.md)、[路徑](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/path.md)和[字串](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/string.md)。

下一篇將把「事件通知」和「後臺工作」組合起來：[並發和事件](09-concurrency-and-events.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/standard-library/08-strings-files-and-paths.md) · [fr-FR](../../fr-FR/standard-library/08-strings-files-and-paths.md) · [ja-JP](../../ja-JP/standard-library/08-strings-files-and-paths.md) · [ko-KR](../../ko-KR/standard-library/08-strings-files-and-paths.md) · [zh-CN](../../zh-CN/standard-library/08-strings-files-and-paths.md) · [zh-TW](08-strings-files-and-paths.md)
