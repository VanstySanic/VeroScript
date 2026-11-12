# VeroScript 概述

VeroScript 是一种面向嵌入式场景的跨平台脚本语言。它采用熟悉的 C++ 风格语法，同时拥有独立的类型系统、模块系统和运行时。源文件使用 `.vs` 扩展名。

原生 Runtime 和 SDK 面向 Windows、Linux 和 macOS；浏览器及 JavaScript 宿主使用 [WebAssembly SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md)，Android 和 iOS 则使用[实验性移动适配器](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/zh-CN/index.md)。各产品可用的组件、执行后端和发行包因目标与版本而异，安装和部署要求见[安装与分发](../../../../../VeroScriptInstaller/Resources/Docs/User/zh-CN/index.md)及相应 SDK 指南。

VeroScript 适合把一部分逻辑从宿主程序中分离出来，让这部分逻辑能够独立开发、检查、运行和重新加载。游戏逻辑、编辑器工具、自动化任务和应用内扩展，都是典型的使用场景。

需要注意的是，VeroScript 不是 C++ 的子集，也不是一个可以直接编译 C++ 源文件的解释器。它借鉴了 C++ 的语法和面向对象模型，但脚本的类型语义、内存模型和可用能力由 VeroScript 自己定义。

## 运行模型

脚本从源代码到执行，大致经过以下步骤：

```text
.vs 源文件
  -> 预处理
  -> 词法分析和语法分析
  -> 语义分析与编译
  -> 字节码
  -> VeroScript VM 执行
```

预处理器支持条件编译、对象宏和函数宏、字符串化、标记拼接以及文件包含。编译阶段负责类型检查、重载解析、泛型实例化和继承关系检查等工作。模块通过 `import` 和 `export` 组织，模块加载器会进一步处理模块路径、隐式导入、包依赖和循环依赖。

常规运行以虚拟机为基础，默认使用混合后端；在支持的环境中，运行时可以使用更快的执行后端，并在必要时回退到虚拟机。VeroScript 也可以生成字节码缓存，以减少后续启动时的编译开销。

## 语言范围

下面的表格用于快速了解 VeroScript 的语言范围，具体语法和规则应以相应的语言文档为准。

| 领域 | 主要内容 |
| --- | --- |
| 基础类型 | 整数、浮点数、布尔值、字符串、宽字符串、枚举和 `nullptr` |
| 数据结构 | 数组、映射、集合、队列、栈、双端队列、优先队列和通道 |
| 类型系统 | `struct`、`class`、`interface`、命名空间、类型别名和访问控制 |
| 面向对象 | 继承、接口、虚函数、重写、静态成员、构造函数和析构函数 |
| 泛型与重载 | 泛型类型和函数、`where` 约束、默认类型参数、函数/方法/运算符重载 |
| 函数能力 | 默认参数、可变参数、引用参数、引用返回、引用别名、函数值、Lambda 和引用捕获 |
| 控制流 | 条件、循环、`foreach`、`switch`、跳转、标签和返回 |
| 标准库 | 字符串、路径、数学、格式化、文件、输入输出和并发 |

`struct` 用于表达具有值语义的数据；`class` 用于表达具有共享身份和生命周期的数据。接口、继承、虚调用和泛型则用于构建可复用的脚本抽象。

