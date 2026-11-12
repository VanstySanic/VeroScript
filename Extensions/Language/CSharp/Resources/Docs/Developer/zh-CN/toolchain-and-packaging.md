# C# 工具链与包契约

## 严格的工具边界

存在入口点时，C# Transpiler 会降低由 C# 编译器选定的入口点，而不是按类名猜测。它仅接受声明在静态类型中的无参数 `static void Main()` 或 `static int Main()`；入口点含糊、带参数或位于实例容器时会以 `VSCT2001` 失败。由于 Binding Catalog v1 尚无宿主索引访问降低，BindGen 会拒绝 C# 索引器。两个工具都不会悄然省略不支持的入口或成员，只有验证成功后才会提交输出。

## 框架和 SDK

`VeroScriptCSharp` 目标为 `net8.0` 与 `net9.0`，使用 C# 语言版本 12.0，生成 XML 文档并启用包 API 验证；运行时包为 `VeroScript.CSharp`。CMake 在构建前检查已安装 .NET SDK，并为两个目标框架运行托管测试。

C# 前端、Transpiler 和 BindGen 项目同样目标为 `net8.0` 与 `net9.0`，但当前 CMake 工具链中其构建期工具需要 .NET 9.0 或更高版本的 SDK。这是工具构建前提，不能与运行时包支持的目标框架混淆。当请求的 .NET 8 或 9 运行时未安装时，打包的命令行工具采用 `Major` 运行时前滚策略，因此可以在更新且仍受支持的主版本运行时上执行。

## 包内容

运行时 `.nupkg` 包含 net8.0/net9.0 托管程序集、XML API 文档、buildTransitive 集成目标和许可证；调试符号通过独立 `.snupkg` 分发。源码 README.md、API.md 和 MOBILE.md 不随包附带，使用指南通过独立 Resources 文档获取。原生 Interop 运行库仍由宿主部署。CMake 将各框架的托管输出和生成的 NuGet 包安装到 SDK 包区域。

## C# AOT 源 Bundle

`VeroScriptCSharpAot` 同时公开单个生成 C# 编译单元和确定性的 `FCSharpAotSourceBundle`。Bundle 包含运行时支持、降级程序、入口点的相对源文件，以及 SDK 项目和机器可读 `ManifestJson`。`BuildCSharpSourceBundle` 打包已发射的 C#，不改写生成源。

源、文件和已检查程序入口共享加载器控制：导入路径、隐式导入路径、`Defines`、循环导入策略和项目范围访问策略。接受 options 的重载还携带宿主目录和代码生成设置。加载、语义检查、降级或 Bundle 构建失败不得返回部分可构建 Bundle。相对路径和清单字段是输出契约，绝不本地化。

## 生成注册检查

BindGen 打包包含目录生成、生成注册编译与运行时测试、C++ 目录导入测试和包验证。变更生成 C# 源或目录形状后，所有生成注册消费者仍必须能编译运行。

## 源转译

`VeroScript.CSharpTranspiler` 和 `VeroScript.CSharpTranspiler.Tool` 提供严格的 C# 到 VeroScript 转换。项目模式将所有源作为单个 Roslyn 编译处理，发射语义跨文件导入、源码映射和确定清单，并原子提交输出集。默认用真实 VeroScript 验证器编译生成源。

## 工具依赖声明

C# 前端、BindGen CLI 与 Transpiler CLI 的输出在 `licenses/` 下保留 Roslyn 原始声明；前端 NuGet 和两个 .NET 工具包也包含这些文件。它们补充项目的 MIT 声明。Roslyn 版本变化时同步更新[声明来源](../../../Licenses/README.md)，保留完整的 `Roslyn-LICENSE.txt` 和 `Roslyn-ThirdPartyNotices.rtf`，包括 RTF 编码。包校验会比较它们与源声明的一致性；不要本地化上游许可证正文。

## 标准容器的泛型参数数量

参阅[C# 前端 — 标准容器的泛型参数数量](../../User/zh-CN/frontend.md#container-mapping-arity)。

## 整数转译与数值支持边界

Transpiler 保留 C# 默认 unchecked 整数语义：加减乘、取负、递增递减、复合赋值和整数强制转换按解析后的位宽回绕；移位计数按 31 或 63 掩码。复合赋值在右操作数之前仅求值一次目标及其旧值；自动属性输出为字段。整数除法与取余在除数为零或有符号最小值/-1 溢出时抛出可捕获的脚本异常。整数值的 `double` 常量仍保留浮点类型。回归测试对同一输入执行 CLR/VeroScript 并比较实际结果。

参阅[C# 转换器 — 整数转译与数值支持边界](../../User/zh-CN/transpiler.md#numeric-and-console-boundaries)。

工具包发布检查会安装 Transpiler，用它生成源码、通过真实编译器验证并执行，核对无符号除法与取余、有符号整数回绕、布尔文本和换行、可捕获的零除数异常，以及关闭验证时仍拒绝不支持的 checked 字符串常量。

C# AOT 的浮点转整数转换向零截断，并在强制转换前按二进制值检查目标范围，保留可表示的 64 位整数值，不经过十进制舍入。无符号目标拒绝负数输入，包括负小数；NaN、无穷和超出范围的值均失败。回归检查在托管执行和 NativeAOT 执行中覆盖整数边界值。

参阅[C# AOT — 明确排除](../../User/zh-CN/aot.md#aot-floating-conversions)。

C# 转译器在整数与浮点数的混合比较前应用 Roslyn 的操作数提升，保证进入核心比较前已经完成 C# 的 float 舍入。

数值条件分支应用 Roslyn 选择的隐式转换，保留整数位宽与 float 舍入，运行时只求值选中的分支。条件分支中的非常量 64 位整数转 float 同样明确拒绝。

参阅[C# 运行时与 Interop 适配器 — 保留调用句柄的释放](runtime-and-interop.md#managed-marshalling-maintenance)。

[Android 与 iOS / MAUI](../../User/zh-CN/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

移动打包仍使用同一项目和包标识：`VeroScriptIncludeMobile=true` 将桌面 .NET 8/9 与 .NET 10 Android/iOS 目标及已验证的原生 SDK 一起打包。

裁剪 `core.vs` 导入的未使用实现时，已解析的字节码调用会保留执行依赖，泛型体会保留自由符号依赖，类型依赖通过 AST 声明和作用域引用收集，公开类型名通过 Core 别名表匹配。`std::sqrt`、`std::algorithm::sort` 等调用和 `std::delegate` 等类型会保留所需实现。局部名称、实例成员名称和普通字符串内容不会被当作全局函数引用。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
