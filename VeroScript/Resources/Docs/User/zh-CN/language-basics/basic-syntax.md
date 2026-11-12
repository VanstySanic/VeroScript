# 基本语法

本文说明 VeroScript 源文件的基本结构，以及语句、代码块、标识符和关键字的写法。

## 源文件

VeroScript 源文件使用 `.vs` 扩展名。一个源文件可以包含全局变量、函数、结构体、类、接口、枚举、命名空间和模块声明。

应用脚本通常提供 `main` 入口；库模块可以只导出类型和函数，不提供入口函数。`main` 可以返回 `int` 或 `void`，推荐返回 `int`，以便明确表达进程退出码。

```VeroScript
int global_value = 1;

int add(int left, int right)
{
	return left + right;
}

int main()
{
	print(add(global_value, 2));
	return 0;
}
```

## 推荐书写规范

文档中的 VeroScript 代码片段遵循以下格式：每级使用一个 Tab 字符缩进；左花括号独占一行；普通语句一行一条；`class`、`struct`、`interface` 和 `enum` 类型声明使用结尾分号。语言也接受部分省略分号的简写形式，但文档代码建议保持完整写法。独立示例文件遵循 examples README 中说明的格式化器风格。

## 语句和分号

普通语句以分号结束：

```VeroScript
int value = 1;
value = value + 1;
print(value);
```

函数声明、类型声明和控制流语句的代码块使用花括号。代码块中的语句可以跨越多行，空格和换行通常不改变语义。

```VeroScript
if (value > 0)
{
	print("positive");
}
```

## 标识符

标识符用于命名变量、函数、类型、字段、命名空间和模块成员。标识符只能由 ASCII 字母、数字和下划线组成，且不能以数字开头：

```VeroScript
int item_count = 3;
string display_name = "VeroScript";
```

标识符区分大小写。`item_count`、`ItemCount` 和 `itemCount` 是三个不同的名称。

## 关键字

关键字具有语言规定的含义，不能作为普通标识符使用。关键字包括：

```text
void bool char short int long float double string wstring
int8 uint8 int16 uint16 int32 uint32 int64 uint64
unsigned auto channel array map multimap set multiset
queue priority_queue stack ordered_map ordered_multimap deque ordered_multiset
fn function
if else while do for switch case default return break continue fallthrough goto
try catch finally throw struct class interface enum
import export using typedef namespace friend
new delete this const ref static explicit
public protected private virtual override final
where operator template in
true false nullptr
```

关键字列表属于语言语法的一部分。新增关键字可能使原本合法的标识符失效，因此库代码不应使用关键字作为公共名称。

`do` 现已保留给 `do ... while`；旧源码若曾将 `do` 用作标识符，需要改名。

## 同类型声明组与原始字符串

顶层、命名空间和局部声明中，当多个变量拥有同一个完整前置类型以及在该声明位置有效的相同 `const` / `ref` 限定符时，一条声明可以引入它们。初始化器从左到右求值，名称也按相同顺序变得可见：

```VeroScript
const int first = 1, second = first + 1;
for (int index = 0, end = 3; index < end; index++)
{
	print(index);
}
```

每一项都保有自己的初始化器并接受通常的声明检查。特别是，`auto first = 1, second = 2;` 会被有意拒绝；也不能用每个名称之后的数组后缀声明不同形状，应将共享的数组长度写在类型前缀中。

UTF-8 原始字符串使用固定形式 `r"..."`。其中的反斜杠和换行会原样保留，不进行转义处理：

```VeroScript
string path = r"C:\tools\new";
string text = r"first line
second line";
```

后续遇到的第一个 `"` 即结束该字面量，因此原始文本中不能包含引号字符。目前没有宽原始字符串、自定义定界符或字符串插值。

## 有意保留的语法边界

VeroScript 拒绝单独的空语句 `;`，有意使用空语句体时请写 `{}`。语言不支持通用逗号表达式、`f(value: 1)` 形式的命名实参、推导式 `auto` 返回类型、结构化绑定，以及 `sizeof`、`alignof` 等 C++ 布局运算符。声明组只适用于同类型声明；请使用显式返回类型以及语言或容器 API。

## 解析器安全限制

表达式、单目表达式和预处理器 `#if` 表达式的嵌套深度最多为 256 层；语句嵌套
最多为 32 层，表达式链也最多包含 256 个链接。超过限制时，编译会报告
`expression nested too deeply`、`statement nested too deeply`、`#if expression
nested too deeply` 或 `expression chain is too long` 等诊断。对于自动生成的
深层代码，请拆分表达式和控制流块。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/basic-syntax.md) · [fr-FR](../../fr-FR/language-basics/basic-syntax.md) · [ja-JP](../../ja-JP/language-basics/basic-syntax.md) · [ko-KR](../../ko-KR/language-basics/basic-syntax.md) · [zh-CN](basic-syntax.md) · [zh-TW](../../zh-TW/language-basics/basic-syntax.md)
