# 反射

VeroScript 提供运行时反射接口，可查询模块、类型、继承关系、全局变量、自由函数、字段、方法、枚举成员和声明元数据，也可通过类型化操作访问公开的脚本实例字段，并调用声明在脚本类或接口中的公开方法。反射不会绕过脚本访问权限。

## 反射对象

语言提供以下公开信息类型：

- `type<T>`：描述类型 `T` 本身。
- `field_info<T>`：描述 `T` 的一个字段。
- `method_info<T>`：描述 `T` 的一个方法。
- `enum_member_info<T>`：描述枚举类型 `T` 的一个枚举成员。
- `module_info`：描述当前模块视图。
- `function_info`：描述一个可见的自由函数。
- `global_info`：描述一个可见的顶层全局变量。

这些类型由编译器提供，不需要导入额外模块。查询使用类型的限定名称和从零开始的索引；索引无效时，运行时会报告错误。

## 类型信息

`type<T>` 的公共成员如下：

| 成员 | 作用 |
| --- | --- |
| `name` | 类型的短名称。 |
| `qualified_name` | 类型的限定名称，可用于创建 `field_info<T>` 和 `method_info<T>`。 |
| `kind()` | 返回类型类别。 |
| `is_abstract()` | 判断类型是否为抽象类型。 |
| `base_count()` / `base_name(i)` | 查询直接基类型数量和名称。 |
| `field_count()` | 查询可反射字段数量。 |
| `method_count()` | 查询可反射方法数量。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` | 查询类型声明上的元数据。 |
| `has_metadata(key)`、`metadata(key)` | 按名称直接查找元数据。 |
| `has_annotation(name)` | 判断是否存在指定注解；`name` 与 `@name` 均可。 |
| `enum_member_count()`、`enum_member_name(i)`、`enum_member_value(i)` | 查询枚举成员。 |
| `enum_underlying_type()` | 查询枚举的底层类型。 |
| `is_scoped_enum()` | 判断枚举是否为作用域枚举。 |

枚举专用查询对非枚举类型没有有意义的结果；调用前应先根据 `kind()` 判断类型类别。

## 字段信息

通过 `field_info<T>(owner, index)` 查询字段：

```VeroScript
type<Record> info = type<Record>();
field_info<Record> field = field_info<Record>(info.qualified_name, 0);

print(field.name());
print(field.declared_type());
print(field.runtime_type());
print(field.is_static());
print(field.is_mutable());
```

字段信息提供以下查询：

| 成员 | 作用 |
| --- | --- |
| `name()` | 字段名称。 |
| `declared_type()` | 源代码声明的字段类型。 |
| `runtime_type()` | 运行时记录的字段类型。 |
| `is_static()` | 判断字段是否为静态字段。 |
| `is_mutable()` | 报告字段是否使用上下文相关的 `mutable` 修饰符。 |
| `is_declared_on_type()` | 判断字段是否直接声明在被查询的类型上。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` | 查询字段声明上的元数据。 |
| `has_metadata(key)`、`metadata(key)`、`has_annotation(name)` | 按名称查询字段元数据和注解。 |

## 方法信息

通过 `method_info<T>(owner, index)` 查询方法：

```VeroScript
method_info<Record> method = method_info<Record>(info.qualified_name, 0);

print(method.name());
print(method.return_type());
print(method.parameter_count());
print(method.parameter_type(0));
print(method.parameter_is_ref(0));
print(method.parameter_is_const(0));
```

方法信息提供以下查询：

| 成员 | 作用 |
| --- | --- |
| `name()` | 方法名称。 |
| `return_type()` | 返回类型。 |
| `parameter_count()` | 参数数量。 |
| `parameter_type(i)` | 第 `i` 个参数的基础类型。 |
| `parameter_name(i)` | 第 `i` 个参数的名称。 |
| `parameter_is_ref(i)` | 判断参数是否为引用参数。 |
| `parameter_is_const(i)` | 判断参数是否为常量参数或只读引用。 |
| `is_static()` | 判断是否为静态方法。 |
| `is_virtual()` | 判断是否为虚方法。 |
| `is_abstract()` | 判断是否为抽象方法。 |
| `is_const()` | 判断成员方法是否为 `const` 方法。 |
| `is_deleted()` | 判断方法是否被 `= delete` 删除。 |
| `is_constructor()` / `is_destructor()` | 判断是否为构造函数或析构函数。 |
| `metadata_count()`、`metadata_key(i)`、`metadata_value(i)` | 查询方法声明上的元数据。 |
| `has_metadata(key)`、`metadata(key)`、`has_annotation(name)` | 按名称查询方法元数据和注解。 |

`parameter_type(i)` 返回去掉 `ref` 或 `const ref` 修饰后的基础类型。若需要恢复参数的完整语义，必须同时检查 `parameter_is_ref(i)` 和 `parameter_is_const(i)`。

## 可执行反射

