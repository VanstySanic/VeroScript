# TLS 設定

透過 `TlsAvailable(context)` 查詢所選情境。HTTPS 伺服器與 WSS 使用選定傳輸服務；curl HTTPS 使用獨立的 TLS 實作。關閉 Boost 伺服器 TLS 不會關閉 curl HTTPS。

FTlsOptions 擁有 PEM 鏈/私鑰/密碼/trust，合計最多 4 MiB；宿主明確讀取，不按 cwd 查找。服務端重用 TLS context，FServerOptions::Tls 控制 HTTPS、WebSocket::FOptions::Tls 控制 WSS。客戶端 wss 與 TrustPem/bUseSystemTrust，SNI/憑證身分按目標主機，IP 按 IP 驗證。握手期限與 context close 生效。

make_tls_options 提供 set_trust_pem/use_system_trust/set_certificate_pem/set_private_key_pem/set_private_key_password；HTTP/WS listen_tls、WS connect_tls。TLS 程式庫與提供者須匹配目標，平台能力以實際原生部署為準。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/tls.md) · [fr-FR](../../fr-FR/Reference/tls.md) · [ja-JP](../../ja-JP/Reference/tls.md) · [ko-KR](../../ko-KR/Reference/tls.md) · [zh-CN](../../zh-CN/Reference/tls.md) · [zh-TW](tls.md)
