# 有界字节管道

原生入口为 `VeroScriptNetworkStream.h`。
流不借用 VM、脚本参数或调用栈。数据保存在拥有的有界环形缓冲中，读写由互斥保护，不运行脚本回调。

## 字节管道

`TryCreateBytePipe(capacity, out, error, owner)` 返回共享的 reader/writer 端点。
reader 只读，writer 只写；复制共享句柄共用同一游标。每端对应一个逻辑消费者／生产者。
TryCreateBytePipe(context, capacity, out, error) 使用 context 的缓冲和总字节额度；
无 context 的独立管道由原生调用方自行管理总量。脚本及 HTTP 下载管道统一使用 context 记账。
`Read/Write` 的数量为受检 `int64`，原生缓冲容量受 `size_t` 和 `int64` 两层范围约束。

| 状态 | 含义 |
| --- | --- |
| Progress / 0 | 本次有进展；非空请求不会以 0 字节报告进展 |
| WouldBlock / 1 | 暂无数据或容量；没有消费字节，等待下次调度 |
| End / 2 | 生产者结束且已读完缓冲 |
| Failed / 3 | 失败，附错误码与可用诊断 |

零长度请求不探测 EOF。`Write` 可部分接受；`WriteAll` 接受整块或完全不接受。
超过整个容量的整块写入直接失败，不进入永远无法恢复的暂停。
`Finish` 结束生产；`Fail` 保留已入队数据，读完后报告错误。
reader 的 `Cancel` 丢弃排队字节并通知生产者；最后一个 reader 释放时也执行此动作。
最后一个 writer 未结束便释放时，reader 得到 Closed 错误。
Owner 持续到两端和底层存储全部释放。

## 脚本入口

| 入口 | 行为 |
| --- | --- |
| `Network::create_byte_pipe(context, capacity)` | 创建 1 字节至 16 MiB 的管道 |
| `pipe.reader()` / `pipe.writer()` | 取得共享端点 |
| `reader.read(count)` | 本次最多读 64 KiB，返回保留 NUL 的 string |
| `writer.write(bytes)` | 本次最多接受 64 KiB，返回实际数量 |
| `state/error_code/error` | 上一次读／写结果，空字符串自身不等于 EOF |
| `buffered/capacity` | 当前排队数量与容量，均为 int64 |
| `writer.finish()` / `writer.fail(message)` | 正常或失败地结束生产 |
| `reader.close()` | 取消流 |

WouldBlock 时交还宿主调度器，HostDriven 模式由拥有线程限量调用 `context.poll()`。

HTTP 接入见 [HTTP 流](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-CN/Reference/streaming.md)。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
