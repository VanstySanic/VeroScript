# VeroScriptCurl

Curl 维护 `Private/HttpClient.cpp` 中的 HTTP 客户端、curl 依赖配置与原始声明。其提供者实现 Http 契约，不负责 Boost 传输或 WebSocket 服务。

公共上下文／操作所有权、目录组合、SDK 边界与构建选项统一维护在[Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/zh-CN/index.md)。

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [用户](../../User/zh-CN/index.md)

<a id="provider-registration"></a>

`curl` 提供者注册 `http-client`／`veroscript.http.client`／`0.1.1`，工厂载荷为 `Http::Backend::IClientService*`。工厂无状态，注册不启动 I/O，并拒绝未知配置。动态发现使用 `veroscript_extension_query`，静态注册使用 `veroscript_curl_query`／`Curl::QueryModule`。Curl 消费不透明、非拥有的套接字就绪和定时器钩子，不依赖 Boost 头或库；HTTPS 遵循 curl 自身的 TLS 配置。捆绑 curl 补丁、静态依赖部署及 Unix 系统库要求由本模块维护；根据实际构建验证缺失／不匹配服务、租约和迁移后的 SDK／包消费端。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
