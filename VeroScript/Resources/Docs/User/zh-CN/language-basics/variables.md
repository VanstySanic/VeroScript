# 变量

变量是具有名称和类型、可以在运行时保存值的声明。变量可以在模块、函数、代码块、结构体或类中声明。

## 声明

变量声明指定类型、名称和初始化表达式：

```VeroScript
int count = 0;
int limit = 10;
string name = "VeroScript";
```

局部变量和模块级变量必须初始化，`int count;` 是无效写法。可以提供显式值、构造调用或花括号初始化。结构体和类的字段可以省略初始化表达式，此时使用对应类型的字段默认值规则。

## 初始化

VeroScript 支持使用构造调用和花括号初始化：

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
```

用户定义类型可以直接调用构造函数：

```VeroScript
struct Point
{
	int x;
	int y;
};

int main()
{
	Point point = Point();
	point.x = 10;
	point.y = 20;
}
```

## 赋值

变量声明完成后，可以通过赋值表达式修改其值：

```VeroScript
int value = 1;
value = 2;
value += 3;
```

赋值两侧的类型必须满足语言规定的可转换关系。赋值不会改变变量的静态类型。

## 局部变量和全局变量

函数或代码块中的变量是局部变量。模块顶层声明的变量是全局变量，可以被同一模块中的函数访问：

```VeroScript
int global_count = 0;

void increment()
{
	global_count += 1;
}
```

跨模块访问全局变量需要满足模块导入和导出规则。变量的可见范围与其作用域不同：作用域描述名称在哪里有效，可见性规则还会受到模块和访问修饰符影响。

## `auto` 变量

`auto` 根据初始化表达式推导类型：

```VeroScript
auto answer = 42;
auto text = "hello";
```

`auto` 变量必须初始化。类型推导完成后，变量仍然是静态类型变量，不能在后续赋值中变成另一种类型。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/variables.md) · [fr-FR](../../fr-FR/language-basics/variables.md) · [ja-JP](../../ja-JP/language-basics/variables.md) · [ko-KR](../../ko-KR/language-basics/variables.md) · [zh-CN](variables.md) · [zh-TW](../../zh-TW/language-basics/variables.md)
