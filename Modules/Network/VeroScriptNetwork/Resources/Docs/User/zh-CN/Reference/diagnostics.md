# 网络诊断与本地化

Network、Http、WebSocket 分别拥有 Resources/Localization/<locale>/network.json、http.json、websocket.json，
支持 en-US、fr-FR、ja-JP、ko-KR、zh-CN、zh-TW。

共享的 Network::FError 提供稳定 Id()，例如 network.cancelled、network.timed_out、network.connect。
成功时 ID 为空，未知枚举值返回 network.unknown；它不分配内存。
原生协议模块共用这个传输错误域。各脚本 operation 的 message_id()、error_code()、error()
分别返回 ID、数值代码和原始详细信息，成功的 ID 为空。
尚未产生错误的未完成任务也返回空 ID，完成与成功仍须检查任务状态。
可读/可写流、SSE reader/client、multipart reader、HTTP attempt/snapshot 和 WebSocket reconnect attempt/snapshot
同样提供 message_id()，其参数规则与对应 error() 详细信息一致。
不要按本地化文本或操作系统/后端返回的详细文本控制流程。

资源格式的参数 0 是原始详细信息。类别有本地化，后端详细信息保留原文。
同步绑定失败另使用所属模块的 network.binding、http.binding 或 websocket.binding，
通过 FScriptError.MessageId/MessageArgs 传给宿主诊断本地化服务；已有诊断不被覆盖。
显示元数据的分配失败保留原始错误。独立数据 SDK 不依赖 Core 或全局语言设置。

将各已安装模块的 `share/VeroScript<Module>/Resources/Localization` 挂载到宿主资源解析器。Http 和 WebSocket 还需要其 Network 依赖中的 `network.json`，不会重复附带该文件。组合宿主可分别挂载模块目录，也可部署保留全部所需组件文件的合并 Catalog 根目录。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
