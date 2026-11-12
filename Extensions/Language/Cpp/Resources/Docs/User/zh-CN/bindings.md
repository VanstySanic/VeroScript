# 宿主绑定与绑定目录

宿主绑定把 C++ 宿主中的函数、类型、成员和元数据暴露给 VeroScript。绑定目录（binding catalog）是描述这层公开接口的 JSON 文件，可由 CLI 校验和格式化，也可以在检查或运行脚本时加载。

绑定目录是接口契约，不会替代实际的 C++ 注册代码，也不会单独提供运行时实现。目录中有某个函数或类型，只表示编译器可以看到它；运行脚本时，宿主仍必须注册与目录一致的真实实现。

## 目录文件与版本

绑定目录的顶层对象由以下字段组成。导入时各数组字段可以省略；宿主导出目录时会输出这些数组。`version` 如果出现，必须是整数 `1`。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `version` | 整数 | 绑定目录格式版本；当前为 `1`。 |
| `functions` | 数组 | 可供脚本调用的宿主函数描述。 |
| `genericTypeTemplates` | 数组 | 泛型宿主类型模板描述。 |
| `types` | 数组 | 宿主类、结构体、枚举及其成员描述。 |
| `typeAliases` | 数组 | 宿主类型别名描述。 |
| `annotations` | 数组 | 注解词汇表，供反射、工具和文档使用。 |

一个完整的空目录可以写成：

```json
{
	"version": 1,
	"functions": [],
	"genericTypeTemplates": [],
	"types": [],
	"typeAliases": [],
	"annotations": []
}
```

文件应保存为 `.json`。代码块中的 `json` 只是文档中的代码高亮标签，并不表示 JSON 文件可以改写成 `.vs` 脚本。

## 函数描述

`functions` 中的每个对象至少需要 `name`、`returnType` 和 `paramTypes`：

| 字段 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `name` | 字符串 | 是 | 脚本可见的函数名。 |
| `returnType` | 字符串 | 是 | 脚本类型名，例如 `int`、`string`、`object`。 |
| `paramTypes` | 字符串数组 | 是 | 参数类型，顺序必须与 C++ 注册签名一致。 |
| `documentation` | 字符串 | 否 | 函数文档。 |
| `paramNames` | 字符串数组 | 否 | 参数名称，按参数顺序排列。 |
| `paramDocumentation` | 字符串数组 | 否 | 参数文档，按参数顺序排列。 |
| `returnDocumentation` | 字符串 | 否 | 返回值文档。 |
| `annotations` | 对象数组 | 否 | 函数上的注解实例。 |
| `paramAnnotations` | 对象数组的数组 | 否 | 参数注解；外层和 `paramTypes` 使用相同的参数位置。 |
| `returnHostTypeName` | 字符串 | 否 | 返回值为 `object`、`enum` 或 `function` 时对应的宿主类型名或签名。 |
| `paramHostTypeNames` | 字符串数组 | 否 | 与 `paramTypes` 平行的宿主类型名。 |
| `returnElementTypeName` | 字符串 | 否 | 一元容器返回值的元素类型。 |
| `paramElementTypeNames` | 字符串数组 | 否 | 一元容器参数的元素类型。 |
| `returnKeyTypeName` | 字符串 | 否 | 二元映射返回值的键类型。 |
| `returnValueTypeName` | 字符串 | 否 | 二元映射返回值的值类型。 |
| `paramKeyTypeNames` | 字符串数组 | 否 | 二元映射参数的键类型。 |
| `paramValueTypeNames` | 字符串数组 | 否 | 二元映射参数的值类型。 |
| `paramFlags` | 整数数组 | 否 | 参数传递方式；省略时等同于所有参数为值传递。 |
| `typeParams` | 字符串数组 | 否 | 泛型宿主函数的类型参数。 |
| `scriptReturnTypeName` | 字符串 | 否 | 泛型调用在脚本侧使用的返回类型名。 |
| `genericHostObjectCast` | 布尔值 | 否 | 是否启用泛型宿主对象转换。仅在对应的泛型注册场景使用。 |

所有“平行数组”都必须使用相同的参数位置：例如 `paramHostTypeNames[1]` 描述 `paramTypes[1]`，`paramFlags[1]` 也描述同一个参数。数组长度或位置不一致时，应以 CLI 校验器报告为准并修正目录。

### 类型名与容器元数据

