# C# 值与所有权

`VeroScriptValue` 拥有独立的 Interop 值句柄，不是会话的子对象。使用 `Dispose()` 或 `using` 释放拥有值。`Clone()` 按运行时克隆语义创建另一个拥有句柄，不保证深拷贝。`Reset()` 将当前值替换为空值。并发修改或释放需要由调用方同步。

## 依赖模块的值内容

拥有句柄不代表值内容独立于会话。脚本对象、函数及具有模块值槽位的可变容器，即使为空，
仍只能在所属会话的创建线程使用，会话关闭后不可访问。主机创建的同类存储在首次交给会话时
建立归属，已有共享别名也遵守该归属。`Clone()` 不会解除这些限制。VM 工作线程回调只能
导出独立的拥有值；具有 variant/函数槽位的空容器也不能克隆到外部拥有存储。私有的借用
回调返回槽仍可在工作线程内转发值。使用后仍应释放拥有句柄，包括会话已经关闭的情况。

## 标量

读取方法应与脚本类型一致：

| 设置 | 读取 | 脚本类型 |
| --- | --- | --- |
| `Set(sbyte)` | `GetInt8()` | `int8` |
| `Set(byte)` | `GetUInt8()` | `uint8` |
| `Set(short)` | `GetInt16()` | `int16` |
| `Set(ushort)` | `GetUInt16()` | `uint16` |
| `Set(int)` | `GetInt32()` | `int32` |
| `Set(uint)` | `GetUInt32()` | `uint32` |
| `Set(long)` | `GetInt64()` | `int64` |
| `Set(ulong)` | `GetUInt64()` | `uint64` |
| `Set(float)` | `GetFloat()` | `float` |
| `Set(double)` | `GetDouble()` | `double` |
| `Set(bool)` | `GetBool()` | `bool` |
| `SetString` | `GetString` | `string` |
| `SetWideString` | `GetWideString` | `wstring` |

类型化枚举使用 `SetEnum(typeName, value)` / `GetEnum()`；`Kind` 与 `TypeName` 描述当前表示。`SetNull()` 创建 `nullptr`，`IsNull` 只检查该值。`Reset()` 后 `Kind` 为 `Invalid`，`IsNull` 为 false。访问已释放包装器会抛出 `ObjectDisposedException`。空宿主引用是否允许，由接收参数的契约决定。

`Set(VeroScriptValue)` 赋值完整的运行时值，保留容器、宿主对象、函数和通道的身份语义。目标必须可写；只读借用值不能作为目标。

## 容器与通道

`SetArray`、`SetSequence`、`SetMap`、`SetSet`、`SetChannel` 要求显式类型名。带 `VeroScriptValueKind` 的重载选择具体类型：`SetSequence` 支持包括 `Queue`、`PriorityQueue`、`Stack`、`Deque` 的序列；`SetMap` 支持 `Map`、`OrderedMap`、`MultiMap`、`OrderedMultiMap`；`SetSet` 支持 `Set`、`MultiSet`、`OrderedMultiSet`。

普通 map 替换重复键的值，multi-map 保留每个键值关联；普通 set 消除重复元素，multi-set 保留重复次数。优先队列的运行时 `top` / `pop` 优先提供最高优先级元素。

使用 `SequenceCount` / `GetSequenceElement`、`MapCount` / `TryGetMapValue`、`SetCount` / `SetContains` 读取相应容器。`EnumerateMap`、`EnumerateSet` 要求 `VeroScriptCapabilities.ContainerEnumeration`，访问只读快照。回调中的 `VeroScriptBorrowedValue` 在每次回调返回时失效；要保留值，应在该次回调中 `Clone()`。

```CSharp
using var key = new VeroScriptValue();
key.SetString("answer");
using var number = new VeroScriptValue();
number.Set(42);
using var map = new VeroScriptValue();
map.SetMap("string", "int", [key], [number]);

map.EnumerateMap((entryKey, entryValue) =>
{
	Console.WriteLine(entryKey.GetString());
	Console.WriteLine(entryValue.GetInt32());
});
```

通道 API 为 `ChannelSend`、`ChannelTrySend`、`ChannelSendFor`、`ChannelReceive`、`ChannelTryReceive`、`ChannelReceiveFor`、`ChannelClose`。`ChannelTrySend`、`ChannelSendFor` 返回 bool，超时以毫秒计。非阻塞和限时接收返回拥有值或 `null`。`ChannelCount`、`ChannelCapacity`、`IsChannelClosed` 提供当前状态。不要在需要保持响应的线程上执行阻塞接收。运行时同步通道操作；并发使用期间不能重置、替换或释放包装器。

## 宿主对象与保留调用对象

`SetHostObject(typeId, typeName, managedObject)` 在原生值持有期间保持托管对象存活。`Func<string, bool> isType` 重载可报告兼容的宿主子类型。`GetHostObject()` 返回 `VeroScriptHostObjectInfo`；`GetHostObject<T>()` 检查托管类型。

`VeroScriptCallable` 拥有保留的函数或闭包，使用后应释放。默认只允许创建线程调用。通过 `RetainCallable(..., ownerThreadOnly: false)` 允许其他线程调用后，宿主仍须将调用与关联会话的所有操作串行化，并保护自身状态。调用对象内部的锁不保护整个会话。

## 借用与生命周期

原生函数、拦截器、泛型工厂、序列化、修改通知和访问器中的借用上下文及值，仅限当前回调线程使用。跨线程访问抛出状态为 `WrongThread` 的 `VeroScriptException`，回调返回后访问抛出 `InvalidOperationException`。仅允许转移的独立值内容可在回调线程克隆后交给其他线程；克隆不会解除依赖模块的值内容的限制。

## 精确数值 API

C# 将 Core bigint/rational 结果保留为不透明 VeroScriptValue 宿主值。VeroScriptCapabilities.Numerics 表示原生能力，不增加 System.Numerics.BigInteger 自动转换或公开托管数值服务。使用类型化脚本函数或公开 C ABI 完成集成。

<a id="managed-call-lifetime"></a>

## 托管调用生命周期

原生操作将提供句柄的托管值／包包装器保留到调用及返回原生视图的复制完成，包括 `IReadOnlyList<VeroScriptValue>` 索引器即时产生的值，以及阻塞 channel 操作或 visitor 的接收对象。保留的是包装器快照：回调中替换参数数组槽位，不会把原生 `ref`／`out` 写回重定向到替换值。这些生命周期保证不允许并发显式释放，也不延长回调借用范围。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](values.md) · [zh-TW](../zh-TW/values.md)
