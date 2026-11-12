# 原生 TLS

通过 `TlsAvailable(context)` 查询所选上下文。HTTPS 服务端与 WSS 使用所选传输服务；curl HTTPS 使用独立的 TLS 实现。关闭 Boost 服务端 TLS 不会关闭 curl HTTPS。

通过 `Network::TlsAvailable(context)` / `Network::tls_available()` 查询实际 TLS 能力。关闭 Network TLS 不关闭 curl 自身的 HTTPS。

## 配置与调用

`FTlsOptions` 接收拥有的 PEM 文本：certificate chain、private key、可选 key password 和客户端 trust。
证书、密钥、trust 和密码的配置文本合计至多 4 MiB；不自动按进程 cwd 读取文件。
资源／文件读取由宿主显式完成；服务端建立配置后复用 TLS context，不为每条连接重新复制密钥文本。

- HTTPS 服务端：设置 `FServerOptions::Tls.bEnabled`，提供证书链和对应私钥后调用 `FServer::Listen`。
- WSS 服务端：同样通过 `WebSocket::FOptions::Tls` 配置。
- WSS 客户端：使用 `wss` URL；`Tls.TrustPem` 可提供当前连接所需的 CA，`bUseSystemTrust` 控制系统信任来源。
- 客户端按 URL 主机校验对端，DNS 主机使用 SNI；IP 地址使用对应证书身份。
- 握手受期限管理；关闭 context 会取消握手和后续 I/O。

脚本 `Network::make_tls_options()` 返回值语义配置，提供 `set_trust_pem`、`use_system_trust`、
`set_certificate_pem`、`set_private_key_pem` 和 `set_private_key_password`。
HTTP 与 WebSocket 分别提供 `listen_tls(context, address, port, options)`；
WebSocket 客户端另有 `connect_tls(context, url, options, timeout)`。

HTTP 升级与 WebSocket 代理由所属协议模块提供；完整平台矩阵仍须实际验收，不能由本机结果替代。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/tls.md) · [fr-FR](../../fr-FR/Reference/tls.md) · [ja-JP](../../ja-JP/Reference/tls.md) · [ko-KR](../../ko-KR/Reference/tls.md) · [zh-CN](tls.md) · [zh-TW](../../zh-TW/Reference/tls.md)
