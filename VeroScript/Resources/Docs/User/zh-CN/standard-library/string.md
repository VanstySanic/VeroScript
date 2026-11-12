# 字符串模块

模块文件：`string.vs`

导入方式：

```VeroScript
import "string.vs";
```

## 查询和截取

| 函数 | 说明 |
| --- | --- |
| `std::strings::substring(string s, int64 start, int64 count)` | 从字节偏移 `start` 起，返回至多 `count` 个 UTF-8 字节。 |
| `std::strings::find(string haystack, string needle)` | 以 `int64` 返回首个匹配的 UTF-8 字节偏移，未找到时为 `-1`。 |
| `std::strings::char_code(string s, int64 index)` | 以 `int` 返回 `index` 处的字节值（`0`–`255`）。 |
| `std::strings::contains(string s, string sub)` | 判断是否包含子串。 |
| `std::strings::starts_with(string s, string prefix)` | 判断是否以指定前缀开头。 |
| `std::strings::ends_with(string s, string suffix)` | 判断是否以指定后缀结尾。 |

字符串索引是从 `0` 开始的 UTF-8 字节偏移，而不是 Unicode 码点索引。
索引超出范围时会产生运行时错误。`std::strings::find`、`std::strings::last_find`、`std::strings::substring` 和
`std::strings::char_code` 使用相同的字节偏移约定。

## 拆分、连接和变换

| 函数 | 说明 |
| --- | --- |
| `std::strings::split(string s, string separator)` | 按分隔符拆分为 `array<string>`。 |
| `std::strings::join(array<string> parts, string separator)` | 使用分隔符连接字符串数组。 |
| `std::strings::trim_left(string s)` | 删除左侧 ASCII 空白：制表、换行、垂直制表、换页、回车和空格。 |
| `std::strings::trim_right(string s)` | 删除右侧空白字符。 |
| `std::strings::trim(string s)` | 删除两侧空白字符。 |
| `std::strings::to_lower(string s)` | 转换为小写。 |
| `std::strings::to_upper(string s)` | 转换为大写。 |
| `std::strings::replace(string s, string from, string to)` | 替换匹配的子串；`from` 为空时返回原字符串。 |
| `std::strings::last_find(string haystack, string needle)` | 返回最后一次匹配的 UTF-8 字节偏移；不存在时返回 `-1`，空 needle 返回 `len(haystack)`。 |
| `std::strings::repeat(string value, int64 count)` | 将字符串重复 `count` 次；负数 count 会产生运行时错误。 |
| `std::strings::remove_prefix(string value, string prefix)` | 存在 `prefix` 时删除它，否则返回原字符串。 |
| `std::strings::remove_suffix(string value, string suffix)` | 存在 `suffix` 时删除它，否则返回原字符串。 |

`std::strings::to_lower` 和 `std::strings::to_upper` 只转换 ASCII 英文字母，其他 UTF-8 字节保持不变；它们不执行 Unicode 大小写折叠。

## 格式化

本模块提供两套格式化函数：`format` 使用大括号字段，`formatf` 使用 printf 风格格式。两套格式语法相互独立，不应混用。

公开签名为：

```text
string format(string fmt, array<string> args);
string formatf(string fmt, array<string> args);
```

脚本中可以直接传入 `string`、布尔值、浮点数以及有符号或无符号整数；编译器会把这些标量转换为字符串并收集到 `array<string>`。如果已有参数数组，也可以将 `array<string>` 作为第二个参数直接传入。

```VeroScript
import "string.vs";

string text = format("value={}, name={}", 42, "VeroScript");
string hex = format("{:8X}", 255);
string legacy = formatf("value=%d", 42);
```

### `format` 字段

| 写法 | 作用 |
| --- | --- |
| `{}` | 使用下一个自动参数。 |
| `{N}` | 使用零基显式索引。 |
| `{:spec}` / `{N:spec}` | 使用格式说明；格式为 `[alignment][width][.precision][type]`。 |
| `{{` / `}}` | 输出字面量大括号。 |

对齐符为 `<`、`>`、`^`，默认右对齐；宽度和精度范围均为 `0`–`1024`，使用空格填充。类型只支持 `x` 和 `X`，分别输出小写和大写十六进制。精度会按固定小数位解析有限数值。自动索引和显式索引不应混用；参数不足、括号不匹配或格式说明非法会产生运行时错误。

<a id="integer-formatting"></a>

format 的 x/X 现在完整支持 64 位十进制整数：正数可达 UINT64_MAX，负数可达 INT64_MIN，负数按 64 位补码输出，因此 -1 为 16 个 f。printf 整数也直接按 64 位解析，不经过 double；宽度和精度仍限制为 1024。

### `formatf` 转换

转换形式为 `%[flags][width][.precision][length]conversion`。支持的标志是 `-`、`+`、空格、`#`、`0`；宽度和精度范围均为 `0`–`1024`，不支持动态 `*`。长度修饰符 `h`、`hh`、`l`、`ll`、`j`、`z`、`t` 可用于源码兼容，但不会改变 VeroScript 值的宽度或 ABI。

支持的转换符为：

| 转换符 | 说明 |
| --- | --- |
| `%d`、`%i` | 有符号整数。 |
| `%u`、`%o`、`%x`、`%X` | 非负十进制、八进制或大小写十六进制整数。 |
| `%f`、`%F`、`%e`、`%E`、`%g`、`%G` | 浮点数；未指定精度时默认使用 `6`。 |
| `%c` | `0`–`255` 的整数或单字节字符串；不接受精度。 |
| `%s` | 字符串；精度限制输出的字节数。 |
| `%%` | 输出字面量 `%`，不消耗参数。 |

`printf` 使用 `formatf` 的同一套转换语法并直接写入标准输出；`print` 的多参数形式使用 `format` 的花括号语法。没有被转换符消耗的多余参数会被忽略。完整规则与错误边界见[字符串语言行为](../language-basics/strings.md#%E6%A0%BC%E5%BC%8F%E5%8C%96)。
`std::strings::trim_left`、`std::strings::trim_right` 和 `std::strings::trim` 会删除两端的 ASCII `0x09`～`0x0D` 及空格 (`0x20`)；中间和非 ASCII 字节保持原样。

`len(s)`、`std::strings::find` 和 `std::strings::last_find` 返回 `int64`，查找无结果时为 `-1`。`std::strings::substring` 使用 `int64` 字节偏移和数量：`start` 可以等于 `len(s)`，`count` 须非负，超过剩余字节数时截取至字符串末尾。`std::strings::char_code` 接收 `int64` 字节索引，返回 `int` 字节值（`0`–`255`）。`std::strings::repeat` 接收 `int64` 次数，结果长度超出 `int64` 范围时拒绝执行；实际分配限制仍适用。

- [标准库基础](foundation.md)

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/string.md) · [fr-FR](../../fr-FR/standard-library/string.md) · [ja-JP](../../ja-JP/standard-library/string.md) · [ko-KR](../../ko-KR/standard-library/string.md) · [zh-CN](string.md) · [zh-TW](../../zh-TW/standard-library/string.md)
