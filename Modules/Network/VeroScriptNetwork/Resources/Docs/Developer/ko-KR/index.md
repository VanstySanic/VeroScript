# 네트워크: 소스 구성

Network는 불투명 컨텍스트, 작업, 할당량과 공통 스트림/파일 계약을, Http/WebSocket은 프로토콜 인터페이스와 바인딩을 소유합니다. Boost는 Asio/Beast, OpenSSL 통합과 BoostRuntime을, Curl은 HTTP 클라이언트와 curl 소스를 소유합니다. 서비스·기반 식별자, 생성 이미지의 수명과 반환 객체의 제어 블록을 모듈 해제 시에도 보존해야 합니다.

수동 레지스트리 호스트는 프런트엔드 등록에만 FBackendCatalog::Binding()을 전달하고 모든 공급자를 등록한 뒤 Update를 호출합니다. 공급자 등록은 이 카탈로그를 보관하면 안 됩니다. 패키지 관리자는 카탈로그 구성과 프로젝트 서비스 선택을 수행합니다. 계약, 기반 또는 일치하는 Core 식별자가 바뀌면 참여 이미지를 다시 빌드합니다.

`Public` · `Internal` （버전이 명시된 공급자 확장 계약） · `Private` · `Resources/Docs` · `Resources/Examples` · `Resources/Localization` · `Resources/Tests` · `Resources/Tools/CMake`

`VeroScriptNetwork/Internal` → `VeroScript::NetworkProvider` （불투명 context/operation, 할당량, 스트림 및 업그레이드 계약）。

`Modules/Network/Backend/VeroScriptBoost/Private/Runtime` → `VeroScriptBoostRuntime`; `Modules/Network/Backend/VeroScriptCurl/Private` → HTTP 클라이언트. 공통 프런트엔드/공급자 빌드 도우미는 `VeroScriptNetwork/Resources/Tools/CMake`에 속합니다. Network, Http, WebSocket의 패키지 구성 템플릿은 각 모듈의 `Resources/Tools/CMake`에 두고 범주 루트는 CMake 대상만 조합합니다.

SDK 는 Public 과 버전이 명시된 불투명 계약을 include/VeroScript*/Backend 에 설치합니다. 구체 context/operation 구조는 비공개입니다. 다른 모듈의 Private 을 참조하지 않습니다. 배포 전에 일치 Core 로 provider 와 바인딩을 다시 빌드하고 실제 플랫폼 결과로 검증합니다.

## 빌드와 네이티브 모듈 등록

루트 빌드의 `VEROSCRIPT_BUILD_NETWORK`, `VEROSCRIPT_BUILD_HTTP`, `VEROSCRIPT_BUILD_WEBSOCKET` 기본값은 OFF입니다. 네트워크 옵션을 선택하면 Boost의 기본값은 ON이 되며 Curl은 HTTP 선택을 따릅니다. `VEROSCRIPT_BUILD_BOOST`와 `VEROSCRIPT_BUILD_CURL`로 공급자를 선택합니다. `Modules/Network` 단독 빌드는 Boost, HTTP, WebSocket을 기본으로 활성화합니다. `VEROSCRIPT_NETWORK_BUILD_BINDINGS`는 Core 대상이 이미 있을 때만 기본으로 ON입니다. 그 외에는 정확히 일치하는 설치된 Core SDK와 함께 명시적으로 활성화합니다. Curl의 `VEROSCRIPT_CURL_USE_SYSTEM_LIBRARY` 기본값은 OFF이며 활성화 시 curl 8.13 이상이 필요합니다. 번들 정적 라이브러리는 PIC를 사용하고 Linux 번들 구성에는 OpenSSL 개발 파일이 필요합니다.

Network를 프로토콜보다 먼저 등록하고 가시적인 의존성을 선언합니다. 정적 호스트는 각 모듈의 `QueryModule` / `GetImplementationIdentity`를, 동적 로더는 `veroscript_extension_query`를 사용합니다. 정적 `ContentIdentity`는 구현을 식별합니다. 공급자에 별도 Core를 포함하지 않습니다. BoostRuntime과 프런트엔드 기반은 참여 이미지 간에 일치해야 하며 비공개 기호와 백엔드 빌드 설정을 프런트엔드 SDK에 노출하지 않습니다.

