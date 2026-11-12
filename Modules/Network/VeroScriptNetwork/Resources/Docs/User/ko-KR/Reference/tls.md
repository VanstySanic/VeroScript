# TLS 구성

선택한 컨텍스트를 `TlsAvailable(context)`로 조회합니다. HTTPS 서버와 WSS는 선택한 전송 서비스를 사용하며 curl HTTPS는 자체 TLS 구현을 사용합니다. Boost 서버 TLS를 비활성화해도 curl HTTPS는 비활성화되지 않습니다.

FTlsOptions 는 PEM chain/key/password/trust 를 소유하며 합계 최대 4 MiB 입니다. 호스트가 명시적으로 읽고 cwd 탐색은 없으며 서버 TLS context 는 재사용합니다. FServerOptions::Tls 는 HTTPS, WebSocket::FOptions::Tls 는 WSS 입니다. 클라이언트는 wss 와 TrustPem/bUseSystemTrust, SNI/인증은 목표 호스트/IP 기준입니다. handshake 기한과 context close 가 적용됩니다.

make_tls_options 의 set_trust_pem/use_system_trust/set_certificate_pem/set_private_key_pem/set_private_key_password, HTTP/WS listen_tls, WS connect_tls 를 사용합니다. 라이브러리/공급자를 대상과 일치시키고 실제 플랫폼 배치를 검증합니다.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/tls.md) · [fr-FR](../../fr-FR/Reference/tls.md) · [ja-JP](../../ja-JP/Reference/tls.md) · [ko-KR](tls.md) · [zh-CN](../../zh-CN/Reference/tls.md) · [zh-TW](../../zh-TW/Reference/tls.md)
