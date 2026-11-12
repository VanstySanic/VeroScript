# 属性与方法函数值

`attribute.vs` · `callable.vs` · `core.vs`

`std::attribute<T>` 保存固定值或同步 getter。副本具有独立配置；函数捕获沿用语言原有的所有权。每次读取重新求值，不自动缓存。

```VeroScript
import "attribute.vs";

int read_count()
{
	return 42;
}
void main()
{
	std::attribute<int> count = 10;
	count.bind(read_count);
	int snapshot = count;
	assert(snapshot == 42 && count() == 42);
	count.reset();
	assert(!count.try_get().has_value());
}
```

`get()`、`operator()` 和转换到 `T` 都读取一次。`try_get()` 返回 optional；`get_or(fallback)` 的备用实参提前求值。未绑定函数和执行错误正常传播；只有未设置、可选 reader 返回空、弱目标过期才表示无值。

`set(value)`、`bind(fn()->T)`、`bind_optional(fn()->optional<T>)` 和 `reset()` 整体替换配置。`is_set()`、`is_bound()` 只检查配置，不执行 getter。

`try_get(ref output)` 向已有变量读取一次，并返回是否取得值。属性未设置或 optional reader 返回空值时，`output` 保持不变；getter 异常继续传播。需要向已有变量写入时可用此重载，返回 optional 的原有 `try_get()` 仍可使用。

`has_same_configuration(ref other)` 不执行 getter，只比较配置身份。复制后最初共享该身份；替换任一配置后，即使值相等，配置也不同。绑定结果可能在配置不变时变化，因此不能据此跳过动态 getter。

`std::make_attribute_by_name<T>(target, "read")` 创建强对象无参 getter。`T` 显式指定，目标类型自动推导，也可作为第二个类型实参指定。名称区分大小写，创建时完成解析，调用保留正常访问与虚分派规则。

`std::make_weak_attribute<T>(weak_target, getter)` 每次读取锁定目标一次，getter 签名为 `fn(U)->T`。`std::make_weak_attribute_by_name<T>(weak_target, "read")` 按名称绑定无参方法。弱目标过期后仍处于已配置状态。结构体使用 `shared<S>` 或 `handle<S>` 包装。

`std::bind_method<F>(target, "method")` 返回普通的类型化函数值。目标必须具有签名完全匹配的 public 实例方法，保留虚分派并强持有目标。结果可以传给 `attribute.bind`、`delegate.bind` 或 `multicast_delegate.add`；移除时保存并使用实际添加的函数值。只需要此工厂时可导入 `callable.vs`。

独立 AOT：C++ 支持固定值、getter 和弱目标；C#、Python 支持固定值、具名 getter 和字面量强方法名称。Python 源码 Lambda、C#/Python 弱绑定保持原有的不支持边界。动态名称、宿主名称绑定和有歧义的泛型选择需要运行时，导出时明确拒绝。VM、Mixed、JIT 使用同一核心实现。

C++ 和 C# 源码导出支持 `try_get()` 与 `try_get(ref output)`。Python 支持 `try_get()`，但拒绝可写 `ref` 输出。三者均支持只读的 `has_same_configuration(ref other)`；这一特殊转换不会为 Python 增加通用可写引用支持。

[`attribute_demo.vs`](../../../../Scripts/examples/attribute_demo.vs) · [`attribute_functions.vs`](../../../../Scripts/examples/attribute_functions.vs) · [`attribute_methods.vs`](../../../../Scripts/examples/attribute_methods.vs) · [`attribute_weak.vs`](../../../../Scripts/examples/attribute_weak.vs)

同一程序中的标准库应保持一个模块身份。库包已包含并重导出 attribute 时，使用包导出的声明或统一公共依赖，避免同时导入另一份源版 `core.vs`／`attribute.vs` 导致同名导出冲突。

多个库包共用时，先用 `export import "attribute.vs";` 构建一份 `shared-stdlib.vslib`，各库导入该组件；应用导入各库和同一个组件，避免额外导入源版副本。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/attribute.md) · [fr-FR](../../fr-FR/standard-library/attribute.md) · [ja-JP](../../ja-JP/standard-library/attribute.md) · [ko-KR](../../ko-KR/standard-library/attribute.md) · [zh-CN](attribute.md) · [zh-TW](../../zh-TW/standard-library/attribute.md)
