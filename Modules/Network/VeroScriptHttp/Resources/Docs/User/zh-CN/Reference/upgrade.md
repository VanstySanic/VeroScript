# HTTP 与 WebSocket 共用端口

Http 与 WebSocket 都只依赖 Network；升级桥接使用 Network 的不透明来源和私有、唯一的传输 lease，
不要求独立 WebSocket 安装 Http，也不在公共头暴露 Boost、socket 或 TLS 后端类型。

## 两个阶段

1. `exchange.PrepareUpgrade(error)` 创建 `shared_ptr<Network::FUpgradeSource>`。来源独立保留原 exchange，
   不拆走连接、不发送 101。`Info()` 提供拥有的请求头元数据、协议、后端身份、合同版本和请求代次。
2. `WebSocket::FConnection::AcceptUpgrade(context, source, options)` 返回普通网络 operation，
   完成握手后结果为 `shared_ptr<WebSocket::FConnection>`，与普通 Connect/Accept 得到的连接使用同一接口。

来源只能属于同一个 context、同一个兼容原生后端，传输最多移交一次。
操作总期限从 AcceptUpgrade 提交起覆盖前置检查、排队及握手；不会在移交时重启期限。
PrepareUpgrade 对原 HTTP exchange 的回复期限没有延期效果；来源长期不用仍可能因原 HTTP 期限失效。

## 检查与失败

PrepareUpgrade 要求 HTTP/1.1 且没有请求正文；不接受 Transfer-Encoding 或非零 Content-Length。
需要共享持有的原始 exchange；无法取得共享所有权的复制对象返回 State。
WebSocket 在转移前检查选项、context、后端合同、请求头限额、GET/HTTP1.1、Host、
Upgrade/Connection、版本 13、16 字节 key 和子协议协商。头限额为 64 KiB。
流式 HTTP 的请求体必须已经完整结束，正在输出响应、已经回复、旧代次或已关闭的 exchange 不能升级。

前置检查失败或尚未转移时取消，来源保持未消费，Http 可按正常规则回复。
开始转移后失败会关闭传输；不能把已消费的来源恢复为 HTTP，也不能补发第二份 HTTP 响应。
取消可能与传输移交、101 写入竞争，取消终态本身不承诺回滚已经发生的协议动作。
使用 `source.IsConsumed()`（脚本 `consumed()`）区分传输是否已经交出，不能仅依据 operation 错误作出回滚假设。

## 传输与所有权

- 传输由同一 I/O 执行器串行移交，HTTP 原资源槽跟随唯一所有者转给 WebSocket；
  不额外占用第二个 MaxConnections 槽，也不遗漏原连接的关闭登记。
- HTTP 解析器已预读到的帧字节原样交给新协议，首帧可以与 HTTP 头放在同一次 TCP 写入。
  握手使用已检查且拥有的请求对象，帧字节不参与重新序列化。
- HTTPS 升级为 WSS 时移动原 TLS stream，不重新建立 TLS 或另开 socket。
- 未消费来源、临时 lease、异步握手与结果保持其所需的资源和代码 Owner。
  移交后，原 Http exchange 或来源释放不会关闭新的 WebSocket。
- Http server 的 Close/Shutdown 只处理仍由 Http 拥有的连接；已转移连接由 WebSocket 和 context 管理。
  context.Shutdown 继续关闭两者。
- 握手存储、预读副本及保留结果纳入模块额度。分配失败可使 context 进入 Allocation，
  仍须正常 Shutdown/析构；不得把尚未回收的原生工作当作已经卸载。

## 脚本

```vs
http::exchange exchange = incoming.exchange();
Network::upgrade_source source = exchange.prepare_upgrade();
websocket::options options = websocket::make_options();
options.set_timeout(10000);
websocket::operation accepting = websocket::accept_upgrade(source, options);
```

先加载 network，再加载 http/websocket，项目声明两种协议的依赖。
source 还提供 `protocol()`、`backend()`、`request_head()`；这些是不可变元数据。
脚本对象由 Network 的绑定服务创建，Http/WS 不伪造另一模块的宿主对象。
没有 WS 时，单独准备来源仍不转移连接；WS 调用的依赖按正常编译与注册规则检查。
HostDriven 仍需要宿主推进 PollIo，不能阻塞等待未推进的 I/O。

完整单文件样例见 [websocket_upgrade.vs](../../../../Examples/websocket_upgrade.vs)。
样例使用本机临时端口，依次演示准备、握手、二进制通信、Http 关闭和 WebSocket 关闭。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/upgrade.md) · [fr-FR](../../fr-FR/Reference/upgrade.md) · [ja-JP](../../ja-JP/Reference/upgrade.md) · [ko-KR](../../ko-KR/Reference/upgrade.md) · [zh-CN](upgrade.md) · [zh-TW](../../zh-TW/Reference/upgrade.md)