选定描述符后，可以调用 `field_info<T>.get<V>(对象)`、`set<V>(对象, 值)`、`method_info<T>.invoke<R>(对象, 参数...)` 和 `invoke_static<R>(参数...)`。`V` 和 `R` 必须显式给出，并与成员的运行时类型精确一致；`R` 可以为 `void`。参数按顺序求值一次，调用前核对描述符的固定签名。重载需结合 `parameter_count()` 和 `parameter_type(i)` 选择。

`set<V>` 不允许写入字段自身声明为 `const` 的成员，即使接收对象可写；接收对象为 `const` 时，仅 `mutable` 字段可写。`get<V>` 可以读取这些字段。

公开的脚本实例字段，以及声明在脚本类或接口中的公开方法可以执行。抽象接口方法会解析到接收对象的具体实现；有方法体的公开接口方法和公开静态接口方法也受支持。实例方法保留虚分派，结构体字段写入会作用于原对象。无效、伪造、过期、来自其他程序或不可访问的描述符会在运行时失败。私有/受保护成员、宿主成员、静态字段、构造/析构、删除的方法仍可查询，但不能通过这些接口执行。C++、Python 和 C# AOT 目前在编译期拒绝可执行反射。不提供旧开发构建字节码包的兼容支持，请使用当前编译器从源码重建。完整用法见 [可执行反射样例](../../../../Scripts/examples/reflection_executable.vs)。

`invoke` 与 `invoke_static` 支持引用参数和引用返回值。引用实参必须显式写成 `&value` 或 `ref value`；传入的类型必须精确匹配，可写引用不能绑定只读存储。返回引用时显式使用 `invoke<int&>(receiver, ...)` 或 `invoke<const int&>(receiver, ...)`，静态调用同理。返回值保留别名和只读属性；普通值读取会取出引用的值。引用存储沿用普通调用的受管理引用单元，不复制实参来模拟写回。全部参数校验通过后才进入方法；进入后发生异常时，已产生的修改遵循普通调用规则。

若要把引用返回继续作为引用实参传入，先绑定一个局部引用，再传入 `&alias` 或 `ref alias`。

对象实参还会按调用点的静态类型校验：允许合法的向上转换和空句柄，隐式向下转换会在方法体执行前被拒绝。需要向下转换时，先显式使用 `cast<T>`。`set<V>` 在编译期检查源类型是否可赋给 `V`。`void` 表达式不能作为实参。

`set<V>` 对值执行普通赋值的受检转换，因此可用 `set<int64>` 将 `int` 值写入 `int64` 字段。数值溢出会在字段修改前被拒绝；`V` 仍须与字段声明类型精确一致。

按值传递的结构体和定长数组实参会在该实参求值时复制，早于后续实参对原数据的修改。引用返回用于初始化按值变量、传递按值参数或产生按值返回时，也会复制其值。类与接口句柄沿用普通调用语义。旧字节码包应从源码重建，以取得这些编译器修复。

## 模块、自由函数、全局变量与枚举成员信息

`module_info()` 描述从入口模块可见的声明。使用 `function_count()`、`global_count()` 和 `type_count()` 取得边界，再构造 `function_info(i)` 或 `global_info(i)`；`type_name(i)` 返回可见脚本类型的名称。被导入模块的私有声明不会泄漏，入口模块自身的私有声明和正常导出的导入声明仍遵循原有 import/export 可见性规则。

`function_info` 提供名称、稳定链接名、模块路径、返回与参数签名、`ref`/`const` 标记、deleted/exported 标记及声明元数据。`global_info` 提供名称、链接名、模块路径、声明类型、运行时类型、const/exported 标记及声明元数据。静态数据成员仍作为字段反射，不会在全局变量列表中重复出现。

对于枚举 `E`，可使用 `enum_member_info<E>(type<E>().qualified_name, i)` 查询成员名称、完整 `int64` 值及声明元数据；它是 `type<E>` 上简洁枚举查询接口的补充。

## 声明元数据

声明可以附加注解。语言会保存注解的名称、键和值，但不会解释注解的业务含义：

```VeroScript
@entity(sample)
struct BaseRecord
{
	int id;
};

struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}
};
```

宿主、编辑器或其他工具可以通过 `metadata_count()`、`metadata_key(i)` 和 `metadata_value(i)` 枚举这些信息，也可以使用 `has_metadata(key)`、`metadata(key)` 和 `has_annotation(name)` 直接查询。键不存在时 `metadata(key)` 返回空字符串；需要区分“空值”和“不存在”时，应先调用 `has_metadata(key)`。`@entity`、`@data` 和 `@call` 在本例中只是可查询的元数据名称，不代表语言内置的业务行为；脚本或宿主集成可以定义自己的约定。

宿主绑定目录中的注解描述与脚本运行时反射是两个层次：绑定目录用于描述宿主公开 API，而这些反射信息类型用于查询当前已编译脚本模块。

## 完整示例

下面的示例查询继承、字段、方法、静态方法、顶层全局量、枚举成员和声明元数据：

