# VeroScriptBoost

Boost 维护 Asio/Beast、OpenSSL 集成和 `Private/Runtime`（`VeroScriptBoostRuntime`）。具体实现与第三方来源清单归本模块，前端 SDK 不依赖这些具体类型。

公共上下文／操作所有权、目录组合、SDK 边界与构建选项统一维护在[Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/zh-CN/index.md)。

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [用户](../../User/zh-CN/index.md)

<a id="provider-registration"></a>

`boost` 提供者注册 `network`（`veroscript.network`），并按启用的协议目标注册 `http-server`（`veroscript.http.server`）、`websocket-client`／`websocket-server`（`veroscript.websocket`）。这些契约版本为 `0.1.1`，工厂载荷依次是 `Network::Backend::IService*`、`Http::Backend::IServerService*`、`WebSocket::Backend::IService*`。工厂无状态，注册不启动 I/O，未知配置会被拒绝。动态发现使用 `veroscript_extension_query`，静态注册使用 `veroscript_boost_query`／`Boost::QueryModule`。只有 Boost 拥有 `VeroScriptBoostRuntime`，前端和 Curl 不得重复安装它。应以匹配的 foundation／Core 身份验证协议开关元数据、租约与迁移后的消费端。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
