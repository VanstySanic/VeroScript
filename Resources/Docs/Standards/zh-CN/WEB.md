# TypeScript 与 JavaScript 代码规范（暂定）

适用范围：[VeroScript 本项目开发与贡献](README.md)。

TypeScript/JavaScript 遵循 [GENERAL.md](GENERAL.md) 的分块、注释和最小必要说明原则。下列子句换行规则为强制要求；其他花括号布局暂时遵循所在模块的既有风格。

## 当前暂定规则

- `else`、`else if`、`catch`、`finally` 和 `do…while` 的尾部 `while` 按 [GENERAL.md](GENERAL.md)
  的换行规则另起一行；块体左花括号也另起一行，与子句对齐。
- 每个逻辑层级使用一个真实 Tab，默认显示宽度为四列；遵循 [GENERAL.md](GENERAL.md) 的缩进规则。
- 类型、函数、条件、循环和 `switch` 按语义分块，避免无意义的单行压缩。
- 每个 `switch` 必须有 `default`。TypeScript 的穷尽性检查可以放在 `default` 中实现，但不能
  因为静态检查而省略 `default`；该分支必须明确处理未知值。
- 每个 `case` 和 `default` 标签的执行内容使用独立花括号作用域；不依赖隐式贯穿。确有意共享
  处理逻辑时，使用紧邻的说明注释表达该意图。唯一例外是该标签唯一执行路径为直接结束当前函数的
  `return`；其中不得混入其他语句。
- 所有控制分支必须使用花括号。唯一例外是分支体仅有一条直接 `return` 或 `return <表达式>` 的
  单行卫语句；它不得与 `else` 链、日志、清理或其他语句组合。
- TypeScript 在模块范围内的类型按 `enum` → `interface` → `type`/其他类型别名 → `class` 排列；
  泛型接口和泛型类分别归入其自身类别。JavaScript 没有对应类型声明时，按导出关系和依赖保持
  同等清晰的顺序。
- 类内容参照 C++ 的整体顺序：关联别名/常量、继承或接口实现、内部类型、方法、字段；字段放在
  对应访问语义区域的最后。
- 多接口实现或适配器分块需要时使用配对 `//~ Begin <Name> Interface` / `//~ End <Name> Interface`
  注释。
- 导出类型、接口和类使用 PascalCase；函数、字段、参数和局部变量使用 camelCase；外部协议字段
  保持其契约名称。
- 对外公开的数据、错误和资源释放语义必须通过类型或必要注释表达。

## 待补充

后续可决定花括号风格、引号/分号/尾逗号、`readonly`、异步取消、Wasm 资源生命周期和类型检查
要求。
