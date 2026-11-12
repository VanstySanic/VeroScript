# WebSocket 메시지와 제어

`Send(FMessage)` 는 전체 메시지와 bFinal=true 를 요구합니다. SendFragment 는 첫 조각의 텍스트/바이너리 형식을 유지하고 마지막에서 bFinal 을 설정합니다. 진행 중 다른 전체 메시지는 State 지만 Ping/close 는 가능합니다. 대기 조각 취소는 상태를 유지하고 활성 쓰기 취소는 연결을 닫습니다. UTF-8 검사는 전체 메시지에 걸치므로 문자가 조각 경계를 넘을 수 있습니다. 전체 MaxMessageBytes 가 적용되고 거부된 조각은 완료 상태를 소비하지 않습니다.

기본 Receive 는 전체 메시지입니다. bReceiveFragments 의 유한 바이트 조각은 bFirst/bFinal 을 가지지만 프레임/UTF-8 경계를 보장하지 않습니다. 두 플래그가 모두 참이어야 전체 메시지이며 마지막 조각만 JSON 으로 파싱하지 않습니다. 불변 결과를 보유하는 동안 할당량도 유지됩니다.

Ping 은 최대 125 바이트이며 직렬 쓰기 큐를 사용하고 Pong 은 자동입니다. TryTakeControl 은 비차단이며 없으면 출력을 보존합니다. 제어 큐를 비우지 않아 초과하면 Limit 로 닫힙니다. 완성된 메시지를 읽은 뒤 후속 receive 는 Limit, 이미 취소된 receive 는 Cancelled 입니다. IdleTimeout 은 기본 60 초, bKeepAlivePings=true 이며 nullopt 로 해제합니다. 개별 작업 총기한과는 별개입니다.

하위 프로토콜은 클라이언트 제안/서버 선호 순서이며 대소문자를 구분하는 비어 있지 않은 HTTP token 입니다. 중복 없이 최대 128 개/16 KiB 이고 서버는 첫 공통 선호를 고릅니다. bRequireSubprotocol 은 기본 false, true 이면 설정/협상이 필요합니다. 클라이언트는 중복/다중/미제안 선택을 거부합니다. 결과는 불변이며 미리 읽은 바이트를 보존하고 선호 설정 시 64 KiB 를 예약합니다. Close 는 타이머를 중지합니다.

스크립트는 options 의 조각/압축/제한/기한/TLS, send_text_fragment/send_binary_fragment/ping/try_control 을 제공합니다. receive 의 is_text/is_first/is_final/data, 제어의 has_value/is_ping/payload 를 확인합니다. 이벤트가 없으면 빈 payload 와 has_value=false 입니다. add_subprotocol/clear_subprotocols/set_require_subprotocol 및 connection.subprotocol 로 협상을 다룹니다.

## 기본값

| API | 값 |
| --- | --- |
| `FOptions::MaxMessageBytes` | `16 MiB` |
| `MaxQueuedBytes / MaxQueuedMessages` | `32 MiB / 128` |
| `Close(Code)` | `1000` |

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/websocket.md) · [fr-FR](../../fr-FR/Reference/websocket.md) · [ja-JP](../../ja-JP/Reference/websocket.md) · [ko-KR](websocket.md) · [zh-CN](../../zh-CN/Reference/websocket.md) · [zh-TW](../../zh-TW/Reference/websocket.md)
