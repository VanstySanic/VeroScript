# iOS — 开发文档

iOS 维护 Objective-C/Objective-C++ 适配与静态 XCFramework 集成。UI 分派和应用生命周期归适配层／宿主，每个 slice 保持真机／模拟器身份。

公共嵌入构建、生命周期、包输出与本地化职责统一维护在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-CN/index.md)。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py ios --out-root dist/mobile/ios
```

iOS 要求 macOS/Xcode、三个 slice，逐归档对象检查 `LC_BUILD_VERSION`／CPU／最低 iOS，拒绝 macOS 或混合平台归档。真实 Objective-C/Swift 应用链接、签名、后台／恢复、取消和资源测量仍是发布条件。

[用户文档](../../User/zh-CN/index.md)


## 移动资源与本地化

公共移动目录清单、部署路径和显式加载流程统一维护在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-CN/index.md#mobile-localization)。

翻译缓冲区所有权、错误与导出维护契约见 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-CN/abi-and-lifetime.md#localization-abi)。

<a id="ios-acceptance-evidence"></a>

按准确源码／产物身份、Xcode／SDK 版本、最低部署系统、运行系统及真机／模拟器架构记录证据。使用 CMAKE_SYSTEM_NAME=iOS 与匹配的 iphoneos／iphonesimulator sysroot，分别构建真机 arm64、模拟器 arm64／x86_64；嵌入构建使用静态 Interop。核对合并归档与 XCFramework，再由 Objective-C++、Swift、C ABI 和静态 AOT 应用消费。实际应用应验证导入、NSError／工厂桥接、Bundle 资源、可写缓存复用、权限拒绝、取消、小栈解析／恢复、ARC 及后台／恢复。编译真机 slice 不等于真机执行，模拟器签名和截图不等于设备签名、原生 Metal 或性能验收；最低 iOS 15.0 编译也不等于在 iOS 15 运行验证。实际回执与源码差异放入临时证据，不在维护手册中保留带日期的状态段落；已有模拟器回执不能自动批准后续构建。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
