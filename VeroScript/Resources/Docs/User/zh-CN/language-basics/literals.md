# 字面量

字面量是在源代码中直接写出的值。VeroScript 支持整数、浮点数、字符、字符串、宽字符串、布尔值和 `nullptr` 字面量。

## 整数字面量

不带前缀的整数按十进制解析；`0x`/`0X` 表示十六进制，`0b`/`0B` 表示二进制：

```VeroScript
int decimal = 42;
int hexadecimal = 0x2A;
int binary = 0b101010;
```

整数可以使用后缀明确取值范围或符号属性：

```VeroScript
unsigned int unsigned_value = 100u;
long long_value = 100L;
int64 wide_value = 9223372036854775807i64;
uint64 unsigned_wide_value = 18446744073709551615u64;
```

支持的整数后缀包括 `u`、`l`、`ul`、`lu`、`i64` 和 `u64` 的大小写形式。`l`/`L` 表示语言中的 64 位 `long`，`i64`/`u64` 直接选择 64 位有符号/无符号字面量；其他无后缀或 `u` 字面量使用 32 位路径。字面量的值必须能够表示在目标类型中。

下划线只能分隔当前进制下两个合法数字，浮点数的小数和指数数字中也可以使用：`1_000`、`0xFF_FF`、`0b1010_0101`、`1_2.5_0e1` 均合法。下划线不能紧邻进制前缀、小数点、指数标记或符号、类型后缀或另一个下划线。前导零不表示八进制；`010` 仍是十进制十。

## 浮点字面量

包含小数点或指数部分的数字会被解析为浮点字面量：

```VeroScript
double pi_approximation = 3.14159;
double scientific = 1.25e2;
double half = .5;
float small = .5f;
double fifty = .5e2;
```

当小数点后面紧跟十进制数字时，可以省略整数部分。因此，`.5`、`.5f` 和 `.5e2` 都是有效的浮点字面量，分别表示 `double` 类型的 `0.5`、`float` 类型的 `0.5` 和 `double` 类型的 `50.0`。单独的 `.` 仍然是成员访问标点，`i64` 等整数后缀不能用于这种写法。

指数部分使用 `e` 或 `E`，后面可以带 `+` 或 `-`。`f` 或 `F` 后缀用于表示 `float` 字面量；无后缀字面量是 `double`。

十进制浮点字面量使用 `.`，不受宿主区域设置影响。`f`／`F` 字面量直接舍入到 binary32，无后缀字面量舍入到 binary64。溢出及超出可接受 binary64 范围的值是编译错误；可由 `double` 表示但对 `float` 过小的值可能舍入为零，例如 `1e-100f`。这一字面量规则不表示运行时文本解析 API 也接受下溢到零。

## 字符字面量

字符字面量的静态类型是 `char`，即语言中的有符号 8 位整数类型：

```VeroScript
char letter = 'A';
char newline = '\n';
char byte_ff = '\xFF';
```

直接字符只能是一个 ASCII 字节。`\xNN` 精确写入一个 8 位位模式，因此 `\x80` 到 `\xFF` 对应负的 `char` 值。空字符、多字符、直接非 ASCII 字符、Unicode 转义以及 `L'A'` 等宽字符形式都会被拒绝。

## 字符串和宽字符串

普通字符串使用双引号，宽字符串在双引号前加 `L`：

```VeroScript
string text = "VeroScript";
wstring wide_text = L"text";
```

字符串字面量支持转义字符，例如换行和引号：

```VeroScript
string message = "first line\nsecond line";
string quoted = "say \"hello\"";
```

| 转义 | `string` | `wstring` | `char` |
| --- | --- | --- | --- |
| `\\`、`\"`、`\'` | 转义反斜杠或引号 | 转义反斜杠或引号 | 转义反斜杠或引号 |
| `\0`、`\a`、`\b`、`\f`、`\n`、`\r`、`\t`、`\v` | 一个字节 | 一个 UTF-16 code unit | 一个字节 |
| `\xNN` | 精确追加一个原始字节 | 追加 `U+00NN` | 一个 8 位位模式 |
| `\uNNNN`、`\UNNNNNNNN` | Unicode 标量编码为 UTF-8 | Unicode 标量编码为 UTF-16 | 不支持 |

未知或不完整的转义是编译错误。Unicode 转义拒绝代理项和超过 `U+10FFFF` 的值。`string` 是带长度的字节序列：源码文本和文本 API 使用 UTF-8，但 `\0` 与 `\xNN` 可以产生嵌入 NUL 或任意字节，因此不能保证每个运行时 `string` 都是合法 UTF-8。`wstring` 是带长度的 UTF-16 code-unit 序列。显式 UTF-8/UTF-16 转换遇到非法输入会报错，不会静默替换。

字符串字面量不能跨越源代码行。`string` 和 `wstring` 是不同的类型，不能直接混合运算；需要时应使用显式转换函数。

数组、Map、Set 和显式类型的大括号初始化器可以带一个尾随逗号，例如 `{1, 2,}` 或 `{'A': 1,}`。空初始化器仍写作 `{}`。前导逗号、连续逗号和函数实参中的尾随逗号不受支持。

## 布尔值和空值

布尔字面量只有 `true` 和 `false`：

```VeroScript
bool enabled = true;
bool finished = false;
```

`nullptr` 表示空对象句柄或空值，常用于类对象和可为空的运行时值：

```VeroScript
class Node
{
public:
	int value;
};

int main()
{
	Node node = nullptr;
	if (node == nullptr)
	{
		print("empty");
	}
}
```

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/literals.md) · [fr-FR](../../fr-FR/language-basics/literals.md) · [ja-JP](../../ja-JP/language-basics/literals.md) · [ko-KR](../../ko-KR/language-basics/literals.md) · [zh-CN](literals.md) · [zh-TW](../../zh-TW/language-basics/literals.md)
