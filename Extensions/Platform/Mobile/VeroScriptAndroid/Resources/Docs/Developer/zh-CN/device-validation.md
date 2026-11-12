# Android AAR 目标验收

## 前提与构建

此独立夹具通过 Java／JNI 及 C11／C++17 Prefab 使用已构建的基础 AAR，不另编译第二份 Core。默认 release APK 启用 R8，仅为此测试应用使用 debug 签名密钥。使用 Python 3.11+ 及 [Android 构建指南](index.md)固定的 JDK、Gradle、SDK、NDK、CMake 版本；先在新的输出目录构建匹配 SDK 产物。`--with-asmjit` 同时准备可选 JIT AAR。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --with-asmjit --out-root dist/mobile/android-acceptance --gradle <Gradle-8.11.1-executable>
```

## 在明确目标上运行

启动模拟器或连接已开启调试的设备，从 `adb devices` 选取准确序列号。提供匹配的基础 AAR；示例明确请求 x86_64 安装 ABI。

```text
python Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device/run.py --aar <absolute-base.aar> --sdk <Android-Sdk> --gradle <Gradle-8.11.1-executable> --java-home <JDK-17> --serial <adb-serial> --abi x86_64 --out <report-directory>
```

添加 `--jit-aar <absolute-jit.aar>` 验证可选后端与受治理原生执行计数；ARM64 使用 `--abi arm64-v8a`。`--page-size 4096`／`--page-size 16384` 核对 Bionic `sysconf` 报告的原生页大小；`--debug` 选择未缩减变体。覆盖 API 26 及后续版本、两种页大小，每个配置使用独立输出目录。验收 JNI 时另在测试模拟器启用 CheckJNI。

## 结果与验收范围

运行器仅卸载／替换 [构建配置](../../../../Resources/Tests/Device/build.gradle.kts) 中声明的测试应用 及其数据，不关闭所选设备／模拟器。保留 APK、命令日志、`commands.json` 和 `device-smoke.json`；校验最终 APK 的 16 KB ZIP 对齐、各用例结果及 `INSTRUMENTATION_CODE: -1`，不能仅凭 adb 进程退出码判定成功。每次尝试先将 `acceptancePassed` 置为 false；包括指定 ABI、页大小在内的全部检查通过后才置为 true。

用例覆盖资产导入／Unicode 名称、UTF-8／NUL 输出、float32 舍入、内存／执行／输出预算、缓存、能力拒绝、恢复、显式后端、取消、并发拒绝、关闭、本地化和 JNI AOT 目录生命周期。目录用例不运行生成的 AOT 业务代码。模拟器原生转译下执行 ARM64 库不等于物理 ARM64 硬件验收；报告须保留实际设备／模拟器身份和产物哈希。Python、C#、图形及最终应用仍需各自验收。

以下纯 Python 回归模拟原生命令，只校验运行器逻辑，不验收 Android 运行时。夹具存在、宿主检查或仅构建成功都不等于目标验收完成。

```text
python -m unittest discover -s Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device -p test_run.py
```

<!-- docs-navigation -->

---

[Android](index.md) · [VeroScript](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/device-validation.md) · [fr-FR](../fr-FR/device-validation.md) · [ja-JP](../ja-JP/device-validation.md) · [ko-KR](../ko-KR/device-validation.md) · [zh-CN](device-validation.md) · [zh-TW](../zh-TW/device-validation.md)
