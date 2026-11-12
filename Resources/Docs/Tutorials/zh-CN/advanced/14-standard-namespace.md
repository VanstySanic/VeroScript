# 14：使用 `std` 标准命名空间

本篇把一个使用未限定模块声明的小脚本整理为推荐的公开 `std` 写法。`std::` 是源码层面的标准命名空间别名；它不会创建新的容器类型，也不会改变运行时行为。

## 第一步：准备一个评分脚本

创建 `scores.vs`：

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	array<int> scores = {81, 92, 88};
	optional<int> selected = optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	print(format("selected={}, root={}", selected.get(), root));
	return 0;
}
```

先检查并运行它：

```powershell
veroscript --check scores.vs
veroscript scores.vs
```

第一个版本已经限定了模块所属的数学函数。模块所属的标准库函数只能通过其公开的 `std` 命名空间访问；只有精简的核心内建 API 保持全局可用。

## 第二步：迁移到 `std::`

只修改名称限定，不改变算法：

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	std::array<int> scores = {81, 92, 88};
	std::optional<int> selected = std::optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	std::print(std::format("selected={}, root={}", selected.get(), root));
	std::print(std::strings::contains("scores", "core"));
	return 0;
}
```

再次运行相同的检查和执行命令。`std::array<int>` 与 `array<int>` 指向相同的语言类型；`std::print` 与 `print` 也指向相同的运行时函数。`std::` 在编译阶段解析，不会增加额外的运行时分派。

数学、I/O、文件系统、字符串、路径、并发、时间、线程、环境和强类型输入入口均只能通过对应命名空间访问。不提供它们的标准库未限定写法，也不提供 `std::now` 这类扁平兼容名称；用户代码仍可定义同名但无关的未限定函数。

## 第三步：理解导入边界

标准命名空间别名不等于模块导入。源码标准库函数仍然需要导入提供它们的模块：

- `math.vs` 提供 `sqrt`、`sin`、`cos`、`tan`、`floor`、`ceil`、`round`、`trunc`、`pow`、`exp`、`log`、`log2`、`asin`、`acos`、`atan2`、`isfinite`、`isnan`、`pi`、`e`、`clamp` 和 `clamp_int`。
- `string.vs` 通过 `std::strings` 提供字符串查询，包括 `last_find`、`repeat`、`remove_prefix` 和 `remove_suffix`。
- `io.vs` 通过 `std::io` 提供流操作，通过 `std::filesystem` 提供文件系统操作。
- `path.vs` 向 `std::filesystem` 补充路径操作；`concurrent.vs` 使用 `std::concurrent`。

运行时控制台函数不需要导入模块。无换行输出使用 `std::console::write` 和
`std::console::error`，发布等待中的输出使用 `std::console::flush()`，
检测交互式终端使用 `std::console::is_interactive()`。

例如，删除 `import "math.vs";` 后，`std::sqrt` 不会因为带有 `std::` 限定而自动加载数学模块。先导入模块，再使用限定名称，能让依赖关系保持清晰。

## 第四步：迁移容器和通用值类型

标准命名空间也覆盖映射、集合、队列、通道、任务和反射信息类型：

```VeroScript
int main()
{
	std::map<string, int> totals = {"math": 2, "io": 1};

	std::print("math={}, io={}", totals["math"], totals["io"]);
	return 0;
}
```

新代码应统一使用分组后的 `std` 写法。实现声明仍保留在各自模块内部的未限定名称，使命名空间层能够让所有后端解析到同一符号身份。

## 第五步：明确值保护和转换

当前示例还把 `std::readonly<T>` 别名与全局转换内置函数组合起来使用。`std::` 并不是所有内置函数的前缀，因此 `cast<T>`、`try_cast<T>` 和 `const_cast<T>` 应保持未限定写法。

```VeroScript
std::readonly<int> published_score = std::readonly<int>(selected.get());
double exact_score = cast<double>(scores[1]);
std::optional<int> narrowed = try_cast<int>(exact_score);

const std::array<int> explicitly_unlocked = {13};
// 只有确定底层存储可写时，才使用这个有意的 const 解除。
const_cast<std::array<int>>(explicitly_unlocked).push(published_score.get());
```

`cast<T>` 要求执行转换；`try_cast<T>` 返回可由调用方检查的 `optional<T>`。`const_cast<T>` 不会转换值，也不会执行运行时类型检查，只会移除编译期的 const 访问能力，因此必须在明确且受控的场景中使用。完整规则请参阅[类型转换](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-reference/type-conversions.md)。

## 根限定名称

开头的 `::` 会从全局/根作用域开始查找，跳过当前命名空间、本地别名和 `using namespace` 的回退结果：

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

需要明确引用根级声明时使用 `::Name`。`std::readonly<T>` 是 `readonly<T>` 包装器的标准命名空间写法，是源码别名，不是第二个运行时类型。


## 练习

1. 把示例中的 `std::map` 换成 `std::unordered_map` 不适用；请查阅完整别名表，选择当前支持的容器并说明原因。
2. 增加一个 `std::set<int>`，把重复分数过滤掉。
3. 导入 `string.vs`，调用 `std::strings::contains` 或 `std::strings::starts_with`。

完整别名表见[`std` 标准命名空间](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/standard-namespace.md)，可运行示例见 [`examples/standard_namespace.vs`](../../../../../VeroScript/Resources/Scripts/examples/standard_namespace.vs)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/advanced/14-standard-namespace.md) · [fr-FR](../../fr-FR/advanced/14-standard-namespace.md) · [ja-JP](../../ja-JP/advanced/14-standard-namespace.md) · [ko-KR](../../ko-KR/advanced/14-standard-namespace.md) · [zh-CN](14-standard-namespace.md) · [zh-TW](../../zh-TW/advanced/14-standard-namespace.md)
