# Python 宿主绑定

持有宿主对象的独立 `Value` 及其子类型检查，在创建它的 `Session` 关闭后仍可安全使用。类型查询使用 Python 类的弱引用。带 clone／equals／hash 协议的值会保留调用这些协议所需的回调状态，因此仍可用作 map/set 键；最后一个相关值释放后，该状态也会释放。不带这些协议的值不会额外保留无关类或会话回调状态。

`HostClass` 的公开方法为 `constructor(...)`、`method(...)`、
`static_method(...)`、`property(...)` 和 `field(...)`，用于明确描述暴露给
VeroScript 的成员。

宿主绑定把 Python 函数、类和序列化器以显式的 VeroScript 类型契约暴露给脚本。绑定名称、参数类型、引用标志和文档字符串会进入会话的绑定元数据；脚本只应依赖已经注册的公共成员。

## 函数与参数

可以使用 `register_function` 注册函数，也可以使用 `host_function` 创建带类型信息的绑定。`HostParameter` 描述 `type_name`、参数名、`by_ref`、`const_ref` 和 `documentation`。参数契约必须与实际回调签名及脚本调用方式一致。

```Python
from veroscript import HostParameter

session.register_function(
	"Add",
	add,
	parameters=(
		HostParameter("int", "left"),
		HostParameter("int", "right"),
	),
	return_type="int",
)
```

脚本函数、构造函数、方法和属性使用脚本类型名称描述；不要用 Python 的隐式转换来掩盖不兼容的注册签名。需要修改实参时显式使用可变引用契约，并让 `const_ref` 保持只读。

## 类、属性与构造

`HostClass` 用于声明宿主类的构造函数、实例方法、静态方法以及读写属性，再通过 `register_class` 注册。字段和方法的可见性由绑定定义决定，而不是由 Python 对象的动态属性决定。`construct`、`call_method`、`get_field` 和 `set_field` 只操作已注册成员。

## 序列化与变更事件

`register_serializer`、`serialize_object` 和 `deserialize_object` 用于在明确的格式边界上转换宿主对象。序列化器应验证输入类型和版本，不能把不可信数据直接当作已验证的宿主对象。

`set_mutation_callback` 接收脚本对宿主对象或容器产生的变更事件。事件包含 `MutationKind` 等结构化信息；回调不应修改正在遍历的对象，也不应在回调中销毁所属会话。通知无法回滚已经完成的变更；需要拒绝或记录变更时，应在绑定策略中明确处理，而不是依赖回调顺序。

## 宿主类型、元数据与拦截

宿主成员必须使用显式签名。签名可以使用标量、已注册的宿主对象、
`enum<Name>` 以及 `array<int>`、`map<string,PythonObject>` 等嵌套容器；
非标量回调值使用 `Value` 表示。宿主类成员目前使用按值参数；自由函数还
支持可变 `ref` 和只读 `const_ref` 参数。

`field(...)` 默认提供实时 getter 和可写 setter；传入 `writable=False` 可设为
只读。`register_struct(...)` 用于注册值语义宿主类型；如果 VeroScript 可能
复制该值，应通过 `protocol(...)` 提供 clone；将其用作 map/set 键时还需要
equals 和 hash。`register_enum(...)` 要求 Python 类型继承 `IntEnum` 或 `IntFlag`；普通的
`enum.Enum` 会被拒绝。通过
`register_generic_class(...)` 或 `register_generic_struct(...)` 注册的泛型模板，
必须在加载依赖它的脚本之前使用 `instantiate(...)` 完成具体化。
对于允许继承的宿主类，应在 `register_class(...)` 中传入 `inheritable=True`，并通过
`protocol(subclass_factory=...)` 提供脚本子类创建托管实例所需的工厂。

注解名默认区分大小写。使用
`register_annotation("@GuiWidget", case_sensitive=False)` 后，处理器订阅与脚本反射
按忽略 ASCII 大小写的规则匹配该注解名，并保留源码中的拼写。参数名和元数据键仍然
精确匹配。名称策略冲突会抛出 `Error`，已有注册保持不变。此注册不会安装编译期处理器。

`register_annotation(...)` 注册注解词汇及其参数，`register_type_alias(...)` 注册
具体或泛型类型别名。`set_instance_call_interceptor(...)` 接收已经解析的
`InterceptContext`：返回 `InterceptResult.UNHANDLED` 使用正常实现，最多调用一次
`context.proceed()` 后返回 `InterceptResult.HANDLED`，也可以返回 `(InterceptResult.HANDLED, replacement)`；
`InterceptResult.FAILED` 表示调用失败。回调结束后 context 立即失效；可信的宿主调用可以使用
`call_method_bypassing_interceptor(...)` 绕过拦截器。

如果签名使用已注册的宿主基类，可以传入已注册且兼容的子类型实例。运行时依据
已注册的类型关系判断兼容性，不会从任意 Python 类自动推断。

## 回调限制

回调执行期间 Python GIL 可用，但回调异常会转换为 `CallbackError`。不要让异常跨越 C ABI，不要把会话或借用值交给其他线程，也不要在诊断和变更回调中无限递归地调用脚本。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
