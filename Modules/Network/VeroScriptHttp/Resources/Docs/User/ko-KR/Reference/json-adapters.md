# JSON 어댑터

선택적 C++ API 는 VeroScriptHttp COMPONENTS Json, VeroScript::HttpJson 및 VeroScriptHttpJson.h 를 사용합니다. 기본 모듈에 JSON 의존성을 강제하거나 별도 스크립트 메서드를 추가하지 않습니다.

TrySetJsonBody 는 완전히 인코딩한 뒤 본문과 Content-Type 을 원자적으로 교체합니다. 성공 시 이전 upload/factory/multipart/framing 을 지우고 실패 시 요청을 보존합니다. TryParseJsonBody 는 완전한 버퍼 응답과 기본 application/json 또는 +json 형식을 요구합니다. 대소문자/매개변수를 허용하고 HTTP 상태만으로 파싱 성공을 판단하지 않습니다. FJsonBodyError.Http 는 본문/미디어 조건, .Json 은 JSON 오류입니다. 실패 시 출력/상태/헤더/본문을 보존하며 초기 스트림 헤더만으로는 파싱할 수 없습니다. TryParseJsonEvent 는 SSE data 만 읽고 형식/ID 는 보존합니다. JSON 정밀도, Unicode, 중복 키, 제한과 작업 검사를 유지합니다.

## 예제

```cmake
find_package(VeroScriptHttp 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::HttpJson)
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
