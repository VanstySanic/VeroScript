# 类型别名与特殊声明

VeroScript 支持类型别名，以及使用 `= default` 和 `= delete` 明确控制构造函数、析构函数、方法、运算符或自由函数的声明状态。

## 类型别名

使用 `using` 为已有类型创建别名：

```VeroScript
using Ints = array<int>;
using Scores = map<string, int>;

Ints values = Ints{1, 2, 3};
Scores scores = Scores();
```

`typedef` 也可以用于类型别名：

```VeroScript
typedef Names = array<string>;
Names names = Names();
```

类型别名不会创建新的运行时类型。别名可以在模块作用域或局部代码块中声明，局部别名只在其作用域内可见。

## 导出类型别名

在模块作用域（包括命名空间内）使用 `export using` 或 `export typedef`。两者都采用 `Alias = Type` 写法。普通别名仅在所属模块内可见，局部别名仅在所属作用域内可见。导入方可以使用带命名空间限定的公开别名；`export import` 会再导出公开别名，不会公开私有别名。`using namespace` 只影响所属模块和作用域的名称查找，`export using namespace` 会被拒绝。

保存为 aliases.vs：

```VeroScript
namespace Measures
{
	export using Count = int;
	export typedef Label = string;
	using InternalCount = int;
}
```

```VeroScript
import "aliases.vs";

int main()
{
	Measures::Count count = 42;
	Measures::Label label = "total";
	print(count);
	return 0;
}
```

将使用方保存为 main.vs，执行 `veroscript main.vs`，输出 `42`。也可以改为导入包含 `export import "aliases.vs";` 的门面模块。需要分发二进制库时，执行 `veroscript --emit-package aliases.vslib aliases.vs`，再将使用方的导入目标改为 `"aliases.vslib"`。

## 默认实现

构造函数和析构函数可以使用 `= default` 请求编译器生成默认实现：

```VeroScript
struct Token
{
	int value = 7;

	Token() = default;
	~Token() = default;
};
```

默认构造会结合字段默认值和类型的默认规则。显式写出 `= default` 可以表达“使用生成实现”的意图，并使声明出现在类型的公开接口中。

## 删除声明

使用 `= delete` 可以保留一个签名，同时明确禁止该调用形式：

```VeroScript
struct Token
{
	Token() = default;
	Token(string text) = delete;

	int read() const
	{
		return 7;
	}
	int read(string mode) const = delete;
};
```

调用被删除的构造函数、方法、运算符或函数会产生编译诊断。删除声明常用于阻止不希望支持的隐式调用或错误类型重载；它不会在运行时执行一个空函数。

`= default` 和 `= delete` 是声明的一部分，不能与同一声明的函数体同时使用。虚函数或接口槽位仍必须满足继承和实现规则。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/declarations.md) · [fr-FR](../../fr-FR/language-basics/declarations.md) · [ja-JP](../../ja-JP/language-basics/declarations.md) · [ko-KR](../../ko-KR/language-basics/declarations.md) · [zh-CN](declarations.md) · [zh-TW](../../zh-TW/language-basics/declarations.md)
