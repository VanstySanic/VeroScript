# VeroScriptBoost

Boost는 Asio/Beast, OpenSSL 통합과 `Private/Runtime` (`VeroScriptBoostRuntime`)을 관리합니다. 구체적인 구현과 타사 소스 목록은 이 모듈에 두며 프런트엔드 SDK는 해당 형식에 의존하지 않습니다.

공통 컨텍스트/작업 소유권, 레지스트리 구성, SDK 경계와 빌드 옵션은 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/ko-KR/index.md)에서 관리합니다.

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [사용자](../../User/ko-KR/index.md)

<a id="provider-registration"></a>

`boost`는 `network`(`veroscript.network`)와 활성 프로토콜에 따라 `http-server`(`veroscript.http.server`), `websocket-client` / `websocket-server`(`veroscript.websocket`)를 등록합니다. 계약 버전은 `0.1.1`이며 반환 핸들은 각각 `Network::Backend::IService*`, `Http::Backend::IServerService*`, `WebSocket::Backend::IService*`입니다. 팩터리는 상태가 없고 등록 시 I/O를 시작하지 않으며 알 수 없는 설정을 거부합니다. 동적 검색은 `veroscript_extension_query`, 정적 등록은 `veroscript_boost_query` / `Boost::QueryModule`입니다. `VeroScriptBoostRuntime`은 Boost만 소유하며 frontend나 Curl은 복사본을 추가 설치하지 않습니다. 일치하는 foundation/Core identity로 프로토콜 활성/비활성 메타데이터, lease와 재배치 소비자를 검증하세요.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
