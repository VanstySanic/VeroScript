# 字符串

VeroScript 提供 `string` 和 `wstring` 两种文本类型。两者都表示文本值，但属于不同的类型，不能在没有显式转换的情况下混合赋值、比较或拼接。本页说明字符串的语言行为以及 `string.vs` 提供的常用辅助函数；字符串字面量的写法见[字面量](literals.md)。

## 文本类型

普通字符串字面量的类型是 `string`，宽字符串字面量的类型是 `wstring`：

```VeroScript
string name = "VeroScript";
wstring title = L"Language reference";
```

`string` 与 `wstring` 都支持 `len`、相等比较和同类型拼接。拼接时，操作数必须属于同一种文本类型：

```VeroScript
string message = "Hello, " + name;
wstring heading = L"Vero" + L"Script";
```

如果需要在两种文本类型之间转换，应显式调用 `to_wstring` 或 `to_string`：

```VeroScript
wstring wide_name = to_wstring(name);
string narrow_name = to_string(wide_name);
```

数值和布尔值转换为文本时，也应使用 `to_string`，而不是依赖字符串与其他类型的隐式拼接：

```VeroScript
string summary = "count=" + to_string(3);
```

## 字符串标准库

字符串辅助函数位于 `string.vs`。使用前导入该模块：

```VeroScript
import "string.vs";
```

常用函数如下：

| 函数 | 说明 |
| --- | --- |
| `std::strings::substring(s, start, count)` | 从 `start` 开始截取指定数量的字符 |
| `std::strings::find(s, needle)` | 查找子串，返回位置；未找到时返回负值 |
| `std::strings::char_code(s, index)` | 返回指定位置的字符编码 |
| `std::strings::contains(s, sub)` | 判断是否包含子串 |
| `std::strings::starts_with(s, prefix)` | 判断是否以指定前缀开头 |
| `std::strings::ends_with(s, suffix)` | 判断是否以指定后缀结尾 |
| `std::strings::trim_left(s)` / `std::strings::trim_right(s)` | 删除左侧或右侧的空白字符 |
| `std::strings::trim(s)` | 删除两侧的空白字符 |
| `std::strings::split(s, separator)` | 按分隔符拆分为 `array<string>` |
| `std::strings::join(parts, separator)` | 使用分隔符连接字符串数组 |
| `std::strings::to_lower(s)` / `std::strings::to_upper(s)` | 转换为小写或大写 |
| `std::strings::replace(s, from, to)` | 将匹配的子串替换为指定文本 |
| `std::strings::last_find(s, needle)` | 返回最后一次匹配的 UTF-8 字节偏移；不存在时返回 `-1` |
| `std::strings::repeat(s, count)` | 重复 `s`；负数 count 会产生运行时错误 |
| `std::strings::remove_prefix(s, prefix)` / `std::strings::remove_suffix(s, suffix)` | 删除匹配的前缀或后缀，否则返回 `s` |

这些函数以值的形式返回结果，不会修改传入的原字符串。例如：

```VeroScript
import "string.vs";

string raw = "  red,green,blue  ";
array<string> colors = std::strings::split(std::strings::trim(raw), ",");
string normalized = std::strings::join(colors, " | ");
print(normalized);
```

`trim_left`、`trim_right` 和 `trim` 移除 ASCII 空白字节 `0x09` 到 `0x0D` 及空格（`0x20`）。字符串索引是从零开始的 UTF-8 字节偏移，不是 Unicode 码点索引。`len`、`find` 和 `last_find` 返回 `int64`，查找无结果时为 `-1`；切片位置与数量、`char_code` 索引和 `repeat` 次数使用 `int64`。边界及返回类型见[字符串模块](../standard-library/string.md)。

## 格式化

VeroScript 提供两套格式化语法。`format` 使用花括号字段，`formatf` 和 `printf` 使用 printf 风格的转换说明。两套语法相互独立，不要在同一个格式字符串中混用。

### 花括号格式化

`string.vs` 的公开签名是：

```text
string format(string fmt, array<string> args);
```

在脚本中通常直接传入标量参数。编译器会把 `string`、布尔值、浮点数以及有符号或无符号整数转换为字符串并收集到参数数组中：

```VeroScript
import "string.vs";

string message = format("name={}, score={}", "Ada", 97);
print(message);
```

如果已经有 `array<string>`，也可以把它作为第二个参数直接传入：

```VeroScript
import "string.vs";

array<string> args = array<string>();
args.push("Ada");
args.push("97");
string message = format("name={}, score={}", args);
```

字段的基本形式如下：

| 写法 | 含义 |
| --- | --- |
| `{}` | 使用下一个自动参数，索引从 `0` 开始。 |
| `{N}` | 使用显式的零基参数索引，例如 `{0}`。 |
| `{:spec}` | 使用下一个自动参数，并应用格式说明。 |
| `{N:spec}` | 使用显式索引并应用格式说明。 |
| `{{` / `}}` | 输出字面量 `{` / `}`。 |

