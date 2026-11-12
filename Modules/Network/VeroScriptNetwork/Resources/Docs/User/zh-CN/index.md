# Network

安装所用 API 的前端包及所选原生后端。`vs-boost` 提供 `network` 和实际启用的 HTTP 服务端、WebSocket 服务；`vs-curl` 提供 `http-client`。仅安装前端不会提供通信实现。服务缺失或契约不匹配会明确报错。

<a id="backend-selection"></a>

脚本用 `Network::create_context_with_backend("boost", false)` 显式选择执行基础。HTTP 客户端／服务端、SSE 和 WebSocket 选项用 `set_backend` 覆盖环境选择。空请求继承 `services.<service>.backend`；未配置时，`auto` 按后端 ID 顺序尝试已启用的提供者。显式 `auto` 覆盖配置默认值，`none` 或配置中明确保存的空值禁用服务。缺少必需服务会明确失败，运行错误不会在另一后端重放操作。`boost`、`curl` 是提供者 ID，不是固定默认值。C++ 消费者传入类型化服务实例；这些脚本选择值不替代 C++ 选项的实例匹配契约。

`vs-network` · `VeroScript::Network`

- [完成回调与程序生命周期](Reference/completion.md)
- [诊断与本地化](Reference/diagnostics.md)
- [文件与原子输出](Reference/files.md)
- [网络参考](Reference/README.md)
- [有界字节流](Reference/streaming.md)
- [TLS 配置](Reference/tls.md)

[Boost](../../../../../Backend/VeroScriptBoost/Resources/Docs/User/zh-CN/index.md) · [Curl](../../../../../Backend/VeroScriptCurl/Resources/Docs/User/zh-CN/index.md)

<!-- docs-navigation -->

---

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
