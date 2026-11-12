# 重载解析

当同一作用域中存在多个同名函数、方法或运算符时，编译器会根据调用上下文选择唯一的可调用候选。

## 候选函数

重载解析首先根据名称和调用形式收集候选，然后检查：

- 参数数量是否匹配；
- 实参类型是否满足参数类型的转换规则；
- `ref`、`const ref` 或 `&` 标记是否与参数声明一致；
- 泛型类型参数是否能够推导，且 `where` 约束是否满足；
- 默认参数是否能够补齐省略的位置。

返回类型通常不单独用于区分同名重载：

```VeroScript
int parse(int value)
{
	return value;
}

string parse(string value)
{
	return value;
}
```

提供实参时，编译器根据实参类型和可用转换选择更匹配的候选；如果没有候选或多个候选同样匹配，编译会报告诊断。

候选匹配会考虑隐式转换，包括兼容的数值转换和未标记为 `explicit` 的单值转换构造函数。`explicit` 构造函数不会参与普通赋值或参数转换；需要通过直接构造或 `cast<T>` 显式请求。编译器不会串联多个用户自定义转换。

## 引用参数

引用标记是参数匹配的一部分，调用时必须明确写出：

```VeroScript
void update(int& value)
{
	value = value + 1;
}

void update(const int& value)
{
	print(value);
}

int number = 1;
update(&number);
```

不要省略引用标记，也不要把只读引用当作可写引用使用。涉及引用重载时，显式标记可以避免候选选择不明确；`ref T` 和 `ref expression` 仍可作为兼容写法。

## 泛型重载

泛型候选只有在类型参数能够确定、约束满足并且参数可转换时才有效：

```VeroScript
T identity<T>(T value)
{
	return value;
}

int number = identity(42);
string text = identity<string>("text");
```

需要消除推导歧义时，可以提供显式类型参数，或调整函数参数使目标类型能够确定。

## 运算符重载

运算符重载也遵循候选匹配规则。成员运算符包含隐含的当前对象，非成员运算符函数使用显式操作数；参数和返回类型必须满足该运算符的实现规则。运算符重载的声明方式见[运算符](../language-basics/operators.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-reference/overload-resolution.md) · [fr-FR](../../fr-FR/language-reference/overload-resolution.md) · [ja-JP](../../ja-JP/language-reference/overload-resolution.md) · [ko-KR](../../ko-KR/language-reference/overload-resolution.md) · [zh-CN](overload-resolution.md) · [zh-TW](../../zh-TW/language-reference/overload-resolution.md)
