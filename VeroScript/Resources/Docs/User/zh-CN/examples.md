# 示例索引

示例文件用于验证语言和运行时行为，也可以作为编写文档时的可执行交叉引用；示例不是 API 规范，具体签名、错误条件和兼容性要求仍以语言参考、标准库参考和工具链参考为准。示例文件通过 Resources 或源码仓库提供，使用时请将文件名替换为实际位置。

运行示例：

```powershell
veroscript hello.vs
veroscript --check reflection.vs
```

[属性与方法函数值](standard-library/attribute.md)：`attribute_demo.vs`、`attribute_functions.vs`、`attribute_methods.vs`、`attribute_weak.vs`。

## 执行后端、资源与权限

存在已安装模块环境时，省略 `--jit-backend` 会继承其 JIT 选择，未配置时默认为 `auto`。自动选择找不到可用后端时，默认的 `mixed` 策略使用 VM。以下示例手动固定兼容的后端文件或目录：

```powershell
veroscript --jit-backend="/absolute/path/to/backend" --backend=jit integer_aliases.vs
```

请把路径替换为实际后端安装位置。标准库不在 Runtime 资源布局中时，使用 `--stdlib-path` 指定目录。嵌入宿主显式提供模块或虚拟源码资源，并接入输出捕获。

嵌入及移动端可从 `portable_basics.vs` 开始：它导入 `numeric.vs`，验证按值捕获的 Lambda，并输出 `portable basics: 42`，脚本仅需输出权限。`runtime_utilities.vs` 的 `std::filesystem::cwd()` 需要文件读取权限，`script_tool.vs` 需要环境变量读取权限，`concurrent_demo.vs` 需要任务创建权限和隔离 VM 工作线程。零超时等待可能取得已完成的结果，应只消费一次。`path_helpers.vs`、`windows_path_text.vs` 仅处理文本，不访问文件。应用在工作线程中执行脚本，并配置适当的执行预算。

C# 移动指南提供 MAUI 示例，Python 移动指南提供嵌入式 CPython 验收脚本。移动编译、应用集成和设备验收需独立完成，桌面脚本测试不能代替这些验证。

## 阅读示例

选择所需主题，查看文件注释和对应参考，再修改输入并重新运行。`int main()` 返回进程退出码，`int main(string[] args)` 还会接收脚本参数。不需要返回值时，也可以使用 `void main()`。

## 入门和综合能力

| 示例 | 主要内容 |
| --- | --- |
| `hello.vs` | 最小脚本、`main` 入口和输出。 |
| `portable_basics.vs` | 显式数字模块导入、数组、按值捕获 Lambda 和结果校验，脚本仅需输出权限。 |
| `argv_echo.vs` | `int main(string[] args)` 和命令行参数。 |
| `object_basics.vs` | 结构体方法、类型声明时同时定义实例，以及可空类引用。 |
| `language_features.vs` | 约束、装箱、重载、默认参数和变长参数。 |
| `fib.vs` | 递归函数和条件控制流。 |
| `goto_demo.vs` | `goto` 与标签。 |
| `exceptions.vs` | 类型异常、兜底处理器、重新抛出和 `finally`。 |

## 值、数组、容器和引用

| 示例 | 主要内容 |
| --- | --- |
| `arrays.vs` | 原生数组和数组访问。 |
| `dynamic_array.vs` | 动态数组。 |
| `brace_initialization.vs` | 花括号初始化。 |
| `integer_aliases.vs` | 整数别名和显式类型。 |
| `numeric_primitives.vs` | 独立的 `float`/`double`、数字后缀、省略整数部分的浮点字面量，以及 8/16/32/64 位整数。 |
| `pair_map.vs` | `pair`、映射条目视图、解构，以及结构体和弱引用值的首次写入。 |
| `multimap.vs` | 多重映射。 |
| `multiset.vs` | 多重集合和重复元素。 |
| `priority_queue.vs` | 优先队列。 |
| `ref_foreach.vs` | 引用变量与基于范围的遍历。 |
| `reference_returns.vs` | 引用别名、引用返回值和类句柄槽重绑定。 |

## 类型、泛型和可调用对象

