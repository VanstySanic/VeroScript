# 脚本对象映射

`json::converter<T>` 在活动 VM 中转换脚本值，生成输入或输出 JSON Schema。
实现使用模块自己的编译处理器和普通脚本字段、构造器与函数调用。

## 宿主接入

完成原生扩展注册后，将绑定、处理器和所有权一起传入模块环境，再编译脚本：

```cpp
Description.Bindings = Registry.GetBindings();
Description.CompileProcessing.Registry = Registry.GetProcessors();
Description.AvailableDependencies = Registry.GetDependencies();
Description.Owners = Registry.GetOwners();
```

项目 owner 需要声明对 `json` provider 的依赖。转换器声明由处理器生成，单独导入绑定目录不能代替编译处理器。
数据 SDK 的 C++ 映射仍使用 [原生映射接口](native-mapping.md)，不依赖脚本编译器。

## 基础用法

```cpp
struct Record
{
public:
	@JsonName(value="record_id") @JsonRequired int64 id = 7;
	string name = "default";
	@JsonIgnore int transient = 0;
};

int main()
{
	Record source = Record();
	json::value document = json::converter<Record>::write(ref source);
	Record result = json::converter<Record>::read(document);
	json::schema shape = json::compile_schema(json::converter<Record>::schema());
	assert(result.id == 7 && shape.is_valid(document));
	return 0;
}
```

`write` 的输入是 const ref，需要显式 `ref`。还提供 `write_with_options`、
`read_with_options`、`try_write`、`try_read` 及其 `*_with_options` 形式。
try 入口以 bool 报告普通转换失败，提交 `json::error`，保留调用方原有输出；成功时清空 error。
VM 已取消、超时或出现不可恢复执行错误时仍结束执行。

## 类型与字段

| 类型 | 默认表示 |
| --- | --- |
| bool、整数、有限浮点、string | 对应 JSON 标量；数字转换检查范围 |
| enum | 已定义成员的名字；`JsonName` 可修改名字 |
| array、deque、queue、stack、priority_queue | JSON array；使用各容器的 `values()` 顺序，栈从底到顶 |
| set、multiset、ordered_multiset | JSON array；set 拒绝重复，multiset 保留数量 |
| map/ordered_map，键为 string | JSON object |
| 非字符串键 map、所有 multimap | 开启 `key_value_pairs` 后为 `[{"key":...,"value":...}]` |
| struct/class | 公开实例字段；class 可以是 null |
| json::value | 保留原有 JSON 值 |

整数不经过 double。窄整数溢出、负数转无符号、非有限浮点均失败。
哈希容器的输出顺序遵循容器自身合同；需要明确插入顺序时使用 ordered 容器。
特殊宿主类型、函数及未提供默认适配的类型需要显式转换器。

| 注解 | 作用 |
| --- | --- |
| `@JsonName(value="name")` | 字段或枚举成员的 JSON 名字 |
| `@JsonIgnore` | 不输出；输入中的这个已声明名字被跳过 |
| `@JsonRequired` | 输入必须包含字段；是否允许 null 由字段类型决定 |
| `@JsonSerializeOnly` | 只输出，输入出现该字段时失败 |
| `@JsonDeserializeOnly` | 只读取，不输出 |
| `@JsonProperty(name="count",setter="set")` | 显式加入无参数 const getter；setter 可省略，省略时只输出 |
| `@JsonFactory(type="Factory")` | 使用 `Factory::create(value, context)` 正常构造目标 |
| `@JsonConverter(type="Adapter")` | 类型或字段使用显式转换器 |

默认包含公开继承字段；排除静态和不可访问字段。const 数据字段只输出。
缺失且未标必填的字段保留正常构造默认值。名字重复、方向与必填规则冲突会报告错误。
未知输入字段默认失败，可通过 `mapping_options.set_ignore_unknown(true)` 忽略。

## 选项、路径和构造

`json::mapping_options()` 提供 `ignore_unknown`、`numeric_enums`、`key_value_pairs`、
`exact_floating` 和相应 `set_*`，以及 `set_limits(depth, nodes)`。
深度范围为 1 至 256，节点数必须为正。配置按值复制，单次转换拥有独立上下文。

