# Interop value、callback 与 package

## value 与 container

ABI 暴露 fixed-width scalar、string、enum、host object、array、map、ordered map、set、queue、stack、deque、channel。container enumeration callback 接收 borrowed value。类型必须符合 VeroScript 契约：元素值的类型验证返回 `TYPE_MISMATCH`，执行失败可能返回 `ENGINE_ERROR`，不执行隐式转换。multiset 计数直接累加出现次数数组的长度，不复制元素或分配遍历快照。

对象槽依据源值所属模块的元数据，接受可赋值给所声明基类或接口的派生脚本对象。例如，继承 `Base` 并实现 `IRead` 的 `Child` 可放入 `array<Base>` 或 `queue<IRead>`。容器类型声明仍保持不变性；这不会把 `array<Child>` 转换为 `array<Base>`，也不增加数值转换。

map/set 的键类型声明在容器为空时也会验证，包括 multimap/multiset。不支持的内建类型（`bool`、`float`、`double`、8 位无符号和 16 位整数、容器、函数及引用）返回 `INVALID_ARGUMENT`，不替换原值。基础键类型为 `char`、`int`、`unsigned int`、`int64`、`uint64`、`string`、`wstring` 及其拼写别名。命名 enum/host 类型（包括实例化的 host 类型）由使用该值的会话解析。

channel capacity 在 first use 前配置。send、receive、try、timed、count、capacity、closed、close 有各自 status behavior。closing 会 wake blocked operation；API 提供时，error text 在 owned UTF-8 buffer 中返回。

## 回调上下文

native function、generic host-type factory、instance-call interceptor、mutation callback、external serializer/deserializer 通过 context setter 与 callback result value 报告 failure。callback exception 必须在经 C return 前 convert。user data、host-object payload、managed root 必须在每个可能 callback entry 中存活。

无效的 ApplyConfig 描述符在应用前拒绝。核心应用配置时发生分配或内部错误，可能留下部分已应用设置；依赖这些设置前，应重新应用完整配置。C 取消句柄在成功和失败路径均指向会话实际安装的控制。Linux 分配故障回归覆盖启用／停用控制、取消一致性及重试恢复。

external serialization 还可通过 ABI visitor contract enumerate reference。reference kind、visitor order、borrowed-value lifetime 是 machine-facing data；visitor 不得 retain borrowed value 或 reentrant mutate owning session。map-entry/set-element visitor 同样遵循 borrowed-view rule，并传播 callback failure，不能把 partial traversal 转为 success。

reentrant destruction 或 session clearing 不是 general callback feature。若 implementation reject，ABI 必须返回 engine error，并保留 owning session，而不是 partial clear。

## package 与 reflection

package API 读取/destroy package handle，并暴露 kind/name/version、metadata、canonical signed byte、load/reload/build operation。reflection export loaded type/field/method/global/enum/interface 的 JSON metadata。JSON field name/type name 是 machine contract，不能 localize。

`vs_interop_session_build_package_file` 通过带有 `message_id` `diag.failed_to_build_package` 的 structured diagnostic 报告 build failure。第一个 diagnostic argument 是 underlying build reason；consumer 应按 selected locale 调用 `vs_interop_session_format_diagnostic`，不得将 `message` 当作 diagnostic contract 的替代。

## 通道

channel value 使用 `vs_interop_value_create_channel` 或 `vs_interop_value_create_bounded_channel` 创建。zero bounded capacity 表示 unbounded channel。 `vs_interop_value_channel_send` 阻塞至 accepted，`vs_interop_value_channel_try_send` 为 non-blocking，`vs_interop_value_channel_send_for` 应用 timeout。receive family 有匹配 blocking/non-blocking/timed operation。count、capacity、closed-state、close 是独立 ABI call。closing 会 wake waiter；host 必须把 closed result 视作 terminal，且不得在 callback scope 后 reuse borrowed value。

## 泛型宿主类型上下文

`vs_interop_generic_type_context_*` function 暴露 generic host-type factory 的 template argument count 与 argument。factory 通过同一 borrowed context 提供 result type、member、protocol declaration、error。context accessor/setter 仅在 factory callback 中 valid。callback 必须在 return 前完成 registration；context 不是 retained builder object。

## call 与 interception

`vs_interop_intercept_context_*` 暴露 receiver、resolved target、argument、`proceed`、callback error channel。`proceed` 是已 resolved call 的 interceptor continuation。需要后续 inbound dispatch 的 host 可使用 bypass entry interceptor 的 session call API；method body 的 nested call 仍可被 intercept。

`vs_interop_call_context_*` 提供借用参数和可变参数访问、`host_this`、程序代际及错误通道。函数与 callable 调用助手遵循会话的重入和生命周期规则。保留 callable 的接口为 `vs_interop_session_retain_callable`、`vs_interop_callable_destroy`、`vs_interop_callable_is_valid` 和 `vs_interop_callable_invoke`。仅限所有者线程调用与弱 `this` 捕获属于生命周期策略，不提供跨线程同步。

