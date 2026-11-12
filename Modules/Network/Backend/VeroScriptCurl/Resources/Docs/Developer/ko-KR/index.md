# VeroScriptCurl

Curl은 `Private/HttpClient.cpp`의 HTTP 클라이언트, curl 의존성 설정과 원본 고지를 관리합니다. 공급자는 Http 계약을 구현하며 Boost 전송이나 WebSocket 서비스는 관리하지 않습니다.

공통 컨텍스트/작업 소유권, 레지스트리 구성, SDK 경계와 빌드 옵션은 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/ko-KR/index.md)에서 관리합니다.

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [사용자](../../User/ko-KR/index.md)

<a id="provider-registration"></a>

`curl`은 `http-client` / `veroscript.http.client` / `0.1.1`을 `Http::Backend::IClientService*` 팩터리로 등록합니다. 팩터리는 상태가 없고 등록은 I/O를 시작하지 않으며 알 수 없는 설정을 거부합니다. 동적 검색은 `veroscript_extension_query`, 정적 등록은 `veroscript_curl_query` / `Curl::QueryModule`입니다. Curl은 비소유 불투명 소켓 준비/타이머 hook을 사용하며 Boost 헤더나 라이브러리에 의존하지 않습니다. HTTPS는 curl 자체 TLS 설정을 따릅니다. 번들 curl 패치, 정적 의존 배포와 Unix 시스템 요구사항을 이 모듈에서 관리하고 실제 빌드의 누락/불일치 서비스, lease와 재배치 SDK/패키지 소비자를 검증하세요.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
