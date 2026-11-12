# 常量

常量是不能通过指定名称修改的变量或成员。VeroScript 使用 `const` 声明常量。

`const` 表示只读，不要求编译期初始化。需要支持域中的标量值在编译时确定时，使用 [constexpr](../language-reference/constexpr.md)。

## 常量变量

`const` 可以用于局部变量和模块级变量。常量必须在声明时初始化：

```VeroScript
const int max_count = 100;

int main()
{
	const string product_name = "VeroScript";
	print(product_name);
}
```

常量初始化完成后，不能再通过该名称赋值：

```VeroScript
const int limit = 10;
// limit = 20; // Compile error.
```

常量的类型仍然是声明中指定的类型。`const` 不会把值转换成另一种类型，也不会改变其作用域。

## 常量成员

结构体或类可以声明常量字段：

```VeroScript
class Settings
{
public:
	const int version = 1;
};
```

没有声明初始化器的非静态字段，也可以改由构造初始化列表初始化：

```VeroScript
class Settings
{
public:
	const int version;

	Settings(int initial_version)
		: version(initial_version)
	{
	}
};
```

初始化列表只能命名当前类型直接声明的实例字段。构造初始化列表中的直接基类项先执行；随后，该列表中的字段项按照字段声明顺序执行，最后才执行构造函数体。已有声明初始化器的字段不能再次出现在构造初始化列表中，同一个字段也不能重复出现。构造函数体内的写入仍属于普通赋值，因此不能修改 `const` 字段。声明初始化器以及列表中省略的字段继续采用既有的默认构造规则。

## `const` 成员函数

成员函数可以在参数列表后使用 `const`，表示该函数不能修改当前对象的可变状态：

```VeroScript
class Counter
{
public:
	int value = 0;

	int get() const
	{
		return value;
	}
};
```

`const` 成员函数适合用于查询对象状态。是否能够调用某个成员函数，由当前对象的常量属性和函数的 `const` 修饰共同决定。

### `mutable` 例外

```VeroScript
struct Label
{
	string source;
	mutable string cached;

	string text() const
	{
		if (cached == "")
		{
			cached = source;
		}
		return cached;
	}
};
```

此例外只适用于被标记的字段。它不会让 const 对象整体可写，也不会保证并发访问安全。`mutable` 字段不能同时是 `const`、`static` 或引用字段。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/constants.md) · [fr-FR](../../fr-FR/language-basics/constants.md) · [ja-JP](../../ja-JP/language-basics/constants.md) · [ko-KR](../../ko-KR/language-basics/constants.md) · [zh-CN](constants.md) · [zh-TW](../../zh-TW/language-basics/constants.md)