| 示例 | 主要内容 |
| --- | --- |
| `operators.vs` | 运算符引用参数、可写及 const 下标引用、赋值引用和后缀快照。 |
| `static_members.vs` | 静态成员和静态初始化。 |
| `default_delete.vs` | 默认和删除的特殊成员函数。 |
| `constructor_initializers.vs` | 构造初始化列表、`const` 字段和初始化顺序。 |
| `friend_access.vs` | `friend` 访问规则。 |
| `handle_struct.vs` | `shared<T>` / `handle<T>` 的结构体共享身份、复制构造与 weak 失效。 |
| `pure_virtual_out_of_class.vs` | 类外定义和纯虚函数语法。 |
| `template_prefix.vs` | 模板成员定义中的前缀语法。 |
| `method_templates.vs` | 方法自有参数及 callable 引用。 |
| `value_template_parameters.vs` | 受检值参数、枚举策略及固定数组长度。 |
| `constexpr.vs` | 编译期标量、纯函数及普通运行时调用。 |
| `template_methods_main.vs` | 跨文件模板方法定义。 |
| `generic_constraints.vs` | 泛型约束和 `where`。 |
| `lambda.vs` | Lambda、值／引用初始化捕获、共享 mutable 计数器及首次到达初始化的局部 static。 |
| `delegates.vs` | `std::delegate` / `std::multicast_delegate`、小写成员、捕获、重绑与广播快照。 |
| `delegate_functions.vs` | 命名函数绑定、零到三个参数；可用于 VM 与 C++/C#/Python AOT。 |
| `reflection.vs` | 反射类型、字段、方法、声明元数据以及 `mutable` 字段。 |
| `reflection_executable.vs` | 类型化字段读写、实例／静态／接口默认方法调用、引用参数及返回值、按值参数隔离。 |

## 模块、预处理器和包

| 示例 | 主要内容 |
| --- | --- |
| `include_alias.vs` | include/import 别名。 |
| `alias_exports.vs`, `alias_reexports.vs` | 跨文件 `export using`／`export typedef`、命名空间查找和门面转导出；配套模块与门面文件仅供导入。 |
| `macro_scope.vs` 与 `macro_scope_module.vs` | 运行入口脚本，验证普通 `#define` 的模块私有性；后者仅供导入。 |
| `macro_exports.vs`, `macro_exports_module.vs`, `macro_exports_facade.vs` | 公开对象、函数、可变参数宏、门面转导出和私有隔离；运行 `macro_exports.vs`。 |
| `macro_implicit.vs` | 无需在业务脚本中编写导入的公开宏；运行 `veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs`。 |
| `reexport_facade.vs` | 模块重新导出门面。 |
| `reexport_main.vs` | 从入口模块重新导出声明。 |
| `preprocessor.vs` | 预处理器条件和宏。 |
| `package/package_library.vs` | 包库的源文件。 |
| `package/package_app.vs` | 包应用入口。 |
| 包示例说明 | 包示例的目录和运行说明。 |

## 标准库、运行时和工具

| 示例 | 主要内容 |
| --- | --- |
| `memory_operations.vs` | 引用身份、二进制缓冲区、字节序、共享视图和独立副本。 |
| `formatting.vs` | `print`、`printf`、`format` 和 `formatf`。 |
| `path_helpers.vs` | 路径标准库。 |
| `standard_library_algorithms.vs` | 泛型算法、确定性的随机范围和可复现的随机排列。 |
| `standard_namespace.vs` | 推荐的 `std::` 写法、`readonly` 值和显式转换。 |
| `windows_path_text.vs` | 任意平台上的 Windows 风格路径文本；不访问文件系统。 |
| `runtime_utilities.vs` | `range`、当前目录和时间工具。 |
| `concurrent_demo.vs` | 通道、函数、静态方法与 Lambda 的 callable 任务，以及协作式取消。 |
| `script_tool.vs` | 脚本工具参数、环境变量、字符串、断言和退出状态。 |

## 宿主和绑定配置样例

| 文件 | 主要内容 |
| --- | --- |
| `workspace.analysis.sample.json` | 语言服务分析配置。 |
| `bindings.sample.json` | 宿主绑定目录的最小示例。 |

将 `workspace.analysis.sample.json` 的内容作为 `initializationOptions.veroscript` 或 `workspace/didChangeConfiguration.settings.veroscript` 传入，或应用到对应 IDE 设置。语言服务器不会自动读取此文件。`bindingCatalogPaths` 应指向实际的绑定目录文件；直接向服务器传入设置时使用绝对路径。

完整运行约定见[样例目录说明](../../../Scripts/examples/README.md)。语言规则见[语言参考](language-reference/index.md)，标准库签名见[标准库参考](standard-library/index.md)，CLI 选项见[命令行工具](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)。

- `console_demo.vs`: `std::console` (终端控制).

[应用国际化 — std::i18n](standard-library/i18n.md): `i18n_demo.vs` · `VeroScript/Resources/Examples/I18n/`.

- [标准库基础](standard-library/foundation.md)：`standard_library_foundation.vs`

## variant

[variant — 动态值盒](standard-library/variant.md): `variant.vs`.

```text
veroscript variant.vs
```
<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/examples.md) · [fr-FR](../fr-FR/examples.md) · [ja-JP](../ja-JP/examples.md) · [ko-KR](../ko-KR/examples.md) · [zh-CN](examples.md) · [zh-TW](../zh-TW/examples.md)