`returnType` 和 `paramTypes` 使用宿主绑定支持的类型名。常用名称及别名如下：

| 类型类别 | 可用名称 |
| --- | --- |
| 基本类型 | `void`、`bool`、`char`、`short`、`int`、`long`、`float`、`double`、`string`、`wstring`、`unsigned`、`unsigned int`、`unsigned char`、`unsigned short`、`unsigned long`、`int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32`、`int64`、`uint64`、`enum`、`function`、`fn` |
| 宿主对象 | `object`、`HostObject` |
| 一元容器 | `array`、`DynamicArray`、`set`、`DynamicSet`、`multiset`、`MultiSet`、`ordered_multiset`、`OrderedMultiSet`、`queue`、`stack`、`deque`、`channel`、`priority_queue`、`PriorityQueue` |
| 二元映射 | `map`、`DynamicMap`、`ordered_map`、`OrderedMap`、`multimap`、`MultiMap`、`ordered_multimap`、`OrderedMultiMap` |

容器的元素、键和值不直接写进 `returnType` 或 `paramTypes`，而是写入对应元数据：

| 类型 | 返回值字段 | 参数字段 |
| --- | --- | --- |
| `array`、集合、队列、栈、双端队列、`channel`、优先队列 | `returnElementTypeName` | `paramElementTypeNames` |
| `map`、`ordered_map`、`multimap`、`ordered_multimap` | `returnKeyTypeName`、`returnValueTypeName` | `paramKeyTypeNames`、`paramValueTypeNames` |
| `object`、`enum`、`function` | `returnHostTypeName` | `paramHostTypeNames` |

例如，一个返回 `Widget` 宿主对象、接受 `array<int>` 和 `map<string,double>` 的函数，可以这样描述相关类型：

```json
{
	"name": "inspect_widget",
	"returnType": "object",
	"paramTypes": ["array", "map"],
	"returnHostTypeName": "Widget",
	"paramElementTypeNames": ["int", ""],
	"paramKeyTypeNames": ["", "string"],
	"paramValueTypeNames": ["", "double"]
}
```

### 参数传递标志

`paramFlags` 的每个元素是位标志：

| 值 | 含义 |
| --- | --- |
| `0` | 按值传递。 |
| `1`（`0x01`） | 非 `const` 引用。 |
| `3`（`0x01 | 0x02`） | `const` 引用。 |

其他位组合无效；`const` 位必须与引用位同时出现。引用标志仍必须与实际注册函数的签名一致，目录不会把一个按值的 C++ 函数变成引用函数。

## 宿主类型与成员

`types` 中的对象至少需要 `name` 和 `kind`。`kind` 只能是 `enum`、`struct` 或 `class`：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | 字符串 | 脚本可见的类型名。 |
| `kind` | 字符串 | `enum`、`struct` 或 `class`。 |
| `documentation` | 字符串 | 类型文档。 |
| `annotations` | 对象数组 | 类型上的注解实例。 |
| `inheritable` | 布尔值 | 类型是否允许派生。 |
| `scopedEnum` | 布尔值 | 枚举是否使用限定访问语义。 |
| `hasClone` | 布尔值 | 是否提供复制行为。 |
| `hasEquals` | 布尔值 | 是否提供相等比较。 |
| `hasHash` | 布尔值 | 是否提供哈希行为。 |
| `hasSubclass` | 布尔值 | 是否提供宿主子类创建支持。 |
| `genericTemplateName` | 字符串 | 实例化自哪个泛型宿主模板。 |
| `genericTypeArguments` | 字符串数组 | 泛型模板的实际类型参数。 |
| `members` | 对象数组 | 构造函数、方法、属性、字段或枚举成员。 |

成员对象至少需要 `kind`；普通成员通常还需要 `name`。支持的成员种类为：

| `kind` | 说明 |
| --- | --- |
| `constructor` | 构造函数；名称可以为空字符串。 |
| `method` | 实例方法。 |
| `staticMethod` | 静态方法。 |
| `propertyGetter` | 属性读取器。 |
| `propertySetter` | 属性写入器。 |
| `enumMember` | 枚举成员。 |
| `field` | 实例字段。 |
| `staticConstant` | 静态常量。 |

