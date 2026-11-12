# 诊断与错误

VeroScript 的问题通常分为编译诊断和运行时错误。编译诊断必须在脚本执行前修复；运行时错误只会在对应路径实际执行时出现。

## 编译诊断

编译阶段会检查词法、语法、名称解析、类型转换、重载、泛型约束、接口实现、模块可见性和声明规则。可以使用 `--check` 只检查脚本：

```powershell
veroscript --check script.vs
```

检查模式不会调用 `main`，适合编辑器保存检查、CI 验证和包构建前检查。诊断通常包含文件、行列位置和错误说明；同一根因可能产生多个关联诊断，应先修复最早的错误。

## 运行时错误

运行时错误可能来自：

- 数组、字符串或容器索引越界；
- 条件为 `false` 的 `assert`；
- 调用未绑定的委托或执行其他无效的运行时操作；
- 文件、目录或工作目录操作失败；
- 对已关闭通道执行不合法操作；
- 递归深度、类型操作或宿主绑定违反运行时限制。

运行时错误会终止当前执行或通过宿主的运行结果和错误流报告。宿主程序应检查 `FRunResult`，并根据需要保存错误文本和诊断位置。

VM、混合和 JIT 执行使用相同的语言诊断。JIT 内核报告算术或转换失败时，运行时会将其
映射到可获得的最佳源码位置，并保留更具体的辅助诊断。宿主应使用结构化诊断和位置，
不要匹配本地化后的显示文本。

## 脚本异常

`throw` 会抛出一个可以跨越函数调用传播、并由 `try`/`catch` 处理的脚本值。类型处理器或
`catch (...)` 可以处理脚本异常，但不能拦截引擎/运行时故障，包括失败的 `assert`。无论哪种
故障传播，`finally` 块都会执行。完整语法和匹配规则请参阅[控制流](../language-basics/control-flow.md#%E5%BC%82%E5%B8%B8)。

## 语言和输出

命令行工具可以选择诊断语言：

```powershell
veroscript --lang zh-CN --check script.vs
```

也可以通过 `VERO_LANG` 设置默认语言。脚本的标准输出和诊断错误流是不同的通道，宿主集成时应分别接收。

## 排查顺序

文档参考页应优先确认声明、作用域、类型和模块可见性；实际排查时建议先用 `--check` 排除编译问题，再用最小输入复现运行时问题，最后检查宿主权限、导入路径和标准库搜索路径。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-reference/diagnostics.md) · [fr-FR](../../fr-FR/language-reference/diagnostics.md) · [ja-JP](../../ja-JP/language-reference/diagnostics.md) · [ko-KR](../../ko-KR/language-reference/diagnostics.md) · [zh-CN](diagnostics.md) · [zh-TW](../../zh-TW/language-reference/diagnostics.md)
