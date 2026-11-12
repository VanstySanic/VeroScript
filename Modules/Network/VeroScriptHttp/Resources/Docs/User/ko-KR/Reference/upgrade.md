# HTTP 에서 WebSocket 으로 전환

선택적 브리지를 통해 Http와 WebSocket은 Network에만 의존할 수 있으며 독립 WebSocket에는 Http가 필요 없습니다. `exchange.PrepareUpgrade`는 exchange를 보유하는 소유된 `Network::FUpgradeSource`를 반환하고 요청 헤더, 프로토콜, 백엔드, 계약 및 세대의 불변 메타데이터를 유지합니다. 이 작업은 연결을 넘기거나 101을 보내지 않습니다. `AcceptUpgrade(context, source, options)`는 일반 연결 작업을 반환합니다. 컨텍스트와 백엔드가 일치해야 하고 이전은 한 번뿐입니다. 제출 시 시작하는 기한은 검사, 대기 및 핸드셰이크를 포함하며 준비만으로 HTTP 응답 기한이 늘어나지 않습니다.

HTTP/1.1, 본문 없음, Transfer-Encoding 없음, 0 아닌 Content-Length 없음, 공유 소유 원본 exchange 가 필요합니다. 이전 전에 옵션/context/계약, 최대 64 KiB 헤더, GET/Host/Upgrade/Connection/version 13/16 바이트 key/하위 프로토콜을 검사합니다. 요청 스트림은 이미 끝나야 하며 응답 중/완료/옛 세대/닫힌 exchange 는 거부합니다. 사전 검사 실패나 이전 전 취소는 일반 HTTP 응답이 가능하지만, 이전 뒤 실패는 연결을 닫고 HTTP 복원이나 두 번째 응답은 불가합니다. 취소가 이전/101 과 경쟁하므로 IsConsumed 또는 consumed 를 확인합니다.

같은 실행기와 기존 연결 슬롯을 사용하고 미리 읽은 프레임을 보존하며 TLS 스트림을 새 연결 없이 넘깁니다. 소스/lease/handshake/결과가 코드와 자원을 유지합니다. 이전 exchange 해제는 새 WebSocket 을 닫지 않습니다. Http Close/Shutdown 은 넘긴 연결을 소유하지 않지만 context Shutdown 은 둘 다 닫습니다. Allocation 으로 context 가 닫혀도 정상 정리는 필요합니다. 스크립트 prepare_upgrade/accept_upgrade 에는 network/http/websocket 의존 가시성이 필요하며 메타데이터는 불변이고 HostDriven I/O 를 계속 진행해야 합니다.

## 예제

```VeroScript
http::exchange exchange = incoming.exchange();
Network::upgrade_source source = exchange.prepare_upgrade();
websocket::options options = websocket::make_options();
options.set_timeout(10000);
websocket::operation accepting = websocket::accept_upgrade(source, options);
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/upgrade.md) · [fr-FR](../../fr-FR/Reference/upgrade.md) · [ja-JP](../../ja-JP/Reference/upgrade.md) · [ko-KR](upgrade.md) · [zh-CN](../../zh-CN/Reference/upgrade.md) · [zh-TW](../../zh-TW/Reference/upgrade.md)