## 外部序列化上下文

serializer context 暴露已注册类型、mutation version 和借用字段。serializer 通过 `vs_interop_external_serialize_context_set_bytes` 复制输出字节；替换失败保留此前已接受的 payload。deserializer 读取字节和 encoded version，再将按名称指定的字段赋值加入临时事务。两者通过 error setter 与 callback result 报告失败。reference enumeration 是独立遍历，不延长借用值的生命周期。adapter 必须复制在 callback 后仍需保留的字节和字符串。

## Annotation 名称匹配策略

`vs_interop_session_register_annotation` 注册的 annotation 名称默认区分大小写。将 `name_flags` 设为 `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE`，可对该定义显式启用 ASCII 字母忽略大小写。参数名与 metadata 键仍精确匹配，反射保留源代码中的拼写。精确匹配与忽略大小写的注册发生重叠时，操作被拒绝且不改变目录。`struct_size` 必须设为 `sizeof(vs_interop_annotation_desc)`；不完整的描述符与未知标志会被拒绝。宿主应使用配套的当前 Interop 头文件和运行库重新构建。

## variant

参阅[Interop SDK — variant](../../User/zh-CN/index.md#exported-value-lifetime)。

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

明确的 variant 目标位置允许具体值隐式装箱。读取必须显式 cast<T> 或 try_cast<T>；variant_is<T> 精确判断存储类型，不做转换。try_cast 使用的 optional<T> 需要 core.vs。空盒不能强制提取；普通转换不匹配返回空 optional。复制失败、取消和限额仍然报错。
## 会话归属与回调快照

导出到 C 的可变模块容器即使当前为空，也绑定来源会话。会话保持开放时，共享的 C 别名和克隆会保留底层图；释放一个句柄不会销毁仍被其他句柄保留的对象。只有最后一个保留引用释放后，才可在所属会话的安全点回收。独立标量、宿主值和枚举值不因此增加会话约束。

由 C 创建、可承载模块相关图的可变容器，在首次传给会话时即绑定该会话，即使当前为空。worker 只能将独立值导出到拥有的 C 句柄；带 `variant` 或函数槽的容器即使为空，也不能这样导出，也不能将这类外部 C 存储转交给 worker 修改。

依赖模块的对象和容器图在复制后仍归属原会话。调用、字段、全局、钩子和序列化接口在访问元数据或修改状态前，以 `ENGINE_ERROR` 拒绝其他会话的图；独立标量和宿主值保持正常传递语义。借用的原生调用和反序列化上下文在同步进入另一会话时仍保留原会话归属，有效期仍限于原回调返回之前。

替换模块的初始化提交前，不能将依赖模块的脚本结果持久保存在拥有的 C 值句柄中。赋值和克隆会返回 `ENGINE_ERROR`；被拒绝的赋值保留目标拥有句柄的原值。回调的借用返回槽仍可将结果传回同一次脚本调用，包括 `vs_interop_intercept_context_proceed(context, out_return)`。临时结果仍在所属会话的安全点回收。

`vs_interop_value_reset` 对借用的回调参数和返回槽返回 `INVALID_ARGUMENT`。重置拥有句柄，或将其值替换为独立标量，会解除原会话归属，使该句柄可独立使用。

map/set visitor 可以重置或销毁源拥有句柄。枚举持有已注册的图，直到所有快照视图都退出作用域；借用条目视图仅在当前 visitor 调用期间有效，需要保留时应克隆。会话执行和配置的重入限制仍然适用。

应用完整配置会保留通过专用 setter 设置的数值、内存和标准库限额。外部内存注册仅在输出成功发布后接管 owner，任何失败都由调用方继续负责该 owner。执行异常会把有效结果输出初始化为失败，分配失败保留 `OUT_OF_MEMORY`。

值替换或重置提交后，旧值清理仍可能失败。已发布的值保持不变，清理错误留在原会话的错误通道，不会把已提交的发布追溯误报为失败。

C 函数值和保留的 callable 沿用 Core 闭包语义：克隆共享同一可变捕获环境，分别执行闭包创建表达式会得到独立环境，引用捕获可在源调用帧退出后继续存活。局部 static 存储归属执行中程序状态的声明位置，在同一状态的调用或闭包实例之间共享。需要在 C 调用间保留状态时，使用 `vs_interop_session_set_hot_reload_enabled`；普通的新执行会初始化并清理各自的状态。其私有槽不会作为普通全局变量反射导出。这些语言功能通过既有 C API 使用所选 VM/JIT。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/values-and-callbacks.md) · [fr-FR](../fr-FR/values-and-callbacks.md) · [ja-JP](../ja-JP/values-and-callbacks.md) · [ko-KR](../ko-KR/values-and-callbacks.md) · [zh-CN](values-and-callbacks.md) · [zh-TW](../zh-TW/values-and-callbacks.md)
