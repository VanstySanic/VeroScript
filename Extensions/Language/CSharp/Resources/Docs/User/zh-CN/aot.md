# C# AOT

`veroscript aot --target=csharp` 会将通过编译检查的 VeroScript 程序转换为 C#
源码。生成的应用可以普通构建，也可以使用 .NET NativeAOT 发布；运行时不依赖
`VeroScriptInterop`。它与用于嵌入的 `VeroScriptCSharp` SDK 是不同的边界。

## 单文件输出

```powershell
veroscript aot --target=csharp app.vs -o Program.cs
```

生成结果是 C# 源文件而不是可执行文件。请将其加入目标为 `net8.0` 或 `net9.0`
的 SDK-style 项目，再使用 `dotnet` 构建。需要时使用 `-I`/`--import-path` 添加
应用模块搜索路径。

## 源码 bundle

```powershell
veroscript aot --target=csharp app.vs --output-dir generated --bundle-name App
dotnet publish generated/App.csproj -c Release -r win-x64 --self-contained true
```

bundle 包含 `App.runtime.g.cs`、`App.program.g.cs`、`App.entry.g.cs`、
`App.csproj` 和 `App.manifest.json`；manifest 记录生成文件及编译单元顺序。

<a id="aot-project-inputs"></a>

### 生成项目中的宿主输入

生成的 `.csproj` 使用 `EnableDefaultCompileItems=false`，只编译显式列出的 `Compile` 项。原始托管宿主声明及生成的 AOT bridge 必须作为额外项目输入加入；仅将文件放在项目旁边不会自动包含它们。bridge 的 partial class 实现须与生成程序在同一程序集编译。保留 manifest 中列出的全部生成单元；实时 VM 注册源码属于另一种 SDK 接入构件。

重新生成 bundle 会替换生成的项目文件。生成后重新应用额外的 `Compile` 项，或在应用自己维护的项目中引用生成单元与宿主输入。具体路径和步骤见[绑定教程](../../../../../../../Resources/Docs/Tutorials/zh-CN/csharp/01-csharp-source-tools.md)。

## 支持范围与明确限制

支持标量、字符串、`pair`、`auto`、容器、自由函数、无约束开放 generic、默认参数、
可变参数、重载、`ref`、namespace、`using namespace`、enum、global、import、控制
流、`switch`、跳转、`throw`、`try`/`catch`/`finally` 以及
`algorithm`/`numeric`/`random` 源码模块。还支持函数值和间接调用、表达式/语句块
lambda 与捕获、脚本类和结构体、构造函数、实例/静态/虚方法、单一类继承、引用返回
以及局部引用别名。

还支持有符号 8 位 `char` 字面量、同类型局部声明组、`do`/`while` 以及传统 `for` 的
初始化或步进表达式列表。普通字符串和原始字符串会保留准确的 VeroScript 字节序列，
包括由 `\xNN` 引入的孤立字节；Unicode 换行标量会发射为转义，以免使生成的 C# 字面量
或源位置注释失效。

泛型类型参数以及词法遮蔽的局部变量、字段、循环/catch 绑定和 lambda 参数都会获得卫生的
C# 名称。初始化器会在新绑定可见前针对先前的 VeroScript 环境发射；即使 C# 否则会将它
绑定到自身或拒绝嵌套，仍保持 VeroScript 语义。

在开放泛型类型的构造函数字段初始化列表中，C# 目标仅支持 `T` 到 `T` 的直接初始化。
用零个或多个参数构造无约束字段类型参数的形式会在 AOT 发射阶段被拒绝；请改用具体字段
类型，或先构造值再将其作为 `T` 传入。

当 lambda 使用实例成员时，捕获的类 `this` 会保留接收者并观察后续字段修改；结构体
`this` 则是值快照。嵌套 lambda 会继续保留已经捕获的接收者。无捕获 lambda 不能使用
`this`。

Binding Catalog v1 中兼容的类、结构体、枚举、成员和容器，可以通过
`--binding-catalog` 与生成的 AOT bridge 降级为直接托管调用，不依赖运行时反射。

## 明确排除

