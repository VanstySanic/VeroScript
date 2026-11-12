# 진단과 현지화

분기는 FError::Code/Id()와 error.message_id()로 판단하고 원문 메시지를 프로토콜로 사용하지 않습니다. Resources/Localization/<locale>/json.json은 여섯 언어를 제공합니다. 범주, 경로 표시와 바인딩 오류는 번역하고 백엔드/사용자 변환기 상세는 원문을 보존합니다.

FScriptError.MessageId/MessageArgs는 호스트 진단 서비스에 전달합니다. 일반 JSON 오류 인수는 원문 상세와 인스턴스 JSON Pointer(루트는 빈 문자열) 두 개, json.mapping.declaration은 상세 하나입니다. 언어/카탈로그 누락 시 기준 영어로 돌아가고 범주/경로는 바꾸지 않습니다. 표시 메타데이터 할당 실패가 원래 오류를 덮어쓰지 않습니다.

예외를 던지지 않는 `json::error`도 같은 인수 규칙의 `message_id`, `message`, `instance_pointer`를 제공합니다. 독립 C++ SDK는 Core를 링크하지 않고 전역 언어 상태도 보유하지 않습니다. 설치한 `share/VeroScriptJson/Resources/Localization`을 호스트 리소스 해석기에 등록하며, 다른 모듈 카탈로그와 합칠 때도 `json.json`을 보존하세요.

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