格式说明的语法为 `[alignment][width][.precision][type]`：

| 部分 | 可用值 | 说明 |
| --- | --- | --- |
| 对齐 | `<`、`>`、`^` | 分别表示左对齐、右对齐、居中；省略时默认右对齐。 |
| 宽度 | 十进制整数 `0`–`1024` | 结果不足时使用空格填充；宽度不会截断内容。 |
| 精度 | `.decimal-integer`，范围 `0`–`1024` | 将参数按有限数值解析，并以固定小数位输出。 |
| 类型 | `x`、`X` | 将十进制整数转换为小写或大写十六进制。 |

`format` 的 `x`/`X` 转换按当前运行时的有符号 32 位十进制整数规则解析，再输出其 32 位十六进制表示；需要处理完整无符号 64 位范围时，应使用 `formatf` 的 `%x` 或 `%X`。

例如：

```VeroScript
import "string.vs";

string right = format("{:>6}", "ok");   // "    ok"
string left = format("{:<6}", "ok");    // "ok    "
string center = format("{:^6}", "ok");  // "  ok  "
string fixed = format("{:.2}", "1.5");  // "1.50"
string hex = format("{:X}", "255");     // "FF"
string repeated = format("{0} / {0}", "same");
string braces = format("{{value}}", array<string>()); // "{value}"
```

自动索引只会在自动字段中前进。为了避免 `{0}`、`{}` 这类写法产生意外结果，应在同一个格式字符串中统一使用自动索引或显式索引。参数不足、花括号不匹配、格式说明非法，或数值格式无法解析参数时，会产生运行时错误。未被字段使用的多余参数不会自动追加到结果中。

### printf 格式化

`formatf` 的公开签名是：

```text
string formatf(string fmt, array<string> args);
```

脚本调用时同样可以直接传入标量参数。`printf` 使用相同的格式语法，但直接写入标准输出。转换说明的基本形式为：

```text
%[flags][width][.precision][length]conversion
```

| 部分 | 可用值或限制 |
| --- | --- |
| 标志 | `-` 左对齐、`+` 显示正号、空格显示正数前的空格、`#` 使用替代前缀、`0` 使用零填充。 |
| 宽度 | 十进制整数，范围 `0`–`1024`；不支持动态宽度 `*`。 |
| 精度 | `.digits`，范围 `0`–`1024`；不支持动态精度 `.*`。 |
| 长度 | `h`、`hh`、`l`、`ll`、`j`、`z`、`t` 均可接受，但不会改变 VeroScript 值的宽度或 ABI。 |
| 转换符 | `d`、`i`、`u`、`o`、`x`、`X`、`f`、`F`、`e`、`E`、`g`、`G`、`c`、`s`。 |

各转换符的参数要求如下：

| 转换符 | 参数与结果 |
| --- | --- |
| `%d`、`%i` | 有符号整数。 |
| `%u` | 非负整数。 |
| `%o` | 八进制整数。 |
| `%x`、`%X` | 小写或大写十六进制整数。 |
| `%f`、`%F` | 定点浮点数；默认精度为 `6`。 |
| `%e`、`%E` | 科学计数法；大写版本使用大写字母。 |
| `%g`、`%G` | 默认浮点表示；大写版本使用大写字母。 |
| `%c` | `0`–`255` 的整数，或单字节字符串；不接受精度。 |
| `%s` | 字符串；精度表示最多输出的字节数。 |
| `%%` | 输出字面量 `%`，不消耗参数。 |

`%c` 和 `%s` 只允许 `-` 标志和宽度；数字转换的精度优先于 `0` 零填充。格式字符串末尾残留 `%`、参数不足、转换符不受支持、参数类型无法解析，都会产生运行时错误。浮点转换要求参数是有限数值。没有被转换符消耗的多余参数会被忽略。

```VeroScript
import "string.vs";

string text = formatf("d=%+06d x=%#08X f=%.2f s=%-5.3s c=%c", -42, 255, 3.14159, "abcdef", 65);
printf("%s%% done", text);
```

### 与输出函数的关系

```VeroScript
import "string.vs";

print("name={}, score={}", "Ada", 97); // print's multi-argument form uses braces
printf("score=%03d", 7);                // printf always uses printf syntax
string value = format("score={}", 7);   // use format when a string is needed
```

`print` 只有一个待输出值时直接输出；传入格式字符串和额外参数时使用花括号格式。`format`/`formatf` 返回字符串，`printf` 直接写入标准输出。`format`、`formatf` 和 `printf` 不接受引用参数或 `void` 表达式作为格式参数。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/strings.md) · [fr-FR](../../fr-FR/language-basics/strings.md) · [ja-JP](../../ja-JP/language-basics/strings.md) · [ko-KR](../../ko-KR/language-basics/strings.md) · [zh-CN](strings.md) · [zh-TW](../../zh-TW/language-basics/strings.md)
