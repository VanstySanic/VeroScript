# Python 转换器

`VeroScriptPythonTranspiler` 将严格的、静态类型化 Python 3.11 子集转换为
VeroScript 源码。它共用 Python 前端的声明分析，与 Python 运行时绑定以及
VeroScript 到 Python 的 AOT 后端相互独立。

支持类型化函数、普通类、整数枚举、带注解的字段和局部变量、构造
函数、方法、字面量、调用、赋值、条件、`while` 和普通表达式。Python 任意
精度整数、动态导入、async、推导式、生成器、描述器、property、多重继承、
反射和未注解的局部变量不会被静默近似，而是报告诊断。

`@dataclass` 会被明确拒绝，因为尚未实现其自动生成的构造函数、相等比较等
运行时行为。包含 `metaclass` 的类关键字参数也因运行时行为尚未实现而明确拒绝。
请在支持的子集内使用显式编写构造函数和方法的普通类。
内建 `str(...)` 转换会被明确拒绝，因为尚未实现 Python 的格式化语义；
在当前模块声明或显式导入的同名用户 `str` 类或函数仍可正常调用。

类体字段默认值接受不可变标量字面量、受支持的纯常量表达式及已知整数枚举成员；共享
容器、对象、任意名称引用和函数调用会被明确拒绝。请在显式 `__init__` 中初始化每实例值；容器先创建为
带注解的局部变量，再赋给实例字段。

```powershell
python -m veroscript_python_transpiler input.py `
  -o input.vs --source-map input.vs.map.json `
  --veroscript veroscript
```

`--timeout <seconds>` 用于限制外部编译器验证时间，必须是有限的正数。库 API
在两种选项类型中都使用 `timeout_seconds` 表示同一设置。

默认使用真实的 VeroScript 编译器验证生成源码。项目模式解析提供的相对
导入，保留输出模块结构，生成 source map 与 `transpile.manifest.json`，并
验证完整模块图。API 提供 `transpile`、`transpile_source`、`transpile_file`、
`transpile_files`。

使用 `as` 对项目模块或声明改名的导入会被明确拒绝；请使用原名。
`import typing as t` 等元数据导入仍受支持。

## 校验与项目模式

默认启用外部编译器校验；只有另一个环节明确负责校验生成源码时，才使用 `--no-validate`。`--timeout` 默认为 `120.0` 秒，限制每次外部调用，而非整个项目的总耗时。源码、source map 和清单在替换前统一暂存，写入失败时尝试恢复；无法解析或存在歧义的导入会被拒绝。

```powershell
python -m veroscript_python_transpiler package/models.py package/service.py `
  --project-root . `
  --output-dir generated/veroscript `
  --veroscript veroscript
```

`len(str)` 按 Unicode 码点计数（补充平面字符和组合标记各按码点计入），不返回 UTF-8 字节数。字符串字面量长度不增加运行时依赖；其他已声明的字符串值使用线性的 Core UTF-8 缓冲区辅助逻辑，并导入标准库 `string.vs`。该字符串路径耗费 O(n) 时间及 O(n) 临时存储，不等同于 Python 的 O(1) 长度查询；它要求 Core 运行时，当前独立 Python AOT 会明确拒绝；生成文件或既有 `string.vs` 不得遮蔽该标准库依赖。容器长度保持直接 Core 操作。无法确定表达式的字符串／容器类型时，先将值放入有类型注解的局部变量。

当 Python 的全局、导入或局部绑定名为 `print`、`len`、`abs`、`min` 或 `max`，而 Core 会将调用解释为内建函数时，转译会明确拒绝。普通方法和已声明的 `str` 仍可调用。只有注解、没有默认值的字段必须在读取前显式初始化；本子集不模拟 Python 缺失属性或完整的确定赋值语义。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