接口及接口实现、多个具体类基类、非公开或结构体继承、确定性析构、friend/using-member、
受约束或带默认值的泛型参数、仅 const 或引用方向不同的重载、预编译函数体、map 解构、
引用 `foreach`、固定长度原生数组、宿主运算符/索引器、泛型宿主成员、宿主属性自增减，
以及需要元素转换的宿主容器都会被拒绝。工具会报告诊断，而不会静默改变语义。
`concurrent.vs`、`channel<T>`、`task<T>`、`task_group<T>` 暂不支持 C# AOT；需要
运行时并发或宿主功能时，请使用 [C# SDK](index.md)。

支持的容器还包括 `multi_map`、`ordered_multi_map`、`multi_set`、
`ordered_multi_set` 和 `priority_queue`，以及普通的 map、ordered map、set、
queue、stack 与 deque。

C# AOT 的浮点转整数转换向零截断，并在强制转换前按二进制值检查目标范围，保留可表示的 64 位整数值，不经过十进制舍入。无符号目标拒绝负数输入，包括负小数；NaN、无穷和超出范围的值均失败。

<a id="aot-floating-conversions"></a>

C# AOT 的整数转 float 保持核心经 double 中间值转换的规则，并覆盖混合运算和复合赋值。含浮点操作数的比较按 double 进行；浮点取余与核心一致，报错提示取模仅对整数有定义。

## 运行时内存支持

独立 AOT 当前不支持 `std::memory` 的查询、缓冲区、视图及相关类型。实际调用或使用这些类型时，生成器会报告明确错误；仅导入未使用的 `memory.vs` 或 `core.vs` 仍可生成。

[Runtime SDK](index.md)

## 声明准备与捕获限制

C# 生成使用 Core 的声明准备流程，包括类外定义和生成方法。原生宿主通过 `FCSharpCodegenBackend` 生成时，可在 `FCodegenOptions.AnnotationProcessing` 中提供注解处理器；处理器代码须在生成期间保持有效。

按值捕获的绑定默认只读。`mutable` lambda 对自有捕获的修改会保留到后续调用；复制函数值共享同一环境，分别求值创建的闭包拥有独立环境。按值初始化捕获在外层作用域从左到右求值一次，按普通名称捕获 const 绑定仍保留只读性质。标量局部 `static` 首次使用时初始化，并按声明共享存储，包括同一 lambda 表达式创建的不同闭包；泛型实例与生成程序的每次入口调用分别隔离状态。初始化失败后可重试，递归初始化报错。引用初始化捕获、非标量局部 static，以及嵌套 lambda 访问外层函数的局部 static 需要 Core 运行时路径，独立 C# 生成会明确拒绝。

独立 C# AOT 支持控制台能力查询、尺寸、颜色、光标和清屏操作。输出重定向时不提供控制能力，也不发出转义序列。入口执行边界恢复 Windows 控制台原始属性与光标状态；其他终端在修改样式或可见性后重置样式并显示光标，脚本失败时同样执行清理。

C# AOT 对函数签名中等价的基本类型别名复用同一个生成委托类型，覆盖引用参数、引用返回及嵌套值容器。`long`/`int64` 与 `unsigned long`/`uint64` 保持等价；有符号性、位宽、对象类型身份以及 `ref`/`const ref` 限定仍须匹配。

## 用户 pair 模板

用户自定义的 `pair<T, U>` 使用普通泛型字段和方法。该双参数模板替代软内置类型时，独立 AOT 会明确拒绝映射的 `entries()` 和键值条目遍历；这些操作请使用 VM 后端。映射值本身是用户 pair 类型时，普通值遍历仍受支持。

接收选项的文件生成入口（`FCSharpCodegenBackend.EmitFromFile`、`EmitCSharpSourceBundleFromFile`）通过 Core 模块加载器统一加载入口和导入模块。不可变模块环境及 `SourceOverlays` 同样适用于虚拟入口路径。需要目标运行时适配器的原生包契约仍会被独立输出明确拒绝。

接收选项的 `EmitCSharpSourceBundleFromProgram` 重载同样使用模块环境发布的绑定表，并拒绝与之冲突的显式绑定表。

生成的入口在 `main` 之前、执行异常边界之内，按编译器记录的顺序初始化脚本静态数据成员和全局变量。初始化中的异常与 `exit` 按入口执行的相同规则处理。初始化之前读取或引用非 `int` 存储需要使用 Core 运行时：其原始零值无法由强类型 C# 字段忠实表示。启动检查跟进静态已知的调用和直接可调用绑定；此类存储尚未初始化时，无法解析目标的间接启动调用会被拒绝。仅创建延迟调用的可调用值仍受支持，直线执行中先写后读，以及普通 `int` 零值的前向读取也仍受支持。

Core 编译成功后，启动时函数调用与类型初始化图的嵌套过深时，会返回要求使用 Core 运行时的诊断并拒绝生成，
以限制生成器的原生检查栈。普通递归和顺序启动调用仍受支持。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](aot.md) · [zh-TW](../zh-TW/aot.md)
