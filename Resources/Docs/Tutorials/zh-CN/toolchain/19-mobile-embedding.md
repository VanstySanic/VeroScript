# 19：在移动应用中执行脚本

在 Android/iOS 应用中使用匹配的移动 SDK。应用需要已有的目标 CPython 嵌入环境或 .NET 10 MAUI；桌面包不提供移动原生二进制。这些接入仍处于实验阶段。

[Python](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/mobile.md) · [CSharp](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/mobile.md)

1. 按所选绑定的指南部署原生库并安装标准库、本地化资源。必须在创建 Session 前完成资源安装。

2. 在同一工作线程中加载目录、选择语言、创建并配置 VM Session，加载 `import "core.vs"; void main() { print(42); }`，检查加载和执行结果，收集 print 回调并关闭 Session。再将收集到的 `42` 返回 UI 线程。

3. 选用非英语语言，以 `void main() { int x = ; }` 重试，显示诊断并检查界面仍可响应。恢复正确脚本，配置执行限制，并在各支持目标上验证最终签名应用。

[en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](../../ja-JP/toolchain/19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)
