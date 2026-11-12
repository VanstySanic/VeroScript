# TLS configuration

TLS availability is queried on the selected context: `TlsAvailable(context)`. HTTPS servers and WSS use the selected transport service; curl HTTPS follows its own TLS implementation. Disabling Boost server TLS does not disable curl HTTPS.

FTlsOptions owns PEM certificate chain/private key/optional password/trust; their combined configuration text is at most 4 MiB. Hosts explicitly read resources/files; no cwd lookup is performed. Server TLS contexts reuse configuration. FServerOptions::Tls enables HTTPS, WebSocket::FOptions::Tls WSS. Clients use wss URL, optional TrustPem and bUseSystemTrust. Peer identity/SNI follows the target hostname (IP identities are checked as IP); handshake deadlines and context close apply.

Script make_tls_options supplies set_trust_pem/use_system_trust/set_certificate_pem/set_private_key_pem/set_private_key_password. HTTP and WebSocket expose listen_tls(context,address,port,options); WebSocket adds connect_tls(context,url,options,timeout). Providers and supplied TLS libraries must match the target; platform availability requires its own native deployment.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](tls.md) · [fr-FR](../../fr-FR/Reference/tls.md) · [ja-JP](../../ja-JP/Reference/tls.md) · [ko-KR](../../ko-KR/Reference/tls.md) · [zh-CN](../../zh-CN/Reference/tls.md) · [zh-TW](../../zh-TW/Reference/tls.md)
