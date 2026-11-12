# TLS 設定

選択したコンテキストを `TlsAvailable(context)` で照会します。HTTPS サーバーと WSS は選択した通信サービスを使い、curl の HTTPS は独立した TLS 実装を使います。Boost のサーバー TLS を無効にしても curl の HTTPS は無効になりません。

FTlsOptions は PEM chain/key/password/trust を所有し合計 4 MiB まで。host が明示読込、cwd 探索なし、server TLS context は再利用します。FServerOptions::Tls が HTTPS、WebSocket::FOptions::Tls が WSS。client は wss と TrustPem/bUseSystemTrust、SNI/検証は target host、IP は IP identity です。handshake 期限/context close が効きます。

make_tls_options の set_trust_pem/use_system_trust/set_certificate_pem/set_private_key_pem/set_private_key_password、HTTP/WS listen_tls、WS connect_tls を使います。提供 library/provider は target に一致させ、実 platform の native 配置を確認します。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/tls.md) · [fr-FR](../../fr-FR/Reference/tls.md) · [ja-JP](tls.md) · [ko-KR](../../ko-KR/Reference/tls.md) · [zh-CN](../../zh-CN/Reference/tls.md) · [zh-TW](../../zh-TW/Reference/tls.md)
