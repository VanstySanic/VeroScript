# Python 值、容器与生命周期

## 类型化容器与引用

公开工厂包括 `Value.array`、`Value.map`、`Value.set`、`Value.queue`、
`Value.priority_queue`、`Value.stack`、`Value.deque` 和 `Value.channel`。
这些工厂要求显式给出元素类型或键和值类型，使空容器也不会产生类型歧义。
普通 `map` 和 `set` 具有唯一元素语义；需要多重映射或多重集合时，应使用相应的
运行时或宿主绑定。`to_python()` 在支持时返回快照，修改快照不会写回脚本容器。

`HostParameter(..., by_ref=True)` 表示以 `Value` 传入的可变引用，
`const_ref=True` 表示只读引用。两个标志不能同时设置；调用失败或显式 `exit`
后不会执行引用写回。

## channel 公开 API

准确的方法名为 `send`、`try_send`、`send_for`、`receive`、`try_receive`、
`receive_for` 和 `close_channel`。`try_send` 与 `send_for` 返回 bool；非阻塞或
限时接收返回值或 `None`。使用 `count`、`channel_capacity` 和 `channel_closed`
读取当前状态。

Python SDK 的 `Value` 表示由 Interop ABI 管理的脚本值。每个 `Value` 拥有独立的原生值句柄；会话操作与保留调用对象还受会话生命周期约束。持有宿主对象的独立值及其协议在原会话关闭后仍可使用，详见[宿主绑定](host-bindings.md)。

## 标量与容器

支持整型、浮点型、布尔、字符串、宽字符串、枚举以及宿主对象等标量。数组、序列、映射、集合和通道通过类型化访问器读取。容器读操作返回快照；修改 Python 侧的列表、字典或集合不会直接修改脚本中的容器。

需要保留容器或对象时，应保留 SDK 返回的 `Value`，而不是只保留一次转换得到的 Python 快照。会话关闭后不能继续调用该会话的操作；独立值句柄的所有权不随会话关闭而转移或消失。

## 调用对象

`Session.retain_callable()` 返回 `RetainedCallable`。它可以通过 `invoke()` 或直接调用语法执行，并支持 `close()`、`closed`、`is_valid` 和上下文管理器：

```Python
from veroscript import Session

with Session() as session:
	session.load_source("""
int add(int left, int right) { return left + right; }
function(int, int)->int make_callback() { return add; }
void main() {}
""", "callback.vs")
	function_value = session.call("make_callback")
	with session.retain_callable(function_value) as callback:
		result = callback(20, 22)
		assert result == 42
```

保留调用对象属于创建线程；会话重载或销毁后可能失效。使用前检查 `is_valid`，完成后及时关闭，避免延长脚本对象和会话资源的生命周期。

## 宿主对象与引用

通过宿主绑定传入的 Python 对象不会自动变成可任意访问的脚本字典。脚本只能使用已经注册的类型、字段和方法。`by_ref` 与 `const_ref` 参数表达引用和值修改权限；是否允许修改还取决于注册的宿主成员及脚本类型语义。

map/set 迭代以及 `keys()`、`values()`、`items()` 返回拥有的快照，可以保留。标量转换为普通 Python 值，其余元素具有独立拥有的 `Value` 句柄。普通 map/set 的顺序未规定；`ordered=True` 保持首次插入顺序。这些读取操作不支持直接写回元素；修改时应调用 VeroScript 函数或宿主回调。

## 错误与回调

类型不匹配会报告 `TypeMismatchError`；线程或生命周期错误会报告 `ThreadError`；宿主回调失败会包装为 `CallbackError`。不要让异常穿过 SDK 边界，也不要在诊断回调中假设可以安全地重入同一个会话。

`Value(None)` 创建 VeroScript 的 `null` 值。向声明为非可空类型的值赋予
`None` 会抛出 `TypeError`。使用 `Value.priority_queue("int", [3, 1, 2])`
创建优先队列；普通 `map` 和 `set` 不保留重复项。

`kind`、`type_name` 和 `value` 属性用于读取当前表示。`clone()` 会创建另一个由调用方拥有的 `Value` 句柄；其行为遵循运行时的克隆语义，不保证深拷贝。每个 `Value` 都拥有原生值句柄，并在 Python 对象销毁时释放；`Value` 没有公开的 `close()` 方法。会话操作返回的值由调用方拥有。会话和保留 callable 可用上下文管理器关闭。

阻塞的 `receive()` 会释放 GIL，让其他 Python 线程继续运行。channel 元素类型必须符合运行时的 channel 契约。

拥有所有权的 Python 宿主值存活期间会保留其克隆、相等和哈希回调，即使回调形成引用环或原会话已关闭。创建线程上的会话自动回收也会保护这些存活的协议。需要确定性的释放时，请使用会话上下文管理器；将最后一个值交给其他线程之前，应先在创建线程关闭会话。

容器支持 `len`、迭代、索引、`get` 和 `contains` 读取。`Session.construct` 创建对象值；`get_field`、`set_field`、`call_method` 访问已注册成员。`weak_this=True` 避免保留方法的接收对象，调用方须处理接收对象过期。`closed` 表示包装器已显式关闭，`is_valid` 表示目标仍然有效。上例得到 `42`。

`count`、`channel_capacity`、`channel_closed` 是只读属性，不带调用括号。以下完整示例成功结束，不产生输出。

```Python
from veroscript import Value

channel = Value.channel("int", 1)
assert channel.channel_capacity == 1 and not channel.channel_closed
assert channel.try_send(42)
assert channel.count == 1
assert channel.receive() == 42
channel.close_channel()
assert channel.channel_closed
```

## 精确数值

`RuntimeCapabilities.NUMERICS` 表示核心支持 bigint/rational。这些值以
`kind == "host_object"` 的 `Value` 表示；使用
`NumericKind(value.numeric_kind)` 验证 `BIGINT`、`RATIONAL` 或 `NONE`。
脚本调用与宿主回调均通过 `Value` 传递，不自动转换为 Python 整数或浮点数。

`session.numerics.bigint(text)`、`rational(text)` 和
`parse(text, NumericKind.BIGINT)` 解析显式文本，`to_text(value)` 输出规范文本。
`convert(value, type_name, exact_float=False)` 返回保留目标固定宽度整数、
`float` 或 `double` 类型的 `Value`；溢出以及 `exact_float=True` 时无法精确
表示的浮点转换均报告错误。

```Python
from veroscript import Backend, NumericKind, Session

with Session(backend=Backend.VM) as session:
	numerics = session.numerics
	integer = numerics.bigint("18446744073709551615")
	assert integer.numeric_kind == NumericKind.BIGINT
	assert numerics.convert(integer, "uint64").value == 18446744073709551615
	assert numerics.to_text(numerics.rational("6/18")) == "1/3"
```

`Session.set_numeric_limits` 接受关键字参数 `max_value_bits`、
`max_input_bytes`、`max_output_bytes`、`max_scratch_bytes`、`max_work_units`。
零选择核心的有限默认值。这些操作预算不限制整个进程的总内存。
会话数值操作要求处于创建线程且会话空闲；宿主回调内使用
`current_numerics()` 借用当前上下文，其方法在没有活动回调时会失败。
原生数值操作释放 GIL。独立 Python AOT 输出暂不支持 bigint/rational。

## 模块相关值的所有权

参阅[Interop SDK — 模块相关值的所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#value-domain-confinement)。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](values.md) · [zh-TW](../zh-TW/values.md)
