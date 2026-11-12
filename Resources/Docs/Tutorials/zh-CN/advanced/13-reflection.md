# 13：运行时反射

本篇使用一个小型“类型检查器”理解 VeroScript 的反射接口。反射 API 提供当前脚本类型系统的元数据，也支持对脚本公开字段和方法执行类型明确的操作。它适合工具、编辑器和序列化适配器；不会把任意成员自动变成可通过字符串动态调用的接口，也不会绕过访问控制。

## 第一步：给声明添加元数据

注解可以附加在类型、字段、方法、自由函数、全局变量和枚举成员上：

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

	int score;

	int add_score(int amount)
	{
		score += amount;
		return score;
	}
};
```

语言会保存注解的名称、键和值，但不会解释它们的业务含义。这里的 `entity`、`data` 和 `call` 是用户定义的元数据约定，不是自动生成行为的关键字。

下面各步都接在同一个文件中，请保留这段 `Record` 声明，再逐步替换或扩展 `main` 函数。

## 第二步：取得类型信息

`type<Record>` 描述 `Record` 本身：

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	print("name={}, qualified={}", info.name, info.qualified_name);
	print("kind={}, abstract={}", info.kind(), info.is_abstract());
	print("fields={}, methods={}", info.field_count(), info.method_count());
	return 0;
}
```

`name` 是短名称，`qualified_name` 是限定名称。字段和方法信息对象需要使用限定名称和从零开始的索引构造，因此通常先保存一个 `type<T>` 对象。

## 第三步：枚举字段

通过 `field_info<Record>(info.qualified_name, i)` 逐项读取字段：

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}, mutable={}",
			field.name(), field.declared_type(), field.runtime_type(),
			field.is_static(), field.is_mutable());

		for (int j = 0; j < field.metadata_count(); j++)
		{
			print("  metadata {}={}", field.metadata_key(j), field.metadata_value(j));
		}
	}

	return 0;
}
```

字段信息还可以告诉你字段是否直接声明在当前类型上，以及是否使用了上下文相关的 `mutable` 修饰符。`declared_type()` 适合展示源代码声明，`runtime_type()` 适合展示运行时记录的类型。

## 第四步：枚举方法和参数

`method_info<T>` 提供方法修饰符和参数信息：

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, static={}, virtual={}, const={}",
			method.name(), method.return_type(), method.is_static(),
			method.is_virtual(), method.is_const());

		for (int j = 0; j < method.parameter_count(); j++)
		{
			print("  param {}: name={}, type={}, ref={}, const={}",
				j, method.parameter_name(j), method.parameter_type(j),
				method.parameter_is_ref(j), method.parameter_is_const(j));
		}
	}

	return 0;
}
```

`parameter_type(j)` 返回去掉 `ref` 或 `const ref` 后的基础类型；是否为引用、是否只读要分别检查 `parameter_is_ref(j)` 和 `parameter_is_const(j)`。这样工具可以在显示签名时准确还原参数语义。

## 第五步：检查模块公开面

`module_info` 可以枚举入口模块可见的自由函数、顶层全局变量和脚本类型。先使用数量作为边界，再构造 `function_info(i)` 或 `global_info(i)`：

```VeroScript
module_info module = module_info();
print("module={}, globals={}, types={}",
	module.path(), module.global_count(), module.type_count());

for (int i = 0; i < module.function_count(); i++)
{
	function_info function_meta = function_info(i);
	print("{} -> {}", function_meta.name(), function_meta.return_type());
}

for (int i = 0; i < module.global_count(); i++)
{
	global_info global_meta = global_info(i);
	print("global {}: declared={}, const={}",
		global_meta.name(), global_meta.declared_type(), global_meta.is_const());
}
```

所有声明描述对象都提供 `has_metadata(key)`、`metadata(key)` 和 `has_annotation(name)`。若要查询枚举成员注解，可构造 `enum_member_info<E>(type<E>().qualified_name, i)`。

例如，读取 `RecordState` 的第一个枚举成员：

```VeroScript
type<RecordState> state_type = type<RecordState>();
enum_member_info<RecordState> state =
	enum_member_info<RecordState>(state_type.qualified_name, 0);

print("{}={}, annotated={}",
	state.name(), state.value(), state.has_annotation("display"));
```

## 第六步：处理数量和错误

反射查询使用数量和索引，而不是返回一个可能为空的动态对象。访问前先检查字段、方法、函数、全局变量、类型或枚举成员数量；索引越界、不存在的类型信息以及不适用的查询都会产生运行时错误。

可执行的类型化反射仅支持公开脚本实例字段，以及公开脚本实例方法和静态方法。它不会绕过访问控制，也不提供不受限制的字符串查找或调用。`__reflection_*` 形式的内部辅助符号不是脚本 API，不应直接使用。

## 第七步：读取、写入和调用选定成员

先根据元数据选出描述符，再明确提供字段类型或返回类型。方法参数按位置传递：

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	int score_index = -1;
	int add_index = -1;
	int identity_index = -1;
	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> candidate = field_info<Record>(info.qualified_name, i);
		if (candidate.name() == "score")
		{
			score_index = i;
		}
	}
	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> candidate = method_info<Record>(info.qualified_name, i);
		if (candidate.name() == "add_score")
		{
			add_index = i;
		}
		if (candidate.name() == "identity")
		{
			identity_index = i;
		}
	}
	if (score_index < 0 || add_index < 0 || identity_index < 0) return 1;

	field_info<Record> score = field_info<Record>(info.qualified_name, score_index);
	method_info<Record> add = method_info<Record>(info.qualified_name, add_index);
	method_info<Record> identity = method_info<Record>(info.qualified_name, identity_index);
	Record record = Record();
	score.set<int>(record, 10);
	print("score={}", score.get<int>(record));
	print("add_score={}", add.invoke<int>(record, 5));
	print("identity={}", identity.invoke_static<int>(21));
	return 0;
}
```

`get<V>` 和 `set<V>` 操作实例字段；`invoke<R>` 调用实例方法，`invoke_static<R>` 调用静态方法。选定成员的值类型或返回类型必须与 `V` 或 `R` 完全匹配。完整参考文档列出了不支持的签名和各后端限制。

## 练习

1. 给 `Record` 增加一个静态方法，并用 `is_static()` 找到它。
2. 使用 `base_count()` 和 `base_name(i)` 显示 `Record` 的直接基类。
3. 使用 `enum_member_count()`、`enum_member_name(i)` 和 `enum_member_value(i)` 显示 `RecordState` 的成员。
4. 编写一个只打印带有 `@data` 元数据字段的工具。

完整规则见[反射](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/reflection.md)；检查元数据的示例见 [`examples/reflection.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection.vs)，可执行操作示例见 [`examples/reflection_executable.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection_executable.vs)。宿主接口描述和脚本反射是两个层次，前者见[绑定目录](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md)。

本教程主线到此结束。需要时可查阅[工具链参考](../../../User/zh-CN/toolchain/index.md)或[标准库参考](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/index.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/advanced/13-reflection.md) · [fr-FR](../../fr-FR/advanced/13-reflection.md) · [ja-JP](../../ja-JP/advanced/13-reflection.md) · [ko-KR](../../ko-KR/advanced/13-reflection.md) · [zh-CN](13-reflection.md) · [zh-TW](../../zh-TW/advanced/13-reflection.md)
