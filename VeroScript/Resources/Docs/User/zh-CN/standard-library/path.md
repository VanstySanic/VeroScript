# 路径模块

模块文件：`path.vs`

导入方式：

```VeroScript
import "path.vs";
```

## 函数

| 函数 | 说明 |
| --- | --- |
| `std::filesystem::basename(string path)` | 返回最后一个路径分隔符后的名称。 |
| `std::filesystem::dirname(string path)` | 返回最后一个路径分隔符前的目录部分；没有分隔符时返回 `"."`。 |
| `std::filesystem::join_path(string left, string right)` | 使用路径分隔符连接两段路径。 |
| `std::filesystem::extension(string path)` | 返回文件扩展名，包含点；没有扩展名时返回空字符串。 |
| `std::filesystem::change_extension(string path, string new_ext)` | 替换或追加扩展名。 |

路径模块同时识别 `/` 和 `\` 作为路径分隔符。`std::filesystem::join_path` 会处理左侧末尾已有分隔符以及任意一侧为空的情况。
`std::filesystem::dirname` 保留 Windows 驱动器根目录中的分隔符，例如 `std::filesystem::dirname("C:\\foo.txt")` 返回 `C:\`。

`std::filesystem::change_extension` 接受带点或不带点的新扩展名；例如传入 `"json"` 和 `".json"` 的结果相同。以点开头的文件名不会把开头的点单独当作扩展名。
`std::filesystem::join_path` 只进行词法拼接：任一侧为空时原样返回另一侧；右侧以 `/`、`\\` 开头或具有 `C:` 这样的 ASCII 盘符前缀时，原样返回右侧。其他情况按需插入 `/`；左侧恰好为 `C:` 时保留盘符相对形式 `C:file`。它不访问磁盘，也不规范化 `.` 或 `..`。


- [标准库基础](foundation.md)

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/path.md) · [fr-FR](../../fr-FR/standard-library/path.md) · [ja-JP](../../ja-JP/standard-library/path.md) · [ko-KR](../../ko-KR/standard-library/path.md) · [zh-CN](path.md) · [zh-TW](../../zh-TW/standard-library/path.md)
