# 08：字符串、文件和路径

本篇完成一个小任务：读取文本文件，清理每一行，并把结果写入另一个文件。你会同时使用字符串、输入输出和路径模块。

## 第一步：准备工作目录

在脚本旁边创建 `input.txt`，内容例如：

```text
  VeroScript
  is practical
```

创建 `clean_text.vs`：

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

`io.vs` 负责文件和目录，`path.vs` 负责拼接路径，`string.vs` 负责清理文本。把这三类职责分开，可以避免手工拼接路径和重复实现字符串逻辑。

## 第二步：运行并检查结果

```powershell
veroscript --check clean_text.vs
veroscript clean_text.vs
Get-Content output/clean.txt
```

输出文件会去掉文本首尾的 ASCII 空白。`trim` 只移除两端的 `0x09` 至 `0x0D` 字节和空格（`0x20`），保留中间及非 ASCII 字节。`is_file` 会跟随符号链接，判断输入路径是否指向普通文件。`read_file` 和 `write_file` 使用当前工作目录解析相对路径，因此运行命令时要注意终端所在位置。

## 第三步：按行处理

如果要分别处理每一行，可以使用字符串拆分和连接：

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

Windows 文本可能包含 `\r\n`。如果输入来自不同平台，可以在处理前明确清理 `\r`，不要假设所有文件都只有一种行尾格式。

## 第四步：格式化结果

文件处理完成后，通常需要生成一段可读的摘要。`format` 使用花括号字段，适合生成字符串；`print` 的多参数形式也使用同一套语法：

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

需要 printf 风格转换符时使用 `formatf` 或 `printf`，不要把 `%d`、`%.2f` 这类写法放进 `format`。完整的字段、宽度、精度和转换规则见[字符串语言行为](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/strings.md#%E6%A0%BC%E5%BC%8F%E5%8C%96)。

## 练习

1. 使用 `std::filesystem::change_extension` 把输出文件扩展名改成 `.clean.txt`。
2. 输出输入文件的 `std::filesystem::basename`、`dirname` 和 `extension`。
3. 如果输出文件已经存在，改用 `std::io::append_file` 追加一条处理记录。
4. 将摘要改成 `format` 生成，并尝试用 `formatf` 输出带两位小数的处理耗时。
5. 删除 `output/clean.txt`，用 `std::filesystem::is_directory` 检查 `output`，再用 `std::filesystem::remove_dir` 删除空目录。

对照示例：[`examples/path_helpers.vs`](../../../../../VeroScript/Resources/Scripts/examples/path_helpers.vs)。完整 API 清单见[输入输出](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/io.md)、[路径](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/path.md)和[字符串](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/string.md)。

下一篇将把“事件通知”和“后台工作”组合起来：[并发和事件](09-concurrency-and-events.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/standard-library/08-strings-files-and-paths.md) · [fr-FR](../../fr-FR/standard-library/08-strings-files-and-paths.md) · [ja-JP](../../ja-JP/standard-library/08-strings-files-and-paths.md) · [ko-KR](../../ko-KR/standard-library/08-strings-files-and-paths.md) · [zh-CN](08-strings-files-and-paths.md) · [zh-TW](../../zh-TW/standard-library/08-strings-files-and-paths.md)
