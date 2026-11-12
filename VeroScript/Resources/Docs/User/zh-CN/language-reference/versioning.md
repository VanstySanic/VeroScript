# 版本与兼容性


版本信息同时影响源代码、字节码缓存、应用包、库包和宿主绑定。部署时应让这些组件使用相互兼容的语言和运行时版本。

可以使用以下命令查看实际安装版本：

```powershell
veroscript --version
```

## 文件格式兼容性

产品、字节码、Interop ABI 和 WebAssembly ABI 统一使用初始版本 `0.1.1`。字节码加载器要求格式版本与魔数完全匹配。请从源码重建旧开发构建生成的 `.vsbc`、`.vsapp` 和 `.vslib`；不提供这些旧二进制文件的兼容性。

- `.vs` 是源文件，通常需要由对应版本的编译器重新检查；
- `.vsbc` 是字节码缓存，不应作为跨版本发布格式；
- `.vsapp` 是可运行应用包，运行时和包版本必须匹配；
- `.vslib` 是库包，公开声明、包版本和运行时必须保持兼容。

版本升级后，应重新执行 `--check`，重新构建包，并在使用 AOT 或宿主绑定时重新生成或验证本地产物。

当前长度及线性索引 API 使用 `int64`。即使 `--version` 显示相同产品版本，也须从源码重建此次变化之前生成的二进制包及字节码缓存。升级时使用匹配的标准库，并重新生成 AOT 产物。

## 语言能力边界

当前实现不提供脚本指针语义和独立的 C++ concepts 语法；可变参数宏的 `__VA_OPT__`、Lambda 引用捕获、引用返回和运行时反射已经属于公开能力。图形界面也不属于核心运行时。文档中的能力列表以当前实现和版本说明为准，不应把 C++ 中存在的语法自动推断为 VeroScript 能力。

## 宿主绑定兼容性

宿主绑定不仅依赖函数名称，还依赖参数类型、返回类型、容器元素类型、键值类型和宿主对象生命周期。修改这些边界时，应同时更新绑定目录、宿主代码和脚本调用方，并进行完整的检查和运行验证。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-reference/versioning.md) · [fr-FR](../../fr-FR/language-reference/versioning.md) · [ja-JP](../../ja-JP/language-reference/versioning.md) · [ko-KR](../../ko-KR/language-reference/versioning.md) · [zh-CN](versioning.md) · [zh-TW](../../zh-TW/language-reference/versioning.md)
