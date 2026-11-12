# C# 包、反射与本地化

## 包元数据

参阅[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#package-container-format)。

`GetMetadata()` 返回已拥有的元数据，`HasCompiledModule` 表示包是否仍可转移
编译模块，`GetCanonicalSignedBytes()` 返回签名覆盖的字节。目录通过
`VeroScriptRuntime.LoadCatalogDirectory` 或 `VeroScriptRuntime.MergeCatalogDirectory`
加载或合并。

## 包与信任边界

`VeroScriptPackage.Read` 用于解析包字节；`LoadPackage` 和 `ReloadPackage` 将包载入会话，`BuildPackageFile` 生成包文件；`VeroScriptPackageKind` 和 `VeroScriptPackageMetadata` 用于识别包种类和元数据。包可以作为传输和加载边界，但不是安全沙箱。宿主必须自行限制绑定、路径、输入、资源和执行时间。

## 反射与导出

`ExportReflectionJson` 导出脚本函数、类型、字段、方法、参数和包信息；`ExportBindingsJson` 导出当前宿主绑定。`VeroScriptResolvedMethod`、`VeroScriptHostObjectInfo` 和 `VeroScriptNativeType` 适合构建工具或验证接口，但反射数据不能替代运行时权限检查。

`EnumerateObjectReferences` 用于发现对象关系；遍历回调中的值遵守借用生命周期，不要保存到回调之外。

## 本地化

`VeroScriptRuntime` 提供当前语言、可用语言 JSON、目录加载和合并能力。应用应从 `GetAvailableLanguagesJson` 发现当前构建的目录，再选择规范 BCP 47 标签。目录合并应使用受信任发布物，并验证消息键及格式参数。 SDK 自身的参数或生命周期检查异常可能使用英文；语言选择适用于运行时诊断。

语言只影响诊断和格式化消息，不改变脚本语义。多线程应用应统一语言策略，避免并发切换共享运行时的语言状态。

## 能力与兼容性

使用 `VeroScriptRuntime.Capabilities` 检查当前构建是否提供 JIT、包、反射、热重载、序列化和执行治理等能力。执行治理包括协作式指令/时间限制、取消和标准库能力策略。使用相互兼容的托管程序集、原生 Interop 库与目录。

`VeroScriptRuntime.AbiVersion` 必须与适配器的 ABI `0x00000101`（0.1.1）完全一致。托管包、原生 Interop 库和运行时目录应来自匹配的发布版本。

## 模块所有权与签名

`LoadPackage` 或 `ReloadPackage` 成功后，编译模块会转移给会话。包元数据与规范签名字节仍可读取，但同一个包实例不能再次转移编译模块；需要另一次转移时，请重新解析原始字节。`Read` 校验容器格式，不验证发布者身份；加载不受信任的包前，应针对 `GetCanonicalSignedBytes()` 验证发布者签名。`BuildPackageFile` 返回包字节与 `VeroScriptRunResult`，选项控制应用/库种类、名称、版本、元数据以及是否移除调试信息。

参阅[C# 值与所有权](values.md#managed-call-lifetime)。

## 选择诊断语言

将选定的标签赋给 `VeroScriptRuntime.CurrentLanguage`，并通过 `VeroScriptRuntime.GetAvailableLanguagesJson()` 取得实际可用的标签后再提供选项。该属性改变进程级诊断语言，切换时应与其他会话协调。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](packages.md) · [zh-TW](../zh-TW/packages.md)
