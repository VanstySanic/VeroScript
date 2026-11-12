# C# 转换器

`VeroScriptCSharpTranspiler` 将严格且可以进行静态分析的 C# 12 子集转换为
VeroScript 源码。它结合 `VeroScriptCSharpFrontend` 使用 Roslyn 的语义信息，
不会进行文本令牌替换。它是独立的源码转换工具，与 C# 运行时 SDK、BindGen 以及
VeroScript 到 C# 的 AOT 后端分开。

## 单文件转换

```powershell
veroscript-csharp-transpiler Program.cs -o Program.vs `
	--source-map Program.vs.map.json `
	--veroscript veroscript
```

`--veroscript` 指定用于验证生成源码的 VeroScript 可执行文件或 SDK 根目录。默认
启用验证。只有解析、降级、验证和输出暂存全部成功后，才会写入 VeroScript 文件
和 source map；转换失败时不会替换已有输出。

公开库 API 提供 `CSharpTranspiler.TranspileSource` 和
`CSharpTranspiler.TranspileFile`。结果包含生成源码、source-map JSON 和
`CSharpDiagnostic` 诊断。`CSharpTranspileOptions` 可控制验证、验证器、生成路径、
超时时间和 C# 解析选项。

## 项目转换

```powershell
veroscript-csharp-transpiler Models/User.cs Services/UserService.cs `
	--project-root . `
	--output-dir generated/veroscript `
	--veroscript veroscript
