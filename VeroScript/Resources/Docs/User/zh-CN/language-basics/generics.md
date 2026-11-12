# 泛型

泛型允许类型或函数在声明时使用类型参数，并在使用时替换为具体类型。VeroScript 支持泛型结构体、类、接口和函数，也支持 `where` 类型约束。

## 泛型类型

在类型名称后的尖括号中声明类型参数：

```VeroScript
class Duo<T, U = int>
{
public:
	T first;
	U second;

	Duo(T first_value, U second_value)
	{
		first = first_value;
		second = second_value;
	}
};

Duo<string> text_pair = Duo<string>("score", 10);
Duo<int, string> number_pair = Duo<int, string>(10, "points");
```

尾部类型参数可以有默认类型，并且默认类型可以引用前面已经声明的类型参数。显式类型参数必须是前缀，后续参数可以由上下文或实参推导。

## 泛型函数

泛型函数在函数名称后声明类型参数：

```VeroScript
T identity<T>(T value)
{
	return value;
}

string text = identity<string>("VeroScript");
int number = identity(42);
```

如果实参足以确定类型，调用时可以省略类型参数。需要明确选择某种类型时，可以显式提供类型参数。

`template<T>` 是泛型声明的另一种可读写法，与名称后的 `<T>` 遵循相同的泛型规则：

```VeroScript
template<T>
T identity_again(T value)
{
	return value;
}
```

## 类型约束

使用 `where` 限制类型参数必须满足的接口契约：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

T read_once<T>(T value) where T : IReadable
{
	return value;
}
```

一个类型参数可以包含多个接口约束：

```VeroScript
T describe<T>(T value) where T : IReadable, IName
{
	return value;
}
```

约束在编译阶段检查。调用泛型函数或实例化泛型类型时，实际类型必须满足声明中的全部约束。

接口类型别名或别名链也可以作为约束：声明 `using Readable = IReadable;` 后，可写 `where T : Readable`。指向 `IBox<int>` 等具体泛型接口实例的别名同样支持。别名在声明所属模块中解析，实际类型仍须实现解析后的接口。

## 泛型成员和接口

泛型类型可以继承接口或泛型基类，接口本身也可以带类型参数：

```VeroScript
interface IBox<T>
{
	virtual T get() const = 0;
};

class Box<T> : public IBox<T>
{
public:
	T value;

	T get() const override
	{
		return value;
	}
};
```

泛型声明中的成员仍遵循普通类型的访问控制、虚方法、重载和静态成员规则。泛型静态成员在每个具体类型实例之间独立保存，详见[静态成员](static-members.md)。

## 类型参数的使用边界

类型参数只能用于编译器能够根据其声明确定的操作。需要调用接口方法或访问约束成员时，应声明对应的 `where` 约束；不要依赖未声明的隐式成员。

`T::member` 这样的限定静态访问在替换为具体类型后检查，该类型必须提供匹配且可访问的静态字段或方法；访问、重载及 const／引用规则仍适用。自身类型、别名与字段默认值示例见[静态成员](static-members.md)。

嵌套类型、局部声明、访问规则和分段泛型实参请参见[类型作用域](scope.md)。

## 方法模板与值参数

`T`、`typename T`、`class T` 都声明类型参数。普通实例方法、静态方法和有正文的非虚接口辅助方法可以有自己的参数；内层参数不能遮蔽外层参数。方法模板不能声明为 virtual、override 或 final。

```VeroScript
template<typename T>
class Box
{
public:
	template<class U = T>
	U echo(U value)
	{
		return value;
	}
};

template<typename T, int Count = 4>
struct Buffer
{
	T values[Count];
	int capacity() const { return Count; }
};
```

值参数支持 bool、固定宽度整数和脚本／宿主枚举，也可写 `template<typename T, T Value>`。值是不可写的编译期常量。支持纯标量运算、受检转换、枚举成员及可求值的 const 初始化式；允许求值合格的 [constexpr 函数](../language-reference/constexpr.md)，不执行其他函数或宿主回调。溢出、除零、越界及错误枚举身份会报错。固定数组长度在实例化后检查。

一个参数有默认项后，后续自有参数必须都有默认项。默认项在声明位置绑定，可引用外层及本表前面的参数。调用先使用显式前缀，再从实参类型结构推导，最后填补默认项。值不从运行时实参的数值推导。

`box.echo<string>("x")` 显式选择模板；`box.echo("x")` 允许推导。`f<>()` 只考虑模板候选。转换质量先于模板偏好；质量相同时普通候选优先，其余只使用可证明的结构专用性，无法区分则报歧义。类型名按自有参数总数精确匹配优先，再考虑尾部默认项；多份可补全声明报歧义。

`identity<int>`、`box.echo<string>` 和静态方法引用可以形成 callable；明确的 `fn(...) -> ...` 目标签名也可帮助推导。动态字符串名称不会自动实例化开放方法模板。类外定义可使用每层 owner 和方法各一组 `template<...>`，按位置映射参数，不能重复默认项。

参见 `method_templates.vs`、`value_template_parameters.vs` 和 `template_methods_main.vs`。这些能力使用 Core VM／Mixed／JIT 或 Core AOT；独立源码导出器在输出前报告不支持的模板能力。编译库保留开放声明，消费者可在没有原始源码时使用新的实参；反射查询开放声明不会自动生成可执行实例。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/generics.md) · [fr-FR](../../fr-FR/language-basics/generics.md) · [ja-JP](../../ja-JP/language-basics/generics.md) · [ko-KR](../../ko-KR/language-basics/generics.md) · [zh-CN](generics.md) · [zh-TW](../../zh-TW/language-basics/generics.md)