成员可以使用函数描述中的文档、注解、参数、返回值和类型元数据字段，并额外支持：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `enumValue` | 整数 | 枚举值；必须是精确的 JSON 整数，且在安全的 JSON 数值范围内。 |
| `enumValueInt64` | 字符串 | 完整的有符号 64 位枚举值；需要表示大于 JSON 安全整数范围的值时使用。 |
| `operator` | 布尔值 | 是否为运算符成员。 |
| `constMethod` | 布尔值 | 是否为 `const` 方法。 |
| `mutableThis` | 布尔值 | 方法是否允许修改当前对象。 |
| `nativeName` | 字符串 | 宿主侧的原生名称；脚本可见名称由 `name` 决定。 |

成员的 `returnType`、`paramTypes`、`paramFlags` 与函数描述具有相同含义。枚举成员应使用 `enumMember`；对于超出 JSON 精确整数范围的值，不要把近似的 JSON number 写入 `enumValue`，应写入十进制字符串 `enumValueInt64`。

## 泛型模板与类型别名

### `genericTypeTemplates`

每个泛型模板对象包含：

| 字段 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `name` | 字符串 | 是 | 模板名称。 |
| `arity` | 整数 `1`–`255` | 是 | 类型参数个数。 |
| `documentation` | 字符串 | 否 | 模板文档。 |

### `typeAliases`

每个别名对象包含：

| 字段 | 类型 | 必需 | 说明 |
| --- | --- | --- | --- |
| `name` | 字符串 | 是 | 别名名称。 |
| `typeParams` | 字符串数组 | 是 | 别名的类型参数，可以为空数组。 |
| `targetTypeName` | 字符串 | 是 | 别名指向的目标类型名。 |
| `documentation` | 字符串 | 否 | 别名文档。 |

例如：

```json
{
	"genericTypeTemplates": [
		{ "name": "Box", "arity": 1, "documentation": "A generic host box." }
	],
	"typeAliases": [
		{
			"name": "WidgetId",
			"typeParams": [],
			"targetTypeName": "int",
			"documentation": "The identifier used by the host."
		}
	]
}
```

## 注解词汇表与注解实例

顶层 `annotations` 是注解词汇表。每个条目支持：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | 字符串 | 注解名称。 |
| `detail` | 字符串 | 简短类别或签名说明。 |
| `documentation` | 字符串 | 注解文档。 |
| `arguments` | 对象数组 | 注解参数说明。 |
| `caseSensitive` | 布尔值 | 默认为 `true`；`false` 表示此定义的名称忽略 ASCII 字母大小写。 |
| `targets` | 整数 | 目标位掩码，默认 `131071`（所有支持的目标），有效值为 `1` 到 `131071`。 |

名称保留源码中的拼写；接受的拼写范围相交时，注册会被拒绝。参数名和元数据键仍区分大小写。每个参数描述还可包含同形的递归 `children` 数组，用于描述嵌套参数，不是可执行的值。原生注册使用 `FHostBindingRegistry::RegisterAnnotation(..., Targets, CaseSensitive)`。目录元数据不会安装处理器回调。

词汇表中的 `arguments` 对象支持 `name`、`detail` 和 `documentation`。词汇表用于描述可用的注解名称，不会自动把注解应用到函数或类型。

函数、类型和成员上的 `annotations` 是注解实例，格式为：

```json
{
	"name": "HOST_FUNCTION",
	"arguments": [
		{ "name": "Category", "value": "Examples|Math" },
		{ "name": "ReadOnly" }
	]
}
```

注解参数对象中的 `value` 可以省略。省略表示无值的标志参数；写成 `"value": ""` 则表示参数明确带有空字符串值。这两个含义不同。`paramAnnotations` 是参数注解实例的二维数组，外层位置必须与 `paramTypes` 一致。

从 Resources 集成文档取得[绑定目录示例](../../../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json)，将副本保存到可写工作目录，命名为 `bindings.sample.json`。它演示自定义注解元数据和宿主类型，不应假定功能 SDK 归档包含该示例。下列命令从该工作目录执行，并将 `app.vs` 替换为自己的脚本。

## 校验、格式化与加载

CLI 提供目录维护命令：

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

省略 `-o` 或 `--output` 时，`bindings format` 会重写输入目录文件；指定输出路径时，改为写入该文件。标准输出只包含写入确认，不是格式化后的 JSON；请从输出文件读取结果。

