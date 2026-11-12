# 输入输出模块

模块文件：`io.vs`

导入方式：

```VeroScript
import "io.vs";
```

## 文件和目录

| 函数 | 说明 |
| --- | --- |
| `std::io::read_file(string path)` | 读取整个文本文件。 |
| `std::io::write_file(string path, string content)` | 覆盖写入文本文件。 |
| `std::io::append_file(string path, string content)` | 追加文本；文件不存在时创建。 |
| `std::io::read_bytes(string path)` | 将完整文件读入拥有的字节缓冲区。 |
| `std::io::read_bytes(string path, int64 max_bytes)` | 按显式最大字节数读取，超出上限会报错。 |
| `std::io::write_bytes(string path, region source)` | 用缓冲区或视图中的字节覆盖文件。 |
| `std::io::append_bytes(string path, region source)` | 追加缓冲区或视图中的字节。 |
| `std::filesystem::exists(string path)` | 判断路径是否存在。 |
| `std::filesystem::delete_file(string path)` | 删除文件。 |
| `std::filesystem::is_file(string path)` | 判断路径是否指向普通文件。 |
| `std::filesystem::is_directory(string path)` | 判断路径是否指向目录。 |
| `std::filesystem::remove_dir(string path)` | 删除一个空目录。 |
| `std::filesystem::list_dir(string path)` | 返回目录项名称数组。 |
| `std::filesystem::mkdir(string path)` | 创建目录及其父目录。 |
| `std::filesystem::cwd()` | 返回当前工作目录。 |
| `std::filesystem::chdir(string path)` | 修改当前工作目录。 |

文件路径的具体可访问范围取决于宿主进程的权限和当前工作目录。读写、删除、创建目录或切换目录失败时，运行时会报告错误。
`std::filesystem::exists` 只在确认路径不存在时返回 `false`；检查失败会报告错误。`std::filesystem::delete_file` 不删除实际目录，可删除文件或符号链接。
`std::filesystem::chdir` 修改宿主进程的工作目录，也影响其他脚本会话和宿主代码使用的相对路径。它同时要求环境写入和进程目录更改权限。原生 C++ 宿主使用 `FScriptCapabilities::bAllowProcessDirectoryChange`；C、C#、Python 宿主通过脚本权限位 `PROCESS_DIRECTORY_CHANGE` 授予该权限。

`std::filesystem::is_file` 和 `std::filesystem::is_directory` 会跟随符号链接；路径不存在时返回 `false`，其他检查错误会报告。二者要求文件读取权限。`std::filesystem::remove_dir` 要求文件删除权限；路径不存在时返回 `false`，文件、非空目录和目录符号链接会报错；不会递归删除。

## 标准输入

| 函数 | 说明 |
| --- | --- |
| `std::io::read_line()` | 读取一行文本，不包含行尾换行；EOF 返回空字符串。 |
| `std::io::try_read_line()` | 仅在正常 EOF 时返回空 optional。 |
| `std::io::try_read_token()` | 返回下一个 token，正常 EOF 时返回空 optional。 |
| `std::io::pause()` | 等待一行输入；非交互环境遇到 EOF 时立即返回。 |
| `std::io::read_token()` | 读取下一个空白分隔的标记。 |
| `std::io::read_string()` | 读取字符串标记。 |
| `std::io::read_int()` | 读取整数标记。 |
| `std::io::read_double()` | 读取浮点数标记。 |
| `std::io::read_bool()` | 读取布尔值标记。 |

令牌输入函数以空白分隔输入。需要保留整行时使用 `std::io::read_line`，需要逐个读取值时使用对应的 `read_*` 函数。


- [标准库基础](foundation.md)

`region` 表示 buffer、view、readonly_view 的对应重载。文件读取拒绝目录，包括空目录。字节 I/O 上限、显式零最大值及 EOF 约定见[基础参考](foundation.md#io-contract)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/io.md) · [fr-FR](../../fr-FR/standard-library/io.md) · [ja-JP](../../ja-JP/standard-library/io.md) · [ko-KR](../../ko-KR/standard-library/io.md) · [zh-CN](io.md) · [zh-TW](../../zh-TW/standard-library/io.md)