错误包含 `instance_pointer()`，字段与元素路径使用 JSON Pointer 转义。
沿当前递归路径检测 class 对象环；不同分支重复引用同一对象可以重复输出。
JSON 转换不恢复共享引用身份。

反序列化创建新值后才提交输出，无默认构造器的类型使用工厂或显式转换器。
工厂、属性和转换器可能修改外部状态；输出保持原值不表示外部副作用也会回滚。

## 自定义转换器

脚本类型可用 `JsonConverter` 指定具有下列静态方法的适配器：

```cpp
static bool encode(const Target& input, json::value& output, json::mapping_context context);
static bool decode(json::value input, Target& output, json::mapping_context context);
static json::value schema(json::mapping_context context);
```

对特殊宿主类型也可显式使用 `json::custom_converter<Target, Adapter>`。
这个入口还要求 `Adapter::create(value, context) -> Target` 返回合法初始值。
非默认构造的 struct 类型转换器同样需要工厂或 create；class 转换器可以从空句柄开始构造。

适配器通过 `context.fail`、`fail_range` 或 `adopt(error)` 提交诊断；返回 false 而未提供诊断时生成 Callback 错误。
可调用 `json::converter<Child>::encode/decode/schema_nested` 复用同一上下文，保持路径和预算。
自定义 Schema 必须描述实际转换，不能以空约束代替未知格式。

## Schema 和资格

封闭类型表、继承及泛型的多态转换见[多态映射](polymorphic-mapping.md)。

`schema()` 生成输入 Schema；`schema_with_options(options, false)` 生成输出 Schema。
输入规则包含必填、忽略、未知字段和只输出字段禁入；输出规则只包含实际输出的字段。
整数范围、容器元素、枚举和可空性与转换保持一致。递归类型使用 `$defs`，生成 Schema 不执行对象构造器。
同一 `mapping_context` 的 `schema_nested` 缓存按类型及输入/输出方向区分；
切换 `set_schema_input` 后不会复用另一方向的定义，切回原方向仍可复用已生成的引用。

## 声明作用域

自动映射面向模块或命名空间直接声明的类型。函数内的局部类型及类型内的嵌套类型不生成模块级辅助函数，
避免泄露私有作用域标识或绕过访问规则；需要转换时，可在合法可见的作用域使用 `json::custom_converter<T, Adapter>`。
这些类型上的 `JsonFactory` 或 `JsonConverter` 会报告自动映射的作用域限制。
`JsonPolymorphic` 向表类自身生成静态方法，可使用没有类型/值模板参数的局部或嵌套 class；
生成方法保持表类的所有权与可见性，基类和变体仍须满足各自的映射与访问规则。

生成映射在原声明所在模块解析命名空间内的字段类型、模块内别名、基类和工厂/转换器名称；
跨文件消费者调用导出的类型化转换函数。生成代码继续接受普通访问权限与构造规则检查。
基类别名链在别名所属模块解析；同名模板遵循 Core 的参数数量、默认参数及模块优先规则。
声明查找遵循直接导入、显式再导出与项目内部可见性，不从其他不可见模块选取同名声明。

模板映射按声明顺序保留类型参数和值参数；整数、bool 和枚举值参数参与具体类型的推导及基类字段类型替换。
值表达式中命名空间内的 const/constexpr 全局量、constexpr 函数及枚举成员会保留限定作用域。
生成器为模板参数分配独立的内部名称，避免与辅助函数的参数及局部变量冲突；Schema 引用按完整封闭类型区分，
同一上下文中的不同值参数实例不会共用错误的类型缓存。同一封闭类型的重复引用继续复用已有定义。
`static constexpr` 字段与其他静态字段一样不参与实例映射。`JsonPolymorphic` 注册表必须是非模板 class。

## 二进制库

使用 JSON 处理器构建的导出类型会在二进制库中保留对应转换方法，消费方不依赖原始源码标签。
未启用该处理器构建的普通库，可以在消费者中为公开字段生成映射；构造及私有访问仍遵守语言规则。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/script-mapping.md) · [fr-FR](../../fr-FR/Reference/script-mapping.md) · [ja-JP](../../ja-JP/Reference/script-mapping.md) · [ko-KR](../../ko-KR/Reference/script-mapping.md) · [zh-CN](script-mapping.md) · [zh-TW](../../zh-TW/Reference/script-mapping.md)
