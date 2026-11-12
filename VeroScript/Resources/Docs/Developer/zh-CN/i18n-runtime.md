# 应用国际化运行时契约

核心负责值类型、原子配置根、不可变提供者快照及正常原生调用诊断。应用国际化不修改诊断语言。ICU、资源文件格式及 GUI 刷新由扩展负责。

## 公开 API

- [`VeroScriptI18n.h`](../../../../Public/VeroScriptI18n.h) 提供 `FI18nArgument`、`FI18nArguments`、`FI18nConfiguration`、`FI18nText`、`FI18nContext`、`FI18nSnapshot`、`FI18nVersion`。
- 使用 `CreateContext(config, services, out, error)` 创建上下文，以 `CaptureSnapshot()`、`TryLookup`、`TryResolve`、`TryFormat` 查询；`ToScriptValue` / `FromScriptValue` 校验核心包装对象身份。
- `FI18nServices::Publish` 原子发布两个不可变视图；`PublishResources` / `PublishFormatter` 保留另一侧的最新视图，`Acquire` 返回拥有数据的快照。资源或配置内容改变时使用新视图。同一视图重新发布时若省略所有者，会保留已有模块引用；带拥有控制块的空别名 shared_ptr 也是有效的保活引用。
- `ConfigureAndSetServices` 原子替换配置和服务引用，不会以事务方式修改其他上下文共用的服务本体。`Set*` 方法与其他字段的最新值合并。
- 资源解析器每次接收一个精确的规范语言，返回 `Found` / `NotFound` / `Failed`；找到空模板也是成功。显式回退链仅由核心处理。
- 格式化器不可变且允许并发调用 `SupportsFormat` 和 `Format`。请求保留原始类型化参数、请求语言、匹配语言、有效格式区域及类型化选项。`literal` / `vero.named` 保留给核心。
- `FI18nWork` 借用当前操作的检查回调与输出上限，零表示不限。耗时提供者工作须检查取消，并在分配输出前遵守预算；核心在回调前后检查，但不能强制中断阻塞的原生调用。
- 值与快照拥有其数据，不得保留回调上下文、VM、借用的请求视图或 `FI18nWork`。单个普通原生值的修改由调用者在外部同步；上下文和服务方法支持线程安全。自定义值及提供者须不可变并支持并发读取。
- 提供者/自定义值的模块所有者须活得比回调及虚析构函数更久；动态代码须明确提供所有者。避免上下文/服务 → 提供者 → 上下文/服务的强引用环，使用弱引用。
- 同一上下文的 `Try*` 递归按线程拒绝。提供者内仍可读取或更新配置；即使回调替换或重置调用者的值变量，当前操作也固定持有起始快照和文本值。借用的 string_view 输入须在返回前有效，错误输出不得拥有输入字符串；不得重入正在执行的 VM。
- 版本相等性包含上下文身份、配置修订、资源修订及格式化器修订。修订耗尽时报错，不回绕，也不具持久化或跨进程含义。
- 结构化失败使用 `FI18nError::Status`、自有 `Diagnostic` 及域/键/语言/名称。解析器的 `NotFound` 不应携带错误；失败须有非 Ok 状态和诊断，成功回调不能返回错误。
- `FNativeCallContext::DiagnosticOutput` 是自有 `FScriptError` 的借用输出；VM/JIT 桥接保留 ID/参数，并设置当前调用位置。`I18nServices` 是当前调用借用的服务访问；脚本创建的上下文持有拥有所有权的服务选择，不持有 VM。
- 配置 `VeroScriptVm::SetI18nServices`，或 C++ Engine/Session 的 `FEngineConfig::I18nServices` / `I18nLimits`。工作线程继承服务选择及预算，使用自身执行控制；宿主配置变化后，已有上下文保留原服务引用。

## 编译与执行

解析器和编译器使用公共 `StandardLibrary::GetBindingCatalog`，规范内存项位于 i18n 项之前。既有核心相对操作码保留内部名称 `CallMemory`，改为索引公共目录，不增加新操作码或 `FValue` 分支。`Memory::UsesRuntimeMemory` 排除 i18n 项。字节码/包加载检查相同目录；存活的上下文、提供者和自定义载荷属于运行时值，不会序列化进库。

AsmJit 在进入前拒绝不支持的核心相对调用，沿用 VM 回退且不重放回调。标准值使用普通宿主克隆/相等协议，一般值相等性不要求键哈希；Map/Set 键相等性仍要求有效的哈希协议。

普通具名宿主方法支持按参数类型重载。脚本 API 保留 `text::message_key(domain,key)` / `text::message(domain,key,args)` 和 `format` / `format_with`；类型化格式化不会隐式调用仅接受字符串的位置参数 `std::format`。

## Validation

`VeroScriptI18nTests` 覆盖标量精度、自有/COW 数据、提供者、版本、并发发布、重入、取消/输出限制、脚本泛型/容器/Attribute、工作线程继承、字节码/库往返、VM/Mixed/JIT 及诊断桥接。JIT 部分在提供兼容后端时运行；`VeroScriptI18nExample` 链接 Core 和匹配的 Bytecode VM SDK。

[I18n SDK 示例](../../../Examples/I18n/README.md) · [用户 API](../../User/zh-CN/standard-library/i18n.md).

<!-- docs-navigation -->

---

[开发文档](index.md) · [en-US](../en-US/i18n-runtime.md) · [fr-FR](../fr-FR/i18n-runtime.md) · [ja-JP](../ja-JP/i18n-runtime.md) · [ko-KR](../ko-KR/i18n-runtime.md) · [zh-CN](i18n-runtime.md) · [zh-TW](../zh-TW/i18n-runtime.md)
