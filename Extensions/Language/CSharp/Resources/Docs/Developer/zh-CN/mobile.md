# CSharp — Mobile SDK

本页面向构建与发布 C# 移动 SDK 的维护者。桌面和移动共用 `VeroScriptCSharp.csproj` 与 NuGet 标识 `VeroScript.CSharp`。默认目标为 .NET 8/9；`VeroScriptIncludeMobile=true` 加入 .NET 10 Android/iOS 两个目标。Android 要求 API 26、arm64-v8a/x86_64；iOS 要求 15.0、arm64 真机和 arm64/x86_64 模拟器。包含移动的打包必须覆盖四个托管目标、匹配的工作负载与完整且已验证的原生根目录。

`prepare-native.py` 使用同一源码版本的 Android AAR 和静态 `VeroScriptIOS.xcframework` SDK，输入包含 Core/C++/Interop。C# 应用不需要 CPython SDK 或 Python 移动包构建器，也不使用 Java JNI 包装器或原生 JIT 模块。在新目录准备，校验后打包。

## 原生 SDK 准备

```sh
gradle --project-dir Extensions/Platform/Mobile/VeroScriptAndroid --no-daemon assembleRelease
python3 Resources/Tools/Packaging/package-veroscript-mobile.py ios \
  --cmake /path/to/cmake --out-root dist/mobile/ios
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py prepare \
  --android-aar Extensions/Platform/Mobile/VeroScriptAndroid/build/outputs/aar/VeroScriptAndroid-release.aar \
  --ios-sdk dist/mobile/ios --output dist/csharp-mobile/native
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py verify \
  dist/csharp-mobile/native
dotnet pack Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj \
  -c Release -p:VeroScriptIncludeMobile=true \
  -p:VeroScriptMobileNativeRoot=/absolute/path/to/native \
  -o /absolute/path/to/mobile-nuget
```

Gradle 若输出其他文件名，请使用实际 AAR 名称。Android 需要已配置的 SDK／NDK／Gradle 工具链；iOS 需要 macOS 与完整 Xcode。移动托管目标需要 .NET 10 Android/iOS 工作负载。准备解释器不叫 `python3` 时，用 `VeroScriptMobilePython` 指定。准备流程不下载 SDK，也不签名应用。

## 集成约定

准备阶段校验正本头文件、标准库、六语言目录、再分发声明与 SHA-256 文件清单。检查 Android 标识／API 和 16 KB ELF LOAD 对齐，保留两个 ABI 的 `libVeroScriptInterop.so` 与匹配的 `libc++_shared.so`；审核静态 iOS 归档对象的真机／模拟器架构和最低 OS。iOS SDK 需要 Foundation/libc++。拒绝已有输出目录。

NuGet 自动导入 `buildTransitive/VeroScript.CSharp.targets`。源码 `ProjectReference` 使用者必须导入该文件并提供 `VeroScriptMobileNativeRoot`。保留 Android 原生库／资产，以及 iOS 的 `__Internal`、`NativeReference`、`ForceLoad` 和生成的 `ReferenceNativeSymbol` 符号保留项。引用解析后检查实际 iOS 绑定程序集，包括传递引用；拒绝桌面／Android 绑定程序集。标准库与本地化目录属于应用资源；字节码缓存使用应用管理的可写目录。

NuGet 在 `native/` 下保留已准备根目录的相对布局，包括 `android/<abi>`、`ios`、`assets` 和 `licenses`，打包时不重复这些路径段。MSBuild 目标通过 `OutputType=Exe`，或 Android SDK 推断后的 `AndroidApplication=true` 识别应用。Android 应用的推断输出类型即使是 `Library`，仍须选入原生库与 MAUI 资产；普通类库使用者不触发这些部署项。

资源安装校验路径和摘要，要求大小写精确的 `stdlib/core.vs`，释放流并在重试时修复旧文件。失败后可能留下已验证文件，运行前应重试完成安装。异步资源安装完成后，在同一工作线程创建、配置、使用和释放会话。明确选择 VM；包不提供原生 JIT。

[应用集成](../../User/zh-CN/mobile.md)

## 发行验收

可移植 C# 测试将动态与 `IOS` 静态导入配置同原生导出清单核对，覆盖资源损坏、路径越界、取消、流释放、旧文件修复，以及真实标准库／目录安装。Python 测试覆盖 SDK 准备、MSBuild 资产选择、不兼容 RID／最低 OS、不完整目标矩阵及已解析 iOS 程序集的拒绝。这些属于宿主测试。

稳定移动发行前，在 Android 模拟器／真机及 iOS 模拟器／真机以 Release 构建并运行 MAUI 示例。覆盖 Android 16 KB 页环境、依赖与 APK ZIP 对齐、iOS 签名／链接、AOT／裁剪、取消、回调、资源导入及冷启动；记录工具链版本、应用包检查与真实结果。目标平台验收完成前，移动支持仍为实验性。
## 出版文档

移动包仅收录功能文件、运行资源和许可证，不携带 README、手册或示例；集成 User 文档通过独立 Resources 发行物获取。原生资源路径和设备验收要求保持不变。

## 桌面与移动端共用的 NuGet 源

上面的命令准备启用移动支持的 `VeroScript.CSharp` 包。在桌面或移动集合中发布 CSharp 业务时，还须准备一份规范包源，收齐已注册的六个 C# 包 ID 及各自匹配的 `.nupkg`、`.snupkg`。通过 `VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY` 选定包源。使用 `package-veroscript-mobile-release.py` 复用已生成的包时，将同一份启用移动支持的 `VeroScript.CSharp` 文件传给 `--csharp-package`。各目标复用相同字节。完整生产器输入与示例见[移动业务发布步骤](../../../../../../../Resources/Tools/Packaging/README.md#mobile-sdk-tooling)。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](mobile.md) · [zh-TW](../zh-TW/mobile.md)
