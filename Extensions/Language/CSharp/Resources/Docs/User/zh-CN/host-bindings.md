# C# 宿主绑定

## 拦截与变更

使用 `SetInstanceCallInterceptor` 安装实例拦截器，使用 `SetMutationHandler`
安装变更处理器。需要绕过拦截器时使用 `CallMethodBypassingInterceptor`。

宿主绑定通过显式的 Interop 类型描述把 CLR 函数、类型、成员和序列化器暴露给 VeroScript。绑定元数据同时服务于调用检查、反射和工具集成。

## 原生类型与函数

`VeroScriptNativeType` 描述基础类型、宿主类型、类型参数和递归结构；其静态原始类型属性可用于构造签名。注册函数时同时声明返回类型、参数类型和 `VeroScriptParameterFlags`。引用、只读引用和可变引用必须与实际回调契约一致。

## 类、泛型与拦截

会话 API 可以注册宿主类型、成员、泛型类型和类型别名，再通过 `Construct`、`CallMethod`、`GetField` 与 `SetField` 使用已注册成员。成员可区分实例、静态、属性和方法；CLR 的未注册成员不会因为反射存在而自动公开。

泛型宿主类型工厂使用 `VeroScriptGenericHostTypeContext` 的
`ArgumentCount`、`GetArgument`、`SetType`、`AddMember`、`SetProtocol` 和
`SetError`。拦截上下文提供 `Receiver`、`ArgumentCount`、`GetTarget`、
`GetArgument`、`Proceed` 和 `SetError`；其中借用值只在回调期间有效。

拦截器可以观察或拒绝特定调用。返回 `VeroScriptInterceptResult` 时应明确表示继续、替换还是拒绝；拦截器不能绕过访问策略，也不应把借用值保存到回调之外。

## 序列化与变更

通过注册序列化器以及 `SerializeObject`、`DeserializeObject` 在受控边界上转换对象。反序列化前验证类型、版本和数据来源，不要将不可信字节直接还原为具有权限的宿主对象。

`SetMutationHandler` 接收 `VeroScriptMutationKind` 等结构化变更信息。序列化上下文使用 `GetField`/`SetBytes`，反序列化上下文使用 `EncodedVersion`/`GetBytes`/`AddField`。变更回调应保持短小、避免重入同一会话，并在回调返回前完成对借用数据的读取；通知无法回滚已经完成的变更。`ApplyClassDefaults` 可在创建或加载对象后应用已注册的默认值。

## 异常、线程和所有权

托管异常不能穿过原生 ABI；SDK 会将回调失败报告为 `VeroScriptException` 或对应状态。会话操作遵守创建线程限制，借用上下文和值限于当前回调线程；独立拥有值的使用遵守其同步契约，保留调用对象默认限创建线程。任何由 API 返回的拥有值都要显式 `Dispose`。

注册入口除 `RegisterFunction` 外，还包括 `RegisterHostFunction`、`RegisterAnnotation`、`RegisterHostType`、`RegisterHostMember`、`RegisterHostProtocol`、`RegisterTypeAlias`、`RegisterGenericHostTypeTemplate` 和 `InstantiateGenericHostType`。`VeroScriptNativeParameter` 将类型与 `None`、`Ref` 或 `ConstRef` 组合；仅当回调要写回脚本存储时才使用 `Ref`。`VeroScriptHostTypeDefinition` 与 `VeroScriptHostMemberDefinition` 描述要公开的 enum/struct/class、构造函数、方法、属性、字段、常量和运算符。

## 注解名称策略

`RegisterAnnotation` 的 `VeroScriptAnnotationDefinition.CaseSensitive` 默认为
`true`。设为 `false` 可对该注解名称中的 ASCII 字母忽略大小写；参数名与
metadata 键仍精确匹配，反射保留源代码拼写。精确匹配与忽略大小写的注册重叠时，
操作失败且目录不变；`ExportBindingsJson()` 保留 `caseSensitive`。注册同时向编译、
反射和工具提供文档，处理器需另外安装。应配套部署当前原生 SDK，其注解描述符
已增加名称策略字段。

## 签名、嵌套调用与持久化

`VeroScriptNativeType` 区分 `Void`、`Bool`、`String`、`HostObject`、`Enum`、`Function` 等类型。容器与函数使用 `TypeArguments`；原生签名 `fn(int)->int` 对应源码类型 `function(int)->int`，这些标识符不翻译。

`VeroScriptCallContext` 回调内使用 `GetArgument` 读取只读参数，使用 `GetMutableArgument` 访问已声明的可变引用。将结果写入传入的借用返回值，主动失败使用 `context.SetError(...)`。`ArgumentCount`、`ProgramGeneration`、`GetHostThis<T>()` 提供调用信息。`InvokeFunction`、`InvokeCallable` 可在回调内调用脚本目标，不应改为重入会话操作；返回的拥有值要释放。`VeroScriptHostFunctionDefinition` 可补充参数名、文档、注解、类型参数与泛型宿主对象标志。泛型类型的克隆、相等、哈希和子类协议通过工厂安装，并由会话保留。

`VeroScriptInterceptContext` 支持 `Unhandled`、`Proceed`、替换结果或 `Failed`。`RegisterExternalSerializer` 注册外部持久化；序列化上下文提供 `TypeName`、`MutationVersion`、`FieldCount`、`GetField`、`SetBytes`，反序列化上下文提供 `EncodedVersion`、`GetBytes`、`AddField`、`SetError`。`EnumerateObjectReferences` 访问脚本和宿主引用，借用值在每次访问回调返回时失效；`RetainCallable` 创建拥有的函数或闭包引用。须验证字节的类型、版本与来源；这些 API 不认证包或原生库。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