`validate` 检查 JSON 类型、必需字段、可用枚举值、并行数组和数值范围；`format` 以工具链的稳定格式输出 JSON。格式化到新文件时，建议先使用临时路径并检查结果，再替换原文件。

检查或运行脚本时，可以重复使用 `--binding-catalog`：

```powershell
veroscript --binding-catalog bindings.sample.json app.vs
```

加载的目录提供类型可见性和元数据。通过 CLI 导入的函数和成员描述用于编译期目录信息，不能替代运行时回调；实际宿主未注册对应实现时，脚本仍可能在运行阶段失败。

导入器还会拒绝超出安全边界的 JSON：文件大小上限为 `128 MiB`，JSON 值数量上限为 `4,194,304`，嵌套深度上限为 `256`。这些限制用于防止目录文件在校验阶段消耗过多资源。

## 在 C++ 宿主中注册

嵌入式宿主通常通过 `VeroScriptSession` 完成绑定。公开注册边界包括：

- `RegisterNative`：注册简单原生函数，或使用参数描述注册结构化函数。
- `RegisterHostType`：注册宿主类、结构体或枚举。
- `RegisterHostMember`：注册构造函数、方法、静态成员、属性或字段。
- `RegisterHostOperator`：注册宿主类型的运算符。
- `RegisterHostTypeClone`：提供宿主类型的复制行为。
- `SetHostTypeInheritable` 与 `SetHostEnumScoped`：设置类型和枚举语义。
- `RegisterHostTypeSubclassFactory`：为可派生宿主类型注册实例创建方式。
- `RegisterGenericHostTypeTemplate` 与 `InstantiateGenericHostType`：注册并实例化泛型宿主类型模板。
- `GetBindingCatalog`：读取当前会话可导出的绑定目录。

宿主注册完成后，应导出目录并运行 CLI 校验，使文档、静态检查和运行时注册保持一致。静态宿主成员通常通过 `Type.staticMethod(...)` 访问；宿主常量使用 `Type::Constant` 形式。更完整的引擎生命周期、`CheckSource`/`RunSource` 以及 `CheckFile`/`RunFile` 用法，参见[宿主集成](embedding.md)。

## 可空宿主对象参数

显式参数为 `HostObject` 的静态宿主成员可以接收脚本的 `nullptr`。回调会收到空的
宿主对象句柄，解引用载荷前必须先检查。空的实例接收者仍然无效；此规则只适用于
静态调用的显式参数。

## 兼容性原则

绑定目录属于公开接口的一部分。新增函数或成员通常是兼容扩展；删除条目、改变参数类型、改变引用标志、改变枚举值、改变宿主类型名或改变成员可见性，都可能破坏脚本。升级目录或宿主绑定时，应同时运行目录校验、脚本检查和目标项目的集成测试。

## 宿主重载

<a id="host-overload-resolution"></a>

普通宿主实例方法、静态方法和原生自由函数支持按规范化参数类型及引用限定重载。注册时拒绝重复参数签名及仅返回类型不同的声明。调用根据实参类型、数值转换和 variant 后备规则选择；同等候选报告歧义。具名宿主成员使用由签名派生的稳定原生标识。构造函数和运算符保留既有协议。仅按名称的 `FindFunction` 对重载集合返回空；通过 `GetFunctions()` 枚举各声明及其独立文档。绑定目录导入导出保留所有重载。现有单一签名注册继续使用相同公开接口。

省略 `ref` 时保留原生接口既有的值输入／句柄行为；变量槽回写仍需显式 `ref`。普通实参优先选择可用的值签名。显式引用检查存储类型和可写性，`const ref` 可接收 const 变量。编译扩展保留全部重载。VM、Mixed、JIT 和 Core AOT 使用核心选择结果；独立 C++ 源码导出保留其单独的 C 链接桥接契约，遇到不支持的具名宿主重载时明确提示使用 Core AOT。

## 原生回调元数据

IScriptCallInvoker::GetActiveModule() 仅在当前原生回调期间提供借用的只读语义元数据；执行宿主不提供时可返回 nullptr。回调返回后不得保留该指针、借用的调用服务或元数据内部引用；保留值或程序所有者也不会延长此回调借用契约。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/bindings.md) · [fr-FR](../fr-FR/bindings.md) · [ja-JP](../ja-JP/bindings.md) · [ko-KR](../ko-KR/bindings.md) · [zh-CN](bindings.md) · [zh-TW](../zh-TW/bindings.md)
