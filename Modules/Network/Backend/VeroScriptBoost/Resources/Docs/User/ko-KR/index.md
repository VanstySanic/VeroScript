# VeroScriptBoost

`vs-boost`

## 백엔드 선택과 설치

Boost는 DNS, TCP, UDP 및 설치한 SDK에 포함된 HTTP 서버/WebSocket 기능을 제공합니다. `Boost::CreateNetworkService()`를 `Network::FContext`에, `CreateHttpServerService()`를 `Http::FServerOptions::Service`에, `CreateWebSocketService()`를 `WebSocket::FOptions::Service`에 전달합니다. 서버와 WebSocket 서비스에는 호환 컨텍스트가 필요합니다.

스크립트에서는 설치된 Network, HTTP 서버 또는 WebSocket 서비스에 `boost`를 명시적으로 선택할 수 있습니다. 선택과 상속은 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ko-KR/index.md#backend-selection)를 따르며 `boost`는 고정 기본값이 아닌 공급자 ID입니다.

## 네이티브 SDK 사용

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptNetwork.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost)
```

선택한 패키지가 요구하는 정확한 의존성과 원문 고지를 함께 설치하세요. 공유 `VeroScriptBoostRuntime`은 Boost가 소유하며 Windows에서는 호환 DLL을 실행 파일 옆이나 네이티브 모듈 배치 위치에 둡니다. Linux/macOS에서는 설치된 상대 로더 경로를 사용합니다. iOS는 같은 호스트 이미지에 정적으로 등록합니다. 이 네이티브 백엔드들은 브라우저 전송 구현을 제공하지 않습니다.

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ko-KR/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/ko-KR/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/ko-KR/index.md)

<a id="system-trust"></a>

클라이언트에서 `bUseSystemTrust`를 켜면 Boost는 OpenSSL 기본 경로에 더해 Windows ROOT 저장소 또는 macOS 시스템 신뢰 앵커를 OpenSSL 컨텍스트에 추가합니다. macOS에서는 앵커 인증서를 가져오며 검증은 OpenSSL 전송 계층이 수행합니다. 명시적인 추가 루트는 `TrustPem`으로 제공하고 제공한 루트만 쓰려면 시스템 신뢰를 끕니다. 신뢰 저장소 가용성과 배포는 실제 대상에서 확인하세요.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
