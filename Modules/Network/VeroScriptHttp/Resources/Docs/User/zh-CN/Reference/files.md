# HTTP 文件上传与断点下载

文件执行器的范围、原子输出、取消及物理清理约束见 [Network 文件流](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/Reference/files.md)。

## 文件上传

原生 `Http::MakeFileUploadFactory(shared_ptr<FileExecutor>, readOptions)` 为每次尝试或重定向回放
创建新的异步文件来源。正数 Length 可提供已知上传长度；未指定 Length 从指定 Offset 读到文件快照结尾。
Length=0 仍验证打开并读到 EOF，不能因声明空正文而忽略不存在的文件。
上传工厂自身不执行同步 stat/read，也不在工厂调用时重启请求总期限；取消请求会取消当前文件来源。
重试和重放须按 [客户端策略](client-policy.md) 显式授权。

脚本 `request.set_upload_file(files, readOptions)` 替换旧正文，清除冲突的正文来源及长度/传输编码头。
multipart 的 `add_field_path(name, files, options)` 与
`add_file_path(name, filename, contentType, files, options)` 保留重复字段和二进制数据，
使用相同的可重放文件来源。其他 multipart 规则见 [multipart](multipart.md)。

## 下载与续传

原生调用 `Http::DownloadFile(context, client, files, request, options)`；
脚本调用 `client.download_file(files, request, options)`。
client、files 必须属于同一 context。只接受无上传正文的 GET；Range/If-Range 由下载层管理，
调用者不能同时手填这些头或 PrefixPath/PrefixLength。

下载请求明确选择 identity 表示，不启用内容解压。下载以有界管道接入文件执行器，
不在内存聚合完整正文。请求自己的 MaxBodyBytes/MaxTotalBodyBytes 仍生效；
大文件需按实际需求同时调整请求流量限制与文件 MaxBytes。
Request.Operation 的期限从提交起覆盖排队、网络、前缀复制与文件提交，忽略 File.Operation 的独立期限。

| 情况 | 验证与处理 |
| --- | --- |
| 全量下载 | 必须为 200、无 Content-Range，验证实际正文完整性和已声明长度后才提交 |
| 续传 | 提供已有 PartialPath、Offset，以及 strong ETag 或合法 IMF-fixdate Last-Modified；本地文件长度必须等于 Offset |
| 206 | 必须返回相同 validator；Content-Range 起点等于 Offset、末尾到表示结尾，Content-Length 若存在必须一致 |
| 200 响应续传 | 明确失败，不把全量响应错误追加到前缀 |
| 416 | 只有 Content-Range 确认总长度等于 Offset 且 validator 相符时，才把已有完整前缀提交为目标；排空错误正文但不追加 |
| 其他状态/压缩/不匹配范围 | 失败，默认清除输出临时文件，保留原目标和输入前缀 |

下载任务的 Operation 提供终态；ResponseHead 是已发布的不可变头快照。
成功结果包含 Response 与 File，Response.Body 为空，BodyBytes 是网络响应正文数量。
Progress 包含 File 进度、ReceivedBytes、可选 RepresentationBytes、Resumed、AlreadyComplete。
续传的 File.Bytes 包括复制的前缀；ReceivedBytes 只计本次收到的正文，416 时包括被丢弃的错误正文。
不能用网络接收完毕代替文件已提交。

## 脚本入口摘要

```vs
Network::file_executor files = Network::create_file_executor(context);
Network::file_read_options input = Network::make_file_read_options("payload.bin", absolute_base);
input.set_range(0, -1);
http::request request = http::create_request("POST", url);
request.set_upload_file(files, input);

http::download_options output = http::make_download_options("received.bin", absolute_base);
http::download_operation task = client.download_file(files, get_request, output);
http::operation operation = task.operation();
```

`create_file_executor_with_limits(context, workers, maxPending, blockBytes, pollMs)` 可配置共享池。
read/write options 提供 set_max_bytes、set_timeout；write options 还提供 set_expected_length、
set_prefix、set_commit_options。download options 提供 set_file_options、set_buffer_bytes、
set_resume(partialPath, offset, validator, useLastModified)、clear_resume。
任务/进度访问器均为显式调用；HostDriven 模式仍需宿主持续 PollIo，不能直接阻塞等待未完成任务。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](../../fr-FR/Reference/files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](files.md) · [zh-TW](../../zh-TW/Reference/files.md)
