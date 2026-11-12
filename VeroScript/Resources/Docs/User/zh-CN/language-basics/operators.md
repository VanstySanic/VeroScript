# 运算符

运算符用于计算、比较、组合或修改表达式的值。VeroScript 支持算术、比较、逻辑、位运算、赋值、索引、成员访问和条件运算符。

## 运算符分类

| 类别 | 运算符 |
| --- | --- |
| 算术 | `+`、`-`、`*`、`/`、`%` |
| 比较 | `==`、`!=`、`<`、`>`、`<=`、`>=` |
| 逻辑 | `!`、`&&`、`||` |
| 位运算 | `&`、`|`、`^`、`<<`、`>>` |
| 赋值 | `=`、`+=`、`-=`、`*=`、`/=`、`%=`、`&=`、`|=`、`^=`、`<<=`、`>>=` |
| 自增和自减 | `++`、`--` |
| 条件 | `condition ? when_true : when_false` |
| 访问 | `.`、`::`、`[]`、`()` |

## 优先级和结合性

下表从最低优先级排列到最高优先级：

| 层级 | 运算符 | 结合性 |
| --- | --- | --- |
| 赋值 | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` | 右结合 |
| 条件 | `?:` | 右结合 |
| 逻辑或 | `||` | 左结合 |
| 逻辑与 | `&&` | 左结合 |
| 按位或 | `|` | 左结合 |
| 按位异或 | `^` | 左结合 |
| 按位与 | `&` | 左结合 |
| 比较 | `==`, `!=`, `<`, `>`, `<=`, `>=` | 左结合 |
| 移位 | `<<`, `>>` | 左结合 |
| 加减 | `+`, `-` | 左结合 |
| 乘除余 | `*`, `/`, `%` | 左结合 |
| 前缀 | `!`, `~`, 一元 `+`/`-`, 前缀 `++`/`--`, `new`, `delete`, 转换和 `ref` | 右结合 |
| 后缀/访问 | 调用 `()`, 索引 `[]`, `.`, `::`, 后缀 `++`/`--` | 左结合 |

`&&` 和 `||` 先计算左操作数并短路；`?:` 先计算条件，随后只计算一个分支。除此之外，当副作用顺序会影响结果时，不应依赖操作数或函数实参之间的相对求值顺序，应拆成独立语句。运算符重载不会改变语法优先级或结合性。VeroScript 没有通用逗号运算符；逗号分隔的表达式只允许出现在经典 `for` 的初始化和迭代子句中。

对于包含多个不同运算符的表达式，可以使用括号明确计算顺序：

```VeroScript
int result = (a + b) * c;
bool valid = (value >= minimum) && (value <= maximum);
```

## 算术运算

`+`、`-`、`*`、`/` 和 `%` 用于数值运算。`+` 也可以用于相同文本类型的字符串拼接：

```VeroScript
int total = 10 + 2;
int remainder = total % 3;
string message = "value: " + to_string(total);
```

`string` 和 `wstring` 不能直接混合拼接。

## 比较和逻辑运算

比较运算符产生布尔结果。逻辑运算符用于组合布尔表达式：

```VeroScript
bool in_range = value >= 0 && value <= 100;
bool invalid = value < 0 || value > 100;
bool enabled = !disabled;
```

上例中的 `empty` 是否可用取决于对象类型和已导入的库；对字符串通常使用标准库提供的字符串函数完成检查。

## 赋值运算

`=` 进行普通赋值，复合赋值运算符把运算和赋值合并：

```VeroScript
value = 10;
value += 2;
value *= 3;
```

赋值两侧必须满足语言规定的类型转换规则。赋值不会改变左侧变量的静态类型。

## 访问运算

索引运算符 `[]` 用于访问数组或容器元素，成员访问运算符 `.` 用于访问对象成员：

```VeroScript
int first = values[0];
point.x = 10;
```

命名空间和类型成员也可以使用 `::`：

```VeroScript
Color color = Color::Red;
```

值成员访问使用 `.`；命名空间、类型、枚举成员和静态成员优先使用 `::`。开头的 `::` 会从全局/根作用域开始查找，并忽略当前命名空间、本地别名和 `using namespace` 的回退结果。

## 运算符重载

用户定义类型可以声明运算符方法，使类型参与特定运算。运算符重载必须满足编译器规定的参数和返回类型规则；不应仅为了缩短代码而重载含义不直观的运算符。

成员运算符以当前对象作为隐含的左操作数；非成员运算符函数可以显式声明多个操作数：

```VeroScript
class Vec
{
public:
	int x;

	Vec(int value)
	{
		x = value;
	}

	Vec operator+(Vec other)
	{
		return Vec(x + other.x);
	}

	void operator+=(Vec other)
	{
		x = x + other.x;
	}

