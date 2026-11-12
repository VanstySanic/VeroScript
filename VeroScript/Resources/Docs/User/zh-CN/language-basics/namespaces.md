# 命名空间

命名空间用于组织名称，避免不同功能之间发生名称冲突。它可以包含变量、函数、结构体、类、接口、枚举和其他类型声明。

## 声明和限定名称

使用 `namespace` 声明命名空间，使用 `::` 访问其中的成员：

```VeroScript
namespace Util
{
	int Seed = 10;

	int next_seed()
	{
		Seed = Seed + 1;
		return Seed;
	}
}

int current = Util::Seed;
int next = Util::next_seed();
```

限定名称明确表示成员所属的命名空间。对于类型成员、枚举成员和静态成员，也推荐使用 `::`：

```VeroScript
enum class State
{
	Ready,
	Running
};

State state = State::Running;
```

## 全局限定

开头的 `::` 会从全局/根作用域开始名称查找：

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

`::GlobalValue` 不会回退到当前命名空间、本地类型别名或 `using namespace`。它也可以限定根级类型或命名空间，`using namespace ::Name` 同样有效。

## 匿名命名空间

`namespace { ... }` 用于声明匿名命名空间。在 VeroScript 中，它是透明的声明分组：不会创建合成命名空间，也不会提供 C++ 式的翻译单元内部链接。成员仍然属于外层命名空间。

在全局作用域中，可以不增加限定地使用匿名命名空间的成员：

```VeroScript
namespace
{
	int transparent_helper()
	{
		return 40;
	}
}

int main()
{
	print(transparent_helper() + 2);
	return 0;
}
```

在命名命名空间内部也遵循同样的规则，成员会保留外层命名空间限定：

```VeroScript
namespace Outer
{
	namespace
	{
		int nested_helper()
		{
			return 9;
		}
	}
}

int main()
{
	print(Outer::nested_helper());
	return 0;
}
```

匿名命名空间不会改变导出规则或模块可见性。需要供其他模块使用的声明应分别添加 `export`；当透明的分组有助于组织源码时，可以使用匿名命名空间。

## 标准命名空间

内建容器、通用值类型和公开运行时函数推荐使用保留的 `std` 命名空间：

```VeroScript
import "string.vs";

std::array<int> values = {1, 2, 3};
std::optional<int> selected = std::optional<int>(values[0]);
std::print(std::format("selected: {}", selected.get()));
```

原有的 `array`、`optional`、`print`、`format` 等全局写法继续作为兼容别名。
`std::` 在编译期解析，不会增加 VM 或 JIT 运行时分派。`std::sqrt` 等源码标准库
函数仍需正常导入对应模块，限定名称不会绕过 `import`/`export`。全局 `std`
命名空间为语言保留，脚本不能向其中添加声明。

命名空间用于名称组织，不会自动改变成员的导出状态。模块外部是否能够访问某个声明，仍由模块的 `export` 规则决定。

完整的类型和函数别名表见[`std` 标准命名空间](standard-namespace.md)。

## 使用命名空间

`using namespace` 可以把命名空间中的直接成员引入当前包含作用域：

```VeroScript
namespace MathTools
{
	int base = 2;
}

using namespace MathTools;

int value = base + 3;
```

使用指令只影响它所在的包含作用域和后续代码，不会重新导出名称，也不会影响同级的其他命名空间。名称可能冲突时，应保留完整限定名：

```VeroScript
int value = MathTools::base + 3;
```

命名空间适合表达稳定的功能边界；当目标是从另一个源文件引入声明时，应使用模块的 `import`。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/namespaces.md) · [fr-FR](../../fr-FR/language-basics/namespaces.md) · [ja-JP](../../ja-JP/language-basics/namespaces.md) · [ko-KR](../../ko-KR/language-basics/namespaces.md) · [zh-CN](namespaces.md) · [zh-TW](../../zh-TW/language-basics/namespaces.md)
