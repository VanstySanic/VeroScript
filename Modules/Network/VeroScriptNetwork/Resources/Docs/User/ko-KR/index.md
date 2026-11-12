# Network

사용할 API의 프런트엔드와 선택한 네이티브 백엔드를 설치하세요. `vs-boost`는 `network`와 활성화된 HTTP 서버/WebSocket 서비스를, `vs-curl`은 `http-client`를 제공합니다. 프런트엔드만 설치하면 전송 구현은 제공되지 않습니다. 서비스가 없거나 계약이 맞지 않으면 오류를 반환합니다.

<a id="backend-selection"></a>

스크립트에서는 `Network::create_context_with_backend("boost", false)`로 실행 기반을 명시적으로 선택합니다. HTTP 클라이언트／서버, SSE, WebSocket 옵션의 `set_backend`는 환경 선택을 재정의합니다. 빈 요청은 `services.<service>.backend`를 상속하며 설정이 없으면 `auto`가 활성 후보를 백엔드 ID 순으로 시도합니다. 명시적인 `auto`는 구성된 선택을 재정의하고, `none`이나 구성에 명시적으로 저장한 빈 값은 서비스를 비활성화합니다. 필수 서비스가 없으면 오류가 발생하며 실행 중 오류로 다른 백엔드에서 작업을 재실행하지 않습니다. `boost`와 `curl`은 제공자 ID이며 고정 기본값이 아닙니다. C++ 소비자는 형식화된 서비스 인스턴스를 전달합니다. 스크립트 선택 토큰은 C++ 옵션의 인스턴스 일치 계약을 대체하지 않습니다.

`vs-network` · `VeroScript::Network`

- [완료 알림과 프로그램 수명](Reference/completion.md)
- [진단과 현지화](Reference/diagnostics.md)
- [파일과 원자적 출력](Reference/files.md)
- [네트워크 참조](Reference/README.md)
- [유한 바이트 스트림](Reference/streaming.md)
- [TLS 구성](Reference/tls.md)

[Boost](../../../../../Backend/VeroScriptBoost/Resources/Docs/User/ko-KR/index.md) · [Curl](../../../../../Backend/VeroScriptCurl/Resources/Docs/User/ko-KR/index.md)

<!-- docs-navigation -->

---

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