	bool operator==(Vec other)
	{
		return x == other.x;
	}

	int operator[](int index)
	{
		return x + index;
	}
};

Vec operator+(int value, Vec vector)
{
	return Vec(value + vector.x);
}

Vec sum = Vec(3) + Vec(4);
Vec shifted = 1 + Vec(2);
int item = sum[0];
```

运算符重载可以覆盖二元运算、一元运算、复合赋值、索引和函数调用等形式。类型还可以声明转换运算符，例如 `bool operator bool()`，使对象能够显式转换为布尔值：

```VeroScript
class Token
{
public:
	int value = 42;
	bool operator bool()
	{
		return value != 0;
	}
};

int main()
{
	Token token = Token();
	bool available = token;
	print(available);
	return 0;
}
```


内置布尔运算符 `&&` 和 `||` 始终保持短路求值，不会被用户自定义重载替换。只有当至少一个操作数属于可重载的用户类型或宿主类型时，才会考虑用户自定义运算符候选；因此，需要先转换两个基本类型操作数的运算符不会被选中。

某些比较运算可以根据已声明的基础运算符获得镜像行为，例如使用 `operator<` 支持相反方向的比较；代码仍应优先声明能够直接表达类型语义的运算符。运算符重载应保持类型的自然含义，避免让相同符号承担与常见语义无关的行为。

## 运算符重载中的引用

运算符实参按 C++ 风格隐式绑定引用参数：`a + b`、`a[i]` 和 `f(x)` 不必添加 `&` 或 `ref`。普通函数与方法调用仍要求显式引用实参标记。`T&` 只能绑定类型别名归一化后类型一致且可写的存储位置，不能绑定常量、临时值或需要数值转换的实参；`const T&` 可以绑定只读存储和转换后生成的临时值。

其余转换相同时，可写左值优先匹配 `T&` 而不是 `const T&`。按值与引用重载的转换无法区分时会产生歧义，编译器会拒绝调用。

返回 `T&` 的运算符表达式可以作为赋值目标或局部引用别名的来源；返回 `const T&` 时只能读取。接收对象的 const 限制和成员访问权限同样适用于运算符。引用返回仍受生命周期检查约束；`return this` 引用当前对象的句柄。

前置自增/自减的成员重载不带显式参数，后置版本带一个按值 `int` 标记参数。自由函数的前置版本接收操作数，后置版本再增加该标记。显式重载决定表达式的返回值：返回 `void` 的复合赋值不能当作对象值使用，非 void 返回值也不会被自动写回赋值目标。

`operator()` 支持默认实参及本语言的有类型可变参数，其他运算符不允许这两者。成员 `operator=` 接收一个参数，不支持自由函数 `operator=`。变量声明中的初始化保持原有初始化语义。

只有 `operator()` 和 `operator[]` 可以声明为 `static`。继承的运算符使用其声明类型的泛型实参。静态字段和命名空间全局变量可以绑定引用参数；临时结构体的字段不能绑定可写引用。

这些规则扩展现有的 VeroScript 运算符模型。自由函数 `operator[]`/`operator()`、比较镜像以及合成复合赋值仍是本语言的扩展。类句柄与结构体值保留 VeroScript 的语义；这不代表完整支持 C++ 指针、右值引用或移动语义。

本节新增能力适用于 VM、Mixed 与 JIT 执行后端。源码 AOT 导出器有独立的能力边界：这些引用运算符组合、赋值重载、后置标记以及调用运算符的默认/可变参数暂未适配，会明确拒绝导出。

```VeroScript
class Cell
{
public:
	int value = 0;
	int& operator[](int index)
	{
		return value;
	}
	const int& operator[](int index) const { return value; }
	void operator()(int& target, const int& delta = 1) const { target += delta; }
	Cell& operator=(const int& next)
	{
		value = next;
		return this;
	}
	Cell& operator++()
	{
		value++;
		return this;
	}
	Cell operator++(int tag)
	{
		Cell old = Cell();
		old.value = value;
		value++;
		return old;
	}
};

void main()
{
	Cell cell = Cell();
	cell[0] = 3;
	cell[0] += 2;
	int number = 4;
	cell(number);             // number == 5
	cell(number, 2);          // number == 7
	print((cell++).value);    // 5
	print((++cell).value);    // 7
	(cell = 9)[0] = 10;
	const Cell view = cell;
	print(view[0]);           // 10
}
```

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/operators.md) · [fr-FR](../../fr-FR/language-basics/operators.md) · [ja-JP](../../ja-JP/language-basics/operators.md) · [ko-KR](../../ko-KR/language-basics/operators.md) · [zh-CN](operators.md) · [zh-TW](../../zh-TW/language-basics/operators.md)