```VeroScript
@setting(primary)
export const int ReflectionVersion = 1;

@utility(sample)
export int echo_value(const int& value)
{
	return value;
}

enum class RecordState
{
	@display(primary)
	Ready,
	Archived
};

struct BaseRecord
{
	int id;

	int read_id() const
	{
		return id;
	}
};

@entity(sample)
struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}

	static int identity(int value)
	{
		return value;
	}
};

int main()
{
	type<Record> info = type<Record>();
	print("{} ({})", info.name, info.kind());
	print("bases={}, fields={}, methods={}",
		info.base_count(), info.field_count(), info.method_count());

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}",
			field.name(), field.declared_type(), field.runtime_type(), field.is_static());
	}

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, params={}, static={}, virtual={}",
			method.name(), method.return_type(), method.parameter_count(),
			method.is_static(), method.is_virtual());
	}

	module_info module = module_info();
	print("module={}, functions={}, globals={}, types={}",
		module.path(), module.function_count(), module.global_count(), module.type_count());

	for (int i = 0; i < module.function_count(); i++)
	{
		function_info function_meta = function_info(i);
		print("function {}: returns={}, exported={}",
			function_meta.name(), function_meta.return_type(), function_meta.is_exported());
	}

	for (int i = 0; i < module.global_count(); i++)
	{
		global_info global_meta = global_info(i);
		print("global {}: declared={}, runtime={}, const={}",
			global_meta.name(), global_meta.declared_type(),
			global_meta.runtime_type(), global_meta.is_const());
	}

	type<RecordState> state_type = type<RecordState>();
	enum_member_info<RecordState> state =
		enum_member_info<RecordState>(state_type.qualified_name, 0);
	print("enum member {}={} annotated={}",
		state.name(), state.value(), state.has_annotation("display"));
	return 0;
}
```

可运行版本见[示例索引](../examples.md)中的反射示例。反射 API 与入口函数返回类型无关；示例使用 `int main` 并显式返回进程状态码。

## 边界和错误

- 反射支持信息查询和上述类型化执行操作，但不提供对任意成员的无限制动态访问。
- `field_count()`、`method_count()` 和枚举成员数量都是当前运行时类型信息的结果，应在访问前检查索引范围。
- 对不存在的类型信息、无效索引或不适用的查询，运行时会报告错误；不要把无效索引当作“没有结果”。

类型系统的基础规则见[数据类型](data-types.md)、[结构体](structs.md)、[类](classes.md)和[枚举](enums.md)。宿主侧的接口描述见[绑定目录](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md)。

## 结构化反射

<a id="reflection-metadata-contract"></a>

`type<T>`、field_info、method_info、module_info、function_info、global_info、enum_member_info 提供 annotation_count/annotation/annotations、comment_count/comment/comments、documentation、comments_available、comments_retention、declaration 和 source_metadata。按索引访问前检查数量。annotation_info 与 annotation_argument_info 提供 argument_count/argument/arguments，类型化值访问须与标量类型相符。definition() 返回已注册的描述信息，可能未注册时先检查 is_valid()。

宿主选择 Full、Documentation 或 NoComments；Unavailable 表示没有源码信息。NoComments 不移除注解或另行注册的文档。元数据对象保留不可变快照，热重载不会让旧对象悄然改指向。查询不会绕过访问控制，也不会让私有成员变得可调用。

[注解与源码元数据](annotations.md)

## 嵌套类型反射

所属查询使用 `display_name()`、`is_nested()`、`is_local()`、`declaring_type_name()`、`declaring_function_name()` 和 `access()`；不属于类型成员的声明返回 `none` 访问类别。`qualified_name` 仍为具体运行时身份，显示名称不能作为身份键。

`nested_type_count()`、`nested_type_name(i)`、`nested_type_parameter_count(i)` 和 `nested_type_declaration(i)` 枚举可访问的直接子类型声明，包含尚未物化的泛型。参数数量不计捕获的外层参数；别名不产生额外运行时类型。查询创建时固定访问上下文，局部／私有名称不会因此进入全局发现。

## 模板元数据

`module_info.template_declaration_count()` 查询开放声明，不进行实例化。按索引调用 `template_declaration_id/name/kind/owner` 可取得记录身份。`template_parameter_count`、`template_own_parameter_count` 和 `template_enclosing_parameter_count` 区分完整、自有与外层环境。`template_parameter_id/name/kind/value_type/default/is_enclosing` 接受 `(声明索引, 参数索引)`；完整参数表先列外层参数。

`type<T>`、`method_info<T>` 和 `function_info` 提供 `template_declaration_id()`、`template_argument_count()`，以及按索引查询的 `template_argument_encoding/name/kind/value_type`。外层实参使用独立的 `template_enclosing_argument_count/encoding/name`。普通非模板声明返回空身份及零数量。编码保留整数宽度、符号和枚举身份，显示名不能作为身份键。开放查询不产生可执行方法；已有仅类型参数的数量查询保持原含义。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/reflection.md) · [fr-FR](../../fr-FR/language-basics/reflection.md) · [ja-JP](../../ja-JP/language-basics/reflection.md) · [ko-KR](../../ko-KR/language-basics/reflection.md) · [zh-CN](reflection.md) · [zh-TW](../../zh-TW/language-basics/reflection.md)
