# JSON 诊断与本地化

错误码及 FError::Id()/error.message_id() 用于程序判断，message 是原始详细信息，不作为稳定协议。
本模块拥有 Resources/Localization/<locale>/json.json，支持 en-US、fr-FR、ja-JP、ko-KR、zh-CN、zh-TW。
类别、路径说明和绑定诊断有翻译；底层或用户转换器提供的详细信息保留原文。

宿主收到脚本异常时，FScriptError.MessageId 和 MessageArgs 可交给现有诊断本地化服务。
普通 JSON 错误有两个参数：原始详细信息、实例 JSON Pointer；根路径为空字符串。
编译声明错误 json.mapping.declaration 有一个参数：详细信息。
未知语言或缺失资源时沿用宿主的英文 fallback，不改变错误类别和路径。
本地化元数据无法分配时保留原始失败，避免显示错误覆盖首个错误。

非抛错 try 接口返回的 json::error 仍可直接读取 message_id、message 和 instance_pointer；
宿主可以用同样的参数规则生成显示文本。独立 C++ 数据 SDK 不链接 Core，也不维护全局语言状态。

已安装的 JSON 资源位于 `share/VeroScriptJson/Resources/Localization`，应挂载到宿主资源解析器；与其他模块的 Catalog 合并时仍须保留 `json.json`。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
