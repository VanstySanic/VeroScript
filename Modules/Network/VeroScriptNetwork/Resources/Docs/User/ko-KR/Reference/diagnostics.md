# 진단과 현지화

network.json/http.json/websocket.json 은 각 모듈의 여섯 Resources/Localization 에 있습니다. FError::Id 는 안정/무할당이며 성공은 빈 값, 미지 enum 은 network.unknown 입니다. 전송 오류 도메인을 공유하며 operation.message_id/error_code/error 는 ID/코드/원문 상세입니다. pending 도 빈 ID 일 수 있으므로 완료/성공을 따로 확인합니다. 스트림/SSE/multipart/history/reconnect 도 같습니다.

인수 0 은 원문 상세입니다. 범주만 번역하고 백엔드 상세는 보존하며 번역/OS 문구로 분기하지 않습니다. 동기 바인딩은 network.binding/http.binding/websocket.binding 과 `FScriptError.MessageId/MessageArgs` 를 사용하고 표시 할당 실패에도 첫 오류를 유지합니다. 독립 SDK 에는 Core/전역 언어가 없습니다.

설치한 각 모듈의 `share/VeroScript<Module>/Resources/Localization`을 호스트 리소스 해석기에 등록하세요. Http와 WebSocket에는 의존하는 Network의 `network.json`도 필요하며 이를 중복 포함하지 않습니다. 여러 모듈을 사용하는 호스트는 각 디렉터리를 따로 등록하거나 필요한 모든 구성 요소 파일을 보존한 통합 카탈로그 루트를 배포할 수 있습니다.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
