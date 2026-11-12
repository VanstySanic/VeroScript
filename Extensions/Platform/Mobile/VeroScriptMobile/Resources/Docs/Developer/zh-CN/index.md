# Mobile — 开发文档

UI 分派和应用生命周期由适配层／宿主负责，Core 保持平台中立。embedded-only 排除桌面 CLI/tooling 和语言导出器。静态 Interop 属于源码依赖图或合并的 iOS 归档，桌面 Interop 包仍使用共享库。平台身份须区分 Android/Linux、iOS/macOS、真机／模拟器。平台允许标志不能证明后端已构建／选择或设备已验收。验证资源导入、缓存失效／损坏、输入输出上限、取消及析构。

```text
cmake -S . -B build/embedded -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF -DBUILD_TESTING=ON
cmake --build build/embedded --config Release --target VeroScriptMobileTests
```

`package-veroscript-mobile.py` 生成独立实验产物，拒绝已有输出，不出版到桌面发行集合。

工具链版本与平台专属验收条件分别维护在 [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/zh-CN/index.md) 和 [iOS](../../../../../VeroScriptIOS/Resources/Docs/Developer/zh-CN/index.md)。

[用户文档](../../User/zh-CN/index.md)


## 移动资源与本地化

<a id="mobile-localization"></a>

`mobile` 本地化配置集成 Core、C++ Session、Interop 和 `Extensions/Platform/Mobile/VeroScriptMobile/Resources/Localization`，不包含桌面 Tooling/UI。Android Gradle 将同一目录复制到 AAR，打包器验证完整清单和精确字节；iOS 复制到 `assets/localization`。应用在选语言前显式解包、加载，不得引入移动环境自动探索。移动包仅收录功能文件、运行资源和许可证，不携带 README、手册或示例；集成 User 文档通过独立 Resources 发行物获取。原生资源路径和设备验收要求保持不变。

Mobile `TranslateMessage` 经公开 C ABI 委托。缓冲区所有权与错误行为见 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-CN/abi-and-lifetime.md)，Android JNI 编码要求归 [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/zh-CN/index.md) 维护。

Core 与目标 AOT 实现指纹纳入 `Shared/Private/VeroScriptFloatParsing.h`、`VeroScriptFloatFormatting.h` 和 `VeroScriptJsonString.h`。Python 源码发行保留全部必需的 `Shared/Private` 输入，包括供 Application／包运行时使用的 `VeroScriptDarwinPaths.h`。Android Python 源码摘要还覆盖 Application 及其 C 源码。这些是私有构建输入，不是公开 SDK 头文件或设备验收证据。

<a id="mobile-execution-worker"></a>

`Private/VeroScriptMobile.cpp::WithCompilerStack` 在 Android／Apple 检查调用栈大小；小于 8 MiB 时创建 8 MiB pthread 并同步 join，否则在调用线程执行。工作线程拥有整个 FSession 的创建、执行和销毁，不只是解析阶段；捕获异常后在 join 完成时重新抛出。保持取消互斥锁保护下的活动 Session 指针在销毁前清空，并在所有退出路径清理外层执行状态。直接使用 FSession 不经过此辅助路径，栈由宿主提供。该预算不提高语言嵌套、调用深度或执行限制。维护小栈合法输入、解析上限诊断、异常传播、取消和复用回归；Android 夹具覆盖嵌套后恢复，Apple 回归显式固定 512 KiB 调用栈。

<a id="mobile-message-coverage"></a>

Mobile 目录应覆盖 C++ Session／runner、Android AOT／Java 参数校验及 iOS 工厂／后端错误。C++ 与 Java 校验使用既有翻译入口，JNI／NSError 在边界翻译适配异常。std::system_error 仅翻译项目上下文，保留错误码和 category；操作系统后缀遵循系统库语言。资源失败继续使用既有回退。六语言须保持消息键及占位符一致。

纯 Core／C++／C 源码宿主可用 `VEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF` 关闭适配器。`VEROSCRIPT_INTEROP_STATIC=ON` 是源码／静态移动构建选项，不改变桌面共享 Interop 发行方式。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