<a id="network-execution-assembly"></a>

Core는 실행 계약을 제공하며 공식 바이트코드 VM은 별도 백엔드입니다. 바이트코드 실행을 선택하는 호스트는 `LoadVmBackend` / `SetVmBackendFactory`로 호환 팩터리를 명시적으로 설정하고, 컴파일 중 스크립트 확장을 실행한다면 `FCompileOptions::ScriptCompileExtensions.VmBackendFactory`에도 설정해야 합니다. 독립 바인딩 테스트 빌드에는 `-DVEROSCRIPT_TEST_VM_LIBRARY=<VM 라이브러리 절대 경로>`가 필요합니다. 전체 Core SDK의 `bin/Vm`에 플랫폼별 라이브러리가 포함됩니다. `BUILD_TESTING=OFF`로 테스트 없이 바인딩을 빌드할 수 있으며 데이터 계층만 빌드할 때는 VM이 필요 없습니다.

백엔드 없는 SDK, 기능 활성화/비활성화, 누락·불일치, 명시적 선택, 동적/정적 수명, 보존 결과, 재배치한 SDK와 정확한 의존성을 따로 검증하세요. 실행이 허용될 때 네이티브 프로토콜과 각 플랫폼을 확인합니다. 정적 문서 검사는 런타임 승인이 아닙니다.

<a id="cross-image-results"></a>

연산 결과는 생산 이미지의 `CreateTypedResult`/`OwnResult`에서 꺼내며 `FOwnedResult`, 컨텍스트와 페이로드 소유권을 유지합니다. `FOperation::Result<T>()`는 형식 이름·크기·정렬을 비교하고 이미지 간 `std::any_cast` RTTI 주소 일치에 의존하지 않습니다. 일치하는 C++ ABI/foundation 빌드는 여전히 필요하며 이 경계는 이식 가능한 C ABI가 아닙니다. 헤더/결과 저장 변경 뒤 소비자와 공급자를 함께 빌드하세요. 종료 시 콜백을 한 번만 해제하도록 swap 추출을 유지합니다.

POSIX 비덮어쓰기 원자 출력은 가능하면 `renameat2(RENAME_NOREPLACE)`를 쓰고 미지원 시스템 호출/파일시스템에서만 하드 링크로 대체합니다. 명시적 실패와 임시 파일 정리를 유지하세요.

<a id="provider-facade-ownership"></a>

제공자 facade wrapping은 소유 불투명 context를 받고 생성 이미지의 수명 anchor를 보존합니다. HTTP frontend가 형식화된 exchange factory를 제공하여 `shared_from_this()`와 HTTP upgrade가 같은 제어 블록을 사용하게 합니다. 제공자 할당 제어 블록이 context를 멤버로 보관하기만 해서는 안전한 unload에 충분하지 않습니다. 반환 객체와 보관 결과는 공통 factory/소유권 helper를 사용해야 합니다. catalog는 환경 참조 순환을 막기 위해 factory를 약한 참조로 캐시하며 제공자 등록에서 catalog를 보관하지 않습니다.

## 현지화 리소스 스테이징

소스 도우미 `Resources/Tools/CMake/VeroScriptLocalization.cmake`는 `network`, `http`, `websocket`, `network-all` 및 독립적인 `json` 프로필을 선택합니다. HTTP/WebSocket 프로필은 Network를 포함하고 `network-all`은 네트워크 세 모듈을 합칩니다. 이는 빌드/배포 입력이며 런타임 SDK API가 아닙니다. 스테이징은 대상 프로필을 교체하고 이전의 불필요한 항목을 삭제합니다. 프로필을 순차 중첩하지 말고 필요한 전체 리소스 루트를 먼저 선택하세요. 사용자의 리소스 등록 방법은 User 진단 참조에서 설명합니다.

<!-- docs-navigation -->

---

[User](../../User/ko-KR/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
