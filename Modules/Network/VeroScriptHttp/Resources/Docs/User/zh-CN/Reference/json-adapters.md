# HTTP / SSE JSON 适配

这些原生 C++ 组合接口按需依赖 Json。Network、Http、WebSocket 的基本接口继续独立构建与消费。
脚本可以组合已有 JSON 与网络入口；这里的 C++ 便利函数没有另行声明为脚本方法。

## HTTP 与 SSE

```cmake
find_package(VeroScriptHttp 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::HttpJson)
```

包含 VeroScriptHttpJson.h：

- TrySetJsonBody(request, value, error, write_options, work) 先完整编码，再原子替换正文和 Content-Type。
  成功时移除旧上传源、重放工厂、multipart 和手工 framing 字段；失败保留原请求。
- TryParseJsonBody(response, out, error, parse_options, work, require_content_type) 要求完整的缓冲响应。
  默认接受 application/json 或带 +json 后缀的媒体类型，允许大小写和参数；不以 HTTP 状态决定解析成功。
- FJsonBodyError.Http 表示正文/媒体类型条件错误，Json 表示 JSON 错误。
  解析失败保留 out、HTTP 状态、头和正文。流式头快照不能当作完整缓冲正文解析。
- TryParseJsonEvent(event, out, error, parse_options, work) 只解析 SSE data；事件类型和 ID 留在原事件中。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
