# Python 包、反射与本地化

## 包类型与构建结果

参阅[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#package-container-format)。

`PackageKind.APPLICATION` 创建可运行的应用包，`PackageKind.LIBRARY` 创建库包。
`Session.build_package_file(...)` 返回 `(data, RunResult)`；构造 `Package(data)`
前应先检查 `result.success` 和诊断。`Package.from_file(path)` 解析文件，
`package_kind` 表示包类型。解析只校验容器结构，不验证发布者真实性；加载不可信
字节前，应先对 `canonical_signed_bytes` 验证签名。

解析后的 `Package` 是不可变快照。读取其他字节时应创建新对象；再次调用
`__init__` 会抛出 `veroscript.Error`。

## 签名字节与目录

公开属性 `canonical_signed_bytes` 返回用于签名校验的规范字节序列。
`merge_catalog_directory` 会添加应用目录，而不会替换 runtime 目录。

## 包操作

`Package.from_file()` 读取已生成的包；通过 `package_kind` 区分包类型。会话可以使用 `load_package` 和 `reload_package` 加载包，也可以用 `build_package_file` 生成包文件。发布包时应把导入关系、标准库依赖和目标架构作为整体验证。

包边界是加载边界，不是安全沙箱。不要把不可信包当作隔离代码执行；宿主仍需控制可用的绑定、文件路径、输入和资源上限。

## 反射与绑定导出

`reflection()` 返回脚本函数、类型、方法、字段、参数和包元数据；`bindings()` 返回当前宿主绑定视图。反射数据适合生成工具提示、验证接口和构建编辑器元数据，但不能替代运行时的权限检查。

## 本地化

使用 `set_language()` 选择目录，使用 `get_language()` 读取当前语言，使用 `available_languages()` 发现可用目录。`merge_catalog_directory()` 可将外部目录合并到运行时目录；目录内容应来自受信任的发布物，并在合并后验证消息键和格式参数。

语言选择影响诊断和格式化消息，不改变脚本语义。多线程宿主应在应用层统一语言策略，避免同一会话并发切换语言。

## 能力与性能

运行前检查包、反射、JIT、热重载、序列化和执行治理等能力是否由当前构建提供。执行治理包括协作式指令/时间限制、取消和标准库能力策略。JIT 统计中的 `direct_native_calls` 表示在同一代 JIT 代码中绕过虚拟机值桥、直接到达另一个原生入口的调用次数；它是诊断数据，不是固定性能提升的保证。对冷启动、热调用、包加载和缓存命中分别测量；不要把启用 JIT 或字节码缓存误认为所有场景都会更快。Python 层的对象转换、GIL 和回调成本也应计入基准。

## 返回的对象引用

`enumerate_object_references(instance)` 接受脚本对象的 `Value`，须在会话空闲时由创建线程调用。返回 `ExternalReference` 元组，每项的 `kind` 为 `ExternalReferenceKind.SCRIPT_OBJECT` 或 `ExternalReferenceKind.HOST_OBJECT`，`value` 拥有复制后的 `Value` 包装器。这里复制的是引用，保持对象身份，不会深拷贝对象图。元组及其值可以在枚举结束后持有，不是回调借用视图；普通值、会话、释放和线程规则仍然适用。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](packages.md) · [zh-TW](../zh-TW/packages.md)
