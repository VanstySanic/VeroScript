# Android — 开发文档

Android 维护 Java/JNI 与 AAR/Prefab 适配。UI 分派和应用生命周期归适配层／宿主。JNI 使用标准 UTF-8 字节数组以及显式目录加载／语言选择，翻译异常不能使用 JNI modified UTF-8。桌面 JNI smoke 使用真实 JVM 与测试 AssetManager，不能代替 Android 设备验收。

公共嵌入构建、生命周期、包输出与本地化职责统一维护在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-CN/index.md)。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --out-root dist/mobile/android
```

Android 要求 JDK 17、Gradle 8.11.1、AGP 8.9.2、SDK 35、NDK 28.2.13676358 和 CMake 3.31.6。检查两 ABI 的 JNI/Interop/libc++、Android/API 身份及 16 KB ELF LOAD 对齐；真机／模拟器、R8、最终 APK ZIP 对齐仍须验证。

[用户文档](../../User/zh-CN/index.md)


## 移动资源与本地化

公共移动目录清单、部署路径和显式加载流程统一维护在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-CN/index.md#mobile-localization)。

翻译缓冲区所有权、错误与导出维护契约见 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-CN/abi-and-lifetime.md#localization-abi)。

[Android AAR 目标验收](device-validation.md)

基础 Android AAR 在 embedded-only 构建中也显式启用 Application（`VEROSCRIPT_BUILD_APPLICATION=ON`）。除 LICENSE 与 NDK 声明外，保留 `assets/veroscript/licenses/MINIZ-LICENSE.txt`、`JSON-LICENSE.txt` 和 `SHA256-LICENSE.txt`；移动校验器会要求这些文件。应用容器支持不代表启用包安装客户端或桌面工具。

<a id="jni-host-fixture"></a>

可选桌面 JNI 夹具使用 `VEROSCRIPT_TEST_ANDROID_JNI_HOST=ON`、`BUILD_TESTING=ON` 及 JAVA_HOME 指定的实际 JDK。构建 VeroScriptAndroidHostSmoke 后运行 CTest 条目 VeroScriptAndroidJniHostSmoke；它通过真实 JNI 适配和测试 AssetManager，在 `-Xcheck:jni` 下验证 UTF-8／NUL、导入、异常、取消与释放，不构成 Android ABI／API、APK 或设备验收。目标验收由 [设备指南](device-validation.md)维护。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