```

项目模式将所有输入作为一个 C# 编译单元进行解析，解析跨文件符号，生成显式导入，
并保留相对于 `--project-root` 的路径。每个输入生成一个 VeroScript 源文件和一个
`.map.json` source map。默认清单为 `transpile.manifest.json`，也可以使用
`--manifest` 指定路径。所有生成文件与清单作为一个输出集合提交；暂存失败时会
回滚。

库 API 的项目入口是 `CSharpTranspiler.TranspileProject`，使用
`CSharpTranspileProjectOptions` 配置。结果包含生成文件记录、确定性的清单 JSON 和
诊断。每个输入都必须位于项目根目录内，并且不能重复提供同一个路径。

如果两个输入会产生相同的相对输出名称，后一个名称会添加确定性的路径哈希后缀。

## 命令行选项

| 选项 | 作用范围与含义 |
| --- | --- |
| `-o`、`--output` | 单文件输出路径。单文件模式必需，项目模式不能使用。 |
| `-h`、`--help` | 输出用法信息并退出。 |
| `--version` | 输出转换器版本并退出。 |
| `--source-map` | 单文件 source map 路径。默认为 `<output>.map.json`，项目模式不能使用。 |
| `--output-dir` | 启用项目模式并选择生成输出目录。 |
| `--manifest` | 项目清单路径。默认为输出目录中的 `transpile.manifest.json`。 |
| `--project-root` | 用于计算项目相对输出名称的根目录。所有输入都必须位于其中。 |
| `--reference` | 添加元数据程序集；多个程序集需要重复此选项。 |
| `--language-version` | 选择解析时使用的 C# 语言版本。 |
| `--veroscript` | 选择用作验证器的 VeroScript 可执行文件或 SDK 根目录。 |
| `--timeout` | 设置生成源码验证超时时间（秒）；默认值为 120。 |
| `--no-validate` | 跳过编译器验证；仅在另有明确环节校验生成源码时使用。 |

当输入超过一个，或提供了 `--output-dir` 或 `--manifest` 时，会选择项目模式。
项目模式始终需要 `--output-dir`。

## 输出元数据

source map 记录源码路径、生成路径和映射数组。每个映射记录生成的行，以及对应
源码的路径、行和列：

```json
{
	"version": 1,
	"source": "Program.cs",
	"generated": "Program.vs",
	"mappings": [
		{
			"generatedLine": 1,
			"sourcePath": "Program.cs",
			"sourceLine": 1,
			"sourceColumn": 1
		}
	]
}
```

项目清单使用以下稳定字段：

```json
{
	"version": 1,
	"kind": "veroscript.csharp-transpiled-project",
	"projectWideAccess": true,
	"files": [
		{
			"input": "Models/User.cs",
			"output": "Models/User.vs",
			"sourceMap": "Models/User.vs.map.json"
		}
	]
}
```

清单中的路径是项目相对路径，并统一使用 `/` 作为分隔符。应将同一次转换生成的
清单、源码和 source map 一起保留。

## 支持的 C# 范围

当前子集包括：

- 命名空间、枚举、类、结构体，以及作为命名空间级声明使用的静态类；
- public、protected、private 字段，对称的自动属性，构造函数，静态成员、实例成员
  和方法；
- 无约束泛型类型和自由函数、基本类型以及 VeroScript 标准容器类型；
- 局部类型推断、调用、对象和集合创建、索引、转换、算术、赋值、条件、循环、
  包括 `do`/`while` 以及具有多个声明或迭代表达式的传统 `for`、`foreach` 和
  `switch`；
- 普通的 `throw`、`try`、`catch` 和 `finally`；
- 类型别名、常量局部变量、`params`、委托/函数类型，以及表达式或语句块 lambda；
- 返回引用的方法、引用局部变量、标签和 `goto`，并将它们降级为对应的
  VeroScript 别名与控制流形式。

`Console.Write` 与 `Console.WriteLine` 的换行行为差异会被保留。对于受支持的
lambda，C# 按引用捕获的行为也会保留。
字符串字面量会保留 NUL 和其他受支持的控制字符。未配对的 UTF-16 代理项没有无损的
VeroScript UTF-8 表示，因此会安全地被拒绝。

可表达的显式泛型成员调用会保留具体类型参数。例如
`Helpers.Pick<long>(1)` 会生成 `Helpers::Pick<int64>(1)`。嵌套类型声明会以
诊断拒绝，不会生成语义不明确或无效的 VeroScript；需要转换时请将声明移到
namespace 作用域。

## 明确的边界

无法忠实表示的功能会报告 `VSCT2001` 诊断并拒绝，而不会被静默近似。目前包括：

- `async`/`await`、任务、迭代器和 LINQ 查询语法；
- 委托声明和事件；
- 可空引用语义、unsafe/指针代码、反射和 `dynamic`；
- 泛型约束和接口；
- `catch` 过滤器；
- 非对称属性或计算属性；
- 构造函数链；
- 分布在多个文件中的 partial 类型。

`--reference` 可重复添加元数据程序集，`--language-version` 选择 C# 语法版本，两者都不会扩展受支持的 VeroScript 语义。`--timeout` 限制生成源码的校验时间。只有另一个明确环节负责编译器校验时，才使用 `--no-validate`；单纯生成成功不代表输出能够执行。

## 整数转译与数值支持边界

Transpiler 保留 C# 默认 unchecked 整数语义：加减乘、取负、递增递减、复合赋值和整数强制转换按解析后的位宽回绕；移位计数按 31 或 63 掩码。复合赋值在右操作数之前仅求值一次目标及其旧值；自动属性输出为字段。整数除法与取余在除数为零或有符号最小值/-1 溢出时抛出可捕获的脚本异常。整数值的 `double` 常量仍保留浮点类型。

<a id="numeric-and-console-boundaries"></a>

显式 `checked`/`unchecked` 上下文、非常量浮点除法与取余、非常量浮点转整数，以及非有限浮点常量均以 `VSCT2001` 拒绝，包括关闭验证时。有限的编译期数值常量由 Roslyn 折叠。解析使用默认 unchecked 编译设置，不导入消费项目的溢出检查编译选项。

枚举的整数强制转换按声明的底层位宽回绕。无符号 64 位枚举、枚举算术或位运算变更、非常量 `double` 转 `float` 窄化，以及非常量 64 位整数转 `float` 均以 `VSCT2001` 拒绝。Console 支持单个字符串、整数或布尔参数，以及无参数 `Console.WriteLine()`；`Console.Write` 使用无换行的 `std::console::write`，`Console.WriteLine` 使用带换行的 `std::print`。布尔文本使用 `True`/`False`，字符串拼接及 `+=` 同样如此。标准函数使用限定名称，防止源码中的同名函数截获输出或整数文本转换。格式化/多参数 Console 重载，以及浮点、枚举、对象或数组的格式化均明确拒绝，不会悄然改变 CLR 文本语义。

C# 转译器在整数与浮点数的混合比较前应用 Roslyn 的操作数提升，保证进入核心比较前已经完成 C# 的 float 舍入。

数值条件分支应用 Roslyn 选择的隐式转换，保留整数位宽与 float 舍入，运行时只求值选中的分支。条件分支中的非常量 64 位整数转 float 同样明确拒绝。

## 链接输出路径

Windows 的 `\\?\`、`\\.\` 和卷 GUID 路径别名会按最终文件系统位置进行比较。这些别名同样不能绕过校验；互不相同的输出路径仍然受支持。

BindGen 与 Transpiler CLI 在暂存输出前解析已有的目录和文件链接。通过符号链接或 Windows junction 指定的路径不能覆盖输入源码，也不能重复指向同一输出文件；链接目录中互不相同的输出路径仍然受支持。

`--timeout` 和 `ValidationTimeout` 对每次验证调用同时限制验证器退出及标准输出、标准错误的读取。即使启动器已退出、子进程仍持有重定向管道，到期也会以 `VSCT2004` 失败。验证失败不会替换 CLI 输出文件。

静态字段与自动属性只接受省略或编译期常量初始化器。非常量静态初始化器及静态构造函数会以 `VSCT2001` 拒绝，即使关闭验证也是如此，因为 VeroScript 全局初始化无法保持 CLR 类型初始化的时机、副作用及依赖顺序。请从 `Main` 或应用控制的方法中显式初始化动态静态状态。

字符串、数组和已映射集合的字段与自动属性必须具有显式的非 null 初始化器。已映射集合包括 `List<T>`、`Dictionary<TKey, TValue>`、`SortedDictionary<TKey, TValue>`、`HashSet<T>`、`Queue<T>` 和 `Stack<T>`。这些类型省略初始化或赋值为 null 会以 `VSCT2001` 拒绝，即使关闭验证也是如此，因为把 CLR null 引用变成空字符串或空容器会改变相等比较和异常行为。支持的非 null 数组、已映射集合与 null 进行相等或不等比较仍受支持；字符串与 null 的比较不属于支持子集。

## 输出文件名冲突

Transpiler 的项目输出文件名在所有宿主上都会忽略大小写，并按 Unicode NFC 规范化后比较。名称按输入路径顺序分配；冲突时添加确定性的路径哈希后缀，该名称仍被占用时再添加数字后缀。请使用返回记录或清单中的实际文件名，不要自行推算。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
