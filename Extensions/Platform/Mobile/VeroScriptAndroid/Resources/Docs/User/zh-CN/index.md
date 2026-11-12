# Android — 实验性移动嵌入

实验性 AAR 面向 `arm64-v8a`、`x86_64`、API 26+，使用 VM 与 NDK `c++_shared`。向应用添加 AAR 并保持原生依赖兼容。包含 `assets/veroscript/stdlib`、C ABI Prefab 头和许可证；应用脚本放在 `src/main/assets/scripts`。`scripts/helper.vs` 映射为 `/veroscript/scripts/helper.vs`。在工作线程创建／执行／关闭，保留 engine 引用供 `requestCancel()` 使用。通过 `executeAsset` 读取入口，或调用 `execute(source, absoluteLabel)`。每次执行使用新状态，不支持并发执行。`close()` 等待并可重复调用，但不能在自身回调中同步关闭。UTF-8 字节数组保留 NUL 和补充字符，须检查 `outputTruncated`。

导入 SDK 的 [VeroScriptEngine](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Platform/Mobile/VeroScriptAndroid/src/main/java/com/vanstysanic/veroscript/android/VeroScriptEngine.java) 类型。以下方法体片段应在工作线程中执行，并假定已有 Android `context`。

```java
try (VeroScriptEngine engine = new VeroScriptEngine(context.getAssets(), "scripts"))
{
	VeroScriptEngine.Result result = engine.executeAsset("scripts/main.vs");
	// Inspect result.success, output, diagnostics and outputTruncated.
}
```

[实验性移动嵌入](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-CN/index.md)

## 移动本地化的显式配置

[移动本地化的显式配置](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-CN/index.md)

## 受管理字节存储限额

Android 通过完整 `VeroScriptEngine` 构造函数的最后两个参数 `maxBufferBytes` / `maxRetainedBytes` 设置。Java `long` 携带 uint64 位模式，普通预算使用正数字节数。 默认 16 MiB / 32 MiB，0 表示无限。

## 受控原生执行与静态 AOT

参阅[实验性移动嵌入 — 受控原生执行与静态 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/zh-CN/index.md#mobile-jit-selection)。

<a id="android-native-selection"></a>

Android 接受 `ExecutionBackend`、`jitBackendPath`、应用创建的 `AotPrograms` 目录和 `aotProgramId`；生成应用使用 `executePackage`，原生引导使用 `VeroScriptAndroid.h`。目录保留程序，生成入口代码须保持静态链接，ID 不能携带函数地址。

<!-- docs-navigation -->

---

[用户文档](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
