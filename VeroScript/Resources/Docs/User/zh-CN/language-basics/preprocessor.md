# 预处理器

预处理器在词法分析和语义分析之前处理源文件。它支持条件编译、对象宏、函数宏、字符串化、标记拼接和模块包含。

## 对象宏

对象宏把名称替换为预定义的标记序列：

```VeroScript
#define VERSION 2
#define MAX_COUNT 64

int version = VERSION;
int capacity = MAX_COUNT;
```

宏替换发生在编译器进行类型检查之前。宏本身没有 VeroScript 的静态类型，替换后的表达式必须能够通过正常的语法和类型检查。

## 函数宏

函数宏可以接收参数。复杂表达式中的参数应使用括号，避免替换后的运算优先级改变：

```VeroScript
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int value = MAX(3, 8);
```

宏适合表达简单的词法替换；需要类型检查、重载或调试能力时，应优先使用函数或泛型函数。

## 字符串化和标记拼接

在函数宏中，`#` 将参数转换为字符串，`##` 将相邻标记拼接为一个标记：

```VeroScript
#define STR(x) #x
#define CAT(a, b) a##b

int VALUE = 9;
string name = STR(hello);
int result = CAT(VAL, UE);
```

字符串化和标记拼接都在宏展开阶段执行。拼接后的结果仍必须是合法的 VeroScript 标记。

## 可变参数宏

函数宏可以使用 `...` 接收可变参数，并通过 `__VA_ARGS__` 展开：

```VeroScript
#define LOG(fmt, ...) print(fmt, ##__VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

当前实现支持使用 `##__VA_ARGS__` 消除没有可变参数时多余的逗号，也支持 `__VA_OPT__(tokens)`：当可变参数非空时展开括号内的标记，否则展开为空：

```VeroScript
#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

`__VA_OPT__` 只能出现在可变参数宏的替换列表中，并且必须跟随一对括号。

## 动态内置宏

编译器还提供两个按当前源模块动态生成的宏：

| 宏 | 展开结果 |
| --- | --- |
| `__FILE__` | 当前模块的规范逻辑路径；内存源代码没有路径时为 `<memory>`。 |
| `__LINE__` | 宏调用所在物理源行号，从 `1` 开始。 |

它们在每个源模块的预处理上下文中计算，而不是普通的全局宏：

```VeroScript
#define REPORT(name) print("{}:{} {}", __FILE__, __LINE__, name)

int main()
{
	REPORT("ready");
	return 0;
}
```

`__FILE__`、`__LINE__`、`__VA_ARGS__` 和 `__VA_OPT__` 是保留名称，不能被脚本、命令行初始定义、宿主预定义宏或函数宏参数重新定义。

## 条件编译

使用 `#if`、`#ifdef`、`#ifndef`、`#elif`、`#else` 和 `#endif` 控制源代码是否参与后续编译：

```VeroScript
#define VERSION 2

#if VERSION >= 2
int feature_level = 2;
#else
int feature_level = 1;
#endif
```

完整的源码指令集合是 `#include`、`#define`、`#define_export`、`#undef`、`#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif` 和 `#error`。条件中可用 `defined(NAME)` 或 `defined NAME` 判断宏是否存在。表达式会先展开宏；仍未定义的标识符按零处理。

`#if` 表达式使用 64 位整数常量（十进制、C 风格前导零八进制或十六进制）、括号、一元 `+ - ! ~`、算术 `* / % + -`、移位、比较、相等、位运算 `& ^ |`、短路 `&& ||` 和 `?:`。它不使用脚本字面量中的二进制前缀或数字分隔符规则。实际求值分支中的除零和溢出是错误；短路分支不会求值。

活动分支中的 `#error message` 会立即停止预处理，并在诊断中保留裁剪首尾空白后的消息；非活动条件分支会忽略它。消息不会再次进行宏展开。`#warning`、`#line` 和 `#pragma`（包括 `#pragma once`）不受支持，并会按具体指令名报告。

命令行工具可以使用 `-D` 或 `--define` 提供简单对象宏；源文件仍可定义前文所述的函数式宏和可变参数宏：

```powershell
veroscript -D ENABLE_LOGGING=1 script.vs
```

## 包含模块

`#include` 是 `export import` 的别名：导入并转导出公开声明和公开宏，不进行文本包含。

```VeroScript
#include "math.vs"

int main()
{
	print(std::clamp_int(15, 0, 10));
}
```

需要表达模块依赖和导出关系时，推荐使用 `import` 和 `export import`，而不是把 `#include` 当作 C++ 文本包含机制使用。

## 公开宏

<a id="exported-macro-contract"></a>

使用 `#define_export`，定义语法与 `#define` 相同，支持对象宏、函数宏和可变参数宏。普通定义保持私有。显式 `import` 在路径字符串结束后立即引入目标模块最终公开的宏；`export import` 同时再导出这些宏。`#include` 是 `export import` 的别名，包含公开声明和公开宏。`.vslib` 库遵循相同规则。宿主隐式导入也从业务源码开头引入已经确定的公开宏，但不自动转导出。宏提供模块及其显式依赖先独立建立接口；提供模块之间的宏依赖仍写普通 `import`。私有宏保持私有，冲突沿用普通导入规则。例如，`veroscript --implicit-import settings.vs main.vs` 使 `main.vs` 无需编写导入即可使用 `settings.vs` 的公开宏。

```VeroScript
// settings.vs
#define PRIVATE_VALUE 7
#define_export VALUE 21
#define_export DOUBLE(x) ((x) * 2)

```

```VeroScript
// main.vs
import "settings.vs";
int main()
{
	print(DOUBLE(VALUE));
	return 0;
}
```

替换体在调用方当前的宏环境中展开，不捕获私有辅助宏；需要时显式导出辅助宏。`__FILE__` 和 `__LINE__` 指向调用位置。同名的不同原始定义发生冲突，即使替换体相同；替换已导入定义前先 `#undef`。重复导入相同原始定义具有幂等性，`#undef` 后再次导入会恢复定义。普通 `#define` 重新定义后，最终定义为私有；`#undef` 从公开接口中移除定义。

没有公开宏定义的循环依赖继续受支持。循环源码闭包中出现任何 `#define_export`（包括非活动指令）时，公开接口无法确定，会拒绝加载；需要解除源码导入环。库在构建时确定条件导出的结果；应用包不携带公开宏接口。

<a id="preprocess-resource-limits"></a>

## 预处理资源限制

默认情况下，每个文件的展开输出（含中间替换文本）最多为 32 MiB；同一次编译的所有文件共享 268,435,456 个内部计量单位的工作预算。超过任一上限会报告预处理错误并拒绝编译。内部计量单位不是 VM 指令数或执行时间保证。原生嵌入宿主可通过 `FPreprocessContext::Budget` 提供 `FPreprocessBudget`；零表示上限为零，不表示无限。这些限制与脚本编译扩展的执行预算分别计算。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/preprocessor.md) · [fr-FR](../../fr-FR/language-basics/preprocessor.md) · [ja-JP](../../ja-JP/language-basics/preprocessor.md) · [ko-KR](../../ko-KR/language-basics/preprocessor.md) · [zh-CN](preprocessor.md) · [zh-TW](../../zh-TW/language-basics/preprocessor.md)