参阅[命令行工具 — 后端选择](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md#cli-backend-policy)。

性能测量应使用非调试的普通运行，固定输入和后端，并等待正常预热后再比较。字节码缓存只会降低启动时的编译成本，不会改变脚本实际执行的工作量。

## 运行脚本

使用命令行工具可以直接运行一个脚本：

```powershell
veroscript hello.vs
```

常用命令如下：

```powershell
# Check without executing
veroscript --check hello.vs

# Start debugging mode
veroscript --debug hello.vs

# Package as a runnable application
veroscript --emit-package app.vsapp hello.vs
```

脚本需要提供 `main` 入口。入口可以返回 `int`，也可以返回 `void`。推荐使用 `int main`，返回值会成为进程退出码；`void main` 仍然受支持，并按成功退出处理。无参数入口和带命令行参数的入口分别如下：

```VeroScript
int main()
{
	print("Hello, VeroScript!");
	return 0;
}
```

```VeroScript
int main(string[] args)
{
	print(len(args));
	return 0;
}
```

传递给程序的命令行参数可以从 `args` 中读取；脚本文件路径本身不包含在 `args` 中。

VeroScript 支持三种常见文件形式：`.vsapp` 是可运行的应用包，`.vslib` 是供其他脚本导入的库包，`.vsbc` 是字节码缓存文件。库包不能直接作为应用运行。

## 嵌入 C++ 宿主程序

VeroScript 的一个主要用途是嵌入 C++ 应用。宿主程序可以加载或编译脚本，调用脚本函数、方法和构造函数，也可以把宿主能力注册给脚本使用。可注册的内容包括原生函数、宿主类型、字段、方法、运算符、泛型类型和类型别名。

`VeroScriptEngine` 提供检查和运行脚本的高层入口；`VeroScriptSession` 用于需要更细粒度控制模块、绑定和运行时状态的集成场景。宿主还可以接收诊断、输出和调试回调，并查询或修改脚本中的类型、字段和全局值。

如果宿主不是 C++，可以使用 `VeroScriptInterop` 的 C11/C++17 ABI。它通过不透明会话和值句柄提供跨语言调用、宿主绑定、包、反射 JSON、序列化和回调能力，不要求适配器依赖 C++ 对象布局。浏览器和 WebAssembly 场景则使用独立的 `VeroScriptWebAssembly` SDK：它提供 TypeScript 包装层、语言目录发现、内存中的源代码检查/运行和虚拟导入文件。

热重载需要显式启用，默认关闭。函数身份、捕获名称及类型保持兼容时，全局量、对象、容器和闭包（包括捕获的 `this`）可以迁移。活动通道、不兼容的类型变化和捕获布局变化仍会被拒绝。宿主原生代码产生的外部副作用不属于脚本状态事务。

## 工具链与 AOT

语言工具使用与编译器相同的预处理和语义分析基础，因此可以提供诊断、代码大纲、语义标记、跳转、悬停、补全、签名帮助、引用查找、重命名和格式化等功能。调试器通过 DAP 工作，调试时使用虚拟机执行路径。

在不需要动态加载的场景下，可以使用 AOT 后端把脚本生成 C++17 源文件，再交给本地编译器构建。AOT 和虚拟机是两种不同的执行方式，具体选择取决于宿主程序的部署和调试需求。

## 版本说明

本文档描述当前公开的语言能力。脚本、字节码缓存、包和宿主绑定应使用相互兼容的版本。

VeroScript 有意保持独立的运行时边界。当前版本不提供脚本指针语义和独立的 C++ 概念语法；可变参数宏的 `__VA_OPT__`、引用捕获、引用返回和运行时反射属于当前公开语言能力，但仍遵循 VeroScript 自己的生命周期与类型规则。图形界面也不属于核心运行时。

完整文档从[文档首页](../../../../../Resources/Docs/User/zh-CN/index.md)开始。语言基本语法和语义见[语言基本特性](language-basics/index.md)，标准库 API 见[标准库参考](standard-library/index.md)，命令行、调试、AOT、宿主集成、WebAssembly/WASM 和 Interop SDK 见[工具链](../../../../../Resources/Docs/User/zh-CN/toolchain/index.md)。

<a id="backend-selection"></a>

## 服务后端选择

宿主或 CLI 提供已安装／已部署的模块环境后，未指定的后端先继承环境的 `services.<service>.backend`；环境也未配置时，`auto` 按后端 ID 升序尝试已启用的候选，选择首个兼容且能成功创建的工厂。显式 ID、工厂以及文件／目录优先，失败不会悄悄改用其他 ID。后端 ID 如 `asmjit`，安装包名如 `vs-asmjit`，二者用途不同。

显式 `auto` 可覆盖环境默认值；`none` 禁用服务，配置中明确保存的空后端值也表示禁用。“未配置”与“保存空值”不同。产品装配的 VM 仅在自动包选择找不到可用解释器时回退使用。没有可用的可选 JIT 时，Mixed 使用 VM；强制 JIT 仍要求可用且兼容的后端。必需服务没有可用候选时明确报错。

选择只发生在工厂创建与准备阶段，业务开始后不替换后端，运行错误不会重放操作。解释器执行域在开始执行后仍会固定。限额、取消、有界清理、调试与热重载的执行规则保持原样。工厂和实例保留模块及环境所有者。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/introduction.md) · [fr-FR](../fr-FR/introduction.md) · [ja-JP](../ja-JP/introduction.md) · [ko-KR](../ko-KR/introduction.md) · [zh-CN](introduction.md) · [zh-TW](../zh-TW/introduction.md)
