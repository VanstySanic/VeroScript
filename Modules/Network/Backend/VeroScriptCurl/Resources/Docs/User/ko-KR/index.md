# VeroScriptCurl

`vs-curl`

## 백엔드 선택과 설치

Curl은 HTTP 클라이언트, 연결 풀, 쿠키, 리디렉션, 재시도와 스트리밍을 제공합니다. `Curl::CreateHttpClientService()`를 `Http::FClient` 또는 `Http::FSseClientOptions::Service`에 전달합니다. 컨텍스트는 소켓을 소유하지 않는 감시 기능과 타이머를 제공해야 하며 Boost가 이를 지원합니다. Curl은 실행 기반을 자동으로 선택하거나 설치하지 않습니다. HTTPS는 서버 TLS와 별개인 curl TLS 구현을 사용합니다.

스크립트에서는 HTTP 클라이언트나 SSE에 `curl`을 명시적으로 선택하고 호환되는 Network 컨텍스트를 별도로 선택합니다. 선택과 상속은 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ko-KR/index.md#backend-selection)를 따르며 `curl`은 고정 기본값이 아닌 공급자 ID입니다.

## 네이티브 SDK 사용

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptCurl.h>
#include <VeroScriptHttp.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
VeroScript::Http::FClient client(context, VeroScript::Curl::CreateHttpClientService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptCurl 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost VeroScript::Curl)
```

선택한 SDK와 실행기에 필요한 정확한 의존성과 원본 고지를 설치하세요. 위 예제의 Boost 실행기를 사용한다면 해당 [배포 가이드](../../../../../VeroScriptBoost/Resources/Docs/User/ko-KR/index.md)를 따르세요. 이 네이티브 공급자는 브라우저 전송을 제공하지 않습니다.

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ko-KR/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/ko-KR/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/ko-KR/index.md)

<a id="system-trust"></a>

번들 macOS curl은 OpenSSL과 Apple SecTrust를 사용하도록 구성됩니다. 명시적 `CaFile`이 없으면 네이티브 신뢰 연동을 사용하며 CA 파일을 지정하면 해당 요청에 제공한 신뢰 자료를 사용합니다. 번들 Windows curl은 Schannel을, 다른 번들 네이티브 대상은 구성된 OpenSSL 신뢰 경로를 사용합니다. 시스템 libcurl SDK는 그 라이브러리의 TLS/신뢰 구성을 따릅니다. Boost 서버/WSS TLS와는 독립적입니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
